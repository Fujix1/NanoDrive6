#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nd6clock {
// Frequencies implemented by SI5351_cls::setFreq's fixed PLL table.
// A VGM clock can differ by a handful of hertz from the nominal hardware clock.
// Passing that value through unchanged would select SI5351's 4 MHz default.
static constexpr uint32_t Supported[] = {
    1250000, 1500000, 1536000, 1789772, 2000000, 2578000, 3000000,
    3072000, 3332000, 3375000, 3500000, 3579545, 4000000, 4500000,
    5000000, 6000000, 6144000, 7159000, 7600489, 7670453, 7987000,
    8000000, 9000000, 14318180};

inline uint32_t match(uint32_t requested) {
  if (!requested) return 0;
  uint32_t best = requested;
  uint32_t delta = UINT32_MAX;
  for (size_t i = 0; i < sizeof(Supported) / sizeof(Supported[0]); ++i) {
    const uint32_t candidate = Supported[i];
    const uint32_t diff = requested > candidate ? requested - candidate : candidate - requested;
    if (diff < delta) { delta = diff; best = candidate; }
  }
  // At most 0.1%: deliberately keep distinct chip clocks apart.
  return uint64_t(delta) * 1000000 <= uint64_t(requested) * 1000 ? best : requested;
}
}  // namespace nd6clock
