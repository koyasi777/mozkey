#ifndef MOZC_SESSION_ZENZ_TEXT_PRIVACY_ANALYZER_H_
#define MOZC_SESSION_ZENZ_TEXT_PRIVACY_ANALYZER_H_

#include "absl/strings/string_view.h"

namespace mozc {
namespace session {

// Privacy policies intentionally remain separate in C2A.
//
// kLiveText preserves the existing Session key/value privacy policy.
// kLegacyContext preserves the existing context sanitizer policy exactly.
//
// C2B can refine context semantics after this shared implementation has been
// independently validated.
enum class ZenzTextPrivacyPolicy {
  kLiveText,
  kLegacyContext,
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