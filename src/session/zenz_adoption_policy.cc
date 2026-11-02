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

#include "session/zenz_adoption_policy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"
#include "session/zenz_output_validator.h"

namespace mozc::session {
namespace {

bool DecodeUtf8Char(absl::string_view s, size_t* index, char32_t* out) {
  if (index == nullptr || out == nullptr || *index >= s.size()) {
    return false;
  }

  const unsigned char c0 = static_cast<unsigned char>(s[*index]);
  if (c0 < 0x80) {
    *out = c0;
    ++*index;
    return true;
  }

  size_t length = 0;
  char32_t code = 0;
  if ((c0 & 0xE0) == 0xC0) {
    length = 2;
    code = c0 & 0x1F;
  } else if ((c0 & 0xF0) == 0xE0) {
    length = 3;
    code = c0 & 0x0F;
  } else if ((c0 & 0xF8) == 0xF0) {
    length = 4;
    code = c0 & 0x07;
  } else {
    return false;
  }

  if (*index + length > s.size()) {
    return false;
  }

  for (size_t i = 1; i < length; ++i) {
    const unsigned char cx = static_cast<unsigned char>(s[*index + i]);
    if ((cx & 0xC0) != 0x80) {
      return false;
    }
    code = (code << 6) | (cx & 0x3F);
  }

  *index += length;
  *out = code;
  return true;
}

void AppendUtf8Char(char32_t c, std::string* out) {
  if (out == nullptr) {
    return;
  }

  if (c <= 0x7F) {
    out->push_back(static_cast<char>(c));
  } else if (c <= 0x7FF) {
    out->push_back(static_cast<char>(0xC0 | (c >> 6)));
    out->push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else if (c <= 0xFFFF) {
    out->push_back(static_cast<char>(0xE0 | (c >> 12)));
    out->push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (c >> 18)));
    out->push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (c & 0x3F)));
  }
}

std::string HiraganaToKatakana(absl::string_view s) {
  std::string result;
  for (size_t i = 0; i < s.size();) {
    const size_t old_index = i;
    char32_t c = 0;
    if (!DecodeUtf8Char(s, &i, &c)) {
      result.append(s.substr(old_index, 1));
      i = old_index + 1;
      continue;
    }

    // Hiragana letters map to Katakana by adding 0x60.  Keep marks and the
    // prolonged sound mark as-is when they are outside this contiguous range.
    if (0x3041 <= c && c <= 0x3096) {
      c += 0x60;
    } else if (c == 0x309D) {
      c = 0x30FD;
    } else if (c == 0x309E) {
      c = 0x30FE;
    }
    AppendUtf8Char(c, &result);
  }
  return result;
}

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

bool DecodeUtf8CharAt(absl::string_view s, size_t index, char32_t* out,
                      size_t* length) {
  if (out == nullptr || length == nullptr || index >= s.size()) {
    return false;
  }
  size_t cursor = index;
  if (!DecodeUtf8Char(s, &cursor, out)) {
    return false;
  }
  *length = cursor - index;
  return true;
}

bool DecodePreviousUtf8Char(absl::string_view s, size_t index,
                            char32_t* out, size_t* begin,
                            size_t* length) {
  if (out == nullptr || begin == nullptr || length == nullptr || index == 0 ||
      index > s.size()) {
    return false;
  }

  size_t start = index - 1;
  while (start > 0 &&
         (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) {
    --start;
  }

  size_t cursor = start;
  if (!DecodeUtf8Char(s, &cursor, out) || cursor != index) {
    return false;
  }
  *begin = start;
  *length = index - start;
  return true;
}

bool IsAsciiWordLike(char32_t c) {
  return (U'0' <= c && c <= U'9') || (U'A' <= c && c <= U'Z') ||
         (U'a' <= c && c <= U'z') || c == U'_' || c == U'-' ||
         c == U'.' || c == U'+' || c == U'#';
}

bool IsJapaneseLetterLike(char32_t c) {
  return (0x3040 <= c && c <= 0x309F) ||  // Hiragana
         (0x30A0 <= c && c <= 0x30FF) ||  // Katakana
         (0x3400 <= c && c <= 0x9FFF) ||  // CJK Unified Ideographs
         (0xF900 <= c && c <= 0xFAFF) ||  // CJK Compatibility Ideographs
         (0xFF66 <= c && c <= 0xFF9D);    // Halfwidth Katakana
}

bool IsJapaneseParticleBoundary(char32_t c) {
  switch (c) {
    case U'の':
    case U'は':
    case U'を':
    case U'に':
    case U'で':
    case U'へ':
    case U'と':
    case U'が':
    case U'も':
    case U'や':
    case U'か':
      return true;
    default:
      return false;
  }
}

bool GetLeftNeighbor(absl::string_view value, size_t pos, bool* present,
                     char32_t* neighbor) {
  if (present == nullptr || neighbor == nullptr || pos > value.size()) {
    return false;
  }
  if (pos == 0) {
    *present = false;
    *neighbor = 0;
    return true;
  }

  size_t begin = 0;
  size_t length = 0;
  if (!DecodePreviousUtf8Char(value, pos, neighbor, &begin, &length)) {
    return false;
  }
  *present = true;
  return true;
}

bool GetRightNeighbor(absl::string_view value, size_t pos, bool* present,
                      char32_t* neighbor) {
  if (present == nullptr || neighbor == nullptr || pos > value.size()) {
    return false;
  }
  if (pos == value.size()) {
    *present = false;
    *neighbor = 0;
    return true;
  }

  size_t length = 0;
  if (!DecodeUtf8CharAt(value, pos, neighbor, &length)) {
    return false;
  }
  *present = true;
  return true;
}

bool NumericNeighborMatches(bool baseline_present, char32_t baseline,
                            bool candidate_present, char32_t candidate) {
  if (baseline_present != candidate_present) {
    return false;
  }
  if (!baseline_present) {
    return true;
  }

  // Japanese text next to a number is semantic content that Zenz is allowed to
  // improve (1代 -> 1台, 100円では変えない -> 100円では買えない).
  // Non-Japanese separators and symbols are user-visible formatting and must be
  // preserved exactly (100% must not become 100$, HTTP/2 must keep '/').
  if (IsJapaneseLetterLike(baseline)) {
    return IsJapaneseLetterLike(candidate);
  }
  return baseline == candidate;
}

bool NumericLiteralContextsMatch(absl::string_view baseline,
                                 absl::string_view candidate,
                                 absl::string_view surface) {
  if (surface.empty()) {
    return false;
  }

  size_t baseline_pos = 0;
  size_t candidate_pos = 0;
  while (true) {
    baseline_pos = baseline.find(surface, baseline_pos);
    candidate_pos = candidate.find(surface, candidate_pos);
    if (baseline_pos == absl::string_view::npos ||
        candidate_pos == absl::string_view::npos) {
      return baseline_pos == candidate_pos;
    }

    bool baseline_left_present = false;
    bool candidate_left_present = false;
    bool baseline_right_present = false;
    bool candidate_right_present = false;
    char32_t baseline_left = 0;
    char32_t candidate_left = 0;
    char32_t baseline_right = 0;
    char32_t candidate_right = 0;

    if (!GetLeftNeighbor(baseline, baseline_pos, &baseline_left_present,
                         &baseline_left) ||
        !GetLeftNeighbor(candidate, candidate_pos, &candidate_left_present,
                         &candidate_left) ||
        !GetRightNeighbor(baseline, baseline_pos + surface.size(),
                          &baseline_right_present, &baseline_right) ||
        !GetRightNeighbor(candidate, candidate_pos + surface.size(),
                          &candidate_right_present, &candidate_right)) {
      return false;
    }

    if (!NumericNeighborMatches(baseline_left_present, baseline_left,
                                candidate_left_present, candidate_left) ||
        !NumericNeighborMatches(baseline_right_present, baseline_right,
                                candidate_right_present, candidate_right)) {
      return false;
    }

    baseline_pos += surface.size();
    candidate_pos += surface.size();
  }
}

bool IsPunctuationOrSpaceBoundary(char32_t c) {
  if (c <= 0x20) {
    return true;
  }
  switch (c) {
    case U'、':
    case U'。':
    case U'，':
    case U'．':
    case U'・':
    case U'「':
    case U'」':
    case U'『':
    case U'』':
    case U'（':
    case U'）':
    case U'(':
    case U')':
    case U'[':
    case U']':
    case U'{':
    case U'}':
    case U' ':
    case U'\t':
    case U'\n':
      return true;
    default:
      return false;
  }
}

bool IsProtectedSurfaceBoundaryChar(char32_t c) {
  if (IsPunctuationOrSpaceBoundary(c) || IsJapaneseParticleBoundary(c)) {
    return true;
  }
  return !IsJapaneseLetterLike(c) && !IsAsciiWordLike(c);
}

bool HasProtectedSurfaceBoundaries(absl::string_view value, size_t pos,
                                   absl::string_view surface) {
  if (surface.empty() || pos > value.size() ||
      pos + surface.size() > value.size()) {
    return false;
  }

  if (pos > 0) {
    char32_t left = 0;
    size_t left_begin = 0;
    size_t left_len = 0;
    if (!DecodePreviousUtf8Char(value, pos, &left, &left_begin, &left_len) ||
        !IsProtectedSurfaceBoundaryChar(left)) {
      return false;
    }
  }

  const size_t right_pos = pos + surface.size();
  if (right_pos < value.size()) {
    char32_t right = 0;
    size_t right_len = 0;
    if (!DecodeUtf8CharAt(value, right_pos, &right, &right_len) ||
        !IsProtectedSurfaceBoundaryChar(right)) {
      return false;
    }
  }

  return true;
}

std::string FindUniqueRightBoundaryAfterSurface(absl::string_view value,
                                                absl::string_view surface) {
  if (surface.empty()) {
    return std::string();
  }

  std::string boundary;
  bool seen_boundary = false;
  size_t pos = 0;
  while ((pos = value.find(surface, pos)) != absl::string_view::npos) {
    const size_t right_pos = pos + surface.size();
    if (right_pos >= value.size()) {
      pos = right_pos;
      continue;
    }

    char32_t right = 0;
    size_t right_len = 0;
    if (!DecodeUtf8CharAt(value, right_pos, &right, &right_len) ||
        !IsProtectedSurfaceBoundaryChar(right)) {
      pos = right_pos;
      continue;
    }

    const std::string candidate(value.substr(right_pos, right_len));
    if (!seen_boundary) {
      boundary = candidate;
      seen_boundary = true;
    } else if (boundary != candidate) {
      return std::string();
    }
    pos = right_pos;
  }
  return seen_boundary ? boundary : std::string();
}

bool IsAllJapaneseAttachedContent(absl::string_view value) {
  if (value.empty()) {
    return false;
  }

  for (size_t i = 0; i < value.size();) {
    const size_t old_i = i;
    char32_t c = 0;
    if (!DecodeUtf8Char(value, &i, &c) || i == old_i) {
      return false;
    }
    if (!IsJapaneseLetterLike(c) || IsJapaneseParticleBoundary(c)) {
      return false;
    }
  }
  return true;
}

bool TryRepairAttachedProtectedSpan(absl::string_view mozc_value,
                                    const ProtectedConversionSpan& span,
                                    std::string* value) {
  if (value == nullptr || span.value.empty()) {
    return false;
  }

  const std::string expected_boundary =
      FindUniqueRightBoundaryAfterSurface(mozc_value, span.value);
  if (expected_boundary.empty()) {
    return false;
  }

  size_t pos = 0;
  while ((pos = value->find(span.value, pos)) != std::string::npos) {
    if (HasProtectedSurfaceBoundaries(*value, pos, span.value)) {
      pos += span.value.size();
      continue;
    }

    const size_t attached_begin = pos + span.value.size();
    const size_t boundary_pos = value->find(expected_boundary, attached_begin);
    if (boundary_pos == std::string::npos || boundary_pos == attached_begin ||
        boundary_pos - attached_begin > 12) {
      pos = attached_begin;
      continue;
    }

    const absl::string_view attached(value->data() + attached_begin,
                                     boundary_pos - attached_begin);
    if (!IsAllJapaneseAttachedContent(attached)) {
      pos = attached_begin;
      continue;
    }

    value->erase(attached_begin, boundary_pos - attached_begin);
    return true;
  }
  return false;
}
size_t CountBoundaryPreservedOccurrences(absl::string_view value,
                                         absl::string_view surface) {
  if (surface.empty()) {
    return 0;
  }

  size_t count = 0;
  size_t pos = 0;
  while ((pos = value.find(surface, pos)) != absl::string_view::npos) {
    if (HasProtectedSurfaceBoundaries(value, pos, surface)) {
      ++count;
    }
    pos += surface.size();
  }
  return count;
}

std::string BuildProtectedPlaceholder(size_t index) {
  return std::string("__MOZC_ZENZ_PROTECTED_") + std::to_string(index) +
         "__";
}

std::string BuildUniqueProtectedPlaceholder(size_t index,
                                            absl::string_view key) {
  std::string placeholder = BuildProtectedPlaceholder(index);
  size_t suffix = 0;
  while (absl::StrContains(key, placeholder)) {
    placeholder = std::string("__MOZC_ZENZ_PROTECTED_") +
                  std::to_string(index) + "_" + std::to_string(++suffix) +
                  "__";
  }
  return placeholder;
}

size_t ReplaceAllLiteral(absl::string_view from, absl::string_view to,
                         std::string* value) {
  if (value == nullptr || from.empty()) {
    return 0;
  }

  size_t count = 0;
  size_t pos = 0;
  while ((pos = value->find(from.data(), pos, from.size())) !=
         std::string::npos) {
    value->replace(pos, from.size(), to.data(), to.size());
    pos += to.size();
    ++count;
  }
  return count;
}

bool IsRepairCandidateSafe(absl::string_view from,
                           const ProtectedConversionSpan& span) {
  if (from.empty() || from == span.value) {
    return false;
  }
  if (!span.repairable ||
      span.tier != ProtectedConversionSpan::Tier::kIdentityCritical) {
    return false;
  }

  // Very short replacements such as "AI" or "Go" are too collision-prone.
  size_t ascii_alnum_count = 0;
  for (const unsigned char c : span.value) {
    if ((('0' <= c) && (c <= '9')) || (('A' <= c) && (c <= 'Z')) ||
        (('a' <= c) && (c <= 'z'))) {
      ++ascii_alnum_count;
    }
  }
  return ascii_alnum_count >= 3;
}

bool TryRepairProtectedSpan(const ProtectedConversionSpan& span,
                            std::string* value) {
  if (value == nullptr) {
    return false;
  }

  const std::array<std::string, 2> candidates = {
      HiraganaToKatakana(span.key),
      span.key,
  };

  for (const std::string& candidate : candidates) {
    if (!IsRepairCandidateSafe(candidate, span)) {
      continue;
    }
    if (CountOccurrences(*value, candidate) != 1) {
      continue;
    }

    *value = absl::StrReplaceAll(*value, {{candidate, span.value}});
    return absl::StrContains(*value, span.value);
  }

  return false;
}

bool BaselineSegmentsMatchAdoptionInput(
    const std::vector<ZenzBaselineSegment>& baseline_segments,
    absl::string_view key, absl::string_view mozc_value) {
  if (baseline_segments.empty()) {
    return false;
  }

  std::string concatenated_key;
  std::string concatenated_value;
  for (const ZenzBaselineSegment& segment : baseline_segments) {
    if (segment.key.empty() || segment.value.empty()) {
      return false;
    }
    concatenated_key.append(segment.key);
    concatenated_value.append(segment.value);
  }
  return concatenated_key == key && concatenated_value == mozc_value;
}

// v8.4: Repair only a provably aligned, small change to a selected system name.
// Avoid guessing from similar spellings or the absolute byte offset in Zenz's
// output: the latter may change when text before the name is corrected.
struct NameRepairScalars {
  std::vector<char32_t> chars;
  std::vector<size_t> byte_offsets;
};

bool DecodeNameRepairScalars(absl::string_view text, NameRepairScalars* out) {
  if (out == nullptr) return false;
  out->chars.clear();
  out->byte_offsets.clear();
  for (size_t i = 0; i < text.size();) {
    const size_t begin = i;
    char32_t c = 0;
    if (!DecodeUtf8Char(text, &i, &c)) return false;
    out->chars.push_back(c);
    out->byte_offsets.push_back(begin);
    if (out->chars.size() > 128) return false;
  }
  out->byte_offsets.push_back(text.size());
  return true;
}

bool UniqueNameRepairBoundary(
    const std::vector<std::vector<int>>& forward,
    const std::vector<std::vector<int>>& backward, size_t source_index,
    int distance, size_t* target_index) {
  bool found = false;
  for (size_t j = 0; j < forward[source_index].size(); ++j) {
    // Every node on an optimal edit path satisfies this equality.  If a source
    // boundary has two optimal target positions, there is no safe repair.
    if (forward[source_index][j] + backward[source_index][j] != distance) {
      continue;
    }
    if (found) return false;
    *target_index = j;
    found = true;
  }
  return found;
}

bool TryRepairSelectedSystemName(
    absl::string_view mozc_value, const ProtectedConversionSpan& span,
    std::string* zenz_value) {
  if (zenz_value == nullptr || span.value.empty() ||
      span.source_value_offset == std::string::npos ||
      span.source_value_offset > mozc_value.size() ||
      mozc_value.substr(span.source_value_offset, span.value.size()) !=
          span.value ||
      CountOccurrences(mozc_value, span.value) != 1 ||
      CountOccurrences(*zenz_value, span.value) != 0) {
    return false;
  }

  NameRepairScalars source, target;
  if (!DecodeNameRepairScalars(mozc_value, &source) ||
      !DecodeNameRepairScalars(*zenz_value, &target)) {
    return false;
  }
  const size_t name_end_byte = span.source_value_offset + span.value.size();
  const auto begin = std::find(source.byte_offsets.begin(),
                               source.byte_offsets.end(),
                               span.source_value_offset);
  const auto end = std::find(source.byte_offsets.begin(),
                             source.byte_offsets.end(), name_end_byte);
  if (begin == source.byte_offsets.end() ||
      end == source.byte_offsets.end()) {
    return false;
  }
  const size_t name_begin = begin - source.byte_offsets.begin();
  const size_t name_end = end - source.byte_offsets.begin();
  const size_t name_length = name_end - name_begin;
  if (name_length < 2 || name_length > 16) return false;

  const size_t n = source.chars.size();
  const size_t m = target.chars.size();
  std::vector<std::vector<int>> forward(n + 1,
                                        std::vector<int>(m + 1, 0));
  std::vector<std::vector<int>> backward(n + 1,
                                         std::vector<int>(m + 1, 0));
  for (size_t i = 0; i <= n; ++i) forward[i][0] = static_cast<int>(i);
  for (size_t j = 0; j <= m; ++j) forward[0][j] = static_cast<int>(j);
  for (size_t i = 1; i <= n; ++i) {
    for (size_t j = 1; j <= m; ++j) {
      forward[i][j] = std::min(
          {forward[i - 1][j] + 1, forward[i][j - 1] + 1,
           forward[i - 1][j - 1] +
               (source.chars[i - 1] == target.chars[j - 1] ? 0 : 1)});
    }
  }
  for (size_t i = 0; i <= n; ++i) backward[i][m] = static_cast<int>(n - i);
  for (size_t j = 0; j <= m; ++j) backward[n][j] = static_cast<int>(m - j);
  for (size_t i = n; i-- > 0;) {
    for (size_t j = m; j-- > 0;) {
      backward[i][j] = std::min(
          {backward[i + 1][j] + 1, backward[i][j + 1] + 1,
           backward[i + 1][j + 1] +
               (source.chars[i] == target.chars[j] ? 0 : 1)});
    }
  }
  const int distance = forward[n][m];
  // Conservative initial scope: refuse large, potentially reordered rewrites.
  if (distance > 8) return false;
  size_t target_begin = 0, target_end = 0;
  if (!UniqueNameRepairBoundary(forward, backward, name_begin, distance,
                                &target_begin) ||
      !UniqueNameRepairBoundary(forward, backward, name_end, distance,
                                &target_end) ||
      target_end < target_begin ||
      target_end - target_begin != name_length) {
    return false;
  }

  size_t substitutions = 0;
  size_t equal_chars = 0;
  for (size_t k = 0; k < name_length; ++k) {
    if (source.chars[name_begin + k] == target.chars[target_begin + k]) {
      ++equal_chars;
    } else {
      ++substitutions;
    }
  }
  // Only repair a small spelling change, never reconstruct an arbitrary
  // missing name or insert/delete a different-length name token.
  const size_t allowed_substitutions = name_length >= 5 ? 2 : 1;
  if (substitutions == 0 || substitutions > allowed_substitutions ||
      equal_chars == 0 ||
      forward[name_begin][target_begin] +
              static_cast<int>(substitutions) +
              backward[name_end][target_end] != distance) {
    // Require the character-for-character name substitution itself to lie on
    // an optimal edit path. Otherwise a shifted / overlapping match could
    // accidentally consume the neighboring Japanese text.
    return false;
  }

  std::string repaired(*zenz_value);
  repaired.replace(target.byte_offsets[target_begin],
                   target.byte_offsets[target_end] -
                       target.byte_offsets[target_begin],
                   span.value);
  // A no-op Zenz correction is a rejection, not accepted learning evidence.
  if (repaired == mozc_value ||
      CountOccurrences(repaired, span.value) != 1) {
    return false;
  }
  *zenz_value = std::move(repaired);
  return true;
}

}  // namespace

