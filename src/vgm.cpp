
#include "vgm.h"

#include <cassert>
#include <codecvt>
#include <locale>
#include <string>

#include "file.h"
#include "fm.h"

#define ONE_CYCLE \
  22676  // 22.67573696145125 us
         // 1 / 44100 * 1 000 000

//---------------------------------------------------------------------
static std::string wstringToUTF8(const std::wstring& src) {
  std::wstring_convert<std::codecvt_utf8<wchar_t> > converter;
  return converter.to_bytes(src);
}

//---------------------------------------------------------------------
static uint32_t gd3p;
void parseGD3(t_gd3* gd3, u32_t offset) {}

//---------------------------------------------------------------------
// VGM クラス
VGM::VGM() {
  // メモリ確保
  psramInit();  // ALWAYS CALL THIS BEFORE USING THE PSRAM
  vgmData = (uint8_t*)ps_calloc(MAX_FILE_SIZE, sizeof(uint8_t));

  // チップスロット
  for (int i = 0; i < sizeof chipSlot / sizeof chipSlot[0]; i++) {
    chipSlot[i] = -1;
  }

  if (CHIP0 != CHIP_NONE) {
    chipSlot[CHIP0] = 0;
  }
  if (CHIP1 != CHIP_NONE) {
    chipSlot[CHIP1] = 1;
  }
  if (CHIP2 != CHIP_NONE) {
    chipSlot[CHIP2] = 2;
  }
}

