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
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/util.h"

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

bool IsAsciiPunctuationKeyChar(absl::string_view text) {
  if (text.size() != 1) {
    return false;
  }
  const unsigned char c = static_cast<unsigned char>(text.front());
  return (0x21 <= c && c <= 0x2F) || (0x3A <= c && c <= 0x40) ||
         (0x5B <= c && c <= 0x60) || (0x7B <= c && c <= 0x7E);
}

bool IsPeelableUnchangedSeparator(absl::string_view key_char,
                                  absl::string_view mozc_char,
                                  absl::string_view zenz_char) {
  if (!IsAsciiPunctuationKeyChar(key_char) || mozc_char != zenz_char ||
      Util::CharsLen(mozc_char) != 1) {
    return false;
  }

  // Keep letters, numbers, kana, kanji, and emoji inside the lexical unit.
  // Only an unchanged symbol/punctuation surface can act as a learning
  // separator. The baseline itself is evidence that this key-edge produced
  // this surface before Zenz changed the neighboring lexical material.
  return Util::GetScriptType(mozc_char) == Util::UNKNOWN_SCRIPT;
}

bool IsFullWidthAsciiPunctuation(char32_t c) {
  return (0xFF01 <= c && c <= 0xFF0F) ||
         (0xFF1A <= c && c <= 0xFF20) ||
         (0xFF3B <= c && c <= 0xFF40) ||
         (0xFF5B <= c && c <= 0xFF5E);
}

// Live preedit keys may expose display-restored punctuation while the Zenz
// request keeps the composer's ASCII form.  Keep this mapping deliberately
// narrower than NFKC so letters, numbers, kana, and kanji remain identity
// sensitive.
std::string NormalizeProjectionPunctuationKey(absl::string_view key) {
  std::string result;
  result.reserve(key.size());

  for (char32_t c : Util::Utf8ToUtf32(key)) {
    if (IsFullWidthAsciiPunctuation(c)) {
      c -= 0xFEE0;
    } else if (c == 0x301C) {  // WAVE DASH -> ASCII TILDE.
      c = U'~';
    }
    Util::CodepointToUtf8Append(c, &result);
  }
  return result;
}

}  // namespace

bool ReconcileZenzBaselineKeysForRequest(
    const std::vector<ZenzBaselineSegment>& baseline_segments,
    absl::string_view full_key,
    std::vector<ZenzBaselineSegment>* reconciled_segments) {
  if (reconciled_segments == nullptr) {
    return false;
  }
  reconciled_segments->clear();
  if (baseline_segments.empty() || full_key.empty()) {
    return false;
  }

  *reconciled_segments = baseline_segments;

  std::string concatenated_key;
  for (const ZenzBaselineSegment& segment : *reconciled_segments) {
    if (segment.key.empty() || segment.value.empty()) {
      reconciled_segments->clear();
      return false;
    }
    concatenated_key.append(segment.key);
  }
  if (concatenated_key == full_key) {
    return true;
  }

  concatenated_key.clear();
  for (ZenzBaselineSegment& segment : *reconciled_segments) {
    segment.key = NormalizeProjectionPunctuationKey(segment.key);
    concatenated_key.append(segment.key);
  }
  if (concatenated_key == full_key) {
    return true;
  }

  reconciled_segments->clear();
  return false;
}

ZenzSegmentProjection ProjectZenzValueToMozcSegments(
    const std::vector<ZenzBaselineSegment>& baseline_segments,
    absl::string_view full_key, absl::string_view full_value) {
  const int segment_size = static_cast<int>(baseline_segments.size());
  if (segment_size <= 0 || full_key.empty() || full_value.empty()) {
    return {};
  }

  std::vector<ZenzBaselineSegment> projection_segments;
  if (!ReconcileZenzBaselineKeysForRequest(
          baseline_segments, full_key, &projection_segments)) {
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
    const std::string& value = projection_segments[i].value;
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

  ZenzSegmentProjection result;
  result.segments.reserve(segment_size);

  auto append_baseline_segment = [&](int index,
                                     absl::string_view zenz_value) -> bool {
    if (zenz_value.empty()) {
      return false;
    }

    const ZenzBaselineSegment& baseline = projection_segments[index];
    if (zenz_value == baseline.value) {
      result.segments.push_back(
          {baseline.key, baseline.value, std::string(zenz_value), false, true,
           {}});
      return true;
    }

    absl::string_view key = baseline.key;
    absl::string_view mozc_value = baseline.value;
    absl::string_view accepted_value = zenz_value;

    std::vector<ZenzProjectedSegment> leading;
    std::vector<ZenzProjectedSegment> trailing_reversed;

    while (!key.empty() && !mozc_value.empty() && !accepted_value.empty()) {
      const absl::string_view key_char = Util::Utf8SubString(key, 0, 1);
      const absl::string_view mozc_char =
          Util::Utf8SubString(mozc_value, 0, 1);
      const absl::string_view accepted_char =
          Util::Utf8SubString(accepted_value, 0, 1);
      if (!IsPeelableUnchangedSeparator(key_char, mozc_char, accepted_char)) {
        break;
      }

      leading.push_back({std::string(key_char), std::string(mozc_char),
                         std::string(accepted_char), false, true, {}});
      key.remove_prefix(key_char.size());
      mozc_value.remove_prefix(mozc_char.size());
      accepted_value.remove_prefix(accepted_char.size());
    }

    while (!key.empty() && !mozc_value.empty() && !accepted_value.empty()) {
      const size_t key_len = Util::CharsLen(key);
      const size_t mozc_len = Util::CharsLen(mozc_value);
      const size_t accepted_len = Util::CharsLen(accepted_value);
      const absl::string_view key_char =
          Util::Utf8SubString(key, key_len - 1, 1);
      const absl::string_view mozc_char =
          Util::Utf8SubString(mozc_value, mozc_len - 1, 1);
      const absl::string_view accepted_char =
          Util::Utf8SubString(accepted_value, accepted_len - 1, 1);
      if (!IsPeelableUnchangedSeparator(key_char, mozc_char, accepted_char)) {
        break;
      }

      trailing_reversed.push_back(
          {std::string(key_char), std::string(mozc_char),
           std::string(accepted_char), false, true, {}});
      key.remove_suffix(key_char.size());
      mozc_value.remove_suffix(mozc_char.size());
      accepted_value.remove_suffix(accepted_char.size());
    }

    const bool peeled = !leading.empty() || !trailing_reversed.empty();
    if (!peeled || key.empty() || mozc_value.empty() ||
        accepted_value.empty()) {
      result.segments.push_back(
          {baseline.key, baseline.value, std::string(zenz_value), true, true,
           {}});
      return true;
    }

    for (ZenzProjectedSegment& separator : leading) {
      result.segments.push_back(std::move(separator));
    }

    result.segments.push_back(
        {std::string(key), std::string(mozc_value),
         std::string(accepted_value), accepted_value != mozc_value, true, {}});

    for (auto iter = trailing_reversed.rbegin();
         iter != trailing_reversed.rend(); ++iter) {
      result.segments.push_back(std::move(*iter));
    }
    return true;
  };

  auto append_gap = [&](int first_index, int last_index,
                        absl::string_view zenz_value) -> bool {
    const int gap_size = last_index - first_index + 1;
    if (gap_size <= 0) {
      return zenz_value.empty();
    }
    if (zenz_value.empty()) {
      return false;
    }

    if (gap_size == 1) {
      return append_baseline_segment(first_index, zenz_value);
    }

    std::string gap_key;
    std::string gap_mozc_value;
    std::vector<std::string> gap_baseline_keys;
    gap_baseline_keys.reserve(gap_size);
    for (int i = first_index; i <= last_index; ++i) {
      gap_key.append(projection_segments[i].key);
      gap_mozc_value.append(projection_segments[i].value);
      gap_baseline_keys.push_back(projection_segments[i].key);
    }

    if (gap_mozc_value == zenz_value) {
      for (int i = first_index; i <= last_index; ++i) {
        if (!append_baseline_segment(i, projection_segments[i].value)) {
          return false;
        }
      }
      return true;
    }

    // The outer correspondence is exact because it is delimited by anchors,
    // but the internal boundary is ambiguous. Preserve the whole gap as one
    // unresolved unit so a later Mozc-native resolver can inspect it without
    // discarding independently safe material on either side.
    result.segments.push_back(
        {std::move(gap_key), std::move(gap_mozc_value),
         std::string(zenz_value), true, false,
         std::move(gap_baseline_keys)});
    return true;
  };

  int left_index = -1;
  size_t left_value_end = 0;
  for (const Anchor& anchor : anchors) {
    if (!append_gap(left_index + 1, anchor.index - 1,
                    full_value.substr(left_value_end,
                                      anchor.begin - left_value_end))) {
      return {};
    }
    if (!append_baseline_segment(anchor.index,
                                 projection_segments[anchor.index].value)) {
      return {};
    }
    left_index = anchor.index;
    left_value_end = anchor.end;
  }

  if (!append_gap(left_index + 1, segment_size - 1,
                  full_value.substr(left_value_end))) {
    return {};
  }

  std::string reconstructed_key;
  std::string reconstructed_value;
  for (const ZenzProjectedSegment& segment : result.segments) {
    if (segment.key.empty() || segment.zenz_value.empty()) {
      return {};
    }
    reconstructed_key.append(segment.key);
    reconstructed_value.append(segment.zenz_value);
  }
  if (reconstructed_key != full_key || reconstructed_value != full_value) {
    return {};
  }

  result.success = true;
  return result;
}

}  // namespace mozc::session
