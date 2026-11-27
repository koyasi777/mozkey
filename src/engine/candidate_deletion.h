// src/engine/candidate_deletion.h

#ifndef MOZC_ENGINE_CANDIDATE_DELETION_H_
#define MOZC_ENGINE_CANDIDATE_DELETION_H_

#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"

namespace mozc {
namespace engine {

enum class CandidateDeletionAction {
  kNone,
  kDeleteHistory,
  kSuppress,
};

inline bool HasDeletableHistory(const converter::Candidate& candidate) {
  return candidate.attributes &
         (converter::Attribute::USER_HISTORY_PREDICTION |
          converter::Attribute::USER_SEGMENT_HISTORY_REWRITER);
}

// Determine the action advertised for the displayed candidate. Keep history
// deletion distinct from suppression, even if the requested mutation fails.
inline CandidateDeletionAction GetCandidateDeletionAction(
    const Segment& segment, int candidate_index,
    bool allow_last_candidate_suppression) {
  if (!segment.is_valid_index(candidate_index)) {
    return CandidateDeletionAction::kNone;
  }
  const converter::Candidate& candidate = segment.candidate(candidate_index);
  if (HasDeletableHistory(candidate)) {
    return CandidateDeletionAction::kDeleteHistory;
  }
  // Only ordinary conversion requires a remaining regular candidate.
  // Prediction/suggestion may legitimately yield no candidates after refresh.
  if (candidate_index < 0 ||
      (!allow_last_candidate_suppression && segment.candidates_size() <= 1) ||
      candidate.key.empty() || candidate.value.empty() ||
      (candidate.attributes & converter::Attribute::COMMAND_CANDIDATE)) {
    return CandidateDeletionAction::kNone;
  }
  return CandidateDeletionAction::kSuppress;
}

}  // namespace engine
}  // namespace mozc

#endif  // MOZC_ENGINE_CANDIDATE_DELETION_H_
