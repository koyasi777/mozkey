#ifndef MOZC_SESSION_ZENZ_CONTEXT_SANITIZER_H_
#define MOZC_SESSION_ZENZ_CONTEXT_SANITIZER_H_

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"
#include "session/zenz_context_script_analyzer.h"
#include "session/zenz_text_privacy_analyzer.h"

namespace mozc {
namespace session {

struct ZenzContextSanitizationResult {
  std::string sanitized_context;
  std::string context_class;
  bool allowed_for_prompt = false;
  bool allowed_for_learning = false;
  std::string reason;
};

class ZenzContextSanitizer {
 public:
  ZenzContextSanitizationResult SanitizeForZenz(
      absl::string_view raw_context,
      size_t max_chars) const;

 private:
  ZenzContextScriptAnalyzer script_analyzer_;
  ZenzTextPrivacyAnalyzer privacy_analyzer_;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_CONTEXT_SANITIZER_H_
