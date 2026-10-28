#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ndsif {
inline uint64_t readLE(const uint8_t* p, unsigned n) {
  uint64_t value = 0;
  for (unsigned i = 0; i < n; ++i) value |= uint64_t(p[i]) << (i * 8);
  return value;
}
inline void writeLE(uint8_t* p, uint64_t value, unsigned n) {
  for (unsigned i = 0; i < n; ++i) p[i] = uint8_t(value >> (i * 8));
}

// Portable scheduler. The board serializes public methods with a short critical
// section; chip I/O is performed AFTER releasing it. No allocation or UI work.
class DacStream {
 public:
  static constexpr uint8_t PROFILE = 1;
  static constexpr uint32_t TICK_RATE = 44100;
  static constexpr uint32_t CAPACITY = 8192; // 32 KiB of value/wait records.
  static constexpr uint16_t EVENT_CAPACITY = 512;
  static constexpr uint8_t MAX_RECORDS = 60;
  static constexpr uint32_t MAX_LATE_US = 2000;
  enum State : uint8_t { Ready, Running, Ended, Faulted };
  enum Fault : uint8_t { None, Underflow, Overflow, Position, Invalid, Transport, Late };
  enum Kind : uint8_t { Dac, Select, Wait, Register, Psg };
  struct Record { uint8_t kind, value; uint16_t wait; };
  struct Chunk {
    Record records[MAX_RECORDS];
    uint32_t sequence;
    uint64_t position, end;
    uint8_t count;
  };
  struct Action { Kind kind; uint8_t page, address, value, instance; };

  static bool decode(const uint8_t* data, size_t size, Chunk& chunk);
  static uint64_t tickUS(uint64_t tick) {
    return (tick / TICK_RATE) * 1000000 + (tick % TICK_RATE) * 1000000 / TICK_RATE;
  }
  void reset();
  bool append(const Chunk& chunk);
  bool events(uint64_t tick, uint8_t id, uint8_t instance, uint8_t page,
              const uint8_t* data, size_t count);
  bool seal(uint32_t sequence, uint64_t end);
  bool start(uint32_t leadTicks, uint64_t now);
  // Returns one action at most. Wait records also return true (no chip I/O).
  bool next(uint64_t now, Action& action);
  void fail(Fault fault);
  bool takeStop() { const bool result = stop_; stop_ = false; return result; }
  bool running() const { return state_ == Running; }
  bool pristine() const { return state_ == Ready && acceptedSequence_ == 0 && !sealed_; }
  bool writable() const { return state_ == Ready || state_ == Running; }
  uint64_t remainingUS(uint64_t now) const {
    return running() && deadline_ > now ? deadline_ - now : 0;
  }
  size_t status(uint8_t* out, uint64_t now) const;
  static size_t capabilities(uint8_t* out);

 private:
  struct Event { uint64_t tick; uint8_t id, instance, page, address, value; };
  bool popEvent(uint64_t tick, Action& action);
  Record records_[CAPACITY];
  Event events_[EVENT_CAPACITY];
  uint32_t head_ = 0, count_ = 0, eventHead_ = 0, eventCount_ = 0;
  uint32_t acceptedSequence_ = 0, consumedSequence_ = 0, consumedEvents_ = 0;
  uint32_t underflows_ = 0, overflows_ = 0, rejected_ = 0, lateDac_ = 0, maxLateUS_ = 0;
  uint64_t acceptedTick_ = 0, nextTick_ = 0, executedTick_ = 0, lastEventTick_ = 0;
  uint64_t origin_ = 0, deadline_ = 0;
  State state_ = Ready;
  Fault fault_ = None;
  bool sealed_ = false, stop_ = false;
};
static_assert(sizeof(DacStream::Record) == 4, "DAC record must be four bytes");
} // namespace ndsif
