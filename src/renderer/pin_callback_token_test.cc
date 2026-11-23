// Copyright 2026 Mozkey contributors.
#include "renderer/pin_callback_token.h"

#include <cstdint>

#include "testing/gunit.h"

namespace mozc::renderer {
namespace {

constexpr uint32_t kPin = 32;
constexpr uint32_t kUnpin = 33;

TEST(PinCallbackTokenTest, RoundTripAndTypeAndStaleCandidateRejection) {
  const PinCallbackMessage packed =
      PackPinCallback(kPin, 7, "こうしょう", "考証");
  EXPECT_EQ(packed.wparam & 0xFFU, kPin);
  EXPECT_TRUE(MatchesPinCallback(packed, kPin, 7, "こうしょう", "考証"));
  EXPECT_FALSE(MatchesPinCallback(packed, kPin, 8, "こうしょう", "考証"));
  EXPECT_FALSE(MatchesPinCallback(packed, kPin, 7, "こうしょう", "交渉"));
  EXPECT_FALSE(MatchesPinCallback(packed, kPin, 7, "こうじょう", "考証"));
  EXPECT_FALSE(MatchesPinCallback(packed, kUnpin, 7, "こうしょう", "考証"));
}

TEST(PinCallbackTokenTest, UnpinAndPortableThirtyTwoBitWireFields) {
  const PinCallbackMessage packed =
      PackPinCallback(kUnpin, -3, "きょう", "今日");
  EXPECT_EQ(packed.wparam & 0xFFU, kUnpin);
  EXPECT_TRUE(MatchesPinCallback(
      {static_cast<uint32_t>(packed.wparam),
       static_cast<uint32_t>(packed.lparam)},
      kUnpin, -3, "きょう", "今日"));
  EXPECT_FALSE(MatchesPinCallback(packed, kPin, -3, "きょう", "今日"));
}

}  // namespace
}  // namespace mozc::renderer