//---------------------------------------------------------------------
// vgm 再生準備
bool VGM::ready() {
  vgmLoaded = false;
  xgmLoaded = false;
  _pos = 0;

  if (vgmData == NULL) {
    Serial.println("ERROR: PSRAM確保失敗");
    return false;
  }

  freq[0] = SI5351_UNDEFINED;
  freq[1] = SI5351_UNDEFINED;
  freq[2] = SI5351_UNDEFINED;

  _vgmLoop = 0;
  _vgmDelay = 0;
  _vgmSamples = 0;

  // VGM ident
  if (get_ui32_at(0) != 0x206d6756) {
    Serial.println("ERROR: VGMファイル解析失敗");
    vgmLoaded = false;
    return false;
  }

  // version
  version = get_ui32_at(8);

  // total # samples
  totalSamples = get_ui32_at(0x18);

  // loop offset
  loopOffset = get_ui32_at(0x1c);

  // vg3 offset
  gd3Offset = get_ui32_at(0x14) + 0x14;

  u32_t gd3Size = get_ui32_at(gd3Offset + 0x8);

  // data offset
  dataOffset = (version >= 0x150) ? get_ui32_at(0x34) + 0x34 : 0x40;
  _pos = dataOffset;

  // Setup Clocks
  uint32_t sn76489_clock = get_ui32_at(0x0c);
  if (sn76489_clock) {
    if (CHIP0 == CHIP_SN76489_0) {
      freq[0] = normalizeFreq(sn76489_clock, CHIP_SN76489_0);
    } else if (CHIP1 == CHIP_SN76489_0) {
      freq[1] = normalizeFreq(sn76489_clock, CHIP_SN76489_0);
    }

    // デュアルSN76489
    if ((sn76489_clock & (1 << 30))) {
      // Serial.printf("DUAL, version: %x\n", version);

      if (version < 0x170) {
        freq[2] = normalizeFreq(sn76489_clock, CHIP_SN76489_0);
      } else {
        uint32_t headerSize = get_ui32_at(0xbc);
        uint32_t chpClockOffset = get_ui32_at(0xbc + headerSize);
        uint8_t entryCount = get_ui8_at(0xbc + headerSize + chpClockOffset);
        uint8_t chipID = get_ui8_at(0xbc + headerSize + chpClockOffset + 1);
        uint32_t clock = get_ui32_at(0xbc + headerSize + chpClockOffset + 2);

        // Serial.printf("chipId: %d, freq: %x\n", chipID, clock);

        if (chipID == 0) {
          freq[1] = normalizeFreq(sn76489_clock, CHIP_SN76489_0);
          freq[2] = normalizeFreq(clock, CHIP_SN76489_0);
        } else if (chipID == 1) {
          freq[2] = normalizeFreq(clock, CHIP_SN76489_0);
        }
      }
    }
  }

  uint32_t ym2612_clock = get_ui32_at(0x2c);
  if (ym2612_clock) {
    if (CHIP0 == CHIP_YM2612) {
      freq[0] = normalizeFreq(ym2612_clock, CHIP_YM2612);
    } else if (CHIP1 == CHIP_YM2612) {
      freq[1] = normalizeFreq(ym2612_clock, CHIP_YM2612);
    }
  }

  uint32_t ay8910_clock = (version >= 0x151 && dataOffset >= 0x78) ? get_ui32_at(0x74) : 0;
  if (ay8910_clock) {
    if (CHIP0 == CHIP_AY8910) {
      freq[0] = normalizeFreq(ay8910_clock, CHIP_AY8910);
    } else if (CHIP1 == CHIP_AY8910) {
      freq[1] = normalizeFreq(ay8910_clock, CHIP_AY8910);
    }
    if (CHIP0 == CHIP_YM2203_0) {
      freq[0] = normalizeFreq(ay8910_clock, CHIP_AY8910);
    }
  }

  uint32_t ym2203_clock = (version >= 0x151 && dataOffset >= 0x78) ? get_ui32_at(0x44) : 0;
  if (ym2203_clock) {
    if (ym2203_clock & 0x40000000) {  // check the second chip
      if (CHIP0 == CHIP_YM2203_0) {
        freq[0] = normalizeFreq(ym2203_clock, CHIP_YM2203_0);
      }
      if (CHIP1 == CHIP_YM2203_1) {
        freq[1] = normalizeFreq(ym2203_clock, CHIP_YM2203_1);
      }
    } else {
      if (CHIP0 == CHIP_YM2203_0) {
        freq[0] = normalizeFreq(ym2203_clock, CHIP_YM2203_0);
      } else if (CHIP1 == CHIP_YM2203_1) {
        freq[1] = normalizeFreq(ym2203_clock, CHIP_YM2203_1);
      }

      // Use YM2612 as YM2203
      if (CHIP0 == CHIP_YM2612) {
        freq[0] = normalizeFreq(ym2203_clock, CHIP_YM2612);
      } else if (CHIP1 == CHIP_YM2612) {
        freq[1] = normalizeFreq(ym2203_clock, CHIP_YM2612);
      }
      // Use YM2610 as YM2203
      if (CHIP0 == CHIP_YM2610) {
        freq[0] = normalizeFreq(ym2203_clock, CHIP_YM2610);
      } else if (CHIP1 == CHIP_YM2610) {
        freq[1] = normalizeFreq(ym2203_clock, CHIP_YM2612);
      }
    }
  }

  uint32_t ym2151_clock = get_ui32_at(0x30);
  if (ym2151_clock) {
    if (CHIP0 == CHIP_YM2151) {
      freq[0] = normalizeFreq(ym2151_clock, CHIP_YM2151);
    }
    if (CHIP1 == CHIP_YM2151) {
      freq[1] = normalizeFreq(ym2151_clock, CHIP_YM2151);
    }
  }

  uint32_t ym3812_clock = get_ui32_at(0x50);
  if (ym3812_clock) {
    if (CHIP0 == CHIP_YM3812) {
      freq[0] = normalizeFreq(ym3812_clock, CHIP_YM3812);
    }
    if (CHIP1 == CHIP_YM3812) {
      freq[1] = normalizeFreq(ym3812_clock, CHIP_YM3812);
    }
    if (CHIP0 == CHIP_YMF262) {
      freq[0] = normalizeFreq(ym3812_clock, CHIP_YM3812);
    }
    if (CHIP0 == CHIP_YMF262) {
      freq[1] = normalizeFreq(ym3812_clock, CHIP_YM3812);
    }
  }

  uint32_t ymf262_clock = get_ui32_at(0x5c);
  if (ymf262_clock) {
    if (CHIP0 == CHIP_YMF262) {
      freq[0] = normalizeFreq(ymf262_clock, CHIP_YMF262);
    }
    if (CHIP1 == CHIP_YMF262) {
      freq[1] = normalizeFreq(ymf262_clock, CHIP_YMF262);
    }
  }

  // 周波数設定
  if (freq[0] != SI5351_UNDEFINED) {
    SI5351.setFreq(freq[0], 0);
  }
  if (freq[1] != SI5351_UNDEFINED) {
    SI5351.setFreq(freq[1], 1);
  }
  if (freq[2] != SI5351_UNDEFINED) {
    SI5351.setFreq(freq[2], 0);
  }

  SI5351.enableOutputs(true);

  vgmLoaded = true;  // VGM 開始できる

  // GD3 tags
  _parseGD3();

  String chip[2] = {"", ""};
  int c = 0;

  if (freq[0] != 0) {
    char buf[7];
    dtostrf((double)freq[0] / 1000000.0, 1, 4, buf);
    chip[c++] = CHIP_LABEL[CHIP0] + " @ " + String(buf).substring(0, 5) + " MHz";
  }

  if (freq[1] != 0) {
    char buf[7];
    dtostrf((double)freq[1] / 1000000.0, 1, 4, buf);
    chip[c++] = CHIP_LABEL[CHIP1] + " @ " + String(buf).substring(0, 5) + " MHz";
  }

  if (c < 2 && freq[2] != 0) {
    char buf[7];
    dtostrf((double)freq[2] / 1000000.0, 1, 4, buf);
    chip[c++] = CHIP_LABEL[CHIP2] + " @ " + String(buf).substring(0, 5) + " MHz";
  }

  u32_t n = 1 + ndFile.currentFile;  // フォルダ内曲番

  updateDisp({gd3.trackEn, gd3.trackJp, gd3.gameEn, gd3.gameJp, gd3.systemEn, gd3.systemJp, gd3.authorEn, gd3.authorJp,
              gd3.date, chip[0], chip[1], FORMAT_LABEL[vgm.format], 0, n, ndFile.files[ndFile.currentDir].size()});

  _vgmStart = micros();
  return true;
}

