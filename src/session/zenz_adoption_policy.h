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

#ifndef MOZC_SESSION_ZENZ_ADOPTION_POLICY_H_
#define MOZC_SESSION_ZENZ_ADOPTION_POLICY_H_

#include <cstddef>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "session/zenz_orthography_policy.h"
#include "session/zenz_segment_projection.h"

namespace mozc::session {

// A surface that normal Mozc live conversion has already selected from a
// user-dictionary candidate.  Zenz live correction is allowed to improve the
// surrounding sentence, but must not silently destroy these surfaces.
struct ProtectedConversionSpan {
  enum class Tier {
    // Product names, handles, ASCII/mixed-script spellings, and other surfaces
    // where the exact spelling is part of the user's intent.
    kIdentityCritical,

    // ASCII numeric literals typed by the user, such as "1", "100", or
    // "3.14".  The exact numeric spelling is preserved, but adjacent Japanese
    // text is allowed to change so Zenz can still correct expressions such as
    // "1代" -> "1台" and surrounding homophones.
    kNumericLiteral,

    // Other user-dictionary surfaces selected by normal Mozc live conversion.
    // They are protected from silent overwrite.  They are not eligible for
    // reading-derived kana replacement, but boundary/attachment repair may be
    // applied when it is locally safe.
    kUserPreferred,
  };

  std::string key;
  std::string value;
  Tier tier = Tier::kUserPreferred;

  // Bounded repair is permitted only for identity-critical surfaces whose
  // reading-derived kana spelling can be found unambiguously in the Zenz output.
  bool repairable = false;

  // Number of occurrences of this surface in the normal Mozc live-conversion
  // value.  Zenz adoption must preserve at least this many occurrences, or
  // repair the missing occurrence when it is safe.
  size_t required_occurrences = 1;

  // Placeholder used only in the Zenz prompt.  When non-empty, the prompt key
  // contains this token instead of key, and the Zenz output is restored to
  // value before validation and adoption.
  std::string placeholder;
};

// Classifies an ASCII identity surface extracted from the visible Mozc
// baseline.  Pure signed integers and decimals are numeric literals; mixed
// technical spellings remain identity-critical.
ProtectedConversionSpan::Tier ClassifyProtectedAsciiSurface(
    absl::string_view surface);

struct ZenzProtectedPromptInput {
  absl::string_view key;
  std::vector<ProtectedConversionSpan> protected_spans;
};

struct ZenzProtectedPromptResult {
  std::string key;
  std::vector<ProtectedConversionSpan> protected_spans;
  size_t placeholder_count = 0;
};

struct ZenzAdoptionInput {
  absl::string_view key;
  absl::string_view mozc_value;
  absl::string_view zenz_value;
  std::vector<ProtectedConversionSpan> protected_spans;
  std::vector<ZenzBaselineSegment> baseline_segments;
};

struct ZenzAdoptionResult {
  enum class Action {
    kAcceptAsIs,
    kAcceptWithRepair,
    kReject,
  };

  Action action = Action::kReject;
  std::string value;
  std::string reason;
};

class ZenzAdoptionPolicy {
 public:
  ZenzProtectedPromptResult ProtectPromptKey(
      const ZenzProtectedPromptInput& input) const;

  std::string RestorePlaceholders(
      absl::string_view value,
      const std::vector<ProtectedConversionSpan>& protected_spans) const;

  ZenzAdoptionResult Decide(const ZenzAdoptionInput& input) const;

 private:
  ZenzOrthographyPolicy orthography_policy_;
};

}  // namespace mozc::session

#endif  // MOZC_SESSION_ZENZ_ADOPTION_POLICY_H_
