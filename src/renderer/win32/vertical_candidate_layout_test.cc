#include "renderer/win32/vertical_candidate_layout.h"

#include <vector>

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

VerticalCandidateLayout::CandidateMetrics Metrics(
    const Size& shortcut_size, const Size& value_size,
    const Size& description_size) {
  return VerticalCandidateLayout::CandidateMetrics{
      shortcut_size, value_size, description_size};
}

TEST(VerticalCandidateLayoutTest, CandidatesProceedFromRightToLeft) {
  VerticalCandidateLayout layout;
  VerticalCandidateLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.column_padding = 2;
  parameters.vertical_padding = 3;
  parameters.section_gap = 4;

  std::vector<VerticalCandidateLayout::CandidateMetrics> metrics = {
      Metrics(Size(8, 10), Size(16, 40), Size()),
      Metrics(Size(8, 10), Size(20, 60), Size()),
  };

  layout.Initialize(metrics, parameters);

  EXPECT_EQ(layout.candidate_count(), 2);
  EXPECT_EQ(layout.GetTotalSize().width, 46);
  EXPECT_EQ(layout.GetTotalSize().height, 82);

  // Candidate 0 is the rightmost column; candidate 1 is immediately to its
  // left.  Candidate order itself is not reversed.
  ExpectRect(layout.GetCandidateRect(0), 25, 1, 20, 80);
  ExpectRect(layout.GetCandidateRect(1), 1, 1, 24, 80);

  // Candidate text starts at the same vertical position even when the
  // candidates have different lengths.
  ExpectRect(layout.GetValueRect(0), 27, 18, 16, 40);
  ExpectRect(layout.GetValueRect(1), 3, 18, 20, 60);
}

TEST(VerticalCandidateLayoutTest, SectionsUseSharedVerticalZones) {
  VerticalCandidateLayout layout;
  VerticalCandidateLayout::Parameters parameters;
  parameters.window_border = 2;
  parameters.column_padding = 3;
  parameters.vertical_padding = 4;
  parameters.section_gap = 5;

  std::vector<VerticalCandidateLayout::CandidateMetrics> metrics = {
      Metrics(Size(8, 10), Size(14, 30), Size(12, 20)),
      Metrics(Size(10, 12), Size(16, 50), Size(18, 8)),
  };

  layout.Initialize(metrics, parameters);

  // shortcut zone = 12, value zone = 50, description zone = 20.
  // body height = 2*4 + 12 + 5 + 50 + 5 + 20 = 100.
  ExpectRect(layout.GetCandidateRect(0), 26, 2, 20, 100);
  ExpectRect(layout.GetCandidateRect(1), 2, 2, 24, 100);

  // All value strings share the same top regardless of shortcut height.
  ExpectRect(layout.GetValueRect(0), 29, 23, 14, 30);
  ExpectRect(layout.GetValueRect(1), 6, 23, 16, 50);

  // Description starts after the tallest value zone, not after each
  // candidate's individual value height.
  ExpectRect(layout.GetDescriptionRect(0), 30, 78, 12, 20);
  ExpectRect(layout.GetDescriptionRect(1), 5, 78, 18, 8);
}

TEST(VerticalCandidateLayoutTest, FooterCanWidenWindowAndBodyStaysRightAligned) {
  VerticalCandidateLayout layout;
  VerticalCandidateLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.column_padding = 2;
  parameters.vertical_padding = 3;
  parameters.section_gap = 4;
  parameters.footer_size = Size(100, 20);

  std::vector<VerticalCandidateLayout::CandidateMetrics> metrics = {
      Metrics(Size(8, 10), Size(16, 40), Size()),
      Metrics(Size(8, 10), Size(20, 60), Size()),
  };

  layout.Initialize(metrics, parameters);

  EXPECT_EQ(layout.GetTotalSize().width, 102);
  EXPECT_EQ(layout.GetTotalSize().height, 102);

  // A wide horizontal footer may enlarge the window, but the vertical
  // candidate body remains attached to the right edge so candidate 0 stays
  // nearest the composition text.
  ExpectRect(layout.GetCandidateRect(0), 81, 1, 20, 80);
  ExpectRect(layout.GetCandidateRect(1), 57, 1, 24, 80);
  ExpectRect(layout.GetFooterRect(), 1, 81, 100, 20);
}

TEST(VerticalCandidateLayoutTest, EmptyOptionalSectionsDoNotCreateGaps) {
  VerticalCandidateLayout layout;
  VerticalCandidateLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.column_padding = 2;
  parameters.vertical_padding = 3;
  parameters.section_gap = 9;

  std::vector<VerticalCandidateLayout::CandidateMetrics> metrics = {
      Metrics(Size(), Size(16, 40), Size()),
  };

  layout.Initialize(metrics, parameters);

  // No shortcut/description zones means no section gaps are inserted.
  ExpectRect(layout.GetCandidateRect(0), 1, 1, 20, 46);
  ExpectRect(layout.GetValueRect(0), 3, 4, 16, 40);
}

TEST(VerticalCandidateLayoutTest,
     CrossAxisEdgePaddingAddsMarginsWithoutWideningCandidateColumn) {
  VerticalCandidateLayout layout;
  VerticalCandidateLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.column_padding = 2;
  parameters.vertical_padding = 3;
  parameters.cross_axis_edge_padding = 5;

  std::vector<VerticalCandidateLayout::CandidateMetrics> metrics = {
      Metrics(Size(), Size(10, 30), Size()),
  };

  layout.Initialize(metrics, parameters);

  // Candidate column width remains 10 + 2 * 2 = 14.  Only the physical
  // left/right edges gain 5 px each.
  EXPECT_EQ(layout.GetTotalSize().width, 26);
  EXPECT_EQ(layout.GetTotalSize().height, 38);
  ExpectRect(layout.GetCandidateRect(0), 6, 1, 14, 36);
  ExpectRect(layout.GetValueRect(0), 8, 4, 10, 30);
}

TEST(VerticalCandidateLayoutTest, EmptyCandidateListKeepsFooterGeometryValid) {
  VerticalCandidateLayout layout;
  VerticalCandidateLayout::Parameters parameters;
  parameters.window_border = 2;
  parameters.footer_size = Size(50, 18);

  layout.Initialize({}, parameters);

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
