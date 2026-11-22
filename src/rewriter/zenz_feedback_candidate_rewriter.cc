#include "rewriter/zenz_feedback_candidate_rewriter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/util.h"
#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/inner_segment.h"
#include "converter/segments.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "session/zenz_feedback_store.h"
#include "session/zenz_orthography_policy.h"
#include "session/zenz_output_validator.h"

namespace mozc {
namespace {

constexpr size_t kMinKeyChars = 2;
constexpr size_t kMaxKeyChars = 128;
constexpr size_t kMaxValueChars = 128;
constexpr size_t kMaxValueBytes = 512;
// A deliberately narrow experiment: do not resegment, merge, or split native
// conversion segments. Only one already-existing Mozc segment may change.
constexpr size_t kMaxProjectionSegments = 8;
// Do not mutate immutable or nonstandard candidates while applying a
// segment-local projection. This applies to existing lower-ranked candidates
// too, not only to the segment's top candidate.
constexpr uint32_t kProtectedProjectionAttributes =
    converter::Attribute::NO_MODIFICATION |
    converter::Attribute::DISABLE_RESCORING |
    converter::Attribute::COMMAND_CANDIDATE |
    converter::Attribute::PARTIALLY_KEY_CONSUMED |
    converter::Attribute::TYPING_CORRECTION |
    converter::Attribute::NO_LEARNING |
    converter::Attribute::USER_DICTIONARY;

bool ContainsUnsafeTextChar(absl::string_view text) {
  for (const char c : text) {
    if (c == '\t' || c == '\r' || c == '\n') {
      return true;
    }
  }
  return false;
}

bool IsSafeCandidateText(absl::string_view key, absl::string_view value) {
  if (key.empty() || value.empty()) {
    return false;
  }

  if (!Util::IsValidUtf8(key) || !Util::IsValidUtf8(value)) {
    return false;
  }

  if (ContainsUnsafeTextChar(key) || ContainsUnsafeTextChar(value)) {
    return false;
  }

  if (Util::CharsLen(key) < kMinKeyChars ||
      Util::CharsLen(key) > kMaxKeyChars ||
      Util::CharsLen(value) > kMaxValueChars ||
      value.size() > kMaxValueBytes) {
    return false;
  }

  return true;
}

int FindCandidateByValue(const Segment& segment, absl::string_view value) {
  for (size_t i = 0; i < segment.candidates_size(); ++i) {
    if (segment.candidate(i).value == value) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int ClampCost(int64_t cost) {
  return static_cast<int>(std::clamp<int64_t>(
      cost, 0, std::numeric_limits<int32_t>::max()));
}

int FeedbackCostDelta(const session::ZenzFeedbackCandidate& candidate) {
  // A positive total score lowers the cost and makes the candidate easier to
  // surface.  Ordinary rejected feedback only reduces this bonus; it should not
  // erase the candidate from the set.
  constexpr int kMaxBonus = 1800;
  constexpr int kMaxPenalty = 2400;
  const int raw_delta = -candidate.total_score / 2;
  return std::clamp(raw_delta, -kMaxBonus, kMaxPenalty);
}

void ApplyFeedbackCost(const session::ZenzFeedbackCandidate& feedback_candidate,
                       converter::Candidate* candidate) {
  const int delta = FeedbackCostDelta(feedback_candidate);
  candidate->cost = ClampCost(static_cast<int64_t>(candidate->cost) + delta);
  candidate->wcost = ClampCost(static_cast<int64_t>(candidate->wcost) + delta);
}

int FindSyntheticInsertPosition(const Segment& segment, int adjusted_cost) {
  int position = 0;
  for (size_t i = 0; i < segment.candidates_size(); ++i) {
    if (segment.candidate(i).cost <= adjusted_cost) {
      ++position;
    }
  }
  return position;
}

int FindMovePosition(const Segment& segment, int existing_pos,
                     int adjusted_cost) {
  int position = 0;
  for (size_t i = 0; i < segment.candidates_size(); ++i) {
    if (static_cast<int>(i) == existing_pos) {
      continue;
    }
    if (segment.candidate(i).cost <= adjusted_cost) {
      ++position;
    }
  }
  return std::min<int>(position, segment.candidates_size() - 1);
}

void MarkFeedbackCandidate(Segment* segment, int candidate_pos) {
  if (segment == nullptr || candidate_pos < 0 ||
      candidate_pos >= static_cast<int>(segment->candidates_size())) {
    return;
  }

  converter::Candidate* candidate = segment->mutable_candidate(candidate_pos);
  candidate->attributes |=
      converter::Attribute::RERANKED |
      converter::Attribute::USER_SEGMENT_HISTORY_REWRITER;

  if (candidate_pos == 0) {
    for (size_t i = 0; i < segment->candidates_size(); ++i) {
      segment->mutable_candidate(i)->attributes &=
          ~converter::Attribute::BEST_CANDIDATE;
    }
    candidate->attributes |= converter::Attribute::BEST_CANDIDATE;
  } else {
    candidate->attributes &= ~converter::Attribute::BEST_CANDIDATE;
  }
}

std::string BuildFullConversionKey(const Segments& segments) {
  std::string key;
  for (const Segment& segment : segments.conversion_segments()) {
    key.append(segment.key());
  }
  return key;
}

// Joins only the visible top candidates of the converter-owned segments.
// This is the baseline which the user saw before Zenz corrected the surface.
std::string BuildTopConversionValue(const Segments& segments) {
  std::string value;
  for (const Segment& segment : segments.conversion_segments()) {
    if (segment.candidates_size() == 0) {
      return {};
    }
    value.append(segment.candidate(0).value);
  }
  return value;
}

// Project a full-reading correction onto exactly one existing Mozc segment.
// Everything to the left and right must match the original candidate surfaces
// byte for byte. Because each boundary separates complete valid UTF-8 values,
// the replacement also falls on codepoint boundaries. Ambiguous or
// cross-segment corrections are refused; Zenz supplies no boundary metadata.
int FindSingleChangedSegment(const Segments& segments,
                             absl::string_view original_full_value,
                             absl::string_view corrected_full_value,
                             std::string* projected_value) {
  if (projected_value == nullptr || corrected_full_value.empty() ||
      segments.conversion_segments_size() < 2 ||
      segments.conversion_segments_size() > kMaxProjectionSegments) {
    return -1;
  }
  projected_value->clear();
  int match_index = -1;
  size_t left_bytes = 0;
  for (size_t i = 0; i < segments.conversion_segments_size(); ++i) {
    const Segment& segment = segments.conversion_segment(i);
    if (segment.key().empty() || segment.candidates_size() == 0) {
      return -1;
    }
    const absl::string_view previous = segment.candidate(0).value;
    if (previous.empty() || left_bytes + previous.size() >
                                original_full_value.size()) {
      return -1;
    }
    const size_t right_bytes = original_full_value.size() -
                               left_bytes - previous.size();
    if (corrected_full_value.size() >= left_bytes + right_bytes &&
        corrected_full_value.substr(0, left_bytes) ==
            original_full_value.substr(0, left_bytes) &&
        corrected_full_value.substr(corrected_full_value.size() - right_bytes) ==
            original_full_value.substr(original_full_value.size() - right_bytes)) {
      const absl::string_view replacement = corrected_full_value.substr(
          left_bytes, corrected_full_value.size() - left_bytes - right_bytes);
      if (!replacement.empty() && replacement != previous &&
          Util::IsValidUtf8(replacement) &&
          !ContainsUnsafeTextChar(replacement)) {
        // Do not choose arbitrarily when multiple native segments can
        // explain the same full-phrase correction.
        if (match_index >= 0) {
          projected_value->clear();
          return -1;
        }
        match_index = static_cast<int>(i);
        *projected_value = std::string(replacement);
      }
    }
    left_bytes += previous.size();
  }
  return left_bytes == original_full_value.size() ? match_index : -1;
}

bool HasEmptyConversionSegment(const Segments& segments) {
  for (const Segment& segment : segments.conversion_segments()) {
    if (segment.key().empty() || segment.candidates_size() == 0) {
      return true;
    }
  }
  return false;
}

void FillSyntheticCandidate(const converter::Candidate& base_candidate,
                            absl::string_view key,
                            absl::string_view value,
                            converter::Candidate* candidate) {
  *candidate = base_candidate;
  candidate->key = std::string(key);
  candidate->content_key = std::string(key);
  candidate->value = std::string(value);
  candidate->content_value = std::string(value);

  candidate->inner_segment_boundary = converter::InnerSegmentBoundary();
  // A synthetic Zenz-feedback surface is not a system-dictionary node path.
  candidate->proper_noun_spans.clear();

  candidate->attributes &= ~converter::Attribute::BEST_CANDIDATE;
  candidate->attributes |=
      converter::Attribute::RERANKED |
      converter::Attribute::USER_SEGMENT_HISTORY_REWRITER;

  candidate->description = "Zenz 学習";
  candidate->a11y_description.clear();
  candidate->display_value.clear();
}

session::ZenzFeedbackReusePolicy GetZenzFeedbackReusePolicy(
    const config::Config& config) {
  session::ZenzFeedbackReusePolicy policy;
  policy.minimum_key_length_enabled =
      config.use_zenz_feedback_min_key_length();
  policy.minimum_key_length =
      std::clamp<uint32_t>(config.zenz_feedback_min_key_length(), 1, 20);
  policy.auto_block_policy.enabled =
      config.use_zenz_auto_block_rejected_correction();
  policy.auto_block_policy.minimum_reject_count =
      static_cast<int>(config.zenz_auto_block_reject_threshold());
  policy.auto_block_policy.minimum_reject_percentage = static_cast<int>(
      config.zenz_auto_block_minimum_reject_percentage());
  return policy;
}

}  // namespace

int ZenzFeedbackCandidateRewriter::capability(
    const ConversionRequest& request) const {
  if (request.request_type() != ConversionRequest::CONVERSION) {
    return RewriterInterface::NOT_AVAILABLE;
  }

  if (!request.config().use_zenz_feedback_reuse()) {
    return RewriterInterface::NOT_AVAILABLE;
  }

  // This rewriter has no EngineConverterInterface for reverse conversion.
  // In strict mode, do not inject or rerank unvalidated saved Zenz feedback.
  // Other Mozc rewriters, user dictionary, and ordinary history remain active.
  if (request.config().use_zenz_strict_reading_preservation()) {
    return RewriterInterface::NOT_AVAILABLE;
  }

  if (request.incognito_mode()) {
    return RewriterInterface::NOT_AVAILABLE;
  }

  if (!request.options().enable_user_history_for_conversion) {
    return RewriterInterface::NOT_AVAILABLE;
  }

  return RewriterInterface::CONVERSION;
}

bool ZenzFeedbackCandidateRewriter::Rewrite(
    const ConversionRequest& request,
    Segments* segments) const {
  if (segments == nullptr) {
    return false;
  }

  if (capability(request) == RewriterInterface::NOT_AVAILABLE) {
    return false;
  }

  const size_t segment_count = segments->conversion_segments_size();
  if (segment_count == 0 || segment_count > kMaxProjectionSegments ||
      HasEmptyConversionSegment(*segments)) {
    return false;
  }

  const bool multi_segment = segment_count > 1;
  // Never read persisted feedback for private input in the new multi-segment
  // path. The pre-existing single-segment behavior is unchanged.
  if (multi_segment &&
      (request.composer().GetInputFieldType() == commands::Context::PASSWORD ||
       request.context().is_private_input())) {
    return false;
  }

  constexpr absl::string_view kContextClass = "empty";

  Segment* segment = segments->mutable_conversion_segment(0);
  if (segment == nullptr || segment->candidates_size() == 0) {
    return false;
  }

  const std::string full_key = BuildFullConversionKey(*segments);
  if (full_key.empty() ||
      (multi_segment && full_key != request.key())) {
    return false;
  }

  session::ZenzFeedbackStore store;
  const std::vector<session::ZenzFeedbackCandidate> ranked_candidates =
      store.GetRankedCandidates(
          full_key, kContextClass,
          GetZenzFeedbackReusePolicy(request.config()));

  if (ranked_candidates.empty()) {
    return false;
  }

  const std::string original_top_value =
      multi_segment ? BuildTopConversionValue(*segments)
                    : segment->candidate(0).value;
  if (original_top_value.empty()) {
    return false;
  }

  const session::ZenzOrthographyPolicy orthography_policy;
  for (const session::ZenzFeedbackCandidate& feedback_candidate :
       ranked_candidates) {
    const std::string zenz_value =
        session::ZenzOutputValidator::RepairUserControlledSymbols(
            full_key, original_top_value, feedback_candidate.value);

    if (!IsSafeCandidateText(full_key, zenz_value)) {
      continue;
    }

    // Persisted Zenz feedback cannot bypass the user's current emoji policy.
    // The ordinary EmojiRewriter and explicit dictionary selections are not
    // affected by this check.
    if (!request.config().allow_zenz_emoji_changes() &&
        !session::ZenzOutputValidator::PreservesEmojiSequences(
            original_top_value, zenz_value)) {
      continue;
    }

    if (zenz_value == original_top_value) {
      continue;
    }

    // Learned feedback must not bypass the same obvious-omission guard used
    // for fresh Zenz responses, even if a synthetic candidate already exists.
    if (session::ZenzOutputValidator::IsObviousSurfaceTruncation(
            original_top_value, zenz_value)) {
      continue;
    }

    const session::ZenzOrthographyDecision orthography_decision =
        orthography_policy.Evaluate(original_top_value, zenz_value);
    if (!orthography_decision.allow) {
      continue;
    }

    // On multi-segment conversion, only use a full-reading correction when
    // precisely one existing native segment can reproduce it. Never collapse
    // or infer boundaries, and never inject the full phrase into one segment.
    std::string projected_value;
    if (multi_segment) {
      const int changed_segment = FindSingleChangedSegment(
          *segments, original_top_value, zenz_value, &projected_value);
      if (changed_segment < 0) {
        continue;
      }
      segment = segments->mutable_conversion_segment(changed_segment);
      if (segment == nullptr || segment->candidates_size() == 0) {
        continue;
      }
      if ((segment->candidate(0).attributes &
           kProtectedProjectionAttributes) != 0) {
        continue;
      }
    } else {
      projected_value = zenz_value;
    }

    const int existing_pos = FindCandidateByValue(*segment, projected_value);
    if (existing_pos == 0) {
      continue;
    }

    if (existing_pos > 0) {
      // The top candidate may be modifiable while a lower-ranked candidate
      // is not. Do not alter its cost, attributes, or position in that case.
      if (multi_segment &&
          (segment->candidate(existing_pos).attributes &
           kProtectedProjectionAttributes) != 0) {
        continue;
      }
      converter::Candidate* existing_candidate =
          segment->mutable_candidate(existing_pos);
      ApplyFeedbackCost(feedback_candidate, existing_candidate);
      const int insert_pos = FindMovePosition(
          *segment, existing_pos, existing_candidate->cost);
      segment->move_candidate(existing_pos, insert_pos);
      MarkFeedbackCandidate(segment, insert_pos);
      return true;
    }

    const converter::Candidate base_candidate = segment->candidate(0);
    converter::Candidate synthetic_candidate;
    FillSyntheticCandidate(base_candidate, segment->key(), projected_value,
                           &synthetic_candidate);
    ApplyFeedbackCost(feedback_candidate, &synthetic_candidate);

    const int insert_pos =
        FindSyntheticInsertPosition(*segment, synthetic_candidate.cost);
    converter::Candidate* candidate = segment->insert_candidate(insert_pos);
    if (candidate == nullptr) {
      return false;
    }

    *candidate = synthetic_candidate;
    MarkFeedbackCandidate(segment, insert_pos);
    return true;
  }

  return false;
}

}  // namespace mozc
