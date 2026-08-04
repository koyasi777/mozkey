#include "zenz/zenz_wire_protocol.h"

#include <cstddef>
#include <cstdint>

#include "testing/gunit.h"

namespace mozc {
namespace zenz {
namespace {

TEST(ZenzWireProtocolTest, ConstantsRemainStable) {
  EXPECT_EQ(kZenzWireMagic, 0x315A4E5A);
  EXPECT_EQ(kZenzWireVersion, 1);
  EXPECT_EQ(kZenzWireKindRequest, 1);
  EXPECT_EQ(kZenzWireKindResponse, 2);
  EXPECT_EQ(kZenzWireStatusOk, 0);
  EXPECT_EQ(kZenzWireStatusError, 1);
  EXPECT_EQ(kZenzWireStatusTimeout, 2);
}

TEST(ZenzWireProtocolTest, RequestHeaderLayoutRemainsStable) {
  EXPECT_EQ(sizeof(ZenzWireRequestHeader), 24);
  EXPECT_EQ(offsetof(ZenzWireRequestHeader, magic), 0);
  EXPECT_EQ(offsetof(ZenzWireRequestHeader, version), 4);
  EXPECT_EQ(offsetof(ZenzWireRequestHeader, kind), 6);
  EXPECT_EQ(offsetof(ZenzWireRequestHeader, generation), 8);
  EXPECT_EQ(offsetof(ZenzWireRequestHeader, timeout_msec), 12);
  EXPECT_EQ(offsetof(ZenzWireRequestHeader, max_output_chars), 16);
  EXPECT_EQ(offsetof(ZenzWireRequestHeader, prompt_size), 20);
}

TEST(ZenzWireProtocolTest, ResponseHeaderLayoutRemainsStable) {
  EXPECT_EQ(sizeof(ZenzWireResponseHeader), 28);
  EXPECT_EQ(offsetof(ZenzWireResponseHeader, magic), 0);
  EXPECT_EQ(offsetof(ZenzWireResponseHeader, version), 4);
  EXPECT_EQ(offsetof(ZenzWireResponseHeader, kind), 6);
  EXPECT_EQ(offsetof(ZenzWireResponseHeader, generation), 8);
  EXPECT_EQ(offsetof(ZenzWireResponseHeader, status), 12);
  EXPECT_EQ(offsetof(ZenzWireResponseHeader, latency_msec), 16);
  EXPECT_EQ(offsetof(ZenzWireResponseHeader, value_size), 20);
  EXPECT_EQ(offsetof(ZenzWireResponseHeader, debug_size), 24);
}

}  // namespace
}  // namespace zenz
}  // namespace mozc
