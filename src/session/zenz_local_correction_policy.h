// Copyright 2010-2021, Google Inc.
// All rights reserved.

#ifndef MOZC_SESSION_ZENZ_LOCAL_CORRECTION_POLICY_H_
#define MOZC_SESSION_ZENZ_LOCAL_CORRECTION_POLICY_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "session/zenz_segment_projection.h"

namespace mozc::session {

struct ZenzLocalCorrection {
  std::string reading;
  std::string rejected_value;
  std::string accepted_value;
};

struct ZenzLocalCorrectionReplayResult {
  std::string value;
  int applied_count = 0;
};

// Shrinks an exact reading/rejected/accepted triple to the smallest
// contrastive core by removing only UTF-8 character-aligned prefix/suffix
// material that is byte-for-byte identical in all three strings.
//
// This is structural canonicalization only. Callers must still enforce the
// current Mozc baseline, generated-fallback provenance, privacy, and projection
// boundary gates before learning or replaying the returned core.
std::optional<ZenzLocalCorrection> CanonicalizeZenzLocalCorrectionCore(
    absl::string_view reading,
    absl::string_view rejected_value,
    absl::string_view accepted_value);

// Extracts only exact local Zenz -> final corrections whose boundaries are
// independently known in both projections and whose final surface equals the
// original Mozc baseline surface. This deliberately refuses to infer a new
// reading/surface mapping from ambiguous or rewritten spans.
std::vector<ZenzLocalCorrection> ExtractZenzLocalCorrections(
    const std::vector<ZenzBaselineSegment>& baseline_segments,
    absl::string_view full_key,
    absl::string_view rejected_zenz_value,
    absl::string_view final_value);

// Replays exact contrastive corrections onto a current Zenz value. A correction
// is eligible only when the current projected span has the same reading, the
// same rejected surface, and the current Mozc baseline still equals the stored
// accepted surface.
ZenzLocalCorrectionReplayResult ReplayZenzLocalCorrections(
    const std::vector<ZenzBaselineSegment>& baseline_segments,
    absl::string_view full_key,
    absl::string_view zenz_value,
    absl::Span<const ZenzLocalCorrection> corrections);

}  // namespace mozc::session

#endif  // MOZC_SESSION_ZENZ_LOCAL_CORRECTION_POLICY_H_