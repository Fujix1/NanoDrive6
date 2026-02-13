#ifndef INPUT_H
#define INPUT_H
#include <Arduino.h>

#include "SI5351.hpp"

#define INPUT_PIN 1
#define INPUT_CAPTURE_INTERVAL 60  // ms キャプチャインターバル
#define INPUT_REPEAT_DELAY 300     // ms リピート開始までの時間

typedef enum { btnNONE, btnRIGHT, btnUP, btnDOWN, btnLEFT, btnSELECT, btnFUNC } Button;

#define VAL_0 0        // 0 - 50
#define VAL_1 530      // 530 前後    460 - 510
#define VAL_2 1301     // 1301 前後  1240 - 1290
#define VAL_3 1980     // 1980 前後  1900 - 1980
#define VAL_4 2900     // 2905 前後  2860 - 2910
#define VAL_NONE 4095  // 4095

class Input {
 public:
  Input();
  void init();
  void inputHandler();
  void setEnabled(bool state);
  Button inputBuffer = btnNONE;
  Button checkButton2();

 private:
  bool _enabled = false;
  uint32_t _buttonLastTick = 0;  // 最後にボタンが押された時間
  uint32_t _buttonLastTick2 = 0;
  uint32_t _buttonRepeatStarted = 0;  // リピート開始時間
  bool _btnFlag;

  Button _lastButton = btnNONE;
  Button _readButton();
};

extern Input input;

#endif
