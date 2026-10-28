#include "ndsif_serial.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <new>
#include <hal/usb_serial_jtag_ll.h>
#include "dac_stream.h"
#include "ndsif.h"
#include "ClockMatch.h"
#include "NJU72341.h"
#include "SI5351.hpp"
#include "fm.h"
#include "nd.h"
#include "disp.h"

namespace {
ndsif::Protocol protocol;
ndsif::DacStream* stream = nullptr; // Allocate the cache only in NDSIF mode.
portMUX_TYPE streamMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE usbMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool busResetPending = false;
int usbTxIdleSpace = 0;
QueueHandle_t jobs = nullptr;
SemaphoreHandle_t completed = nullptr;
TaskHandle_t playbackTaskHandle = nullptr, receiveTaskHandle = nullptr;
enum Operation { Reset, Registers, Clock, Start };
struct Job {
  Operation operation;
  const uint8_t* data = nullptr;
  size_t count = 0, offset = 0;
  uint32_t value = 0;
  uint8_t id = 0, instance = 0, page = 0;
  bool result = false;
};
const ndsif::Capability capabilities[] = {
    {0x52, 0, 0, 5, ndsif::CLOCK_VARIABLE, 0},
    {0x53, 0, 1, 5, ndsif::CLOCK_VARIABLE, 0},
    {0x50, 0, 0, 5, ndsif::CLOCK_VARIABLE, 0},
    {0x30, 1, 0, 5, ndsif::CLOCK_VARIABLE, 0},
};

bool submit(Job& job) {
  Job* pointer = &job;
  // The RX task keeps the frame and this job alive until the I/O owner finishes.
  xQueueSend(jobs, &pointer, portMAX_DELAY);
  xSemaphoreTake(completed, portMAX_DELAY);
  return job.result;
}
void resetChips() { Job job; job.operation = Reset; submit(job); }
void writeRegisters(uint8_t id, uint8_t instance, uint8_t page, const uint8_t* data, size_t count) {
  const bool ym = instance == 0 && ((id == 0x52 && page == 0) || (id == 0x53 && page == 1));
  const bool psg = page == 0 && ((id == 0x50 && instance == 0) || (id == 0x30 && instance == 1));
  if ((!ym && !psg) || !count) return;
  portENTER_CRITICAL(&streamMux);
  const bool pristine = stream->pristine(), writable = stream->writable();
  portEXIT_CRITICAL(&streamMux);
  if (!writable) return;
  // Reject the entire burst if it attempts to bypass buffered DAC/2B ordering.
  if (ym && page == 0 && !pristine) {
    for (size_t i = 0; i < count; ++i)
      if (data[i * 2] == 0x2a || data[i * 2] == 0x2b) return;
  }
  Job job; job.operation = Registers; job.id = id; job.instance = instance;
  job.page = page; job.data = data; job.count = count;
  submit(job);
}
bool setClock(uint8_t id, uint8_t instance, uint32_t hz) {
  if (!((instance == 0 && (id == 0x52 || id == 0x53 || id == 0x50)) ||
        (id == 0x30 && instance == 1))) return false;
  hz = nd6clock::match(hz);
  if (!nd6clock::isSupported(hz)) return false;
  Job job; job.operation = Clock; job.id = id; job.value = hz;
  return submit(job);
}
void setOutputVolume(uint8_t attenuation) {
  // Called only by the RX task on core 0. The DAC owner on core 1 never
  // waits for I2C and the original FM/PSG registers and DAC bytes stay intact.
  nju72341.setVolumeAll(attenuation);
}
void usbBusReset(void*, esp_event_base_t, int32_t, void*) {
  portENTER_CRITICAL(&usbMux); busResetPending = true; portEXIT_CRITICAL(&usbMux);
}

size_t extension(uint8_t type, uint8_t id, const uint8_t* data, size_t size, uint8_t* out) {
  using S = ndsif::DacStream;
  if (id == 0x18 && type == 1 && size == 0) return S::capabilities(out);
  if (id == 0x19 && type == 0) {
    // Decode/validate outside the critical section. Only the bounded ring copy
    // and position update block the consumer, never CRC/COBS or chip I/O.
    S::Chunk chunk;
    const bool valid = S::decode(data, size, chunk);
    portENTER_CRITICAL(&streamMux);
    if (valid) stream->append(chunk); else stream->fail(S::Invalid);
    portEXIT_CRITICAL(&streamMux);
    return 0;
  }
  if (id == 0x1a && type == 1 && size == 5 && data[0] == S::PROFILE) {
    Job job; job.operation = Start; job.value = uint32_t(ndsif::readLE(data + 1, 4));
    return submit(job) ? 0 : SIZE_MAX;
  }
  if (id == 0x1b && type == 0) {
    const bool valid = size == 13 && data[0] == S::PROFILE;
    const uint32_t sequence = valid ? uint32_t(ndsif::readLE(data + 1, 4)) : 0;
    const uint64_t end = valid ? ndsif::readLE(data + 5, 8) : 0;
    portENTER_CRITICAL(&streamMux);
    if (valid) stream->seal(sequence, end); else stream->fail(S::Invalid);
    portEXIT_CRITICAL(&streamMux);
    return 0;
  }
  if (id == 0x1c && type == 1 && size == 1 && data[0] == S::PROFILE) {
    portENTER_CRITICAL(&streamMux);
    const size_t result = stream->status(out, esp_timer_get_time());
    portEXIT_CRITICAL(&streamMux);
    return result;
  }
  if (id == 0x1d && type == 0) {
    bool valid = size >= 13 && data[0] == S::PROFILE;
    const uint64_t tick = valid ? ndsif::readLE(data + 1, 8) : 0;
    const uint8_t target = valid ? data[9] : 0;
    const bool bytes = target == 0x50 || target == 0x30;
    if (!bytes && ((size - 12) & 1)) valid = false;
    portENTER_CRITICAL(&streamMux);
    if (valid) stream->events(tick, target, data[10], data[11], data + 12,
                             bytes ? size - 12 : (size - 12) / 2);
    else stream->fail(S::Invalid);
    portEXIT_CRITICAL(&streamMux);
    return 0;
  }
  return SIZE_MAX;
}

void emit(const ndsif::DacStream::Action& action) {
  using S = ndsif::DacStream;
  switch (action.kind) {
    case S::Dac: FM.setYM2612DAC(action.value, 0); break;
    case S::Select: FM.setYM2612(0, 0x2b, action.value, 0); break;
    case S::Register: FM.setYM2612(action.page, action.address, action.value, 0); break;
    case S::Psg: {
      const unsigned slot = action.instance == 0 ? 1 : 0;
      FM.write(action.value, action.instance == 0 ? 1 : 2, ND::freq[slot]);
      break;
    }
    default: break;
  }
}

void playbackTask(void*) {
  Job* pending = nullptr;
  while (true) {
    ndsif::DacStream::Action action;
    const uint64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&streamMux);
    const bool hasAction = stream->next(now, action);
    const bool stop = stream->takeStop();
    const bool running = stream->running();
    const uint64_t remaining = stream->remainingUS(now);
    portEXIT_CRITICAL(&streamMux);
    if (stop) FM.reset();
    if (hasAction) { emit(action); continue; }
    // Idle FM-only playback waits ON the queue, so a new BURST wakes this
    // owner immediately. Polling and then delaying one tick adds that tick
    // to every inter-core handoff and makes dense register streams fall behind.
    if (!pending) xQueueReceive(jobs, &pending, running ? 0 : 1);
    if (pending && (pending->operation != Registers || !running || remaining >= 20)) {
      Job& job = *pending;
      bool done = true;
      switch (job.operation) {
        case Reset:
          portENTER_CRITICAL(&streamMux); stream->reset(); portEXIT_CRITICAL(&streamMux);
          FM.reset(); job.result = true;
          break;
        case Start:
          portENTER_CRITICAL(&streamMux);
          job.result = stream->start(job.value, esp_timer_get_time());
          portEXIT_CRITICAL(&streamMux);
          break;
        case Registers: {
          portENTER_CRITICAL(&streamMux);
          const bool writable = stream->writable();
          portEXIT_CRITICAL(&streamMux);
          if (!writable) break;
          const bool psg = job.id == 0x50 || job.id == 0x30;
          ndsif::DacStream::Action write = {psg ? ndsif::DacStream::Psg : ndsif::DacStream::Register,
              job.page, uint8_t(psg ? 0 : job.data[job.offset * 2]),
              job.data[psg ? job.offset : job.offset * 2 + 1], job.instance};
          emit(write);
          done = ++job.offset == job.count; job.result = true;
          break;
        }
        case Clock:
          if (!running) {
            const unsigned slot = job.id == 0x50 ? 1 : 0;
            const bool changed = ND::freq[slot] != static_cast<si5351Freq_t>(job.value);
            ND::freq[slot] = static_cast<si5351Freq_t>(job.value);
            SI5351.setFreq(ND::freq[slot], slot); job.result = true;
            if (changed) ndsifModeUpdateFooter(uint32_t(ND::freq[0]), uint32_t(ND::freq[1]));
          }
          break;
        default: break;
      }
      if (done) { pending = nullptr; xSemaphoreGive(completed); }
      continue;
    }
    if (!running) {
      FM.applyPendingChannelMask();
      FM.applyPendingYM2612OutputMode();
    }
    // The idle receive above already yields for one tick and can be woken.
    // Keep the existing long-wait yield unchanged while DAC is running.
    if (running && remaining > 2000) vTaskDelay(1);
    // While DAC is active, this dedicated core owns the clock and chip bus.
    // USB parsing and UI work run on the other core.
  }
}

