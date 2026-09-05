#include "serialman.h"

#include <Arduino.h>

#include "NJU72341.h"
#include "SI5351.hpp"
#include "disp.h"
#include "file.h"
#include "fm.h"
#include "input.h"
#include "nd.h"

#define SERIAL_SIZE_RX 65535

namespace {
constexpr uint32_t PCM_BURST_SPREAD_US = 4;
constexpr TickType_t TRACK_MASK_POLL_INTERVAL = pdMS_TO_TICKS(20);

SemaphoreHandle_t metadataMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  return mutex;
}
String latestTrack;
bool trackPending = false;

String jsonString(const String& value) {
  String result = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    const unsigned char c = value[i];
    if (c == '"' || c == '\\') {
      result += '\\';
      result += (char)c;
    } else if (c < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned int)c);
      result += escaped;
    } else {
      result += (char)c;
    }
  }
  return result + '"';
}

void sendAppFrame(const String& json) {
  // A single write keeps our frame together; the leading newline terminates any diagnostic text.
  const String frame = "\n@ND6 " + json + "\n";
  Serial.write((const uint8_t*)frame.c_str(), frame.length());
}

void sendIdentity() {
  sendAppFrame(String("{\"event\":\"identity\",\"model\":\"ND6\",\"hardware\":") +
               jsonString(ND::versionLabel()) + ",\"firmware\":" + jsonString(ND_FIRMWARE_VERSION) +
               ",\"protocol\":1}");
}

void sendPendingTrack(bool force) {
  const auto mutex = metadataMutex();
  if (!mutex) return;
  String snapshot;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (force || trackPending) {
    snapshot = latestTrack;
    trackPending = false;
  }
  xSemaphoreGive(mutex);
  if (!snapshot.isEmpty()) sendAppFrame(snapshot);
}
}  // namespace

void SerialMan::setTrackMetadata(const String& title, const String& system, const String& composer,
                                 const String& date, const String& path, String type) {
  type.toLowerCase();
  const String json = String("{\"event\":\"track\",\"title\":") + jsonString(title) +
      ",\"system\":" + jsonString(system) + ",\"composer\":" + jsonString(composer) +
      ",\"date\":" + jsonString(date) + ",\"path\":" + jsonString(path) +
      ",\"type\":" + jsonString(type) + "}";
  const auto mutex = metadataMutex();
  if (!mutex) return;
  xSemaphoreTake(mutex, portMAX_DELAY);
  latestTrack = json;
  trackPending = true;
  xSemaphoreGive(mutex);
}

constexpr std::array<si5351Freq_t, 5> YM2612ClockOptions = {
    SI5351_7670,  // 7.670453 MHz
    SI5351_8000,  // 8 MHz
    SI5351_6000,  // 6 MHz
    SI5351_7600,  // 7.600489 MHz
    SI5351_7159,  // 7.159000 MHz
};

constexpr std::array<si5351Freq_t, 5> SN76489ClockOptions = {
    SI5351_3579,  // 3.57954545 MHz
    SI5351_4000,  // 4 MHz
    SI5351_2000,  // 2 MHz
    SI5351_1789,  // 1.789772 MHz
    SI5351_1536,  // 1.536 MHz
};

u8_t getSerial() {
  while (1) {
    if (Serial.available()) {
      return Serial.read();
    }
  }
}

u32_t getSerial32() { return getSerial() + (getSerial() << 8) + (getSerial() << 16) + (getSerial() << 24); }

