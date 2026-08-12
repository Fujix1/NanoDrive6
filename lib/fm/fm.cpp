#include "fm.h"

#include <driver/dedic_gpio.h>
#include <math.h>

#include "../../include/config.h"
#include "../../include/keyinfo.h"
#include "../../include/nd.h"

dedic_gpio_bundle_handle_t dataBus = NULL;  // GPIOバンドル用ハンドラ
static portMUX_TYPE ym2612ChmaskMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t ym2612PanMode = TPAN_NORMAL;
static volatile uint8_t sn76489AttenuationMode = SN_ATT_0;
// YM2612 のアドレスラッチは port 0/1 で共有されるため、chip ごとに管理する。
// bank ごとに分けると port 1 の FM 書き込み後も DAC の 0x2A 再選択を
// 省略してしまい、DAC データが別レジスタへ送られる。
static byte ym2612LastAddr[3] = {};
static bool ym2612LastAddrValid[3] = {};

// ------------------------------------------------------------------------------
// FM音源クラス
//    表記の違い
//    YM**** -> SN76489AN
//    WR -> WE
//    CS -> CE(OE)
//    READY -> No connect

void FMChip::begin() {
  // データバス用 GPIO バンドル
  const int bundleA_gpios[] = {D0, D1, D2, D3, D4, D5, D6, D7};
  gpio_config_t io_conf = {
      .mode = GPIO_MODE_OUTPUT,
  };
  for (int i = 0; i < sizeof(bundleA_gpios) / sizeof(bundleA_gpios[0]); i++) {
    io_conf.pin_bit_mask = 1ULL << bundleA_gpios[i];
    gpio_config(&io_conf);
  }
  // decic config
  dedic_gpio_bundle_config_t bundle_config = {
      .gpio_array = bundleA_gpios,
      .array_size = sizeof(bundleA_gpios) / sizeof(bundleA_gpios[0]),
      .flags =
          {
              .out_en = 1,
          },
  };
  ESP_ERROR_CHECK(dedic_gpio_new_bundle(&bundle_config, &dataBus));

  // その他の GPIO
  pinMode(WR, OUTPUT);
  pinMode(CS0, OUTPUT);
  pinMode(CS1, OUTPUT);
  pinMode(CS2, OUTPUT);

  pinMode(A0, OUTPUT);
  pinMode(A1, OUTPUT);
  pinMode(IC, OUTPUT);

  WR_HIGH;
  A0_LOW;
  A1_LOW;
  IC_LOW;

  CS0_HIGH;
  CS1_HIGH;
  CS2_HIGH;
}

void FMChip::reset(void) {
  for (uint8_t chip = 0; chip < 3; chip++) {
    _snLatchedReg[chip] = 0;
    _snNoiseControl[chip] = 0;
    for (uint8_t ch = 0; ch < 3; ch++) {
      _snTonePeriod[chip][ch] = 0;
    }
    for (uint8_t ch = 0; ch < 4; ch++) {
      _snVolume[chip][ch] = 15;
    }
  }

  for (uint8_t chip = 0; chip < 3; chip++) {
    ym2612LastAddr[chip] = 0;
    ym2612LastAddrValid[chip] = false;
    for (uint8_t bank = 0; bank < 2; bank++) {
      for (uint8_t reg = 0; reg < 16; reg++) {
        _ym2612TlReg[chip][bank][reg] = 0;
        _ym2612TlRegValid[chip][bank][reg] = false;
      }
      for (uint8_t ch = 0; ch < 3; ch++) {
        _ym2612FreqLow[chip][bank][ch] = 0;
        _ym2612FreqHigh[chip][bank][ch] = 0;
        _ym2612Alg[chip][bank][ch] = 0;
      }
    }
    for (uint8_t ch = 0; ch < 6; ch++) {
      _ym2612KeyOnSlots[chip][ch] = 0;
    }
  }
  _ym2612DacLevelDecimator = 0;
  _ym2612DacLevelPeak = 0;

  CS0_LOW;
  CS1_LOW;
  CS2_LOW;

  WR_HIGH;
  A0_LOW;
  IC_LOW;

  ets_delay_us(32);
  // 72 cycles for YM2151 at 4MHz: 0.25us * 72 = 18us
  // 192 cycles for YM3438 -> 8MHz 0.125us * 192 = 24us
  IC_HIGH;
  CS0_HIGH;
  CS1_HIGH;
  CS2_HIGH;

  // stop sound output from SN76489
  FM.write(0x9f, 1, SI5351_1500);
  FM.write(0xbf, 1, SI5351_1500);
  FM.write(0xdf, 1, SI5351_1500);
  FM.write(0xff, 1, SI5351_1500);

  FM.write(0x9f, 2, SI5351_1500);
  FM.write(0xbf, 2, SI5351_1500);
  FM.write(0xdf, 2, SI5351_1500);
  FM.write(0xff, 2, SI5351_1500);

  _psgFrqLowByte = 0;

  delay(16);
}

