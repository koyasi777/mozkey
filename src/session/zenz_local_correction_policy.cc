// Copyright 2010-2021, Google Inc.
// All rights reserved.

#include "session/zenz_local_correction_policy.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/util.h"
#include "session/zenz_segment_projection.h"

namespace mozc::session {

namespace {

std::string JoinUtf8Chars(const std::vector<std::string>& chars,
                          size_t begin, size_t end) {
  std::string result;
  for (size_t i = begin; i < end; ++i) {
    result.append(chars[i]);
  }
  return result;
}

}  // namespace

std::optional<ZenzLocalCorrection> CanonicalizeZenzLocalCorrectionCore(
    absl::string_view reading,
    absl::string_view rejected_value,
    absl::string_view accepted_value) {
  if (reading.empty() || rejected_value.empty() || accepted_value.empty() ||
      rejected_value == accepted_value) {
    return std::nullopt;
  }

  const std::vector<std::string> reading_chars =
      Util::SplitStringToUtf8Chars(reading);
  const std::vector<std::string> rejected_chars =
      Util::SplitStringToUtf8Chars(rejected_value);
  const std::vector<std::string> accepted_chars =
      Util::SplitStringToUtf8Chars(accepted_value);

  const size_t min_size =
      std::min({reading_chars.size(), rejected_chars.size(),
                accepted_chars.size()});

  size_t prefix = 0;
  while (prefix < min_size &&
         reading_chars[prefix] == rejected_chars[prefix] &&
         reading_chars[prefix] == accepted_chars[prefix]) {
    ++prefix;
  }

  size_t suffix = 0;
  while (suffix < reading_chars.size() - prefix &&
         suffix < rejected_chars.size() - prefix &&
         suffix < accepted_chars.size() - prefix &&
         reading_chars[reading_chars.size() - 1 - suffix] ==
             rejected_chars[rejected_chars.size() - 1 - suffix] &&
         reading_chars[reading_chars.size() - 1 - suffix] ==
             accepted_chars[accepted_chars.size() - 1 - suffix]) {
    ++suffix;
  }

  const std::string core_reading =
      JoinUtf8Chars(reading_chars, prefix, reading_chars.size() - suffix);
  const std::string core_rejected =
      JoinUtf8Chars(rejected_chars, prefix, rejected_chars.size() - suffix);
  const std::string core_accepted =
      JoinUtf8Chars(accepted_chars, prefix, accepted_chars.size() - suffix);

  if (core_reading.empty() || core_rejected.empty() || core_accepted.empty() ||
      core_rejected == core_accepted) {
    return std::nullopt;
  }

  return ZenzLocalCorrection{
      core_reading,
      core_rejected,
      core_accepted,
  };
}

std::vector<ZenzLocalCorrection> ExtractZenzLocalCorrections(
    const std::vector<ZenzBaselineSegment>& baseline_segments,
    absl::string_view full_key,
    absl::string_view rejected_zenz_value,
    absl::string_view final_value) {
  if (baseline_segments.empty() ||
      full_key.empty() ||
      rejected_zenz_value.empty() ||
      final_value.empty() ||
      rejected_zenz_value == final_value) {
    return {};
  }

  const ZenzSegmentProjection rejected_projection =
      ProjectZenzValueToMozcSegments(
          baseline_segments, full_key, rejected_zenz_value);
  const ZenzSegmentProjection final_projection =
      ProjectZenzValueToMozcSegments(
          baseline_segments, full_key, final_value);

  if (!rejected_projection.success ||
      !final_projection.success ||
      rejected_projection.segments.size() !=
          final_projection.segments.size()) {
    return {};
  }

  std::vector<ZenzLocalCorrection> corrections;
  for (size_t i = 0; i < rejected_projection.segments.size(); ++i) {
    const ZenzProjectedSegment& rejected =
        rejected_projection.segments[i];
    const ZenzProjectedSegment& final =
        final_projection.segments[i];

    // Both sides must independently prove the exact same original Mozc
    // boundary. Aggregated/ambiguous spans are intentionally ignored.
    if (!rejected.boundary_known ||
        !final.boundary_known ||
        rejected.key != final.key ||
        rejected.mozc_value != final.mozc_value) {
      continue;
    }

    // No user-visible local disagreement.
    if (rejected.zenz_value == final.zenz_value) {
      continue;
    }

    // The local correction is replayable only when the user's final choice is
    // exactly the Mozc baseline for this span. This prevents the store from
    // becoming a general reading -> surface dictionary.
    if (final.zenz_value != final.mozc_value) {
      continue;
    }

    // Zenz must actually have changed this span away from the baseline.
    if (rejected.zenz_value == rejected.mozc_value) {
      continue;
    }

    if (rejected.key.empty() ||
        rejected.zenz_value.empty() ||
        final.zenz_value.empty()) {
      continue;
    }

    const std::optional<ZenzLocalCorrection> canonical =
        CanonicalizeZenzLocalCorrectionCore(
            rejected.key, rejected.zenz_value, final.zenz_value);
    if (canonical.has_value()) {
      corrections.push_back(*canonical);
    }
  }

  return corrections;
}

ZenzLocalCorrectionReplayResult ReplayZenzLocalCorrections(
    const std::vector<ZenzBaselineSegment>& baseline_segments,
    absl::string_view full_key,
    absl::string_view zenz_value,
    absl::Span<const ZenzLocalCorrection> corrections) {
  ZenzLocalCorrectionReplayResult result;
  result.value = std::string(zenz_value);

  if (baseline_segments.empty() ||
      full_key.empty() ||
      zenz_value.empty() ||
      corrections.empty()) {
    return result;
  }

  const ZenzSegmentProjection projection =
      ProjectZenzValueToMozcSegments(
          baseline_segments, full_key, zenz_value);
  if (!projection.success) {
    return result;
  }

  std::string rebuilt;
  for (const ZenzProjectedSegment& segment : projection.segments) {
    std::string surface = segment.zenz_value;

    if (segment.boundary_known && segment.changed) {
      const std::optional<ZenzLocalCorrection> canonical =
          CanonicalizeZenzLocalCorrectionCore(
              segment.key, segment.zenz_value, segment.mozc_value);

      bool matched = false;
      for (const ZenzLocalCorrection& correction : corrections) {
        const bool exact_match =
            correction.reading == segment.key &&
            correction.rejected_value == segment.zenz_value &&
            correction.accepted_value == segment.mozc_value;
        const bool canonical_match =
            canonical.has_value() &&
            correction.reading == canonical->reading &&
            correction.rejected_value == canonical->rejected_value &&
            correction.accepted_value == canonical->accepted_value;
        if (exact_match || canonical_match) {
          matched = true;
          break;
        }
      }

      if (matched) {
        // The removed prefix/suffix is byte-for-byte identical in the reading,
        // rejected surface, and current Mozc baseline. Restoring this segment
        // to the baseline therefore changes only the proven contrastive core.
        surface = segment.mozc_value;
        ++result.applied_count;
      }
    }

    rebuilt.append(surface);
  }

  if (result.applied_count > 0) {
    result.value = std::move(rebuilt);
  }
  return result;
}

}  // namespace mozc::session