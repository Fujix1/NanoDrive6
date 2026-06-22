/**
 * Nano Drive 6.1
 * 2024 - 2026 (C) Fujix
 * e2j.net

 *  Open Font Render
 *  URL: https://github.com/takkaO/OpenFontRender
 *  Author: takkaO
 *  License: FreeType License
 *  Portions of this software are copyright © The FreeTypeProject (www.freetype.org).
 *  All rights reserved.
 *
 *  LovyanGFX
 *  URL: https://github.com/lovyan03/LovyanGFX
 *  Author: lovyan03
 *  License: FreeBSD
 *
 *  PNGdec
 *  URL: https://github.com/bitbank2/PNGdec
 *  Author: Larry Bank
 *  License: Apache-2.0
 *
 *  Adafruit TCA8418
 *  URL: https://github.com/adafruit/Adafruit_TCA8418
 *  Author: Adafruit
 *  License: BSD-3-Clause
 *
 *  Adafruit BusIO
 *  URL: https://github.com/adafruit/Adafruit_BusIO
 *  Author: Adafruit
 *  License: MIT

 *  BIZ UDPGothic
 *  URL:https://fonts.google.com/specimen/BIZ+UDPGothic/license
 *  License: SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
 *  Copyright 2022 The BIZ UDGothic Project Authors
 *  (https://github.com/googlefonts/morisawa-biz-ud-mincho) This Font Software is
 *  licensed under the SIL Open Font License, Version 1.1 . This license is
 *  copied below, and is also available with a FAQ at:
 *  https://openfontlicense.org
 *
 */

#include "NJU72341.h"
#include "SI5351.hpp"
#include "common.h"
#include "config.h"
#include "disp.h"
#include "file.h"
#include "fm.h"
#include "input.h"
#include "serialman.h"
#include "vgm.h"

void setup() {
  disableCore0WDT();  // ウォッチドッグ0無効化

  // 最初にミュート
  pinMode(NJU72341_MUTE_PIN, OUTPUT);
  digitalWrite(NJU72341_MUTE_PIN, LOW);
  pinMode(43, OUTPUT);

  pinMode(D0, OUTPUT);
  digitalWrite(D0, HIGH);

  Serial.begin(1500000);
  Serial.printf("Heap - %'d Bytes free\n", ESP.getFreeHeap());
  Serial.printf("Flash - %'d Bytes at %'d\n", ESP.getFlashChipSize(), ESP.getFlashChipSpeed());
  Serial.printf("PSRAM - Total %'d, Free %'d\n", ESP.getPsramSize(), ESP.getFreePsram());

  delay(100);
  Wire.begin(I2C_SDA, I2C_SCL, I2C_CLOCK);

  if (!input.init()) {
    Serial.println("Input IC TCA8418 failed.");
  }

  // ディスプレイ初期化
  if (!initDisp()) {
    Serial.println("initDisp failed.");
  }

  lcd.setFont(&fonts::Font2);
  lcd.println("NANO DRIVE 6.1");
  lcd.println("2024-2026 fujix@e2j.net");
  lcd.printf("Version 6.1 dev\n\n");

  // PSRAM 初期化確認
  if (psramInit()) {
    lcd.printf("PSRAM initialized.\n");
  } else {
    lcd.printf("ERROR: PSRAM not available.\n");
    exit;
  }

  // ユーザ設定
  ndConfig.init();
  ndConfig.loadCfg();

  // I2C機器初期化
  // NJU72341/NJU72342 初期化
  nju72341.init(I2C_SDA, I2C_SCL, NJU72341_MUTE_PIN, ndConfig.get(CFG_FADEOUT), false);

  // SI5351 初期化
  SI5351.begin();
  SI5351.setFreq(SI5351_7670, 0);
  SI5351.setFreq(SI5351_3579, 1);
  SI5351.enableOutputs(true);

  // VGM用GPIO初期化
  // Lovyanの初期化で上書きされるので、initDisp();の後に呼び出す
  FM.begin();
  FM.reset();

  // 動作切り替え
  // プレイヤーモード
  if (ndConfig.currentMode == MODE_PLAYER) {
    // SD読み込み
    if (ndFile.init() == true) {
      Serial.printf("SD init.\n");
      ndFile.listDir("/");
    } else {
      exit;
    }

    // ファイル数確認
    if (ndFile.totalSongs == 0) {
      lcd.printf("ERROR: No file to play on the SD.\n");
      exit;
    }

    // 読み込み履歴復元
    u16_t lastDirIndex = 0, lastTrackIndex = 0;
    u32_t history = ndConfig.loadHistory();
    lastDirIndex = history & 0xffff;
    lastTrackIndex = (history & 0xffff0000) >> 16;

    switch (ndConfig.get(CFG_HISTORY)) {
      case HISTORY_FOLDER:
        ndFile.dirPlay(lastDirIndex);
        break;
      case HISTORY_FILE:
        ndFile.play(lastDirIndex, lastTrackIndex);
        break;
      default:
        ndFile.dirPlay(0);
    }
  }

  else {
    // シリアルモード
    serialMan.init();
    serialMan.startSerialTask();
  }

  // 入力有効化
  input.setEnabled(true);

  cfgWindow.init();

  Serial.printf("Heap - %'d Bytes free\n", ESP.getFreeHeap());
  Serial.printf("Flash - %'d Bytes at %'d\n", ESP.getFlashChipSize(), ESP.getFlashChipSpeed());
  Serial.printf("PSRAM - Total %'d, Free %'d\n", ESP.getPsramSize(), ESP.getFreePsram());
}

void loop() {
  if (ndConfig.currentMode == MODE_PLAYER) {
    while (1) {
      FM.applyPendingYM2612OutputMode();
      if (vgm.vgmLoaded) {
        vgm.vgmProcess();
      } else if (vgm.xgmLoaded) {
        if (vgm.XGMVersion == 1)
          vgm.xgmProcess();
        else
          vgm.xgm2Process();
      }
      input.inputHandler();
    }

  } else {
    while (1) {
      FM.applyPendingYM2612OutputMode();
      input.inputHandler();
      delay(1);
    }
  }
}