// SN76489
static byte applySN76489Attenuation(byte data) {
  if ((data & 0x80) == 0) {
    return data;
  }

  const uint8_t reg = (data >> 4) & 0x07;
  if ((reg & 0x01) == 0) {
    return data;
  }

  uint8_t attenuation = data & 0x0f;
  switch (sn76489AttenuationMode) {
    case SN_ATT_2:
      if (attenuation <= 7) {
        attenuation++;
      }
      break;
    case SN_ATT_4:
      if (attenuation <= 4) {
        attenuation += 2;
      } else if (attenuation <= 9) {
        attenuation++;
      }
      break;
    case SN_ATT_0:
    default:
      break;
  }

  return (data & 0xf0) | (attenuation & 0x0f);
}

void FMChip::write(byte data, byte chipno, si5351Freq_t freq) {
  //

  if ((data & 0x90) == 0x80 && (data & 0x60) >> 5 != 3) {
    // Low byte 周波数 0x8n, 0xan, 0xcn
    _psgFrqLowByte = data;

  } else if ((data & 0x80) == 0) {  // High byte
    if ((_psgFrqLowByte & 0x0F) == 0) {
      if ((data & 0x3F) == 0) _psgFrqLowByte |= 1;
    }
    writeRaw(_psgFrqLowByte, chipno, freq);
    writeRaw(data, chipno, freq);

  } else {
    writeRaw(data, chipno, freq);
  }
}

void FMChip::writeRaw(byte data, byte chipno, si5351Freq_t freq) {
  const byte visualData = data;
  data = applySN76489Attenuation(data);

  switch (chipno) {
    case 0:
      CS0_LOW;
      break;
    case 1:
      CS1_LOW;
      break;
    case 2:
      CS2_LOW;
      break;
  }
  WR_HIGH;
  dedic_gpio_bundle_write(dataBus, 0xff, data);

  // コントロールレジスタに登録するには WR_LOW → WR_HIGH 最低32クロック
  // 4MHz     :　0.25us   * 32 = 8 us
  // 3.579MHz :  0.2794us * 32 = 8.94 us
  // 1.5MHz   :  0.66us   * 32 = 21.3 us
  WR_LOW;

  ets_delay_us((32000000 / freq) + 1);

  WR_HIGH;
  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }

  _updateSN76489VisualState(visualData, chipno, freq);
}

static NoteInfo freqToNote(double freq) {
  if (freq <= 0) {
    return {0, 0};
  }

  const double n = 12.0 * log2(freq / 440.0);
  const int noteIndexFromC0 = (int)round(n) + 57;
  if (noteIndexFromC0 < 12) {
    return {0, 0};
  }

  return {noteIndexFromC0 / 12, noteIndexFromC0 % 12};
}

static NoteInfo sn76489ToneToNote(uint16_t period, si5351Freq_t clock) {
  if (clock <= 0) {
    return {0, 0};
  }

  const uint16_t effectivePeriod = (period == 0) ? 0x0400 : period;
  return freqToNote((double)clock / (32.0 * (double)effectivePeriod));
}

static t_device sn76489DeviceFromChipno(uint8_t chipno) {
  if (chipno == 1) {
    return SN76489_0_KEY;
  }
  if (chipno == 2) {
    return SN76489_1_KEY;
  }
  return DEVICE_COUNT;
}

