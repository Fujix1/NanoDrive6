#include "serialman.h"

#include <Arduino.h>

#include "NJU72341.h"
#include "SI5351.hpp"
#include "disp.h"
#include "fm.h"

u8_t getSerial() {
  while (1) {
    if (Serial.available()) {
      return Serial.read();
    }
  }
}

u32_t getSerial32() { return getSerial() + (getSerial() << 8) + (getSerial() << 16) + (getSerial() << 24); }

// シリアル受信用タスク
void serialCheckerTask(void *param) {
  u8_t command, reg, dat, samples;
  u32_t clock0 = SI5351_3579, clock1 = SI5351_2000;
  int32_t wait = 0;
  // lcd.setCursor(0, 24);
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
      case 0x80 ... 0x8f:
        dat = getSerial();
        FM.setYM2612DAC(dat, 0);
        // samples = command & 15;
        // wait = samples * 22.675 - 16;
        // if (wait > 0) {
        //   ets_delay_us(wait);
        // }
        break;

        // Additional Commands

      case 0xf0: {
        // クロック0の周波数設定
        clock0 = getSerial32();
        SI5351.setFreq((si5351Freq_t)clock0, 0);
        lcd.setCursor(0, 24);
        lcd.printf("CLOCK 0: %d", clock0);
        break;
      }

      case 0xf1: {
        // クロック1の周波数設定
        clock1 = getSerial32();
        SI5351.setFreq((si5351Freq_t)clock1, 1);
        lcd.setCursor(0, 40);
        lcd.printf("CLOCK 1: %d", clock1);
        break;
      }

      case 0x00: {
        // PSG mute
        FM.write(0x9f, 1, SI5351_1500);
        FM.write(0xbf, 1, SI5351_1500);
        FM.write(0xdf, 1, SI5351_1500);
        FM.write(0xff, 1, SI5351_1500);

        FM.write(0x9f, 2, SI5351_1500);
        FM.write(0xbf, 2, SI5351_1500);
        FM.write(0xdf, 2, SI5351_1500);
        FM.write(0xff, 2, SI5351_1500);
        break;
      }

      default:
        lcd.setCursor(0, 24);
        lcd.printf("Unknown: %02x", command);
        break;
    }
  }
}

// コンストラクタ
SerialMan::SerialMan() {}

// シリアル受信初期化
void SerialMan::init() {
  uint8_t data = 1;

  lcd.clear(TFT_BLACK);
  lcd.setFont(&fonts::Font2);
  lcd.setCursor(0, 2);
  lcd.printf("Serial test\n");

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

// シリアルマネージャのインスタンス
SerialMan serialMan = SerialMan();
