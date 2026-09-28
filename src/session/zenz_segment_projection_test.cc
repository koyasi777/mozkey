// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "session/zenz_segment_projection.h"

#include <string>
#include <vector>

#include "testing/gunit.h"

namespace mozc::session {
namespace {

TEST(ZenzSegmentProjectionTest, ProjectsSingleChangedSegmentBetweenAnchors) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"とうきょう", "東京"}, {"に", "に"}, {"いく", "行く"}};

  const ZenzSegmentProjection projection = ProjectZenzValueToMozcSegments(
      baseline, "とうきょうにいく", "Tokyoに行く");

  ASSERT_TRUE(projection.success);
  ASSERT_EQ(projection.segments.size(), 3);
  EXPECT_EQ(projection.segments[0].zenz_value, "Tokyo");
  EXPECT_TRUE(projection.segments[0].changed);
  EXPECT_TRUE(projection.segments[0].boundary_known);
  EXPECT_EQ(projection.segments[1].zenz_value, "に");
  EXPECT_FALSE(projection.segments[1].changed);
  EXPECT_EQ(projection.segments[2].zenz_value, "行く");
  EXPECT_FALSE(projection.segments[2].changed);
}

TEST(ZenzSegmentProjectionTest, ProjectsSingleSegmentRewrite) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"とうきょうにいく", "東京にいく"}};

  const ZenzSegmentProjection projection = ProjectZenzValueToMozcSegments(
      baseline, "とうきょうにいく", "Tokyoに行く");

  ASSERT_TRUE(projection.success);
  ASSERT_EQ(projection.segments.size(), 1);
  EXPECT_EQ(projection.segments[0].zenz_value, "Tokyoに行く");
  EXPECT_TRUE(projection.segments[0].changed);
  EXPECT_TRUE(projection.segments[0].boundary_known);
}

TEST(ZenzSegmentProjectionTest,
     PreservesAmbiguousAdjacentChangedSegmentsAsUnresolvedAggregate) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"とうきょう", "東京"}, {"おおさか", "大阪"}};

  const ZenzSegmentProjection projection = ProjectZenzValueToMozcSegments(
      baseline, "とうきょうおおさか", "TokyoOsaka");

  ASSERT_TRUE(projection.success);
  ASSERT_EQ(projection.segments.size(), 1);
  EXPECT_EQ(projection.segments[0].key, "とうきょうおおさか");
  EXPECT_EQ(projection.segments[0].mozc_value, "東京大阪");
  EXPECT_EQ(projection.segments[0].zenz_value, "TokyoOsaka");
  EXPECT_TRUE(projection.segments[0].changed);
  EXPECT_FALSE(projection.segments[0].boundary_known);
  EXPECT_EQ(projection.segments[0].baseline_keys,
            std::vector<std::string>({"とうきょう", "おおさか"}));
}

TEST(ZenzSegmentProjectionTest,
     KeepsSafeSymbolAnchorsAroundAmbiguousChangedSpan) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"ほんむ", "本務"}, {"りょう", "量"}, {"(", "（"},
      {"げんかく", "幻覚"}, {")", "）"}, {"のことです", "のことです"},
      {".", "。"}};

  const ZenzSegmentProjection projection = ProjectZenzValueToMozcSegments(
      baseline, "ほんむりょう(げんかく)のことです.",
      "本無料（幻覚）のことです。");

  ASSERT_TRUE(projection.success);
  ASSERT_EQ(projection.segments.size(), 6);

  EXPECT_EQ(projection.segments[0].key, "ほんむりょう");
  EXPECT_EQ(projection.segments[0].zenz_value, "本無料");
  EXPECT_TRUE(projection.segments[0].changed);
  EXPECT_FALSE(projection.segments[0].boundary_known);
  EXPECT_EQ(projection.segments[0].baseline_keys,
            std::vector<std::string>({"ほんむ", "りょう"}));

  EXPECT_EQ(projection.segments[1].key, "(");
  EXPECT_EQ(projection.segments[1].zenz_value, "（");
  EXPECT_TRUE(projection.segments[1].boundary_known);
  EXPECT_EQ(projection.segments[2].key, "げんかく");
  EXPECT_EQ(projection.segments[2].zenz_value, "幻覚");
  EXPECT_EQ(projection.segments[3].key, ")");
  EXPECT_EQ(projection.segments[3].zenz_value, "）");
  EXPECT_EQ(projection.segments[4].key, "のことです");
  EXPECT_EQ(projection.segments[4].zenz_value, "のことです");
  EXPECT_EQ(projection.segments[5].key, ".");
  EXPECT_EQ(projection.segments[5].zenz_value, "。");
}