void FMChip::_updateSN76489ChannelNote(uint8_t chipno, uint8_t ch, si5351Freq_t freq) {
  if (chipno >= 3 || ch >= 4) {
    return;
  }

  t_device device = sn76489DeviceFromChipno(chipno);
  if (device == DEVICE_COUNT) {
    return;
  }

  uint8_t trackNo = 0xff;
  if (chipno == 1) {
    trackNo = (uint8_t)(8 + ch);  // Track 9-12: SN76489 (1)
  } else if (chipno == 2) {
    trackNo = (uint8_t)(12 + ch);  // Track 13-16: SN76489 (2)
  }

  NoteInfo note = {0, 0};
  uint8_t level = 0;
  if (_snVolume[chipno][ch] < 15) {
    level = (uint8_t)(15 - _snVolume[chipno][ch]);
    if (ch < 3) {
      note = sn76489ToneToNote(_snTonePeriod[chipno][ch], freq);
    } else {
      switch (_snNoiseControl[chipno] & 0x03) {
        case 0:
          note = freqToNote((double)freq / 512.0);
          break;
        case 1:
          note = freqToNote((double)freq / 1024.0);
          break;
        case 2:
          note = freqToNote((double)freq / 2048.0);
          break;
        case 3:
          note = sn76489ToneToNote(_snTonePeriod[chipno][2], freq);
          break;
      }
    }
  }

  if (xSemaphoreTake(KeyBoard.keyinfoMutex, 0) == pdTRUE) {
    KeyBoard.keyInfo[device][ch] = note;
    if (trackNo < 16) {
      KeyBoard.trackLevel[trackNo] = level;
    }
    xSemaphoreGive(KeyBoard.keyinfoMutex);
  }
}

void FMChip::_updateSN76489VisualState(byte data, uint8_t chipno, si5351Freq_t freq) {
  if (chipno >= 3) {
    return;
  }
  if (sn76489DeviceFromChipno(chipno) == DEVICE_COUNT) {
    return;
  }

  if ((data & 0x80) != 0) {
    const uint8_t reg = (data >> 4) & 0x07;
    _snLatchedReg[chipno] = reg;

    if ((reg & 0x01) == 0) {
      const uint8_t ch = reg >> 1;
      if (ch < 3) {
        _snTonePeriod[chipno][ch] =
            (uint16_t)((_snTonePeriod[chipno][ch] & 0x03f0) | (data & 0x0f));
        _updateSN76489ChannelNote(chipno, ch, freq);
        if (ch == 2 && (_snNoiseControl[chipno] & 0x03) == 3) {
          _updateSN76489ChannelNote(chipno, 3, freq);
        }
      } else {
        _snNoiseControl[chipno] = data & 0x07;
        _updateSN76489ChannelNote(chipno, 3, freq);
      }
    } else {
      const uint8_t ch = reg >> 1;
      if (ch < 4) {
        _snVolume[chipno][ch] = data & 0x0f;
        _updateSN76489ChannelNote(chipno, ch, freq);
      }
    }
    return;
  }

  const uint8_t reg = _snLatchedReg[chipno];
  if ((reg & 0x01) == 0) {
    const uint8_t ch = reg >> 1;
    if (ch < 3) {
      _snTonePeriod[chipno][ch] =
          (uint16_t)((_snTonePeriod[chipno][ch] & 0x000f) | ((data & 0x3f) << 4));
      _updateSN76489ChannelNote(chipno, ch, freq);
      if (ch == 2 && (_snNoiseControl[chipno] & 0x03) == 3) {
        _updateSN76489ChannelNote(chipno, 3, freq);
      }
    }
  }
}

static NoteInfo ym2612FreqToNote(uint16_t rawFreq) {
  const uint16_t fnum = rawFreq & 0x07ff;
  const uint8_t block = (rawFreq >> 11) & 0x07;
  if (fnum == 0) {
    return {0, 0};
  }

  double clock = ND::freq[0];
  if (clock <= 0) {
    clock = 7670453.0;
  }

  const double freq = (double)fnum * clock / (144.0 * pow(2.0, 20 - block));
  if (freq <= 0) {
    return {0, 0};
  }

  const double n = 12.0 * log2(freq / 440.0);
  const int noteIndexFromC0 = (int)round(n) + 57;
  if (noteIndexFromC0 < 12) {
    return {0, 0};
  }

  return {noteIndexFromC0 / 12, noteIndexFromC0 % 12};
}

