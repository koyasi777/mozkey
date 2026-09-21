// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef MOZC_SESSION_ZENZ_SEGMENT_PROJECTION_H_
#define MOZC_SESSION_ZENZ_SEGMENT_PROJECTION_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace mozc::session {

// Normal-Mozc segment snapshot taken at the time a Zenz request is made.
// `value` is the orthographic baseline selected by normal Mozc.
struct ZenzBaselineSegment {
  std::string key;
  std::string value;
};

// A conservative projection of one full Zenz value back onto normal-Mozc
// segment boundaries.
struct ZenzProjectedSegment {
  std::string key;
  std::string mozc_value;
  std::string zenz_value;
  bool changed = false;
};

struct ZenzSegmentProjection {
  bool success = false;
  std::vector<ZenzProjectedSegment> segments;
};

// Projects `full_value` onto `baseline_segments` without guessing ambiguous
// boundaries.  Unchanged normal-Mozc surfaces that occur exactly once are used
// as anchors.  A gap containing one segment may absorb a changed Zenz surface;
// a multi-segment gap is accepted only when it is unchanged byte-for-byte.
ZenzSegmentProjection ProjectZenzValueToMozcSegments(
    const std::vector<ZenzBaselineSegment>& baseline_segments,
    absl::string_view full_key, absl::string_view full_value);

}  // namespace mozc::session

#endif  // MOZC_SESSION_ZENZ_SEGMENT_PROJECTION_H_