// GD3タグをパース
void VGM::_parseGD3() {
  _gd3p = gd3Offset + 12;

  // GD3 が無い場合
  if (vgm.size < gd3Offset) {
    gd3.trackEn = ndFile.files[ndFile.currentDir][ndFile.currentFile];
    gd3.trackJp = ndFile.files[ndFile.currentDir][ndFile.currentFile];
    gd3.gameEn = "(No GD3 info)";
    gd3.gameJp = "(GD3情報なし)";
    gd3.systemEn = "";
    gd3.systemJp = "";
    gd3.authorEn = "";
    gd3.authorJp = "";
    gd3.date = "";
  } else {
    gd3.trackEn = _digGD3();
    gd3.trackJp = _digGD3();
    gd3.gameEn = _digGD3();
    gd3.gameJp = _digGD3();
    gd3.systemEn = _digGD3();
    gd3.systemJp = _digGD3();
    gd3.authorEn = _digGD3();
    gd3.authorJp = _digGD3();
    gd3.date = _digGD3();
    // gd3.converted = _digGD3();
    // gd3.notes = _digGD3();

    if (gd3.trackJp == "") gd3.trackJp = gd3.trackEn;
    if (gd3.gameJp == "") gd3.gameJp = gd3.gameEn;
    if (gd3.systemJp == "") gd3.systemJp = gd3.systemEn;
    if (gd3.authorJp == "") gd3.authorJp = gd3.authorEn;
  }
}

String VGM::_digGD3() {
  std::wstring wst;
  wst.clear();
  while (vgmData[_gd3p] != 0 || vgmData[_gd3p + 1] != 0) {
    wst += (char16_t)((vgmData[_gd3p + 1] << 8) | vgmData[_gd3p]);
    _gd3p += 2;
  }
  _gd3p += 2;
  std::string sst = wstringToUTF8(wst);
  return (String)sst.c_str();
}