uint8_t FMChip::_getYM2612DisplayLevel(uint8_t chipno, uint8_t ch) const {
  static constexpr uint8_t kYm2612CarrierSlots[8] = {0x08, 0x08, 0x08, 0x08,
                                                     0x0a, 0x0e, 0x0e, 0x0f};
  static constexpr uint8_t kYm2612TlOffsets[4] = {0, 8, 4, 12};

  if (chipno >= 3 || ch >= 6 || _ym2612KeyOnSlots[chipno][ch] == 0) {
    return 0;
  }

  const uint8_t bank = ch / 3;
  const uint8_t bankCh = ch % 3;
  const uint8_t alg = _ym2612Alg[chipno][bank][bankCh] & 0x07;
  const uint8_t carrierMask = kYm2612CarrierSlots[alg];
  const uint8_t keyOnMask = _ym2612KeyOnSlots[chipno][ch];
  uint8_t minTl = 127;
  bool hasCarrier = false;

  for (uint8_t op = 0; op < 4; op++) {
    const uint8_t slotBit = (uint8_t)(1u << op);
    if ((carrierMask & slotBit) == 0 || (keyOnMask & slotBit) == 0) {
      continue;
    }

    const uint8_t reg = kYm2612TlOffsets[op] + bankCh;
    if (!_ym2612TlRegValid[chipno][bank][reg]) {
      continue;
    }
    const uint8_t tl = _ym2612TlReg[chipno][bank][reg] & 0x7f;
    if (!hasCarrier || tl < minTl) {
      minTl = tl;
      hasCarrier = true;
    }
  }

  if (!hasCarrier) {
    return 0;
  }
  return (uint8_t)((127 - minTl) >> 3);
}

void FMChip::_updateYM2612TrackLevel(uint8_t chipno, uint8_t ch) {
  if (chipno != 0 || ch >= 6) {
    return;
  }

  const uint8_t level = _getYM2612DisplayLevel(chipno, ch);
  if (xSemaphoreTake(KeyBoard.keyinfoMutex, 0) == pdTRUE) {
    KeyBoard.trackLevel[ch] = level;
    xSemaphoreGive(KeyBoard.keyinfoMutex);
  }
}

void FMChip::_updateYM2612KeyState(byte data, uint8_t chipno) {
  if (chipno != 0) {
    return;
  }

  uint8_t ch = data & 0x03;
  if (ch >= 3) {
    return;
  }
  if ((data & 0x04) != 0) {
    ch += 3;
  }

  const uint8_t bank = ch / 3;
  const uint8_t bankCh = ch % 3;
  const uint8_t slotMask = (uint8_t)((data >> 4) & 0x0f);
  const bool keyOn = slotMask != 0;
  NoteInfo note = {0, 0};
  if (keyOn) {
    const uint16_t rawFreq =
        ((uint16_t)(_ym2612FreqHigh[chipno][bank][bankCh] & 0x3f) << 8) |
        _ym2612FreqLow[chipno][bank][bankCh];
    note = ym2612FreqToNote(rawFreq);
  }

  if (xSemaphoreTake(KeyBoard.keyinfoMutex, 0) == pdTRUE) {
    KeyBoard.keyInfo[YM2612_KEY][ch] = note;
    KeyBoard.trackKeyOn[ch] = keyOn;
    xSemaphoreGive(KeyBoard.keyinfoMutex);
  }

  _ym2612KeyOnSlots[chipno][ch] = slotMask;
  _updateYM2612TrackLevel(chipno, ch);
}

