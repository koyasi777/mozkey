// Copyright 2026, Mozkey authors.
// All rights reserved.

#include "renderer/win32/vertical_infolist_layout.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

int SectionWidth(const Size& size, int left_padding, int right_padding) {
  if (size.width <= 0 || size.height <= 0) {
    return 0;
  }
  return std::max(0, left_padding) + size.width +
         std::max(0, right_padding);
}

}  // namespace

void VerticalInfolistLayout::Layout(
    const std::vector<ItemMetrics>& items, const Parameters& parameters) {
  item_rects_.clear();
  title_rects_.clear();
  description_rects_.clear();
  caption_rect_ = Rect();

  const int border = std::max(0, parameters.window_border);
  const int row_padding = std::max(0, parameters.row_padding);
  const int vertical_padding = std::max(0, parameters.vertical_padding);
  const int section_gap = std::max(0, parameters.section_gap);
  const int caption_width = std::max(0, parameters.caption_width);
  const int window_height = std::max(
      parameters.window_height, border * 2 + vertical_padding * 2 + 1);
  const int content_height = std::max(1, window_height - border * 2);
  const int text_height =
      std::max(1, content_height - vertical_padding * 2);

  std::vector<int> item_widths;
  item_widths.reserve(items.size());

  int body_width = 0;
  for (const ItemMetrics& item : items) {
    const int title_width =
        SectionWidth(item.title_size, item.title_left_padding,
                     item.title_right_padding);
    const int description_width =
        SectionWidth(item.description_size, item.description_left_padding,
                     item.description_right_padding);
    const bool has_title = title_width > 0;
    const bool has_description = description_width > 0;
    const int title_description_gap =
        has_title && has_description ? section_gap : 0;
    const int item_width = std::max(
        1, row_padding * 2 + title_width + title_description_gap +
               description_width);
    item_widths.push_back(item_width);
    body_width += item_width;
  }

  const int window_width =
      std::max(1, border * 2 + caption_width + body_width);
  window_size_ = Size(window_width, window_height);

  if (caption_width > 0) {
    caption_rect_ =
        Rect(window_width - border - caption_width, border, caption_width,
             content_height);
  }

  int right = window_width - border - caption_width;
  item_rects_.reserve(items.size());
  title_rects_.reserve(items.size());
  description_rects_.reserve(items.size());

  for (size_t i = 0; i < items.size(); ++i) {
    const ItemMetrics& item = items[i];
    const int item_width = item_widths[i];
    const Rect item_rect(right - item_width, border, item_width, content_height);
    item_rects_.push_back(item_rect);

    int section_right = item_rect.Right() - row_padding;
    const int text_top = item_rect.Top() + vertical_padding;

    const int title_left_padding = std::max(0, item.title_left_padding);
    const int title_right_padding = std::max(0, item.title_right_padding);
    const int title_section_width =
        SectionWidth(item.title_size, title_left_padding, title_right_padding);
    if (title_section_width > 0) {
      const int section_left = section_right - title_section_width;
      title_rects_.emplace_back(section_left + title_left_padding, text_top,
                                item.title_size.width, text_height);
      section_right = section_left;
    } else {
      title_rects_.emplace_back();
    }

    const int description_left_padding =
        std::max(0, item.description_left_padding);
    const int description_right_padding =
        std::max(0, item.description_right_padding);
    const int description_section_width =
        SectionWidth(item.description_size, description_left_padding,
                     item.description_right_padding);
    if (description_section_width > 0) {
      if (title_section_width > 0) {
        section_right -= section_gap;
      }
      const int description_indent = std::clamp(
          item.description_top_indent, 0, std::max(0, text_height - 1));
      const int description_height =
          std::max(1, text_height - description_indent);
      const int section_left = section_right - description_section_width;
      description_rects_.emplace_back(
          section_left + description_left_padding,
          text_top + description_indent, item.description_size.width,
          description_height);
    } else {
      description_rects_.emplace_back();
    }

    right = item_rect.Left();
  }
}

Rect VerticalInfolistLayout::GetItemRect(size_t index) const {
  return index < item_rects_.size() ? item_rects_[index] : Rect();
}

Rect VerticalInfolistLayout::GetTitleRect(size_t index) const {
  return index < title_rects_.size() ? title_rects_[index] : Rect();
}

Rect VerticalInfolistLayout::GetDescriptionRect(size_t index) const {
  return index < description_rects_.size() ? description_rects_[index] : Rect();
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc