#include "ndsif.h"

static ndsif::Protocol protocol;
static unsigned count, first, last, audioCalls;
static ndsif::Capability caps[] = {
  {0x52, 0, 0, 5, ndsif::CLOCK_VARIABLE, 0},
  {0x53, 0, 1, 5, ndsif::CLOCK_VARIABLE, 0},
  {0x50, 0, 0, 5, ndsif::CLOCK_VARIABLE, 0},
  {0x30, 1, 0, 5, ndsif::CLOCK_VARIABLE, 0},
};
static void writeRegisters(uint8_t, uint8_t, uint8_t, const uint8_t* data, size_t n) {
  count = unsigned(n); first = data[0]; last = data[n - 1];
}
static void writeYM(uint8_t a, uint8_t v) { ++count; first = a; last = v; }
static size_t audio(uint8_t op, const uint8_t*, size_t, uint8_t* out) {
  ++audioCalls;
  if (op == 0x13) { out[0] = 0xa5; return 1; }
  return 0;
}
static size_t extension(uint8_t type, uint8_t op, const uint8_t*, size_t n, uint8_t* out) {
  if (type == 1 && op == 0x18 && n == 0) { out[0] = 1; return 1; }
  return SIZE_MAX;
}
extern "C" {
__declspec(dllexport) void init(unsigned device) {
  count = first = last = audioCalls = 0;
  protocol.configure(nullptr, device == 8 ? writeYM : nullptr, "test", nullptr,
                     device == 8 ? audio : nullptr, nullptr);
  protocol.setDeviceInfo(device == 8 ? "NanoDrive 8" : "NanoDrive 6", 4096, 1024);
  if (device == 6) { protocol.setRegisterWriter(writeRegisters); protocol.setExtension(extension); }
  protocol.setCapabilities(caps, 4);
}
__declspec(dllexport) void feed(const uint8_t* bytes, unsigned n) {
  for (unsigned i = 0; i < n; ++i) protocol.feed(bytes[i], 1);
}
__declspec(dllexport) unsigned output(uint8_t* out) {
  unsigned n = unsigned(protocol.outputSize());
  for (unsigned i = 0; i < n; ++i) out[i] = protocol.output()[i];
  protocol.consumeOutput(n); return n;
}
__declspec(dllexport) unsigned metric(unsigned id) {
  return id == 0 ? count : id == 1 ? first : id == 2 ? last : audioCalls;
}
}
