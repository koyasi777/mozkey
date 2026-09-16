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
#import <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

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

constexpr CGFloat kVerticalCoreTextSafety = 4.0;

NSDictionary *VerticalFrameAttributes() {
  return @{
    (__bridge id)kCTFrameProgressionAttributeName :
        @(kCTFrameProgressionRightToLeft)
  };
}

NSAttributedString *MakeVerticalAttributedString(
    NSAttributedString *base) {
  if (base == nil) {
    return [[NSAttributedString alloc] initWithString:@""];
  }

  NSMutableAttributedString *vertical =
      [[NSMutableAttributedString alloc] initWithAttributedString:base];
  if ([vertical length] == 0) {
    return vertical;
  }

  const NSRange full_range = NSMakeRange(0, [vertical length]);
  [vertical addAttribute:(__bridge id)kCTVerticalFormsAttributeName
                   value:@YES
                   range:full_range];

  NSFont *font =
      [base attribute:NSFontAttributeName atIndex:0 effectiveRange:nullptr];
  if (font != nil) {
    CTFontRef ct_font = CTFontCreateWithName(
        (__bridge CFStringRef)[font fontName], [font pointSize], nullptr);
    if (ct_font != nullptr) {
      [vertical addAttribute:(__bridge id)kCTFontAttributeName
                       value:(__bridge id)ct_font
                       range:full_range];
      CFRelease(ct_font);
    }
  }

  NSColor *foreground =
      [base attribute:NSForegroundColorAttributeName
              atIndex:0
       effectiveRange:nullptr];
  if (foreground != nil) {
    NSColor *rgb = [foreground
        colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    if (rgb != nil && [rgb CGColor] != nullptr) {
      [vertical addAttribute:(__bridge id)kCTForegroundColorAttributeName
                       value:(__bridge id)[rgb CGColor]
                       range:full_range];
    }
  }

  return vertical;
}

CFIndex VisibleVerticalStringLength(
    CTFramesetterRef framesetter, CGFloat width, CGFloat height) {
  if (framesetter == nullptr || width <= 0 || height <= 0) {
    return 0;
  }

  const CGRect local_rect = CGRectMake(0, 0, width, height);
  CGPathRef path = CGPathCreateWithRect(local_rect, nullptr);
  if (path == nullptr) {
    return 0;
  }

  CTFrameRef frame = CTFramesetterCreateFrame(
      framesetter, CFRangeMake(0, 0), path,
      (__bridge CFDictionaryRef)VerticalFrameAttributes());
  CGPathRelease(path);
  if (frame == nullptr) {
    return 0;
  }

  const CFRange visible = CTFrameGetVisibleStringRange(frame);
  CFRelease(frame);
  return visible.length;
}

struct VerticalRubyMetrics {
  // Technical Core Text frame width needed for full-fit shaping.
  CGFloat frame_width = 0.0;
  // Visible pill-column width. This intentionally ignores Latin-run
  // typographic ascent/descent inflation and instead follows one nominal CJK
  // vertical column plus actual glyph-path cross-axis ink.
  CGFloat visual_width = 0.0;
  CGFloat frame_height = 0.0;
};

VerticalRubyMetrics MeasureVerticalAttributedString(
    NSAttributedString *text) {
  VerticalRubyMetrics result;
  if (text == nil || [text length] == 0) {
    return result;
  }

  CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(
      (__bridge CFAttributedStringRef)text);
  if (framesetter == nullptr) {
    return result;
  }

  CFRange fit_range = CFRangeMake(0, 0);
  const CGSize suggested = CTFramesetterSuggestFrameSizeWithConstraints(
      framesetter, CFRangeMake(0, 0),
      (__bridge CFDictionaryRef)VerticalFrameAttributes(),
      CGSizeMake(CGFLOAT_MAX, CGFLOAT_MAX), &fit_range);

  NSFont *font =
      [text attribute:NSFontAttributeName atIndex:0 effectiveRange:nullptr];
  const CGFloat line_floor =
      font != nil
          ? std::ceil(std::max<CGFloat>(
                1.0, [font ascender] - [font descender] + [font leading]))
          : 1.0;
  const CGFloat point_size =
      font != nil ? std::max<CGFloat>(1.0, [font pointSize]) : 16.0;

  result.frame_width = std::ceil(std::max<CGFloat>(
      line_floor, std::max<CGFloat>(1.0, suggested.width) + 1.0));

  CGFloat low = 1.0;
  CGFloat high = std::max({
      std::ceil(std::max<CGFloat>(1.0, suggested.height)) + 4.0,
      line_floor + 4.0,
      point_size * std::max<NSUInteger>(1, [text length]) * 2.5 + 32.0,
  });

  const CFIndex total_length = static_cast<CFIndex>([text length]);
  while (VisibleVerticalStringLength(
             framesetter, result.frame_width, high) < total_length) {
    high *= 2.0;
    if (high > 20000.0) {
      break;
    }
  }

  for (int iteration = 0; iteration < 24; ++iteration) {
    const CGFloat middle = (low + high) / 2.0;
    if (VisibleVerticalStringLength(
            framesetter, result.frame_width, middle) >= total_length) {
      high = middle;
    } else {
      low = middle;
    }
  }

  result.frame_height = std::ceil(std::max({
      std::max<CGFloat>(1.0, suggested.height),
      std::ceil(high + 1.0),
      line_floor,
  }));

  CGFloat nominal_column_width = 1.0;
  NSDictionary *attributes =
      [text attributesAtIndex:0 effectiveRange:nullptr];
  if (attributes != nil) {
    NSAttributedString *cjk_probe =
        [[NSAttributedString alloc] initWithString:@"日"
                                        attributes:attributes];
    CTLineRef cjk_line = CTLineCreateWithAttributedString(
        (__bridge CFAttributedStringRef)cjk_probe);
    if (cjk_line != nullptr) {
      CGFloat ascent = 0.0;
      CGFloat descent = 0.0;
      CGFloat leading = 0.0;
      CTLineGetTypographicBounds(
          cjk_line, &ascent, &descent, &leading);
      nominal_column_width = std::ceil(std::max<CGFloat>(
          1.0, ascent + descent + leading));
      CFRelease(cjk_line);
    }
  }

  CGFloat glyph_ink_cross_axis = 1.0;
  CTLineRef line = CTLineCreateWithAttributedString(
      (__bridge CFAttributedStringRef)text);
  if (line != nullptr) {
    const CGRect glyph_bounds = CTLineGetBoundsWithOptions(
        line, kCTLineBoundsUseGlyphPathBounds |
                  kCTLineBoundsExcludeTypographicLeading);
    // In a vertical CTLine the glyph-path Y extent is the visible cross-axis
    // ink. A pending Latin run can inflate CTLine ascent/descent without
    // increasing this ink extent, so the pill must not use those inflated
    // typographic metrics as its visible column width.
    glyph_ink_cross_axis = std::ceil(std::max<CGFloat>(
        1.0, CGRectGetHeight(glyph_bounds)));
    CFRelease(line);
  }

  result.visual_width = std::ceil(std::max(
      nominal_column_width, glyph_ink_cross_axis));

  CFRelease(framesetter);
  return result;
}

void DrawVerticalAttributedString(
    NSAttributedString *text, const NSRect &visual_rect,
    CGFloat technical_frame_width) {
  if (text == nil || [text length] == 0 ||
      visual_rect.size.width <= 0 || visual_rect.size.height <= 0 ||
      technical_frame_width <= 0) {
    return;
  }

  CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(
      (__bridge CFAttributedStringRef)text);
  if (framesetter == nullptr) {
    return;
  }

  const CGFloat draw_width =
      technical_frame_width + kVerticalCoreTextSafety;
  const CGRect local_rect =
      CGRectMake(0, 0, draw_width, visual_rect.size.height);
  CGPathRef path = CGPathCreateWithRect(local_rect, nullptr);
  if (path == nullptr) {
    CFRelease(framesetter);
    return;
  }

  CTFrameRef frame = CTFramesetterCreateFrame(
      framesetter, CFRangeMake(0, 0), path,
      (__bridge CFDictionaryRef)VerticalFrameAttributes());

  CGFloat correction_x = 0.0;
  if (frame != nullptr) {
    CFArrayRef lines = CTFrameGetLines(frame);
    const CFIndex line_count =
        lines != nullptr ? CFArrayGetCount(lines) : 0;
    if (line_count > 0) {
      std::vector<CGPoint> origins(static_cast<size_t>(line_count));
      CTFrameGetLineOrigins(
          frame, CFRangeMake(0, 0), origins.data());
      correction_x = draw_width / 2.0 - origins[0].x;
    }
  }

  CGContextRef context =
      [[NSGraphicsContext currentContext] CGContext];
  if (context != nullptr && frame != nullptr) {
    // RubyView uses Cocoa's normal non-flipped coordinate system, so Core Text
    // can draw directly in Quartz coordinates.  Keep the technical frame
    // wider than the visible pill column and center the actual CT line origin.
    const CGFloat draw_x =
        NSMidX(visual_rect) - draw_width / 2.0 + correction_x;
    CGContextSaveGState(context);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    CGContextTranslateCTM(
        context, draw_x, NSMinY(visual_rect));
    CTFrameDraw(frame, context);
    CGContextRestoreGState(context);
  }

  if (frame != nullptr) {
    CFRelease(frame);
  }
  CGPathRelease(path);
  CFRelease(framesetter);
}

}  // namespace

