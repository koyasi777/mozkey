// Copyright 2010-2021, Google Inc.
// All rights reserved.

#ifndef MOZC_WIN32_TIP_TIP_INPROCESS_RENDERER_H_
#define MOZC_WIN32_TIP_TIP_INPROCESS_RENDERER_H_

#include <windows.h>

#include "protocol/renderer_command.pb.h"

namespace mozc {
namespace win32 {
namespace tsf {

// Owns the Windows renderer windows on a dedicated renderer UI thread.
//
// This path is used only for immersive TSF hosts. Creating the renderer popups
// in the host process, with the host top-level HWND supplied at creation time,
// lets Windows keep them in the host's presentation layer. Conventional desktop
// applications continue to use mozc_renderer.exe.
class TipInProcessRenderer {
 public:
  TipInProcessRenderer() = delete;

  static void OnUpdated(const commands::RendererCommand& command,
                        HWND host_window);
  static void OnUIThreadUninitialized();
};

}  // namespace tsf
}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_TIP_TIP_INPROCESS_RENDERER_H_