void FMChip::_updateYM2612PanState(byte bank, byte addr, byte data) {
  if ((uint8_t)(addr - 0xB4) > 2 || bank >= 2) {
    return;
  }

  const uint8_t ch = bank * 3 + (addr - 0xB4);
  tPan pan = PAN_MUTE;
  const bool left = (data & 0x80) != 0;
  const bool right = (data & 0x40) != 0;
  if (left && right) {
    pan = PAN_CENTER;
  } else if (left) {
    pan = PAN_LEFT;
  } else if (right) {
    pan = PAN_RIGHT;
  }

  if (xSemaphoreTake(KeyBoard.keyinfoMutex, 0) == pdTRUE) {
    KeyBoard.trackPan[ch] = pan;
    if (ch == 5) {
      // YM2612 DACはChannel 6のパン設定を通るため、Track 7のPCM表示にも反映する。
      KeyBoard.trackPan[6] = pan;
    }
    xSemaphoreGive(KeyBoard.keyinfoMutex);
  }
}

void FMChip::_updateYM2612DacLevel(byte data, uint8_t chipno) {
  if (chipno != 0) {
    return;
  }

  int16_t centered = (int16_t)data - 0x80;
  if (centered < 0) {
    centered = -centered;
  }

  uint8_t level = (uint8_t)(centered >> 3);
  if (level > 15) {
    level = 15;
  }
  if (level > _ym2612DacLevelPeak) {
    _ym2612DacLevelPeak = level;
  }

  // DACは約15kHzで呼ばれる。送信側では描画通知やmutexを使わず、数サンプルごとの
  // ピークだけをTrack 7へ渡す。表示側が読み損ねても再生タイミングを優先する。
  _ym2612DacLevelDecimator++;
  if ((_ym2612DacLevelDecimator & 0x07) != 0) {
    return;
  }

  if (_ym2612DacLevelPeak > KeyBoard.trackLevel[6]) {
    KeyBoard.trackLevel[6] = _ym2612DacLevelPeak;
  }
  _ym2612DacLevelPeak = 0;
}

void FMChip::_updateYM2612VisualState(byte bank, byte addr, byte data, uint8_t chipno) {
  if (chipno != 0 || bank >= 2) {
    return;
  }

  if (addr >= 0x40 && addr <= 0x4F) {
    const uint8_t reg = addr - 0x40;
    const uint8_t bankCh = reg & 0x03;
    if (bankCh < 3) {
      _updateYM2612TrackLevel(chipno, bank * 3 + bankCh);
    }
  } else if ((uint8_t)(addr - 0xB0) <= 2) {
    const uint8_t bankCh = addr - 0xB0;
    _ym2612Alg[chipno][bank][bankCh] = data & 0x07;
    _updateYM2612TrackLevel(chipno, bank * 3 + bankCh);
  } else if ((uint8_t)(addr - 0xA0) <= 2) {
    const uint8_t bankCh = addr - 0xA0;
    const uint8_t ch = bank * 3 + bankCh;
    _ym2612FreqLow[chipno][bank][bankCh] = data;
    if (xSemaphoreTake(KeyBoard.keyinfoMutex, 0) == pdTRUE) {
      if (KeyBoard.trackKeyOn[ch]) {
        const uint16_t rawFreq =
            ((uint16_t)(_ym2612FreqHigh[chipno][bank][bankCh] & 0x3f) << 8) |
            _ym2612FreqLow[chipno][bank][bankCh];
        KeyBoard.keyInfo[YM2612_KEY][ch] = ym2612FreqToNote(rawFreq);
      }
      xSemaphoreGive(KeyBoard.keyinfoMutex);
    }
  } else if ((uint8_t)(addr - 0xA4) <= 2) {
    const uint8_t bankCh = addr - 0xA4;
    const uint8_t ch = bank * 3 + bankCh;
    _ym2612FreqHigh[chipno][bank][bankCh] = data;
    if (xSemaphoreTake(KeyBoard.keyinfoMutex, 0) == pdTRUE) {
      if (KeyBoard.trackKeyOn[ch]) {
        const uint16_t rawFreq =
            ((uint16_t)(_ym2612FreqHigh[chipno][bank][bankCh] & 0x3f) << 8) |
            _ym2612FreqLow[chipno][bank][bankCh];
        KeyBoard.keyInfo[YM2612_KEY][ch] = ym2612FreqToNote(rawFreq);
      }
      xSemaphoreGive(KeyBoard.keyinfoMutex);
    }
  } else if (addr == 0x28) {
    _updateYM2612KeyState(data, chipno);
  } else if ((uint8_t)(addr - 0xB4) <= 2) {
    _updateYM2612PanState(bank, addr, data);
  }
}

