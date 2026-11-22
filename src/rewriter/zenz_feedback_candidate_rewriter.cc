#include "rewriter/zenz_feedback_candidate_rewriter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
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
// Never resegment, merge, or split converter-owned native segments.
// A full-reading correction may change multiple verified segments.
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

// Find a unique segment-by-segment projection of one full-reading Zenz surface.
// Each native segment can retain its top value, use an existing full-key
// candidate, or receive a synthetic value. We never guess the boundary between
// TWO adjacent synthetic values: at least one side must be fixed by a native
// top value or an explicitly listed full-key candidate.
//
// The bounded DP counts possible projections, saturating at two. No projection
// is used when more than one splitting could explain the Zenz surface, even if
// the output text looks correct. UTF-8 validity is checked by the caller;
// candidate boundaries must still lie on UTF-8 codepoint boundaries.
bool FindUniqueSegmentProjection(const Segments& segments,
                                 absl::string_view corrected_full_value,
                                 std::vector<std::string>* projected_values) {
  if (projected_values == nullptr || corrected_full_value.empty() ||
      segments.conversion_segments_size() < 2 ||
      segments.conversion_segments_size() > kMaxProjectionSegments) {
    return false;
  }
  projected_values->clear();

  const size_t segment_count = segments.conversion_segments_size();
  const size_t value_bytes = corrected_full_value.size();
  for (const Segment& segment : segments.conversion_segments()) {
    if (segment.key().empty() || segment.candidates_size() == 0 ||
        segment.candidate(0).value.empty()) {
      return false;
    }
  }

  const auto is_boundary = [&](size_t pos) {
    return pos == value_bytes ||
           (static_cast<unsigned char>(corrected_full_value[pos]) & 0xc0) !=
               0x80;
  };

  struct ProjectionState {
    int count = -1;  // 0, 1, 2 (2 also represents more than two)
    size_t next_offset = 0;
    bool next_was_synthetic = false;
  };
  std::vector<ProjectionState> states((segment_count + 1) *
                                      (value_bytes + 1) * 2);
  const auto state_at = [&](size_t index, size_t offset,
                            bool previous_was_synthetic) -> ProjectionState& {
    return states[(index * (value_bytes + 1) + offset) * 2 +
                  (previous_was_synthetic ? 1 : 0)];
  };

  std::function<int(size_t, size_t, bool)> count_projections;
  count_projections = [&](size_t index, size_t offset,
                          bool previous_was_synthetic) -> int {
    ProjectionState& state =
        state_at(index, offset, previous_was_synthetic);
    if (state.count >= 0) {
      return state.count;
    }
    if (index == segment_count) {
      state.count = offset == value_bytes ? 1 : 0;
      return state.count;
    }
    state.count = 0;
    const Segment& segment = segments.conversion_segment(index);
    const absl::string_view original = segment.candidate(0).value;

    const auto add_path = [&](size_t end, bool is_synthetic,
                              int suffix_count) {
      if (suffix_count == 0) {
        return;
      }
      if (state.count == 0 && suffix_count == 1) {
        state.next_offset = end;
        state.next_was_synthetic = is_synthetic;
      }
      state.count = std::min(2, state.count + suffix_count);
    };

    // The original top value is the most reliable boundary anchor.
    if (offset + original.size() <= value_bytes &&
        corrected_full_value.substr(offset, original.size()) == original) {
      const size_t end = offset + original.size();
      add_path(end, false, count_projections(index + 1, end, false));
    }

    // Existing candidates may also anchor a boundary, but only if they consume
    // precisely this native segment's reading. Do not count duplicate surfaces
    // as distinct projections.
    for (size_t pos = 1; pos < segment.candidates_size() && state.count < 2;
         ++pos) {
      const converter::Candidate& candidate = segment.candidate(pos);
      if (candidate.key != segment.key() || candidate.value.empty() ||
          candidate.value == original ||
          FindCandidateByValue(segment, candidate.value) !=
              static_cast<int>(pos) ||
          offset + candidate.value.size() > value_bytes ||
          corrected_full_value.substr(offset, candidate.value.size()) !=
              candidate.value) {
        continue;
      }
      const size_t end = offset + candidate.value.size();
      if (!is_boundary(end)) {
        continue;
      }
      add_path(end, false, count_projections(index + 1, end, false));
    }

    // Synthetic values cannot follow another synthetic value, because Zenz
    // does not provide an internal boundary to distinguish those alternatives.
    if (!previous_was_synthetic && state.count < 2) {
      for (size_t end = offset + 1; end <= value_bytes; ++end) {
        if (!is_boundary(end)) {
          continue;
        }
        const int suffix_count = count_projections(index + 1, end, true);
        if (suffix_count == 0) {
          continue;
        }
        const absl::string_view replacement =
            corrected_full_value.substr(offset, end - offset);
        if (replacement == original ||
            FindCandidateByValue(segment, replacement) >= 0) {
          continue;
        }
        add_path(end, true, suffix_count);
        if (state.count == 2) {
          break;
        }
      }
    }
    return state.count;
  };

  // A unique projection is required; no arbitrary best alignment is chosen.
  if (count_projections(0, 0, false) != 1) {
    return false;
  }

  size_t offset = 0;
  bool previous_was_synthetic = false;
  for (size_t index = 0; index < segment_count; ++index) {
    const ProjectionState& state =
        state_at(index, offset, previous_was_synthetic);
    if (state.count != 1 || state.next_offset <= offset) {
      projected_values->clear();
      return false;
    }
    projected_values->emplace_back(corrected_full_value.substr(
        offset, state.next_offset - offset));
    offset = state.next_offset;
    previous_was_synthetic = state.next_was_synthetic;
  }
  return offset == value_bytes;
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

    if (multi_segment) {
      std::vector<std::string> projected_values;
      if (!FindUniqueSegmentProjection(*segments, zenz_value,
                                       &projected_values)) {
        continue;
      }

      // Validate EVERY changed segment before touching any candidate. One
      // protected candidate must prevent a partial projection of this phrase.
      bool safe_projection = true;
      size_t changed_count = 0;
      for (size_t i = 0; i < segment_count; ++i) {
        const Segment& target = segments->conversion_segment(i);
        if (projected_values[i] == target.candidate(0).value) {
          continue;
        }
        ++changed_count;
        const int existing_pos =
            FindCandidateByValue(target, projected_values[i]);
        if ((target.candidate(0).attributes &
             kProtectedProjectionAttributes) != 0 ||
            (existing_pos > 0 &&
             (target.candidate(existing_pos).attributes &
              kProtectedProjectionAttributes) != 0)) {
          safe_projection = false;
          break;
        }
      }
      if (!safe_projection || changed_count == 0) {
        continue;
      }

      for (size_t i = 0; i < segment_count; ++i) {
        Segment* target = segments->mutable_conversion_segment(i);
        const std::string& projected_value = projected_values[i];
        if (projected_value == target->candidate(0).value) {
          continue;
        }
        const int existing_pos = FindCandidateByValue(*target, projected_value);
        if (existing_pos > 0) {
          converter::Candidate* existing_candidate =
              target->mutable_candidate(existing_pos);
          ApplyFeedbackCost(feedback_candidate, existing_candidate);
          const int insert_pos = FindMovePosition(
              *target, existing_pos, existing_candidate->cost);
          target->move_candidate(existing_pos, insert_pos);
          MarkFeedbackCandidate(target, insert_pos);
        } else {
          const converter::Candidate base_candidate = target->candidate(0);
          converter::Candidate synthetic_candidate;
          FillSyntheticCandidate(base_candidate, target->key(),
                                 projected_value, &synthetic_candidate);
          ApplyFeedbackCost(feedback_candidate, &synthetic_candidate);
          const int insert_pos = FindSyntheticInsertPosition(
              *target, synthetic_candidate.cost);
          converter::Candidate* candidate =
              target->insert_candidate(insert_pos);
          if (candidate == nullptr) {
            return false;
          }
          *candidate = synthetic_candidate;
          MarkFeedbackCandidate(target, insert_pos);
        }
      }
      return true;
    }

    // Preserve the pre-existing single-segment behavior unchanged.
    const int existing_pos = FindCandidateByValue(*segment, zenz_value);
    if (existing_pos == 0) {
      continue;
    }

    if (existing_pos > 0) {
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
    FillSyntheticCandidate(base_candidate, full_key, zenz_value,
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
