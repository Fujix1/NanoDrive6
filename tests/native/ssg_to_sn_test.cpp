#include "ssg_to_sn.h"

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <vector>

static bool containsBytes(const std::vector<uint8_t>& bytes,
                          std::initializer_list<uint8_t> expected) {
  if (expected.size() > bytes.size()) return false;
  for (std::size_t start = 0; start + expected.size() <= bytes.size(); ++start) {
    std::size_t offset = 0;
    for (uint8_t value : expected) {
      if (bytes[start + offset] != value) break;
      ++offset;
    }
    if (offset == expected.size()) return true;
  }
  return false;
}

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
  converter.write(7, 0x39, sink);  // Mixer disables tone A and all noise.
  assert(bytes.front() == 0x9f);
  bytes.clear();
  converter.write(7, 0x38, sink);
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

  // Vampire Killer の主パターン: A/B tone + C noise。
  // SN tone 3をnoise clockへ予約し、NP=10をSN period 6へ変換する。
  converter.reset(1789773, 2000000, SsgToSn::Source::AY8910);
  converter.write(0, 100, sink);
  converter.write(2, 200, sink);
  converter.write(4, 44, sink);
  converter.write(5, 1, sink);  // C period = 300
  converter.write(8, 15, sink);
  converter.write(9, 15, sink);
  converter.write(10, 15, sink);
  converter.write(6, 10, sink);
  bytes.clear();
  converter.write(7, 0x1c, sink);
  assert(containsBytes(bytes, {0xc6, 0x00}));
  assert(containsBytes(bytes, {0xe7, 0xf1}));

  // Bをnoise専用へ切り替える場合、C toneをSN tone 2へ移す。
  bytes.clear();
  converter.write(7, 0x2a, sink);
  assert(containsBytes(bytes, {0xa8, 0x0a}));  // C period 300 -> SN period 168

  // Aをnoise専用へ切り替える場合、B toneをSN tone 1へ移す。
  bytes.clear();
  converter.write(7, 0x31, sink);
  assert(containsBytes(bytes, {0x80, 0x07}));  // B period 200 -> SN period 112

  // 3 toneとnoiseが同時ならtone割り当てを維持し、最も近い固定noiseへ退避する。
  bytes.clear();
  converter.write(7, 0x00, sink);
  assert(containsBytes(bytes, {0xe4}));

  // Noise専用chのエンベロープもSN noise volumeへ反映する。
  converter.reset(705600, 2000000, SsgToSn::Source::AY8910);
  converter.write(6, 1, sink);
  converter.write(11, 1, sink);
  converter.write(13, 8, sink);
  converter.write(8, 0x10, sink);
  converter.write(7, 0x37, sink);  // tone全停止、noise Aのみ。
  bytes.clear();
  converter.advanceTo(15, sink);
  assert((bytes == std::vector<uint8_t>{0xff}));
  bytes.clear();
  converter.advanceTo(16, sink);
  assert((bytes == std::vector<uint8_t>{0xf1}));

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