//----------------------------------------------------------------------
// 周波数の実際の値を設定
si5351Freq_t VGM::normalizeFreq(uint32_t freq, t_chip chip) {
  switch (chip) {
    case CHIP_AY8910: {
      switch (freq) {
        case 1500000:
          return SI5351_3000;
          break;
        case 1536000:
          return SI5351_3072;
          break;
        case 1789750:
        case 1789772:
        case 1789773:
        case 1789775:
          return SI5351_3579;
          break;
        case 2000000:
          return SI5351_4000;
          break;
        default:
          return SI5351_4000;
          break;
      }
      break;
    }

    case CHIP_YM2203_0:
    case CHIP_YM2203_1: {
      switch (freq) {
        case 1076741824:  // デュアル 1.5MHz
        case 1075241824:  // デュアル 1.5MHz
          return SI5351_1500;
          break;
        case 3000000:  // 3MHz
          return SI5351_3000;
          break;
        case 3072000:  // 3.072MHz
          return SI5351_3072;
          break;
        case 3579580:  // 3.579MHz
        case 3579545:
          return SI5351_3579;
          break;
        case 3993600:
          return SI5351_4000;
          break;
        case 4000000:
        case 1077741824:  // デュアル 4MHz
          return SI5351_4000;
          break;
        case 4500000:  // 4.5MHz
          return SI5351_4500;
          break;
        default:
          return SI5351_3579;
          break;
      }
    }
    case CHIP_YM2151: {
      switch (freq) {
        case 3375000:
          return SI5351_3375;
          break;
        case 3500000:
          return SI5351_3500;
          break;
        case 3579580:
        case 3579545:
        case 3580000:
          return SI5351_3579;
          break;
        case 4000000:
          return SI5351_4000;
          break;
        default:
          return SI5351_3579;
          break;
      }
      break;
    }
    case CHIP_YM2608: {
      switch (freq) {
        case 7987000:
          return SI5351_7987;
          break;
        case 8000000:
          return SI5351_8000;
          break;
        default:
          return SI5351_8000;
          break;
      }
      break;
    }
    case CHIP_YM2612: {
      switch (freq) {
        case 8000000:
        case 0x807a1200:
          return SI5351_8000;
          break;
        case 7670453:
          return SI5351_7670;
          break;
        case 1500000:  // YM2203 @ 1.5MHz
          return SI5351_3000;
          break;
        case 3000000:  // YM2203 @ 3MHz
          return SI5351_6000;
          break;
        case 3579580:  // YM2203 @ 3.579MHz
        case 3579545:
          return SI5351_7159;
          break;
        case 3993600:
          return SI5351_8000;
          break;
        case 4000000:
        case 1077741824:  // デュアル 4MHz
          return SI5351_8000;
          break;
        default:
          return SI5351_7670;
      }
      break;
    }
    case CHIP_SN76489_0: {
      switch (freq) {
        case 1536000:
          return SI5351_1536;
          break;
        case 1789772:
        case 0x40000000 + 1789772:
          return SI5351_1789;
          break;
        case 3579580:
        case 3579545:
        case 0x40000000 + 3579580:
        case 0x40000000 + 3579545:
          return SI5351_3579;
          break;
        case 4000000:
        case 0x40000000 + 4000000:
          return SI5351_4000;
          break;
        case 2578000:
          return SI5351_2578;
          break;
        case 2000000:
        case 0x40000000 + 2000000:
          return SI5351_2000;
          break;
        default:
          return SI5351_3579;
          break;
      }
      break;
    }

    case CHIP_YM2610: {
      switch (freq) {
        case 8000000:
        case 0x807a1200:
          return SI5351_8000;
          break;
        case 7670453:
          return SI5351_7670;
          break;
        case 1500000:  // YM2203 @ 1.5MHz
          return SI5351_3000;
          break;
        case 3000000:  // YM2203 @ 3MHz
          return SI5351_6000;
          break;
        case 3579580:  // YM2203 @ 3.579MHz
        case 3579545:
          return SI5351_7159;
          break;
        case 3993600:
          return SI5351_8000;
          break;
        case 4000000:
        case 1077741824:  // デュアル 4MHz
          return SI5351_8000;
          break;
        default:
          return SI5351_7670;
      }
      break;
    }
    case CHIP_YM3812: {
      switch (freq) {
        case 3500000:
          return SI5351_3500;
          break;
        case 3000000:
          return SI5351_3000;
          break;
        case 4000000:
        case 0x40000000 + 4000000:
          return SI5351_4000;
          break;
        case 1789772:
        case 0x40000000 + 1789772:
          return SI5351_1789;
          break;
        case 3579580:
        case 3579545:
        case 0x40000000 + 3579580:
        case 0x40000000 + 3579545:
          return SI5351_3579;
          break;
        case 2578000:
          return SI5351_2578;
          break;
        case 2000000:
        case 0x40000000 + 2000000:
          return SI5351_2000;
          break;
        default:
          return SI5351_3579;
          break;
      }
      break;
    }
    case CHIP_YMF262: {
      switch (freq) {
        case 0xda7a64:
          return SI5351_14318;
          break;
        default:
          return SI5351_14318;
          break;
      }
      break;
    }
  }

  return SI5351_UNDEFINED;
}

//----------------------------------------------------------------------
// 8 bit 返す
uint8_t VGM::get_ui8() { return vgmData[_pos++]; }

