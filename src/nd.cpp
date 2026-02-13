#include "nd.h"

// ND ステートの初期化
FileFormat ND::fileFormat = FileFormat::Unknown;
bool ND::canPlay = false;

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
