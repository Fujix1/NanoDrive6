#pragma once

#include <stdint.h>

// YM2203 SSG / AY-3-8910 -> SN76489 のトーン・ノイズ変換。
// エンベロープは VGM の 44.1 kHz 時刻単位で音量へ変換する。
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
    mixerWritten_ = false;
    noiseControl_ = 0xff;
    for (auto& reg : regs_) reg = 0;
    for (uint8_t sn = 0; sn < 3; ++sn) {
      periods_[sn] = 0xffff;
      ayToSn_[sn] = sn;
    }
    for (auto& volume : volumes_) volume = 0xff;
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
      updateMappedTone(reg / 2, sink);
    } else if (reg == 6) {
      if (hasActiveNoise()) rebuildRouting(sink);
    } else if (reg == 7) {
      mixerWritten_ = true;
      rebuildRouting(sink);
    } else if (reg >= 8 && reg <= 10) {
      // 音量0との切り替えで、ノイズクロック用に tone 3 を予約できるかが変わる。
      if (mixerWritten_ && (regs_[7] & 0x38) != 0x38) {
        rebuildRouting(sink);
      } else {
        updateMappedTone(reg - 8, sink);
      }
    } else if (reg == 13) {
      // Shape 書き込みはエンベロープ位相を先頭へ戻す。
      envelopePhase_ = 0;
      envelopeState_ = 0;
      envelopeHeld_ = false;
      updateAllVolumes(sink);
    } else if (reg >= 0x2d) {
      rebuildRouting(sink);
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
    updateAllVolumes(sink);
  }

 private:
  static constexpr uint8_t UNMAPPED = 0xff;

  bool channelPotentiallyAudible(uint8_t ch) const { return (regs_[8 + ch] & 0x1f) != 0; }

  bool toneEnabled(uint8_t ch) const {
    // 音量0のたびに物理chを入れ替えるとクリック要因になるため、
    // toneの割り当てはR7のルーティングだけで安定させる。
    return !(regs_[7] & (1 << ch));
  }

  bool noiseEnabled(uint8_t ch) const {
    return mixerWritten_ && channelPotentiallyAudible(ch) && !(regs_[7] & (8 << ch));
  }

  bool hasActiveNoise() const {
    for (uint8_t ch = 0; ch < 3; ++ch) {
      if (noiseEnabled(ch)) return true;
    }
    return false;
  }

  uint16_t convertedPeriod(uint32_t period) const {
    if (!period) period = 1;
    // YM2203 tone = master/(32*period)、AY tone = master/(16*period)。
    // SN tone = clock/(32*N)。ノイズも同じ比率で tone 3 の周期へ変換できる。
    const uint64_t denominator = uint64_t(source_) * multiplier_ * 32;
    const uint64_t numerator = uint64_t(period) * target_ * toneDivider_;
    uint64_t n = (numerator + denominator / 2) / denominator;
    if (n < 1) n = 1;
    if (n > 1023) n = 1023;
    return uint16_t(n);
  }

  template <class Sink>
  void writePeriod(uint8_t sn, uint16_t period, Sink sink) {
    if (periods_[sn] == period) return;
    sink(uint8_t(0x80 | (sn << 5) | (period & 15)));
    sink(uint8_t((period >> 4) & 63));
    periods_[sn] = period;
  }

  template <class Sink>
  void writeVolume(uint8_t sn, uint8_t volume, Sink sink) {
    if (volumes_[sn] == volume) return;
    sink(uint8_t(0x90 | (sn << 5) | volume));
    volumes_[sn] = volume;
  }

  uint8_t channelAttenuation(uint8_t ch) const {
    uint8_t level = regs_[8 + ch] & 15;
    if (regs_[8 + ch] & 0x10) {
      const uint8_t envelope = envelopeLevel();
      // YM2203 の32段だけ、既存の16段SSG音量曲線へ丸める。
      level = envelopeSteps_ == 32 ? envelope >> 1 : envelope;
    }

    // SSG の約 3 dB ステップを SN の 2 dB ステップへ近似する。
    static const uint8_t attenuation[16] = {15, 14, 14, 14, 14, 14, 14, 12,
                                            11, 9,  8,  6,  5,  3,  2,  0};
    return attenuation[level];
  }

  template <class Sink>
  void updateTone(uint8_t sn, uint8_t ay, Sink sink) {
    const uint32_t period =
        uint32_t(regs_[ay * 2]) | ((uint32_t(regs_[ay * 2 + 1]) & 15) << 8);
    writePeriod(sn, convertedPeriod(period), sink);
    writeVolume(sn, toneEnabled(ay) ? channelAttenuation(ay) : 15, sink);
  }

  template <class Sink>
  void updateMappedTone(uint8_t ay, Sink sink) {
    for (uint8_t sn = 0; sn < 3; ++sn) {
      if (ayToSn_[sn] == ay) {
        updateTone(sn, ay, sink);
        return;
      }
    }
  }

  uint8_t noiseAttenuation() const {
    // 同じノイズ源を複数chへ出した場合は、各DACの概算振幅を加算して
    // SN noise ch の最も近い減衰量へ丸める。
    static const uint16_t amplitude[16] = {1000, 794, 631, 501, 398, 316, 251, 200,
                                           158, 126, 100, 79,  63,  50,  40,  0};
    uint32_t sum = 0;
    for (uint8_t ch = 0; ch < 3; ++ch) {
      if (noiseEnabled(ch)) sum += amplitude[channelAttenuation(ch)];
    }
    if (!sum) return 15;
    if (sum >= amplitude[0]) return 1;

    uint8_t best = 0;
    uint32_t bestError = amplitude[0] - sum;
    for (uint8_t volume = 1; volume < 15; ++volume) {
      const uint32_t error = amplitude[volume] > sum ? amplitude[volume] - sum
                                                     : sum - amplitude[volume];
      if (error < bestError) {
        best = volume;
        bestError = error;
      }
    }
    // AY/YM2203 のノイズが前に出すぎないよう、SN側だけ約2 dB（1段）下げる。
    return best < 14 ? best + 1 : 14;  // 15はmuteなので最小可聴音量で止める。
  }

  uint8_t fixedNoiseMode() const {
    uint32_t period = regs_[6] & 31;
    if (!period) period = 1;
    const uint64_t sourceDivider = uint64_t(toneDivider_) * period;
    const uint64_t desired =
        (uint64_t(source_) * multiplier_ + sourceDivider / 2) / sourceDivider;

    uint8_t best = 0;
    uint64_t bestError = ~uint64_t(0);
    for (uint8_t mode = 0; mode < 3; ++mode) {
      const uint32_t divider = uint32_t(512) << mode;
      const uint64_t candidate = (uint64_t(target_) + divider / 2) / divider;
      const uint64_t error = candidate > desired ? candidate - desired : desired - candidate;
      if (error < bestError) {
        best = mode;
        bestError = error;
      }
    }
    return best;
  }

  template <class Sink>
  void writeNoiseControl(uint8_t control, Sink sink) {
    if (noiseControl_ == control) return;
    // Noise register 書き込みでLFSRがリセットされるため、モード変更時だけ送る。
    writeVolume(3, 15, sink);
    sink(control);
    noiseControl_ = control;
  }

  template <class Sink>
  void updateNoiseVolume(Sink sink) {
    writeVolume(3, noiseAttenuation(), sink);
  }

  template <class Sink>
  void updateAllVolumes(Sink sink) {
    for (uint8_t sn = 0; sn < 3; ++sn) {
      const uint8_t ay = ayToSn_[sn];
      if (ay != UNMAPPED) {
        writeVolume(sn, toneEnabled(ay) ? channelAttenuation(ay) : 15, sink);
      }
    }
    if (mixerWritten_ || volumes_[3] != 0xff) updateNoiseVolume(sink);
  }

  template <class Sink>
  void rebuildRouting(Sink sink) {
    bool hasNoise = false;
    uint8_t toneCount = 0;
    for (uint8_t ch = 0; ch < 3; ++ch) {
      if (noiseEnabled(ch)) hasNoise = true;
      if (toneEnabled(ch)) ++toneCount;
    }

    // ノイズ使用中にtoneが2本以下なら、SN tone 3を可変ノイズクロックへ予約する。
    const bool variableNoise = hasNoise && toneCount <= 2;
    uint8_t desiredMap[3] = {UNMAPPED, UNMAPPED, UNMAPPED};
    if (variableNoise) {
      uint8_t sn = 0;
      for (uint8_t ay = 0; ay < 3; ++ay) {
        if (toneEnabled(ay)) desiredMap[sn++] = ay;
      }
    } else {
      desiredMap[0] = 0;
      desiredMap[1] = 1;
      desiredMap[2] = 2;
    }

    bool mappingChanged = false;
    for (uint8_t sn = 0; sn < 3; ++sn) {
      if (ayToSn_[sn] != desiredMap[sn]) mappingChanged = true;
    }
    if (mappingChanged) {
      // 割り当て変更時の一瞬だけ、旧chの音程で新しい音量が鳴ることを防ぐ。
      for (uint8_t sn = 0; sn < 3; ++sn) writeVolume(sn, 15, sink);
      for (uint8_t sn = 0; sn < 3; ++sn) ayToSn_[sn] = desiredMap[sn];
    }

    for (uint8_t sn = 0; sn < 3; ++sn) {
      if (ayToSn_[sn] != UNMAPPED) {
        updateTone(sn, ayToSn_[sn], sink);
      } else if (!(variableNoise && sn == 2)) {
        writeVolume(sn, 15, sink);
      }
    }

    if (hasNoise) {
      if (variableNoise) {
        writePeriod(2, convertedPeriod(regs_[6] & 31), sink);
        writeNoiseControl(0xe7, sink);  // white noise, tone 3 clock
      } else {
        writeNoiseControl(uint8_t(0xe4 | fixedNoiseMode()), sink);
      }
      updateNoiseVolume(sink);
    } else {
      writeVolume(3, 15, sink);
    }
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

  uint32_t source_ = 0;
  uint32_t target_ = 0;
  uint8_t toneDivider_ = 32;
  uint8_t envelopeSteps_ = 32;
  uint8_t multiplier_ = 1;
  uint64_t sample_ = 0;
  uint64_t envelopePhase_ = 0;
  uint8_t envelopeState_ = 0;
  bool envelopeHeld_ = false;
  bool mixerWritten_ = false;
  uint8_t noiseControl_ = 0xff;
  uint8_t regs_[16] = {};
  uint8_t ayToSn_[3] = {0, 1, 2};
  uint16_t periods_[3] = {};
  uint8_t volumes_[4] = {};
};
