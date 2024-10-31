#ifndef VGM_H
#define VGM_H

#include <vector>

#include "SI5351.hpp"
#include "common.h"
#include "disp.h"

// Current format
typedef enum {
  FORMAT_VGM,
  FORMAT_XGM,
  FORMAT_XGM2,
} t_format;

const std::vector<String> FORMAT_LABEL = {"VGM", "XGM"};

// GD3 構造体
typedef struct {
  String trackEn, trackJp, gameEn, gameJp, systemEn, systemJp, authorEn, authorJp, date, converted, notes;
} t_gd3;

// チップ定義
typedef enum {
  CHIP_NONE,
  CHIP_SN76489_0,
  CHIP_SN76489_1,
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

// チップ名
const std::vector<String> CHIP_LABEL = {"",       "SN76489", "SN76489", "YM2612", "YM2151", "YM2203",
                                        "YM2203", "YM2608",  "YM2610",  "YM3812", "AY8910", "YMF262"};

class VGM {
 public:
  uint8_t *vgmData;       // データ本体
  uint32_t size = 0;      // ファイルサイズ
  uint32_t version;       // VGM バージョン
  uint32_t dataOffset;    // データオフセット
  uint32_t loopOffset;    // ループオフセット
  uint32_t gd3Offset;     // gd3オフセット
  uint32_t totalSamples;  // 全サンプル数
  t_format format;

  std::vector<si5351Freq_t> freq = {SI5351_UNDEFINED, SI5351_UNDEFINED};

  int8_t chipSlot[10];

  bool vgmLoaded = false;
  bool xgmLoaded = false;

  uint32_t XGMVersion;  // XGM バージョン 1 or 2
  std::vector<uint32_t> XGMSampleAddressTable;
  std::vector<uint32_t> XGMSampleSizeTable;

  uint32_t XGM_SLEN;
  uint32_t XGM_MLEN;
  uint8_t XGM_FLAGS;

  VGM();
  bool ready();     // VGM の再生準備
  bool XGMReady();  // XGM の再生準備
  void vgmProcess();
  void xgmProcess();
  uint64_t getCurrentTime();

 private:
  uint32_t _pos;
  t_gd3 gd3;
  int32_t _vgmDelay = 0;
  uint16_t _vgmLoop = 0;
  uint64_t _vgmSamples;
  uint64_t _vgmStart = 0;
  uint32_t _pcmpos = 0;

  u32_t _xgmSamplePos[4] = {0, 0, 0, 0};
  u8_t _xgmSampleId[4] = {0, 0, 0, 0};
  u8_t _xgmPriorities[4] = {0, 0, 0, 0};
  bool _xgmSampleOn[4] = {false, false, false, false};
  u32_t _xgmFrame;
  u32_t _xgmStartTick;
  u32_t _xgmWaitUntil;
  bool _xgmIsNTSC;

  uint8_t get_ui8();
  uint16_t get_ui16();
  uint32_t get_ui24();
  uint32_t get_ui32();
  uint8_t get_ui8_at(uint32_t p);
  int8_t get_i8_at(uint32_t p);
  uint16_t get_ui16_at(uint32_t p);
  uint32_t get_ui24_at(uint32_t p);
  uint32_t get_ui32_at(uint32_t p);
  si5351Freq_t normalizeFreq(uint32_t freq, t_chip chip);

  uint32_t _gd3p;
  void _parseGD3();
  String _digGD3();
  uint8_t getNumWrites(u8_t command);
};

extern VGM vgm;

#endif