ProtectedConversionSpan::Tier ClassifyProtectedAsciiSurface(
    absl::string_view surface) {
  if (surface.empty()) {
    return ProtectedConversionSpan::Tier::kIdentityCritical;
  }

  size_t pos = 0;
  if (surface[pos] == '+' || surface[pos] == '-') {
    ++pos;
  }

  const size_t integer_begin = pos;
  while (pos < surface.size() && '0' <= surface[pos] &&
         surface[pos] <= '9') {
    ++pos;
  }
  if (pos == integer_begin) {
    return ProtectedConversionSpan::Tier::kIdentityCritical;
  }
  if (pos == surface.size()) {
    return ProtectedConversionSpan::Tier::kNumericLiteral;
  }

  if (surface[pos] != '.') {
    return ProtectedConversionSpan::Tier::kIdentityCritical;
  }
  ++pos;

  const size_t fraction_begin = pos;
  while (pos < surface.size() && '0' <= surface[pos] &&
         surface[pos] <= '9') {
    ++pos;
  }
  if (pos == surface.size() && pos > fraction_begin) {
    return ProtectedConversionSpan::Tier::kNumericLiteral;
  }
  return ProtectedConversionSpan::Tier::kIdentityCritical;
}

bool ShouldProtectSystemProperNounReadings(
    const std::vector<std::string>& token_readings) {
  if (token_readings.empty()) {
    return false;
  }

  // Minimal high-risk seed list: these are deliberately not permanent
  // bans on Zenz conversion, only exemptions from the new system-name guard.
  // Entries are full lexical readings (not substrings of the full sentence).
  // Curated whole-reading exceptions: common non-name meanings can otherwise
  // become impossible to recover when Mozc selected a proper-name node.
  // The list is intentionally NOT all homophones of proper nouns.  In
  // particular, けん/県 and しょうじ/商事 are intentionally excluded to retain
  // protection of compounds such as 奈良県 and 佐藤商事 under the current
  // all-token exception policy. Review each addition against compound tests.
  constexpr absl::string_view kAmbiguousReadings[] = {
      "あい", "あおい", "あかね", "あかり", "あき", "あきた",
      "あさひ", "あや", "あゆみ", "あゆむ", "あらい", "あんな",
      "いこう", "いずみ", "えり", "おか", "かい", "かいとう",
      "かおり", "かおる", "かける", "かこう", "かすみ", "かとう",
      "かな", "かんじ", "かんせい", "かんだ", "きかん", "きし",
      "きせい", "きた", "きょう", "ぎふ", "くるみ", "けんじ",
      "こう", "こうえい", "こうかん", "こうし", "こうしょう", "こうじ",
      "こうせい", "こうち", "さくら", "さつき", "さとう", "さとる",
      "しおり", "しげる", "しこう", "しずか", "しのぶ", "しみず",
      "しょう", "しょうこ", "しんこう", "しんじ", "じゅん", "すすむ",
      "すずき", "すみれ", "せいか", "せいかく", "せいじ", "せんだい",
      "そら", "たくみ", "たける", "とうこう", "とおる", "なお",
      "なつ", "なみ", "なら", "のぞみ", "のぼる", "はじめ",
      "はな", "はら", "はる", "はるか", "ひかり", "ひかる",
      "ひとみ", "ひなた", "ふじ", "まい", "まこと", "まさる",
      "まもる", "みえ", "みき", "みさき", "みどり", "みなと",
      "みなみ", "みらい", "めい", "めぐみ", "もり", "ゆうき",
      "ゆかり", "ゆき", "ゆめ", "ゆり", "りく", "りょう",
      "れい", "れん", "わたる",
  };
  for (const std::string& reading : token_readings) {
    bool is_exception = false;
    for (absl::string_view exception : kAmbiguousReadings) {
      if (absl::string_view(reading) == exception) {
        is_exception = true;
        break;
      }
    }
    if (!is_exception) {
      return true;
    }
  }
  return false;
}

