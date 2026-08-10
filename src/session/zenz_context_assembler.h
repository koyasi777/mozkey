#ifndef MOZC_SESSION_ZENZ_CONTEXT_ASSEMBLER_H_
#define MOZC_SESSION_ZENZ_CONTEXT_ASSEMBLER_H_

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"
#include "session/zenz_context_sanitizer.h"
#include "session/zenz_context_selector.h"

namespace mozc {
namespace session {

// Result for one side of the surrounding context.
//
// |prompt_context| contains only text which passed the Zenz context sanitizer.
// Raw surrounding text is intentionally not retained here.
struct ZenzContextAssemblySide {
  std::string prompt_context;
  std::string context_class;
  bool allowed_for_prompt = false;
  bool allowed_for_learning = false;
  std::string reason;
};

struct ZenzContextAssemblyInput {
  absl::string_view preceding_text;
  absl::string_view following_text;
  size_t left_max_chars = 0;
  size_t right_max_chars = 0;
};

struct ZenzContextAssemblyResult {
  ZenzContextAssemblySide left;
  ZenzContextAssemblySide right;
};

// Selects and sanitizes surrounding context for Zenz.
//
// Directional selection is intentionally performed before privacy/language
// sanitization:
//
//   * left context preserves useful discourse within the current paragraph;
//   * right context prioritizes the immediate syntactic continuation and ends
//     at a strong paragraph boundary or the first sentence boundary;
//   * both directions preserve Unicode character budgets;
//   * only the selected text is passed to ZenzContextSanitizer.
//
// This keeps structural selection separate from privacy and script policy.
class ZenzContextAssembler {
 public:
  ZenzContextAssemblyResult Assemble(
      const ZenzContextAssemblyInput& input) const;

 private:
  static ZenzContextAssemblySide ToAssemblySide(
      const ZenzContextSanitizationResult& result);

  ZenzContextSelector selector_;
  ZenzContextSanitizer sanitizer_;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CONTEXT_ASSEMBLER_H_