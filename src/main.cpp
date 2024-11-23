/**
 * Nano Drive 6
 * 2024 Fujix
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
#include "vgm.h"

void setup() {
  // 最初にミュート
  pinMode(NJU72341_MUTE_PIN, OUTPUT);
  digitalWrite(NJU72341_MUTE_PIN, LOW);
  pinMode(43, OUTPUT);

  pinMode(D0, OUTPUT);
  digitalWrite(D0, HIGH);

  Serial.begin(115200);
  Serial.printf("Heap - %'d Bytes free\n", ESP.getFreeHeap());
  Serial.printf("Flash - %'d Bytes at %'d\n", ESP.getFlashChipSize(), ESP.getFlashChipSpeed());
  Serial.printf("PSRAM - Total %'d, Free %'d\n", ESP.getPsramSize(), ESP.getFreePsram());

  /*
    pinMode(A0, OUTPUT);
    pinMode(A1, OUTPUT);
    pinMode(WR, OUTPUT);
    pinMode(CS0, OUTPUT);
    pinMode(CS1, OUTPUT);
    pinMode(CS2, OUTPUT);
    pinMode(IC, OUTPUT);
    pinMode(D0, OUTPUT);
    pinMode(D1, OUTPUT);
    pinMode(D2, OUTPUT);
    pinMode(D3, OUTPUT);
    pinMode(D4, OUTPUT);
    pinMode(D5, OUTPUT);
    pinMode(D6, OUTPUT);
    pinMode(D7, OUTPUT);
    digitalWrite(D0, HIGH);
    digitalWrite(D1, HIGH);
    digitalWrite(D2, HIGH);
    digitalWrite(D3, HIGH);
    digitalWrite(D4, HIGH);
    digitalWrite(D5, HIGH);
    digitalWrite(D6, HIGH);
    digitalWrite(D7, HIGH);
    A1_HIGH;
    A0_HIGH;
    WR_HIGH;
    CS0_HIGH;
    CS1_HIGH;
    CS2_HIGH;
    IC_HIGH;

    while (1) {
    }
  */
  disableCore0WDT();  // ウォッチドッグ0無効化

  // ディスプレイ初期化
  if (!initDisp()) {
    Serial.println("initDisp failed.");
  }

  lcd.setFont(&fonts::Font2);
  lcd.println("NANO DRIVE 6");
  lcd.println("2024 Fujix@e2j.net");
  lcd.printf("Firmware: 1.7\n\n");

  lcd.printf("Open Font Render by takkaO\n");
  lcd.printf("LovyganGFX by lovyan\n");
  lcd.printf("PNGdec by Larry Bank\n");

  // PSRAM 初期化確認
  if (psramInit()) {
    lcd.printf("PSRAM initialized.\n");
  } else {
    lcd.printf("ERROR: PSRAM not available.\n");
    exit;
  }

  // ユーザ設定
  if (ndConfig.init()) {
    ndConfig.loadCfg();
    lcd.printf("User settings restored.\n");
  } else {
    lcd.printf("ERROR: SPIFFS initialization failed.\n");
    exit;
  }

  // I2C機器初期化
  // NJU72341/NJU72342 初期化
  nju72341.init(ndConfig.get(CFG_FADEOUT), false);

  // SI5351 初期化
  SI5351.begin();
  SI5351.setFreq(SI5351_4000, 0);
  SI5351.setFreq(SI5351_4000, 1);
  SI5351.enableOutputs(true);

  // SD読み込み
  if (ndFile.init() == true) {
    ndFile.listDir("/");
  } else {
    exit;
  }

  // ファイル数確認
  if (ndFile.totalSongs == 0) {
    lcd.printf("ERROR: No file to play on the SD.\n");
    exit;
  }

  // VGM用GPIO初期化
  // Lovyanの初期化で上書きされるので、initDisp();の後に呼び出す
  FM.begin();
  FM.reset();

  // 入力有効化
  input.init();
  input.setEnabled(true);

  cfgWindow.init();

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

  Serial.printf("Heap - %'d Bytes free\n", ESP.getFreeHeap());
  Serial.printf("Flash - %'d Bytes at %'d\n", ESP.getFlashChipSize(), ESP.getFlashChipSpeed());
  Serial.printf("PSRAM - Total %'d, Free %'d\n", ESP.getPsramSize(), ESP.getFreePsram());
}

void loop() {
  while (1) {
    input.inputHandler();
    if (vgm.vgmLoaded) {
      vgm.vgmProcess();
    }
    if (vgm.xgmLoaded) {
      vgm.xgmProcess();
    }
  }
}
