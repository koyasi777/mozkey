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

#include "renderer/mac/RubyWindow.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "protocol/commands.pb.h"
#include "protocol/renderer_command.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/mac/mac_view_util.h"
#include "renderer/renderer_style_handler.h"

namespace {

constexpr CGFloat kBaseFontSize = 13.0;

CGFloat ScaleMetric(uint32_t value, uint32_t size_percent) {
  return std::round(
      static_cast<CGFloat>(value) *
      static_cast<CGFloat>(size_percent) / 100.0);
}

// Ruby spacing values are defined as physical pixels at the Windows
// 144-DPI design baseline. Cocoa geometry is expressed in 72-DPI points.
// Preserve the shared Windows value and convert only at the macOS boundary.
CGFloat ScaleWindowsRubySpacingToCocoaPoints(
    uint32_t value, uint32_t size_percent) {
  const CGFloat windows_design_pixels =
      std::round(
          static_cast<CGFloat>(value) *
          static_cast<CGFloat>(size_percent) / 100.0);
  return windows_design_pixels * 72.0 / 144.0;
}

CGFloat ScaleFontSize(uint32_t size_percent) {
  return std::max<CGFloat>(
      1.0,
      kBaseFontSize * static_cast<CGFloat>(size_percent) / 100.0);
}

NSColor *ToNSColor(uint32_t rgb) {
  return [NSColor
      colorWithCalibratedRed:static_cast<CGFloat>((rgb >> 16) & 0xff) / 255.0
                       green:static_cast<CGFloat>((rgb >> 8) & 0xff) / 255.0
                        blue:static_cast<CGFloat>(rgb & 0xff) / 255.0
                       alpha:1.0];
}

void SetRgbColor(uint32_t rgb,
                 mozc::renderer::RendererStyle::RGBAColor *color) {
  color->set_r(static_cast<double>((rgb >> 16) & 0xff));
  color->set_g(static_cast<double>((rgb >> 8) & 0xff));
  color->set_b(static_cast<double>(rgb & 0xff));
  color->set_a(1.0);
}

}  // namespace

@interface RubyView : NSView
- (void)setRubyText:(NSAttributedString *)text;
- (void)setBackgroundColor:(NSColor *)backgroundColor
               borderColor:(NSColor *)borderColor
              cornerRadius:(CGFloat)cornerRadius
         horizontalPadding:(CGFloat)horizontalPadding
           verticalPadding:(CGFloat)verticalPadding;
- (NSSize)preferredSize;
@end

@implementation RubyView {
  NSAttributedString *text_;
  NSColor *backgroundColor_;
  NSColor *borderColor_;
  CGFloat cornerRadius_;
  CGFloat horizontalPadding_;
  CGFloat verticalPadding_;
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    text_ = [[NSAttributedString alloc] initWithString:@""];
    backgroundColor_ = [NSColor colorWithCalibratedWhite:0.10 alpha:1.0];
    borderColor_ = NSColor.clearColor;
    cornerRadius_ = 5.0;
    horizontalPadding_ = 7.0;
    verticalPadding_ = 6.0;
    [self setWantsLayer:YES];
  }
  return self;
}

- (void)setRubyText:(NSAttributedString *)text {
  text_ = [text copy];
  [self setNeedsDisplay:YES];
}

- (void)setBackgroundColor:(NSColor *)backgroundColor
               borderColor:(NSColor *)borderColor
              cornerRadius:(CGFloat)cornerRadius
         horizontalPadding:(CGFloat)horizontalPadding
           verticalPadding:(CGFloat)verticalPadding {
  backgroundColor_ = backgroundColor;
  borderColor_ = borderColor;
  cornerRadius_ = std::max<CGFloat>(0.0, cornerRadius);
  horizontalPadding_ = std::max<CGFloat>(0.0, horizontalPadding);
  verticalPadding_ = std::max<CGFloat>(0.0, verticalPadding);
  [self setNeedsDisplay:YES];
}

- (NSSize)preferredSize {
  const NSSize textSize = [text_ size];
  return NSMakeSize(
      ceil(textSize.width + horizontalPadding_ * 2.0),
      ceil(textSize.height + verticalPadding_ * 2.0));
}

- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];

  const NSRect pathBounds = NSInsetRect(self.bounds, 0.5, 0.5);
  const CGFloat maximumRadius =
      std::max<CGFloat>(
          0.0,
          std::min(NSWidth(pathBounds), NSHeight(pathBounds)) / 2.0);
  const CGFloat effectiveRadius =
      std::min(cornerRadius_, maximumRadius);

  NSBezierPath *background =
      [NSBezierPath bezierPathWithRoundedRect:pathBounds
                                     xRadius:effectiveRadius
                                     yRadius:effectiveRadius];

  [backgroundColor_ setFill];
  [background fill];

  if (borderColor_ != nil) {
    [borderColor_ setStroke];
    [background setLineWidth:1.0];
    [background stroke];
  }

  const NSSize textSize = [text_ size];
  const NSRect textRect =
      NSMakeRect((NSWidth(self.bounds) - textSize.width) / 2.0,
                 (NSHeight(self.bounds) - textSize.height) / 2.0,
                 textSize.width,
                 textSize.height);
  [text_ drawInRect:textRect];
}
@end

namespace mozc {
namespace renderer {
namespace mac {

RubyWindow::RubyWindow() = default;

RubyWindow::~RubyWindow() = default;

bool RubyWindow::BuildReadingText(
    const commands::RendererCommand &command,
    std::string *reading) const {
  reading->clear();

  if (!command.has_output()) {
    return false;
  }

  const commands::Output &output = command.output();
  if (!output.live_conversion() || !output.has_preedit()) {
    return false;
  }

  const commands::Preedit &preedit = output.preedit();
  for (int i = 0; i < preedit.segment_size(); ++i) {
    const commands::Preedit::Segment &segment = preedit.segment(i);
    if (segment.has_key() && !segment.key().empty()) {
      reading->append(segment.key());
    } else {
      reading->append(segment.value());
    }
  }

  return !reading->empty();
}

bool RubyWindow::Update(const commands::RendererCommand &command) {
  std::string reading;
  if (!BuildReadingText(command, &reading)) {
    return false;
  }

  if (!window_) {
    InitWindow();
  }

  const RendererStyleHandler::RubyWindowStyle appearance =
      RendererStyleHandler::GetRubyWindowStyle();

  RendererStyle renderer_style;
  if (!RendererStyleHandler::GetRendererStyleForWindowType(
          RendererStyleHandler::RendererStyleType::kCandidate,
          &renderer_style)) {
    RendererStyleHandler::GetDefaultRendererStyle(&renderer_style);
  }

  RendererStyle::TextStyle text_style;
  text_style.set_font_size(ScaleFontSize(appearance.size_percent));
  text_style.set_font_weight(
      static_cast<int32_t>(appearance.font_weight));
  SetRgbColor(
      appearance.text_color,
      text_style.mutable_foreground_color());

  if (renderer_style.has_candidate_style() &&
      renderer_style.candidate_style().has_font_name() &&
      !renderer_style.candidate_style().font_name().empty()) {
    text_style.set_font_name(
        renderer_style.candidate_style().font_name());
  }

  RubyView *ruby_view = (RubyView *)view_;
  [ruby_view
      setBackgroundColor:ToNSColor(appearance.background_color)
             borderColor:ToNSColor(appearance.border_color)
            cornerRadius:ScaleMetric(
                             appearance.corner_radius,
                             appearance.size_percent)
       horizontalPadding:ScaleWindowsRubySpacingToCocoaPoints(
                             appearance.horizontal_padding,
                             appearance.size_percent)
         verticalPadding:ScaleMetric(
                             appearance.vertical_padding,
                             appearance.size_percent)];

  [ruby_view setRubyText:
      MacViewUtil::ToNSAttributedString(reading, text_style)];

  composition_gap_ = static_cast<int32_t>(
      ScaleMetric(
          appearance.composition_gap,
          appearance.size_percent));

  [window_ setAlphaValue:std::clamp<CGFloat>(
      static_cast<CGFloat>(appearance.opacity_percent) / 100.0,
      0.0,
      1.0)];

  const NSSize size = [ruby_view preferredSize];
  ResizeWindow(
      static_cast<int32_t>(ceil(size.width)),
      static_cast<int32_t>(ceil(size.height)));
  return true;
}

int32_t RubyWindow::GetCompositionGap() const {
  return composition_gap_;
}

void RubyWindow::InitWindow() {
  RendererBaseWindow::InitWindow();
  [window_ setOpaque:NO];
  [window_ setHasShadow:YES];
  [window_ setBackgroundColor:NSColor.clearColor];
  [window_ setIgnoresMouseEvents:YES];
}

void RubyWindow::ResetView() {
  view_ = [[RubyView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
