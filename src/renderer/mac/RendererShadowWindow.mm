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

#include "renderer/mac/RendererShadowWindow.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "absl/log/log.h"
#include "renderer/window_effect_util.h"

@interface MozcRendererShadowView : NSView
- (void)setShadowImage:(NSImage *)image;
@end

@implementation MozcRendererShadowView {
  NSImage *image_;
}

- (BOOL)isOpaque {
  return NO;
}

- (BOOL)isFlipped {
  return YES;
}

- (void)setShadowImage:(NSImage *)image {
  image_ = image;
  [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];
  NSRectFillUsingOperation(self.bounds, NSCompositingOperationClear);
  if (image_ == nil) {
    return;
  }
  [image_ drawInRect:self.bounds
            fromRect:NSMakeRect(0, 0, image_.size.width, image_.size.height)
           operation:NSCompositingOperationSourceOver
            fraction:1.0
      respectFlipped:YES
               hints:nil];
}

@end

namespace mozc {
namespace renderer {
namespace mac {
namespace {

// CandidateController assigns all renderer body panels levels from
// NSPopUpMenuWindowLevel - 1 through NSPopUpMenuWindowLevel + 2. Reserve the
// immediately lower level for visual-only shadows so no shadow can cover a
// renderer body.
constexpr NSInteger kRendererShadowWindowLevel = NSPopUpMenuWindowLevel - 2;

int ScaleToBackingPixels(CGFloat value, CGFloat backing_scale) {
  return std::max(
      0, static_cast<int>(std::lround(
             value * std::max<CGFloat>(1.0, backing_scale))));
}

NSImage *CreateShadowImage(const WindowShadowGeometry &geometry,
                           uint32_t opacity_percent,
                           CGFloat backing_scale) {
  if (!geometry.enabled() || opacity_percent == 0) {
    return nil;
  }

  const int64_t pixel_count =
      static_cast<int64_t>(geometry.width) * geometry.height;
  if (pixel_count <= 0 ||
      static_cast<uint64_t>(pixel_count) >
          std::numeric_limits<size_t>::max() / 4) {
    return nil;
  }

  std::vector<uint8_t> alpha(static_cast<size_t>(pixel_count));
  if (!RenderWindowShadowAlphaMask(geometry, opacity_percent, alpha.data(),
                                   alpha.size())) {
    return nil;
  }

  const NSInteger bytes_per_row = static_cast<NSInteger>(geometry.width) * 4;
  NSBitmapImageRep *representation = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:nullptr
                    pixelsWide:geometry.width
                    pixelsHigh:geometry.height
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSDeviceRGBColorSpace
                   bitmapFormat:0
                    bytesPerRow:bytes_per_row
                   bitsPerPixel:32];
  if (representation == nil || [representation bitmapData] == nullptr) {
    return nil;
  }

  // The default NSBitmapImageRep format stores RGB followed by premultiplied
  // alpha. Black remains (0, 0, 0) under premultiplication, so only the alpha
  // byte needs to vary.
  uint8_t *bitmap = [representation bitmapData];
  for (size_t i = 0; i < static_cast<size_t>(pixel_count); ++i) {
    bitmap[i * 4 + 0] = 0;
    bitmap[i * 4 + 1] = 0;
    bitmap[i * 4 + 2] = 0;
    bitmap[i * 4 + 3] = alpha[i];
  }

  const CGFloat scale = std::max<CGFloat>(1.0, backing_scale);
  const NSSize point_size =
      NSMakeSize(static_cast<CGFloat>(geometry.width) / scale,
                 static_cast<CGFloat>(geometry.height) / scale);
  [representation setSize:point_size];

