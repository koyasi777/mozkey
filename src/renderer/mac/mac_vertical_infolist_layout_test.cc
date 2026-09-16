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

#include "renderer/mac/mac_vertical_infolist_layout.h"

#include <vector>

#include "base/coordinates.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace mac {
namespace {

TEST(MacVerticalInfolistLayoutTest, UsageOrderAdvancesRightToLeft) {
  MacVerticalInfolistLayout layout;
  MacVerticalInfolistLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.row_padding = 2;
  parameters.caption_width = 10;
  parameters.window_height = 100;

  std::vector<MacVerticalInfolistLayout::ItemMetrics> items(2);
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

TEST(MacVerticalInfolistLayoutTest, TitleIsRightOfDescription) {
  MacVerticalInfolistLayout layout;
  MacVerticalInfolistLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.row_padding = 2;
  parameters.caption_width = 12;
  parameters.window_height = 120;

  MacVerticalInfolistLayout::ItemMetrics item;
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

TEST(MacVerticalInfolistLayoutTest, DescriptionIsIndentedAndSeparatedFromTitle) {
  MacVerticalInfolistLayout layout;
  MacVerticalInfolistLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.row_padding = 3;
  parameters.vertical_padding = 8;
  parameters.section_gap = 5;
  parameters.caption_width = 10;
  parameters.window_height = 120;

  MacVerticalInfolistLayout::ItemMetrics item;
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

TEST(MacVerticalInfolistLayoutTest, EmptyTitleDoesNotCreateSectionGap) {
  MacVerticalInfolistLayout layout;
  MacVerticalInfolistLayout::Parameters parameters;
  parameters.window_border = 1;
  parameters.row_padding = 3;
  parameters.vertical_padding = 8;
  parameters.section_gap = 7;
  parameters.window_height = 120;

  MacVerticalInfolistLayout::ItemMetrics item;
  item.description_size = Size(28, 80);
  item.description_top_indent = 0;

  layout.Layout({item}, parameters);

  EXPECT_TRUE(layout.GetTitleRect(0).IsRectEmpty());
  EXPECT_EQ(layout.GetDescriptionRect(0).Top(), 9);
  EXPECT_EQ(layout.GetDescriptionRect(0).Right(),
            layout.GetItemRect(0).Right() - 3);
}

TEST(MacVerticalInfolistLayoutTest, EmptyIndexIsSafe) {
  MacVerticalInfolistLayout layout;
  layout.Layout({}, MacVerticalInfolistLayout::Parameters());

  EXPECT_TRUE(layout.GetItemRect(0).IsRectEmpty());
  EXPECT_TRUE(layout.GetTitleRect(0).IsRectEmpty());
  EXPECT_TRUE(layout.GetDescriptionRect(0).IsRectEmpty());
}

}  // namespace
}  // namespace mac
}  // namespace renderer
}  // namespace mozc
