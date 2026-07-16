#include "input.h"

#include <Adafruit_TCA8418.h>
#include <Arduino.h>

#include "NJU72341.h"
#include "disp.h"
#include "file.h"
#include "nd.h"
#include "serialman.h"
#include "vgm.h"

namespace {
Adafruit_TCA8418 keypad;
TaskHandle_t tcaTaskHandle = nullptr;
TaskHandle_t adcTaskHandle = nullptr;
TaskHandle_t eventTaskHandle = nullptr;
TaskHandle_t inputHandlerTaskHandle = nullptr;
TimerHandle_t keyRepeatTimer = nullptr;
QueueHandle_t inputEventQueue = nullptr;
bool holdCountdownActive = false;
int8_t holdCountdownSec = 0;
uint32_t holdCountdownNextTick = 0;
int lastPauseConfig = -1;

enum class KeyRepeatState : uint8_t { Idle, Waiting, Repeating };

volatile KeyRepeatState keyRepeatState = KeyRepeatState::Idle;
volatile Button activeButton = btnNONE;

constexpr int ADC_INPUT_PIN = 1;
constexpr int ADC_REPEAT_DELAY = 300;
constexpr int VAL_0 = 0;        // 0 - 50
constexpr int VAL_1 = 530;      // 530 前後    460 - 510
constexpr int VAL_2 = 1301;     // 1301 前後  1240 - 1290
constexpr int VAL_3 = 1980;     // 1980 前後  1900 - 1980
constexpr int VAL_4 = 2900;     // 2905 前後  2860 - 2910
constexpr int VAL_NONE = 4095;  // 4095

Button adcLastButton = btnNONE;
uint32_t adcButtonRepeatStarted = 0;

Button matrixKeyToButton(int key) {
  switch (key) {
    case 0:
      return btnUP;
    case 10:
      return btnDOWN;
    case 20:
      return btnLEFT;
    case 30:
      return btnRIGHT;
    case 40:
      return btnSELECT;
    case 50:
      return btnSW15;
    case 60:
      return btnSW16;
    default:
      return btnNONE;
  }
}

bool isRepeatable(Button button) {
  return button != btnNONE && button != btnSELECT && button != btnSW15 && button != btnSW16;
}

void onKeyDown(Button button) {
  if (button == btnNONE) return;

  if (ND::version == nd_v61 && ND::volumeChip == VolumeChip::NJU72342) {
    if (button == btnSW15) {
      Serial.println("SW15 pressed.");
      return;
    }
  }

  if (button == btnSW15) return;
  if (activeButton == button && keyRepeatState != KeyRepeatState::Idle) return;

  activeButton = button;
  input.inputBuffer = button;

  if (!isRepeatable(button) || keyRepeatTimer == nullptr) return;

  keyRepeatState = KeyRepeatState::Waiting;
  xTimerStop(keyRepeatTimer, 0);
  xTimerChangePeriod(keyRepeatTimer, pdMS_TO_TICKS(INPUT_REPEAT_DELAY), 0);
  xTimerStart(keyRepeatTimer, 0);
}

void onKeyUp(Button button) {
  if (button == btnSW15) return;
  if (activeButton != button) return;

  if (keyRepeatTimer != nullptr) xTimerStop(keyRepeatTimer, 0);
  keyRepeatState = KeyRepeatState::Idle;
  activeButton = btnNONE;
  input.inputBuffer = btnNONE;
  if (button == btnUP || button == btnDOWN || button == btnLEFT || button == btnRIGHT) {
    ndFile.clearPlaybackQueue();
  }
}

void keyRepeatTimerHandler(TimerHandle_t) {
  Button button = activeButton;
  if (!isRepeatable(button)) return;

  input.inputBuffer = button;
  if (keyRepeatState == KeyRepeatState::Waiting) {
    keyRepeatState = KeyRepeatState::Repeating;
    xTimerChangePeriod(keyRepeatTimer, pdMS_TO_TICKS(INPUT_CAPTURE_INTERVAL), 0);
  }
}

void IRAM_ATTR tca8418Irq() {
  if (tcaTaskHandle == nullptr) return;

  BaseType_t taskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(tcaTaskHandle, &taskWoken);
  if (taskWoken == pdTRUE) portYIELD_FROM_ISR();
}

void tcaTask(void*) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    int keyEvent;
    while ((keyEvent = keypad.getEvent()) != 0) {
      if (!input.isEnabled()) continue;

      const int key = (keyEvent & 0x7F) - 1;
      if ((key % 10) != 0) continue;  // COL0のみを操作キーとして使う

      const Button button = matrixKeyToButton(key);
      if ((keyEvent & 0x80) != 0) {
        onKeyDown(button);
      } else {
        onKeyUp(button);
      }
    }

