#pragma once
#include <stddef.h>
#include <stdint.h>

// Experimental NDSIF v1 ordered live YM2612 profile. All methods are called
// under the chip-owner lock; physical I/O happens outside the lock.
class LiveOrdered {
 public:
  static constexpr uint8_t PROFILE = 3, MAX_RECORDS = 100;
  static constexpr uint16_t CAPACITY = 512;
  static constexpr uint32_t RATE = 44100;
  struct Record { uint32_t wait; uint8_t page, reg, value; };
  enum Result : uint8_t { Accepted = 0, Full = 1, Invalid = 2, Sequence = 3 };

  void reset() {
    head_ = count_ = 0; sequence_ = missingFrames_ = 0; active_ = previous_ = scheduled_ = empty_ = false;
    deadline_ = 0; remainder_ = 0;
    highWater_ = 0; accepted_ = executed_ = full_ = late_ = rebased_ = maxLate_ = 0;
    sourceTicks_ = firstUS_ = lastUS_ = 0;
  }
  bool enabled() const { return active_; }
  bool pending() const { return count_ != 0; }
  uint16_t free() const { return CAPACITY - count_; }
  // Temporary ND6 diagnostics. Counts describe scheduler activity, not a
  // measurement at the YM2612 pins; all methods share the owner lock.
  size_t diagnostics(uint8_t* out) const {
    out[0] = 3;
    out[1] = uint8_t(count_); out[2] = uint8_t(count_ >> 8);
    out[3] = uint8_t(highWater_); out[4] = uint8_t(highWater_ >> 8);
    const uint32_t small[] = {accepted_, executed_, full_, late_, rebased_, maxLate_};
    for (unsigned i = 0; i < 6; ++i)
      for (unsigned j = 0; j < 4; ++j) out[5 + 4*i + j] = uint8_t(small[i] >> (8*j));
    const uint64_t large[] = {firstUS_, lastUS_, sourceTicks_};
    for (unsigned i = 0; i < 3; ++i)
      for (unsigned j = 0; j < 8; ++j) out[29 + 8*i + j] = uint8_t(large[i] >> (8*j));
    return 53;
  }
  static size_t capabilities(uint8_t* out) {
    out[0] = PROFILE; out[1] = 5;
    for (unsigned i = 0; i < 4; ++i) out[2 + i] = uint8_t(RATE >> (8 * i));
    out[6] = uint8_t(CAPACITY); out[7] = uint8_t(CAPACITY >> 8);
    out[8] = MAX_RECORDS;
    return 9;
  }
  // Snapshot is a barrier after preceding bursts in the serial stream.
  size_t status(uint8_t* out) const {
    out[0] = PROFILE;
    for (unsigned i = 0; i < 4; ++i) out[1 + i] = uint8_t(sequence_ >> (8*i));
    for (unsigned i = 0; i < 4; ++i) out[5 + i] = uint8_t(accepted_ >> (8*i));
    out[9] = uint8_t(count_); out[10] = uint8_t(count_ >> 8);
    const uint16_t available = free();
    out[11] = uint8_t(available); out[12] = uint8_t(available >> 8);
    for (unsigned i = 0; i < 4; ++i) out[13 + i] = uint8_t(missingFrames_ >> (8*i));
    return 17;
  }
  Result append(const uint8_t* data, size_t size) {
    if (size < 7 || data[0] != PROFILE) return Invalid;
    const uint32_t seq = uint32_t(data[1]) | (uint32_t(data[2]) << 8) |
                         (uint32_t(data[3]) << 16) | (uint32_t(data[4]) << 24);
    const uint32_t gap = seq - sequence_;
    if (gap >= 0x80000000u) return Sequence; // Reject stale/duplicate frames.
    size_t pos = 5, n = 0;
    Record decoded;
    while (pos < size) {
      if (!decode(data, size, pos, decoded) || ++n > MAX_RECORDS) return Invalid;
    }
    if (!n) return Invalid;
    if (n > free()) { ++full_; return Full; }
    pos = 5;
    while (pos < size) {
      decode(data, size, pos, decoded); // Already validated above.
      records_[(head_ + count_++) % CAPACITY] = decoded;
    }
    accepted_ += uint32_t(n);
    if (count_ > highWater_) highWater_ = count_;
    missingFrames_ += gap;
    sequence_ = seq + 1; active_ = true;
    return Accepted;
  }
  bool next(uint64_t now, Record& record) {
    if (!active_ || !count_) return false;
    if (!scheduled_) {
      if (previous_) {
        const uint64_t scaled = uint64_t(records_[head_].wait) * 1000000 + remainder_;
        const uint64_t delta = scaled / RATE;
        remainder_ = uint32_t(scaled % RATE);
        deadline_ += delta;
        // A genuinely empty live stream must not replay accumulated silence.
        if (empty_ && deadline_ < now) {
          if (now - deadline_ > 500) ++rebased_;
          deadline_ = now;
        }
      } else deadline_ = now;
      scheduled_ = true;
      empty_ = false;
    }
    if (now < deadline_) return false;
    const uint64_t late = now - deadline_;
    if (late > 100) ++late_;
    if (late > maxLate_) maxLate_ = late > UINT32_MAX ? UINT32_MAX : uint32_t(late);
    record = records_[head_];
    sourceTicks_ += record.wait;
    if (!executed_) firstUS_ = now;
    lastUS_ = now; ++executed_;
    head_ = uint16_t((head_ + 1) % CAPACITY); --count_;
    scheduled_ = false; previous_ = true; empty_ = count_ == 0;
    return true;
  }
  uint64_t remaining(uint64_t now) const { return scheduled_ && deadline_ > now ? deadline_ - now : 0; }
 private:
  static bool decode(const uint8_t* data, size_t size, size_t& pos, Record& r) {
    const uint8_t tag = data[pos++];
    if (tag <= 0xfd) {
      if (size - pos < 1) return false;
      r.wait = tag; r.page = 0; r.reg = 0x2a; r.value = data[pos++];
      return true;
    }
    const size_t required = tag == 0xfe ? 5 : 7;
    if (size - pos < required) return false;
    r.wait = uint32_t(data[pos]) | (uint32_t(data[pos+1]) << 8) |
             (uint32_t(data[pos+2]) << 16) | (uint32_t(data[pos+3]) << 24);
    pos += 4;
    if (tag == 0xfe) { r.page = 0; r.reg = 0x2a; r.value = data[pos++]; }
    else { r.page = data[pos++]; r.reg = data[pos++]; r.value = data[pos++]; }
    return r.page <= 1 || (r.page == 2 && r.reg == 0);
  }
  Record records_[CAPACITY];
  uint16_t head_ = 0, count_ = 0;
  uint32_t sequence_ = 0, remainder_ = 0;
  uint32_t missingFrames_ = 0;
  uint64_t deadline_ = 0;
  bool active_ = false, previous_ = false, scheduled_ = false, empty_ = false;
  uint16_t highWater_ = 0;
  uint32_t accepted_ = 0, executed_ = 0, full_ = 0, late_ = 0, rebased_ = 0, maxLate_ = 0;
  uint64_t firstUS_ = 0, lastUS_ = 0, sourceTicks_ = 0;
};