TEST(ZenzSegmentProjectionTest,
     PeelsUnchangedAttachedPunctuationFromChangedLexicalSegment) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"げんしょう(", "減少（"},
      {"げんかく", "幻覚"},
      {")", "）"},
      {"のことです.", "のことです。"},
  };

  const ZenzSegmentProjection projection = ProjectZenzValueToMozcSegments(
      baseline, "げんしょう(げんかく)のことです.",
      "現象（幻覚）のことです。");

  ASSERT_TRUE(projection.success);
  ASSERT_EQ(projection.segments.size(), 5);

  EXPECT_EQ(projection.segments[0].key, "げんしょう");
  EXPECT_EQ(projection.segments[0].mozc_value, "減少");
  EXPECT_EQ(projection.segments[0].zenz_value, "現象");
  EXPECT_TRUE(projection.segments[0].changed);
  EXPECT_TRUE(projection.segments[0].boundary_known);

  EXPECT_EQ(projection.segments[1].key, "(");
  EXPECT_EQ(projection.segments[1].mozc_value, "（");
  EXPECT_EQ(projection.segments[1].zenz_value, "（");
  EXPECT_FALSE(projection.segments[1].changed);

  EXPECT_EQ(projection.segments[2].key, "げんかく");
  EXPECT_EQ(projection.segments[2].zenz_value, "幻覚");
  EXPECT_EQ(projection.segments[3].key, ")");
  EXPECT_EQ(projection.segments[3].zenz_value, "）");
  EXPECT_EQ(projection.segments[4].key, "のことです.");
  EXPECT_EQ(projection.segments[4].zenz_value, "のことです。");
}

TEST(ZenzSegmentProjectionTest,
     PeelsUnchangedLeadingPunctuationFromChangedLexicalSegment) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {")げんしょう", "）減少"}, {"です", "です"}};

  const ZenzSegmentProjection projection = ProjectZenzValueToMozcSegments(
      baseline, ")げんしょうです", "）現象です");

  ASSERT_TRUE(projection.success);
  ASSERT_EQ(projection.segments.size(), 3);
  EXPECT_EQ(projection.segments[0].key, ")");
  EXPECT_EQ(projection.segments[0].zenz_value, "）");
  EXPECT_FALSE(projection.segments[0].changed);
  EXPECT_EQ(projection.segments[1].key, "げんしょう");
  EXPECT_EQ(projection.segments[1].mozc_value, "減少");
  EXPECT_EQ(projection.segments[1].zenz_value, "現象");
  EXPECT_TRUE(projection.segments[1].changed);
  EXPECT_EQ(projection.segments[2].key, "です");
}

TEST(ZenzSegmentProjectionTest, DoesNotPeelAttachedAsciiLettersOrNumbers) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"げんしょう1", "減少１"}, {"です", "です"}};

  const ZenzSegmentProjection projection = ProjectZenzValueToMozcSegments(
      baseline, "げんしょう1です", "現象１です");

  ASSERT_TRUE(projection.success);
  ASSERT_EQ(projection.segments.size(), 2);
  EXPECT_EQ(projection.segments[0].key, "げんしょう1");
  EXPECT_EQ(projection.segments[0].zenz_value, "現象１");
  EXPECT_TRUE(projection.segments[0].changed);
  EXPECT_EQ(projection.segments[1].key, "です");
}

