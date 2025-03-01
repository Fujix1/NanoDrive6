#include "input.h"

#include <Arduino.h>

#include "disp.h"
#include "file.h"
#include "serialman.h"

void inputTask(void *param) {
  while (1) {
    Button b = input.checkButton2();
    if (b != btnNONE) input.inputBuffer = b;
    vTaskDelay(INPUT_CAPTURE_INTERVAL);
  }
}

void serialCheckerTask_(void *param) {
  while (1) {
    int s = Serial.available();
    if (s > 0) {  // シリアルデータがある場合
      byte data[s + 1];
      // byte data = Serial.read();
      Serial.readBytes(data, s);
      lcd.setCursor(0, 32);
      // lcd.printf("%02x", Serial.available());
      lcd.printf("%02x %d   ", data[0], s);

      // Serial.printf("Serial data: %c\n", data);  // debug
      /*
      while (Serial.available()) Serial.read();
      if (data == 'a') {
        input.inputBuffer = btnRIGHT;
      } else if (data == 'd') {
        input.inputBuffer = btnLEFT;
      } else if (data == 'w') {
        input.inputBuffer = btnUP;
      } else if (data == 's') {
        input.inputBuffer = btnDOWN;
      }*/
    }
    vTaskDelay(1);
  }
}

Input::Input() { pinMode(INPUT_PIN, ANALOG); }

void Input::init() { xTaskCreateUniversal(inputTask, "inputTask", 8192, NULL, 1, NULL, PRO_CPU_NUM); }

void Input::inputHandler() {
  if (!_enabled) return;

  if (cfgWindow.isVisible) {
    switch (inputBuffer) {
      case btnNONE: {
        break;
      }
      case btnUP: {
        cfgWindow.up();
        break;
      }
      case btnDOWN: {
        cfgWindow.down();
        break;
      }
      case btnLEFT: {
        cfgWindow.left();
        break;
      }
      case btnRIGHT: {
        cfgWindow.right();
        break;
      }
      case btnSELECT: {
        cfgWindow.close();  // 設定ウィンドウ閉じる
        break;
      }
    }

  } else {
    if (ndConfig.currentMode == MODE_PLAYER) {
      switch (inputBuffer) {
        case btnNONE: {
          break;
        }
        case btnUP: {
          ndFile.dirPlay(1);
          break;
        }
        case btnDOWN: {
          ndFile.dirPlay(-1);
          break;
        }
        case btnRIGHT: {
          ndFile.filePlay(-1);
          break;
        }
        case btnLEFT: {
          ndFile.filePlay(1);
          break;
        }
        case btnSELECT: {
          cfgWindow.show();  // 設定ウィンドウ表示
          break;
        }
      }
    } else {
      switch (inputBuffer) {
        case btnNONE: {
          break;
        }
        case btnUP: {
          serialMan.changeYM2612Clock();
          break;
        }
        case btnDOWN: {
          serialMan.changeSN76489Clock();
          break;
        }
        case btnRIGHT: {
          break;
        }
        case btnLEFT: {
          break;
        }
        case btnSELECT: {
          cfgWindow.show();  // 設定ウィンドウ表示
          break;
        }
      }
    }
  }
  inputBuffer = btnNONE;
}

void Input::setEnabled(bool state) { _enabled = state; }

//----------------------------------------------------------------------
// ボタンの状態取得
Button Input::_readButton() {
  u16_t in = analogRead(INPUT_PIN);
  // Serial.printf("%d\n", in);
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

Button Input::checkButton2() {
  uint32_t ms = millis();
  Button btn = _readButton();  // ボタン取得

  if (btn == btnNONE) {
    _lastButton = btnNONE;
    return btnNONE;
  } else {
    if (_lastButton == btn) {  // 前と同じボタンなら
      if (_buttonRepeatStarted == 0) {
        _buttonRepeatStarted = ms;  // リピート開始
        return btn;
      } else {
        if (btn == btnSELECT || btn == btnFUNC ||
            millis() - _buttonRepeatStarted < INPUT_REPEAT_DELAY) {  // リピート開始前
          return btnNONE;
        } else {
          return btn;
        }
      }

    } else {  // 前回と違うボタン
      _lastButton = btn;
      _btnFlag = false;
      _buttonRepeatStarted = 0;

      return btnNONE;
    }
  }
}

Input input = Input();
