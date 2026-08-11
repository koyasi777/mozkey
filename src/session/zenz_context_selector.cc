#include "session/zenz_context_selector.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/util.h"

namespace mozc {
namespace session {
namespace {

std::vector<char32_t> ToCodepoints(
    const absl::string_view text) {
  std::vector<char32_t> codepoints;

  for (ConstChar32Iterator iter(text);
       !iter.Done();
       iter.Next()) {
    codepoints.push_back(iter.Get());
  }

  return codepoints;
}

bool IsHorizontalBlankSpace(
    const char32_t codepoint) {
  return codepoint == U' ' ||
         codepoint == U'\t' ||
         codepoint == U'　';
}

size_t LogicalLineBreakLength(
    const std::vector<char32_t>& codepoints,
    const size_t index) {
  if (index >= codepoints.size()) {
    return 0;
  }

  if (codepoints[index] == U'\r') {
    if (index + 1 < codepoints.size() &&
        codepoints[index + 1] == U'\n') {
      return 2;
    }

    return 1;
  }

  if (codepoints[index] == U'\n') {
    return 1;
  }

  return 0;
}

// Returns the first character position of the current paragraph.
//
// A blank line is defined as two logical line breaks with only horizontal
// blank space between them. CRLF counts as one logical line break.
size_t FindCurrentParagraphStart(
    const std::vector<char32_t>& codepoints) {
  size_t paragraph_start = 0;
  size_t i = 0;

  while (i < codepoints.size()) {
    const size_t first_break_len =
        LogicalLineBreakLength(
            codepoints,
            i);

    if (first_break_len == 0) {
      ++i;
      continue;
    }

    size_t j = i + first_break_len;

    while (j < codepoints.size() &&
           IsHorizontalBlankSpace(
               codepoints[j])) {
      ++j;
    }

    const size_t second_break_len =
        LogicalLineBreakLength(
            codepoints,
            j);

    if (second_break_len == 0) {
      i += first_break_len;
      continue;
    }

    j += second_break_len;

    // Consume additional empty-line material and indentation after the strong
    // boundary so the selected paragraph does not begin with blank lines.
    while (j < codepoints.size()) {
      if (IsHorizontalBlankSpace(
              codepoints[j])) {
        ++j;
        continue;
      }

      const size_t extra_break_len =
          LogicalLineBreakLength(
              codepoints,
              j);

      if (extra_break_len != 0) {
        j += extra_break_len;
        continue;
      }

      break;
    }

    paragraph_start = j;
    i = j;
  }

  return paragraph_start;
}

// Returns the character position immediately before the first strong
// paragraph boundary. If no boundary exists, returns codepoints.size().
size_t FindFirstParagraphBoundary(
    const std::vector<char32_t>& codepoints) {
  size_t i = 0;

  while (i < codepoints.size()) {
    const size_t first_break_len =
        LogicalLineBreakLength(
            codepoints,
            i);

    if (first_break_len == 0) {
      ++i;
      continue;
    }

    size_t j = i + first_break_len;

    while (j < codepoints.size() &&
           IsHorizontalBlankSpace(
               codepoints[j])) {
      ++j;
    }

    if (LogicalLineBreakLength(
            codepoints,
            j) != 0) {
      return i;
    }

    i += first_break_len;
  }

  return codepoints.size();
}

bool IsSentenceTerminator(
    const char32_t codepoint) {
  switch (codepoint) {
    case U'。':
    case U'！':
    case U'？':
    case U'!':
    case U'?':
      return true;

    default:
      return false;
  }
}

bool IsSentenceTrailingCloser(
    const char32_t codepoint) {
  switch (codepoint) {
    case U'」':
    case U'』':
    case U'）':
    case U'］':
    case U'】':
    case U'〉':
    case U'》':
    case U'”':
    case U'’':
    case U')':
    case U']':
    case U'"':
      return true;

    default:
      return false;
  }
}

// Finds the end position of the first sentence, capped by |limit|.
//
// Repeated terminators such as "！？" and immediately following closing
// quotation/bracket characters are retained.
size_t FindFirstSentenceEnd(
    const std::vector<char32_t>& codepoints,
    const size_t limit) {
  for (size_t i = 0; i < limit; ++i) {
    if (!IsSentenceTerminator(
            codepoints[i])) {
      continue;
    }

    size_t end = i + 1;

    while (end < limit &&
           (IsSentenceTerminator(
                codepoints[end]) ||
            IsSentenceTrailingCloser(
                codepoints[end]))) {
      ++end;
    }

    return end;
  }

  return limit;
}

}  // namespace

std::string ZenzContextSelector::SelectLeft(
    const absl::string_view text,
    const size_t max_chars) const {
  if (text.empty() || max_chars == 0) {
    return "";
  }

  const std::vector<char32_t> codepoints =
      ToCodepoints(text);

  if (codepoints.empty()) {
    return "";
  }

  const size_t paragraph_start =
      FindCurrentParagraphStart(
          codepoints);

  const size_t available_chars =
      codepoints.size() - paragraph_start;

  const size_t selected_start =
      available_chars <= max_chars
          ? paragraph_start
          : codepoints.size() - max_chars;

  const size_t selected_chars =
      codepoints.size() - selected_start;

  return std::string(
      Util::Utf8SubString(
          text,
          selected_start,
          selected_chars));
}

std::string ZenzContextSelector::SelectRight(
    const absl::string_view text,
    const size_t max_chars) const {
  if (text.empty() || max_chars == 0) {
    return "";
  }

  const std::vector<char32_t> codepoints =
      ToCodepoints(text);

  if (codepoints.empty()) {
    return "";
  }

  // Preserve the conservative interpretation of an immediate line break.
  // Horizontal whitespace at the end of the current line does not make the
  // next logical line a continuation of the cursor position.
  size_t leading_blank_end = 0;

  while (leading_blank_end <
             codepoints.size() &&
         IsHorizontalBlankSpace(
             codepoints[leading_blank_end])) {
    ++leading_blank_end;
  }

  if (LogicalLineBreakLength(
          codepoints,
          leading_blank_end) != 0) {
    return "";
  }

  const size_t paragraph_limit =
      FindFirstParagraphBoundary(
          codepoints);

  const size_t sentence_limit =
      FindFirstSentenceEnd(
          codepoints,
          paragraph_limit);

  const size_t selected_chars =
      std::min(
          max_chars,
          sentence_limit);

  if (selected_chars == 0) {
    return "";
  }

  return std::string(
      Util::Utf8SubString(
          text,
          0,
          selected_chars));
}

}  // namespace session
}  // namespace mozc
