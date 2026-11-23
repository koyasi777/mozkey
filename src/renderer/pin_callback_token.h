// Copyright 2026 Mozkey contributors.
#ifndef MOZC_RENDERER_PIN_CALLBACK_TOKEN_H_
#define MOZC_RENDERER_PIN_CALLBACK_TOKEN_H_

#include <cstdint>
#include <string_view>

namespace mozc::renderer {

// A snapshot identity for renderer -> TSF callbacks. Legacy callbacks only
// transport (command type, candidate ID) via WPARAM/LPARAM. Pin callbacks must
// also disambiguate the reading and candidate value without placing user text
// in the Windows message. Use the lower 56 bits of a deterministic FNV-1a
// fingerprint over type, ID, reading and value. This is a stale-state guard,
// NOT a cryptographic authentication token or a security boundary.
//
// Layout, on both 32- and 64-bit Windows:
//   WPARAM low 8 bits: command type (32 or 33)
//   WPARAM bits 8..31: fingerprint low 24 bits
//   LPARAM low 32 bits: fingerprint bits 24..55
// All other renderer callbacks retain their pre-existing type/ID layout.
struct PinCallbackMessage {
  uint32_t wparam = 0;
  uint32_t lparam = 0;
};

inline uint64_t ComputePinCallbackFingerprint(uint32_t command_type,
                                              int32_t candidate_id,
                                              std::string_view reading,
                                              std::string_view value) {
  uint64_t hash = 14695981039346656037ULL;
  const auto append_byte = [&hash](uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };
  const auto append_u32 = [&append_byte](uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8) {
      append_byte(static_cast<uint8_t>(v >> shift));
    }
  };
  const auto append_text = [&append_byte, &append_u32](std::string_view text) {
    append_u32(static_cast<uint32_t>(text.size()));
    for (unsigned char byte : text) {
      append_byte(byte);
    }
  };
  append_u32(command_type);
  append_u32(static_cast<uint32_t>(candidate_id));
  append_text(reading);
  append_text(value);
  return hash & 0x00FFFFFFFFFFFFFFULL;
}

inline PinCallbackMessage PackPinCallback(uint32_t command_type,
                                          int32_t candidate_id,
                                          std::string_view reading,
                                          std::string_view value) {
  const uint64_t fingerprint = ComputePinCallbackFingerprint(
      command_type, candidate_id, reading, value);
  return {
      static_cast<uint32_t>((fingerprint & 0xFFFFFFULL) << 8) |
          (command_type & 0xFFU),
      static_cast<uint32_t>(fingerprint >> 24),
  };
}

inline bool MatchesPinCallback(const PinCallbackMessage& encoded,
                               uint32_t command_type, int32_t candidate_id,
                               std::string_view reading,
                               std::string_view value) {
  if ((encoded.wparam & 0xFFU) != command_type) {
    return false;
  }
  const uint64_t transported_fingerprint =
      (static_cast<uint64_t>(encoded.lparam) << 24) |
      (static_cast<uint64_t>(encoded.wparam) >> 8);
  return transported_fingerprint == ComputePinCallbackFingerprint(
      command_type, candidate_id, reading, value);
}

}  // namespace mozc::renderer
#endif  // MOZC_RENDERER_PIN_CALLBACK_TOKEN_H_
