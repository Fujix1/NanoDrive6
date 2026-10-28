#include "ndsif.h"

namespace ndsif {
void Protocol::configure(Reset reset, WriteYM write, const char* firmware, SetClock clock,
                         Audio audio, SetVolume volume) {
  reset_ = reset;
  write_ = write;
  registerWrite_ = nullptr;
  extension_ = nullptr;
  model_ = "";
  dataCapacity_ = 4096;
  eventCapacity_ = 1024;
  clock_ = clock;
  audio_ = audio;
  volume_ = volume;
  firmware_ = firmware;
  capabilities_ = nullptr; capabilityCount_ = 0;
  clear();
}

void Protocol::setDeviceInfo(const char* model, uint32_t dataCapacity, uint16_t eventCapacity) {
  model_ = model ? model : "";
  dataCapacity_ = dataCapacity;
  eventCapacity_ = eventCapacity;
}

bool Protocol::setCapabilities(const Capability* entries, size_t count) {
  if (count > 65535 || (count != 0 && entries == nullptr)) return false;
  for (size_t i = 0; i < count; ++i) {
    const Capability& e = entries[i];
    if (e.clockMode > CLOCK_VARIABLE ||
        (e.clockMode == CLOCK_FIXED && (e.fixedClockHz == 0 || (e.features & 4))) ||
        (e.clockMode != CLOCK_FIXED && e.fixedClockHz != 0) ||
        (e.clockMode == CLOCK_VARIABLE && !(e.features & 4))) return false;
    for (size_t j = 0; j < i; ++j) {
      const Capability& previous = entries[j];
      if (e.writeID == previous.writeID && e.instance == previous.instance &&
          (e.page == previous.page || e.clockMode != previous.clockMode ||
           e.fixedClockHz != previous.fixedClockHz)) return false;
    }
  }
  capabilities_ = entries; capabilityCount_ = static_cast<uint16_t>(count);
  return true;
}

void Protocol::clear(bool resync) {
  rxSize_ = txSize_ = txOffset_ = 0;
  lastByteMs_ = 0;
  discard_ = resync;
}

void Protocol::expire(uint32_t now) {
  if (rxSize_ != 0 && static_cast<uint32_t>(now - lastByteMs_) >= FRAME_TIMEOUT_MS) {
    rxSize_ = 0;
    discard_ = true;
  }
}

uint16_t Protocol::crc(const uint8_t* data, size_t size) {
  uint16_t value = 0xffff;
  for (size_t i = 0; i < size; ++i) {
    value ^= static_cast<uint16_t>(data[i]) << 8;
    for (unsigned bit = 0; bit < 8; ++bit)
      value = (value & 0x8000) ? (value << 1) ^ 0x1021 : value << 1;
  }
  return value;
}

void Protocol::feed(uint8_t byte, uint32_t now) {
  if (outputSize() != 0) return;
  expire(now);
  lastByteMs_ = now;
  if (byte != 0) {
    if (discard_) return;
    if (rxSize_ == MAX_ENCODED) {
      rxSize_ = 0;
      discard_ = true;
    } else {
      encoded_[rxSize_++] = byte;
    }
    return;
  }
  if (discard_) {
    discard_ = false;
    rxSize_ = 0;
    return;
  }
  const size_t size = rxSize_;
  rxSize_ = 0;
  if (size == 0) return;
  size_t src = 0, dst = 0;
  while (src < size) {
    const uint8_t code = encoded_[src++];
    const size_t count = code - 1;
    if (count > size - src || count > MAX_RAW - dst) return;
    for (size_t i = 0; i < count; ++i) raw_[dst++] = encoded_[src++];
    if (code != 0xff && src < size) {
      if (dst == MAX_RAW) return;
      raw_[dst++] = 0;
    }
  }
  dispatch(dst);
}

void Protocol::dispatch(size_t size) {
  if (size < 11 || raw_[0] != 'N' || raw_[1] != 'D' || raw_[2] != 1) return;
  const size_t length = raw_[7] | (static_cast<size_t>(raw_[8]) << 8);
  if (length > MAX_PAYLOAD || size != 11 + length) return;
  const uint16_t checksum = raw_[size - 2] | (static_cast<uint16_t>(raw_[size - 1]) << 8);
  if (crc(raw_, size - 2) != checksum) return;
  const uint8_t type = raw_[3], opcode = raw_[4];
  if (type > 1) return;  // Replies and undefined message types are never requests.
  const uint16_t request = raw_[5] | (static_cast<uint16_t>(raw_[6]) << 8);
  uint8_t* payload = raw_ + 9;
  // All sound IDs are one-way, including unsupported/reserved IDs and bad arguments.
  if (opcode >= 0x30) {
    const bool bytes = opcode == 0x50 || opcode == 0x30;
    if (type == 0 && ((bytes && length >= 3) || (!bytes && length >= 4 && (length & 1) == 0))) {
      if (registerWrite_) {
        // Route only advertised targets. The board also validates addresses/aliases.
        for (size_t i = 0; i < capabilityCount_; ++i) {
          const Capability& e = capabilities_[i];
          if (e.writeID == opcode && e.instance == payload[0] && e.page == payload[1] &&
              (e.features & 1)) {
            registerWrite_(opcode, payload[0], payload[1], payload + 2, bytes ? length - 2 : (length - 2) / 2);
            break;
          }
        }
      } else if (opcode == 0x54 && write_ && payload[0] == 0 && payload[1] == 0) {
        for (size_t i = 2; i < length; i += 2) write_(payload[i], payload[i + 1]);
      }
    }
    return;
  }
  size_t responseLength = 1;
  uint8_t status = 1;
  switch (opcode) {
    case 0x00:
      if (type == 1 && length == 0 && reset_) { reset_(); status = 0; }
      break;
    case 0x01:
      if (type == 1 && length <= 32) {
        for (size_t i = length; i != 0; --i) payload[i] = payload[i - 1];
        responseLength += length;
        status = 0;
      }
      break;
    case 0x02:
      if (type == 1 && length == 0) {
        size_t pos = 1, modelLength = 0;
        while (modelLength < 63 && model_[modelLength] != 0) ++modelLength;
        payload[pos++] = static_cast<uint8_t>(modelLength);
        for (size_t i = 0; i < modelLength; ++i) payload[pos++] = model_[i];
        const size_t lengthPos = pos++;
        size_t count = 0;
        while (count < 63 && firmware_[count] != 0) payload[pos++] = firmware_[count++];
        payload[lengthPos] = static_cast<uint8_t>(count);
        responseLength = pos;
        status = 0;
      }
      break;
    case 0x03:
      if (length == 1 && payload[0] <= 96 && volume_) {
        volume_(payload[0]); status = 0;
      }
      break;
    case 0x04:
      if (length == 6 && clock_) {
        const uint32_t hz = uint32_t(payload[2]) | (uint32_t(payload[3]) << 8) |
            (uint32_t(payload[4]) << 16) | (uint32_t(payload[5]) << 24);
        if (hz != 0 && clock_(payload[0], payload[1], hz)) status = 0;
      }
      break;
    case 0x05:
      if (type == 1 && length == 2) {
        const unsigned offset = unsigned(payload[0]) | (unsigned(payload[1]) << 8);
        if (offset <= capabilityCount_) {
          constexpr size_t HeaderSize = 14, EntrySize = 9;
          const size_t remaining = capabilityCount_ - offset;
          const size_t limit = (MAX_PAYLOAD - HeaderSize) / EntrySize;
          const size_t count = remaining < limit ? remaining : limit;
          const uint16_t next = count == remaining ? 0xffff : uint16_t(offset + count);
          const uint8_t header[] = {0, 0, 1,
              uint8_t(dataCapacity_), uint8_t(dataCapacity_ >> 8),
              uint8_t(dataCapacity_ >> 16), uint8_t(dataCapacity_ >> 24),
              uint8_t(eventCapacity_), uint8_t(eventCapacity_ >> 8),
              uint8_t((volume_ ? 1 : 0) | (extension_ ? 2 : 0)), 0, uint8_t(next), uint8_t(next >> 8), uint8_t(count)};
          for (size_t i = 0; i < sizeof(header); ++i) payload[i] = header[i];
          size_t pos = HeaderSize;
          for (size_t i = 0; i < count; ++i) {
            const Capability& e = capabilities_[offset + i];
            payload[pos++] = e.writeID; payload[pos++] = e.instance;
            payload[pos++] = e.page; payload[pos++] = e.features;
            payload[pos++] = e.clockMode;
            for (unsigned shift = 0; shift < 32; shift += 8)
              payload[pos++] = uint8_t(e.fixedClockHz >> shift);
          }
          responseLength = pos;
          status = 0;
        }
      }
      break;
    case 0x10:
    case 0x11:
    case 0x12:
      if (type == 0 && audio_) audio_(opcode, payload, length, nullptr);
      break;
    case 0x13:
      if (type == 1 && length == 2 && payload[0] == 0xb7 && payload[1] == 0 && audio_) {
        const size_t count = audio_(opcode, payload, length, payload + 1);
        if (count <= MAX_PAYLOAD - 1) { responseLength += count; status = 0; }
      }
      break;
    default:
      if (extension_) {
        const size_t count = extension_(type, opcode, payload, length, payload + 1);
        if (count <= MAX_PAYLOAD - 1) { responseLength += count; status = 0; }
      }
      break;
  }
  if (type == 0) return;
  payload[0] = status;
  reply(opcode, request, responseLength);
}

void Protocol::reply(uint8_t opcode, uint16_t request, size_t payloadSize) {
  raw_[0] = 'N'; raw_[1] = 'D'; raw_[2] = 1; raw_[3] = 2; raw_[4] = opcode;
  raw_[5] = static_cast<uint8_t>(request); raw_[6] = request >> 8;
  raw_[7] = static_cast<uint8_t>(payloadSize); raw_[8] = payloadSize >> 8;
  const size_t size = 9 + payloadSize;
  const uint16_t checksum = crc(raw_, size);
  raw_[size] = static_cast<uint8_t>(checksum); raw_[size + 1] = checksum >> 8;
  txOffset_ = 0;
  tx_[0] = 0;  // Separate boot logs / stale partial replies from this frame.
  size_t dst = 2, codePos = 1;
  uint8_t code = 1;
  for (size_t i = 0; i < size + 2; ++i) {
    if (raw_[i] == 0) {
      tx_[codePos] = code;
      codePos = dst++;
      code = 1;
    } else {
      tx_[dst++] = raw_[i];
      if (++code == 0xff) {
        tx_[codePos] = code;
        codePos = dst++;
        code = 1;
      }
    }
  }
  tx_[codePos] = code;
  tx_[dst++] = 0;
  // HWCDC on ND6 stalls on an exact 64-byte reply (GET_CAPABILITIES with
  // four entries). An extra empty COBS delimiter ends it with a short USB
  // packet without changing the frame, CRC or payload. Empty delimiters are
  // already ignored by NDSIF receivers. The largest reply is 271 bytes.
  if (dst % 64 == 0 && dst < sizeof(tx_)) tx_[dst++] = 0;
  txSize_ = dst;
}

void Protocol::consumeOutput(size_t count) {
  if (count > outputSize()) count = outputSize();
  txOffset_ += count;
  if (txOffset_ == txSize_) txOffset_ = txSize_ = 0;
}
}  // namespace ndsif
