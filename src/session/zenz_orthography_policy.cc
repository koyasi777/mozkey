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

#include "session/zenz_orthography_policy.h"

#include <cstddef>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/util.h"

namespace mozc::session {
namespace {

// Keep version digits and technical connectors attached to Latin material so
// tokens such as GPT-5, C++, UTF-8, HTTP/2, and Windows11 are compared as one
// orthographic surface.  A token is retained only when it contains a letter,
// so ordinary numeric expressions remain outside this policy.
bool IsAsciiTechnicalTokenChar(const unsigned char c) {
  return (('0' <= c) && (c <= '9')) || (('A' <= c) && (c <= 'Z')) ||
         (('a' <= c) && (c <= 'z')) || c == '_' || c == '-' || c == '.' ||
         c == '+' || c == '#' || c == '/';
}

bool HasAsciiLetter(absl::string_view value) {
  for (const unsigned char c : value) {
    if ((('A' <= c) && (c <= 'Z')) || (('a' <= c) && (c <= 'z'))) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> ExtractAsciiTechnicalTokens(
    absl::string_view value) {
  std::vector<std::string> tokens;
  size_t start = absl::string_view::npos;

  auto flush = [&](size_t end) {
    if (start == absl::string_view::npos || end <= start) {
      start = absl::string_view::npos;
      return;
    }
    const absl::string_view token = value.substr(start, end - start);
    if (HasAsciiLetter(token)) {
      tokens.emplace_back(token);
    }
    start = absl::string_view::npos;
  };

  for (size_t i = 0; i < value.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if (IsAsciiTechnicalTokenChar(c)) {
      if (start == absl::string_view::npos) {
        start = i;
      }
    } else {
      flush(i);
    }
  }
  flush(value.size());
  return tokens;
}

std::vector<std::string> ExtractAlphabetRuns(absl::string_view value) {
  std::vector<std::string> runs;
  std::string current;

  for (ConstChar32Iterator iter(value); !iter.Done(); iter.Next()) {
    const char32_t codepoint = iter.Get();
    if (Util::GetScriptType(codepoint) == Util::ALPHABET) {
      Util::CodepointToUtf8Append(codepoint, &current);
      continue;
    }

    if (!current.empty()) {
      runs.push_back(current);
      current.clear();
    }
  }

  if (!current.empty()) {
    runs.push_back(current);
  }
  return runs;
}

bool StringMultisetsMatch(const std::vector<std::string>& baseline_values,
                          const std::vector<std::string>& candidate_values) {
  if (baseline_values.size() != candidate_values.size()) {
    return false;
  }

  std::vector<bool> consumed(baseline_values.size(), false);

  for (const std::string& candidate_value : candidate_values) {
    bool found = false;
    for (size_t i = 0; i < baseline_values.size(); ++i) {
      if (!consumed[i] && baseline_values[i] == candidate_value) {
        consumed[i] = true;
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

}  // namespace

ZenzOrthographyDecision ZenzOrthographyPolicy::Evaluate(
    absl::string_view mozc_value, absl::string_view candidate_value) const {
  if (!Util::IsValidUtf8(mozc_value) || !Util::IsValidUtf8(candidate_value)) {
    ZenzOrthographyDecision decision;
    decision.allow = false;
    decision.reason = "invalid_utf8";
    return decision;
  }

  if (mozc_value == candidate_value) {
    return {};
  }

  const std::vector<std::string> candidate_runs =
      ExtractAlphabetRuns(candidate_value);
  const std::vector<std::string> baseline_runs = ExtractAlphabetRuns(mozc_value);
  if (!StringMultisetsMatch(baseline_runs, candidate_runs)) {
    ZenzOrthographyDecision decision;
    decision.allow = false;
    decision.reason = "alphabetic_surface_changed";
    return decision;
  }

  const std::vector<std::string> baseline_tokens =
      ExtractAsciiTechnicalTokens(mozc_value);
  const std::vector<std::string> candidate_tokens =
      ExtractAsciiTechnicalTokens(candidate_value);
  if (!StringMultisetsMatch(baseline_tokens, candidate_tokens)) {
    ZenzOrthographyDecision decision;
    decision.allow = false;
    decision.reason = "technical_token_surface_changed";
    return decision;
  }

  return {};
}

}  // namespace mozc::session
