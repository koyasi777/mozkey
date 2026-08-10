#ifndef MOZC_SESSION_ZENZ_CONTEXT_ASSEMBLER_H_
#define MOZC_SESSION_ZENZ_CONTEXT_ASSEMBLER_H_

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"
#include "session/zenz_context_sanitizer.h"

namespace mozc {
namespace session {

// Result for one side of the surrounding context.
//
// |prompt_context| contains only text which passed the existing Zenz context
// sanitizer. Raw surrounding text is intentionally not retained here.
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
// Phase B intentionally preserves the pre-existing Session semantics:
//   * left context: trailing |left_max_chars| Unicode characters;
//   * right context: current line only, then leading
//     |right_max_chars| Unicode characters;
//   * privacy/classification: existing ZenzContextSanitizer behavior.
//
// Structural or linguistic policy improvements belong to later phases.
class ZenzContextAssembler {
 public:
  ZenzContextAssemblyResult Assemble(
      const ZenzContextAssemblyInput& input) const;

 private:
  static std::string SelectLeftContext(
      absl::string_view text,
      size_t max_chars);

  static std::string SelectRightContext(
      absl::string_view text,
      size_t max_chars);

  static ZenzContextAssemblySide ToAssemblySide(
      const ZenzContextSanitizationResult& result);

  ZenzContextSanitizer sanitizer_;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CONTEXT_ASSEMBLER_H_