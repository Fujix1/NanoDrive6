#include "ClockMatch.h"
#include <stdio.h>
#include <stdint.h>
#include <fstream>
#include <vector>

static uint32_t le32(const uint8_t* b) {
  return uint32_t(b[0]) | (uint32_t(b[1]) << 8) |
         (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}
int main(int argc, char** argv) {
  if (argc != 2) { fprintf(stderr, "Usage: clock_tests file.vgm\n"); return 2; }
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<uint8_t> h(0x40);
  in.read(reinterpret_cast<char*>(h.data()), h.size());
  if (!in || h[0]!='V' || h[1]!='g' || h[2]!='m' || h[3]!=' ') return 2;
  const uint32_t psg=le32(h.data()+0x0c), fm=le32(h.data()+0x2c);
  const uint32_t chosenFm=nd6clock::match(fm), chosenPsg=nd6clock::match(psg);
  printf("VGM YM2612=%u -> %u Hz; PSG=%u -> %u Hz\n", fm, chosenFm, psg, chosenPsg);
  if (fm!=7670454 || chosenFm!=7670453 || psg!=3579575 || chosenPsg!=3579545) return 1;
  if (nd6clock::match(7670453)!=7670453 || nd6clock::match(7600489)!=7600489 ||
      nd6clock::match(8000000)!=8000000 || nd6clock::match(0)!=0 ||
      nd6clock::match(7300000)!=7300000) return 1;
  puts("PASS: yzs001 header selects the intended chip clocks, including nominal/unsupported boundaries");
}
