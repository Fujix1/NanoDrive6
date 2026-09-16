#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Portable wire decoder and 44,100 Hz scheduler. All methods have one owner.
namespace nd6timed {
inline uint64_t readLE(const uint8_t* p, unsigned n) {
  uint64_t v = 0;
  for (unsigned i = 0; i < n; ++i) v |= uint64_t(p[i]) << (8 * i);
  return v;
}
inline void writeLE(uint8_t* p, uint64_t v, unsigned n) {
  for (unsigned i = 0; i < n; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline uint16_t crcByte(uint16_t crc, uint8_t b) {
  crc ^= uint16_t(b) << 8;
  for (int bit = 0; bit < 8; ++bit)
    crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
  return crc;
}
inline uint16_t crc16(const uint8_t* p, size_t n) {
  uint16_t crc = 0xffff;
  while (n--) crc = crcByte(crc, *p++);
  return crc;
}
inline unsigned commandSize(uint8_t c) {
  if (c == 0x30 || c == 0x50 || (c >= 0x80 && c <= 0x8f)) return 2;
  if (c == 0x52 || c == 0x53 || c == 0x55 || c == 0x61) return 3;
  if (c == 0x62 || c == 0x63 || c == 0x66 || (c >= 0x70 && c <= 0x7f)) return 1;
  return 0;
}
inline uint32_t waitSamples(const uint8_t* p) {
  if (p[0] == 0x61) return uint32_t(readLE(p + 1, 2));
  if (p[0] == 0x62) return 735;
  if (p[0] == 0x63) return 882;
  if (p[0] >= 0x70 && p[0] <= 0x7f) return (p[0] & 15) + 1;
  if (p[0] >= 0x80 && p[0] <= 0x8f) return p[0] & 15;
  return 0;
}
class Stream {
 public:
  enum State : uint8_t { Idle, Preload, Running, Error = 3, Done = 4 };
  enum Fault : uint8_t { None, BadFrame, BadCommand, Overflow, Underrun, HostTimeout };
  static constexpr unsigned Capacity = 16384;
  static constexpr unsigned MaxPayload = 512;
  using Emit = void (*)(void*, const uint8_t*, unsigned);
  using Reset = void (*)(void*);
  Stream(Emit emit, Reset reset, void* context)
      : emit_(emit), reset_(reset), context_(context) {}

  State state = Idle;
  Fault error = None;
  uint32_t session = 0;
  uint64_t consumed = 0;
  uint64_t samples = 0;
  uint64_t maxPCMLateUS = 0;
  bool capabilityPending = false;
  bool statusPending = false;

  static uint64_t sampleUS(uint64_t n) {
    return (n / 44100) * 1000000 + (n % 44100) * 1000000 / 44100;
  }
  bool active() const { return state == Preload || state == Running; }
  uint64_t played(uint64_t now) const {
    if (state == Preload || state == Idle) return 0;
    if (state != Running) return samples;
    const uint64_t elapsed = now >= origin_ ? now - origin_ : 0;
    const uint64_t pos = (elapsed / 1000000) * 44100 + (elapsed % 1000000) * 44100 / 1000000;
    return pos < samples ? pos : samples;
  }
  uint64_t remainingUS(uint64_t now) const {
    return state == Running && deadline_ > now ? deadline_ - now : 0;
  }
  void stop() {
    state = Idle; error = None; count_ = head_ = 0;
    reset_(context_); statusPending = true;
  }
  void fail(Fault why) {
    state = Error; error = why; count_ = head_ = 0;
    reset_(context_); statusPending = true;
  }

  // Returns false only for a legacy byte outside a frame.
  bool input(uint8_t b, uint64_t now) {
    if (frameSize_ && now - frameAt_ > 500000) {
      frameSize_ = 0;
      if (active()) fail(BadFrame);
    }
    if (!frameSize_) {
      if (b == 0xf8) { capabilityPending = true; return true; }
      if (b != 0xf9) return false;
    }
    frameAt_ = now;
    frame_[frameSize_++] = b;
    if (frameSize_ <= 4) frameCRC_ = 0xffff;
    else if (frameSize_ <= 11 || frameSize_ <= 11 + readLE(frame_ + 9, 2))
      frameCRC_ = crcByte(frameCRC_, b);
    static const uint8_t magic[] = {0xf9, 'N', 'D', '6'};
    if (frameSize_ <= 4 && b != magic[frameSize_ - 1]) {
      frameSize_ = b == 0xf9 ? 1 : 0;
      if (active()) fail(BadFrame);
      return true;
    }
    if (frameSize_ < 11) return true;
    const unsigned length = unsigned(readLE(frame_ + 9, 2));
    if (length > MaxPayload) {
      frameSize_ = 0; fail(BadFrame); return true;
    }
    if (frameSize_ < 13 + length) return true;
    frameSize_ = 0;
    if (frameCRC_ != readLE(frame_ + 11 + length, 2)) {
      fail(BadFrame); return true;
    }
    const uint32_t id = uint32_t(readLE(frame_ + 5, 4));
    const uint8_t type = frame_[4];
    const uint8_t* payload = frame_ + 11;
    if (type == 1 && length == 0) {
      stop(); session = id; consumed = samples = maxPCMLateUS = 0;
      state = Preload; lastFrameAt_ = now; ended_ = false;
      return true;
    }
    if (id != session) return true;
    lastFrameAt_ = now;
    if (type == 4 && length == 0) { stop(); return true; }
    if (type == 3 && length == 0 && state == Preload) {
      origin_ = now; deadline_ = origin_ + sampleUS(samples);
      state = Running; statusPending = true; return true;
    }
    if (type == 5 && length == 0) { statusPending = true; return true; }
    if (type != 2 || !active() || !length || ended_) { fail(BadCommand); return true; }
    // Validate the complete payload before committing any bytes.
    bool end = false;
    for (unsigned i = 0; i < length;) {
      const unsigned size = commandSize(payload[i]);
      if (!size || i + size > length || (payload[i] == 0x66 && i + size != length)) {
        fail(BadCommand); return true;
      }
      if (payload[i] == 0x66) end = true;
      i += size;
    }
    if (length > Capacity - count_) { fail(Overflow); return true; }
    const unsigned tail = (head_ + count_) % Capacity;
    const unsigned first = length < Capacity - tail ? length : Capacity - tail;
    memcpy(queue_ + tail, payload, first);
    memcpy(queue_, payload + first, length - first);
    count_ += length;
    ended_ = end;
    return true;
  }

  void tick(uint64_t now) {
    if (!active()) return;
    if (now - lastFrameAt_ > 3000000) { fail(HostTimeout); return; }
    // Bound zero-time work so incoming stop/control frames are serviced promptly.
    for (unsigned work = 0; work < 16; ++work) {
      if (state == Running && remainingUS(now)) return;
      if (!count_) {
        if (state == Running) fail(Underrun);
        return;
      }
      uint8_t cmd[3] = {queue_[head_], 0, 0};
      const unsigned size = commandSize(cmd[0]);
      for (unsigned i = 1; i < size; ++i) cmd[i] = queue_[(head_ + i) % Capacity];
      const uint32_t wait = waitSamples(cmd);
      // Initialization is untimed; park before the first positive wait or end.
      if (state == Preload && (wait || cmd[0] == 0x66)) return;
      head_ = (head_ + size) % Capacity; count_ -= size; consumed += size;
      if (cmd[0] == 0x66) {
        state = Done; statusPending = true; reset_(context_); return;
      }
      if (state == Running && cmd[0] >= 0x80 && cmd[0] <= 0x8f && now > deadline_) {
        const uint64_t late = now - deadline_;
        if (late > maxPCMLateUS) maxPCMLateUS = late;
      }
      if (cmd[0] == 0x30 || cmd[0] == 0x50 || cmd[0] == 0x52 ||
          cmd[0] == 0x53 || cmd[0] == 0x55 || (cmd[0] >= 0x80 && cmd[0] <= 0x8f))
        emit_(context_, cmd, size);
      samples += wait;
      if (wait) deadline_ = origin_ + sampleUS(samples);
    }
  }

  void status(uint8_t* out, bool capability, uint64_t now) const {
    memcpy(out, "@TS1", 4); out[4] = capability ? 'C' : 'S';
    writeLE(out + 5, capability ? 0 : session, 4);
    writeLE(out + 9, consumed, 8); writeLE(out + 17, played(now), 8);
    out[25] = state; out[26] = error; writeLE(out + 27, Capacity, 2);
    // Capability bit 0 enables the reserved status byte as maximum DAC
    // lateness in 10 us steps; old v1 receivers simply ignore this byte.
    out[29] = capability ? 1 :
      uint8_t(maxPCMLateUS / 10 > 255 ? 255 : maxPCMLateUS / 10);
    writeLE(out + 30, crc16(out + 4, 26), 2);
  }

 private:
  Emit emit_; Reset reset_; void* context_;
  uint8_t queue_[Capacity] = {};
  unsigned head_ = 0, count_ = 0;
  uint8_t frame_[MaxPayload + 13] = {};
  unsigned frameSize_ = 0;
  uint16_t frameCRC_ = 0xffff;
  uint64_t frameAt_ = 0, lastFrameAt_ = 0, origin_ = 0, deadline_ = 0;
  bool ended_ = false;
};
}  // namespace nd6timed
