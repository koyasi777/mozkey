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

#ifndef MOZC_RENDERER_MAC_MAC_VERTICAL_CANDIDATE_LAYOUT_H_
#define MOZC_RENDERER_MAC_MAC_VERTICAL_CANDIDATE_LAYOUT_H_

#include <cstddef>
#include <vector>

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace mac {

// Pure physical geometry for the native macOS vertical candidate window.
//
// Semantic candidate order is preserved: candidate index 0 is physically
// rightmost and later candidates proceed to the left.
//
// The candidate value is the primary vertical text. annotation.description is
// a smaller secondary vertical tail below the value in the same physical
// column. The two strings are stacked on the inline axis, never placed in
// parallel side-by-side columns. This follows native vertical Japanese IME
// behavior while keeping every candidate independently sized.
//
// Text shaping, Core Text, Cocoa drawing, colors, and writing-direction
// detection are deliberately outside this class.
class MacVerticalCandidateLayout {
 public:
  struct CandidateMetrics {
    Size shortcut_size;
    Size value_size;
    Size description_size;
    Size information_marker_size;
  };

  struct Parameters {
    int window_border = 0;
    int cross_axis_edge_padding = 0;
    int candidate_gap = 0;
    int card_horizontal_padding = 0;
    int card_vertical_padding = 0;
    int shortcut_body_gap = 0;
    int value_description_gap = 0;
    int information_marker_gap = 0;
    Size footer_size;
  };

  MacVerticalCandidateLayout() = default;
  MacVerticalCandidateLayout(const MacVerticalCandidateLayout&) = delete;
  MacVerticalCandidateLayout& operator=(const MacVerticalCandidateLayout&) =
      delete;

  void Initialize(const std::vector<CandidateMetrics>& candidates,
                  const Parameters& parameters);

  size_t candidate_count() const { return candidates_.size(); }

  Size GetTotalSize() const { return total_size_; }
  Rect GetFooterRect() const { return footer_rect_; }

  Rect GetCandidateRect(size_t index) const;
  Rect GetShortcutRect(size_t index) const;
  Rect GetValueRect(size_t index) const;
  Rect GetDescriptionRect(size_t index) const;
  Rect GetInformationMarkerRect(size_t index) const;

 private:
  struct CandidateGeometry {
    Rect candidate_rect;
    Rect shortcut_rect;
    Rect value_rect;
    Rect description_rect;
    Rect information_marker_rect;
  };

  std::vector<CandidateGeometry> candidates_;
  Size total_size_;
  Rect footer_rect_;
};

}  // namespace mac
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_MAC_MAC_VERTICAL_CANDIDATE_LAYOUT_H_
