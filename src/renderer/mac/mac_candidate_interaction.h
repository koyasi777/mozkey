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
#ifndef MOZC_RENDERER_MAC_MAC_CANDIDATE_INTERACTION_H_
#define MOZC_RENDERER_MAC_MAC_CANDIDATE_INTERACTION_H_

#include <cstdint>
#include <optional>
#include <string>

#include "protocol/candidate_window.pb.h"

namespace mozc::renderer::mac {

struct PassiveSuggestionCandidateIdentity {
  int32_t id = 0;
  std::string value;
};

bool IsPassiveSuggestionCandidateWindow(
    const commands::CandidateWindow& candidate_window);

std::optional<PassiveSuggestionCandidateIdentity>
GetPassiveSuggestionCandidateIdentity(
    const commands::CandidateWindow& candidate_window,
    int candidate_index);

bool ContainsPassiveSuggestionCandidateIdentity(
    const commands::CandidateWindow& candidate_window,
    const PassiveSuggestionCandidateIdentity& identity);

}  // namespace mozc::renderer::mac

#endif  // MOZC_RENDERER_MAC_MAC_CANDIDATE_INTERACTION_H_