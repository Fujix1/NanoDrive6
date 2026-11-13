#pragma once
#include <stddef.h>
#include <stdint.h>

// NDSIF v1 draft profile 2. Caller serializes methods with the chip-owner lock.
// Empty and full queues are normal live conditions, not faults.
class LiveDac {
 public:
  static constexpr uint16_t CAPACITY = 512;
  static constexpr uint8_t PROFILE = 2;
  static constexpr uint8_t MAX_RECORDS = 50;
  static constexpr uint32_t TICKS_PER_SECOND = 44100;
  struct Record { uint32_t wait; uint8_t value; };

  void reset() {
    ++generation_;
    head_ = count_ = 0; enabled_ = scheduled_ = previous_ = false;
    deadline_ = lastWrite_ = 0; remainder_ = 0;
    empty_ = false; highWater_ = 0;
    accepted_ = emitted_ = droppedFrames_ = droppedRecords_ = 0;
    lateAfterEmpty_ = worstLateUS_ = 0;
  }
  bool enabled() const { return enabled_; }
  uint32_t generation() const { return generation_; }
  bool pending() const { return count_ != 0; }
  // Experimental ND6-only snapshot, called under the same lock as push/next.
  // Emitted counts dequeue attempts, not electrical or audible verification.
  size_t diagnostics(uint8_t* out) const {
    out[0] = 1;
    out[1] = uint8_t(count_); out[2] = uint8_t(count_ >> 8);
    out[3] = uint8_t(highWater_); out[4] = uint8_t(highWater_ >> 8);
    const uint32_t values[] = {accepted_, emitted_, droppedFrames_, droppedRecords_, lateAfterEmpty_, worstLateUS_};
    for (unsigned i = 0; i < 6; ++i)
      for (unsigned j = 0; j < 4; ++j) out[5 + i * 4 + j] = uint8_t(values[i] >> (j * 8));
    return 29;
  }
  static size_t capabilities(uint16_t offset, uint8_t* out) {
    if (offset > 1) return SIZE_MAX;
    out[0] = PROFILE; out[1] = 1;
    for (unsigned i = 0; i < 4; ++i) out[2 + i] = uint8_t(TICKS_PER_SECOND >> (8 * i));
    for (unsigned i = 0; i < 4; ++i) out[6 + i] = uint8_t(uint32_t(CAPACITY) >> (8 * i));
    out[10] = MAX_RECORDS; out[11] = 0xff; out[12] = 0xff;
    out[13] = offset == 0 ? 1 : 0;
    if (offset == 0) { out[14] = 0x52; out[15] = 0; return 16; }
    return 14;
  }
  bool accept(const uint8_t* data, size_t size) {
    return size >= 8 && data[0] == PROFILE && data[1] == 0x52 && data[2] == 0 && push(data + 3, size - 3);
  }
  bool push(const uint8_t* data, size_t size) {
    if (!size || size % 5 || size / 5 > MAX_RECORDS) return false;
    if (size / 5 > CAPACITY - count_) {
      ++droppedFrames_; droppedRecords_ += uint32_t(size / 5);
      return false;
    }
    for (size_t i = 0; i < size; i += 5) {
      Record& r = records_[(head_ + count_++) % CAPACITY];
      r.wait = uint32_t(data[i]) | (uint32_t(data[i + 1]) << 8) |
               (uint32_t(data[i + 2]) << 16) | (uint32_t(data[i + 3]) << 24);
      r.value = data[i + 4];
    }
    accepted_ += uint32_t(size / 5);
    if (count_ > highWater_) highWater_ = count_;
    enabled_ = true;
    return true;
  }
  bool next(uint64_t now, uint8_t& value) {
    if (!enabled_ || !count_) return false;
    if (!scheduled_) {
      if (previous_) {
        const uint64_t scaled = uint64_t(records_[head_].wait) * 1000000 + remainder_;
        const uint64_t candidate = lastWrite_ + scaled / TICKS_PER_SECOND;
        if (empty_ && now > candidate && now - candidate > 500) {
          ++lateAfterEmpty_;
          const uint64_t late = now - candidate;
          if (late > worstLateUS_) worstLateUS_ = late > UINT32_MAX ? UINT32_MAX : uint32_t(late);
        }
        remainder_ = uint32_t(scaled % TICKS_PER_SECOND);
        deadline_ = candidate > now ? candidate : now;
      } else {
        deadline_ = now; // Missing first frame: its predecessor is unknown.
      }
      empty_ = false;
      scheduled_ = true;
    }
    if (now < deadline_) return false;
    value = records_[head_].value;
    head_ = uint16_t((head_ + 1) % CAPACITY);
    --count_;
    ++emitted_;
    if (!count_) empty_ = true;
    scheduled_ = false;
    return true;
  }
  void didWrite(uint64_t now, uint32_t generation) {
    if (enabled_ && generation == generation_) { lastWrite_ = now; previous_ = true; }
  }
  uint64_t remaining(uint64_t now) const {
    return scheduled_ && deadline_ > now ? deadline_ - now : 0;
  }

 private:
  Record records_[CAPACITY];
  uint16_t head_ = 0, count_ = 0;
  bool enabled_ = false, scheduled_ = false, previous_ = false;
  uint64_t deadline_ = 0, lastWrite_ = 0;
  uint32_t remainder_ = 0;
  uint32_t generation_ = 0;
  bool empty_ = false;
  uint16_t highWater_ = 0;
  uint32_t accepted_ = 0, emitted_ = 0, droppedFrames_ = 0, droppedRecords_ = 0;
  uint32_t lateAfterEmpty_ = 0, worstLateUS_ = 0;
};
