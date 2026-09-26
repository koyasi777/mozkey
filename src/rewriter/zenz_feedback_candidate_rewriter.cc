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

  candidate->attributes &= ~converter::Attribute::BEST_CANDIDATE;
  candidate->attributes |=
      converter::Attribute::RERANKED |
      converter::Attribute::USER_SEGMENT_HISTORY_REWRITER;

  candidate->description = "Zenz 学習";
  candidate->a11y_description.clear();
  candidate->display_value.clear();
}

session::ZenzFeedbackAutoBlockPolicy GetZenzFeedbackAutoBlockPolicy(
    const config::Config& config) {
  session::ZenzFeedbackAutoBlockPolicy policy;
  policy.enabled = config.use_zenz_auto_block_rejected_correction();
  policy.minimum_reject_count =
      static_cast<int>(config.zenz_auto_block_reject_threshold());
  policy.minimum_reject_percentage = static_cast<int>(
      config.zenz_auto_block_minimum_reject_percentage());
  return policy;
}

}  // namespace

int ZenzFeedbackCandidateRewriter::capability(
    const ConversionRequest& request) const {
  if (request.request_type() != ConversionRequest::CONVERSION) {
    return RewriterInterface::NOT_AVAILABLE;
  }

  if (!request.config().use_zenz_feedback_learning()) {
    return RewriterInterface::NOT_AVAILABLE;
  }

  if (request.incognito_mode()) {
    return RewriterInterface::NOT_AVAILABLE;
  }

  if (!request.options().enable_user_history_for_conversion) {
    return RewriterInterface::NOT_AVAILABLE;
  }

  if (request.config().history_learning_level() !=
      config::Config::DEFAULT_HISTORY) {
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

  if (segments->conversion_segments_size() != 1) {
    return false;
  }

  if (HasEmptyConversionSegment(*segments)) {
    return false;
  }

  constexpr absl::string_view kContextClass = "empty";

  Segment* segment = segments->mutable_conversion_segment(0);
  if (segment == nullptr || segment->candidates_size() == 0) {
    return false;
  }

  const std::string full_key = BuildFullConversionKey(*segments);
  if (full_key.empty()) {
    return false;
  }

  session::ZenzFeedbackStore store;
  const std::vector<session::ZenzFeedbackCandidate> ranked_candidates =
      store.GetRankedCandidates(
          full_key, kContextClass,
          GetZenzFeedbackAutoBlockPolicy(request.config()));

  if (ranked_candidates.empty()) {
    return false;
  }

  const std::string original_top_value = segment->candidate(0).value;
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

    if (zenz_value == original_top_value) {
      continue;
    }

    const session::ZenzOrthographyDecision orthography_decision =
        orthography_policy.Evaluate(original_top_value, zenz_value);
    if (!orthography_decision.allow) {
      continue;
    }

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

    // Legacy feedback rows predate reading-preservation validation. They may
    // still rerank a value already produced by Mozc for this key, but they must
    // not authorize a new synthetic surface that Mozc itself did not produce.
    if (!feedback_candidate.reading_preserved) {
      continue;
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
