#include "NJU72341.h"

#include <Wire.h>

#include "../../src/common.h"
#include "../../src/config.h"

#define FADEOUT_STEPS 50

// Aカーブの音量マップ
static const uint8_t NJU72341_db[FADEOUT_STEPS] = {
    0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  3,  3,
    4,  5,  5,  6,  7,  8,  9,  11, 12, 13, 15, 17, 18, 20, 22, 24, 26,
    28, 30, 32, 35, 38, 41, 44, 48, 52, 57, 62, 68, 74, 80, 88, 96};

static TimerHandle_t hFadeOutTimer;  // フェードアウト用タイマー
static u32_t _fadeOutStartMS;
static byte _fadeOutStep;

static void fadeOutTimerHandler(void* param) {
  if (nju72341.fadeOutStatus != FADEOUT_PROCESSING) {
    return;
  }

  nju72341.setAVolume(_fadeOutStep++);

  if (_fadeOutStep == FADEOUT_STEPS) {
    xTimerStop(hFadeOutTimer, 0);
    nju72341.fadeOutStatus = FADEOUT_COMPLETED;
    _fadeOutStep = 0;
  }
}

void NJU72341::init(uint16_t fadeOutDuration, bool NJU72342) {
  Wire.begin(I2C_SDA, I2C_SCL, 600000);

  if (NJU72342) {
    _slaveAddress = NJU72342_ADDR;
    _isNJU72342 = true;
  } else {
    _slaveAddress = NJU72341_ADDR;
    _isNJU72342 = false;
  }
  _attenuation = 0;
  _currentGain = 255;
  fadeOutStatus = FADEOUT_BEFORE;

  _isFadeoutEnabled = (fadeOutDuration != FO_0);
  pinMode(NJU72341_MUTE_PIN, OUTPUT);
  mute();

  setInputGain(GAIN9);

  // タイマー生成
  if (fadeOutDuration == FO_0) {
    _fadeOutDuration = 8000;
  } else {
    _fadeOutDuration = fadeOutDuration;
  }

  hFadeOutTimer =
      xTimerCreate("FADEOUT_TIMER", _fadeOutDuration / FADEOUT_STEPS, pdTRUE,
                   NULL, fadeOutTimerHandler);
}

void NJU72341::setFadeoutDuration(uint16_t fadeOutDuration) {
  if (fadeOutDuration == FO_0) {
    _isFadeoutEnabled = false;
  } else {
    if (fadeOutDuration != _fadeOutDuration) {
      _isFadeoutEnabled = true;
      _fadeOutDuration = fadeOutDuration;
      xTimerChangePeriod(hFadeOutTimer, _fadeOutDuration / FADEOUT_STEPS, 0);
    }
  }
}

void NJU72341::startFadeout() {
  // すでに処理中のときはなにもしない。フェード時間よりループが短い場合
  if (fadeOutStatus == FADEOUT_PROCESSING) {
    return;
  }

  _fadeOutStartMS = millis();
  _fadeOutStep = 0;
  if (_isFadeoutEnabled) {
    fadeOutStatus = FADEOUT_PROCESSING;
    xTimerStart(hFadeOutTimer, 0);
  } else {
    fadeOutStatus = FADEOUT_COMPLETED;
  }
}

// att 減衰量dB: -1 = 変更しない
void NJU72341::reset(int8_t att) {
  if (att >= 0) {
    _attenuation = att;
  }
  fadeOutStatus = FADEOUT_BEFORE;
  resetFadeout();
}

void NJU72341::resetFadeout() {
  xTimerStop(hFadeOutTimer, 0);
  fadeOutStatus = FADEOUT_BEFORE;
  _fadeOutStep = 0;
}

void NJU72341::setInputGain(tNJU72341_GAIN newInputGain) {
  _inputGain = newInputGain;
  Wire.beginTransmission(_slaveAddress);
  Wire.write(0x00);
  Wire.write(_inputGain);
  Wire.endTransmission();
}

// 全チャンネルの音量設定
// 0:最大, 96: ミュート
void NJU72341::setVolumeAll(uint8_t newGain) {
  uint8_t bit = 119 - newGain - _attenuation;
  Wire.beginTransmission(_slaveAddress);
  Wire.write(0x01);
  Wire.write(bit);
  Wire.write(bit);
  if (_isNJU72342) {
    Wire.write(bit);
    Wire.write(bit);
  }
  Wire.endTransmission();
}

void NJU72341::setVolume_1B_2B(uint8_t newGain) {
  if (_currentGain == newGain) {
    return;  // 変更なしのとき
  }
  _currentGain = newGain;  //  -_attenuation;
  uint8_t bit = 119 - newGain;

  Wire.beginTransmission(_slaveAddress);
  Wire.write(0x01);
  Wire.write(bit);
  Wire.write(bit);
  Wire.endTransmission();
}

void NJU72341::setVolume_3B_4B(uint8_t newGain) {
  uint8_t bit = 119 - newGain;
  Wire.beginTransmission(_slaveAddress);
  Wire.write(0x03);
  Wire.write(bit);
  Wire.write(bit);
  Wire.endTransmission();
}

void NJU72341::mute() {
  _isMuted = true;
  // digitalWrite(NJU72341_MUTE_PIN, HIGH);
  setVolumeAll(96);
}

void NJU72341::unmute() {
  setVolumeAll(0);
  // digitalWrite(NJU72341_MUTE_PIN, LOW);
  _isMuted = false;
}

void NJU72341::setAVolume(uint8_t step) {
  if (step > FADEOUT_STEPS - 1) {
    step = FADEOUT_STEPS - 1;
  }
  uint8_t data = NJU72341_db[step];
  setVolumeAll(NJU72341_db[step]);
}

NJU72341 nju72341 = NJU72341();