void FMChip::setYM2612(byte bank, byte addr, byte data, uint8_t chipno) {
  if (_ym2612OutputMode == FMPCM_FM && addr == 0x2A) {
    return;  // DAC data off (FM only)
  }

  if (chipno >= 3 || bank >= 2) return;

  if (addr >= 0x40 && addr <= 0x4F) {
    const uint8_t reg = addr - 0x40;
    _ym2612TlReg[chipno][bank][reg] = data;
    _ym2612TlRegValid[chipno][bank][reg] = true;
  }

  bool invertPan = ym2612PanMode == TPAN_INVERT;
  if (ND::version == nd_v60) {
    invertPan = !invertPan;
  }
  if ((uint8_t)(addr - 0xB4) <= 2 && invertPan) {
    data = (data & 0x3F) | ((data & 0x80) >> 1) | ((data & 0x40) << 1);
  }

  data = _applyYM2612ChannelMask(bank, addr, data, chipno);
  _updateYM2612VisualState(bank, addr, data, chipno);

  switch (chipno) {
    case 0:
      CS0_LOW;
      break;
    case 1:
      CS1_LOW;
    case 2:
      CS2_LOW;
      break;
  }

  if (bank == 1) {
    A1_HIGH;
  } else {
    A1_LOW;
  }

  ym2612LastAddr[chipno] = addr;
  ym2612LastAddrValid[chipno] = true;

  // Address
  A0_LOW;
  dedic_gpio_bundle_write(dataBus, 0xff, addr);
  WR_LOW;
  WR_HIGH;
  A0_HIGH;

  // アドレスライト後の待ちサイクル
  // アドレス＄21-＄B6 待ちサイクル 17 = 2.21us
  ets_delay_us(5);  // 3 は一部足りない

  // data
  dedic_gpio_bundle_write(dataBus, 0xff, data);

  WR_LOW;
  WR_HIGH;
  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }

  if (bank == 1) {
    A1_LOW;
  }
  // unsigned long deltaTime = micros() - startTime;
  // Serial.printf("%x%d\n", addr, deltaTime);
  if (addr == 0x2a) {
    _updateYM2612DacLevel(data, chipno);
  } else if (addr >= 0x21 && addr <= 0x9e) {
    ets_delay_us(11);  // 83 cycles = 10.79us,
  } else if (addr >= 0xa0 && addr <= 0xb6) {
    ets_delay_us(7);  // 47 cycles = 6.11us
  }

  // YM3438 Twww マニュアルより
  // WR_LOW -> WR_HIGH: Tww 200 ns
  // Dn -> WR_HIGH: Twds 100 ns

  // データ-アドレスライト間, データデータ間 ($21 - $9E) 83サイクル = 10.79 us
  // データ-アドレスライト間  データデータ間 ($A0 - $B6) 47サイクル = 6.11 us
}

byte FMChip::_applyYM2612ChannelMask(byte bank, byte addr, byte data, uint8_t chipno) const {
  if (chipno != 0 || bank >= 2 || addr < 0x40 || addr > 0x4F) {
    return data;
  }

  const uint8_t bankCh = (addr - 0x40) & 0x03;
  if (bankCh >= 3) {
    return data;
  }

  const uint8_t ch = bank * 3 + bankCh;
  const u8_t outputModeMask = _ym2612OutputMode == FMPCM_PCM ? 0x3f : 0x00;
  const u8_t effectiveMask = ym2612_chmask | outputModeMask;
  if (effectiveMask & (u8_t)(1u << ch)) {
    return 0x7F;  // 対象chの全FMオペレータを最大減衰。DAC出力には影響しない。
  }

  return data;
}

