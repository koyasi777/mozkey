#ifndef MOZC_SESSION_ZENZ_TEXT_PRIVACY_ANALYZER_H_
#define MOZC_SESSION_ZENZ_TEXT_PRIVACY_ANALYZER_H_

#include "absl/strings/string_view.h"

namespace mozc {
namespace session {

// Privacy policies remain separate because live text and surrounding context
// have different compatibility and sensitivity requirements.
//
// kLiveText preserves the existing Session key/value privacy policy.
//
// kContext is the surrounding-context policy. It protects structured
// sensitive data instead of treating ordinary short technical text as secret.
enum class ZenzTextPrivacyPolicy {
  kLiveText,
  kContext,
};

enum class ZenzTextPrivacySignal {
  kNone,
  kEmailLike,
  kUrlOrDomainLike,
  kPathLike,
  kSecretPrefix,
  kTokenLike,
  kCredentialWord,
};

struct ZenzTextPrivacyAnalysis {
  ZenzTextPrivacySignal signal =
      ZenzTextPrivacySignal::kNone;

  bool sensitive() const {
    return signal !=
           ZenzTextPrivacySignal::kNone;
  }

  const char* reason() const;
};

class ZenzTextPrivacyAnalyzer {
 public:
  ZenzTextPrivacyAnalysis Analyze(
      absl::string_view text,
      ZenzTextPrivacyPolicy policy) const;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_TEXT_PRIVACY_ANALYZER_H_
