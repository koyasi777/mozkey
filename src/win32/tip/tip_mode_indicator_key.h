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

#ifndef MOZC_WIN32_TIP_TIP_MODE_INDICATOR_KEY_H_
#define MOZC_WIN32_TIP_TIP_MODE_INDICATOR_KEY_H_

#include <windows.h>

#include <algorithm>

#include "win32/base/input_state.h"
#include "win32/base/keyboard.h"

namespace mozc {
namespace win32 {
namespace tsf {

inline bool HasNoModifiers(KeyInformation key_information) {
  // KeyInformation is |Modifiers(16)|SpecialKey(16)|Unicode(32)|.
  return (key_information >> 48) == 0;
}

// Returns true when |key| explicitly requests the IME state that is already
// active. User-configured IMEOn/IMEOff bindings and the dedicated Windows
// VK_IME_ON/VK_IME_OFF state-selection keys share the same semantics here.
inline bool IsNoOpModeIndicatorKey(const VirtualKey& key,
                                   const InputBehavior& behavior,
                                   const InputState& current_state,
                                   bool has_key_information,
                                   KeyInformation key_information) {
  if (!has_key_information) {
    return false;
  }

  if (current_state.open) {
    if (key.virtual_key() == VK_IME_ON &&
        HasNoModifiers(key_information)) {
      return true;
    }
    return std::binary_search(behavior.active_mode_ime_on_keys.begin(),
                              behavior.active_mode_ime_on_keys.end(),
                              key_information);
  }

  if (key.virtual_key() == VK_IME_OFF &&
      HasNoModifiers(key_information)) {
    return true;
  }
  return std::binary_search(behavior.direct_mode_ime_off_keys.begin(),
                            behavior.direct_mode_ime_off_keys.end(),
                            key_information);
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_TIP_TIP_MODE_INDICATOR_KEY_H_