void serviceTx() {
  if (usbTxIdleSpace <= 0 || Serial.availableForWrite() >= usbTxIdleSpace) return;
  // Same HWCDC queued-TX recovery as ND8, on the USB ISR's core.
  portENTER_CRITICAL(&usbMux);
  if (usb_serial_jtag_ll_txfifo_writable()) {
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    usb_serial_jtag_ll_txfifo_flush();
  }
  portEXIT_CRITICAL(&usbMux);
}

void receiveTask(void*) {
  while (true) {
    portENTER_CRITICAL(&usbMux);
    const bool reset = busResetPending; busResetPending = false;
    portEXIT_CRITICAL(&usbMux);
    if (reset) {
      portENTER_CRITICAL(&streamMux); stream->fail(ndsif::DacStream::Transport); portEXIT_CRITICAL(&streamMux);
      protocol.clear(true);
      const int discard = Serial.available();
      for (int i = 0; i < discard; ++i) Serial.read();
    }
    protocol.expire(millis());
    for (unsigned work = 0; work < 512; ++work) {
      if (protocol.outputSize()) {
        if (!Serial.isConnected()) break;
        const int space = Serial.availableForWrite();
        if (space <= 0) break;
        const size_t count = protocol.outputSize() < size_t(space) ? protocol.outputSize() : size_t(space);
        protocol.consumeOutput(Serial.write(protocol.output(), count));
        if (protocol.outputSize()) break;
      }
      if (!Serial.available()) break;
      const int byte = Serial.read();
      if (byte >= 0) protocol.feed(uint8_t(byte), millis());
    }
    // RX/TX and the USB ISR share core 0. DAC never acquires a USB mutex.
    serviceTx();
    vTaskDelay(1);
  }
}
} // namespace

