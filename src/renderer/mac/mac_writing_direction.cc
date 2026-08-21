// Copyright 2026, Mozkey authors.
// All rights reserved.

#include "renderer/mac/mac_writing_direction.h"

#include "protocol/renderer_command.pb.h"

namespace mozc {
namespace renderer {
namespace mac {

WritingDirection ResolveWritingDirection(
    const commands::RendererCommand& command) {
  if (command.has_application_info() &&
      command.application_info().has_composition_target() &&
      command.application_info().composition_target().has_vertical_writing()) {
    return command.application_info().composition_target().vertical_writing()
               ? WritingDirection::kVertical
               : WritingDirection::kHorizontal;
  }

  if (!command.has_preedit_rectangle()) {
    return WritingDirection::kHorizontal;
  }

  const commands::RendererCommand::Rectangle& preedit =
      command.preedit_rectangle();
  const int width = preedit.right() - preedit.left();
  const int height = preedit.bottom() - preedit.top();

  // Preserve the historical macOS renderer heuristic exactly as a
  // compatibility fallback.  The explicit InputMethodKit direction above
  // always wins when present.
  return height < width ? WritingDirection::kVertical
                        : WritingDirection::kHorizontal;
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
