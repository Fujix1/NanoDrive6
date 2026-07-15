#include "nd.h"

#include "NJU72341.h"

const std::array<String, 15> CHIP_LABEL = {"",       "SN76489", "SN76489", "YM2413", "YM2612",
                                           "YM2151", "YM2203",  "YM2203",  "YM2608", "YM2610",
                                           "YM3526", "YM3812",  "AY8910",  "YMF262", "M6258"};

const std::array<String, 7> FORMAT_LABEL = {"--", "VGM", "VGZ", "MDX", "XGM1", "XGM2", "S98"};

// ND ステートの初期化
t_ndVersion ND::version = nd_v60;
VolumeChip ND::volumeChip = VolumeChip::None;
FileFormat ND::fileFormat = FileFormat::Unknown;
bool ND::canPlay = false;
bool ND::isPaused = false;

std::array<si5351Freq_t, 3> ND::freq = {SI5351_UNDEFINED, SI5351_UNDEFINED, SI5351_UNDEFINED};

byte ND::chipSlot[15];
byte ND::clockSlot[15];  // クロック使用番号

std::vector<String> ND::chipNames;

// チップ名のフォーマット
String ND::formatChipName(si5351Freq_t freq, t_chip chip) {
  char buf[10];

  if (freq != SI5351_UNDEFINED) {
    snprintf(buf, sizeof(buf), "%.4f", (double)freq / 1000000.0);
    buf[5] = '\0';
    return CHIP_LABEL[chip] + " @ " + buf + " MHz";
  }
  return CHIP_LABEL[chip];
}

const char* ND::versionLabel() {
  switch (version) {
    case nd_v61:
      return "6.1";
    case nd_v60:
    default:
      return "6";
  }
}

void ND::resetPlaybackState() {
  fileFormat = FileFormat::Unknown;
  canPlay = false;
  isPaused = false;
  freq.fill(SI5351_UNDEFINED);
  chipNames.clear();
}
