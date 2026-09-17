#include "renderer/win32/vertical_candidate_layout.h"

#include <vector>

#include "base/coordinates.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

using Metrics = VerticalCandidateLayout::CandidateMetrics;
using Parameters = VerticalCandidateLayout::Parameters;

void ExpectRect(const Rect& actual, int x, int y, int width, int height) {
  EXPECT_EQ(actual.Left(), x);
  EXPECT_EQ(actual.Top(), y);
  EXPECT_EQ(actual.Width(), width);
  EXPECT_EQ(actual.Height(), height);
}

Parameters DefaultParameters() {
  Parameters p;
  p.window_border = 1;
  p.cross_axis_edge_padding = 2;
  p.candidate_gap = 1;
  p.card_horizontal_padding = 2;
  p.card_vertical_padding = 7;
  p.shortcut_body_gap = 5;
  p.value_description_gap = 12;
  p.information_marker_tail_reserve = 7;
  return p;
}

Parameters PassiveSuggestionParameters() {
  Parameters p = DefaultParameters();
  p.card_vertical_padding = 4;
  p.shortcut_body_gap = 3;
  p.value_description_gap = 7;
  return p;
}

TEST(VerticalCandidateLayoutTest,
     CandidatesUseNaturalWidthsAndProceedFromRightToLeft) {
  VerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  layout.Initialize(
      {
          {Size(8, 10), Size(16, 40), Size()},
          {Size(8, 10), Size(20, 60), Size()},
      },
      p);

  EXPECT_EQ(layout.candidate_count(), 2);
  EXPECT_EQ(layout.GetTotalSize().width, 51);
  EXPECT_EQ(layout.GetTotalSize().height, 91);

  // Candidate 0 is rightmost. Width is local to each candidate, with a 1 px
  // physical gap between cards and 2 px breathing room at both popup edges.
  ExpectRect(layout.GetCandidateRect(0), 28, 1, 20, 69);
  ExpectRect(layout.GetCandidateRect(1), 3, 1, 24, 89);

  ExpectRect(layout.GetValueRect(0), 30, 23, 16, 40);
  ExpectRect(layout.GetValueRect(1), 5, 23, 20, 60);
}

TEST(VerticalCandidateLayoutTest,
     CandidateHeightIsNaturalAndSectionsUseLocalGaps) {
  VerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  layout.Initialize(
      {
          {Size(8, 10), Size(14, 30), Size(12, 20)},
          {Size(10, 12), Size(16, 50), Size(18, 8)},
      },
      p);

  ExpectRect(layout.GetCandidateRect(0), 26, 1, 18, 91);
  ExpectRect(layout.GetCandidateRect(1), 3, 1, 22, 101);

  // Each candidate starts its value after its own shortcut height.
  ExpectRect(layout.GetValueRect(0), 28, 23, 14, 30);
  ExpectRect(layout.GetValueRect(1), 6, 25, 16, 50);

  // Description is a vertical tail below the value, separated by the explicit
  // value/description gap.
  ExpectRect(layout.GetDescriptionRect(0), 29, 65, 12, 20);
  ExpectRect(layout.GetDescriptionRect(1), 5, 87, 18, 8);
}

TEST(VerticalCandidateLayoutTest, LongCandidateDoesNotStretchSiblingCard) {
  VerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  layout.Initialize(
      {
          {Size(), Size(16, 100), Size()},
          {Size(), Size(16, 40), Size()},
      },
      p);

  EXPECT_EQ(layout.GetCandidateRect(0).Height(), 114);
  EXPECT_EQ(layout.GetCandidateRect(1).Height(), 54);
  EXPECT_EQ(layout.GetTotalSize().height, 116);
}

TEST(VerticalCandidateLayoutTest,
     SelectionStripeExtendsToBodyEndWithoutChangingNaturalCardHeight) {
  VerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  layout.Initialize(
      {
          {Size(), Size(16, 100), Size()},
          {Size(), Size(16, 40), Size()},
      },
      p);

  // Natural card geometry remains content-sized.
  EXPECT_EQ(layout.GetCandidateRect(0).Height(), 114);
  EXPECT_EQ(layout.GetCandidateRect(1).Height(), 54);

  // Selection is a stable full-height stripe ending immediately before the
  // footer/body boundary for both short and long candidates.
  EXPECT_EQ(layout.GetCandidateSelectionRect(0).Height(), 114);
  EXPECT_EQ(layout.GetCandidateSelectionRect(1).Height(), 114);
  EXPECT_EQ(layout.GetCandidateSelectionRect(0).Width(),
            layout.GetCandidateRect(0).Width());
  EXPECT_EQ(layout.GetCandidateSelectionRect(1).Width(),
            layout.GetCandidateRect(1).Width());
}

