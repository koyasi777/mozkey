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

#include "win32/tip/tip_mode_indicator_key.h"

#include <windows.h>

#include "testing/gunit.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

TEST(TipModeIndicatorKeyTest, NativeImeStateSelectionKeys) {
  const InputBehavior behavior;
  constexpr bool kHasKeyInformation = true;
  constexpr KeyInformation kKeyInformation = 1;

  InputState state;
  state.open = true;
  EXPECT_TRUE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_ON), behavior, state,
      kHasKeyInformation, kKeyInformation));
  EXPECT_FALSE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_OFF), behavior, state,
      kHasKeyInformation, kKeyInformation));

  state.open = false;
  EXPECT_TRUE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_OFF), behavior, state,
      kHasKeyInformation, kKeyInformation));
  EXPECT_FALSE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_ON), behavior, state,
      kHasKeyInformation, kKeyInformation));
}

TEST(TipModeIndicatorKeyTest, NativeImeStateSelectionKeysRequireNoModifiers) {
  const InputBehavior behavior;
  constexpr KeyInformation kModifiedKeyInformation =
      (static_cast<KeyInformation>(1) << 48) | 1;

  InputState state;
  state.open = true;
  EXPECT_FALSE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_ON), behavior, state, true,
      kModifiedKeyInformation));

  state.open = false;
  EXPECT_FALSE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_OFF), behavior, state, true,
      kModifiedKeyInformation));
}

TEST(TipModeIndicatorKeyTest, ConfiguredImeStateSelectionKeysRemainSupported) {
  InputBehavior behavior;
  behavior.active_mode_ime_on_keys.push_back(10);
  behavior.direct_mode_ime_off_keys.push_back(20);

  InputState state;
  state.open = true;
  EXPECT_TRUE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_F1), behavior, state, true, 10));
  EXPECT_FALSE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_F1), behavior, state, true, 20));

  state.open = false;
  EXPECT_TRUE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_F1), behavior, state, true, 20));
  EXPECT_FALSE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_F1), behavior, state, true, 10));
}

TEST(TipModeIndicatorKeyTest,
     ConfiguredImeStateSelectionKeysMayContainModifiers) {
  constexpr KeyInformation kModifiedKeyInformation =
      (static_cast<KeyInformation>(1) << 48) | 30;

  InputBehavior behavior;
  behavior.active_mode_ime_on_keys.push_back(kModifiedKeyInformation);

  InputState state;
  state.open = true;
  EXPECT_TRUE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_F1), behavior, state, true,
      kModifiedKeyInformation));
}

TEST(TipModeIndicatorKeyTest, RequiresKeyInformation) {
  const InputBehavior behavior;
  InputState state;
  state.open = false;

  EXPECT_FALSE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_OFF), behavior, state, false, 0));
}

}  // namespace
}  // namespace tsf
}  // namespace win32
}  // namespace mozc
