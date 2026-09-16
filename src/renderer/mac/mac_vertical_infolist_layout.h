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

#ifndef MOZC_RENDERER_MAC_MAC_VERTICAL_INFOLIST_LAYOUT_H_
#define MOZC_RENDERER_MAC_MAC_VERTICAL_INFOLIST_LAYOUT_H_

#include <cstddef>
#include <vector>

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace mac {

// Pure geometry for the native macOS Japanese vertical Infolist.
//
// Visual order is right-to-left:
//   caption | usage 0 | usage 1 | ...
//
// Within each usage, the title is the rightmost text section and the
// description flows into columns on its left. Text measurement/shaping is
// performed by the caller (Core Text on macOS); this class owns rectangles
// only.
class MacVerticalInfolistLayout {
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

  MacVerticalInfolistLayout() = default;

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

}  // namespace mac
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_MAC_MAC_VERTICAL_INFOLIST_LAYOUT_H_
