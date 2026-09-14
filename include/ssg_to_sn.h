#pragma once
#include <stdint.h>

// Experimental YM2203 SSG -> SN76489 fixed-volume tone conversion.
// No noise/envelope synthesis. The sink receives complete SN register writes.
class SsgToSn {
 public:
  void reset(uint32_t sourceClock = 0, uint32_t targetClock = 0) {
    source_ = sourceClock;
    target_ = targetClock;
    multiplier_ = 1;
    for (auto &r : regs_) r = 0;
    for (int ch = 0; ch < 3; ++ch) {
      periods_[ch] = 0xffff;
      volumes_[ch] = 0xff;
    }
  }

  template<class Sink> void write(uint8_t reg, uint8_t data, Sink sink) {
    if (!source_ || !target_) return;
    if (reg < 16) regs_[reg] = data;
    else if (reg == 0x2d) multiplier_ = 1;
    else if (reg == 0x2e && multiplier_ == 1) multiplier_ = 2;
    else if (reg == 0x2f) multiplier_ = 4;
    else return;
    if (reg < 6) update(reg / 2, sink);
    else if (reg >= 8 && reg <= 10) update(reg - 8, sink);
    else if (reg == 7 || reg >= 0x2d)
      for (uint8_t ch = 0; ch < 3; ++ch) update(ch, sink);
  }

 private:
  template<class Sink> void update(uint8_t ch, Sink sink) {
    uint32_t period = regs_[ch * 2] | ((regs_[ch * 2 + 1] & 15) << 8);
    if (!period) period = 1;
    // Default YM2203 tone = master / (32 * period); SN = clock / (32 * N).
    const uint64_t denominator = uint64_t(source_) * multiplier_;
    uint64_t n = (uint64_t(period) * target_ + denominator / 2) / denominator;
    if (n < 1) n = 1;
    if (n > 1023) n = 1023;  // SN has only a 10-bit period.
    if (periods_[ch] != n) {
      sink(uint8_t(0x80 | (ch << 5) | (n & 15)));
      sink(uint8_t((n >> 4) & 63));
      periods_[ch] = uint16_t(n);
    }
    const uint8_t level = regs_[8 + ch] & 15;
    // Approximate 3 dB SSG steps with 2 dB SN steps; retain quiet nonzero tones.
    static const uint8_t attenuation[16] = {15,14,14,14,14,14,14,12,11,9,8,6,5,3,2,0};
    const uint8_t volume = ((regs_[7] & (1 << ch)) ||
                            (regs_[8 + ch] & 16)) ? 15 : attenuation[level];
    if (volumes_[ch] != volume) {
      sink(uint8_t(0x90 | (ch << 5) | volume));
      volumes_[ch] = volume;
    }
  }
  uint32_t source_ = 0, target_ = 0;
  uint8_t multiplier_ = 1;
  uint8_t regs_[16] = {};
  uint16_t periods_[3] = {};
  uint8_t volumes_[3] = {};
};
