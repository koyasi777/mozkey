// Copyright 2026 Mozkey contributors.
#ifndef MOZC_REWRITER_EXTERNAL_DICTIONARY_REWRITER_H_
#define MOZC_REWRITER_EXTERNAL_DICTIONARY_REWRITER_H_

#include <memory>
#include <utility>

#include "dictionary/external_dictionary_manager.h"
#include "rewriter/rewriter_interface.h"

namespace mozc {

// Injects exact-match external entries only after the normal conversion has
// produced at least one candidate. Never performs filesystem I/O.
class ExternalDictionaryRewriter final : public RewriterInterface {
 public:
  explicit ExternalDictionaryRewriter(
      std::shared_ptr<const dictionary::ExternalDictionaryManager> manager)
      : manager_(std::move(manager)) {}

  int capability(const ConversionRequest& request) const override;
  bool Rewrite(const ConversionRequest& request,
               Segments* segments) const override;

 private:
  const std::shared_ptr<const dictionary::ExternalDictionaryManager> manager_;
};

}  // namespace mozc

#endif  // MOZC_REWRITER_EXTERNAL_DICTIONARY_REWRITER_H_
