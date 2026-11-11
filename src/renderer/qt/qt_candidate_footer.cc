// Copyright 2026 Mozkey contributors.
#include "renderer/qt/qt_candidate_footer.h"

#include <string>

#include "absl/strings/str_cat.h"

namespace mozc::renderer {
namespace {
constexpr char kPassiveSuggestionHint[] = "Tabキーで選択";
}

QtCandidateFooter GetQtCandidateFooter(
    const commands::CandidateWindow& candidate_window) {
  QtCandidateFooter result;
  if (!candidate_window.has_footer()) {
    return result;
  }
  const commands::Footer& footer = candidate_window.footer();
  if (footer.has_label() && !footer.label().empty() &&
      footer.label() != kPassiveSuggestionHint) {
    result.label = footer.label();
  } else if (footer.has_sub_label() && !footer.sub_label().empty() &&
             footer.sub_label() != kPassiveSuggestionHint) {
    result.label = footer.sub_label();
    result.is_sub_label = true;
  }
  // Do not print 0/N for a passive suggestion without a focused candidate.
  if (footer.index_visible() && candidate_window.has_focused_index() &&
      candidate_window.size() > 0 &&
      candidate_window.focused_index() < candidate_window.size()) {
    result.index = absl::StrCat(candidate_window.focused_index() + 1, "/",
                                candidate_window.size());
  }
  result.visible = !result.label.empty() || !result.index.empty();
  return result;
}

}  // namespace mozc::renderer
