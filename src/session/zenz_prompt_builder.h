#ifndef MOZC_SESSION_ZENZ_PROMPT_BUILDER_H_
#define MOZC_SESSION_ZENZ_PROMPT_BUILDER_H_

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"

namespace mozc {
namespace session {

struct ZenzPromptOptions {
  std::string left_context;
  std::string right_context;
  std::string profile;
  std::string topic;
  std::string style;
  std::string settings;
};

class ZenzPromptBuilder {
 public:
  ZenzPromptBuilder() = default;

  std::string HiraganaToKatakana(absl::string_view input) const;

  std::string Build(absl::string_view left_context,
                    absl::string_view reading_hiragana) const;
  std::string Build(absl::string_view reading_hiragana,
                    const ZenzPromptOptions& options) const;

 private:
  static bool DecodeOneUtf8(absl::string_view input, size_t* index,
                            char32_t* codepoint);
  static void AppendUtf8(char32_t codepoint, std::string* output);
  static bool IsZenzControlCodepoint(char32_t codepoint);
  static bool IsUnsafeControlCodepoint(char32_t codepoint);
  static std::string SanitizeConditionText(absl::string_view input,
                                           size_t max_chars);
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_PROMPT_BUILDER_H_
