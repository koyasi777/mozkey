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

#ifndef MOZC_RENDERER_MAC_RENDERER_SHADOW_WINDOW_H_
#define MOZC_RENDERER_MAC_RENDERER_SHADOW_WINDOW_H_

#import <Cocoa/Cocoa.h>

#include <cstdint>

#include "renderer/renderer_style_handler.h"
#include "renderer/window_effect_util.h"

@class MozcRendererShadowView;

namespace mozc {
namespace renderer {
namespace mac {

// Owns the transparent visual-only panel used for renderer shadows on macOS.
// The owner NSWindow keeps the logical renderer geometry used by WindowUtil;
// this panel is deliberately excluded from positioning and collision logic.
class RendererShadowWindow {
 public:
  RendererShadowWindow();
  RendererShadowWindow(const RendererShadowWindow &) = delete;
  RendererShadowWindow &operator=(const RendererShadowWindow &) = delete;
  ~RendererShadowWindow();

  void Hide();
  // Updates shadow pixels and geometry from the current owner frame. The
  // corner radius is expressed in Cocoa points. Pixel data is regenerated only
  // when rendering inputs change. If the owner is visible, the shadow is also
  // presented. Returns false only when an enabled shadow cannot be rendered.
  bool Update(NSWindow *owner_window, CGFloat owner_corner_radius,
              const RendererStyleHandler::WindowShadowStyle &style);

 private:
  void EnsureWindow();

  NSPanel *window_;
  MozcRendererShadowView *view_;
  bool cached_image_valid_;
  WindowShadowGeometry cached_geometry_;
  uint32_t cached_opacity_percent_;
  CGFloat cached_backing_scale_;
};

}  // namespace mac
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_MAC_RENDERER_SHADOW_WINDOW_H_
