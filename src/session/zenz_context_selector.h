#ifndef MOZC_SESSION_ZENZ_CONTEXT_SELECTOR_H_
#define MOZC_SESSION_ZENZ_CONTEXT_SELECTOR_H_

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"

namespace mozc {
namespace session {

// Selects linguistically useful surrounding text before privacy/language
// sanitization.
//
// C4B introduces this as an isolated candidate component. Production
// ZenzContextAssembler is intentionally not connected to it until a later
// phase.
//
// The two directions are deliberately asymmetric:
//
// Left:
//   * a blank line is a strong discourse/paragraph boundary;
//   * a single line break is soft and may be crossed;
//   * sentence-ending punctuation is not itself a hard boundary because
//     preceding discourse is useful for Japanese topic/anaphora resolution;
//   * the final character budget is taken from the right edge.
//
// Right:
//   * an immediate line break remains a conservative boundary;
//   * a blank line is a strong boundary;
//   * a single internal line break may be crossed;
//   * the first sentence terminator ends the context;
//   * closing quotation/bracket characters immediately after the terminator
//     are retained;
//   * the final character budget is taken from the left edge.
class ZenzContextSelector {
 public:
  std::string SelectLeft(
      absl::string_view text,
      size_t max_chars) const;

  std::string SelectRight(
      absl::string_view text,
      size_t max_chars) const;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CONTEXT_SELECTOR_H_