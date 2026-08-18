#ifndef MOZC_RENDERER_WIN32_VERTICAL_CANDIDATE_LAYOUT_H_
#define MOZC_RENDERER_WIN32_VERTICAL_CANDIDATE_LAYOUT_H_

#include <cstddef>
#include <vector>

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace win32 {

// Computes the geometry of an MS-IME-style vertical candidate body.
//
// Candidate order is preserved semantically: candidate index 0 remains the
// first candidate.  Only its visual placement changes.  Candidate 0 occupies
// the rightmost column and subsequent candidates proceed to the left.
//
// This class is intentionally renderer-independent.  Text measurement and
// drawing are handled by CandidateWindow/TextRenderer; this class only turns
// measured sizes into rectangles.
class VerticalCandidateLayout {
 public:
  struct CandidateMetrics {
    Size shortcut_size;
    Size value_size;
    Size description_size;
  };

  struct Parameters {
    int window_border = 0;
    int column_padding = 0;
    int vertical_padding = 0;
    int section_gap = 0;
    Size footer_size;
  };

  VerticalCandidateLayout() = default;
  VerticalCandidateLayout(const VerticalCandidateLayout&) = delete;
  VerticalCandidateLayout& operator=(const VerticalCandidateLayout&) = delete;

  void Initialize(const std::vector<CandidateMetrics>& candidates,
                  const Parameters& parameters);

  size_t candidate_count() const { return candidates_.size(); }
  Size GetTotalSize() const { return total_size_; }

  // Returns the complete selectable column for the candidate.
  Rect GetCandidateRect(size_t index) const;

  // Returns the actual content rectangle for each section.  Empty sections
  // return an empty rectangle at the section origin.
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
