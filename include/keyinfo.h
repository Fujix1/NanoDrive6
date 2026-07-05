// キーボード表示

#ifndef KEYINFO_H
#define KEYINFO_H

#include <Arduino.h>

#define MAX_CHANNELS 18  // 最大ch数

// 音階情報
struct NoteInfo {
  int octave;  // 1〜8, これ以外はキーオフ扱い
  int note;    // 0=C, 1=C#, 2=D, 3=D#, 4=E, ..., 9=A, 10=A#, 11=B
};

typedef enum { PAN_CENTER = 0x00, PAN_LEFT = 0x01, PAN_RIGHT = 0x02, PAN_MUTE = 0x03 } tPan;

// キーボード表示デバイス定義
typedef enum {
  YM2612_KEY,
  SN76489_0_KEY,
  SN76489_1_KEY,
  SN76489_MIX_KEY,
  DEVICE_COUNT  // デバイスの総数
} t_device;

// デバイスごとのチャンネル数
static const int device_channels[DEVICE_COUNT] = {6, 4, 4, 8};

class keyboard {
 public:
  SemaphoreHandle_t keyinfoMutex;
  keyboard();
  void reset();
  void set(t_device device, uint8_t ch, NoteInfo ni);

  struct NoteInfo keyInfo[DEVICE_COUNT][MAX_CHANNELS];
  tPan trackPan[16];
  bool trackKeyOn[16];
  uint8_t trackLevel[16];  // 0-15

 private:
};

extern keyboard KeyBoard;

#endif
