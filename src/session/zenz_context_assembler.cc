#include "session/zenz_context_assembler.h"

#include <string>

namespace mozc {
namespace session {

ZenzContextAssemblySide ZenzContextAssembler::ToAssemblySide(
    const ZenzContextSanitizationResult& result) {
  ZenzContextAssemblySide side;

  side.prompt_context =
      result.allowed_for_prompt
          ? result.sanitized_context
          : std::string();

  side.context_class = result.context_class;
  side.allowed_for_prompt = result.allowed_for_prompt;
  side.allowed_for_learning = result.allowed_for_learning;
  side.reason = result.reason;

  return side;
}

ZenzContextAssemblyResult ZenzContextAssembler::Assemble(
    const ZenzContextAssemblyInput& input) const {
  ZenzContextAssemblyResult result;

  const std::string selected_left =
      selector_.SelectLeft(
          input.preceding_text,
          input.left_max_chars);

  result.left = ToAssemblySide(
      sanitizer_.SanitizeForZenz(
          selected_left,
          input.left_max_chars));

  const std::string selected_right =
      selector_.SelectRight(
          input.following_text,
          input.right_max_chars);

  result.right = ToAssemblySide(
      sanitizer_.SanitizeForZenz(
          selected_right,
          input.right_max_chars));

  return result;
}

}  // namespace session
}  // namespace mozc
