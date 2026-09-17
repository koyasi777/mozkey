// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
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
#include "renderer/win32/cascading_window_layout.h"

#include "base/coordinates.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

void ExpectRect(const Rect& actual, int x, int y, int width, int height) {
  EXPECT_EQ(actual.Left(), x);
  EXPECT_EQ(actual.Top(), y);
  EXPECT_EQ(actual.Width(), width);
  EXPECT_EQ(actual.Height(), height);
}

TEST(CascadingWindowLayoutTest, WritingDirectionSelectsLayoutMode) {
  EXPECT_EQ(GetCascadingWindowLayoutMode(false),
            CascadingWindowLayoutMode::kHorizontal);
  EXPECT_EQ(GetCascadingWindowLayoutMode(true),
            CascadingWindowLayoutMode::kVertical);
}

TEST(CascadingWindowLayoutTest, HorizontalPreservesLegacyRightPlacement) {
  const Rect selected_candidate(260, 220, 40, 80);
  const Rect selected_row_with_window_border(100, 200, 300, 30);
  const Rect candidate_rect(100, 200, 200, 300);
  const Size cascade_size(80, 100);
  const Point zero_point(0, 0);
  const Rect preedit_rect(300, 200, 20, 300);
  const Rect working_area(0, 0, 1000, 800);

  const Rect result = GetCascadingWindowRect(
      CascadingWindowLayoutMode::kHorizontal, selected_candidate,
      selected_row_with_window_border, candidate_rect, cascade_size,
      zero_point, preedit_rect, working_area);

  ExpectRect(result, 400, 200, 80, 100);
}

TEST(CascadingWindowLayoutTest, VerticalPrefersOutsideLeft) {
  const Rect selected_candidate(260, 220, 40, 80);
  const Rect selected_row_with_window_border(100, 200, 300, 30);
  const Rect candidate_rect(100, 200, 200, 300);
  const Size cascade_size(80, 100);
  const Point zero_point(0, 10);
  const Rect preedit_rect(300, 200, 20, 300);
  const Rect working_area(0, 0, 1000, 800);

  const Rect result = GetCascadingWindowRect(
      CascadingWindowLayoutMode::kVertical, selected_candidate,
      selected_row_with_window_border, candidate_rect, cascade_size,
      zero_point, preedit_rect, working_area);

  ExpectRect(result, 20, 210, 80, 100);
}

TEST(CascadingWindowLayoutTest, VerticalFallsBackRightAtLeftScreenEdge) {
  const Rect selected_candidate(260, 220, 40, 80);
  const Rect selected_row_with_window_border(100, 200, 300, 30);
  const Rect candidate_rect(100, 200, 200, 300);
  const Size cascade_size(80, 100);
  const Point zero_point(0, 10);
  const Rect preedit_rect(300, 200, 20, 300);
  const Rect working_area(50, 0, 950, 800);

  const Rect result = GetCascadingWindowRect(
      CascadingWindowLayoutMode::kVertical, selected_candidate,
      selected_row_with_window_border, candidate_rect, cascade_size,
      zero_point, preedit_rect, working_area);

  ExpectRect(result, 320, 210, 80, 100);
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
