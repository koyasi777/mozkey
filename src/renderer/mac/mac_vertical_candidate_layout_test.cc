// Copyright 2026, Mozkey authors.
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
//     * Neither the name of Mozkey authors nor the names of its
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

#include "renderer/mac/mac_vertical_candidate_layout.h"

#include <vector>

#include "base/coordinates.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace mac {
namespace {

using Metrics = MacVerticalCandidateLayout::CandidateMetrics;
using Parameters = MacVerticalCandidateLayout::Parameters;

Parameters DefaultParameters() {
  Parameters p;
  p.window_border = 1;
  p.cross_axis_edge_padding = 2;
  p.candidate_gap = 1;
  p.card_horizontal_padding = 2;
  p.card_vertical_padding = 7;
  p.shortcut_body_gap = 5;
  p.value_description_gap = 12;
  p.information_marker_gap = 2;
  return p;
}

Parameters PassiveSuggestionParameters() {
  Parameters p = DefaultParameters();
  p.cross_axis_edge_padding = 2;
  p.candidate_gap = 1;
  p.card_vertical_padding = 4;
  p.shortcut_body_gap = 3;
  p.value_description_gap = 7;
  return p;
}

TEST(MacVerticalCandidateLayoutTest,
     CandidateZeroIsRightmostAndSemanticOrderIsPreserved) {
  MacVerticalCandidateLayout layout;
  const std::vector<Metrics> metrics = {
      {Size(10, 19), Size(42, 83), Size(24, 46), Size()},
      {Size(10, 19), Size(42, 83), Size(24, 46), Size()},
      {Size(10, 19), Size(42, 144), Size(), Size()},
  };

  layout.Initialize(metrics, DefaultParameters());

  ASSERT_EQ(layout.candidate_count(), 3);
  const Rect c0 = layout.GetCandidateRect(0);
  const Rect c1 = layout.GetCandidateRect(1);
  const Rect c2 = layout.GetCandidateRect(2);

  EXPECT_GT(c0.Left(), c1.Left());
  EXPECT_GT(c1.Left(), c2.Left());
  EXPECT_EQ(c0.Width(), 46);
  EXPECT_EQ(c1.Width(), 46);
  EXPECT_EQ(c2.Width(), 46);
}

TEST(MacVerticalCandidateLayoutTest,
     DescriptionIsVerticalTailBelowValueNotSideColumn) {
  MacVerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();
  layout.Initialize(
      {{Size(10, 19), Size(42, 83), Size(24, 46), Size()}}, p);

  const Rect candidate = layout.GetCandidateRect(0);
  const Rect value = layout.GetValueRect(0);
  const Rect description = layout.GetDescriptionRect(0);

  ASSERT_FALSE(value.IsRectEmpty());
  ASSERT_FALSE(description.IsRectEmpty());

  EXPECT_EQ(value.Left(),
            candidate.Left() + (candidate.Width() - value.Width()) / 2);
  EXPECT_EQ(description.Left(),
            candidate.Left() +
                (candidate.Width() - description.Width()) / 2);
  EXPECT_EQ(description.Top(),
            value.Bottom() + p.value_description_gap);
  EXPECT_GT(description.Top(), value.Top());
}

TEST(MacVerticalCandidateLayoutTest,
     TypicalFullAnnotationStaysNarrowAndUsesNaturalHeight) {
  MacVerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  layout.Initialize(
      {
          {Size(10, 19), Size(42, 83), Size(24, 46), Size()},
          {Size(10, 19), Size(42, 83), Size(24, 46), Size()},
          {Size(10, 19), Size(42, 144), Size(), Size()},
      },
      p);

  const Rect c0 = layout.GetCandidateRect(0);
  const Rect c1 = layout.GetCandidateRect(1);
  const Rect c2 = layout.GetCandidateRect(2);

  EXPECT_EQ(c0.Width(), 46);
  EXPECT_EQ(c0.Height(), 179);
  EXPECT_EQ(c1.Width(), 46);
  EXPECT_EQ(c1.Height(), 179);
  EXPECT_EQ(c2.Width(), 46);
  EXPECT_EQ(c2.Height(), 182);
}

TEST(MacVerticalCandidateLayoutTest,
     LongDescriptionAffectsOnlyItsOwnHeightNeverSiblingWidthOrHeight) {
  MacVerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  layout.Initialize(
      {
          {Size(10, 19), Size(42, 164), Size(24, 168), Size()},
          {Size(10, 19), Size(42, 83), Size(24, 46), Size()},
      },
      p);

  const Rect long_candidate = layout.GetCandidateRect(0);
  const Rect normal_candidate = layout.GetCandidateRect(1);

  EXPECT_EQ(long_candidate.Width(), 46);
  EXPECT_EQ(normal_candidate.Width(), 46);
  EXPECT_GT(long_candidate.Height(), normal_candidate.Height());
  EXPECT_EQ(normal_candidate.Height(), 179);
}

TEST(MacVerticalCandidateLayoutTest,
     SourceBackedOneGlyphMetricsRemainIndependent) {
  MacVerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  layout.Initialize(
      {
          {Size(10, 19), Size(26, 26), Size(24, 90), Size()},
          {Size(10, 19), Size(26, 26), Size(24, 35), Size()},
          {Size(10, 19), Size(26, 26), Size(24, 41), Size()},
      },
      p);

  EXPECT_EQ(layout.GetCandidateRect(0).Height(), 166);
  EXPECT_EQ(layout.GetCandidateRect(1).Height(), 111);
  EXPECT_EQ(layout.GetCandidateRect(2).Height(), 117);
  EXPECT_EQ(layout.GetCandidateRect(0).Width(), 30);
  EXPECT_EQ(layout.GetCandidateRect(1).Width(), 30);
  EXPECT_EQ(layout.GetCandidateRect(2).Width(), 30);
}

TEST(MacVerticalCandidateLayoutTest,
     CorrectedCoreTextMetricsStayNarrowCenteredAndNaturalHeight) {
  MacVerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  // The visible cross-axis metric is decoupled from Core Text's wider
  // technical frame.  The raster-ink probe measured ordinary 14 pt Japanese
  // at 20 pt visual width and 12 pt descriptions at 17 pt visual width.
  layout.Initialize(
      {
          {Size(10, 19), Size(20, 53), Size(17, 46), Size()},
          {Size(10, 19), Size(20, 53), Size(), Size()},
          {Size(10, 19), Size(20, 53), Size(17, 46), Size()},
          {Size(10, 19), Size(20, 105), Size(), Size()},
      },
      p);

  EXPECT_EQ(layout.GetCandidateRect(0).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(1).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(2).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(3).Width(), 24);

  EXPECT_EQ(layout.GetCandidateRect(0).Height(), 149);
  EXPECT_EQ(layout.GetCandidateRect(1).Height(), 91);
  EXPECT_EQ(layout.GetCandidateRect(2).Height(), 149);
  EXPECT_EQ(layout.GetCandidateRect(3).Height(), 143);

  EXPECT_EQ(layout.GetValueRect(0).Left(),
            layout.GetDescriptionRect(0).Left() -
                (layout.GetValueRect(0).Width() -
                 layout.GetDescriptionRect(0).Width()) / 2);
  EXPECT_EQ(layout.GetDescriptionRect(0).Top(),
            layout.GetValueRect(0).Bottom() +
                p.value_description_gap);
}

TEST(MacVerticalCandidateLayoutTest,
     CorrectedCoreTextStressDoesNotStretchSiblings) {
  MacVerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  // Keep one deliberately long candidate to prove that natural height remains
  // local to that candidate.  Sibling metrics use the corrected Core Text
  // dimensions from the diagnostic probe.
  layout.Initialize(
      {
          {Size(10, 19), Size(20, 350), Size(17, 46), Size()},
          {Size(10, 19), Size(20, 53), Size(17, 46), Size()},
          {Size(10, 19), Size(20, 105), Size(), Size()},
          {Size(10, 19), Size(20, 53), Size(), Size()},
      },
      p);

  EXPECT_EQ(layout.GetCandidateRect(0).Height(), 446);
  EXPECT_EQ(layout.GetCandidateRect(1).Height(), 149);
  EXPECT_EQ(layout.GetCandidateRect(2).Height(), 143);
  EXPECT_EQ(layout.GetCandidateRect(3).Height(), 91);
}

TEST(MacVerticalCandidateLayoutTest,
     PassiveSuggestionCompactBUsesRectangularNaturalBodyWithoutFooter) {
  MacVerticalCandidateLayout layout;
  const Parameters p = PassiveSuggestionParameters();

  layout.Initialize(
      {
          {Size(), Size(20, 53), Size(), Size()},
          {Size(), Size(20, 53), Size(), Size()},
          {Size(), Size(20, 105), Size(), Size()},
      },
      p);

  EXPECT_EQ(layout.GetCandidateRect(0).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(1).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(2).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(0).Height(), 61);
  EXPECT_EQ(layout.GetCandidateRect(1).Height(), 61);
  EXPECT_EQ(layout.GetCandidateRect(2).Height(), 113);
  EXPECT_TRUE(layout.GetFooterRect().IsRectEmpty());

  EXPECT_EQ(layout.GetTotalSize().width, 80);
  EXPECT_EQ(layout.GetTotalSize().height, 115);
}

TEST(MacVerticalCandidateLayoutTest,
     InkNormalizedVisualWidthsKeepOrdinaryScriptsW24AndExpandRealInk) {
  MacVerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  // CandidateView normalizes ordinary Japanese, ASCII, mixed runs, and
  // halfwidth katakana to the nominal 20 pt visible cross-axis.  Only actual
  // glyph ink wider than that floor reaches this geometry layer as a wider
  // value size.  The technical Core Text frame width remains separate.
  layout.Initialize(
      {
          {Size(), Size(20, 53), Size(), Size()},   // Japanese -> W24.
          {Size(), Size(20, 67), Size(), Size()},   // ASCII -> W24.
          {Size(), Size(20, 143), Size(), Size()},  // Mixed -> W24.
          {Size(), Size(20, 58), Size(), Size()},   // Half-katakana -> W24.
          {Size(), Size(24, 58), Size(), Size()},   // Wide real ink -> W28.
          {Size(), Size(20, 53), Size(17, 46), Size()},  // JP + description.
      },
      p);

  ASSERT_EQ(layout.candidate_count(), 6);
  EXPECT_EQ(layout.GetCandidateRect(0).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(1).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(2).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(3).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(4).Width(), 28);
  EXPECT_EQ(layout.GetCandidateRect(5).Width(), 24);
  EXPECT_EQ(layout.GetDescriptionRect(5).Width(), 17);
}

TEST(MacVerticalCandidateLayoutTest, ZeroAreaMetricDoesNotReserveSpace) {
  MacVerticalCandidateLayout layout;
  Parameters p = DefaultParameters();

  layout.Initialize(
      {{Size(10, 19), Size(42, 83), Size(100, 0), Size()}}, p);

  EXPECT_TRUE(layout.GetDescriptionRect(0).IsRectEmpty());

  const int expected_height =
      p.card_vertical_padding * 2 + 19 + p.shortcut_body_gap + 83;
  EXPECT_EQ(layout.GetCandidateRect(0).Height(), expected_height);
}

TEST(MacVerticalCandidateLayoutTest,
     FooterMayWidenWindowWithoutMovingCandidateZeroOffRightFlowEdge) {
  MacVerticalCandidateLayout layout;
  Parameters p = DefaultParameters();
  p.footer_size = Size(240, 20);

  layout.Initialize(
      {{Size(10, 19), Size(42, 83), Size(24, 46), Size()}}, p);

  const Rect candidate = layout.GetCandidateRect(0);
  const Rect footer = layout.GetFooterRect();

  EXPECT_EQ(footer.Width(), 240);
  EXPECT_EQ(candidate.Right(),
            layout.GetTotalSize().width - p.window_border -
                p.cross_axis_edge_padding);
}

TEST(MacVerticalCandidateLayoutTest,
     InformationMarkerFollowsTailInsideNaturalCard) {
  MacVerticalCandidateLayout layout;
  Parameters p = DefaultParameters();

  layout.Initialize(
      {{Size(10, 19), Size(42, 83), Size(24, 46), Size(14, 2)}}, p);

  const Rect description = layout.GetDescriptionRect(0);
  const Rect marker = layout.GetInformationMarkerRect(0);
  const Rect candidate = layout.GetCandidateRect(0);

  ASSERT_FALSE(marker.IsRectEmpty());
  EXPECT_EQ(marker.Top(),
            description.Bottom() + p.information_marker_gap);
  EXPECT_LE(marker.Bottom(),
            candidate.Bottom() - p.card_vertical_padding);
}

TEST(MacVerticalCandidateLayoutTest,
     EmptyInputStillKeepsHorizontalFooterContract) {
  MacVerticalCandidateLayout layout;
  Parameters p = DefaultParameters();
  p.footer_size = Size(100, 20);

  layout.Initialize({}, p);

  EXPECT_EQ(layout.candidate_count(), 0);

  const Size total_size = layout.GetTotalSize();
  EXPECT_EQ(total_size.width, p.window_border * 2 + 100);
  EXPECT_EQ(total_size.height, p.window_border * 2 + 20);

  const Rect footer = layout.GetFooterRect();
  EXPECT_EQ(footer.Left(), p.window_border);
  EXPECT_EQ(footer.Top(), p.window_border);
  EXPECT_EQ(footer.Width(), 100);
  EXPECT_EQ(footer.Height(), 20);
}

}  // namespace
}  // namespace mac
}  // namespace renderer
}  // namespace mozc
