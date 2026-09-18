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

#ifndef MOZC_SESSION_KEY_INFO_UTIL_H_
#define MOZC_SESSION_KEY_INFO_UTIL_H_

#include <vector>

#include "absl/types/span.h"
#include "composer/key_event_util.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"

namespace mozc {

class KeyInfoUtil {
 public:
  KeyInfoUtil() = delete;
  KeyInfoUtil(const KeyInfoUtil&) = delete;
  KeyInfoUtil& operator=(const KeyInfoUtil&) = delete;

  // Returns a sorted list of KeyInformation that is assigned in DIRECT mode.
  static std::vector<KeyInformation> ExtractSortedDirectModeKeys(
      const config::Config& config);

  // Returns a sorted list of keys bound to IMEOff in Direct/DirectInput mode.
  static std::vector<KeyInformation> ExtractSortedDirectModeImeOffKeys(
      const config::Config& config);

  // Returns a sorted list of keys bound to IMEOn in active IME modes.
  static std::vector<KeyInformation> ExtractSortedActiveModeImeOnKeys(
      const config::Config& config);

  // Returns a sorted list of keys bound to IMEOff in active IME modes such as
  // Precomposition, Composition, Conversion, etc. These are the keys that turn
  // the IME off, e.g. a left Alt tap mapped to IMEOff.
  static std::vector<KeyInformation> ExtractSortedActiveModeImeOffKeys(
      const config::Config& config);

  // Returns a sorted list of keys bound to IMEOn in Direct/DirectInput mode.
  // These are the keys that turn the IME on, e.g. a right Alt tap mapped to
  // IMEOn.
  static std::vector<KeyInformation> ExtractSortedDirectModeImeOnKeys(
      const config::Config& config);

  // Returns true if |sorted_keys| contains |key_event|. |sorted_keys| must be
  // sorted.
  static bool ContainsKey(absl::Span<const KeyInformation> sorted_keys,
                          const commands::KeyEvent& key_event);

  // Returns true if |sorted_keys| contains |key_info|. |sorted_keys| must be
  // sorted.
  static bool ContainsKeyInformation(
      absl::Span<const KeyInformation> sorted_keys,
      KeyInformation key_info);
};

}  // namespace mozc

#endif  // MOZC_SESSION_KEY_INFO_UTIL_H_