    keypad.writeRegister(TCA8418_REG_INT_STAT, 0x01);
  }
}

Button readAdcButton() {
  const u16_t in = analogRead(ADC_INPUT_PIN);
  if (in > VAL_NONE - 100)
    return btnNONE;
  else if (in < VAL_0 + 100)
    return btnSELECT;
  else if (VAL_1 - 80 <= in && in < VAL_1 + 80)
    return btnRIGHT;
  else if (VAL_2 - 90 <= in && in < VAL_2 + 80)
    return btnLEFT;
  else if (VAL_3 - 120 <= in && in < VAL_3 + 120)
    return btnDOWN;
  else if (VAL_4 - 150 <= in && in < VAL_4 + 200)
    return btnUP;

  return btnNONE;
}

Button checkAdcButton() {
  const uint32_t ms = millis();
  const Button button = readAdcButton();

  if (button == btnNONE) {
    if (adcLastButton == btnUP || adcLastButton == btnDOWN || adcLastButton == btnLEFT ||
        adcLastButton == btnRIGHT) {
      ndFile.clearPlaybackQueue();
    }
    input.inputBuffer = btnNONE;
    adcLastButton = btnNONE;
    return btnNONE;
  }

  if (adcLastButton == button) {
    if (adcButtonRepeatStarted == 0) {
      adcButtonRepeatStarted = ms;
      return button;
    }

    if (!isRepeatable(button) || millis() - adcButtonRepeatStarted < ADC_REPEAT_DELAY) {
      return btnNONE;
    }
    return button;
  }

  adcLastButton = button;
  adcButtonRepeatStarted = 0;
  return btnNONE;
}

void adcTask(void*) {
  while (true) {
    if (input.isEnabled()) {
      const Button button = checkAdcButton();
      if (button != btnNONE) input.inputBuffer = button;
    }
    vTaskDelay(pdMS_TO_TICKS(INPUT_CAPTURE_INTERVAL));
  }
}

// イベントループ
void eventTask(void*) {
  event ev;
  while (true) {
    if (xQueueReceive(inputEventQueue, &ev, portMAX_DELAY) != pdTRUE) continue;

    // 表示中の画面にイベントを送信
    if (disp.currentView == ViewMode::Config) {
      cfgWindow.eventHandler(ev);
    } else if (disp.currentView == ViewMode::Visual) {
      visualWindow.eventHandler(ev);
    } else {
      playerWindow.eventHandler(ev);
    }
  }
}

void inputHandlerTask(void*) {
  while (true) {
    input.inputHandler();
    vTaskDelay(1);
  }
}

// イベント送信
void sendEvent(event ev) {
  if (inputEventQueue != nullptr) xQueueOverwrite(inputEventQueue, &ev);
}
}  // namespace

// ホールド表示時刻を、現在表示中のウィンドウへ即時反映する。
static void drawHoldTimestamp(int64_t sec) {
  playerWindow.dispData.time = sec;
  if (disp.currentView == ViewMode::Player) {
    if (sec == 0) {
      // カウントダウン完了の 0:00 だけは、フレームバッファ競合で描画を捨てない。
      playerWindow.updateHeaderBlocking(sec);
    } else {
      playerWindow.updateHeader(sec);
    }
  } else if (disp.currentView == ViewMode::Visual) {
    visualWindow.drawTimestamp(sec);
  }
}

// 新しい再生リクエストや設定変更で、進行中の3秒カウントダウンだけを止める。
void cancelPlayHoldCountdown() {
  holdCountdownActive = false;
}

