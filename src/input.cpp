#include "input.h"

#include <Adafruit_TCA8418.h>
#include <Arduino.h>

#include "disp.h"
#include "file.h"
#include "serialman.h"

namespace {
Adafruit_TCA8418 keypad;
TaskHandle_t tcaTaskHandle = nullptr;
TaskHandle_t eventTaskHandle = nullptr;
TimerHandle_t keyRepeatTimer = nullptr;
QueueHandle_t inputEventQueue = nullptr;

enum class KeyRepeatState : uint8_t { Idle, Waiting, Repeating };

volatile KeyRepeatState keyRepeatState = KeyRepeatState::Idle;
volatile Button activeButton = btnNONE;

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
      return btnFUNC;
    default:
      return btnNONE;
  }
}

bool isRepeatable(Button button) {
  return button != btnNONE && button != btnSELECT && button != btnFUNC;
}

void onKeyDown(Button button) {
  if (button == btnNONE) return;
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
  if (activeButton != button) return;

  if (keyRepeatTimer != nullptr) xTimerStop(keyRepeatTimer, 0);
  keyRepeatState = KeyRepeatState::Idle;
  activeButton = btnNONE;
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

// イベントループ
void eventTask(void*) {
  event ev;
  while (true) {
    if (xQueueReceive(inputEventQueue, &ev, portMAX_DELAY) != pdTRUE) continue;

    // 表示中の画面にイベントを送信
    if (disp.currentView == ViewMode::Config) {
      cfgWindow.eventHandler(ev);
    } else {
      playerWindow.eventHandler(ev);
    }
  }
}

// イベント送信
void sendEvent(event ev) {
  if (inputEventQueue != nullptr) xQueueOverwrite(inputEventQueue, &ev);
}
}  // namespace

Input::Input() {}

bool Input::init() {
  pinMode(TCA8418_IRQ_PIN, INPUT);

  if (!keypad.begin(TCA8418_DEFAULT_ADDR, &Wire)) {
    Serial.println("Failed: TCA8418 init.");
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

  keyRepeatTimer = xTimerCreate("keyRepeat", pdMS_TO_TICKS(INPUT_REPEAT_DELAY), pdTRUE,
                                nullptr, keyRepeatTimerHandler);
  if (keyRepeatTimer == nullptr) {
    Serial.println("Failed: key repeat timer init.");
    return false;
  }

  // キュー作成
  inputEventQueue = xQueueCreate(1, sizeof(event));
  if (inputEventQueue == nullptr) {
    Serial.println("Failed: input event queue init.");
    return false;
  }

  // イベントループタスク
  BaseType_t eventTaskCreated = xTaskCreatePinnedToCore(
      eventTask, "inputEvent", 8192, nullptr, 1, &eventTaskHandle, PRO_CPU_NUM);
  if (eventTaskCreated != pdPASS) {
    Serial.println("Failed: input event task init.");
    return false;
  }

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
  if (!_enabled || inputBuffer == btnNONE) return;

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
  } else if (ndConfig.currentMode == MODE_PLAYER) {
    switch (inputBuffer) {
      case btnUP:
        ndFile.dirPlay(1);
        break;
      case btnDOWN:
        ndFile.dirPlay(-1);
        break;
      case btnRIGHT:
        ndFile.filePlay(-1);
        break;
      case btnLEFT:
        ndFile.filePlay(1);
        break;
      case btnSELECT:
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
    keyRepeatState = KeyRepeatState::Idle;
    if (keyRepeatTimer != nullptr) xTimerStop(keyRepeatTimer, 0);
  }
}

bool Input::isEnabled() const { return _enabled; }

Input input;
