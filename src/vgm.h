#ifndef VGM_H
#define VGM_H

#include <vector>

#include "SI5351.hpp"
#include "common.h"
#include "disp.h"

#define XGM1_MAX_PCM_CH 8
#define XGM1_PCM_DELAY 68

// XGM V2 FM
#define WAIT_SHORT 0x00 ... 0x0e
#define WAIT_LONG 0x0F
#define PCM 0x10 ... 0x1f
#define FM_LOAD_INST 0x20 ... 0x2f
#define FM_FREQ 0x30 ... 0x3f
#define FM_KEY 0x40 ... 0x4f
#define FM_KEY_SEQ 0x50 ... 0x5f
#define FM0_PAN 0x60 ... 0x6f
#define FM1_PAN 0x70 ... 0x7f
#define FM_FREQ_WAIT 0x80 ... 0x8f
#define FM_TL 0x90 ... 0x9f
#define FM_FREQ_DELTA 0xa0 ... 0xaf
#define FM_FREQ_DELTA_WAIT 0xb0 ... 0xbf
#define FM_TL_DELTA 0xc0 ... 0xcf
#define FM_TL_DELTA_WAIT 0xd0 ... 0xdf
#define FM_WRITE 0xe0 ... 0xef
#define FRAME_DELAY 0xf0

#define FM_KEY_ADV 0xf8
#define FM_LFO 0xf9
#define FM_CH3_SPECIAL_ON 0xfa
#define FM_CH3_SPECIAL_OFF 0xfb
#define FM_DAC_ON 0xfc
#define FM_DAC_OFF 0xfd
#define FM_LOOP 0xff

// XGM V2 PSG
#define PSG_WAIT_SHORT 0x00 ... 0x0d
#define PSG_WAIT_LONG 0x0e
#define PSG_LOOP 0x0f
#define PSG_FREQ_LOW 0x10 ... 0x1f
#define PSG_FREQ 0x20 ... 0x2f
#define PSG_FREQ_WAIT 0x30 ... 0x3f
#define PSG_FREQ0_DELTA 0x40 ... 0x4f
#define PSG_FREQ1_DELTA 0x50 ... 0x5f
#define PSG_FREQ2_DELTA 0x60 ... 0x6f
#define PSG_FREQ3_DELTA 0x70 ... 0x7f
#define PSG_ENV0 0x80 ... 0x8f
#define PSG_ENV1 0x90 ... 0x9f
#define PSG_ENV2 0xa0 ... 0xaf
#define PSG_ENV3 0xb0 ... 0xbf
#define PSG_ENV0_DELTA 0xc0 ... 0xcf
#define PSG_ENV1_DELTA 0xd0 ... 0xdf
#define PSG_ENV2_DELTA 0xe0 ... 0xef
#define PSG_ENV3_DELTA 0xf0 ... 0xff

#define XGM2_PCM_DELAY 72

// Current format
typedef enum {
  FORMAT_UNKNOWN,
  FORMAT_VGM,
  FORMAT_XGM,
  FORMAT_XGM2,
} t_format;

const std::vector<String> FORMAT_LABEL = {"--", "VGM", "XGM1", "XGM2"};

// GD3 構造体
typedef struct {
  String trackEn, trackJp, gameEn, gameJp, systemEn, systemJp, authorEn, authorJp, date, converted, notes;
} t_gd3;

// チップ定義
typedef enum {
  CHIP_NONE,
  CHIP_SN76489_0,
  CHIP_SN76489_1,
  CHIP_YM2413,
  CHIP_YM2612,
  CHIP_YM2151,
  CHIP_YM2203_0,
  CHIP_YM2203_1,
  CHIP_YM2608,
  CHIP_YM2610,
  CHIP_YM3812,
  CHIP_AY8910,
  CHIP_YMF262,
} t_chip;

// クロック使用番号
typedef enum { CLK_0, CLK_1, CLK_2, CLK_3 } t_clockSlot;

// チップ名
const std::vector<String> CHIP_LABEL = {"",       "SN76489", "SN76489", "YM2413", "YM2612", "YM2151", "YM2203",
                                        "YM2203", "YM2608",  "YM2610",  "YM3812", "AY8910", "YMF262"};

class VGM {
 public:
  t_format format;

  u32_t version;     // VGM バージョン
  u32_t dataOffset;  // データオフセット
  u32_t loopOffset;  // ループオフセット
  u32_t gd3Offset;   // gd3オフセット
  // u32_t totalSamples;  // 全サンプル数
  boolean SN76489_Freq0is0X400;  // SN76489 が Sega VDP ではない

  std::vector<si5351Freq_t> freq = {SI5351_UNDEFINED, SI5351_UNDEFINED};

  s8_t chipSlot[11];

  bool vgmLoaded = false;
  bool xgmLoaded = false;

  u8_t XGMVersion;  // XGM バージョン 1 or 2
  std::vector<u32_t> XGMSampleAddressTable;
  std::vector<u32_t> XGMSampleSizeTable;

  u32_t XGM_SLEN;
  u32_t XGM_MLEN;
  u8_t XGM_FLAGS;
  u32_t XGM_FMLEN;   // XGM2
  u32_t XGM_PSGLEN;  // XGM2

  VGM();
  bool ready();     // VGM の再生準備
  bool XGMReady();  // XGM の再生準備
  void vgmProcess();
  void vgmProcessMain();
  void xgmProcess();
  void xgm2Process();
  u64_t getCurrentTime();

 private:
  t_gd3 gd3;

  u16_t _vgmLoop;
  u64_t _vgmSamples;
  u64_t _vgmRealSamples;
  u64_t _vgmStart;
  u64_t _vgmWaitUntil;
  u32_t _pcmpos = 0;
  s64_t micros64();

  u32_t _xgmSamplePos[XGM1_MAX_PCM_CH];
  u8_t _xgmSampleId[XGM1_MAX_PCM_CH];
  u8_t _xgmPriorities[XGM1_MAX_PCM_CH];
  bool _xgmSampleOn[XGM1_MAX_PCM_CH];
  bool _xgmPCMHalfSpeed[3];
  bool _xgmPCMHalfSent[3];
  u32_t _xgmFrame;
  u32_t _xgmYMSNFrame;
  u64_t _xgmStartTick;
  u64_t _xgmWaitUntil;
  u64_t _xgmWaitYMUntil;
  u64_t _xgmWaitPsgUntil;
  bool _xgmIsNTSC;

  u8_t _xgmYmState[2][0x100];
  s16_t _xgmPsgState[2][4];
  u32_t _xgmYMFrame, _xgmPSGFrame;

  u32_t _xgm2_ym_offset;
  u32_t _xgm2_ym_pos;
  u32_t _xgm2_psg_offset;
  u32_t _xgm2_psg_pos;

  si5351Freq_t normalizeFreq(u32_t freq, t_chip chip);

  u32_t _gd3p;
  void _parseGD3(u32_t pos);
  String _digGD3();
  void _resetGD3();

  // when reach the end of the song
  void endProcedure();

  // xgm1
  bool _xgm1ProcessYMSN();
  void _xgm1ProcessPCM();

  // xgm2
  bool _xgm2ProcessYM();
  bool _xgm2ProcessSN();
  void _xgm2ProcessPCM();

  int _getYMPort(u32_t pos);
  int _getYMChannel(u32_t pos);
  int _getYMSlot(u8_t command);
  u8_t _getChannel(u32_t pos);
};

extern VGM vgm;

#endif
