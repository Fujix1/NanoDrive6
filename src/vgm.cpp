
#include "vgm.h"

#include <cassert>
#include <codecvt>
#include <locale>
#include <string>

#include "file.h"
#include "fm.h"

#define ONE_CYCLE \
  22675.737f  // 22.67573696145125 us
              // 1 / 44100 * 1 000 000

//---------------------------------------------------------------------
static std::string wstringToUTF8(const std::wstring& src) {
  std::wstring_convert<std::codecvt_utf8<wchar_t> > converter;
  return converter.to_bytes(src);
}

//---------------------------------------------------------------------
static u32_t gd3p;
void parseGD3(t_gd3* gd3, u32_t offset) {}

//---------------------------------------------------------------------
// VGM クラス
VGM::VGM() {
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
  ndFile.pos = 0;

  format = FORMAT_UNKNOWN;

  freq[0] = SI5351_UNDEFINED;
  freq[1] = SI5351_UNDEFINED;
  freq[2] = SI5351_UNDEFINED;

  _vgmLoop = 0;
  _vgmSamples = 0;
  _vgmRealSamples = 0;

  // VGM ident
  if (ndFile.get_ui32_at(0) != 0x206d6756) {
    Serial.println("ERROR: VGMファイル解析失敗");
    vgmLoaded = false;
    return false;
  }

  format = FORMAT_VGM;

  // version
  version = ndFile.get_ui32_at(8);
  // total # samples
  // totalSamples = get_ui32_at(0x18);

  // loop offset
  loopOffset = ndFile.get_ui32_at(0x1c);
  // vg3 offset
  gd3Offset = ndFile.get_ui32_at(0x14) + 0x14;

  u32_t gd3Size = ndFile.get_ui32_at(gd3Offset + 0x8);

  // data offset
  dataOffset = (version >= 0x150) ? ndFile.get_ui32_at(0x34) + 0x34 : 0x40;
  ndFile.pos = dataOffset;

  // Setup Clocks
  u32_t sn76489_clock = ndFile.get_ui32_at(0x0c);
  if (sn76489_clock) {
    if (CHIP0 == CHIP_SN76489_0) {
      freq[CHIP0_CLOCK] = normalizeFreq(sn76489_clock, CHIP_SN76489_0);
    } else if (CHIP1 == CHIP_SN76489_0) {
      freq[CHIP1_CLOCK] = normalizeFreq(sn76489_clock, CHIP_SN76489_0);
    }

    // デュアルSN76489
    if ((sn76489_clock & (1 << 30))) {
      // Serial.printf("DUAL, version: %x\n", version);

      if (version < 0x170) {
        freq[2] = normalizeFreq(sn76489_clock, CHIP_SN76489_0);
      } else {
        u32_t headerSize = ndFile.get_ui32_at(0xbc);
        u32_t chpClockOffset = ndFile.get_ui32_at(0xbc + headerSize);
        u8_t entryCount = ndFile.get_ui8_at(0xbc + headerSize + chpClockOffset);
        u8_t chipID = ndFile.get_ui8_at(0xbc + headerSize + chpClockOffset + 1);
        u32_t clock = ndFile.get_ui32_at(0xbc + headerSize + chpClockOffset + 2);

        // Serial.printf("chipId: %d, freq: %x\n", chipID, clock);

        if (chipID == 0) {
          freq[1] = normalizeFreq(sn76489_clock, CHIP_SN76489_0);
          freq[2] = normalizeFreq(clock, CHIP_SN76489_0);
        } else if (chipID == 1) {
          freq[2] = normalizeFreq(clock, CHIP_SN76489_0);
        }
      }
    }

    // SN76489 フラグ
    SN76489_Freq0is0X400 = false;
    if (version >= 0x151) {
      SN76489_Freq0is0X400 = ndFile.get_ui8_at(0x2b) & 0x0001;
    }
  }

  u32_t ym2413_clock = ndFile.get_ui32_at(0x10);
  if (ym2413_clock) {
    if (CHIP0 == CHIP_YM2413) {
      freq[CHIP0_CLOCK] = normalizeFreq(ym2413_clock, CHIP_YM2413);
    }
    if (CHIP1 == CHIP_YM2413) {
      freq[CHIP1_CLOCK] = normalizeFreq(ym2413_clock, CHIP_YM2413);
    }
  }

  u32_t ym2612_clock = ndFile.get_ui32_at(0x2c);
  if (ym2612_clock) {
    if (CHIP0 == CHIP_YM2612) {
      freq[CHIP0_CLOCK] = normalizeFreq(ym2612_clock, CHIP_YM2612);
    } else if (CHIP1 == CHIP_YM2612) {
      freq[CHIP1_CLOCK] = normalizeFreq(ym2612_clock, CHIP_YM2612);
    }
  }

  u32_t ay8910_clock = (version >= 0x151 && dataOffset >= 0x78) ? ndFile.get_ui32_at(0x74) : 0;
  if (ay8910_clock) {
    if (CHIP0 == CHIP_AY8910) {
      freq[CHIP0_CLOCK] = normalizeFreq(ay8910_clock, CHIP_AY8910);
    } else if (CHIP1 == CHIP_AY8910) {
      freq[CHIP1_CLOCK] = normalizeFreq(ay8910_clock, CHIP_AY8910);
    }
    if (CHIP0 == CHIP_YM2203_0) {
      freq[CHIP0_CLOCK] = normalizeFreq(ay8910_clock, CHIP_AY8910);
    }
  }

  u32_t ym2203_clock = (version >= 0x151 && dataOffset >= 0x78) ? ndFile.get_ui32_at(0x44) : 0;
  if (ym2203_clock) {
    if (ym2203_clock & 0x40000000) {  // check the second chip
      if (CHIP0 == CHIP_YM2203_0) {
        freq[CHIP0_CLOCK] = normalizeFreq(ym2203_clock, CHIP_YM2203_0);
      }
      if (CHIP1 == CHIP_YM2203_1) {
        freq[CHIP1_CLOCK] = normalizeFreq(ym2203_clock, CHIP_YM2203_1);
      }
    } else {
      if (CHIP0 == CHIP_YM2203_0) {
        freq[CHIP0_CLOCK] = normalizeFreq(ym2203_clock, CHIP_YM2203_0);
      } else if (CHIP1 == CHIP_YM2203_0) {
        freq[CHIP1_CLOCK] = normalizeFreq(ym2203_clock, CHIP_YM2203_0);
      }

      // Use YM2612 as YM2203
      if (CHIP0 == CHIP_YM2612) {
        freq[CHIP0_CLOCK] = normalizeFreq(ym2203_clock, CHIP_YM2612);
      } else if (CHIP1 == CHIP_YM2612) {
        freq[CHIP1_CLOCK] = normalizeFreq(ym2203_clock, CHIP_YM2612);
      }
      // Use YM2610 as YM2203
      if (CHIP0 == CHIP_YM2610) {
        freq[CHIP0_CLOCK] = normalizeFreq(ym2203_clock, CHIP_YM2610);
      } else if (CHIP1 == CHIP_YM2610) {
        freq[CHIP1_CLOCK] = normalizeFreq(ym2203_clock, CHIP_YM2612);
      }
    }
  }

  u32_t ym2151_clock = ndFile.get_ui32_at(0x30);
  if (ym2151_clock) {
    if (CHIP0 == CHIP_YM2151) {
      freq[CHIP0_CLOCK] = normalizeFreq(ym2151_clock, CHIP_YM2151);
    }
    if (CHIP1 == CHIP_YM2151) {
      freq[CHIP1_CLOCK] = normalizeFreq(ym2151_clock, CHIP_YM2151);
    }
  }

  u32_t ym3812_clock = ndFile.get_ui32_at(0x50);
  if (ym3812_clock) {
    if (CHIP0 == CHIP_YM3812) {
      freq[CHIP0_CLOCK] = normalizeFreq(ym3812_clock, CHIP_YM3812);
    }
    if (CHIP1 == CHIP_YM3812) {
      freq[CHIP1_CLOCK] = normalizeFreq(ym3812_clock, CHIP_YM3812);
    }
    if (CHIP0 == CHIP_YMF262) {
      freq[CHIP0_CLOCK] = normalizeFreq(ym3812_clock, CHIP_YM3812);
    }
    if (CHIP0 == CHIP_YMF262) {
      freq[CHIP1_CLOCK] = normalizeFreq(ym3812_clock, CHIP_YM3812);
    }
  }

  u32_t ymf262_clock = ndFile.get_ui32_at(0x5c);
  if (ymf262_clock) {
    if (CHIP0 == CHIP_YMF262) {
      freq[CHIP0_CLOCK] = normalizeFreq(ymf262_clock, CHIP_YMF262);
    }
    if (CHIP1 == CHIP_YMF262) {
      freq[CHIP1_CLOCK] = normalizeFreq(ymf262_clock, CHIP_YMF262);
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
  _parseGD3(gd3Offset);

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

  _vgmStart = micros64();
  return true;
}

// GD3タグをパース
void VGM::_parseGD3(uint32_t pos) {
  _gd3p = pos + 12;

  gd3.trackEn = _digGD3();
  gd3.trackJp = _digGD3();
  gd3.gameEn = _digGD3();
  gd3.gameJp = _digGD3();
  gd3.systemEn = _digGD3();
  gd3.systemJp = _digGD3();
  gd3.authorEn = _digGD3();
  gd3.authorJp = _digGD3();
  gd3.date = _digGD3();
  gd3.converted = _digGD3();
  // gd3.notes = _digGD3();

  if (gd3.trackJp == "") gd3.trackJp = gd3.trackEn;
  if (gd3.gameJp == "") gd3.gameJp = gd3.gameEn;
  if (gd3.systemJp == "") gd3.systemJp = gd3.systemEn;
  if (gd3.authorJp == "") gd3.authorJp = gd3.authorEn;
}

String VGM::_digGD3() {
  std::wstring wst;
  wst.clear();
  while (ndFile.data[_gd3p] != 0 || ndFile.data[_gd3p + 1] != 0) {
    wst += (char16_t)((ndFile.data[_gd3p + 1] << 8) | ndFile.data[_gd3p]);
    _gd3p += 2;
  }
  _gd3p += 2;
  std::string sst = wstringToUTF8(wst);
  return (String)sst.c_str();
}

void VGM::_resetGD3() {
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

//----------------------------------------------------------------------
// 周波数の実際の値を設定
si5351Freq_t VGM::normalizeFreq(u32_t freq, t_chip chip) {
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
    case CHIP_YM2413: {
      switch (freq) {
        case 2000000:
          return SI5351_2000;
          break;
        case 3579000 ... 3580000:  // 3.579MHz
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
    case CHIP_YM2203_0:
    case CHIP_YM2203_1: {
      switch (freq) {
        case 1500000:     // 1.5MHz
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
        case 3579000 ... 3580000:  // 3.579MHz
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
        case 3579000 ... 3580000:  // 3.579MHz
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
        case 3579000 ... 3580000:  // YM2203 @ 3.579MHz
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
// VGM処理

void VGM::vgmProcess() {
  // フェードアウト完了
  if (nju72341.fadeOutStatus == FADEOUT_COMPLETED) {
    endProcedure();
    return;
  }

  while (_vgmSamples <= _vgmRealSamples) {
    vgmProcessMain();
  }

  _vgmRealSamples = _vgmSamples;
  _vgmWaitUntil = _vgmStart + _vgmRealSamples * 22.67573696145125;

  while (_vgmWaitUntil - 22 > micros64()) {
    ets_delay_us(22);
  }
}

void VGM::vgmProcessMain() {
  u8_t reg;
  u8_t dat;
  u8_t command = ndFile.get_ui8();

  switch (command) {
#ifdef USE_AY8910
    case 0xA0:  // AY8910, YM2203 PSG, YM2149, YMZ294D
      reg = ndFile.get_ui8();
      dat = ndFile.get_ui8();
      FM.setRegister(reg, dat, 0);
      break;
#endif

#ifdef USE_SN76489
    case 0x30:  // SN76489 CHIP 2
      if (SN76489_Freq0is0X400) {
        FM.writeRaw(ndFile.get_ui8(), 2, freq[chipSlot[CHIP_SN76489_1]]);
      } else {
        FM.write(ndFile.get_ui8(), 2, freq[chipSlot[CHIP_SN76489_0]]);
      }
      break;

    case 0x50:  // SN76489 CHIP 1
      // WORKAROUND FOR COMMAND TO UNDEFINED SN CHIP
      // Sonic & Knuckles 30th song
      if (freq[chipSlot[CHIP_SN76489_0]] != SI5351_UNDEFINED) {
        if (SN76489_Freq0is0X400) {
          FM.writeRaw(ndFile.get_ui8(), 1, freq[chipSlot[CHIP_SN76489_0]]);
        } else {
          FM.write(ndFile.get_ui8(), 1, freq[chipSlot[CHIP_SN76489_0]]);
        }
      }
      break;
#endif

#ifdef USE_YM2413
    case 0x51:
      reg = ndFile.get_ui8();
      dat = ndFile.get_ui8();
      FM.setRegisterOPLL(reg, dat, 1);
      break;
#endif

#ifdef USE_YM2612
    case 0x52:  // YM2612 port 0, write value dd to register aa
      reg = ndFile.get_ui8();
      dat = ndFile.get_ui8();
      if ((reg >= 0x30 && reg <= 0xB6) || reg == 0x22 || reg == 0x27 || reg == 0x28 || reg == 0x2A || reg == 0x2B) {
        FM.setYM2612(0, reg, dat, 0);
      }
      break;

    case 0x53:  // YM2612 port 1, write value dd to register aa
      reg = ndFile.get_ui8();
      dat = ndFile.get_ui8();
      if (reg >= 0x30 && reg <= 0xB6) {
        FM.setYM2612(1, reg, dat, 0);
      }
      break;
#endif

#ifdef USE_YM2151
    case 0x54:  // YM2151
    case 0xa4:
      reg = ndFile.get_ui8();
      dat = ndFile.get_ui8();
      if (reg != 0x10 || reg != 0x11) {  // タイマー設定は無視
        FM.setRegisterOPM(reg, dat, 0);
      }
      break;
#endif

#ifdef USE_YM2203_0
    case 0x55:  // YM2203_0
      reg = ndFile.get_ui8();
      dat = ndFile.get_ui8();
      FM.setRegister(reg, dat, 0);
      break;
#endif

#ifdef USE_YM2203_1
    case 0xA5:  // YM2203_1
      reg = ndFile.get_ui8();
      dat = ndFile.get_ui8();
      FM.setRegister(reg, dat, 1);
      break;
#endif

#ifdef USE_YM3812
    case 0x5A:  // YM3812
      reg = ndFile.get_ui8();
      dat = ndFile.get_ui8();
      FM.setRegister(reg, dat, 1);
      break;

#endif

#ifdef USE_YMF262
    case 0x5A:  // YM3812
    case 0x5E:  // YMF262 Port 0
      reg = ndFile.get_ui8();
      dat = ndFile.get_ui8();
      FM.setRegisterOPL3(0, reg, dat, 1);
      break;
    case 0x5F:  // YMF262 Port 1
      reg = ndFile.get_ui8();
      dat = ndFile.get_ui8();
      FM.setRegisterOPL3(1, reg, dat, 1);
      break;
#endif

    // Wait n samples, n can range from 0 to 65535 (approx 1.49 seconds)
    case 0x61: {
      u16_t w = ndFile.get_ui16();
      _vgmSamples += w;
      break;
    }

    // wait 735 samples (60th of a second)
    case 0x62:
      _vgmSamples += 735;
      break;

    // wait 882 samples (50th of a second)
    case 0x63:
      _vgmSamples += 882;
      break;

    case 0x66:
      if (!loopOffset || ndConfig.get(CFG_FADEOUT) == FO_0) {  // ループしない曲
        endProcedure();
        return;
      } else {
        _vgmLoop++;
        if (_vgmLoop == ndConfig.get(CFG_NUM_LOOP) &&
            ndConfig.get(CFG_NUM_LOOP) != LOOP_INIFITE) {  //   フェードアウトON
          nju72341.startFadeout();
        }

        ndFile.pos = loopOffset + 0x1C;  // ループする曲
      }
      break;

    case 0x67:
      ndFile.get_ui8();                 // 0x66
      ndFile.get_ui8();                 // 0x00 data type
      ndFile.pos += ndFile.get_ui32();  // size of data, in bytes
      break;

    case 0x70 ... 0x7f:
      _vgmSamples += (command & 15) + 1;
      break;

    case 0x80 ... 0x8f:
      FM.setYM2612DAC(ndFile.data[_pcmpos++], 0);
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
      _pcmpos = 0x47 + ndFile.get_ui32();
      break;
    default:
      ESP_LOGI("Unknown VGM Command: %0.2X\n", command);
      break;
  }
}

//---------------------------------------------------------------------
// xgm
// XGM setup
bool VGM::XGMReady() {
  bool hasGd3 = false;

  vgmLoaded = false;
  xgmLoaded = false;
  ndFile.pos = 0;

  _vgmSamples = 0;
  _vgmLoop = 0;
  _xgmFrame = 0;
  _xgmYMSNFrame = 0;

  // XGM2
  _xgmYMFrame = 0;
  _xgmPSGFrame = 0;

  _xgmWaitUntil = 0;

  XGMSampleAddressTable.clear();
  XGMSampleSizeTable.clear();
  XGMSampleAddressTable.push_back(0);
  XGMSampleSizeTable.push_back(0);

  for (int i = 0; i < XGM1_MAX_PCM_CH; i++) {
    _xgmSampleOn[i] = false;
    _xgmPriorities[i] = 0;
  }

  // XGM ident
  if (ndFile.get_ui32_at(0) == 0x204d4758) {
    XGMVersion = 1;
    format = FORMAT_XGM;
    Serial.println("XGM Version 1.1");
  } else if (ndFile.get_ui32_at(0) == 0x324d4758) {
    XGMVersion = 2;
    format = FORMAT_XGM2;
    Serial.println("XGM Version 2");
  } else {
    Serial.println("ERROR: XGM ファイル解析失敗");
    format = FORMAT_UNKNOWN;
    u32_t n = 1 + ndFile.currentFile;
    updateDisp({"Bad XGM file ident", "XGMファイル解析失敗", "", "", "--", "--", "--", "--", "--", "", "",
                FORMAT_LABEL[vgm.format], 0, n, ndFile.files[ndFile.currentDir].size()});
    Serial.println("ERROR: Bad XGM file ident.");

    xgmLoaded = false;
    return false;
  }

  // XGM 1.1 / 2 information
  switch (XGMVersion) {
    case 1: {
      // Sample id table
      ndFile.pos = 0x4;
      for (int i = 0; i < 63; i++) {
        XGMSampleAddressTable.push_back(ndFile.get_ui16() * 256 + 0x104);
        XGMSampleSizeTable.push_back(ndFile.get_ui16() * 256);
      }

      // Sample data block size = SLEN
      XGM_SLEN = ndFile.get_ui16() << 8;

      // Version
      // Serial.printf("XGM Version: %d\n", get_ui8());

      // NTSC/PAL, GD3, MultiTrack
      XGM_FLAGS = ndFile.get_ui8_at(0x103);
      // PAL
      // GD3
      // Ignore MultiTrack
      _xgmIsNTSC = (XGM_FLAGS & 0b1 == 0);
      hasGd3 = XGM_FLAGS & 0b10;

      // Music data block size = MLEN
      XGM_MLEN = ndFile.get_ui32_at(0x104 + XGM_SLEN);
      // Serial.printf("MLEN: %x\n", XGM_MLEN);

      // Music data block position
      ndFile.pos = 0x108 + XGM_SLEN;
      // Serial.printf("0x108+ SLEN: %x\n", _pos);

      gd3Offset = 0x108 + XGM_SLEN + XGM_MLEN;

      break;
    }
    case 2: {
      ndFile.pos = 0x4;
      // Format description
      XGM_FLAGS = ndFile.get_ui8_at(0x0005);
      _xgmIsNTSC = (XGM_FLAGS & 0b0001);
      // ignore multi track
      // packed FM / PSG / GD3
      hasGd3 = XGM_FLAGS & 0b100;

      // SLEN: Sample data bloc size / 256 (ex: $0200 means 512*256 = 131072 bytes)
      XGM_SLEN = ndFile.get_ui16_at(0x0006) << 8;
      Serial.printf("XGM_SLEN: %x\n", XGM_SLEN);

      // FMLEN: FM music data block size / 256 (ex: $0040 means 64*256 = 16384 bytes)
      XGM_FMLEN = ndFile.get_ui16_at(0x0008) << 8;
      Serial.printf("XGM_FMLEN: %x\n", XGM_FMLEN);

      // PSGLEN: PSG music data block size / 256 (ex: $0020 means 32*256 = 8192 bytes)
      XGM_PSGLEN = ndFile.get_ui16_at(0x000a) << 8;
      Serial.printf("XGM_PSGLEN: %x\n", XGM_PSGLEN);

      // SID: sample id table
      ndFile.pos = 0x000c;

      for (int i = 0; i < 124; i++) {
        u16_t value = ndFile.get_ui16();
        if (value == 0xffff) {
          XGMSampleAddressTable.push_back(0);
        } else {
          XGMSampleAddressTable.push_back(value * 256 + 0x104);
        }
      }

      for (int i = 1; i < 124; i++) {
        if (XGMSampleAddressTable[i] == 0) {
          XGMSampleSizeTable.push_back(0);
        } else {
          XGMSampleSizeTable.push_back(XGMSampleAddressTable[i + 1] - XGMSampleAddressTable[i]);

          // Last sample size
          if (XGMSampleSizeTable[i] > XGM_SLEN - 0x104) {
            XGMSampleSizeTable[i] = XGM_SLEN - XGMSampleAddressTable[i] + 0x104;
          }
        }
      }

      for (int i = 0; i < 123; i++) {
        // Serial.printf("Sample: id %d, add %x, size %x\n", i, XGMSampleAddressTable[i], XGMSampleSizeTable[i]);
      }
      gd3Offset = 0x104 + XGM_SLEN + XGM_FMLEN + XGM_PSGLEN;

      _xgm2_ym_offset = 0x104 + XGM_SLEN;
      _xgm2_psg_offset = 0x104 + XGM_SLEN + XGM_FMLEN;
      _xgm2_ym_pos = _xgm2_ym_offset;
      _xgm2_psg_pos = _xgm2_psg_offset;

      break;
    }
    default: {
      Serial.println("Unexpected XGM version.");
      return false;
    }
  }

  // GD3
  if (hasGd3) {
    _parseGD3(gd3Offset);
  } else {
    _resetGD3();
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

  if (freq[0] != SI5351_UNDEFINED) {
    char buf[7];
    dtostrf((double)freq[0] / 1000000.0, 1, 4, buf);
    chip[c++] = CHIP_LABEL[CHIP0] + " @ " + String(buf).substring(0, 5) + " MHz";
  }

  if (freq[1] != SI5351_UNDEFINED) {
    char buf[7];
    dtostrf((double)freq[1] / 1000000.0, 1, 4, buf);
    chip[c++] = CHIP_LABEL[CHIP1] + " @ " + String(buf).substring(0, 5) + " MHz";
  }

  if (c < 2 && freq[2] != SI5351_UNDEFINED) {
    char buf[7];
    dtostrf((double)freq[2] / 1000000.0, 1, 4, buf);
    chip[c++] = CHIP_LABEL[CHIP2] + " @ " + String(buf).substring(0, 5) + " MHz";
  }

  u32_t n = 1 + ndFile.currentFile;  // フォルダ内曲番

  updateDisp({gd3.trackEn, gd3.trackJp, gd3.gameEn, gd3.gameJp, gd3.systemEn, gd3.systemJp, gd3.authorEn, gd3.authorJp,
              gd3.date, chip[0], chip[1], FORMAT_LABEL[vgm.format], 0, n, ndFile.files[ndFile.currentDir].size()});

  xgmLoaded = true;
  _xgmStartTick = micros64();

  return true;
}

//---------------------------------------------------------------
// XGM1 処理
void VGM::xgmProcess() {
  // フェードアウト完了
  if (nju72341.fadeOutStatus == FADEOUT_COMPLETED) {
    endProcedure();
    return;
  }

  while (_xgmYMSNFrame <= _xgmFrame) {
    if (_xgm1ProcessYMSN()) {
      endProcedure();
      return;
    }
  }

  _xgmFrame = _xgmYMSNFrame;
  _xgmWaitUntil = _xgmStartTick + _xgmYMSNFrame * 16666;  // 60Hz
  _vgmSamples = _xgmYMSNFrame * 735;                      // 44100 / 60

  // PCM Stream mixing
  while (_xgmWaitUntil - XGM1_PCM_DELAY > micros()) {
    _xgm1ProcessPCM();
    ets_delay_us(XGM1_PCM_DELAY);
  }
}

void VGM::_xgm1ProcessPCM() {
  int16_t samp = 0;
  bool sampFlag = false;
  for (int i = 0; i < XGM1_MAX_PCM_CH; i++) {
    if (_xgmSampleOn[i]) {
      samp += (s8_t)ndFile.get_ui8_at(XGMSampleAddressTable[_xgmSampleId[i]] + _xgmSamplePos[i]++);
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
}

bool VGM::_xgm1ProcessYMSN() {
  u8_t command = ndFile.get_ui8();

  switch (command) {
    case 0x00:
      // frame wait
      _xgmYMSNFrame++;
      break;

    case 0x10 ... 0x1f:
      for (int i = 0; i < command % 16 + 1; i++) {
        FM.write(ndFile.get_ui8(), 1, freq[chipSlot[CHIP_SN76489_0]]);
      }
      break;

    case 0x20 ... 0x2f:
      for (int i = 0; i < command % 16 + 1; i++) {
        FM.setYM2612(0, ndFile.get_ui8(), ndFile.get_ui8(), 0);
      }
      break;

    case 0x30 ... 0x3f:
      for (int i = 0; i < command % 16 + 1; i++) {
        FM.setYM2612(1, ndFile.get_ui8(), ndFile.get_ui8(), 0);
      }
      break;

    case 0x40 ... 0x4f:
      for (int i = 0; i < command % 16 + 1; i++) {
        FM.setYM2612(0, 0x28, ndFile.get_ui8(), 0);
      }
      break;

    case 0x50 ... 0x5f: {
      // PCM play command:
      u8_t priority = command & 0xc;
      u8_t channel = command & 0x3;
      u8_t sampleID = ndFile.get_ui8();

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
      ndFile.pos = 0x108 + XGM_SLEN + ndFile.get_ui24();  // ループ位置
      break;
    }

    case 0x7f: {
      // End command (end of music data).
      return true;
    }
  }

  return false;
}

//---------------------------------------------------------------
// XGM2 処理
void VGM::xgm2Process() {
  // フェードアウト完了
  if (nju72341.fadeOutStatus == FADEOUT_COMPLETED) {
    endProcedure();
    return;
  }

  while (_xgmYMFrame <= _xgmFrame) {
    if (_xgm2ProcessYM()) {
      endProcedure();
      return;
    };
  }
  while (_xgmPSGFrame <= _xgmFrame) {
    if (_xgm2ProcessSN()) {
      endProcedure();
      return;
    };
  }

  _xgmFrame = (_xgmPSGFrame < _xgmYMFrame) ? _xgmPSGFrame : _xgmYMFrame;
  _xgmWaitUntil = _xgmStartTick + _xgmFrame * 16666;  // 60Hz
  _vgmSamples = _xgmFrame * 735;                      // 44100 / 60

  while (_xgmWaitUntil - XGM2_PCM_DELAY >= micros()) {
    _xgm2ProcessPCM();
    ets_delay_us(XGM2_PCM_DELAY);
  }
}

void VGM::_xgm2ProcessPCM() {
  int16_t samp = 0;
  bool sampFlag = false;

  for (int i = 0; i < 3; i++) {
    if (_xgmSampleOn[i]) {
      if (_xgmPCMHalfSent[i] == false) {
        samp += (s8_t)ndFile.get_ui8_at(XGMSampleAddressTable[_xgmSampleId[i]] + _xgmSamplePos[i]++);
        sampFlag = true;

        if (_xgmSamplePos[i] >= XGMSampleSizeTable[_xgmSampleId[i]]) {
          _xgmSampleOn[i] = false;
          _xgmSampleId[i] = 0;
        }
      }
      if (_xgmPCMHalfSpeed[i]) _xgmPCMHalfSent[i] = !_xgmPCMHalfSent[i];
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
}

/**
 * @brief   XGM2 PSG 処理
 *
 * @return true   曲終了
 * @return false  処理を継続
 */

bool VGM::_xgm2ProcessYM() {
  u8_t port = _getYMPort(_xgm2_ym_pos);
  u8_t channel = _getYMChannel(_xgm2_ym_pos);
  u8_t command = ndFile.get_ui8_at(_xgm2_ym_pos++);
  u8_t reg;
  u8_t value;

  switch (command) {
    case FM_LOOP: {
      u32_t loopOffset = ndFile.get_ui24_at(_xgm2_ym_pos);
      Serial.printf("0x%x - FM end/loop: offset: %x\n", _xgm2_ym_pos - _xgm2_ym_offset - 1, loopOffset);
      if (loopOffset == 0xffffff) {
        return true;  // 曲終了
      } else {
        _vgmLoop++;
        _xgm2_ym_pos = _xgm2_ym_offset + loopOffset;

        Serial.printf("loops: %d\n", _vgmLoop);
        if (_vgmLoop == ndConfig.get(CFG_NUM_LOOP) &&
            ndConfig.get(CFG_NUM_LOOP) != LOOP_INIFITE) {  //   フェードアウトON
          nju72341.startFadeout();
        }
      }
      break;
    }

    case PCM: {
      u8_t ch = command & 0b011;
      bool halfspeed = command & 0b100;
      u8_t priority = command & 0b1000;
      u8_t sampleID = ndFile.get_ui8_at(_xgm2_ym_pos++);

      // Serial.printf("PCM Ch: %d, Half: %d, Pr: %d, Id: %d\n", ch, halfspeed, priority, sampleID);

      _xgmSampleId[ch] = sampleID;
      if (sampleID == 0) {
        _xgmSampleOn[ch] = false;
      } else {
        _xgmSampleOn[ch] = true;
        _xgmSamplePos[ch] = 0;
        _xgmPCMHalfSpeed[ch] = halfspeed;
        _xgmPCMHalfSent[ch] = false;
      }
      break;
    }

    case FM_LOAD_INST: {
      for (int i = 0x30; i <= 0x9C; i += 0x04) {
        reg = i + channel;
        value = ndFile.get_ui8_at(_xgm2_ym_pos++);
        _xgmYmState[port][reg] = value;
        FM.setYM2612(port, reg, value, 0);
      }

      reg = 0xb0 + channel;
      value = ndFile.get_ui8_at(_xgm2_ym_pos++);
      _xgmYmState[port][reg] = value;
      FM.setYM2612(port, reg, value, 0);

      reg = 0xb4 + channel;
      value = ndFile.get_ui8_at(_xgm2_ym_pos++);
      _xgmYmState[port][reg] = value;
      FM.setYM2612(port, reg, value, 0);
      break;
    }

    case FM_WRITE: {
      u8_t comsize = (command & 7) + 1;
      for (int j = 0; j < comsize; j++) {
        reg = ndFile.get_ui8_at(_xgm2_ym_pos++);
        value = ndFile.get_ui8_at(_xgm2_ym_pos++);
        _xgmYmState[port][reg] = value;
        FM.setYM2612(port, reg, value, 0);
      }
      break;
    }

    case FM0_PAN:
    case FM1_PAN: {
      reg = 0xb4 + channel;
      value = (_xgmYmState[port][reg] & 0x3f) | ((command << 4) & 0xc0);
      _xgmYmState[port][reg] = value;
      FM.setYM2612(port, reg, value, 0);
      break;
    }

    case FM_FREQ:
    case FM_FREQ_WAIT: {
      // wait
      if ((command & 0xf0) == 0x80) {
        _xgmYMFrame++;
      }

      u8_t data1 = ndFile.get_ui8_at(_xgm2_ym_pos++);
      u8_t data2 = ndFile.get_ui8_at(_xgm2_ym_pos++);

      // pre-key off?
      if ((data1 & 0x40) != 0) {
        FM.setYM2612(0, 0x28, 0x00 + (port << 2) + channel, 0);
      }

      // special mode ?
      reg = ((command & 8) != 0) ? 0xa8 : 0xa0;
      // set channel from slot
      if ((command & 8) != 0) {
        channel = _getYMSlot(command) - 1;
      }
      u16_t lvalue = ((data1 & 0x3f) << 8) | (data2 & 0xff);
      _xgmYmState[port][reg + channel + 4] = ((lvalue >> 8) & 0x3f);
      _xgmYmState[port][reg + channel + 0] = (lvalue & 0xff);
      FM.setYM2612(port, reg + channel + 4, _xgmYmState[port][reg + channel + 4], 0);
      FM.setYM2612(port, reg + channel + 0, _xgmYmState[port][reg + channel + 0], 0);

      // post-key on?
      if ((data1 & 0x80) != 0) {
        FM.setYM2612(0, 0x28, 0xf0 + (port << 2) + channel, 0);
      }
      break;
    }

    case FM_FREQ_DELTA:
    case FM_FREQ_DELTA_WAIT: {
      // wait
      if ((command & 0xf0) == 0xb0) {
        _xgmYMFrame++;
      }

      u8_t data1 = ndFile.get_ui8_at(_xgm2_ym_pos++);
      // sepecial mode?
      reg = ((command & 8) != 0) ? 0xa8 : 0xa0;
      // set channel from slot
      if ((command & 8) != 0) {
        channel = _getYMSlot(command) - 1;
      }
      // get state
      u16_t lvalue = (_xgmYmState[port][reg + channel + 4] & 0x3f) << 8;
      lvalue |= _xgmYmState[port][reg + channel + 0] & 0xff;
      int delta = ((data1 >> 1) & 0x7f) + 1;
      delta = ((data1 & 1) != 0) ? -delta : delta;
      lvalue += delta;
      _xgmYmState[port][reg + channel + 4] = (lvalue >> 8) & 0x3f;
      _xgmYmState[port][reg + channel + 0] = lvalue & 0xff;
      FM.setYM2612(port, reg + channel + 4, _xgmYmState[port][reg + channel + 4], 0);
      FM.setYM2612(port, reg + channel + 0, _xgmYmState[port][reg + channel + 0], 0);
      break;
    }

    case FM_TL: {
      u8_t data1 = ndFile.get_ui8_at(_xgm2_ym_pos++);
      // compute reg
      reg = 0x40 + (_getYMSlot(command) << 2) + channel;
      // save state
      _xgmYmState[port][reg] = (data1 >> 1) & 0x7F;
      // create commands
      FM.setYM2612(port, reg, _xgmYmState[port][reg], 0);
      break;
    }

    case FM_TL_DELTA:
    case FM_TL_DELTA_WAIT: {
      // wait
      if ((command & 0xf0) == 0xd0) {
        _xgmYMFrame++;
      }

      u8_t data1 = ndFile.get_ui8_at(_xgm2_ym_pos++);
      // compute reg
      reg = 0x40 + (_getYMSlot(command) << 2) + channel;

      int delta = ((data1 >> 2) & 0x3f) + 1;
      delta = ((data1 & 2) != 0) ? -delta : delta;

      // get state
      int lvalue = _xgmYmState[port][reg] & 0xff;
      lvalue += delta;
      // save state
      _xgmYmState[port][reg] = lvalue;
      // create commands
      FM.setYM2612(port, reg, _xgmYmState[port][reg], 0);
      break;
    }

    case FM_KEY: {
      FM.setYM2612(0, 0x28, (((command & 8) != 0) ? 0xf0 : 0x00) + (port << 2) + channel, 0);
      break;
    }

    case FM_KEY_SEQ: {
      // create key sequence commands
      if ((command & 8) != 0) {
        // ON-OFF sequence
        FM.setYM2612(0, 0x28, 0xf0 + (port << 2) + channel, 0);
        FM.setYM2612(0, 0x28, 0x00 + (port << 2) + channel, 0);
      } else {
        // OFF-ON sequence
        FM.setYM2612(0, 0x28, 0x00 + (port << 2) + channel, 0);
        FM.setYM2612(0, 0x28, 0xf0 + (port << 2) + channel, 0);
      }
      break;
    }

    case FM_DAC_ON: {
      _xgmYmState[0][0x2b] = 0x80;
      FM.setYM2612(0, 0x2b, 0x80, 0);
      break;
    }

    case FM_DAC_OFF: {
      _xgmYmState[0][0x2b] = 0x00;
      FM.setYM2612(0, 0x2b, 0x00, 0);
      break;
    }

    case FM_LFO: {
      u8_t data1 = ndFile.get_ui8_at(_xgm2_ym_pos++);
      _xgmYmState[0][0x22] = data1;
      FM.setYM2612(0, 0x22, _xgmYmState[0][0x22], 0);
      break;
    }

    case FM_CH3_SPECIAL_ON: {
      value = (_xgmYmState[0][0x27] & 0xbf) | 0x40;
      _xgmYmState[0][0x27] = value;
      FM.setYM2612(0, 0x27, value, 0);
      break;
    }

    case FM_CH3_SPECIAL_OFF: {
      value = (_xgmYmState[0][0x27] & 0xbf) | 0x00;
      _xgmYmState[0][0x27] = value;
      FM.setYM2612(0, 0x27, value, 0);
      break;
    }

    case WAIT_SHORT: {
      _xgmYMFrame += command + 1;
      break;
    }
    case WAIT_LONG: {
      _xgmYMFrame += ndFile.get_ui8_at(_xgm2_ym_pos++) + 16;
      break;
    }

    case FRAME_DELAY: {
      break;
    }

    default: {
      Serial.printf("Unknown XGM2 command: 0x%0x @ 0x%0x\n", command, _xgm2_ym_pos);
    }
  }

  return false;
}

// XGM 2 port and channel
int VGM::_getYMPort(u32_t pos) {
  u8_t command = ndFile.get_ui8_at(pos);
  switch (command) {
    case FM0_PAN:
      return 0;
    case FM1_PAN:
      return 1;
    case FM_LOAD_INST:
    case FM_FREQ:
    case FM_FREQ_WAIT:
    case FM_FREQ_DELTA:
    case FM_FREQ_DELTA_WAIT:
    case FM_KEY:
    case FM_KEY_SEQ:
      return (command >> 2) & 1;
    case FM_WRITE:
      return (command >> 3) & 1;
    case FM_TL:
    case FM_TL_DELTA:
    case FM_TL_DELTA_WAIT:
      return (ndFile.get_ui8_at(pos + 1) >> 0) & 1;
    case FM_KEY_ADV:
      return (ndFile.get_ui8_at(pos + 1) >> 2) & 1;
  }
  return 0;
}

int VGM::_getYMChannel(u32_t pos) {
  u8_t command = ndFile.get_ui8_at(pos);
  switch (command) {
    case FM_FREQ:
    case FM_FREQ_WAIT:
    case FM_FREQ_DELTA:
    case FM_FREQ_DELTA_WAIT:
      if ((command & 8) != 0) return 2;
    case FM_LOAD_INST:
    case FM_KEY:
    case FM_KEY_SEQ:
    case FM_TL:
    case FM_TL_DELTA:
    case FM_TL_DELTA_WAIT:
    case FM0_PAN:
    case FM1_PAN:
      return (command & 3);
    case FM_WRITE:
      if ((ndFile.get_ui8_at(pos + 1) & 0xF8) == 0xA8) return 2;
    case FM_KEY_ADV:
      return (ndFile.get_ui8_at(pos + 1) & 3);
  }
  return -1;
}

int VGM::_getYMSlot(u8_t command) {
  switch (command) {
    case FM_FREQ:
    case FM_FREQ_WAIT:
    case FM_FREQ_DELTA:
    case FM_FREQ_DELTA_WAIT:
      return ((command & 8) != 0) ? ((command & 3) + 1) : -1;
    case FM_TL:
    case FM_TL_DELTA:
    case FM_TL_DELTA_WAIT:
      return ((command >> 2) & 3);
  }
  return -1;
}

/**
 * @brief SN76489 command processing
 *
 * @return true  End of music data
 * @return false  Continue processing
 */
bool VGM::_xgm2ProcessSN() {
  u8_t channel = _getChannel(_xgm2_psg_pos);
  u8_t command = ndFile.get_ui8_at(_xgm2_psg_pos++);
  u8_t reg;
  u8_t value;

  switch (command) {
    int oldHighFreq;

    case PSG_LOOP: {
      u32_t loopOffset = ndFile.get_ui24_at(_xgm2_psg_pos);
      // Serial.printf("0x%x - PSG end/loop: offset: %x\n", _xgm2_psg_pos - _xgm2_psg_offset - 1, loopOffset);
      if (loopOffset == 0xFFFFFF) {
        return true;  // 処理終了
      } else {
        _xgm2_psg_pos = _xgm2_psg_offset + loopOffset;
      }
      break;
    }

    case PSG_ENV0:
    case PSG_ENV1:
    case PSG_ENV2:
    case PSG_ENV3: {
      _xgmPsgState[0][channel] = command & 0x0f;
      value = (0x90 + (channel << 5)) | _xgmPsgState[0][channel];
      // Serial.printf("0x%x - %2x: PSG ENV ch %d 0x%x\n", _xgm2_psg_pos - _xgm2_psg_offset - 1, command, channel,
      // value);
      FM.writeRaw(value, 1, freq[chipSlot[CHIP_SN76489_0]]);
      break;
    }

    case PSG_ENV0_DELTA:
    case PSG_ENV1_DELTA:
    case PSG_ENV2_DELTA:
    case PSG_ENV3_DELTA: {
      // wait
      if ((command & 0x08)) {
        _xgmPSGFrame++;
      }

      int delta = ((command >> 0) & 3) + 1;
      delta = ((command & 4) != 0) ? -delta : delta;
      _xgmPsgState[0][channel] = delta + (_xgmPsgState[0][channel] & 0xf);
      value = (0x90 + (channel << 5)) | _xgmPsgState[0][channel];
      // Serial.printf("0x%x - %2x: PSG ENV DELTA ch %d 0x%x\n", _xgm2_psg_pos - _xgm2_psg_offset - 1, command,
      // channel,
      //              value);
      FM.writeRaw(value, 1, freq[chipSlot[CHIP_SN76489_0]]);
      break;
    }

    case PSG_FREQ:
    case PSG_FREQ_WAIT: {
      // wait
      if ((command & 0xf0) == 0x30) {
        _xgmPSGFrame++;
      }

      u8_t data1 = ndFile.get_ui8_at(_xgm2_psg_pos++);
      oldHighFreq = _xgmPsgState[1][channel] & 0x03f0;
      int lvalue = ((command & 0x3) << 8) | (data1 & 0xFF);

      _xgmPsgState[1][channel] = lvalue;

      // Workaround for Sega VDP and SN76489
      // value = (0x80 + (channel << 5)) | (lvalue & 0x0f);
      if (lvalue == 0) {
        value = ((0x80 + (channel << 5)) | 1);
      } else {
        value = ((0x80 + (channel << 5)) | (lvalue & 0x0f));
      }
      FM.writeRaw(value, 1, freq[chipSlot[CHIP_SN76489_0]]);

      // Always send High value for SN76489
      //      if ((oldHighFreq != (lvalue & 0x3F0)) && (channel < 3)) {
      if (channel < 3) {
        value = (0x00 | ((lvalue >> 4) & 0x3f));
        // Serial.printf("   PSG_FREQ+ ch %d 0x%0x\n", channel, value);
        FM.writeRaw(value, 1, freq[chipSlot[CHIP_SN76489_0]]);
      }
      break;
    }

    case PSG_FREQ_LOW: {
      // wait
      if ((command & 0x1)) {
        _xgmPSGFrame++;
      }

      oldHighFreq = _xgmPsgState[1][channel] & 0x03f0;

      u8_t data1 = ndFile.get_ui8_at(_xgm2_psg_pos++);
      int lvalue = (_xgmPsgState[1][channel] & 0x03f0) | (data1 & 0xF);
      _xgmPsgState[1][channel] = lvalue;
      if (lvalue == 0) {
        value = ((0x80 + (channel << 5)) | 1);
      } else {
        value = ((0x80 + (channel << 5)) | (lvalue & 0x0f));
      }
      FM.writeRaw(value, 1, freq[chipSlot[CHIP_SN76489_0]]);
      // Serial.printf("0x%x - %2x: PSG_FREQ_LOW ch %d 0x%x\n", _xgm2_psg_pos - _xgm2_psg_offset - 2, command,
      // channel,
      //               value);

      break;
    }

    case PSG_FREQ0_DELTA:
    case PSG_FREQ1_DELTA:
    case PSG_FREQ2_DELTA:
    case PSG_FREQ3_DELTA: {
      // wait
      if ((command & 0x08)) {
        _xgmPSGFrame++;
      }

      oldHighFreq = _xgmPsgState[1][channel] & 0x03f0;
      int delta = ((command >> 0) & 3) + 1;
      delta = ((command & 4) != 0) != 0 ? -delta : delta;
      int lvalue = (_xgmPsgState[1][channel] & 0x03ff) + delta;
      _xgmPsgState[1][channel] = lvalue;

      // Workaround for Sega VDP and SN76489
      // value = (0x80 + (channel << 5)) | (lvalue & 0x0f);
      if (lvalue == 0) {
        value = ((0x80 + (channel << 5)) | 1);
      } else {
        value = ((0x80 + (channel << 5)) | (lvalue & 0x0f));
      }

      FM.writeRaw(value, 1, freq[chipSlot[CHIP_SN76489_0]]);
      // Serial.printf("0x%x - %2x: PSG FREQ DELTA ch %d 0x%x\n", _xgm2_psg_pos - _xgm2_psg_offset - 1, command,
      // channel,
      //              value);

      // high byte changed and not channel 3 (single byte write for channel 3)
      if ((oldHighFreq != (lvalue & 0x3f0)) && (channel < 3)) {
        // if (channel < 3) {
        value = (0x00 | ((lvalue >> 4) & 0x3f));
        FM.writeRaw(value, 1, freq[chipSlot[CHIP_SN76489_0]]);
        // Serial.printf(" %2x: PSG_FREQ_DELTA+ ch %d 0x%x\n", command, channel, value);
      }
      break;
    }

    case PSG_WAIT_SHORT: {
      _xgmPSGFrame += command + 1;
      break;
    }
    case PSG_WAIT_LONG: {
      _xgmPSGFrame += ndFile.get_ui8_at(_xgm2_psg_pos++) + 15;
      break;
    }
  }
  return false;
}

u8_t VGM::_getChannel(u32_t pos) {
  u8_t command = ndFile.get_ui8_at(pos);
  switch (command) {
    case PSG_ENV0:
    case PSG_ENV0_DELTA:
    case PSG_FREQ0_DELTA:
      return 0;
    case PSG_ENV1:
    case PSG_ENV1_DELTA:
    case PSG_FREQ1_DELTA:
      return 1;
    case PSG_ENV2:
    case PSG_ENV2_DELTA:
    case PSG_FREQ2_DELTA:
      return 2;
    case PSG_ENV3:
    case PSG_ENV3_DELTA:
    case PSG_FREQ3_DELTA:
      return 3;
    case PSG_FREQ:
    case PSG_FREQ_WAIT:
      return ((command >> 2) & 3);
    case PSG_FREQ_LOW:
      return ((ndFile.get_ui8_at(pos + 1) >> 5) & 3);
  }
  return 255;
}

//---------------------------------------------------------------
// 曲終了時の処理
void VGM::endProcedure() {
  xgmLoaded = false;
  vgmLoaded = false;

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

u64_t VGM::getCurrentTime() {
  if (_vgmSamples >= 264600000) _vgmSamples = 0;
  return _vgmSamples / 44100;
}

s64_t VGM::micros64() {
  //
  return esp_timer_get_time();
}

VGM vgm = VGM();