bool isPlayHoldCountdownActive() {
  return holdCountdownActive;
}

// ホールドを解除し、0:00から実再生が始まるようにする。
static void finishPlayHold() {
  holdCountdownActive = false;
  nju72341.unmute();
  ND::isPaused = false;
  drawHoldTimestamp(0);
}

// CFG_PAUSE変更を、再生中のホールド状態へ即時反映する。
void syncPlayHoldConfig() {
  const int pauseConfig = ndConfig.get(CFG_PAUSE);
  if (pauseConfig == lastPauseConfig) return;

  lastPauseConfig = pauseConfig;
  if (!ND::isPaused) {
    cancelPlayHoldCountdown();
    return;
  }

  switch (pauseConfig) {
    case HOLD_NONE:
      finishPlayHold();
      break;
    case HOLD_3SEC:
      cancelPlayHoldCountdown();
      drawHoldTimestamp(-3);
      break;
    case HOLD_YES:
    default:
      cancelPlayHoldCountdown();
      drawHoldTimestamp(0);
      break;
  }
}

// 3秒ホールドのカウントダウンを非ブロッキングで進める。
static void updateHoldCountdown() {
  if (!holdCountdownActive) return;

  if (!ND::isPaused) {
    cancelPlayHoldCountdown();
    return;
  }

  if (static_cast<int32_t>(millis() - holdCountdownNextTick) < 0) return;

  holdCountdownSec++;
  holdCountdownNextTick += 1000;
  if (holdCountdownSec < 0) {
    drawHoldTimestamp(holdCountdownSec);
    return;
  }

  finishPlayHold();
}

static bool releasePlayHold() {
  if (!ND::isPaused) return false;

  if (ndConfig.get(CFG_PAUSE) == HOLD_3SEC) {
    if (!holdCountdownActive) {
      // 入力処理を止めず、次回以降の inputHandler() で -2, -1, 0 へ進める。
      holdCountdownActive = true;
      holdCountdownSec = -3;
      holdCountdownNextTick = millis() + 1000;
      drawHoldTimestamp(holdCountdownSec);
    }
    return true;
  }

  finishPlayHold();
  return true;
}

Input::Input() {}

bool Input::init() {
  keyRepeatTimer = xTimerCreate("keyRepeat", pdMS_TO_TICKS(INPUT_REPEAT_DELAY), pdTRUE,
                                nullptr, keyRepeatTimerHandler);
  if (keyRepeatTimer == nullptr) {
    Serial.println("Failed: key repeat timer init.");
    return false;
  }

  inputEventQueue = xQueueCreate(1, sizeof(event));
  if (inputEventQueue == nullptr) {
    Serial.println("Failed: input event queue init.");
    return false;
  }

  BaseType_t eventTaskCreated = xTaskCreatePinnedToCore(
      eventTask, "inputEvent", 8192, nullptr, 1, &eventTaskHandle, PRO_CPU_NUM);
  if (eventTaskCreated != pdPASS) {
    Serial.println("Failed: input event task init.");
    return false;
  }

  BaseType_t inputHandlerTaskCreated = xTaskCreatePinnedToCore(
      inputHandlerTask, "inputHandler", 4096, nullptr, 1, &inputHandlerTaskHandle, PRO_CPU_NUM);
  if (inputHandlerTaskCreated != pdPASS) {
    Serial.println("Failed: input handler task init.");
    return false;
  }

  pinMode(TCA8418_IRQ_PIN, INPUT);

  if (!keypad.begin(TCA8418_DEFAULT_ADDR, &Wire)) {
    Serial.println("Failed: TCA8418 init.");
    pinMode(ADC_INPUT_PIN, ANALOG);
    BaseType_t adcTaskCreated = xTaskCreatePinnedToCore(adcTask, "adcInput", 4096, nullptr, 1,
                                                       &adcTaskHandle, PRO_CPU_NUM);
    if (adcTaskCreated != pdPASS) {
      Serial.println("Failed: ADC input task init.");
    } else {
      Serial.println("ADC input init.");
    }
    return false;
  }

  keypad.matrix(1, 2);
  keypad.writeRegister(0x1D, 0xFF);  // ROW0-7をマトリクス入力にする
  keypad.writeRegister(0x1E, 0x03);  // COL0-1をマトリクス入力にする
  keypad.writeRegister(0x1F, 0x00);
  keypad.writeRegister(0x20, 0x00);  // GPIOイベントは使用しない
  keypad.writeRegister(0x21, 0x00);
  keypad.writeRegister(0x22, 0x00);

  while (keypad.getEvent() != 0) {
  }
  keypad.flush();
  keypad.writeRegister(TCA8418_REG_INT_STAT, 0x01);

  uint8_t config = keypad.readRegister(TCA8418_REG_CFG);
  config &= ~TCA8418_REG_CFG_GPI_IEN;
  config |= TCA8418_REG_CFG_KE_IEN;
  keypad.writeRegister(TCA8418_REG_CFG, config);

  BaseType_t taskCreated = xTaskCreatePinnedToCore(tcaTask, "tcaTask", 4096, nullptr, 1,
                                                   &tcaTaskHandle, PRO_CPU_NUM);
  if (taskCreated != pdPASS) {
    Serial.println("Failed: TCA8418 task init.");
    return false;
  }

  attachInterrupt(digitalPinToInterrupt(TCA8418_IRQ_PIN), tca8418Irq, FALLING);
  Serial.println("TCA8418 init.");
  return true;
}

