#include "ssg_to_sn.h"

#include <cassert>
#include <cstddef>
#include <vector>

static uint8_t envelopeVolumeAt(uint8_t shape, uint64_t sample) {
  SsgToSn converter;
  uint8_t volume = 0xff;
  auto sink = [&](uint8_t value) {
    if ((value & 0xf0) == 0x90) volume = value & 0x0f;
  };

  // 705600 / 16 = 44100: period 1 なら VGM 1 sample ごとに1段進む。
  converter.reset(705600, 2000000);
  converter.write(11, 1, sink);
  converter.write(12, 0, sink);
  converter.write(13, shape, sink);
  converter.write(8, 0x10, sink);
  converter.advanceTo(sample, sink);
  assert(volume != 0xff);
  return volume;
}

int main() {
  SsgToSn converter;
  std::vector<uint8_t> bytes;
  auto sink = [&](uint8_t value) { bytes.push_back(value); };

  converter.reset(4000000, 1500000);
  converter.write(0, 100, sink);  // 1250 Hz -> SN period 38 (rounded).
  assert((bytes == std::vector<uint8_t>{0x86, 0x02, 0x9f}));
  bytes.clear();
  converter.write(8, 15, sink);
  assert((bytes == std::vector<uint8_t>{0x90}));
  bytes.clear();
  converter.write(8, 15, sink);
  assert(bytes.empty());
  converter.write(8, 13, sink);
  assert((bytes == std::vector<uint8_t>{0x93}));
  bytes.clear();
  converter.write(7, 1, sink);  // Mixer disables A, also initializes silent B/C.
  assert(bytes.front() == 0x9f);
  bytes.clear();
  converter.write(7, 0, sink);
  assert((bytes == std::vector<uint8_t>{0x93}));
  bytes.clear();
  converter.write(8, 0, sink);
  assert((bytes == std::vector<uint8_t>{0x9f}));
  bytes.clear();
  converter.write(8, 16, sink);  // Shape 0 の初期値は最大音量。
  assert((bytes == std::vector<uint8_t>{0x90}));
  bytes.clear();
  converter.write(0x2e, 0, sink);  // Double SSG frequency -> period 19.
  assert(bytes[0] == 0x83 && bytes[1] == 1);

  // AY-3-8910 tone は header clock/(16*period)。同一クロックなら YM2203 の半周期になる。
  converter.reset(2000000, 2000000, SsgToSn::Source::AY8910);
  bytes.clear();
  converter.write(0, 100, sink);
  assert((bytes == std::vector<uint8_t>{0x82, 0x03, 0x9f}));

  // AY のエンベロープは16段で、period 1・705.6 kHz なら1 sampleごとに1段進む。
  converter.reset(705600, 2000000, SsgToSn::Source::AY8910);
  converter.write(11, 1, sink);
  converter.write(13, 8, sink);
  converter.write(8, 0x10, sink);
  bytes.clear();
  converter.advanceTo(15, sink);
  assert((bytes == std::vector<uint8_t>{0x9f}));
  bytes.clear();
  converter.advanceTo(16, sink);
  assert((bytes == std::vector<uint8_t>{0x90}));

  converter.reset();
  bytes.clear();
  converter.write(8, 15, sink);
  assert(bytes.empty());

  // Lilia's channel C vibrato must not collapse to one rounded SN period.
  converter.reset(4000000, 4000000);
  converter.write(10, 9, sink);
  bytes.clear();
  static const uint8_t vibratoPeriods[] = {140, 141, 142, 141, 140};
  for (uint8_t period : vibratoPeriods) {
    converter.write(4, period, sink);
  }
  assert((bytes == std::vector<uint8_t>{0xcc, 8, 0xcd, 8, 0xce, 8, 0xcd, 8, 0xcc, 8}));

  // Apathetic Story bass: 4 MHz source periods 1704/1912 fit at 2 MHz.
  converter.reset(4000000, 2000000);
  converter.write(5, 6, sink);
  bytes.clear();
  converter.write(4, 0xa8, sink);  // 0x6a8 / 2 = 852
  assert((bytes == std::vector<uint8_t>{0xc4, 53}));
  converter.write(5, 7, sink);
  bytes.clear();
  converter.write(4, 0x78, sink);  // 0x778 / 2 = 956
  assert((bytes == std::vector<uint8_t>{0xcc, 59}));

  // 全16 shape の開始、最初のランプ終端、次周期先頭を確認する。
  static const uint8_t at0[16] = {
      0, 0, 0, 0, 15, 15, 15, 15, 0, 0, 0, 0, 15, 15, 15, 15};
  static const uint8_t at31[16] = {
      15, 15, 15, 15, 0, 0, 0, 0, 15, 15, 15, 15, 0, 0, 0, 0};
  static const uint8_t at32[16] = {
      15, 15, 15, 15, 15, 15, 15, 15, 0, 15, 15, 0, 15, 0, 0, 15};
  static const uint8_t at63[16] = {
      15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 0, 0, 0, 0, 15, 15};
  static const uint8_t at64[16] = {
      15, 15, 15, 15, 15, 15, 15, 15, 0, 15, 0, 0, 15, 0, 15, 15};
  for (uint8_t shape = 0; shape < 16; ++shape) {
    assert(envelopeVolumeAt(shape, 0) == at0[shape]);
    assert(envelopeVolumeAt(shape, 31) == at31[shape]);
    assert(envelopeVolumeAt(shape, 32) == at32[shape]);
    assert(envelopeVolumeAt(shape, 63) == at63[shape]);
    assert(envelopeVolumeAt(shape, 64) == at64[shape]);
  }

  // 遅延分は全段階を連打せず、到達した音量だけ送る。
  converter.reset(705600, 2000000);
  converter.write(11, 1, sink);
  converter.write(13, 8, sink);
  converter.write(8, 0x10, sink);
  bytes.clear();
  converter.advanceTo(31, sink);
  assert((bytes == std::vector<uint8_t>{0x9f}));

  // Shape 書き込みで位相が再スタートする。
  bytes.clear();
  converter.write(13, 8, sink);
  assert((bytes == std::vector<uint8_t>{0x90}));

  // 16bit periodは段階更新間隔へ反映する。
  converter.reset(705600, 2000000);
  converter.write(11, 2, sink);
  converter.write(12, 0, sink);
  converter.write(13, 8, sink);
  converter.write(8, 0x10, sink);
  bytes.clear();
  converter.advanceTo(3, sink);
  assert(bytes.empty());
  converter.advanceTo(4, sink);
  assert((bytes == std::vector<uint8_t>{0x92}));
}
