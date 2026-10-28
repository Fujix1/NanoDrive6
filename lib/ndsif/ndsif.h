#ifndef NDSIF_H
#define NDSIF_H

#include <stddef.h>
#include <stdint.h>

namespace ndsif {
// API identity and monotonically increasing specification revision.
// Separate from the frame wire-version byte and the ND8 firmware version.
constexpr char VERSION[] = "1.0";
constexpr uint32_t REVISION = 40; // ND6 experimental DAC profile; see NDSIF_ND6.md.

// Logical input-clock capabilities, independent of the oscillator/clock IC.
enum ClockMode : uint8_t { CLOCK_UNKNOWN = 0, CLOCK_FIXED = 1, CLOCK_VARIABLE = 2 };
struct Capability {
  uint8_t writeID, instance, page, features;
  ClockMode clockMode;
  uint32_t fixedClockHz; // Nonzero only for CLOCK_FIXED; logical chip input Hz.
};

// Formal-v1 draft framing. Device-specific routes and identity are board-owned.
class Protocol {
 public:
  static constexpr size_t MAX_PAYLOAD = 256;
  static constexpr size_t MAX_RAW = 9 + MAX_PAYLOAD + 2;
  static constexpr size_t MAX_ENCODED = MAX_RAW + MAX_RAW / 254 + 1;
  static constexpr uint32_t FRAME_TIMEOUT_MS = 500;
  using Reset = void (*)();
  using WriteYM = void (*)(uint8_t, uint8_t);
  // The board validates the entire burst before touching any hardware.
  using WriteRegisters = void (*)(uint8_t, uint8_t, uint8_t, const uint8_t*, size_t);
  using Audio = size_t (*)(uint8_t, const uint8_t*, size_t, uint8_t*);
  using SetClock = bool (*)(uint8_t, uint8_t, uint32_t);
  using SetVolume = void (*)(uint8_t);
  // SIZE_MAX rejects a control; otherwise return response bytes (excluding status).
  using Extension = size_t (*)(uint8_t, uint8_t, const uint8_t*, size_t, uint8_t*);

  void configure(Reset reset, WriteYM write, const char* firmware, SetClock clock = nullptr,
                 Audio audio = nullptr, SetVolume volume = nullptr);
  void setExtension(Extension extension) { extension_ = extension; }
  void setRegisterWriter(WriteRegisters write) { registerWrite_ = write; }
  void setDeviceInfo(const char* model, uint32_t dataCapacity = 0, uint16_t eventCapacity = 0);
  // Board-owned stable table; must remain alive until replaced/configured.
  bool setCapabilities(const Capability* entries, size_t count);
  // After transport data loss, ignore bytes until a delimiter arrives.
  void clear(bool resync = false);
  void expire(uint32_t now);
  // Caller must drain the pending response before feeding another byte.
  void feed(uint8_t byte, uint32_t now);
  const uint8_t* output() const { return tx_ + txOffset_; }
  size_t outputSize() const { return txSize_ - txOffset_; }
  void consumeOutput(size_t count);

 private:
  void dispatch(size_t size);
  void reply(uint8_t opcode, uint16_t request, size_t payloadSize);
  static uint16_t crc(const uint8_t* data, size_t size);
  const Capability* capabilities_ = nullptr;
  uint16_t capabilityCount_ = 0;
  Reset reset_ = nullptr;
  WriteYM write_ = nullptr;
  WriteRegisters registerWrite_ = nullptr;
  Extension extension_ = nullptr;
  const char* model_ = "";
  uint32_t dataCapacity_ = 4096;
  uint16_t eventCapacity_ = 1024;
  SetClock clock_ = nullptr;
  Audio audio_ = nullptr;
  SetVolume volume_ = nullptr;
  const char* firmware_ = "";
  uint8_t encoded_[MAX_ENCODED];
  uint8_t raw_[MAX_RAW];
  uint8_t tx_[MAX_ENCODED + 2];  // Leading and trailing delimiters.
  size_t rxSize_ = 0;
  size_t txSize_ = 0;
  size_t txOffset_ = 0;
  uint32_t lastByteMs_ = 0;
  bool discard_ = false;
};
}  // namespace ndsif
#endif
