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

#include "composer/key_event_util.h"

#include <cctype>
#include <cstddef>
#include <cstdint>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "protocol/commands.pb.h"

namespace mozc {
namespace {

using ::mozc::commands::KeyEvent;

constexpr uint32_t kAltMask =
    KeyEvent::ALT | KeyEvent::LEFT_ALT | KeyEvent::RIGHT_ALT;
constexpr uint32_t kCtrlMask =
    KeyEvent::CTRL | KeyEvent::LEFT_CTRL | KeyEvent::RIGHT_CTRL;
constexpr uint32_t kShiftMask =
    KeyEvent::SHIFT | KeyEvent::LEFT_SHIFT | KeyEvent::RIGHT_SHIFT;
constexpr uint32_t kCapsMask = KeyEvent::CAPS;

uint32_t Ignore(uint32_t modifiers, uint32_t modifiers_to_be_ignored) {
  return modifiers & ~modifiers_to_be_ignored;
}

bool Any(uint32_t modifiers_to_be_tested, uint32_t modifiers_to_be_queried) {
  return (modifiers_to_be_tested & modifiers_to_be_queried) != 0;
}

bool None(uint32_t modifiers_to_be_tested, uint32_t modifiers_to_be_queried) {
  return !Any(modifiers_to_be_tested, modifiers_to_be_queried);
}

int CountSetBits(uint32_t value) {
  int count = 0;
  while (value != 0) {
    count += value & 1;
    value >>= 1;
  }
  return count;
}

// Returns the set of physical side states accepted by one stored binding.
// Bit 0: no modifier, bit 1: left, bit 2: right, bit 3: both sides.
uint8_t AcceptedPhysicalSideStates(uint32_t modifiers, uint32_t generic,
                                   uint32_t left, uint32_t right) {
  const bool has_generic = (modifiers & generic) != 0;
  const bool has_left = (modifiers & left) != 0;
  const bool has_right = (modifiers & right) != 0;

  if (!has_generic && !has_left && !has_right) {
    return 1u << 0;
  }
  if (has_left && has_right) {
    return 1u << 3;
  }
  if (has_left) {
    return 1u << 1;
  }
  if (has_right) {
    return 1u << 2;
  }

  // Generic Ctrl / Shift / Alt means either side.  A physical event with both
  // sides held also falls back to the generic representation.
  return (1u << 1) | (1u << 2) | (1u << 3);
}

}  // namespace

uint32_t KeyEventUtil::GetModifiers(const KeyEvent& key_event) {
  uint32_t modifiers = 0;
  if (key_event.has_modifiers()) {
    modifiers = key_event.modifiers();
  } else {
    for (const int key : key_event.modifier_keys()) {
      modifiers |= key;
    }
  }
  return modifiers;
}

bool KeyEventUtil::GetKeyInformation(const KeyEvent& key_event,
                                     KeyInformation* key) {
  DCHECK(key);

  const uint16_t modifier_keys = static_cast<uint16_t>(GetModifiers(key_event));
  const uint16_t special_key = key_event.has_special_key()
                                   ? key_event.special_key()
                                   : KeyEvent::NO_SPECIALKEY;
  const uint32_t key_code = key_event.has_key_code() ? key_event.key_code() : 0;

  // Make sure the translation from the obsolete specification.
  // key_code should no longer contain control characters.
  if (0 < key_code && key_code <= 32) {
    return false;
  }

  *key = (static_cast<KeyInformation>(modifier_keys) << 48) |
         (static_cast<KeyInformation>(special_key) << 32) |
         (static_cast<KeyInformation>(key_code));

  return true;
}

absl::InlinedVector<KeyEvent, 9> KeyEventUtil::GetKeyEventLookupCandidates(
    const KeyEvent& key_event) {
  absl::InlinedVector<KeyEvent, 9> candidates;
  absl::InlinedVector<KeyInformation, 9> candidate_keys;

  const auto append_candidate =
      [&candidates, &candidate_keys](const KeyEvent& candidate) {
        KeyInformation key = 0;
        if (!GetKeyInformation(candidate, &key)) {
          return;
        }
        for (const KeyInformation existing_key : candidate_keys) {
          if (existing_key == key) {
            return;
          }
        }
        candidate_keys.push_back(key);
        candidates.push_back(candidate);
      };

  // The most specific Caps-qualified variants must precede the Caps-free
  // fallbacks.  In particular, Caps Ctrl Delete must match a physical
  // Caps LeftCtrl Delete before an unqualified LeftCtrl Delete rule.
  const auto append_side_variants = [&append_candidate](
                                        const KeyEvent& source_event) {
    const uint32_t modifiers = GetModifiers(source_event);
    absl::InlinedVector<uint32_t, 3> side_modifier_masks;
    if ((modifiers & (KeyEvent::LEFT_CTRL | KeyEvent::RIGHT_CTRL)) != 0) {
      side_modifier_masks.push_back(KeyEvent::LEFT_CTRL | KeyEvent::RIGHT_CTRL);
    }
    if ((modifiers & (KeyEvent::LEFT_SHIFT | KeyEvent::RIGHT_SHIFT)) != 0) {
      side_modifier_masks.push_back(KeyEvent::LEFT_SHIFT | KeyEvent::RIGHT_SHIFT);
    }
    if ((modifiers & (KeyEvent::LEFT_ALT | KeyEvent::RIGHT_ALT)) != 0) {
      side_modifier_masks.push_back(KeyEvent::LEFT_ALT | KeyEvent::RIGHT_ALT);
    }

    const uint32_t combination_count = 1u << side_modifier_masks.size();
    for (size_t generalized_count = 0;
         generalized_count <= side_modifier_masks.size();
         ++generalized_count) {
      for (uint32_t subset = 0; subset < combination_count; ++subset) {
        if (CountSetBits(subset) != static_cast<int>(generalized_count)) {
          continue;
        }
        uint32_t remove_modifiers = 0;
        for (size_t i = 0; i < side_modifier_masks.size(); ++i) {
          if ((subset & (1u << i)) != 0) {
            remove_modifiers |= side_modifier_masks[i];
          }
        }
        KeyEvent candidate;
        RemoveModifiers(source_event, remove_modifiers, &candidate);
        append_candidate(candidate);
      }
    }
  };

  // The first candidate is the original event (subset == 0).  Each
  // subsequent candidate drops one or more physical side restrictions.
  append_side_variants(key_event);

  // Normalize CapsLock only when present.  Besides avoiding duplicate work
  // on ordinary keystrokes, this preserves the established alphabetic case
  // flip and keeps Caps-free fallbacks below Caps-qualified bindings.
  if (GetModifiers(key_event) & KeyEvent::CAPS) {
    KeyEvent normalized_key_event;
    RemoveModifiers(key_event, KeyEvent::CAPS, &normalized_key_event);
    if (key_event.has_key_code()) {
      const uint32_t key_code = key_event.key_code();
      if ('A' <= key_code && key_code <= 'Z') {
        normalized_key_event.set_key_code(key_code + ('a' - 'A'));
      } else if ('a' <= key_code && key_code <= 'z') {
        normalized_key_event.set_key_code(key_code + ('A' - 'a'));
      }
    }
    append_side_variants(normalized_key_event);
  }

  return candidates;
}

absl::InlinedVector<KeyInformation, 9>
KeyEventUtil::GetKeyInformationLookupCandidates(const KeyEvent& key_event) {
  absl::InlinedVector<KeyInformation, 9> result;
  for (const KeyEvent& candidate : GetKeyEventLookupCandidates(key_event)) {
    KeyInformation key = 0;
    if (GetKeyInformation(candidate, &key)) {
      result.push_back(key);
    }
  }
  return result;
}

bool KeyEventUtil::KeyBindingPatternsOverlap(const KeyEvent& lhs,
                                             const KeyEvent& rhs) {
  KeyInformation lhs_key = 0;
  KeyInformation rhs_key = 0;
  if (!GetKeyInformation(lhs, &lhs_key) ||
      !GetKeyInformation(rhs, &rhs_key)) {
    return false;
  }

  constexpr KeyInformation kModifierInformationMask =
      static_cast<KeyInformation>(0xFFFF) << 48;
  const uint32_t lhs_modifiers = GetModifiers(lhs);
  const uint32_t rhs_modifiers = GetModifiers(rhs);

  const bool lhs_caps = (lhs_modifiers & KeyEvent::CAPS) != 0;
  const bool rhs_caps = (rhs_modifiers & KeyEvent::CAPS) != 0;
  if (lhs_caps == rhs_caps) {
    // Without a Caps difference the physical key identity must match exactly.
    if ((lhs_key & ~kModifierInformationMask) !=
        (rhs_key & ~kModifierInformationMask)) {
      return false;
    }
  } else {
    // A Caps-qualified binding may overlap an unqualified fallback.  Mirror
    // lookup's CapsLock normalization, including ASCII alphabet case flip;
    // simply ignoring the CAPS modifier bit would create false conflicts.
    const KeyEvent& caps_key_event = lhs_caps ? lhs : rhs;
    const KeyInformation plain_key = lhs_caps ? rhs_key : lhs_key;
    KeyEvent normalized_caps_key_event;
    NormalizeModifiers(caps_key_event, &normalized_caps_key_event);
    KeyInformation normalized_caps_key = 0;
    if (!GetKeyInformation(normalized_caps_key_event, &normalized_caps_key) ||
        (normalized_caps_key & ~kModifierInformationMask) !=
            (plain_key & ~kModifierInformationMask)) {
      return false;
    }
  }
  constexpr uint32_t kSideAwareModifierMask =
      KeyEvent::CTRL | KeyEvent::LEFT_CTRL | KeyEvent::RIGHT_CTRL |
      KeyEvent::SHIFT | KeyEvent::LEFT_SHIFT | KeyEvent::RIGHT_SHIFT |
      KeyEvent::ALT | KeyEvent::LEFT_ALT | KeyEvent::RIGHT_ALT;

  // KEY_DOWN / KEY_UP and future non-side modifiers must agree.  Caps can
  // differ because the Caps-qualified exact match and the normalized fallback
  // are both candidates for a single physical event.
  constexpr uint32_t kIgnorableForOverlap =
      kSideAwareModifierMask | KeyEvent::CAPS;
  if ((lhs_modifiers & ~kIgnorableForOverlap) !=
      (rhs_modifiers & ~kIgnorableForOverlap)) {
    return false;
  }

  const auto family_overlaps = [lhs_modifiers, rhs_modifiers](
                                   uint32_t generic, uint32_t left,
                                   uint32_t right) {
    const uint8_t lhs_states =
        AcceptedPhysicalSideStates(lhs_modifiers, generic, left, right);
    const uint8_t rhs_states =
        AcceptedPhysicalSideStates(rhs_modifiers, generic, left, right);
    return (lhs_states & rhs_states) != 0;
  };

  return family_overlaps(KeyEvent::CTRL, KeyEvent::LEFT_CTRL,
                         KeyEvent::RIGHT_CTRL) &&
         family_overlaps(KeyEvent::SHIFT, KeyEvent::LEFT_SHIFT,
                         KeyEvent::RIGHT_SHIFT) &&
         family_overlaps(KeyEvent::ALT, KeyEvent::LEFT_ALT,
                         KeyEvent::RIGHT_ALT);
}

void KeyEventUtil::NormalizeModifiers(const KeyEvent& key_event,
                                      KeyEvent* new_key_event) {
  DCHECK(new_key_event);

  // CTRL (or ALT, SHIFT) should be set on modifier_keys when
  // LEFT (or RIGHT) ctrl is set.
  // LEFT_CTRL (or others) is not handled on Japanese, so we remove these.
  constexpr uint32_t kIgnorableModifierMask =
      (KeyEvent::CAPS | KeyEvent::LEFT_ALT | KeyEvent::RIGHT_ALT |
       KeyEvent::LEFT_CTRL | KeyEvent::RIGHT_CTRL | KeyEvent::LEFT_SHIFT |
       KeyEvent::RIGHT_SHIFT);

  RemoveModifiers(key_event, kIgnorableModifierMask, new_key_event);

  // Reverts the flip of alphabetical key events caused by CapsLock.
  const uint32_t original_modifiers = GetModifiers(key_event);
  if ((original_modifiers & KeyEvent::CAPS) && key_event.has_key_code()) {
    const uint32_t key_code = key_event.key_code();
    if ('A' <= key_code && key_code <= 'Z') {
      new_key_event->set_key_code(key_code + ('a' - 'A'));
    } else if ('a' <= key_code && key_code <= 'z') {
      new_key_event->set_key_code(key_code + ('A' - 'a'));
    }
  }
}

void KeyEventUtil::NormalizeNumpadKey(const KeyEvent& key_event,
                                      KeyEvent* new_key_event) {
  DCHECK(new_key_event);
  *new_key_event = key_event;

  if (!IsNumpadKey(*new_key_event)) {
    return;
  }
  const KeyEvent::SpecialKey numpad_key = new_key_event->special_key();

  // KeyEvent::SEPARATOR is transformed to Enter.
  if (numpad_key == KeyEvent::SEPARATOR) {
    new_key_event->set_special_key(KeyEvent::ENTER);
    return;
  }

  new_key_event->clear_special_key();

  // Handles number keys
  if (KeyEvent::NUMPAD0 <= numpad_key && numpad_key <= KeyEvent::NUMPAD9) {
    new_key_event->set_key_code(
        static_cast<uint32_t>('0' + (numpad_key - KeyEvent::NUMPAD0)));
    return;
  }

  char new_key_code;
  switch (numpad_key) {
    case KeyEvent::MULTIPLY:
      new_key_code = '*';
      break;
    case KeyEvent::ADD:
      new_key_code = '+';
      break;
    case KeyEvent::SUBTRACT:
      new_key_code = '-';
      break;
    case KeyEvent::DECIMAL:
      new_key_code = '.';
      break;
    case KeyEvent::DIVIDE:
      new_key_code = '/';
      break;
    case KeyEvent::EQUALS:
      new_key_code = '=';
      break;
    case KeyEvent::COMMA:
      new_key_code = ',';
      break;
    default:
      LOG(ERROR) << "Unexpected numpad key: " << numpad_key;
      return;
  }

  new_key_event->set_key_code(static_cast<uint32_t>(new_key_code));
}

void KeyEventUtil::RemoveModifiers(const KeyEvent& key_event,
                                   uint32_t remove_modifiers,
                                   KeyEvent* new_key_event) {
  DCHECK(new_key_event);
  *new_key_event = key_event;

  if (HasAlt(remove_modifiers)) {
    remove_modifiers |= KeyEvent::LEFT_ALT | KeyEvent::RIGHT_ALT;
  }
  if (HasCtrl(remove_modifiers)) {
    remove_modifiers |= KeyEvent::LEFT_CTRL | KeyEvent::RIGHT_CTRL;
  }
  if (HasShift(remove_modifiers)) {
    remove_modifiers |= KeyEvent::LEFT_SHIFT | KeyEvent::RIGHT_SHIFT;
  }

  new_key_event->clear_modifier_keys();
  for (size_t i = 0; i < key_event.modifier_keys_size(); ++i) {
    const KeyEvent::ModifierKey mod_key = key_event.modifier_keys(i);
    if (!(remove_modifiers & mod_key)) {
      new_key_event->add_modifier_keys(mod_key);
    }
  }
}

bool KeyEventUtil::MaybeGetKeyStub(const KeyEvent& key_event,
                                   KeyInformation* key) {
  DCHECK(key);

  // If any modifier keys were pressed, this function does nothing.
  if (KeyEventUtil::GetModifiers(key_event) != 0) {
    return false;
  }

  // No stub rule is supported for special keys yet.
  if (key_event.has_special_key()) {
    return false;
  }

  // Check if both key_code and key_string are invalid.
  if ((!key_event.has_key_code() || key_event.key_code() <= 32) &&
      (!key_event.has_key_string() || key_event.key_string().empty())) {
    return false;
  }

  KeyEvent stub_key_event;
  stub_key_event.set_special_key(KeyEvent::TEXT_INPUT);
  if (!GetKeyInformation(stub_key_event, key)) {
    return false;
  }

  return true;
}

bool KeyEventUtil::HasAlt(uint32_t modifiers) {
  return Any(modifiers, kAltMask);
}

bool KeyEventUtil::HasCtrl(uint32_t modifiers) {
  return Any(modifiers, kCtrlMask);
}

bool KeyEventUtil::HasShift(uint32_t modifiers) {
  return Any(modifiers, kShiftMask);
}

bool KeyEventUtil::HasCaps(uint32_t modifiers) {
  return Any(modifiers, kCapsMask);
}

bool KeyEventUtil::IsAlt(uint32_t modifiers) {
  if (!HasAlt(modifiers)) {
    return false;
  }
  return None(Ignore(modifiers, kCapsMask), ~kAltMask);
}

bool KeyEventUtil::IsCtrl(uint32_t modifiers) {
  if (!HasCtrl(modifiers)) {
    return false;
  }
  return None(Ignore(modifiers, kCapsMask), ~kCtrlMask);
}

bool KeyEventUtil::IsShift(uint32_t modifiers) {
  if (!HasShift(modifiers)) {
    return false;
  }
  return None(Ignore(modifiers, kCapsMask), ~kShiftMask);
}

bool KeyEventUtil::IsAltCtrl(uint32_t modifiers) {
  if (!HasAlt(modifiers) || !HasCtrl(modifiers)) {
    return false;
  }
  return None(Ignore(modifiers, kCapsMask), ~(kAltMask | kCtrlMask));
}

bool KeyEventUtil::IsAltShift(uint32_t modifiers) {
  if (!HasAlt(modifiers) || !HasShift(modifiers)) {
    return false;
  }
  return None(Ignore(modifiers, kCapsMask), ~(kAltMask | kShiftMask));
}

bool KeyEventUtil::IsCtrlShift(uint32_t modifiers) {
  if (!HasCtrl(modifiers) || !HasShift(modifiers)) {
    return false;
  }
  return None(Ignore(modifiers, kCapsMask), ~(kCtrlMask | kShiftMask));
}

bool KeyEventUtil::IsAltCtrlShift(uint32_t modifiers) {
  if (!HasAlt(modifiers) || !HasCtrl(modifiers) || !HasShift(modifiers)) {
    return false;
  }
  const auto kAltCtrlShiftMask = kAltMask | kCtrlMask | kShiftMask;
  return None(Ignore(modifiers, kCapsMask), ~kAltCtrlShiftMask);
}

bool KeyEventUtil::IsLowerAlphabet(const KeyEvent& key_event) {
  if (!key_event.has_key_code()) {
    return false;
  }

  const uint32_t key_code = key_event.key_code();
  const uint32_t modifier_keys = GetModifiers(key_event);
  const bool change_case = (HasShift(modifier_keys) != HasCaps(modifier_keys));

  if (change_case) {
    return isupper(key_code) != 0;
  } else {
    return islower(key_code) != 0;
  }
}

bool KeyEventUtil::IsUpperAlphabet(const KeyEvent& key_event) {
  if (!key_event.has_key_code()) {
    return false;
  }

  const uint32_t key_code = key_event.key_code();
  const uint32_t modifier_keys = GetModifiers(key_event);
  const bool change_case = (HasShift(modifier_keys) != HasCaps(modifier_keys));

  if (change_case) {
    return islower(key_code) != 0;
  } else {
    return isupper(key_code) != 0;
  }
}

bool KeyEventUtil::IsNumpadKey(const KeyEvent& key_event) {
  if (!key_event.has_special_key()) {
    return false;
  }

  const KeyEvent::SpecialKey special_key = key_event.special_key();
  if (KeyEvent::NUMPAD0 <= special_key && special_key <= KeyEvent::EQUALS) {
    return true;
  }
  if (special_key == KeyEvent::COMMA) {
    return true;
  }
  return false;
}

}  // namespace mozc
