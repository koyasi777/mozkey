// src/engine/contextual_candidate_reranker.h

#ifndef MOZC_ENGINE_CONTEXTUAL_CANDIDATE_RERANKER_H_
#define MOZC_ENGINE_CONTEXTUAL_CANDIDATE_RERANKER_H_

#include "absl/strings/string_view.h"
#include "converter/segments.h"

namespace mozc {
namespace engine {

class ContextualCandidateReranker {
 public:
  ContextualCandidateReranker() = default;

  ContextualCandidateReranker(const ContextualCandidateReranker&) = delete;
  ContextualCandidateReranker& operator=(const ContextualCandidateReranker&) =
      delete;

  // preceding_text provides optional client left context when Segments lacks
  // a committed HISTORY segment. It is never retained or logged.
  void Rerank(converter::Segments* segments,
              absl::string_view preceding_text = {}) const;
};

}  // namespace engine
}  // namespace mozc

#endif  // MOZC_ENGINE_CONTEXTUAL_CANDIDATE_RERANKER_H_
