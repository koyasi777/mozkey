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

  ZenzAdoptionResult result;
  const bool repaired = protected_surface_repaired || orthography_repaired;
  result.action = repaired ? ZenzAdoptionResult::Action::kAcceptWithRepair
                           : ZenzAdoptionResult::Action::kAcceptAsIs;
  result.value = std::move(adopted_value);
  if (orthography_repaired) {
    result.reason = "orthographic_transition_repaired";
  } else if (protected_surface_repaired) {
    result.reason = "protected_user_dictionary_surface_repaired";
  } else {
    result.reason = "accepted";
  }
  return result;
}

}  // namespace mozc::session
