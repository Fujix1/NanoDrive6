#include "../include/ssg_to_sn.h"
#include <cassert>
#include <vector>

int main() {
  SsgToSn converter;
  std::vector<uint8_t> bytes;
  auto sink = [&](uint8_t b) { bytes.push_back(b); };
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
  converter.write(7, 1, sink); // Mixer disables A, also initializes silent B/C.
  assert(bytes.front() == 0x9f);
  bytes.clear();
  converter.write(7, 0, sink);
  assert((bytes == std::vector<uint8_t>{0x93}));
  bytes.clear();
  converter.write(8, 0, sink);
  assert((bytes == std::vector<uint8_t>{0x9f}));
  bytes.clear();
  converter.write(8, 16, sink); // Envelope is unsupported, stays silent.
  assert(bytes.empty());
  converter.write(0x2e, 0, sink); // Double SSG frequency -> period 19.
  assert(bytes[0] == 0x83 && bytes[1] == 1);
  converter.reset();
  bytes.clear();
  converter.write(8, 15, sink);
  assert(bytes.empty());
  // Lilia's channel C vibrato must not collapse to one rounded SN period.
  converter.reset(4000000, 4000000);
  converter.write(10, 9, sink);
  bytes.clear();
  for (uint8_t period : {140, 141, 142, 141, 140}) {
    converter.write(4, period, sink);
  }
  assert((bytes == std::vector<uint8_t>{
      0xcc, 8, 0xcd, 8, 0xce, 8, 0xcd, 8, 0xcc, 8}));
  // Apathetic Story bass: 4 MHz source periods 1704/1912 fit at 2 MHz.
  converter.reset(4000000, 2000000);
  converter.write(5, 6, sink);
  bytes.clear();
  converter.write(4, 0xa8, sink); // 0x6a8 / 2 = 852
  assert((bytes == std::vector<uint8_t>{0xc4, 53}));
  converter.write(5, 7, sink);
  bytes.clear();
  converter.write(4, 0x78, sink); // 0x778 / 2 = 956
  assert((bytes == std::vector<uint8_t>{0xcc, 59}));
}
