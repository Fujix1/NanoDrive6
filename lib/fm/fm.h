#ifndef FM_H
#define FM_H
#include <Arduino.h>

#include "SI5351.hpp"

// GPIO Assignment
#define D0 9
#define D1 10
#define D2 11
#define D3 12
#define D4 13
#define D5 14
#define D6 21
#define D7 47

#define A0 18
#define A1 8
#define WR 40
#define CS0 38
#define CS1 39
#define CS2 43
#define IC 48

#define A1_HIGH (gpio_set_level((gpio_num_t)A1, 1))
#define A1_LOW (gpio_set_level((gpio_num_t)A1, 0))
#define A0_HIGH (gpio_set_level((gpio_num_t)A0, 1))
#define A0_LOW (gpio_set_level((gpio_num_t)A0, 0))
#define WR_HIGH (gpio_set_level((gpio_num_t)WR, 1))
#define WR_LOW (gpio_set_level((gpio_num_t)WR, 0))
#define CS0_HIGH (gpio_set_level((gpio_num_t)CS0, 1))
#define CS0_LOW (gpio_set_level((gpio_num_t)CS0, 0))
#define CS1_HIGH (gpio_set_level((gpio_num_t)CS1, 1))
#define CS1_LOW (gpio_set_level((gpio_num_t)CS1, 0))
#define CS2_HIGH (gpio_set_level((gpio_num_t)CS2, 1))
#define CS2_LOW (gpio_set_level((gpio_num_t)CS2, 0))
#define IC_HIGH (gpio_set_level((gpio_num_t)IC, 1))
#define IC_LOW (gpio_set_level((gpio_num_t)IC, 0))

class FMChip {
 public:
  void begin();
  void reset();
  void setRegister(byte addr, byte value, int chipno);
  void setRegisterOPM(byte addr, byte value, uint8_t chipno);
  void setRegisterOPL3(byte port, byte addr, byte data, int chipno);
  void setYM2612(byte port, byte addr, byte data, uint8_t chipno);
  void setYM2612DAC(byte data, uint8_t chipno);
  void requestApplyYM2612OutputMode();
  void applyPendingYM2612OutputMode();
  // ch 0-5: YM2612 CH1-6, ch 6-9/10-13: SN76489 (1)/(2) tone CH1-3/noise
  void requestToggleChannelMask(u8_t ch);
  void requestResetChannelMask();
  void applyPendingChannelMask();
  uint16_t getChannelMask();
  bool requestMidiNote(uint8_t note, bool keyOn);
  void applyPendingMidiNotes();
  void write(byte data, byte chipno, si5351Freq_t freq);
  void writeRaw(byte data, byte chipno, si5351Freq_t freq);

  // YM2612 channel mask (bit 0-5 = CH1-6). CH6 includes DAC.
  u8_t ym2612_chmask = 0x00;

 private:
  struct MidiNoteEvent {
    uint8_t note;
    bool keyOn;
  };

  u8_t _psgFrqLowByte = 0;
  u8_t _snLatchedReg[3] = {};
  u16_t _snTonePeriod[3][3] = {};
  u8_t _snVolume[3][4] = {};
  u8_t _snNoiseControl[3] = {};
  si5351Freq_t _snClock[3] = {SI5351_1500, SI5351_1500, SI5351_1500};
  u8_t _sn76489ChMask = 0x00;  // bit 0-3/4-7 = SN76489 (1)/(2) tone CH1-3/noise
  u8_t _ym2612TlReg[3][2][16] = {};
  bool _ym2612TlRegValid[3][2][16] = {};
  u8_t _ym2612FreqLow[3][2][3] = {};
  u8_t _ym2612FreqHigh[3][2][3] = {};
  u8_t _ym2612PlaybackFreqLow[2][3] = {};
  u8_t _ym2612PlaybackFreqHigh[2][3] = {};
  u8_t _ym2612PlaybackKeyOnSlots[6] = {};
  u8_t _ym2612Alg[3][2][3] = {};
  u8_t _ym2612KeyOnSlots[3][6] = {};
  u8_t _ym2612DacLevelDecimator = 0;
  u8_t _ym2612DacLevelPeak = 0;
  byte _ym2612DacData[3] = {0x80, 0x80, 0x80};
  uint16_t _appliedChannelMask = 0;
  volatile u8_t _ym2612OutputMode = 0;  // FMPCM_BOTH。設定変更時だけ同期する。
  volatile bool _ym2612OutputModeApplyPending = false;
  volatile u8_t _pendingYm2612ChToggle = 0x00;
  volatile u8_t _pendingSn76489ChToggle = 0x00;
  volatile bool _pendingChannelMaskReset = false;
  QueueHandle_t _midiNoteQueue = nullptr;
  u8_t _midiActiveMask = 0;
  int8_t _midiChannelNote[6] = {-1, -1, -1, -1, -1, -1};
  uint32_t _midiVoiceOrder[6] = {};
  uint32_t _midiVoiceCounter = 0;

  byte _applySN76489ChannelMask(byte data, uint8_t chipno) const;
  byte _applyYM2612ChannelMask(byte bank, byte addr, byte data, uint8_t chipno) const;
  void _setYM2612(byte bank, byte addr, byte data, uint8_t chipno, bool midiWrite);
  void _cacheYM2612PlaybackControl(byte bank, byte addr, byte data, uint8_t chipno);
  bool _holdYM2612PlaybackControl(byte bank, byte addr, byte data, uint8_t chipno) const;
  void _startMidiNote(uint8_t ch, uint8_t note);
  void _stopMidiNote(uint8_t ch);
  void _restoreYM2612PlaybackControl(uint8_t ch);
  void _writeCachedSN76489Volume(uint8_t ch);
  void _updateSN76489VisualState(byte data, uint8_t chipno, si5351Freq_t freq);
  void _updateSN76489ChannelNote(uint8_t chipno, uint8_t ch, si5351Freq_t freq);
  void _updateYM2612VisualState(byte bank, byte addr, byte data, uint8_t chipno);
  void _updateYM2612KeyState(byte data, uint8_t chipno);
  void _updateYM2612PanState(byte bank, byte addr, byte data);
  void _updateYM2612DacLevel(byte data, uint8_t chipno);
  uint8_t _getYM2612DisplayLevel(uint8_t chipno, uint8_t ch) const;
  void _updateYM2612TrackLevel(uint8_t chipno, uint8_t ch);
  void _writeCachedYM2612ChannelTl(uint8_t chipno, uint8_t ch);
  void _writeCachedYM2612Tl(uint8_t chipno);
};

extern FMChip FM;
#endif