ZenzProtectedPromptResult ZenzAdoptionPolicy::ProtectPromptKey(
    const ZenzProtectedPromptInput& input) const {
  ZenzProtectedPromptResult result;
  result.key = std::string(input.key);
  result.protected_spans = input.protected_spans;

  std::vector<size_t> order;
  order.reserve(result.protected_spans.size());
  for (size_t i = 0; i < result.protected_spans.size(); ++i) {
    const ProtectedConversionSpan& span = result.protected_spans[i];
    // Prompt-side placeholders are intentionally limited to identity-critical
    // surfaces such as ASCII / mixed-script product names.  Japanese
    // user-dictionary entries are protected on the output side instead: keeping
    // their natural reading in the prompt preserves Zenz's ability to improve
    // the surrounding Japanese text, while Decide() still repairs or rejects
    // boundary changes such as an extra kana attached to the protected surface.
    if (!span.key.empty() && !span.value.empty() &&
        span.tier == ProtectedConversionSpan::Tier::kIdentityCritical) {
      order.push_back(i);
    }
  }

  std::stable_sort(order.begin(), order.end(),
                   [&](size_t lhs, size_t rhs) {
                     return result.protected_spans[lhs].key.size() >
                            result.protected_spans[rhs].key.size();
                   });

  size_t placeholder_index = 0;
  for (const size_t span_index : order) {
    ProtectedConversionSpan& span = result.protected_spans[span_index];
    if (CountOccurrences(result.key, span.key) == 0) {
      continue;
    }

    const std::string placeholder =
        BuildUniqueProtectedPlaceholder(placeholder_index++, result.key);
    const size_t replaced =
        ReplaceAllLiteral(span.key, placeholder, &result.key);
    if (replaced == 0) {
      continue;
    }

    span.placeholder = placeholder;
    if (span.required_occurrences < replaced) {
      span.required_occurrences = replaced;
    }
    result.placeholder_count += replaced;
  }

  return result;
}

