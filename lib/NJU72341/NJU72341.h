/*
 * NJU72341, NJU72342 I2C Volume Controller
 */

#ifndef __NJU72341_H
#define __NJU72341_H

#include <Arduino.h>

#include "../../src/config.h"

#define NJU72341_ADDR 0x44  // I2C address
#define NJU72342_ADDR 0x40  // I2C address

typedef enum {
  GAIN0 = 0,
  GAIN3 = 0b01010101,
  GAIN6 = 0b10101010,
  GAIN9 = 0b11111111
} tNJU72341_GAIN;

typedef enum {
  FADEOUT_BEFORE,      // フェードアウト前
  FADEOUT_PROCESSING,  // フェードアウト処理中
  FADEOUT_COMPLETED,   // フェードアウト完了
} tFadeOutStatus;

class NJU72341 {
 public:
  tFadeOutStatus fadeOutStatus = FADEOUT_BEFORE;
  void init(uint16_t fadeOutDuration, bool NJU72342);
  void reset(int8_t att);
  void setInputGain(tNJU72341_GAIN newInputGain);
  void setVolume_1B_2B(uint8_t newGain);
  void setVolume_3B_4B(uint8_t newGain);
  void setVolumeAll(uint8_t newGain);
  void setAVolume(uint8_t step);
  void mute();
  void unmute();
  void startFadeout();
  void setFadeoutDuration(uint16_t fadeOutDuration);
  void resetFadeout();

 private:
  tNJU72341_GAIN _inputGain = GAIN0;
  bool _isMuted = false;
  uint8_t _currentGain = 96;
  uint8_t _attenuation = 0;  // 音量調整値
  uint32_t _fadeoutStarted;  // フェードアウト開始時間
  bool _isFadeoutEnabled;    // フェードアウトするか
  uint16_t _fadeOutDuration;
  byte _slaveAddress;
  bool _isNJU72342;
};

extern NJU72341 nju72341;

#endif