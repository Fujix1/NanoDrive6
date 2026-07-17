#ifndef INPUT_H
#define INPUT_H
#include <Arduino.h>

#define INPUT_CAPTURE_INTERVAL 60  // ms キャプチャインターバル
#define INPUT_REPEAT_DELAY 400     // ms リピート開始までの時間
#define TCA8418_IRQ_PIN 1

typedef enum { btnNONE, btnRIGHT, btnUP, btnDOWN, btnLEFT, btnSELECT, btnSW15, btnSW16 } Button;

enum class event {
  None,
  Right,
  Up,
  Down,
  Left,
  Select,
  Option,
  Close,
  SwitchView,
  Browser,
  UpDir
};

void cancelPlayHoldCountdown();
bool isPlayHoldCountdownActive();
void syncPlayHoldConfig();

class Input {
 public:
  Input();
  bool init();
  void inputHandler();
  void setEnabled(bool state);
  bool isEnabled() const;
  volatile Button inputBuffer = btnNONE;

 private:
  volatile bool _enabled = false;
};

extern Input input;

#endif
