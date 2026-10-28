#include "dac_stream.h"
#include <string.h>

namespace ndsif {
bool DacStream::decode(const uint8_t* data, size_t size, Chunk& chunk) {
  if (size < 19 || size > 255 || (size - 15) % 4 ||
      data[0] != PROFILE || data[1] != 0x52 || data[2] != 0) return false;
  chunk.sequence = uint32_t(readLE(data + 3, 4));
  chunk.position = chunk.end = readLE(data + 7, 8);
  chunk.count = uint8_t((size - 15) / 4);
  if (chunk.count > MAX_RECORDS || chunk.sequence > UINT32_MAX - chunk.count) return false;
  for (unsigned i = 0; i < chunk.count; ++i) {
    const uint8_t* p = data + 15 + i * 4;
    Record& r = chunk.records[i];
    r.kind = p[0]; r.value = p[1]; r.wait = uint16_t(readLE(p + 2, 2));
    if (r.kind > Wait || (r.kind == Wait && (r.value != 0 || r.wait == 0)) ||
        (r.kind == Select && (r.value & 0x7f)) || chunk.end > UINT64_MAX - r.wait) return false;
    chunk.end += r.wait;
  }
  // Keep tick-to-us conversion and the timer's origin addition representable.
  return chunk.end <= UINT64_MAX / 1000000;
}

void DacStream::reset() {
  head_ = count_ = eventHead_ = eventCount_ = 0;
  acceptedSequence_ = consumedSequence_ = consumedEvents_ = 0;
  underflows_ = overflows_ = rejected_ = lateDac_ = maxLateUS_ = 0;
  acceptedTick_ = nextTick_ = executedTick_ = lastEventTick_ = 0;
  origin_ = deadline_ = 0;
  state_ = Ready; fault_ = None; sealed_ = stop_ = false;
}

void DacStream::fail(Fault fault) {
  if (state_ == Faulted) return;
  fault_ = fault; state_ = Faulted; stop_ = true;
  if (fault == Underflow) ++underflows_;
  else if (fault == Overflow) ++overflows_;
  else ++rejected_;
  // Retain queue/positions for diagnostics, but never execute them until RESET.
}

bool DacStream::append(const Chunk& chunk) {
  if (!writable() || sealed_) { fail(Invalid); return false; }
  if (chunk.sequence != acceptedSequence_ || chunk.position != acceptedTick_) {
    fail(Position); return false;
  }
  if (chunk.count > CAPACITY - count_) { fail(Overflow); return false; }
  const uint32_t tail = (head_ + count_) % CAPACITY;
  const uint32_t first = chunk.count < CAPACITY - tail ? chunk.count : CAPACITY - tail;
  memcpy(records_ + tail, chunk.records, first * sizeof(Record));
  memcpy(records_, chunk.records + first, (chunk.count - first) * sizeof(Record));
  count_ += chunk.count;
  acceptedSequence_ += chunk.count;
  acceptedTick_ = chunk.end;
  return true;
}

bool DacStream::events(uint64_t tick, uint8_t id, uint8_t instance, uint8_t page,
                       const uint8_t* data, size_t count) {
  // Validate the entire frame before reserving any event slots. 2A/2B belong
  // to the ordered DAC records, never the coarse FM event queue.
  const bool psg = (id == 0x50 && instance == 0) || (id == 0x30 && instance == 1);
  const bool ym = instance == 0 && ((id == 0x52 && page == 0) || (id == 0x53 && page == 1));
  if (!writable() || sealed_ || !count || count > 244 ||
      (!psg && !ym) || (psg && page != 0)) { fail(Invalid); return false; }
  if (ym) {
    for (size_t i = 0; i < count; ++i)
      if (page == 0 && (data[i * 2] == 0x2a || data[i * 2] == 0x2b)) {
        fail(Invalid); return false;
      }
  }
  if (tick < acceptedTick_ || tick < lastEventTick_ || tick > UINT64_MAX / 1000000) {
    fail(Position); return false;
  }
  if (count > EVENT_CAPACITY - eventCount_) { fail(Overflow); return false; }
  for (size_t i = 0; i < count; ++i) {
    Event& e = events_[(eventHead_ + eventCount_ + i) % EVENT_CAPACITY];
    e.tick = tick; e.id = id; e.instance = instance; e.page = page;
    e.address = psg ? 0 : data[i * 2]; e.value = data[psg ? i : i * 2 + 1];
  }
  eventCount_ += uint32_t(count); lastEventTick_ = tick;
  return true;
}

bool DacStream::seal(uint32_t sequence, uint64_t end) {
  if (!writable() || sealed_ || sequence != acceptedSequence_ || end != acceptedTick_ ||
      lastEventTick_ > end) { fail(Position); return false; }
  sealed_ = true;
  return true;
}

bool DacStream::start(uint32_t leadTicks, uint64_t now) {
  if (state_ != Ready || !count_ || !leadTicks ||
      (!sealed_ && acceptedTick_ < leadTicks)) return false;
  origin_ = deadline_ = now;
  state_ = Running;
  return true;
}

bool DacStream::popEvent(uint64_t tick, Action& action) {
  if (!eventCount_ || events_[eventHead_].tick > tick || events_[eventHead_].tick > acceptedTick_)
    return false;
  const Event& e = events_[eventHead_];
  action = {e.id == 0x50 || e.id == 0x30 ? Psg : Register, e.page, e.address, e.value, e.instance};
  eventHead_ = (eventHead_ + 1) % EVENT_CAPACITY;
  --eventCount_; ++consumedEvents_;
  return true;
}

bool DacStream::next(uint64_t now, Action& action) {
  if (!running()) return false;
  const uint64_t elapsed = now > origin_ ? now - origin_ : 0;
  const uint64_t tick = (elapsed / 1000000) * TICK_RATE + (elapsed % 1000000) * TICK_RATE / 1000000;
  if (now < deadline_) {
    // FM/PSG may run late; they never run early. Leave room before the next DAC.
    return deadline_ - now >= 20 && popEvent(tick, action);
  }
  if (!count_) {
    if (sealed_ && nextTick_ == acceptedTick_) {
      if (popEvent(acceptedTick_, action)) return true;
      state_ = Ended; stop_ = true;
    } else fail(Underflow);
    return false;
  }
  const Record r = records_[head_];
  if (r.kind == Dac && now > deadline_) {
    const uint64_t late = now - deadline_;
    ++lateDac_;
    if (late > maxLateUS_) maxLateUS_ = late > UINT32_MAX ? UINT32_MAX : uint32_t(late);
    if (late > MAX_LATE_US) { fail(Late); return false; }
  }
  action = {static_cast<Kind>(r.kind), 0, uint8_t(r.kind == Select ? 0x2b : 0x2a), r.value, 0};
  executedTick_ = nextTick_;
  nextTick_ += r.wait;
  deadline_ = origin_ + tickUS(nextTick_);
  head_ = (head_ + 1) % CAPACITY;
  --count_; ++consumedSequence_;
  return true;
}

size_t DacStream::status(uint8_t* out, uint64_t now) const {
  out[0] = PROFILE; out[1] = state_; out[2] = fault_; out[3] = sealed_ ? 1 : 0;
  const uint32_t values[] = {acceptedSequence_, consumedSequence_, count_, CAPACITY - count_,
      eventCount_, consumedEvents_, underflows_, overflows_, rejected_, lateDac_, maxLateUS_};
  for (unsigned i = 0; i < 11; ++i) writeLE(out + 4 + i * 4, values[i], 4);
  uint64_t played = executedTick_;
  if (running() && now >= origin_) {
    const uint64_t elapsed = now - origin_;
    played = (elapsed / 1000000) * TICK_RATE + (elapsed % 1000000) * TICK_RATE / 1000000;
    if (played > acceptedTick_) played = acceptedTick_;
  } else if (state_ == Ended) played = acceptedTick_;
  writeLE(out + 48, acceptedTick_, 8);
  writeLE(out + 56, played, 8);
  writeLE(out + 64, executedTick_, 8);
  return 72;
}

size_t DacStream::capabilities(uint8_t* out) {
  out[0] = PROFILE;
  writeLE(out + 1, 40, 4); writeLE(out + 5, TICK_RATE, 4);
  writeLE(out + 9, CAPACITY, 4); writeLE(out + 13, EVENT_CAPACITY, 2);
  out[15] = MAX_RECORDS; out[16] = sizeof(Record);
  writeLE(out + 17, MAX_LATE_US, 4);
  return 21;
}
} // namespace ndsif