void Input::inputHandler() {
  if (!_enabled) return;

  updateHoldCountdown();

  if (inputBuffer == btnNONE) return;

  if (inputBuffer == btnSW16) {
    sendEvent(event::SwitchView);
    inputBuffer = btnNONE;
    return;
  }

  if (disp.currentView == ViewMode::Config) {
    switch (inputBuffer) {
      case btnUP:
        sendEvent(event::Up);
        break;
      case btnDOWN:
        sendEvent(event::Down);
        break;
      case btnLEFT:
        sendEvent(event::Left);
        break;
      case btnRIGHT:
        sendEvent(event::Right);
        break;
      case btnSELECT:
        sendEvent(event::Close);
        break;
      default:
        break;
    }
  } else if (disp.currentView == ViewMode::Visual) {
    switch (inputBuffer) {
      case btnUP:
        ndFile.requestDirPlay(1);
        break;
      case btnDOWN:
        ndFile.requestDirPlay(-1);
        break;
      case btnRIGHT:
        ndFile.requestFilePlay(-1);
        break;
      case btnLEFT:
        ndFile.requestFilePlay(1);
        break;
      case btnSELECT:
        if (releasePlayHold()) {
          break;
        }
        sendEvent(event::Close);
        break;
      default:
        break;
    }
  } else if (ndConfig.currentMode == MODE_PLAYER) {
    switch (inputBuffer) {
      case btnUP:
        ndFile.requestDirPlay(1);
        break;
      case btnDOWN:
        ndFile.requestDirPlay(-1);
        break;
      case btnRIGHT:
        ndFile.requestFilePlay(-1);
        break;
      case btnLEFT:
        ndFile.requestFilePlay(1);
        break;
      case btnSELECT:
        if (releasePlayHold()) {
          break;
        }
        sendEvent(event::Option);
        break;
      default:
        break;
    }
  } else {
    switch (inputBuffer) {
      case btnUP:
        serialMan.changeYM2612Clock();
        break;
      case btnDOWN:
        serialMan.changeSN76489Clock();
        break;
      case btnSELECT:
        sendEvent(event::Option);
        break;
      default:
        break;
    }
  }

  inputBuffer = btnNONE;
}

void Input::setEnabled(bool state) {
  _enabled = state;
  if (!state) {
    inputBuffer = btnNONE;
    activeButton = btnNONE;
    adcLastButton = btnNONE;
    adcButtonRepeatStarted = 0;
    keyRepeatState = KeyRepeatState::Idle;
    ndFile.clearPlaybackQueue();
    if (keyRepeatTimer != nullptr) xTimerStop(keyRepeatTimer, 0);
  }
}

bool Input::isEnabled() const { return _enabled; }

Input input;