namespace NdsifSerial {
void beginUsb() {
  // Read only the boot mode here, before the USB ISR/buffers are installed.
  // Full configuration loading remains in NDConfig after hardware detection.
  Preferences boot;
  const bool opened = boot.begin("NanoDrive", true);
  const uint8_t mode = opened ? boot.getUChar("mode", 0) : 0;
  if (opened) boot.end();
  if (mode == MODE_NDSIF) {
    // esp_intr_alloc installs HWCDC's ISR on the calling core. Start it on
    // the RX/TX core, rather than on setup/DAC's core. The startup task uses
    // only this handle value; no pointer into setup's stack survives here.
    TaskHandle_t readyTask = nullptr;
    const BaseType_t result = xTaskCreatePinnedToCore([](void* caller) {
      const size_t rx = Serial.setRxBufferSize(8192);
      const size_t tx = Serial.setTxBufferSize(512);
      configASSERT(rx == 8192 && tx == 512);
      Serial.begin(1500000);
      Serial.setTxTimeoutMs(0);
      usbTxIdleSpace = Serial.availableForWrite();
      xTaskNotifyGive(static_cast<TaskHandle_t>(caller));
      vTaskDelete(nullptr);
    }, "ndsifUSB", 3072, xTaskGetCurrentTaskHandle(), 2, &readyTask, PRO_CPU_NUM);
    configASSERT(result == pdPASS && readyTask);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  } else {
    Serial.begin(1500000);
  }
}
void init() {
  jobs = xQueueCreate(1, sizeof(Job*));
  completed = xSemaphoreCreateBinary();
  configASSERT(jobs && completed);
  void* memory = heap_caps_malloc(sizeof(ndsif::DacStream), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  configASSERT(memory);
  stream = new (memory) ndsif::DacStream;
  stream->reset();
  protocol.configure(resetChips, nullptr, ND_FIRMWARE_VERSION, setClock, nullptr,
                     ND::volumeChip != VolumeChip::None ? setOutputVolume : nullptr);
  protocol.setRegisterWriter(writeRegisters);
  protocol.setExtension(extension);
  protocol.setDeviceInfo(ND::version == nd_v61 ? "NanoDrive 6.1" : "NanoDrive 6");
  const bool ready = protocol.setCapabilities(capabilities, sizeof(capabilities) / sizeof(capabilities[0]));
  configASSERT(ready);
  Serial.onEvent(ARDUINO_HW_CDC_BUS_RESET_EVENT, usbBusReset);
  ND::freq[0] = SI5351_7670; ND::freq[1] = SI5351_3579;
  ndsifModeUpdateFooter(uint32_t(ND::freq[0]), uint32_t(ND::freq[1]));
  FM.reset();
  nju72341.setVolumeAll(0);
}
void start() {
  // Sub-tick DAC polling can starve core 1's idle task while audio is active.
  disableCore1WDT();
  BaseType_t result = xTaskCreatePinnedToCore(playbackTask, "ndsifDAC", 6144, nullptr, 3,
                                             &playbackTaskHandle, APP_CPU_NUM);
  configASSERT(result == pdPASS && playbackTaskHandle);
  result = xTaskCreatePinnedToCore(receiveTask, "ndsifRX", 6144, nullptr, 2,
                                   &receiveTaskHandle, PRO_CPU_NUM);
  configASSERT(result == pdPASS && receiveTaskHandle);
}
} // namespace NdsifSerial
