#ifndef MOZC_ZENZ_ZENZ_WIRE_PROTOCOL_H_
#define MOZC_ZENZ_ZENZ_WIRE_PROTOCOL_H_

#include <cstdint>
#include <type_traits>

namespace mozc {
namespace zenz {

// Local scorer protocol shared by the Mozc client and mozc_zenz_scorer.
//
// The protocol is intentionally a packed, native-endian structure because both
// endpoints are local processes built for the same platform and architecture.
// Keep the constants and field layout stable unless both endpoints are updated
// together with a protocol version change.
inline constexpr uint32_t kZenzWireMagic = 0x315A4E5A;  // "ZNZ1"
inline constexpr uint16_t kZenzWireVersion = 1;
inline constexpr uint16_t kZenzWireKindRequest = 1;
inline constexpr uint16_t kZenzWireKindResponse = 2;

inline constexpr uint32_t kZenzWireStatusOk = 0;
inline constexpr uint32_t kZenzWireStatusError = 1;
inline constexpr uint32_t kZenzWireStatusTimeout = 2;

#pragma pack(push, 1)
struct ZenzWireRequestHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t kind;
  uint32_t generation;
  uint32_t timeout_msec;
  uint32_t max_output_chars;
  uint32_t prompt_size;
};

struct ZenzWireResponseHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t kind;
  uint32_t generation;
  uint32_t status;
  uint32_t latency_msec;
  uint32_t value_size;
  uint32_t debug_size;
};
#pragma pack(pop)

static_assert(sizeof(ZenzWireRequestHeader) == 24);
static_assert(sizeof(ZenzWireResponseHeader) == 28);
static_assert(std::is_trivially_copyable_v<ZenzWireRequestHeader>);
static_assert(std::is_trivially_copyable_v<ZenzWireResponseHeader>);
static_assert(std::is_standard_layout_v<ZenzWireRequestHeader>);
static_assert(std::is_standard_layout_v<ZenzWireResponseHeader>);

}  // namespace zenz
}  // namespace mozc

#endif  // MOZC_ZENZ_ZENZ_WIRE_PROTOCOL_H_