void FMChip::_writeCachedYM2612Tl(uint8_t chipno) {
  if (chipno >= 3) return;

  for (uint8_t bank = 0; bank < 2; bank++) {
    for (uint8_t reg = 0; reg < 16; reg++) {
      if (_ym2612TlRegValid[chipno][bank][reg]) {
        setYM2612(bank, 0x40 + reg, _ym2612TlReg[chipno][bank][reg], chipno);
      }
    }
  }
}

void FMChip::_writeCachedYM2612ChannelTl(uint8_t chipno, uint8_t ch) {
  if (chipno >= 3 || ch >= 6) return;

  const uint8_t bank = ch / 3;
  const uint8_t bankCh = ch % 3;
  for (uint8_t reg = bankCh; reg < 16; reg += 4) {
    if (_ym2612TlRegValid[chipno][bank][reg]) {
      setYM2612(bank, 0x40 + reg, _ym2612TlReg[chipno][bank][reg], chipno);
    }
  }
}

void FMChip::requestApplyYM2612OutputMode() {
  const int outputMode = ndConfig.get(CFG_FMPCM);
  if (outputMode >= FMPCM_BOTH && outputMode <= FMPCM_PCM) {
    _ym2612OutputMode = (u8_t)outputMode;
  }

  const int panMode = ndConfig.get(CFG_YM2612_PAN);
  if (panMode >= TPAN_NORMAL && panMode <= TPAN_INVERT) {
    ym2612PanMode = (u8_t)panMode;
  }

  const int snAttenuation = ndConfig.get(CFG_SNATT);
  if (snAttenuation >= SN_ATT_0 && snAttenuation <= SN_ATT_4) {
    sn76489AttenuationMode = (u8_t)snAttenuation;
  }
  _ym2612OutputModeApplyPending = true;
}

void FMChip::applyPendingYM2612OutputMode() {
  if (!_ym2612OutputModeApplyPending) return;
  _ym2612OutputModeApplyPending = false;

  for (uint8_t chipno = 0; chipno < 3; chipno++) {
    _writeCachedYM2612Tl(chipno);
  }
}

void FMChip::requestToggleChannelMask(u8_t ch) {
  if (ch >= 6) return;

  portENTER_CRITICAL(&ym2612ChmaskMux);
  _pendingYm2612ChToggle ^= (u8_t)(1u << ch);
  portEXIT_CRITICAL(&ym2612ChmaskMux);
}

void FMChip::requestResetChannelMask() {
  portENTER_CRITICAL(&ym2612ChmaskMux);
  _pendingYm2612ChMaskReset = true;
  portEXIT_CRITICAL(&ym2612ChmaskMux);
}

void FMChip::applyPendingChannelMask() {
  u8_t pending = 0x00;
  bool reset = false;

  portENTER_CRITICAL(&ym2612ChmaskMux);
  pending = _pendingYm2612ChToggle;
  _pendingYm2612ChToggle = 0x00;
  reset = _pendingYm2612ChMaskReset;
  _pendingYm2612ChMaskReset = false;
  portEXIT_CRITICAL(&ym2612ChmaskMux);

  if (reset) {
    if (ym2612_chmask != 0x00) {
      ym2612_chmask = 0x00;
      _writeCachedYM2612Tl(0);
    }
    return;
  }

  for (u8_t ch = 0; ch < 6; ch++) {
    if (pending & (u8_t)(1u << ch)) {
      ym2612_chmask ^= (u8_t)(1u << ch);
      _writeCachedYM2612ChannelTl(0, ch);
    }
  }
}

