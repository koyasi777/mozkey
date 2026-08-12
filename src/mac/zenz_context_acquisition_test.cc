// Copyright 2026
// Licensed under the same terms as Mozc.

#include "mac/zenz_context_acquisition.h"

#include <optional>

#include "testing/gunit.h"

namespace mozc::mac {
namespace {

TEST(ZenzContextAcquisitionTest, ZeroRequestsDoNotProduceNativeRanges) {
  const std::optional<ZenzContextNativeRanges> ranges =
      GetZenzContextNativeRanges(100, 50, 0, 0, 0);

  ASSERT_TRUE(ranges.has_value());
  EXPECT_FALSE(ranges->preceding.has_value());
  EXPECT_FALSE(ranges->following.has_value());
}

TEST(ZenzContextAcquisitionTest, UsesIndependentDirectionalUtf16Budgets) {
  const std::optional<ZenzContextNativeRanges> ranges =
      GetZenzContextNativeRanges(100, 50, 0, 24, 10);

  ASSERT_TRUE(ranges.has_value());
  ASSERT_TRUE(ranges->preceding.has_value());
  ASSERT_TRUE(ranges->following.has_value());
  EXPECT_EQ(ranges->preceding->location, 1);
  EXPECT_EQ(ranges->preceding->length, 49);
  EXPECT_EQ(ranges->following->location, 50);
  EXPECT_EQ(ranges->following->length, 21);
}

TEST(ZenzContextAcquisitionTest, ExcludesSelectedText) {
  const std::optional<ZenzContextNativeRanges> ranges =
      GetZenzContextNativeRanges(30, 10, 4, 3, 4);

  ASSERT_TRUE(ranges.has_value());
  ASSERT_TRUE(ranges->preceding.has_value());
  ASSERT_TRUE(ranges->following.has_value());
  EXPECT_EQ(ranges->preceding->location, 3);
  EXPECT_EQ(ranges->preceding->length, 7);
  EXPECT_EQ(ranges->following->location, 14);
  EXPECT_EQ(ranges->following->length, 9);
}

TEST(ZenzContextAcquisitionTest, ClampsNativeRangesAtDocumentBoundaries) {
  const std::optional<ZenzContextNativeRanges> ranges =
      GetZenzContextNativeRanges(10, 3, 2, 24, 24);

  ASSERT_TRUE(ranges.has_value());
  ASSERT_TRUE(ranges->preceding.has_value());
  ASSERT_TRUE(ranges->following.has_value());
  EXPECT_EQ(ranges->preceding->location, 0);
  EXPECT_EQ(ranges->preceding->length, 3);
  EXPECT_EQ(ranges->following->location, 5);
  EXPECT_EQ(ranges->following->length, 5);
}

TEST(ZenzContextAcquisitionTest, RequestedBoundaryProducesEmptyNativeRange) {
  const std::optional<ZenzContextNativeRanges> ranges =
      GetZenzContextNativeRanges(5, 0, 5, 1, 1);

  ASSERT_TRUE(ranges.has_value());
  ASSERT_TRUE(ranges->preceding.has_value());
  ASSERT_TRUE(ranges->following.has_value());
  EXPECT_EQ(ranges->preceding->location, 0);
  EXPECT_EQ(ranges->preceding->length, 0);
  EXPECT_EQ(ranges->following->location, 5);
  EXPECT_EQ(ranges->following->length, 0);
}

TEST(ZenzContextAcquisitionTest, RejectsInvalidSelectionLocation) {
  EXPECT_FALSE(GetZenzContextNativeRanges(10, 11, 0, 1, 1).has_value());
}

TEST(ZenzContextAcquisitionTest, RejectsSelectionPastDocumentEnd) {
  EXPECT_FALSE(GetZenzContextNativeRanges(10, 8, 3, 1, 1).has_value());
}

TEST(ZenzContextAcquisitionTest, DefensivelyClampsServerRequestLengths) {
  const std::optional<ZenzContextNativeRanges> ranges =
      GetZenzContextNativeRanges(1000, 500, 0, 4096, 4096);

  ASSERT_TRUE(ranges.has_value());
  ASSERT_TRUE(ranges->preceding.has_value());
  ASSERT_TRUE(ranges->following.has_value());
  EXPECT_EQ(ranges->preceding->location, 243);
  EXPECT_EQ(ranges->preceding->length, 257);
  EXPECT_EQ(ranges->following->location, 500);
  EXPECT_EQ(ranges->following->length, 257);
}

TEST(ZenzContextAcquisitionTest, AddsOneUtf16UnitOfEdgePadding) {
  const std::optional<ZenzContextNativeRanges> ranges =
      GetZenzContextNativeRanges(100, 50, 0, 1, 1);

  ASSERT_TRUE(ranges.has_value());
  ASSERT_TRUE(ranges->preceding.has_value());
  ASSERT_TRUE(ranges->following.has_value());
  EXPECT_EQ(ranges->preceding->location, 47);
  EXPECT_EQ(ranges->preceding->length, 3);
  EXPECT_EQ(ranges->following->location, 50);
  EXPECT_EQ(ranges->following->length, 3);
}

TEST(ZenzContextAcquisitionTest, TakesLeadingUnicodeCharacters) {
  constexpr char kText[] =
      "\xE7\x94\xB2\xF0\xA0\xAE\x9F\xE4\xB9\x99\xE4\xB8\x99";
  constexpr char kFirstTwo[] = "\xE7\x94\xB2\xF0\xA0\xAE\x9F";

  EXPECT_EQ(TakeLeadingZenzContextCharacters(kText, 2), kFirstTwo);
  EXPECT_EQ(TakeLeadingZenzContextCharacters(kText, 99), kText);
  EXPECT_EQ(TakeLeadingZenzContextCharacters(kText, 0), "");
}

TEST(ZenzContextAcquisitionTest, TakesTrailingUnicodeCharacters) {
  constexpr char kText[] =
      "\xE7\x94\xB2\xF0\xA0\xAE\x9F\xE4\xB9\x99\xE4\xB8\x99";
  constexpr char kLastTwo[] = "\xE4\xB9\x99\xE4\xB8\x99";

  EXPECT_EQ(TakeTrailingZenzContextCharacters(kText, 2), kLastTwo);
  EXPECT_EQ(TakeTrailingZenzContextCharacters(kText, 99), kText);
  EXPECT_EQ(TakeTrailingZenzContextCharacters(kText, 0), "");
}

}  // namespace
}  // namespace mozc::mac
