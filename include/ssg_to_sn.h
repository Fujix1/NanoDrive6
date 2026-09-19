#pragma once

#include <stdint.h>

// YM2203 SSG / AY-3-8910 -> SN76489 のトーン変換。
// ノイズは変換しない。エンベロープは VGM の 44.1 kHz 時刻単位で音量へ変換する。
class SsgToSn {
 public:
  enum class Source : uint8_t {
    YM2203,
    AY8910,
  };

  void reset(uint32_t sourceClock = 0, uint32_t targetClock = 0,
             Source source = Source::YM2203) {
    source_ = sourceClock;
    target_ = targetClock;
    toneDivider_ = source == Source::AY8910 ? 16 : 32;
    envelopeSteps_ = source == Source::AY8910 ? 16 : 32;
    multiplier_ = 1;
    sample_ = 0;
    envelopePhase_ = 0;
    envelopeState_ = 0;
    envelopeHeld_ = false;
    for (auto& reg : regs_) reg = 0;
    for (int ch = 0; ch < 3; ++ch) {
      periods_[ch] = 0xffff;
      volumes_[ch] = 0xff;
    }
  }

  template <class Sink>
  void write(uint8_t reg, uint8_t data, Sink sink) {
    if (!source_ || !target_) return;

    if (reg < 16) {
      regs_[reg] = data;
    } else if (reg == 0x2d) {
      multiplier_ = 1;
    } else if (reg == 0x2e && multiplier_ == 1) {
      multiplier_ = 2;
    } else if (reg == 0x2f) {
      multiplier_ = 4;
    } else {
      return;
    }

    if (reg < 6) {
      update(reg / 2, sink);
    } else if (reg >= 8 && reg <= 10) {
      update(reg - 8, sink);
    } else if (reg == 13) {
      // Shape 書き込みはエンベロープ位相を先頭へ戻す。
      envelopePhase_ = 0;
      envelopeState_ = 0;
      envelopeHeld_ = false;
      for (uint8_t ch = 0; ch < 3; ++ch) updateVolume(ch, sink);
    } else if (reg == 7 || reg >= 0x2d) {
      for (uint8_t ch = 0; ch < 3; ++ch) update(ch, sink);
    }
  }

  // sample は VGM の絶対サンプル位置（44.1 kHz）。遅延時は途中の段階を
  // 連打せず、到達した位相の音量だけを sink へ渡す。
  template <class Sink>
  void advanceTo(uint64_t sample, Sink sink) {
    if (!source_ || sample <= sample_) return;

    const uint64_t elapsed = sample - sample_;
    sample_ = sample;
    if (envelopeHeld_) return;

    uint32_t period = uint32_t(regs_[11]) | (uint32_t(regs_[12]) << 8);
    if (!period) period = 1;

    // AY-3-8910 は master/16、YM2203 内蔵 SSG は32段を倍速で進めるため、
    // どちらも1段 = 16 * period / (VGM header clock * prescaler) 秒となる。
    const uint64_t threshold = uint64_t(44100) * 16 * period;
    envelopePhase_ += elapsed * uint64_t(source_) * multiplier_;
    const uint64_t steps = envelopePhase_ / threshold;
    envelopePhase_ %= threshold;
    if (!steps) return;

    advanceEnvelope(steps);
    for (uint8_t ch = 0; ch < 3; ++ch) {
      if (regs_[8 + ch] & 0x10) updateVolume(ch, sink);
    }
  }

 private:
  template <class Sink>
  void update(uint8_t ch, Sink sink) {
    uint32_t period = regs_[ch * 2] | ((regs_[ch * 2 + 1] & 15) << 8);
    if (!period) period = 1;

    // YM2203 tone = master/(32*period)、AY tone = master/(16*period)。
    // SN tone = clock/(32*N)。
    const uint64_t denominator = uint64_t(source_) * multiplier_ * 32;
    const uint64_t numerator = uint64_t(period) * target_ * toneDivider_;
    uint64_t n = (numerator + denominator / 2) / denominator;
    if (n < 1) n = 1;
    if (n > 1023) n = 1023;

    if (periods_[ch] != n) {
      sink(uint8_t(0x80 | (ch << 5) | (n & 15)));
      sink(uint8_t((n >> 4) & 63));
      periods_[ch] = uint16_t(n);
    }

    updateVolume(ch, sink);
  }

  void advanceEnvelope(uint64_t steps) {
    const bool stopAfterFirstCycle = !(regs_[13] & 0x08) || (regs_[13] & 0x01);
    if (stopAfterFirstCycle) {
      const uint64_t stepsUntilEnd =
          envelopeState_ < envelopeSteps_ ? envelopeSteps_ - envelopeState_ : 0;
      if (envelopeState_ >= envelopeSteps_ || steps >= stepsUntilEnd) {
        envelopeState_ = envelopeSteps_;
        envelopeHeld_ = true;
        return;
      }
    }
    envelopeState_ = uint8_t((envelopeState_ + steps) & (envelopeSteps_ * 2 - 1));
  }

  uint8_t envelopeLevel() const {
    const bool continueEnvelope = regs_[13] & 0x08;
    const bool attack = regs_[13] & 0x04;
    const bool alternate = regs_[13] & 0x02;
    const bool hold = regs_[13] & 0x01;

    const uint8_t maximum = envelopeSteps_ - 1;
    if ((hold || !continueEnvelope) && envelopeState_ >= envelopeSteps_) {
      return ((attack ^ alternate) && continueEnvelope) ? maximum : 0;
    }

    bool effectiveAttack = attack;
    if (alternate && (envelopeState_ & envelopeSteps_)) effectiveAttack = !effectiveAttack;
    return uint8_t(envelopeState_ & maximum) ^ (effectiveAttack ? 0 : maximum);
  }

  template <class Sink>
  void updateVolume(uint8_t ch, Sink sink) {
    uint8_t level = regs_[8 + ch] & 15;
    if (regs_[8 + ch] & 0x10) {
      const uint8_t envelope = envelopeLevel();
      // YM2203 の32段だけ、既存の16段SSG音量曲線へ丸める。
      level = envelopeSteps_ == 32 ? envelope >> 1 : envelope;
    }

    // SSG の約 3 dB ステップを SN の 2 dB ステップへ近似する。
    static const uint8_t attenuation[16] = {15, 14, 14, 14, 14, 14, 14, 12,
                                            11, 9,  8,  6,  5,  3,  2,  0};
    const uint8_t volume = (regs_[7] & (1 << ch)) ? 15 : attenuation[level];
    if (volumes_[ch] != volume) {
      sink(uint8_t(0x90 | (ch << 5) | volume));
      volumes_[ch] = volume;
    }
  }

  uint32_t source_ = 0;
  uint32_t target_ = 0;
  uint8_t toneDivider_ = 32;
  uint8_t envelopeSteps_ = 32;
  uint8_t multiplier_ = 1;
  uint64_t sample_ = 0;
  uint64_t envelopePhase_ = 0;
  uint8_t envelopeState_ = 0;
  bool envelopeHeld_ = false;
  uint8_t regs_[16] = {};
  uint16_t periods_[3] = {};
  uint8_t volumes_[3] = {};
};
