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
#include "renderer/mac/mac_candidate_interaction.h"

#include "protocol/commands.pb.h"

namespace mozc::renderer::mac {

bool IsPassiveSuggestionCandidateWindow(
    const commands::CandidateWindow& candidate_window) {
  return candidate_window.has_category() &&
         candidate_window.category() == commands::SUGGESTION &&
         candidate_window.candidate_size() > 0 &&
         !candidate_window.has_focused_index();
}

std::optional<PassiveSuggestionCandidateIdentity>
GetPassiveSuggestionCandidateIdentity(
    const commands::CandidateWindow& candidate_window,
    int candidate_index) {
  if (!IsPassiveSuggestionCandidateWindow(candidate_window) ||
      candidate_index < 0 ||
      candidate_index >= candidate_window.candidate_size()) {
    return std::nullopt;
  }
  const auto& candidate = candidate_window.candidate(candidate_index);
  if (!candidate.has_id()) {
    return std::nullopt;
  }
  return PassiveSuggestionCandidateIdentity{
      .id = candidate.id(),
      .value = candidate.value(),
  };
}

bool ContainsPassiveSuggestionCandidateIdentity(
    const commands::CandidateWindow& candidate_window,
    const PassiveSuggestionCandidateIdentity& identity) {
  if (!IsPassiveSuggestionCandidateWindow(candidate_window)) {
    return false;
  }
  for (const auto& candidate : candidate_window.candidate()) {
    if (candidate.has_id() &&
        candidate.id() == identity.id &&
        candidate.value() == identity.value) {
      return true;
    }
  }
  return false;
}

}  // namespace mozc::renderer::mac