std::string ZenzAdoptionPolicy::RestorePlaceholders(
    absl::string_view value,
    const std::vector<ProtectedConversionSpan>& protected_spans) const {
  std::string restored(value);
  for (const ProtectedConversionSpan& span : protected_spans) {
    if (span.placeholder.empty() || span.value.empty()) {
      continue;
    }
    ReplaceAllLiteral(span.placeholder, span.value, &restored);
  }
  return restored;
}

ZenzAdoptionResult ZenzAdoptionPolicy::Decide(
    const ZenzAdoptionInput& input) const {
  std::string adopted_value(input.zenz_value);
  bool protected_surface_repaired = false;
  bool selected_system_name_repaired = false;
  size_t selected_system_name_count = 0;
  for (const ProtectedConversionSpan& candidate : input.protected_spans) {
    if (candidate.tier == ProtectedConversionSpan::Tier::kSystemProperNoun) {
      ++selected_system_name_count;
    }
  }
  // Selected-name locations are stable lexical anchors in the *baseline*.
  // The output position may change when Zenz corrects nearby text, but names
  // must remain in the same relative order.
  std::vector<std::pair<size_t, size_t>> system_name_positions;

  for (const ProtectedConversionSpan& span : input.protected_spans) {
    if (span.value.empty()) {
      continue;
    }

    const size_t required_occurrences =
        span.required_occurrences == 0 ? 1 : span.required_occurrences;

    if (span.tier == ProtectedConversionSpan::Tier::kNumericLiteral) {
      const size_t baseline_occurrences =
          CountOccurrences(input.mozc_value, span.value);
      const size_t adopted_occurrences =
          CountOccurrences(adopted_value, span.value);
      if (baseline_occurrences < required_occurrences ||
          adopted_occurrences != baseline_occurrences) {
        ZenzAdoptionResult result;
        result.action = ZenzAdoptionResult::Action::kReject;
        result.value = std::string(input.mozc_value);
        result.reason = "protected_numeric_literal_not_preserved";
        return result;
      }
      if (!NumericLiteralContextsMatch(input.mozc_value, adopted_value,
                                       span.value)) {
        ZenzAdoptionResult result;
        result.action = ZenzAdoptionResult::Action::kReject;
        result.value = std::string(input.mozc_value);
        result.reason = "protected_numeric_literal_context_changed";
        return result;
      }
      continue;
    }

    if (span.tier == ProtectedConversionSpan::Tier::kSystemProperNoun) {
      // Only for a single, unique selected name: align the original
      // and Zenz text on Unicode boundaries and repair a small,
      // unambiguous local name spelling difference. All other rules
      // (baseline offsets, boundary expansions, orthography) still run.
      if (selected_system_name_count == 1 && required_occurrences == 1 &&
          CountOccurrences(adopted_value, span.value) == 0 &&
          TryRepairSelectedSystemName(input.mozc_value, span,
                                      &adopted_value)) {
        selected_system_name_repaired = true;
        protected_surface_repaired = true;
      }
      // The selected dictionary expression is a lexical unit, not necessarily
      // a character-class-delimited string. Japanese names may be adjacent
      // to other Japanese text on either side. Require exact spelling and
      // uniqueness, rather than punctuation/particle boundaries.
      // Repeated names are excluded by the session-side collector until a
      // positional adoption guard can identify each individual occurrence.
      if (required_occurrences != 1 ||
          CountOccurrences(input.mozc_value, span.value) != 1 ||
          CountOccurrences(adopted_value, span.value) != 1) {
        ZenzAdoptionResult result;
        result.action = ZenzAdoptionResult::Action::kReject;
        result.value = std::string(input.mozc_value);
        result.reason = "protected_system_proper_noun_not_preserved";
        return result;
      }
      if (span.source_value_offset != std::string::npos) {
        const size_t before = span.source_value_offset;
        if (before > input.mozc_value.size() ||
            input.mozc_value.substr(before, span.value.size()) != span.value) {
          ZenzAdoptionResult result;
          result.action = ZenzAdoptionResult::Action::kReject;
          result.value = std::string(input.mozc_value);
          result.reason = "protected_system_proper_noun_stale_offset";
          return result;
        }
        const size_t after = adopted_value.find(span.value);
        const absl::string_view old_right =
            input.mozc_value.substr(before + span.value.size());
        const absl::string_view new_right =
            absl::string_view(adopted_value).substr(after + span.value.size());
        // Reject an added place/company ending if it turns the selected name
        // into a different compound. Unlike a generic character boundary
        // check this still permits adjacent kana and particle corrections.
        constexpr absl::string_view kNewNameEndings[] = {
            "都", "道", "府", "県", "市", "区", "町", "村", "郡", "商事",
        };
        for (absl::string_view ending : kNewNameEndings) {
          if (absl::StartsWith(new_right, ending) &&
              !absl::StartsWith(old_right, ending)) {
            ZenzAdoptionResult result;
            result.action = ZenzAdoptionResult::Action::kReject;
            result.value = std::string(input.mozc_value);
            result.reason = "protected_system_proper_noun_boundary_expanded";
            return result;
          }
        }
        system_name_positions.emplace_back(before, after);
      }
      continue;
    }

    while (CountBoundaryPreservedOccurrences(adopted_value, span.value) <
           required_occurrences) {
      if (TryRepairAttachedProtectedSpan(input.mozc_value, span,
                                         &adopted_value)) {
        protected_surface_repaired = true;
        continue;
      }

      if (CountOccurrences(adopted_value, span.value) < required_occurrences &&
          TryRepairProtectedSpan(span, &adopted_value)) {
        protected_surface_repaired = true;
        continue;
      }

      ZenzAdoptionResult result;
      result.action = ZenzAdoptionResult::Action::kReject;
      result.value = std::string(input.mozc_value);
      result.reason = CountOccurrences(adopted_value, span.value) >=
                              required_occurrences
                          ? "protected_user_dictionary_surface_boundary_changed"
                          : "protected_user_dictionary_surface_not_preserved";
      return result;
    }
  }

  if (system_name_positions.size() > 1) {
    std::sort(system_name_positions.begin(), system_name_positions.end());
    for (size_t i = 1; i < system_name_positions.size(); ++i) {
      if (system_name_positions[i - 1].second >=
          system_name_positions[i].second) {
        ZenzAdoptionResult result;
        result.action = ZenzAdoptionResult::Action::kReject;
        result.value = std::string(input.mozc_value);
        result.reason = "protected_system_proper_noun_order_changed";
        return result;
      }
    }
  }

  bool orthography_repaired = false;
  const bool baseline_segments_match = BaselineSegmentsMatchAdoptionInput(
      input.baseline_segments, input.key, input.mozc_value);
  if (baseline_segments_match) {
    const ZenzSegmentProjection projection = ProjectZenzValueToMozcSegments(
        input.baseline_segments, input.key, adopted_value);

    if (projection.success) {
      std::string orthography_repaired_value;
      for (const ZenzProjectedSegment& segment : projection.segments) {
        if (!segment.changed) {
          orthography_repaired_value.append(segment.zenz_value);
          continue;
        }

        const ZenzOrthographyDecision decision = orthography_policy_.Evaluate(
            segment.mozc_value, segment.zenz_value);
        if (decision.allow) {
          orthography_repaired_value.append(segment.zenz_value);
          continue;
        }

        // An unresolved multi-segment aggregate proves only the outer span;
        // it does not identify which original Mozc segment owns the unsafe
        // orthographic transition.  Repairing the whole aggregate back to the
        // Mozc baseline would report an accepted Zenz result even though none
        // of that ambiguous rewrite was adopted.  Fail closed instead.  Known
        // one-segment boundaries may still be repaired locally below.
        if (!segment.boundary_known) {
          ZenzAdoptionResult result;
          result.action = ZenzAdoptionResult::Action::kReject;
          result.value = std::string(input.mozc_value);
          result.reason = "orthographic_transition_projection_failed";
          return result;
        }

        orthography_repaired_value.append(segment.mozc_value);
        orthography_repaired = true;
      }
      adopted_value = std::move(orthography_repaired_value);
    } else {
      const ZenzOrthographyDecision decision =
          orthography_policy_.Evaluate(input.mozc_value, adopted_value);
      if (!decision.allow) {
        ZenzAdoptionResult result;
        result.action = ZenzAdoptionResult::Action::kReject;
        result.value = std::string(input.mozc_value);
        result.reason = "orthographic_transition_projection_failed";
        return result;
      }
    }
  } else {
    const ZenzOrthographyDecision decision =
        orthography_policy_.Evaluate(input.mozc_value, adopted_value);
    if (!decision.allow) {
      ZenzAdoptionResult result;
      result.action = ZenzAdoptionResult::Action::kReject;
      result.value = std::string(input.mozc_value);
      result.reason = input.baseline_segments.empty()
                          ? "orthographic_transition_missing_baseline_segments"
                          : "orthographic_transition_stale_baseline_segments";
      return result;
    }
  }

  // Reject a clear deletion from the full Mozc surface even when Japanese-
  // only rewrites pass the orthography checks.  Check the final repaired value
  // so that repair cannot accidentally reintroduce a truncated phrase.
  if (ZenzOutputValidator::IsObviousSurfaceTruncation(input.mozc_value,
                                                     adopted_value)) {
    ZenzAdoptionResult result;
    result.action = ZenzAdoptionResult::Action::kReject;
    result.value = std::string(input.mozc_value);
    result.reason = "obvious_surface_truncation";
    return result;
  }

  ZenzAdoptionResult result;
  const bool repaired = protected_surface_repaired || orthography_repaired;
  result.action = repaired ? ZenzAdoptionResult::Action::kAcceptWithRepair
                           : ZenzAdoptionResult::Action::kAcceptAsIs;
  result.value = std::move(adopted_value);
  if (orthography_repaired) {
    result.reason = "orthographic_transition_repaired";
  } else if (selected_system_name_repaired) {
    result.reason = "protected_system_proper_noun_repaired";
  } else if (protected_surface_repaired) {
    result.reason = "protected_user_dictionary_surface_repaired";
  } else {
    result.reason = "accepted";
  }
  return result;
}

}  // namespace mozc::session
