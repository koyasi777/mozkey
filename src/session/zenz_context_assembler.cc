#include "session/zenz_context_assembler.h"

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"
#include "base/util.h"

namespace mozc {
namespace session {

std::string ZenzContextAssembler::SelectLeftContext(
    const absl::string_view text,
    const size_t max_chars) {
  if (max_chars == 0 || text.empty()) {
    return "";
  }

  const size_t len = Util::CharsLen(text);
  if (len <= max_chars) {
    return std::string(text);
  }

  return std::string(
      Util::Utf8SubString(text, len - max_chars, max_chars));
}

std::string ZenzContextAssembler::SelectRightContext(
    const absl::string_view text,
    const size_t max_chars) {
  if (max_chars == 0 || text.empty()) {
    return "";
  }

  // Preserve the existing Session behavior exactly. Right context describes
  // only the continuation of the current line.
  const size_t line_break_pos = text.find_first_of("\r\n");
  const absl::string_view current_line =
      line_break_pos == absl::string_view::npos
          ? text
          : text.substr(0, line_break_pos);

  const size_t len = Util::CharsLen(current_line);
  if (len <= max_chars) {
    return std::string(current_line);
  }

  return std::string(
      Util::Utf8SubString(current_line, 0, max_chars));
}

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
      SelectLeftContext(
          input.preceding_text,
          input.left_max_chars);

  result.left = ToAssemblySide(
      sanitizer_.SanitizeForZenz(
          selected_left,
          input.left_max_chars));

  const std::string selected_right =
      SelectRightContext(
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