TEST(VerticalCandidateLayoutTest,
     FooterMayWidenWindowWithoutMovingCandidateZeroOffRightFlowEdge) {
  VerticalCandidateLayout layout;
  Parameters p = DefaultParameters();
  p.footer_size = Size(100, 20);

  layout.Initialize(
      {
          {Size(8, 10), Size(16, 40), Size()},
          {Size(8, 10), Size(20, 60), Size()},
      },
      p);

  EXPECT_EQ(layout.GetTotalSize().width, 102);
  EXPECT_EQ(layout.GetFooterRect().Width(), 100);

  const Rect candidate_zero = layout.GetCandidateRect(0);
  EXPECT_EQ(candidate_zero.Right(),
            layout.GetTotalSize().width - p.window_border -
                p.cross_axis_edge_padding);
  ExpectRect(layout.GetCandidateRect(0), 79, 1, 20, 69);
  ExpectRect(layout.GetCandidateRect(1), 54, 1, 24, 89);
}

TEST(VerticalCandidateLayoutTest,
     PassiveSuggestionUsesCompactButNonzeroVerticalBreathingRoom) {
  VerticalCandidateLayout layout;
  const Parameters p = PassiveSuggestionParameters();

  layout.Initialize(
      {
          {Size(), Size(20, 53), Size()},
          {Size(), Size(20, 105), Size()},
      },
      p);

  EXPECT_EQ(layout.GetCandidateRect(0).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(1).Width(), 24);
  EXPECT_EQ(layout.GetCandidateRect(0).Height(), 61);
  EXPECT_EQ(layout.GetCandidateRect(1).Height(), 113);
}

TEST(VerticalCandidateLayoutTest,
     InformationMarkerAddsOnlyRequiredTrailingReserveToCompactSuggestion) {
  VerticalCandidateLayout layout;
  const Parameters p = PassiveSuggestionParameters();

  Metrics without_marker{Size(), Size(20, 53), Size()};
  Metrics with_marker{Size(), Size(20, 53), Size()};
  with_marker.has_information_marker = true;

  layout.Initialize({without_marker, with_marker}, p);

  const Rect plain_candidate = layout.GetCandidateRect(0);
  const Rect marked_candidate = layout.GetCandidateRect(1);
  const Rect plain_value = layout.GetValueRect(0);
  const Rect marked_value = layout.GetValueRect(1);

  // Top padding and text placement remain the compact SUGGESTION geometry.
  EXPECT_EQ(plain_value.Top(), marked_value.Top());
  EXPECT_EQ(plain_value.Bottom(), marked_value.Bottom());

  // Only the trailing edge grows from 4 to the 7 DIP marker reserve.
  EXPECT_EQ(plain_candidate.Height(), 61);
  EXPECT_EQ(marked_candidate.Height(), 64);
  EXPECT_EQ(plain_candidate.Bottom() - plain_value.Bottom(), 4);
  EXPECT_EQ(marked_candidate.Bottom() - marked_value.Bottom(), 7);
}

TEST(VerticalCandidateLayoutTest,
     CrossAxisEdgePaddingAndCandidateGapDoNotWidenCandidateCards) {
  VerticalCandidateLayout layout;
  Parameters p = DefaultParameters();
  p.cross_axis_edge_padding = 5;
  p.candidate_gap = 3;

  layout.Initialize(
      {
          {Size(), Size(10, 30), Size()},
          {Size(), Size(10, 30), Size()},
      },
      p);

  EXPECT_EQ(layout.GetCandidateRect(0).Width(), 14);
  EXPECT_EQ(layout.GetCandidateRect(1).Width(), 14);
  EXPECT_EQ(layout.GetTotalSize().width, 43);

  EXPECT_EQ(layout.GetCandidateRect(0).Right() -
                layout.GetCandidateRect(1).Right(),
            17);
}

TEST(VerticalCandidateLayoutTest, EmptyOptionalSectionsDoNotCreateGaps) {
  VerticalCandidateLayout layout;
  const Parameters p = DefaultParameters();

  layout.Initialize({{Size(), Size(16, 40), Size()}}, p);

  ExpectRect(layout.GetCandidateRect(0), 3, 1, 20, 54);
  ExpectRect(layout.GetValueRect(0), 5, 8, 16, 40);
  EXPECT_TRUE(layout.GetShortcutRect(0).IsRectEmpty());
  EXPECT_TRUE(layout.GetDescriptionRect(0).IsRectEmpty());
}

TEST(VerticalCandidateLayoutTest, EmptyCandidateListKeepsFooterGeometryValid) {
  VerticalCandidateLayout layout;
  Parameters p = DefaultParameters();
  p.window_border = 2;
  p.footer_size = Size(50, 18);

  layout.Initialize({}, p);

  EXPECT_EQ(layout.candidate_count(), 0);
  EXPECT_EQ(layout.GetTotalSize().width, 54);
  EXPECT_EQ(layout.GetTotalSize().height, 22);
  ExpectRect(layout.GetFooterRect(), 2, 2, 50, 18);

  EXPECT_TRUE(layout.GetCandidateRect(0).IsRectEmpty());
  EXPECT_TRUE(layout.GetShortcutRect(0).IsRectEmpty());
  EXPECT_TRUE(layout.GetValueRect(0).IsRectEmpty());
  EXPECT_TRUE(layout.GetDescriptionRect(0).IsRectEmpty());
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
