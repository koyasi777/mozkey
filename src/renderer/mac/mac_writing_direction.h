// Copyright 2026, Mozkey authors.
// All rights reserved.

#ifndef MOZC_RENDERER_MAC_MAC_WRITING_DIRECTION_H_
#define MOZC_RENDERER_MAC_MAC_WRITING_DIRECTION_H_

#include "protocol/renderer_command.pb.h"

namespace mozc {
namespace renderer {
namespace mac {

// Physical writing direction of the active host composition.
//
// This is intentionally independent from CandidateWindow::direction(), which is
// only a server hint for arranging candidate items inside a candidate window.
enum class WritingDirection {
  kHorizontal,
  kVertical,
};

// Resolves the host composition's physical writing direction.
//
// Priority:
//   1. Explicit ApplicationInfo::composition_target::vertical_writing.
//   2. Historical preedit-rectangle shape heuristic for compatibility.
//   3. Horizontal when neither source is available.
//
// An explicit false value is meaningful and must override the geometry
// heuristic.
WritingDirection ResolveWritingDirection(
    const commands::RendererCommand& command);

inline bool IsVerticalWriting(WritingDirection direction) {
  return direction == WritingDirection::kVertical;
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_MAC_MAC_WRITING_DIRECTION_H_
