#ifndef MOZC_RENDERER_WIN32_VERTICAL_CANDIDATE_LAYOUT_H_
#define MOZC_RENDERER_WIN32_VERTICAL_CANDIDATE_LAYOUT_H_

#include <cstddef>
#include <vector>

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace win32 {

// Pure physical geometry for the native Windows vertical candidate window.
//
// Semantic candidate order is preserved: candidate index 0 is physically
// rightmost and later candidates proceed to the left.
//
// Each candidate is a natural-size vertical card.  Shortcut, value, and
// description are stacked on the inline axis inside that card.  A long
// candidate therefore changes its own height and the window height, but does
// not stretch the selectable rectangles of its siblings.
//
// Text measurement, GDI drawing, colors, and writing-direction detection are
// deliberately outside this class.
class VerticalCandidateLayout {
 public:
  struct CandidateMetrics {
    Size shortcut_size;
    Size value_size;
    Size description_size;
    bool has_information_marker = false;
  };

  struct Parameters {
    int window_border = 0;
    int cross_axis_edge_padding = 0;
    int candidate_gap = 0;
    int card_horizontal_padding = 0;
    int card_vertical_padding = 0;
    int shortcut_body_gap = 0;
    int value_description_gap = 0;
    // Minimum trailing space from the final text glyph to the card bottom
    // when DrawInformationIcon needs to occupy the tail of this candidate.
    int information_marker_tail_reserve = 0;
    Size footer_size;
  };

  VerticalCandidateLayout() = default;
  VerticalCandidateLayout(const VerticalCandidateLayout&) = delete;
  VerticalCandidateLayout& operator=(const VerticalCandidateLayout&) = delete;

  void Initialize(const std::vector<CandidateMetrics>& candidates,
                  const Parameters& parameters);

  size_t candidate_count() const { return candidates_.size(); }
  Size GetTotalSize() const { return total_size_; }

  // Returns the natural-size candidate card used for content layout and
  // hit-testing.
  Rect GetCandidateRect(size_t index) const;

  // Returns the visual selection stripe for a candidate.  It keeps the
  // candidate's natural width but extends to the bottom of the candidate
  // body so focus does not visually shrink or grow with the text length.
  Rect GetCandidateSelectionRect(size_t index) const;

  Rect GetShortcutRect(size_t index) const;
  Rect GetValueRect(size_t index) const;
  Rect GetDescriptionRect(size_t index) const;

  Rect GetFooterRect() const { return footer_rect_; }

 private:
  struct CandidateGeometry {
    Rect candidate_rect;
    Rect shortcut_rect;
    Rect value_rect;
    Rect description_rect;
  };

  std::vector<CandidateGeometry> candidates_;
  Size total_size_;
  Rect footer_rect_;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_VERTICAL_CANDIDATE_LAYOUT_H_
