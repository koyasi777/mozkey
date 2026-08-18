// Copyright 2026
// Licensed under the same terms as Mozc.

#ifndef MOZC_SESSION_VERTICAL_WRITING_KEY_TRANSFORM_H_
#define MOZC_SESSION_VERTICAL_WRITING_KEY_TRANSFORM_H_

#include "protocol/commands.pb.h"
#include "session/keymap.h"

namespace mozc {
namespace session {

// Only candidate-related states are transformed in Commit 15. Ordinary
// composition cursor movement is intentionally outside this contract.
enum class VerticalWritingKeyState {
  kOther = 0,
  kSuggestion,
  kConversion,
  kPrediction,
};

// Transforms candidate-navigation arrows, and vertical-writing segment-width
// Shift+Up/Down, to logical keys whose existing keymap commands match Japanese
// vertical-writing geometry.
//
// The transform is deliberately keymap-aware. Candidate navigation is enabled
// only for keymaps whose ordinary Conversion arrows have the MS-IME/Kotoeri
// semantic contract:
//   Down  = PredictAndConvert
//   Up    = ConvertPrev
//   Left  = SegmentFocusLeft
//   Right = SegmentFocusRight
//
// Segment-width Shift+Up/Down is independently enabled only when the active
// keymap maps Shift+Left/Right to SegmentWidthShrink/SegmentWidthExpand.
// Consequently ATOK-style bindings are preserved. Returns true only when the
// key itself was changed.
bool TransformVerticalWritingCandidateArrowKey(
    bool vertical_writing, VerticalWritingKeyState state,
    const keymap::KeyMapManager& keymap, commands::KeyEvent* key);

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_VERTICAL_WRITING_KEY_TRANSFORM_H_