//----------------------------------------------------------------------
// 16 bit 返す
uint16_t VGM::get_ui16() { return get_ui8() + (get_ui8() << 8); }

//----------------------------------------------------------------------
// 24 bit 返す
uint32_t VGM::get_ui24() { return get_ui8() + (get_ui8() << 8) + (get_ui8() << 16); }

//----------------------------------------------------------------------
// 32 bit 返す
uint32_t VGM::get_ui32() { return get_ui8() + (get_ui8() << 8) + (get_ui8() << 16) + (get_ui8() << 24); }

//----------------------------------------------------------------------
// 指定場所の 8 bit 返す
uint8_t VGM::get_ui8_at(uint32_t p) { return vgmData[p]; }

int8_t VGM::get_i8_at(uint32_t p) { return (int8_t)vgmData[p]; }

//----------------------------------------------------------------------
// 指定場所の 16 bit 返す
uint16_t VGM::get_ui16_at(uint32_t p) { return (uint32_t(vgmData[p])) + (uint32_t(vgmData[p + 1]) << 8); }

//----------------------------------------------------------------------
// 指定場所の 24 bit 返す
uint32_t VGM::get_ui24_at(uint32_t p) {
  return (uint32_t(vgmData[p])) + (uint32_t(vgmData[p + 1]) << 8) + (uint32_t(vgmData[p + 2]) << 16);
}
//----------------------------------------------------------------------
// 指定場所の 32 bit 返す
uint32_t VGM::get_ui32_at(uint32_t p) {
  return (uint32_t(vgmData[p])) + (uint32_t(vgmData[p + 1]) << 8) + (uint32_t(vgmData[p + 2]) << 16) +
         (uint32_t(vgmData[p + 3]) << 24);
}

// XGM コマンドの X を返す
uint8_t VGM::getNumWrites(u8_t command) { return command % 0xf; }

//----------------------------------------------------------------------
// VGM処理