  NSImage *image = [[NSImage alloc] initWithSize:point_size];
  [image addRepresentation:representation];
  return image;
}

}  // namespace

RendererShadowWindow::RendererShadowWindow()
    : window_(nil),
      view_(nil),
      cached_image_valid_(false),
      cached_geometry_(),
      cached_opacity_percent_(0),
      cached_backing_scale_(0.0) {}

RendererShadowWindow::~RendererShadowWindow() {
  if (window_ != nil) {
    [window_ orderOut:nil];
    [window_ close];
  }
}

void RendererShadowWindow::EnsureWindow() {
  if (window_ != nil) {
    return;
  }

  window_ = [[NSPanel alloc]
      initWithContentRect:NSMakeRect(0, 0, 1, 1)
                styleMask:(NSWindowStyleMaskBorderless |
                           NSWindowStyleMaskNonactivatingPanel)
                  backing:NSBackingStoreBuffered
                    defer:YES];
  view_ = [[MozcRendererShadowView alloc]
      initWithFrame:NSMakeRect(0, 0, 1, 1)];
  [window_ setContentView:view_];
  [window_ setOpaque:NO];
  [window_ setHasShadow:NO];
  [window_ setBackgroundColor:NSColor.clearColor];
  [window_ setIgnoresMouseEvents:YES];
  [window_ setFloatingPanel:YES];
  [window_ setWorksWhenModal:YES];
  [window_ setReleasedWhenClosed:NO];
  [window_ setDisplaysWhenScreenProfileChanges:YES];
  [window_ setLevel:kRendererShadowWindowLevel];
  [window_ orderOut:nil];
}

void RendererShadowWindow::Hide() {
  if (window_ != nil) {
    [window_ orderOut:nil];
  }
}

bool RendererShadowWindow::Update(
    NSWindow *owner_window, CGFloat owner_corner_radius,
    const RendererStyleHandler::WindowShadowStyle &style) {
  if (owner_window == nil) {
    Hide();
    return true;
  }

  const uint32_t shadow_size = std::min(style.size, kMaxWindowShadowSize);
  const uint32_t shadow_distance =
      std::min(style.distance, kMaxWindowShadowDistance);
  const uint32_t shadow_opacity =
      std::min(style.opacity_percent, kMaxWindowShadowOpacityPercent);
  if (shadow_size == 0 || shadow_opacity == 0) {
    Hide();
    return true;
  }

  const NSRect owner_frame = [owner_window frame];
  if (NSWidth(owner_frame) <= 0.0 || NSHeight(owner_frame) <= 0.0) {
    Hide();
    return true;
  }

  const CGFloat backing_scale =
      std::max<CGFloat>(1.0, [owner_window backingScaleFactor]);
  const int owner_width =
      ScaleToBackingPixels(NSWidth(owner_frame), backing_scale);
  const int owner_height =
      ScaleToBackingPixels(NSHeight(owner_frame), backing_scale);
  const int shadow_size_pixels =
      ScaleToBackingPixels(static_cast<CGFloat>(shadow_size), backing_scale);
  const int shadow_distance_pixels =
      ScaleToBackingPixels(static_cast<CGFloat>(shadow_distance), backing_scale);
  const int corner_radius_pixels =
      ScaleToBackingPixels(std::max<CGFloat>(0.0, owner_corner_radius),
                           backing_scale);

  const WindowShadowGeometry geometry = ComputeWindowShadowGeometry(
      owner_width, owner_height, shadow_size_pixels, shadow_distance_pixels,
      style.angle_degrees, corner_radius_pixels);
  if (!geometry.enabled()) {
    Hide();
    return true;
  }

  EnsureWindow();
  if (!cached_image_valid_ || geometry != cached_geometry_ ||
      shadow_opacity != cached_opacity_percent_ ||
      backing_scale != cached_backing_scale_) {
    NSImage *image =
        CreateShadowImage(geometry, shadow_opacity, backing_scale);
    if (image == nil) {
      LOG(ERROR) << "Failed to create macOS renderer shadow bitmap.";
      cached_image_valid_ = false;
      Hide();
      return false;
    }
    [view_ setShadowImage:image];
    cached_geometry_ = geometry;
    cached_opacity_percent_ = shadow_opacity;
    cached_backing_scale_ = backing_scale;
    cached_image_valid_ = true;
  }

  [window_ setCollectionBehavior:
      ([owner_window collectionBehavior] |
       NSWindowCollectionBehaviorTransient |
       NSWindowCollectionBehaviorIgnoresCycle)];
  const CGFloat scale = std::max<CGFloat>(1.0, backing_scale);
  const CGFloat left_margin = geometry.left_margin / scale;
  const CGFloat right_margin = geometry.right_margin / scale;
  const CGFloat top_margin = geometry.top_margin / scale;
  const CGFloat bottom_margin = geometry.bottom_margin / scale;
  const NSRect shadow_frame =
      NSMakeRect(NSMinX(owner_frame) - left_margin,
                 NSMinY(owner_frame) - bottom_margin,
                 NSWidth(owner_frame) + left_margin + right_margin,
                 NSHeight(owner_frame) + top_margin + bottom_margin);

  [window_ setFrame:shadow_frame display:NO];
  [view_ setFrame:NSMakeRect(0, 0, NSWidth(shadow_frame),
                            NSHeight(shadow_frame))];

  if ([owner_window isVisible]) {
    // Levels dominate same-level ordering, so this can come to the front of
    // the shadow level without covering any renderer body.
    [window_ orderFront:nil];
  } else {
    [window_ orderOut:nil];
  }
  return true;
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
