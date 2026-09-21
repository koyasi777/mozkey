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

#include "session/zenz_segment_projection.h"

#include <cstddef>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace mozc::session {
namespace {

size_t CountOccurrences(absl::string_view haystack, absl::string_view needle) {
  if (needle.empty()) {
    return 0;
  }

  size_t count = 0;
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != absl::string_view::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

}  // namespace

ZenzSegmentProjection ProjectZenzValueToMozcSegments(
    const std::vector<ZenzBaselineSegment>& baseline_segments,
    absl::string_view full_key, absl::string_view full_value) {
  const int segment_size = static_cast<int>(baseline_segments.size());
  if (segment_size <= 0 || full_key.empty() || full_value.empty()) {
    return {};
  }

  std::string concatenated_key;
  for (const ZenzBaselineSegment& segment : baseline_segments) {
    if (segment.key.empty() || segment.value.empty()) {
      return {};
    }
    concatenated_key.append(segment.key);
  }

  // The snapshot must describe the same full Zenz request.  Otherwise it may
  // belong to an older live-conversion generation and must not be used.
  if (concatenated_key != full_key) {
    return {};
  }

  struct Anchor {
    int index;
    size_t begin;
    size_t end;
  };

  std::vector<Anchor> anchors;
  anchors.reserve(segment_size);
  for (int i = 0; i < segment_size; ++i) {
    const std::string& value = baseline_segments[i].value;
    if (CountOccurrences(full_value, value) != 1) {
      continue;
    }
    const size_t begin = full_value.find(value);
    if (begin == absl::string_view::npos) {
      return {};
    }
    anchors.push_back(Anchor{i, begin, begin + value.size()});
  }

  size_t previous_anchor_end = 0;
  for (const Anchor& anchor : anchors) {
    if (anchor.begin < previous_anchor_end) {
      return {};
    }
    previous_anchor_end = anchor.end;
  }

  std::vector<std::string> projected_values(segment_size);
  int left_index = -1;
  size_t left_value_end = 0;

  auto assign_gap = [&](int first_index, int last_index,
                        absl::string_view value) -> bool {
    const int gap_size = last_index - first_index + 1;
    if (gap_size <= 0) {
      return value.empty();
    }

    if (gap_size == 1) {
      if (value.empty()) {
        return false;
      }
      projected_values[first_index] = std::string(value);
      return true;
    }

    std::string original_gap_value;
    for (int i = first_index; i <= last_index; ++i) {
      original_gap_value.append(baseline_segments[i].value);
    }
    if (original_gap_value != value) {
      return false;
    }
    for (int i = first_index; i <= last_index; ++i) {
      projected_values[i] = baseline_segments[i].value;
    }
    return true;
  };

  for (const Anchor& anchor : anchors) {
    if (!assign_gap(left_index + 1, anchor.index - 1,
                    full_value.substr(left_value_end,
                                      anchor.begin - left_value_end))) {
      return {};
    }
    projected_values[anchor.index] = baseline_segments[anchor.index].value;
    left_index = anchor.index;
    left_value_end = anchor.end;
  }

  if (!assign_gap(left_index + 1, segment_size - 1,
                  full_value.substr(left_value_end))) {
    return {};
  }

  std::string reconstructed_value;
  for (const std::string& value : projected_values) {
    if (value.empty()) {
      return {};
    }
    reconstructed_value.append(value);
  }
  if (reconstructed_value != full_value) {
    return {};
  }

  ZenzSegmentProjection result;
  result.success = true;
  result.segments.reserve(segment_size);
  for (int i = 0; i < segment_size; ++i) {
    result.segments.push_back(
        {baseline_segments[i].key, baseline_segments[i].value,
         projected_values[i],
         projected_values[i] != baseline_segments[i].value});
  }
  return result;
}

}  // namespace mozc::session
