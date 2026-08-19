#include "renderer/win32/vertical_infolist_layout.h"

#include <vector>

#include "base/coordinates.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

TEST(VerticalInfolistLayoutTest, UsageOrderAdvancesRightToLeft) {
  VerticalInfolistLayout layout;
  VerticalInfolistLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.row_padding = 2;
  parameters.caption_width = 10;
  parameters.window_height = 100;

  std::vector<VerticalInfolistLayout::ItemMetrics> items(2);
  items[0].title_size = Size(10, 60);
  items[0].description_size = Size(20, 80);
  items[1].title_size = Size(12, 60);
  items[1].description_size = Size(24, 80);

  layout.Layout(items, parameters);

  ASSERT_EQ(layout.item_count(), 2);
  EXPECT_EQ(layout.GetItemRect(0).Right(), layout.caption_rect().Left());
  EXPECT_EQ(layout.GetItemRect(1).Right(), layout.GetItemRect(0).Left());
  EXPECT_LT(layout.GetItemRect(1).Left(), layout.GetItemRect(0).Left());
  EXPECT_EQ(layout.window_size().height, 100);
}

TEST(VerticalInfolistLayoutTest, TitleIsRightOfDescription) {
  VerticalInfolistLayout layout;
  VerticalInfolistLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.row_padding = 2;
  parameters.caption_width = 12;
  parameters.window_height = 120;

  VerticalInfolistLayout::ItemMetrics item;
  item.title_size = Size(14, 80);
  item.title_left_padding = 1;
  item.title_right_padding = 2;
  item.description_size = Size(42, 100);
  item.description_left_padding = 3;
  item.description_right_padding = 4;

  layout.Layout({item}, parameters);

  const Rect title = layout.GetTitleRect(0);
  const Rect description = layout.GetDescriptionRect(0);
  EXPECT_GT(title.Left(), description.Left());
  EXPECT_LE(description.Right(), title.Left());
  EXPECT_EQ(title.Top(), description.Top());
  EXPECT_EQ(title.Height(), description.Height());
}

TEST(VerticalInfolistLayoutTest, DescriptionIsIndentedAndSeparatedFromTitle) {
  VerticalInfolistLayout layout;
  VerticalInfolistLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.row_padding = 3;
  parameters.vertical_padding = 8;
  parameters.section_gap = 5;
  parameters.caption_width = 10;
  parameters.window_height = 120;

  VerticalInfolistLayout::ItemMetrics item;
  item.title_size = Size(14, 80);
  item.description_size = Size(28, 80);
  item.description_top_indent = 16;

  layout.Layout({item}, parameters);

  const Rect title = layout.GetTitleRect(0);
  const Rect description = layout.GetDescriptionRect(0);

  EXPECT_EQ(title.Top(), 9);
  EXPECT_EQ(description.Top(), title.Top() + 16);
  EXPECT_EQ(title.Left() - description.Right(), 5);
  EXPECT_EQ(description.Height(), title.Height() - 16);
}

TEST(VerticalInfolistLayoutTest, EmptyTitleDoesNotCreateSectionGap) {
  VerticalInfolistLayout layout;
  VerticalInfolistLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.row_padding = 3;
  parameters.vertical_padding = 8;
  parameters.section_gap = 7;
  parameters.window_height = 120;

  VerticalInfolistLayout::ItemMetrics item;
  item.description_size = Size(28, 80);
  item.description_top_indent = 0;

  layout.Layout({item}, parameters);

  EXPECT_TRUE(layout.GetTitleRect(0).IsRectEmpty());
  EXPECT_EQ(layout.GetDescriptionRect(0).Top(), 9);
  EXPECT_EQ(layout.GetDescriptionRect(0).Right(),
            layout.GetItemRect(0).Right() - 3);
}

TEST(VerticalInfolistLayoutTest, EmptyIndexIsSafe) {
  VerticalInfolistLayout layout;
  layout.Layout({}, VerticalInfolistLayout::Parameters());

  EXPECT_TRUE(layout.GetItemRect(0).IsRectEmpty());
  EXPECT_TRUE(layout.GetTitleRect(0).IsRectEmpty());
  EXPECT_TRUE(layout.GetDescriptionRect(0).IsRectEmpty());
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc