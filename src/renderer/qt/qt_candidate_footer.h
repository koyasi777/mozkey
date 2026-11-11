// Copyright 2026 Mozkey contributors.
// Linux Qt presentation rules shared by the renderer and its unit tests.
#ifndef MOZC_RENDERER_QT_QT_CANDIDATE_FOOTER_H_
#define MOZC_RENDERER_QT_QT_CANDIDATE_FOOTER_H_

#include <string>

#include "protocol/candidate_window.pb.h"

namespace mozc::renderer {

struct QtCandidateFooter {
  bool visible = false;
  bool is_sub_label = false;
  std::string label;
  std::string index;
};

// Suppress the passive "Tab to select" hint exactly as Windows/macOS do.
// In particular, no empty separator/gradient row should remain when a
// non-expanded suggestion has nothing useful to put in its footer.
QtCandidateFooter GetQtCandidateFooter(
    const commands::CandidateWindow& candidate_window);

}  // namespace mozc::renderer
#endif  // MOZC_RENDERER_QT_QT_CANDIDATE_FOOTER_H_
