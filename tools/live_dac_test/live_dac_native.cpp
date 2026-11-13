#include "live_dac.h"
#include "ndsif.h"
#include "live_ordered.h"

static LiveDac live;
static LiveOrdered ordered;
static ndsif::Protocol protocol;
static ndsif::Capability caps[] = {{0x52, 0, 0, 5, ndsif::CLOCK_VARIABLE, 0}};
static size_t extension(uint8_t type, uint8_t command, const uint8_t* data, size_t size, uint8_t* out) {
  if (command == 0x1e && type == 1 && size == 2)
    return LiveDac::capabilities(uint16_t(data[0]) | (uint16_t(data[1]) << 8), out);
  if (command == 0x1f && type == 0) { live.accept(data, size); return 0; }
  if (command == 0x21 && type == 1 && size == 0) return LiveOrdered::capabilities(out);
  if (command == 0x22 && type == 0) { ordered.append(data, size); return 0; }
  if (command == 0x23 && type == 1 && size == 0) return ordered.status(out);
  if (command == 0x20 && type == 1 && size == 4 && data[0] == 'N' && data[1] == '6' && data[2] == 'D' && data[3] == '3')
    return ordered.diagnostics(out);
  if (command == 0x20 && type == 1 && size == 4 && data[0] == 'N' && data[1] == '6' && data[2] == 'D' && data[3] == '1')
    return live.diagnostics(out);
  return SIZE_MAX;
}

extern "C" {
__declspec(dllexport) void ordered_reset() { ordered.reset(); }
__declspec(dllexport) int ordered_append(const uint8_t* data, unsigned size) { return ordered.append(data, size); }
__declspec(dllexport) unsigned ordered_diagnostics(uint8_t* out) { return unsigned(ordered.diagnostics(out)); }
__declspec(dllexport) unsigned ordered_status(uint8_t* out) { return unsigned(ordered.status(out)); }
__declspec(dllexport) int ordered_next(uint64_t now, uint8_t* out) {
  LiveOrdered::Record r;
  if (!ordered.next(now, r)) return 0;
  out[0] = r.page; out[1] = r.reg; out[2] = r.value;
  return 1;
}
__declspec(dllexport) void live_reset() { live.reset(); }
__declspec(dllexport) int live_push(const uint8_t* data, unsigned size) { return live.push(data, size); }
__declspec(dllexport) int live_next(uint64_t now, uint8_t* value) { return live.next(now, *value); }
__declspec(dllexport) void live_written(uint64_t now) { live.didWrite(now, live.generation()); }
__declspec(dllexport) unsigned live_generation() { return live.generation(); }
__declspec(dllexport) void live_written_generation(uint64_t now, unsigned generation) { live.didWrite(now, generation); }
__declspec(dllexport) int live_enabled() { return live.enabled(); }
__declspec(dllexport) unsigned live_diagnostics(uint8_t* out) { return unsigned(live.diagnostics(out)); }
__declspec(dllexport) void protocol_init(unsigned features) {
  protocol.configure(nullptr, nullptr, "test");
  protocol.setCapabilities(caps, 1);
  protocol.setExtension(extension);
  protocol.setOptionalControlFeatures(uint16_t(features));
}
__declspec(dllexport) void protocol_feed(const uint8_t* bytes, unsigned size) {
  for (unsigned i = 0; i < size; ++i) protocol.feed(bytes[i], 1);
}
__declspec(dllexport) unsigned protocol_output(uint8_t* out) {
  const unsigned size = unsigned(protocol.outputSize());
  for (unsigned i = 0; i < size; ++i) out[i] = protocol.output()[i];
  protocol.consumeOutput(size); return size;
}
}