// YM2612 の DAC データ送信専用
void FMChip::setYM2612DAC(byte data, uint8_t chipno) {
  if (_ym2612OutputMode == FMPCM_FM) {
    return;  // DAC data off (FM only)
  }

  if (chipno >= 3) return;

  switch (chipno) {
    case 0:
      CS0_LOW;
      break;
    case 1:
      CS1_LOW;
      break;
    case 2:
      CS2_LOW;
      break;
  }

  // DAC data register は port 0。直前にどちらかの port で別アドレスを
  // 選択していた場合だけ、共有アドレスラッチを 0x2A に戻す。
  A1_LOW;
  if (!ym2612LastAddrValid[chipno] || ym2612LastAddr[chipno] != 0x2a) {
    ym2612LastAddr[chipno] = 0x2a;
    ym2612LastAddrValid[chipno] = true;
    // Address
    A0_LOW;
    dedic_gpio_bundle_write(dataBus, 0xff, 0x2a);
    WR_LOW;
    WR_HIGH;
    A0_HIGH;
    // アドレスライト後の待ちサイクル
    // アドレス＄21-＄B6 待ちサイクル 17 = 2.21us
    ets_delay_us(3);
  }

  // data
  dedic_gpio_bundle_write(dataBus, 0xff, data);
  WR_LOW;
  WR_HIGH;
  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }

  _updateYM2612DacLevel(data, chipno);
}

// YM2203, AY-8910用レジスタ設定
void FMChip::setRegister(byte addr, byte data, int chipno = 0) {
  // Address
  dedic_gpio_bundle_write(dataBus, 0xff, addr);
  A0_LOW;  // 375ns
  switch (chipno) {
    case 0:
      CS0_LOW;
      CS1_HIGH;
      CS2_HIGH;
      break;
    case 1:
      CS0_HIGH;
      CS1_LOW;
      CS2_HIGH;
      break;
    case 2:
      CS0_HIGH;
      CS1_HIGH;
      CS2_LOW;
      break;
  }
  ets_delay_us(2);
  WR_LOW;
  ets_delay_us(2);
  WR_HIGH;
  A0_HIGH;

  ets_delay_us(3);

  // data
  dedic_gpio_bundle_write(dataBus, 0xff, data);
  ets_delay_us(2);
  WR_LOW;
  ets_delay_us(2);
  WR_HIGH;
  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }
  ets_delay_us(16);  // 最低16
}

// 　YM2151用レジスタ設定(最適化済)
void FMChip::setRegisterOPM(byte addr, byte data, uint8_t chipno = 0) {
  dedic_gpio_bundle_write(dataBus, 0xff, addr);
  A0_LOW;
  switch (chipno) {
    case 0:
      CS0_LOW;
      CS1_HIGH;
      CS2_HIGH;
      break;
    case 1:
      CS0_HIGH;
      CS1_LOW;
      CS2_HIGH;
      break;
    case 2:
      CS0_HIGH;
      CS1_HIGH;
      CS2_LOW;
      break;
  }
  ets_delay_us(2);
  WR_LOW;
  ets_delay_us(2);
  WR_HIGH;
  A0_HIGH;

  ets_delay_us(3);

  // data
  dedic_gpio_bundle_write(dataBus, 0xff, data);
  ets_delay_us(2);
  WR_LOW;
  ets_delay_us(2);
  WR_HIGH;

  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }
  ets_delay_us(12);
}

void FMChip::setRegisterOPL3(byte port, byte addr, byte data, int chipno) {
  switch (chipno) {
    case 0:
      CS0_LOW;
      CS1_HIGH;
      CS2_HIGH;
      break;
    case 1:
      CS0_HIGH;
      CS1_LOW;
      CS2_HIGH;
    case 2:
      CS0_HIGH;
      CS1_HIGH;
      CS2_LOW;
      break;
  }
  if (port == 1) {
    A1_HIGH;
  } else {
    A1_LOW;
  }

  // Address
  A0_LOW;
  dedic_gpio_bundle_write(dataBus, 0xff, addr);
  WR_LOW;
  ets_delay_us(16);
  WR_HIGH;
  A0_HIGH;

  ets_delay_us(16);

  // 32 clocks to write address
  // 14.318180 MHz: 69.84 ns / cycle
  //  x 32 = 2,234.88 ns = 2.235 us

  // data
  dedic_gpio_bundle_write(dataBus, 0xff, data);
  WR_LOW;
  ets_delay_us(16);
  WR_HIGH;
  switch (chipno) {
    case 0:
      CS0_HIGH;
      break;
    case 1:
      CS1_HIGH;
      break;
    case 2:
      CS2_HIGH;
      break;
  }
  if (port == 1) {
    A1_LOW;
  }

  ets_delay_us(16);
}

FMChip FM;
