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
}

TEST(ZenzSegmentProjectionTest, RejectsAmbiguousAdjacentChangedSegments) {
  const std::vector<ZenzBaselineSegment> baseline = {
      {"とうきょう", "東京"}, {"おおさか", "大阪"}};

  const ZenzSegmentProjection projection = ProjectZenzValueToMozcSegments(
      baseline, "とうきょうおおさか", "TokyoOsaka");

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