void VGM::vgmProcess() {
  // フェードアウト完了
  if (nju72341.fadeOutStatus == FADEOUT_COMPLETED) {
    switch (ndConfig.get(CFG_REPEAT)) {
      case REPEAT_ONE: {
        ndFile.filePlay(0);
        break;
      }
      case REPEAT_FOLDER: {
        ndFile.filePlay(1);
        break;
      }
      case REPEAT_ALL: {
        if (ndFile.getNumFilesinCurrentDir() - 1 == ndFile.currentFile)
          ndFile.dirPlay(1);
        else
          ndFile.filePlay(1);
        break;
      }
    }
    return;
  }

  uint8_t reg;
  uint8_t dat;
  uint8_t command = get_ui8();

  switch (command) {
    case 0xA0:  // AY8910, YM2203 PSG, YM2149, YMZ294D
      reg = get_ui8();
      dat = get_ui8();
      FM.setRegister(reg, dat, 0);
      break;

    case 0x30:  // SN76489 CHIP 2
      FM.write(get_ui8(), 2, freq[chipSlot[CHIP_SN76489_1]]);

      break;

    case 0x50:  // SN76489 CHIP 1
      FM.write(get_ui8(), 1, freq[chipSlot[CHIP_SN76489_0]]);

      break;

    case 0x52:  // YM2612 port 0, write value dd to register aa
      reg = get_ui8();
      dat = get_ui8();
      FM.setYM2612(0, reg, dat, 0);
      break;

    case 0x53:  // YM2612 port 1, write value dd to register aa
      reg = get_ui8();
      dat = get_ui8();
      FM.setYM2612(1, reg, dat, 0);
      break;

    case 0x54:  // YM2151
    case 0xa4:
      reg = get_ui8();
      dat = get_ui8();
      FM.setRegisterOPM(reg, dat, 0);
      break;

    case 0x55:  // YM2203_0
      reg = get_ui8();
      dat = get_ui8();
      FM.setRegister(reg, dat, 0);
      break;

    case 0xA5:  // YM2203_1
      reg = get_ui8();
      dat = get_ui8();
      FM.setRegister(reg, dat, 1);
      break;

    case 0x5A:  // YM3812
      reg = get_ui8();
      dat = get_ui8();
      FM.setRegisterOPL3(0, reg, dat, 1);
      break;

    case 0x5E:  // YMF262 Port 0
      reg = get_ui8();
      dat = get_ui8();
      FM.setRegisterOPL3(0, reg, dat, 1);
      break;

    case 0x5F:  // YMF262 Port 1
      reg = get_ui8();
      dat = get_ui8();
      FM.setRegisterOPL3(1, reg, dat, 1);
      break;

    // Wait n samples, n can range from 0 to 65535 (approx 1.49 seconds)
    case 0x61: {
      u16_t w = get_ui16();
      _vgmDelay += w * ONE_CYCLE;
      _vgmSamples += w;
      break;
    }

    // wait 735 samples (60th of a second)
    case 0x62:
      _vgmDelay += 735 * ONE_CYCLE;
      _vgmSamples += 735;
      break;

    // wait 882 samples (50th of a second)
    case 0x63:
      _vgmDelay += 882 * ONE_CYCLE;
      _vgmSamples += 882;
      break;

    case 0x66:
      if (!loopOffset || ndConfig.get(CFG_FADEOUT) == FO_0) {  // ループしない曲
        switch (ndConfig.get(CFG_REPEAT)) {
          case REPEAT_ONE: {
            ndFile.filePlay(0);
            break;
          }
          case REPEAT_FOLDER: {
            ndFile.filePlay(1);
            break;
          }
          case REPEAT_ALL: {
            if (ndFile.getNumFilesinCurrentDir() - 1 == ndFile.currentFile)
              ndFile.dirPlay(1);
            else
              ndFile.filePlay(1);
            break;
          }
        }
      } else {
        _vgmLoop++;
        if (_vgmLoop == ndConfig.get(CFG_NUM_LOOP) &&
            ndConfig.get(CFG_NUM_LOOP) != LOOP_INIFITE) {  //   フェードアウトON
          nju72341.startFadeout();
        }

        _pos = loopOffset + 0x1C;  // ループする曲
      }
      break;

    case 0x67:
      get_ui8();           // 0x66
      get_ui8();           // 0x00 data type
      _pos += get_ui32();  // size of data, in bytes
      break;

    case 0x70 ... 0x7f:
      _vgmDelay += ((command & 15) + 1) * ONE_CYCLE;
      _vgmSamples += (command & 15) + 1;
      break;

    case 0x80 ... 0x8f:
      FM.setYM2612(0, 0x2a, vgmData[_pcmpos++], 0);
      _vgmDelay += (command & 15) * ONE_CYCLE - 19800;
      _vgmSamples += (command & 15);
      break;

    case 0x90:
      // Setup Stream Control
      // get_vgm_ui32();
      break;
    case 0x91:
      // Set Stream Data
      // get_vgm_ui32();
      break;
    case 0x92:
      // Set Stream Frequency
      // get_vgm_ui8();
      // get_vgm_ui32();
      break;
    case 0x93:
      // Start Stream
      // get_vgm_ui8();
      // pcmpos = get_vgm_ui32();
      // get_vgm_ui8();
      // pcmlength = get_vgm_ui32();
      // pcmoffset = 0;
      // 8KHz
      // pause_pcm(5.5125);
      break;
    case 0xe0:
      _pcmpos = 0x47 + get_ui32();
      //_vgmSamples++;
      break;
    default:
      ESP_LOGI("Unknown VGM Command: %0.2X\n", command);
      break;
  }

  if (_vgmDelay >= 1000) {
    ets_delay_us(_vgmDelay / 1000);
    _vgmDelay = _vgmDelay % 1000;
    const uint64_t vgmTime = _vgmSamples * 22.67573696145125f;
    const uint64_t realTime = micros() - _vgmStart;
    if (realTime > vgmTime) _vgmDelay -= (realTime - vgmTime);
  }
}