@interface RubyView : NSView
- (void)setVerticalWriting:(BOOL)verticalWriting;
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
  BOOL verticalWriting_;
  VerticalRubyMetrics verticalMetrics_;
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
    verticalWriting_ = NO;
    verticalMetrics_ = VerticalRubyMetrics();
    [self setWantsLayer:YES];
  }
  return self;
}

- (void)setVerticalWriting:(BOOL)verticalWriting {
  verticalWriting_ = verticalWriting;
}

- (void)setRubyText:(NSAttributedString *)text {
  if (verticalWriting_) {
    text_ = [MakeVerticalAttributedString(text) copy];
    verticalMetrics_ = MeasureVerticalAttributedString(text_);
  } else {
    text_ = [text copy];
    verticalMetrics_ = VerticalRubyMetrics();
  }
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
  if (verticalWriting_) {
    return NSMakeSize(
        ceil(verticalMetrics_.visual_width + horizontalPadding_ * 2.0),
        ceil(verticalMetrics_.frame_height + verticalPadding_ * 2.0));
  }

  const NSSize textSize = [text_ size];
  return NSMakeSize(
      ceil(textSize.width + horizontalPadding_ * 2.0),
      ceil(textSize.height + verticalPadding_ * 2.0));
}

- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];
  NSRectFillUsingOperation(self.bounds, NSCompositingOperationClear);

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

  if (verticalWriting_) {
    const NSRect textRect =
        NSMakeRect(
            (NSWidth(self.bounds) - verticalMetrics_.visual_width) / 2.0,
            verticalPadding_,
            verticalMetrics_.visual_width,
            verticalMetrics_.frame_height);
    DrawVerticalAttributedString(
        text_, textRect, verticalMetrics_.frame_width);
    return;
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

void RubyWindow::SetWritingDirection(
    WritingDirection writing_direction) {
  writing_direction_ = writing_direction;
}

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
  const bool vertical_writing =
      IsVerticalWriting(writing_direction_);
  [ruby_view setVerticalWriting:vertical_writing];

  const CGFloat horizontal_padding =
      vertical_writing
          ? ScaleMetric(
                appearance.vertical_padding,
                appearance.size_percent)
          : ScaleWindowsRubySpacingToCocoaPoints(
                appearance.horizontal_padding,
                appearance.size_percent);
  const CGFloat vertical_padding =
      vertical_writing
          ? ScaleWindowsRubySpacingToCocoaPoints(
                appearance.horizontal_padding,
                appearance.size_percent)
          : ScaleMetric(
                appearance.vertical_padding,
                appearance.size_percent);

  const CGFloat corner_radius =
      ScaleMetric(appearance.corner_radius, appearance.size_percent);
  [ruby_view
      setBackgroundColor:ToNSColor(appearance.background_color)
             borderColor:ToNSColor(appearance.border_color)
            cornerRadius:corner_radius
       horizontalPadding:horizontal_padding
         verticalPadding:vertical_padding];

  [ruby_view setRubyText:
      MacViewUtil::ToNSAttributedString(reading, text_style)];

  text_top_offset_ =
      vertical_writing
          ? static_cast<int32_t>(ceil(vertical_padding))
          : 0;

  composition_gap_ = static_cast<int32_t>(
      ScaleMetric(
          appearance.composition_gap,
          appearance.size_percent));

  const NSSize size = [ruby_view preferredSize];
  ResizeWindow(
      static_cast<int32_t>(ceil(size.width)),
      static_cast<int32_t>(ceil(size.height)));
  SetWindowEffects(appearance.opacity_percent, corner_radius,
                   appearance.shadow);
  return true;
}

int32_t RubyWindow::GetCompositionGap() const {
  return composition_gap_;
}

int32_t RubyWindow::GetTextTopOffset() const {
  return text_top_offset_;
}

void RubyWindow::InitWindow() {
  RendererBaseWindow::InitWindow();
  [window_ setOpaque:NO];
  [window_ setHasShadow:NO];
  [window_ setBackgroundColor:NSColor.clearColor];
  [window_ setIgnoresMouseEvents:YES];
}

void RubyWindow::ResetView() {
  view_ = [[RubyView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
}

}  // namespace mac
}  // namespace renderer
}  // namespace mozc
