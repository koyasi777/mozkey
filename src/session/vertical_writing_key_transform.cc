// Copyright 2026
// Licensed under the same terms as Mozc.

#include "session/vertical_writing_key_transform.h"

#include <cstdint>

#include "composer/key_event_util.h"
#include "protocol/commands.pb.h"
#include "session/keymap.h"

namespace mozc {
namespace session {
namespace {

using SpecialKey = commands::KeyEvent::SpecialKey;

commands::KeyEvent MakeLookupKey(SpecialKey special_key) {
  commands::KeyEvent key;
  key.set_special_key(special_key);
  return key;
}

bool IsPlainSpecialKey(const commands::KeyEvent& key,
                       SpecialKey special_key) {
  return key.has_special_key() && key.special_key() == special_key &&
         KeyEventUtil::GetModifiers(key) == 0 && !key.has_key_code() &&
         !key.has_key_string();
}

bool IsShiftOnlySpecialKey(const commands::KeyEvent& key,
                           SpecialKey special_key) {
  constexpr uint32_t kShiftMask =
      commands::KeyEvent::SHIFT | commands::KeyEvent::LEFT_SHIFT |
      commands::KeyEvent::RIGHT_SHIFT;
  const uint32_t modifiers = KeyEventUtil::GetModifiers(key);
  return key.has_special_key() && key.special_key() == special_key &&
         (modifiers & kShiftMask) != 0 && (modifiers & ~kShiftMask) == 0 &&
         !key.has_key_code() && !key.has_key_string();
}

bool GetConversionCommand(const keymap::KeyMapManager& keymap,
                          SpecialKey special_key,
                          keymap::ConversionState::Commands* command) {
  const commands::KeyEvent lookup_key = MakeLookupKey(special_key);
  return keymap.GetCommandConversion(lookup_key, command);
}

bool HasConventionalVerticalCandidateContract(
    const keymap::KeyMapManager& keymap) {
  keymap::ConversionState::Commands down = keymap::ConversionState::NONE;
  keymap::ConversionState::Commands up = keymap::ConversionState::NONE;
  keymap::ConversionState::Commands left = keymap::ConversionState::NONE;
  keymap::ConversionState::Commands right = keymap::ConversionState::NONE;

  return GetConversionCommand(keymap, commands::KeyEvent::DOWN, &down) &&
         down == keymap::ConversionState::PREDICT_AND_CONVERT &&
         GetConversionCommand(keymap, commands::KeyEvent::UP, &up) &&
         up == keymap::ConversionState::CONVERT_PREV &&
         GetConversionCommand(keymap, commands::KeyEvent::LEFT, &left) &&
         left == keymap::ConversionState::SEGMENT_FOCUS_LEFT &&
         GetConversionCommand(keymap, commands::KeyEvent::RIGHT, &right) &&
         right == keymap::ConversionState::SEGMENT_FOCUS_RIGHT;
}

bool HasVerticalSegmentWidthContract(
    const keymap::KeyMapManager& keymap,
    const commands::KeyEvent& shifted_arrow) {
  commands::KeyEvent shrink_key = shifted_arrow;
  shrink_key.set_special_key(commands::KeyEvent::LEFT);
  commands::KeyEvent expand_key = shifted_arrow;
  expand_key.set_special_key(commands::KeyEvent::RIGHT);

  keymap::ConversionState::Commands shrink = keymap::ConversionState::NONE;
  keymap::ConversionState::Commands expand = keymap::ConversionState::NONE;
  return keymap.GetCommandConversion(shrink_key, &shrink) &&
         shrink == keymap::ConversionState::SEGMENT_WIDTH_SHRINK &&
         keymap.GetCommandConversion(expand_key, &expand) &&
         expand == keymap::ConversionState::SEGMENT_WIDTH_EXPAND;
}

bool CurrentStateAcceptsLogicalKey(VerticalWritingKeyState state,
                                   SpecialKey logical_key,
                                   const keymap::KeyMapManager& keymap) {
  const commands::KeyEvent lookup_key = MakeLookupKey(logical_key);
  keymap::ConversionState::Commands command =
      keymap::ConversionState::NONE;

  const bool found =
      state == VerticalWritingKeyState::kPrediction
          ? keymap.GetCommandPrediction(lookup_key, &command)
          : keymap.GetCommandConversion(lookup_key, &command);
  if (!found) {
    return false;
  }

  switch (logical_key) {
    case commands::KeyEvent::DOWN:
      return command == keymap::ConversionState::PREDICT_AND_CONVERT ||
             command == keymap::ConversionState::CONVERT_NEXT;
    case commands::KeyEvent::UP:
      return command == keymap::ConversionState::CONVERT_PREV;
    case commands::KeyEvent::LEFT:
      return command == keymap::ConversionState::SEGMENT_FOCUS_LEFT;
    case commands::KeyEvent::RIGHT:
      return command == keymap::ConversionState::SEGMENT_FOCUS_RIGHT;
    default:
      return false;
  }
}

}  // namespace

bool TransformVerticalWritingCandidateArrowKey(
    bool vertical_writing, VerticalWritingKeyState state,
    const keymap::KeyMapManager& keymap, commands::KeyEvent* key) {
  if (!vertical_writing || key == nullptr) {
    return false;
  }

  // In Japanese vertical writing, text inside a segment advances top-to-bottom.
  // Reuse the user's existing horizontal segment-width commands rather than
  // introducing new commands or rewriting the keymap:
  //
  //   physical Shift+Up   -> logical Shift+Left  -> SegmentWidthShrink
  //   physical Shift+Down -> logical Shift+Right -> SegmentWidthExpand
  //
  // Keep Shift+Left/Right themselves unchanged as compatibility bindings.
  // The pair is enabled only when the active keymap actually maps the logical
  // Shift+Left/Right pair to shrink/expand. This automatically excludes ATOK's
  // different arrow contract and respects compatible custom keymaps.
  if (state == VerticalWritingKeyState::kConversion &&
      (IsShiftOnlySpecialKey(*key, commands::KeyEvent::UP) ||
       IsShiftOnlySpecialKey(*key, commands::KeyEvent::DOWN)) &&
      HasVerticalSegmentWidthContract(keymap, *key)) {
    key->set_special_key(
        key->special_key() == commands::KeyEvent::UP
            ? commands::KeyEvent::LEFT
            : commands::KeyEvent::RIGHT);
    return true;
  }

  if (!HasConventionalVerticalCandidateContract(keymap)) {
    return false;
  }

  if (state == VerticalWritingKeyState::kSuggestion) {
    // A suggestion is not focused yet. Left is the visual "enter candidates /
    // advance leftward" operation in vertical writing. Reuse the existing Down
    // binding so PredictAndConvert keeps all current live-conversion behavior.
    if (!IsPlainSpecialKey(*key, commands::KeyEvent::LEFT)) {
      return false;
    }

    const commands::KeyEvent lookup_key =
        MakeLookupKey(commands::KeyEvent::DOWN);
    keymap::CompositionState::Commands command =
        keymap::CompositionState::NONE;
    if (!keymap.GetCommandSuggestion(lookup_key, &command) ||
        command != keymap::CompositionState::PREDICT_AND_CONVERT) {
      return false;
    }

    key->set_special_key(commands::KeyEvent::DOWN);
    return true;
  }

  if (state != VerticalWritingKeyState::kConversion &&
      state != VerticalWritingKeyState::kPrediction) {
    return false;
  }

  SpecialKey logical_key = commands::KeyEvent::NO_SPECIALKEY;
  if (IsPlainSpecialKey(*key, commands::KeyEvent::LEFT)) {
    // Candidate columns advance from right to left.
    logical_key = commands::KeyEvent::DOWN;
  } else if (IsPlainSpecialKey(*key, commands::KeyEvent::RIGHT)) {
    logical_key = commands::KeyEvent::UP;
  } else if (IsPlainSpecialKey(*key, commands::KeyEvent::UP)) {
    // Text/segment flow is top to bottom.
    logical_key = commands::KeyEvent::LEFT;
  } else if (IsPlainSpecialKey(*key, commands::KeyEvent::DOWN)) {
    logical_key = commands::KeyEvent::RIGHT;
  } else {
    return false;
  }

  if (!CurrentStateAcceptsLogicalKey(state, logical_key, keymap)) {
    return false;
  }

  key->set_special_key(logical_key);
  return true;
}

}  // namespace session
}  // namespace mozc