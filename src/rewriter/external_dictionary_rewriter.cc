// Copyright 2026 Mozkey contributors.
#include "rewriter/external_dictionary_rewriter.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "dictionary/external_dictionary_manager.h"
#include "protocol/commands.pb.h"
#include "request/conversion_request.h"

namespace mozc {

int ExternalDictionaryRewriter::capability(
    const ConversionRequest& request) const {
  if (!manager_ || request.request_type() != ConversionRequest::CONVERSION ||
      request.incognito_mode() || request.context().is_private_input() ||
      request.context().input_field_type() == commands::Context::PASSWORD ||
      request.options().used_in_predictor_realtime_conversion) {
    return RewriterInterface::NOT_AVAILABLE;
  }
  return RewriterInterface::CONVERSION;
}

bool ExternalDictionaryRewriter::Rewrite(
    const ConversionRequest& request, Segments* segments) const {
  if (capability(request) == RewriterInterface::NOT_AVAILABLE ||
      segments == nullptr || segments->conversion_segments_size() == 0) {
    return false;
  }

  // A multi-segment candidate consumes whole existing segments without
  // rewriting any native boundary. Match the entire *typed* conversion reading
  // against an exact key or an opted-in longer key, never a sub-segment.
  constexpr size_t kMaxExternalSegmentSpan = 16;
  const size_t span = segments->conversion_segments_size();
  if (span > kMaxExternalSegmentSpan) return false;

  std::string full_key;
  std::string native_top_value;
  full_key.reserve(request.key().size());
  for (const Segment& part : segments->conversion_segments()) {
    if (part.key().empty() || part.candidates_size() == 0 ||
        part.candidate(0).effective_converted_segment_count() != 1 ||
        (part.candidate(0).attributes &
         converter::Attribute::COMMAND_CANDIDATE)) {
      return false;
    }
    full_key.append(part.key());
    native_top_value.append(part.candidate(0).value);
  }
  if (full_key.empty() || full_key != request.key()) return false;

  Segment* segment = segments->mutable_conversion_segment(0);
  const auto snapshot = manager_->GetSnapshot();
  const auto found = snapshot->entries.find(full_key);
  const auto prefix_values = snapshot->LookupPrefix(full_key);
  if (found == snapshot->entries.end() && prefix_values.empty()) return false;

  // Preserve Phase 1 placement for exact matches. Prefix matches follow exact
  // matches, never replacing the native top or an existing identical candidate.
  size_t insertion_index = 1;
  bool changed = false;
  const auto insert = [&](const std::string& value, bool prefix) {
    // The concatenation of existing top candidates is already selectable.
    // Avoid a duplicate full-reading surface across multiple segments.
    if (value == native_top_value) return;
    bool already_present = false;
    for (size_t i = 0; i < segment->candidates_size(); ++i) {
      const converter::Candidate& existing = segment->candidate(i);
      if (existing.value == value &&
          (span == 1 ||
           (existing.key == full_key &&
            existing.effective_converted_segment_count() == span))) {
        already_present = true;
        break;
      }
    }
    if (already_present) return;

    converter::Candidate* candidate =
        segment->insert_candidate(static_cast<int>(insertion_index));
    if (candidate == nullptr) return;
    ++insertion_index;
    const converter::Candidate& base = segment->candidate(0);
    candidate->key = full_key;
    candidate->content_key = full_key;
    candidate->value = value;
    candidate->content_value = value;
    // Selecting this candidate consumes exactly the original native segments.
    // EngineConverter already honors this span for display, focus and commit;
    // no ResizeSegments request or pre-selection Viterbi rerun is necessary.
    candidate->converted_segment_count = static_cast<uint32_t>(span);
    // These fields are presentation metadata copied from the existing
    // conversion, not a new Viterbi/POS analysis of the external word.
    candidate->lid = base.lid;
    candidate->rid = base.rid;
    candidate->cost = base.cost;
    candidate->wcost = base.wcost;
    candidate->structure_cost = base.structure_cost;
    candidate->description = prefix ? "外部辞書（前方一致）" : "外部辞書";
    candidate->attributes |= converter::Attribute::NO_LEARNING |
                             converter::Attribute::NO_VARIANTS_EXPANSION |
                             converter::Attribute::NO_MODIFICATION;
    candidate->ranking_constraints |= converter::RankingConstraint::NO_TOP;
    changed = true;
  };
  if (found != snapshot->entries.end()) {
    for (const std::string& value : found->second) insert(value, false);
  }
  for (const std::string& value : prefix_values) insert(value, true);
  return changed;
}

}  // namespace mozc