// シリアル受信用タスク
void serialCheckerTask(void* param) {
  u8_t command, reg, dat;
  u32_t clock0 = SI5351_3579, clock1 = SI5351_2000;
  lcd.setCursor(5, 77);
  // lcd.printf("%02x %02x %02x", 0x53, reg, dat);

  while (1) {
    command = getSerial();
    switch (command) {
      case 0x30: {  // SN76489 chip 2
        dat = getSerial();
        FM.write(dat, 2, (si5351Freq_t)clock0);
        break;
      }
      case 0x50: {  // SN76489 chip 1
        dat = getSerial();
        FM.write(dat, 1, (si5351Freq_t)clock1);
        break;
      }
      case 0x52: {
        reg = getSerial();
        dat = getSerial();
        FM.setYM2612(0, reg, dat, 0);
        break;
      }
      case 0x53: {
        reg = getSerial();
        dat = getSerial();
        FM.setYM2612(1, reg, dat, 0);
        break;
      }
      case 0x55: {
        reg = getSerial();
        dat = getSerial();
        FM.setRegister(reg, dat, 0);
        break;
      }
      case 0x80 ... 0x8f: {
        dat = getSerial();
        FM.setYM2612DAC(dat, 0);

        // PC側がイベント時刻を管理しているため、ND6側ではPCM周波数を作らない。
        // USB CDCの受信FIFOに後続データがある場合だけ、ごく短い間隔でバーストを広げる。
        if (Serial.available() > 0) {
          ets_delay_us(PCM_BURST_SPREAD_US);
        }
        break;
      }

        // Additional Commands

      case 0xf0: {
        // クロック0の周波数設定
        clock0 = getSerial32();
        SI5351.setFreq((si5351Freq_t)clock0, 0);
        ND::freq[0] = (si5351Freq_t)clock0;
        serialModeDraw();
        break;
      }

      case 0xf1: {
        // クロック1の周波数設定
        clock1 = getSerial32();
        SI5351.setFreq((si5351Freq_t)clock1, 1);
        ND::freq[1] = (si5351Freq_t)clock1;
        serialModeDraw();
        break;
      }

      case 0x00: {
        // リセット
        FM.reset();
        break;
      }

      default:
        // lcd.setCursor(5, 77);
        lcd.printf("%02x ", command);
        // Serial.printf("%02x\n", command);
        break;
    }
  }
}

// プレイヤーモード用トラックマスク入力タスク
void trackMaskSerialTask(void* param) {
  (void)param;

  while (1) {
    while (Serial.available() > 0) {
      const int key = Serial.read();
      if (key == '?') {
        sendIdentity();
        sendPendingTrack(true);
      } else if (key == '0') {
        FM.requestResetChannelMask();
      } else if (key >= '1' && key <= '6') {
        FM.requestToggleChannelMask((u8_t)(key - '1'));
      } else if (key == ' ') {
        requestPlayHoldRelease();
      } else {
        switch (key) {
          case 'w':
          case 'W':
            ndFile.requestDirPlayIfIdle(-1);
            break;
          case 's':
          case 'S':
            ndFile.requestDirPlayIfIdle(1);
            break;
          case 'a':
          case 'A':
            ndFile.requestFilePlayIfIdle(-1);
            break;
          case 'd':
          case 'D':
            ndFile.requestFilePlayIfIdle(1);
            break;
          default:
            break;
        }
      }
    }
    if (Serial) sendPendingTrack(false);
    vTaskDelay(TRACK_MASK_POLL_INTERVAL);
  }
}

// コンストラクタ
SerialMan::SerialMan() {}

// シリアル受信初期化
void SerialMan::init() {
  uint8_t data = 1;

  Serial.setRxBufferSize(SERIAL_SIZE_RX);  // シリアルバッファサイズ設定

  // 画面描画
  ND::freq[0] = YM2612ClockOptions[YM2612Clock];
  ND::freq[1] = SN76489ClockOptions[SN76489Clock];
  serialModeDraw();

  // 音出す
  SI5351.setFreq(SI5351_7670, 0);
  SI5351.setFreq(SI5351_3579, 1);
  FM.reset();
  nju72341.setVolumeAll(0);
}

// シリアル受信用タスク開始
void SerialMan::startSerialTask() {
  xTaskCreateUniversal(serialCheckerTask, "serialTask", 10000, NULL, 1, NULL, APP_CPU_NUM);
}

void SerialMan::startTrackMaskTask() {
  xTaskCreatePinnedToCore(trackMaskSerialTask, "trackMaskSerial", 4096, NULL, tskIDLE_PRIORITY, NULL, PRO_CPU_NUM);
}

// YM2612クロック変更
void SerialMan::changeYM2612Clock() {
  if (YM2612Clock == YM2612ClockOptions.size() - 1) {
    YM2612Clock = 0;
  } else {
    YM2612Clock++;
  }

  ND::freq[0] = YM2612ClockOptions[YM2612Clock];
  SI5351.setFreq(YM2612ClockOptions[YM2612Clock], 0);
  serialModeDraw();
}

// SN76489クロック変更
void SerialMan::changeSN76489Clock() {
  if (SN76489Clock == SN76489ClockOptions.size() - 1) {
    SN76489Clock = 0;
  } else {
    SN76489Clock++;
  }
  ND::freq[1] = SN76489ClockOptions[SN76489Clock];
  SI5351.setFreq(SN76489ClockOptions[SN76489Clock], 1);
  serialModeDraw();
}

// シリアルマネージャのインスタンス
SerialMan serialMan = SerialMan();