TEST(ZenzSegmentProjectionTest,
     ReconcilesFullWidthAsciiPunctuationKeysWithAsciiRequestKey) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"じっさいには", "実際には"},
      {"そんざいしないものを", "存在しないものを"},
      {"みたり", "見たり"},
      {"きいたり", "聞いたり"},
      {"する", "する"},
      {"げんしょう", "減少"},
      {"（", "（"},
      {"げんかく", "幻覚"},
      {"）", "）"},
      {"のことです", "のことです"},
  };

  const ZenzSegmentProjection projection = ProjectZenzValueToMozcSegments(
      baseline,
      "じっさいにはそんざいしないものをみたりきいたりするげんしょう(げんかく)のことです",
      "実際には存在しないものを見たり聞いたりする現象（幻覚）のことです");

  ASSERT_TRUE(projection.success);
  ASSERT_EQ(projection.segments.size(), 10);

  EXPECT_EQ(projection.segments[5].key, "げんしょう");
  EXPECT_EQ(projection.segments[5].mozc_value, "減少");
  EXPECT_EQ(projection.segments[5].zenz_value, "現象");
  EXPECT_TRUE(projection.segments[5].changed);

  EXPECT_EQ(projection.segments[6].key, "(");
  EXPECT_EQ(projection.segments[6].mozc_value, "（");
  EXPECT_EQ(projection.segments[6].zenz_value, "（");
  EXPECT_FALSE(projection.segments[6].changed);

  EXPECT_EQ(projection.segments[8].key, ")");
  EXPECT_EQ(projection.segments[8].mozc_value, "）");
  EXPECT_EQ(projection.segments[8].zenz_value, "）");
  EXPECT_FALSE(projection.segments[8].changed);
}

TEST(ZenzSegmentProjectionTest,
     ReconcilesWaveDashDisplayKeyWithAsciiTildeRequestKey) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"〜", "〜"}, {"です", "です"}};

  const ZenzSegmentProjection projection =
      ProjectZenzValueToMozcSegments(baseline, "~です", "〜です");

  ASSERT_TRUE(projection.success);
  ASSERT_EQ(projection.segments.size(), 2);
  EXPECT_EQ(projection.segments[0].key, "~");
  EXPECT_EQ(projection.segments[0].zenz_value, "〜");
  EXPECT_FALSE(projection.segments[0].changed);
}

TEST(ZenzSegmentProjectionTest,
     ReconcileBaselineKeysReturnsCanonicalRequestKeys) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"（", "（"}, {"〜", "〜"}, {"）", "）"}};

  std::vector<ZenzBaselineSegment> reconciled;
  ASSERT_TRUE(ReconcileZenzBaselineKeysForRequest(
      baseline, "(~)", &reconciled));
  ASSERT_EQ(reconciled.size(), 3);
  EXPECT_EQ(reconciled[0].key, "(");
  EXPECT_EQ(reconciled[1].key, "~");
  EXPECT_EQ(reconciled[2].key, ")");
}

TEST(ZenzSegmentProjectionTest,
     DoesNotNormalizeFullWidthLettersForRequestIdentity) {
  const std::vector<ZenzBaselineSegment> baseline = {{"Ａ", "Ａ"}};

  const ZenzSegmentProjection projection =
      ProjectZenzValueToMozcSegments(baseline, "A", "Ａ");

  EXPECT_FALSE(projection.success);
  EXPECT_TRUE(projection.segments.empty());
}

TEST(ZenzSegmentProjectionTest,
     DoesNotAcceptDifferentAsciiPunctuationAfterWidthReconciliation) {
  const std::vector<ZenzBaselineSegment> baseline = {{"（", "（"}};

  const ZenzSegmentProjection projection =
      ProjectZenzValueToMozcSegments(baseline, "[", "（");

  EXPECT_FALSE(projection.success);
  EXPECT_TRUE(projection.segments.empty());
}

TEST(ZenzSegmentProjectionTest, RejectsMismatchedFullKey) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"とうきょう", "東京"}};

  const ZenzSegmentProjection projection =
      ProjectZenzValueToMozcSegments(baseline, "おおさか", "Tokyo");

  EXPECT_FALSE(projection.success);
}

}  // namespace
}  // namespace mozc::session