//---------------------------------------------------------------------
// xgm (not fully tested, only supports v1.1 so far)
// XGM setup
bool VGM::XGMReady() {
  vgmLoaded = false;
  xgmLoaded = false;
  _pos = 0;

  _vgmSamples = 0;
  _vgmLoop = 0;
  _xgmFrame = 0;

  _xgmWaitUntil = 0;

  XGMSampleAddressTable.clear();
  XGMSampleSizeTable.clear();
  XGMSampleAddressTable.push_back(0);
  XGMSampleSizeTable.push_back(0);
  _xgmSampleOn[0] = false;
  _xgmSampleOn[1] = false;
  _xgmSampleOn[2] = false;
  _xgmSampleOn[3] = false;
  _xgmPriorities[0] = 0;
  _xgmPriorities[1] = 0;
  _xgmPriorities[2] = 0;
  _xgmPriorities[3] = 0;

  // XGM 1 ident
  if (get_ui32_at(0) != 0x204d4758) {
    Serial.println("ERROR: XGM ファイル解析失敗");
    xgmLoaded = false;
    return false;
  }
  XGMVersion = 1;

  // Sample id table
  _pos = 0x4;
  for (int i = 0; i < 63; i++) {
    XGMSampleAddressTable.push_back(get_ui16() * 256 + 0x104);
    XGMSampleSizeTable.push_back(get_ui16() * 256);
    // Serial.printf("Sample Address: 0x%x, Size: 0x%x\n", XGMSampleAddressTable[i] << 8, XGMSampleSizeTable[i] << 8);
  }

  // Sample data block size = SLEN
  XGM_SLEN = get_ui16() << 8;

  // Version
  // Serial.printf("XGM Version: %d\n", get_ui8());

  // NTSC/PAL, GD3, MultiTrack
  XGM_FLAGS = get_ui8_at(0x103);
  // PAL
  // GD3
  // Ignore MultiTrack
  _xgmIsNTSC = (XGM_FLAGS & 0b1 == 0);

  // Music data block size = MLEN
  XGM_MLEN = get_ui32_at(0x104 + XGM_SLEN);
  // Serial.printf("MLEN: %x\n", XGM_MLEN);

  // Music data block position
  _pos = 0x108 + XGM_SLEN;
  // Serial.printf("0x108+ SLEN: %x\n", _pos);

  // GD3
  // Serial.printf("XGM_FLAGS: %d\n", XGM_FLAGS);
  gd3Offset = 0x108 + XGM_SLEN + XGM_MLEN;
  if (XGM_FLAGS & 0b10) {
    _parseGD3();
  } else {
    gd3.trackEn = ndFile.files[ndFile.currentDir][ndFile.currentFile];
    gd3.trackJp = ndFile.files[ndFile.currentDir][ndFile.currentFile];
    gd3.gameEn = "(No GD3 info)";
    gd3.gameJp = "(GD3情報なし)";
    gd3.systemEn = "";
    gd3.systemJp = "";
    gd3.authorEn = "";
    gd3.authorJp = "";
    gd3.date = "";
  }

  // FM PCM 構成は固定
  freq[0] = normalizeFreq(7670453, CHIP_YM2612);
  freq[1] = normalizeFreq(3579545, CHIP_SN76489_0);
  SI5351.setFreq(freq[0], 0);
  SI5351.setFreq(freq[1], 1);
  SI5351.enableOutputs(true);

  // PCM DAC Select
  FM.setYM2612(0, 0x2b, 0b10000000, 0);

  // 表示
  String chip[2] = {"", ""};
  int c = 0;

  if (freq[0] != 0) {
    char buf[7];
    dtostrf((double)freq[0] / 1000000.0, 1, 4, buf);
    chip[c++] = CHIP_LABEL[CHIP0] + " @ " + String(buf).substring(0, 5) + " MHz";
  }

  if (freq[1] != 0) {
    char buf[7];
    dtostrf((double)freq[1] / 1000000.0, 1, 4, buf);
    chip[c++] = CHIP_LABEL[CHIP1] + " @ " + String(buf).substring(0, 5) + " MHz";
  }

  if (c < 2 && freq[2] != 0) {
    char buf[7];
    dtostrf((double)freq[2] / 1000000.0, 1, 4, buf);
    chip[c++] = CHIP_LABEL[CHIP2] + " @ " + String(buf).substring(0, 5) + " MHz";
  }

  u32_t n = 1 + ndFile.currentFile;  // フォルダ内曲番

  updateDisp({gd3.trackEn, gd3.trackJp, gd3.gameEn, gd3.gameJp, gd3.systemEn, gd3.systemJp, gd3.authorEn, gd3.authorJp,
              gd3.date, chip[0], chip[1], FORMAT_LABEL[vgm.format], 0, n, ndFile.files[ndFile.currentDir].size()});

  xgmLoaded = true;
  _xgmStartTick = micros();

  return true;
}

