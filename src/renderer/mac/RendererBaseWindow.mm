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

#include <Carbon/Carbon.h>
#include <Cocoa/Cocoa.h>
#include <objc/message.h>

#include <algorithm>
#include <cstdint>

#include "absl/log/log.h"
#include "base/coordinates.h"
#include "base/mac/mac_util.h"
#include "protocol/commands.pb.h"
#include "renderer/mac/RendererBaseWindow.h"
#include "renderer/renderer_style_handler.h"

namespace mozc {
namespace renderer {
namespace mac {

RendererBaseWindow::RendererBaseWindow()
    : window_(nil),
      view_(nil),
      window_effects_configured_(false),
      effect_corner_radius_(0.0),
      shadow_style_(),
      window_level_(NSPopUpMenuWindowLevel) {}

void RendererBaseWindow::InitWindow() {
  if (window_) {
    LOG(ERROR) << "window is already initialized.";
    return;
  }
  const NSUInteger style_mask = NSWindowStyleMaskUtilityWindow | NSWindowStyleMaskDocModalWindow |
                                NSWindowStyleMaskNonactivatingPanel;
  window_ = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1)
                                       styleMask:style_mask
                                         backing:NSBackingStoreBuffered
                                           defer:YES];
  ResetView();
  [window_ setContentView:view_];
  [window_ setDisplaysWhenScreenProfileChanges:YES];
  [window_ makeKeyAndOrderFront:nil];
  [window_ setFloatingPanel:YES];
  [window_ setWorksWhenModal:YES];
  [window_ setBackgroundColor:NSColor.whiteColor];
  [window_ setReleasedWhenClosed:NO];
  [window_ setLevel:window_level_];
  [window_ orderOut:window_];
}

RendererBaseWindow::~RendererBaseWindow() {
  shadow_window_.Hide();
  if (window_ != nil) {
    [window_ close];
  }
}

Size RendererBaseWindow::GetWindowSize() const {
  if (!window_) {
    return Size(0, 0);
  }
  NSRect rect = [window_ frame];
  return Size(rect.size.width, rect.size.height);
}

void RendererBaseWindow::Hide() {
  shadow_window_.Hide();
  if (window_) {
    [window_ orderOut:window_];
  }
}

void RendererBaseWindow::Show() {
  if (!window_) {
    InitWindow();
  }
  [window_ orderFront:window_];
  UpdateShadowWindow();
}

bool RendererBaseWindow::IsVisible() {
  if (!window_) {
    return false;
  }
  return ([window_ isVisible] == YES);
}

void RendererBaseWindow::ResetView() {
  view_ = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
}

void RendererBaseWindow::MoveWindow(const NSPoint &point) {
  if (!window_) {
    return;
  }
  NSRect rect = [window_ frame];
  rect.origin.x = point.x;
  rect.origin.y = point.y;
  [window_ setFrame:rect display:FALSE];
  UpdateShadowWindow();
}

void RendererBaseWindow::ResizeWindow(int32_t width, int32_t height) {
  if (!window_) {
    return;
  }
  NSRect rect = [window_ frame];
  rect.size.width = width;
  rect.size.height = height;
  [window_ setFrame:rect display:FALSE];
  UpdateShadowWindow();
}

void RendererBaseWindow::SetWindowLevel(NSInteger window_level) {
  if (window_level_ != window_level) {
    window_level_ = window_level;
    if (window_) {
      [window_ setLevel:window_level_];
    }
  }
}

void RendererBaseWindow::SetWindowEffects(
    uint32_t opacity_percent, CGFloat corner_radius,
    const RendererStyleHandler::WindowShadowStyle &shadow_style) {
  if (!window_) {
    return;
  }

  [window_ setAlphaValue:std::clamp<CGFloat>(
      static_cast<CGFloat>(opacity_percent) / 100.0, 0.0, 1.0)];
  [window_ setHasShadow:NO];

  window_effects_configured_ = true;
  effect_corner_radius_ = std::max<CGFloat>(0.0, corner_radius);
  shadow_style_ = shadow_style;
  UpdateShadowWindow();
}

void RendererBaseWindow::UpdateShadowWindow() {
  if (!window_effects_configured_ || !window_) {
    shadow_window_.Hide();
    return;
  }
  if (!shadow_window_.Update(window_, effect_corner_radius_, shadow_style_)) {
    LOG(ERROR) << "Failed to update macOS renderer shadow window.";
  }
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
