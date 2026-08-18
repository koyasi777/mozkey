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
#include "win32/tip/tip_writing_direction.h"

#include <optional>

#include "testing/gunit.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

TEST(TipWritingDirectionTest, VerticalWritingAttributeTakesPrecedence) {
  EXPECT_EQ(ResolveWritingDirection(true, 0), WritingDirection::kVertical);
  EXPECT_EQ(ResolveWritingDirection(false, 2700),
            WritingDirection::kHorizontal);
}

TEST(TipWritingDirectionTest, OrientationFallbackRecognizesCardinalAxes) {
  EXPECT_EQ(ResolveWritingDirection(std::nullopt, 0),
            WritingDirection::kHorizontal);
  EXPECT_EQ(ResolveWritingDirection(std::nullopt, 1800),
            WritingDirection::kHorizontal);
  EXPECT_EQ(ResolveWritingDirection(std::nullopt, 900),
            WritingDirection::kVertical);
  EXPECT_EQ(ResolveWritingDirection(std::nullopt, 2700),
            WritingDirection::kVertical);
  EXPECT_EQ(ResolveWritingDirection(std::nullopt, -900),
            WritingDirection::kVertical);
}

TEST(TipWritingDirectionTest, OrientationFallbackRejectsAmbiguousAngles) {
  EXPECT_EQ(ResolveWritingDirection(std::nullopt, 450),
            WritingDirection::kUnknown);
  EXPECT_EQ(ResolveWritingDirection(std::nullopt, std::nullopt),
            WritingDirection::kUnknown);
}

TEST(TipWritingDirectionTest, GeometryDetectsObservedHorizontalGrowth) {
  EXPECT_EQ(InferWritingDirectionFromExtentGrowth(34, 49, 67, 49),
            WritingDirection::kHorizontal);
}

TEST(TipWritingDirectionTest, GeometryDetectsObservedVerticalGrowth) {
  EXPECT_EQ(InferWritingDirectionFromExtentGrowth(51, 40, 51, 79),
            WritingDirection::kVertical);
}

TEST(TipWritingDirectionTest, GeometryAllowsSmallCrossAxisNoise) {
  EXPECT_EQ(InferWritingDirectionFromExtentGrowth(50, 40, 51, 80),
            WritingDirection::kVertical);
  EXPECT_EQ(InferWritingDirectionFromExtentGrowth(50, 40, 85, 41),
            WritingDirection::kHorizontal);
}

TEST(TipWritingDirectionTest, GeometryRejectsAmbiguousOrInvalidGrowth) {
  EXPECT_EQ(InferWritingDirectionFromExtentGrowth(50, 40, 70, 60),
            WritingDirection::kUnknown);
  EXPECT_EQ(InferWritingDirectionFromExtentGrowth(50, 40, 51, 40),
            WritingDirection::kUnknown);
  EXPECT_EQ(InferWritingDirectionFromExtentGrowth(50, 40, 49, 80),
            WritingDirection::kUnknown);
}

}  // namespace
}  // namespace tsf
}  // namespace win32
}  // namespace mozc