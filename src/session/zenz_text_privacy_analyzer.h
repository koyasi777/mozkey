#ifndef MOZC_SESSION_ZENZ_TEXT_PRIVACY_ANALYZER_H_
#define MOZC_SESSION_ZENZ_TEXT_PRIVACY_ANALYZER_H_

#include "absl/strings/string_view.h"

namespace mozc {
namespace session {

// Privacy policies intentionally remain separate in C2A.
//
// kLiveText preserves the existing Session key/value privacy policy.
//
// kLegacyContext preserves the historical context sanitizer policy, including
// the broad four-digit and eight-visible-ASCII fallbacks.
//
// kContext is the refined surrounding-context policy. It protects structured
// sensitive data instead of treating ordinary short technical text as secret.
//
// The sanitizer deliberately remains on kLegacyContext until C2C so this policy
// can be characterized independently before production behavior changes.
enum class ZenzTextPrivacyPolicy {
  kLiveText,
  kLegacyContext,
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
  kLegacyLongVisibleAscii,
  kLegacyLongDigitRun,
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