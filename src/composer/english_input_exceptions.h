// User-defined, explicit exceptions to ordinary Roman input.
#ifndef MOZC_COMPOSER_ENGLISH_INPUT_EXCEPTIONS_H_
#define MOZC_COMPOSER_ENGLISH_INPUT_EXCEPTIONS_H_

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "protocol/config.pb.h"

namespace mozc::composer {

class EnglishInputExceptions final {
 public:
  using Entry = config::Config::EnglishInputException;

  // Only lowercase ASCII alphabetic input is currently supported. Delimiters
  // such as '-' and '+' need separate word-boundary semantics; reject them.
  static bool IsValidInput(absl::string_view input);
  static bool IsValidPreferred(absl::string_view preferred);
  static bool IsValidEntry(const Entry& entry);

  // Returns the exact, enabled rule, or nullptr. Never infer an English word.
  // A malformed or duplicate entry fails closed.
  static const Entry* Find(const config::Config& config,
                           absl::string_view raw);

  // Build once when several entries must be checked in the same operation.
  // Keys refer to the supplied Config; the Config must outlive this index.
  // Invalid/disabled entries never match, and even disabled duplicates make
  // their input ambiguous. No probabilistic or implicit matching occurs.
  class Index final {
   public:
    explicit Index(const config::Config& config);
    const Entry* Find(absl::string_view raw) const;

   private:
    absl::flat_hash_map<absl::string_view, const Entry*> entries_;
  };
};

}  // namespace mozc::composer

#endif  // MOZC_COMPOSER_ENGLISH_INPUT_EXCEPTIONS_H_
