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

TEST(TipModeIndicatorKeyTest, NativeImeKeysIgnoreCapsLockToggle) {
  const InputBehavior behavior;
  constexpr KeyInformation kBase = 1;
  constexpr KeyInformation kCaps =
      static_cast<KeyInformation>(commands::KeyEvent::CAPS) << 48;
  constexpr KeyInformation kWithCaps = kBase | kCaps;
  constexpr KeyInformation kCandidates[] = {kWithCaps, kBase};

  EXPECT_TRUE(HasNoModifiers(kBase));
  EXPECT_TRUE(HasNoModifiers(kWithCaps));

  InputState state;
  state.open = true;
  EXPECT_TRUE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_ON), behavior, state, true,
      kWithCaps));
  EXPECT_FALSE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_OFF), behavior, state, true,
      kWithCaps));

  KeyInformation matched_key = 0;
  EXPECT_TRUE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_ON), behavior, state, true,
      kWithCaps, kCandidates, &matched_key));
  EXPECT_EQ(matched_key, kWithCaps);

  state.open = false;
  EXPECT_TRUE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_OFF), behavior, state, true,
      kWithCaps));
  EXPECT_FALSE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_ON), behavior, state, true,
      kWithCaps));

  matched_key = 0;
  EXPECT_TRUE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_IME_OFF), behavior, state, true,
      kWithCaps, kCandidates, &matched_key));
  EXPECT_EQ(matched_key, kWithCaps);
}

TEST(TipModeIndicatorKeyTest, NativeImeKeysStillRejectHeldModifiersWithCaps) {
  const InputBehavior behavior;
  constexpr KeyInformation kBase = 1;
  constexpr KeyInformation kCaps =
      static_cast<KeyInformation>(commands::KeyEvent::CAPS) << 48;

  for (const auto modifier : {commands::KeyEvent::CTRL,
                              commands::KeyEvent::SHIFT,
                              commands::KeyEvent::ALT,
                              commands::KeyEvent::LEFT_CTRL,
                              commands::KeyEvent::RIGHT_SHIFT,
                              commands::KeyEvent::KEY_UP}) {
    const KeyInformation key =
        kBase | kCaps | (static_cast<KeyInformation>(modifier) << 48);
    SCOPED_TRACE(static_cast<int>(modifier));
    EXPECT_FALSE(HasNoModifiers(key));
    InputState state;
    state.open = true;
    EXPECT_FALSE(IsNoOpModeIndicatorKey(
        VirtualKey::FromVirtualKey(VK_IME_ON), behavior, state, true, key));
    state.open = false;
    EXPECT_FALSE(IsNoOpModeIndicatorKey(
        VirtualKey::FromVirtualKey(VK_IME_OFF), behavior, state, true, key));
  }
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

TEST(TipModeIndicatorKeyTest,
     ConfiguredImeStateSelectionKeysUseSideAwareLookupCandidates) {
  constexpr KeyInformation kRightCtrlGenericShift =
      static_cast<KeyInformation>(261) << 48;
  constexpr KeyInformation kRightCtrlBothShifts =
      static_cast<KeyInformation>(1413) << 48;
  constexpr KeyInformation kCtrlBothShifts =
      static_cast<KeyInformation>(1157) << 48;
  constexpr KeyInformation kGenericCtrlShift =
      static_cast<KeyInformation>(5) << 48;
  constexpr KeyInformation kBothShiftCandidates[] = {
      kRightCtrlBothShifts,
      kCtrlBothShifts,
      kRightCtrlGenericShift,
      kGenericCtrlShift,
  };

  InputBehavior behavior;
  behavior.direct_mode_ime_off_keys.push_back(kRightCtrlGenericShift);

  InputState state;
  state.open = false;

  KeyInformation matched_key = 0;
  EXPECT_TRUE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_RSHIFT), behavior, state, true,
      kRightCtrlBothShifts, kBothShiftCandidates, &matched_key));
  EXPECT_EQ(matched_key, kRightCtrlGenericShift);

  // Candidate lookup keeps the configured generic Shift binding discoverable
  // across side-specific physical snapshots.  This verifies semantic identity
  // lookup only; it does not define which TSF key-up event ends a
  // modifier-only chord when both physical Shift keys are held.
  constexpr KeyInformation kRightCtrlLeftShift =
      static_cast<KeyInformation>(389) << 48;
  constexpr KeyInformation kCtrlLeftShiftAfterRelease =
      static_cast<KeyInformation>(133) << 48;
  constexpr KeyInformation kLeftShiftReleaseCandidates[] = {
      kRightCtrlLeftShift,
      kCtrlLeftShiftAfterRelease,
      kRightCtrlGenericShift,
      kGenericCtrlShift,
  };
  KeyInformation release_matched_key = 0;
  constexpr KeyInformation kConfiguredSemanticBinding[] = {
      kRightCtrlGenericShift,
  };
  EXPECT_TRUE(FindConfiguredModeKey(
      kConfiguredSemanticBinding, kLeftShiftReleaseCandidates,
      &release_matched_key));
  EXPECT_EQ(release_matched_key, kRightCtrlGenericShift);

  constexpr KeyInformation kLeftCtrlLeftShift =
      static_cast<KeyInformation>(165) << 48;
  constexpr KeyInformation kCtrlLeftShift =
      static_cast<KeyInformation>(133) << 48;
  constexpr KeyInformation kLeftCtrlGenericShift =
      static_cast<KeyInformation>(37) << 48;
  constexpr KeyInformation kWrongSideCandidates[] = {
      kLeftCtrlLeftShift,
      kCtrlLeftShift,
      kLeftCtrlGenericShift,
      kGenericCtrlShift,
  };

  EXPECT_FALSE(IsNoOpModeIndicatorKey(
      VirtualKey::FromVirtualKey(VK_LSHIFT), behavior, state, true,
      kLeftCtrlLeftShift, kWrongSideCandidates));
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