// XGM 処理 (Experimental)
void VGM::xgmProcess() {
  // フェードアウト完了
  if (nju72341.fadeOutStatus == FADEOUT_COMPLETED) {
    switch (ndConfig.get(CFG_REPEAT)) {
      case REPEAT_ONE: {
        ndFile.filePlay(0);
        break;
      }
      case REPEAT_FOLDER: {
        ndFile.filePlay(1);
        break;
      }
      case REPEAT_ALL: {
        if (ndFile.getNumFilesinCurrentDir() - 1 == ndFile.currentFile)
          ndFile.dirPlay(1);
        else
          ndFile.filePlay(1);
        break;
      }
    }
    return;
  }

  u8_t command = get_ui8();

  switch (command) {
    case 0x00:
      // frame wait
      _xgmFrame++;
      _xgmWaitUntil = _xgmStartTick + _xgmFrame * 16666;  // 60Hz
      _vgmSamples = _xgmFrame * 735;                      // 44100 / 60
      break;

    case 0x10 ... 0x1f:
      for (int i = 0; i < command % 16 + 1; i++) {
        FM.write(get_ui8(), 1, freq[chipSlot[CHIP_SN76489_0]]);
      }
      break;

    case 0x20 ... 0x2f:
      for (int i = 0; i < command % 16 + 1; i++) {
        FM.setYM2612(0, get_ui8(), get_ui8(), 0);
      }
      break;

    case 0x30 ... 0x3f:
      for (int i = 0; i < command % 16 + 1; i++) {
        FM.setYM2612(1, get_ui8(), get_ui8(), 0);
      }
      break;

    case 0x40 ... 0x4f:
      for (int i = 0; i < command % 16 + 1; i++) {
        FM.setYM2612(0, 0x28, get_ui8(), 0);
      }
      break;

    case 0x50 ... 0x5f: {
      // PCM play command:
      u8_t priority = command & 0xc;
      u8_t channel = command & 0x3;
      u8_t sampleID = get_ui8();

      if (_xgmSampleOn[channel] == false || _xgmPriorities[channel] <= priority) {
        if (sampleID == 0) {  // ID 0 は停止
          _xgmSampleOn[channel] = false;
        } else {
          _xgmSampleOn[channel] = true;
        }
        _xgmPriorities[channel] = priority;
        _xgmSamplePos[channel] = 0;
        _xgmSampleId[channel] = sampleID;
      }
      break;
    }

    case 0x7e: {
      // Loop command, used for music looping sequence
      _vgmLoop++;
      Serial.printf("loops: %d\n", _vgmLoop);
      if (_vgmLoop == ndConfig.get(CFG_NUM_LOOP) && ndConfig.get(CFG_NUM_LOOP) != LOOP_INIFITE) {  //   フェードアウトON
        nju72341.startFadeout();
      }

      _pos = 0x108 + XGM_SLEN + get_ui24();  // ループ位置
      break;
    }

    case 0x7f: {
      // End command (end of music data).
      switch (ndConfig.get(CFG_REPEAT)) {
        case REPEAT_ONE: {
          ndFile.filePlay(0);
          break;
        }
        case REPEAT_FOLDER: {
          ndFile.filePlay(1);
          break;
        }
        case REPEAT_ALL: {
          if (ndFile.getNumFilesinCurrentDir() - 1 == ndFile.currentFile)
            ndFile.dirPlay(1);
          else
            ndFile.filePlay(1);
          break;
        }
      }
    }
  }

  // PCM Stream mixing
  while (_xgmWaitUntil > micros()) {
    int16_t samp = 0;
    bool sampFlag = false;
    for (int i = 0; i < 4; i++) {
      if (_xgmSampleOn[i]) {
        samp += (int8_t)get_ui8_at(XGMSampleAddressTable[_xgmSampleId[i]] + _xgmSamplePos[i]++);
        sampFlag = true;
        if (_xgmSamplePos[i] >= XGMSampleSizeTable[_xgmSampleId[i]]) {
          _xgmSampleOn[i] = false;
        }
      }
    }
    if (sampFlag) {
      if (samp > INT8_MAX) {
        samp = INT8_MAX;
      } else if (samp < INT8_MIN)
        samp = INT8_MIN;
      samp += 128;
      FM.setYM2612DAC(samp, 0);
    }
    ets_delay_us(68);
  }
}

uint64_t VGM::getCurrentTime() {
  if (_vgmSamples >= 264600000) _vgmSamples = 0;
  return _vgmSamples / 44100;
}

VGM vgm = VGM();
