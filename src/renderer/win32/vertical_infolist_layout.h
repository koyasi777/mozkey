// Copyright 2026, Mozkey authors.
// All rights reserved.

#ifndef MOZC_RENDERER_WIN32_VERTICAL_INFOLIST_LAYOUT_H_
#define MOZC_RENDERER_WIN32_VERTICAL_INFOLIST_LAYOUT_H_

#include <cstddef>
#include <vector>

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace win32 {

// Pure geometry for a native Japanese vertical Infolist.
//
// Visual order is right-to-left:
//   caption | usage 0 | usage 1 | ...
//
// Within each usage, the title is the rightmost text section and the
// description flows into columns on its left. Text measurement/shaping is
// performed by TextRenderer; this class owns only rectangles.
class VerticalInfolistLayout {
 public:
  struct ItemMetrics {
    Size title_size;
    int title_left_padding = 0;
    int title_right_padding = 0;
    Size description_size;
    int description_left_padding = 0;
    int description_right_padding = 0;

    // Inline-axis indent for the description. In vertical writing this moves
    // the description downward, expressing the title -> body hierarchy
    // without inserting artificial spaces into the Japanese text itself.
    int description_top_indent = 0;
  };

  struct Parameters {
    int window_border = 0;

    // Cross-axis padding inside each usage block.
    int row_padding = 0;

    // Inline-axis top/bottom padding. Kept separate from row_padding because
    // the two axes have different typographic roles in vertical writing.
    int vertical_padding = 0;

    // Cross-axis gap between the title column and description columns.
    int section_gap = 0;

    int caption_width = 0;
    int window_height = 0;
  };

  VerticalInfolistLayout() = default;

  void Layout(const std::vector<ItemMetrics>& items,
              const Parameters& parameters);

  Size window_size() const { return window_size_; }
  Rect caption_rect() const { return caption_rect_; }

  size_t item_count() const { return item_rects_.size(); }
  Rect GetItemRect(size_t index) const;
  Rect GetTitleRect(size_t index) const;
  Rect GetDescriptionRect(size_t index) const;

 private:
  Size window_size_;
  Rect caption_rect_;
  std::vector<Rect> item_rects_;
  std::vector<Rect> title_rects_;
  std::vector<Rect> description_rects_;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_VERTICAL_INFOLIST_LAYOUT_H_