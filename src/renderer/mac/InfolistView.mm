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

#include <algorithm>
#include <cmath>
#include <new>
#include <set>
#include <string>
#include <vector>

#import "renderer/mac/InfolistView.h"
#import <CoreText/CoreText.h>


#include "absl/log/log.h"
#include "client/client_interface.h"
#include "protocol/commands.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/mac/mac_vertical_infolist_layout.h"
#include "renderer/mac/mac_view_util.h"
#include "renderer/renderer_style_handler.h"
#include "renderer/table_layout.h"

using mozc::commands::CandidateWindow;
using mozc::commands::Information;
using mozc::commands::InformationList;
using mozc::renderer::RendererStyle;
using mozc::renderer::RendererStyleHandler;
using mozc::renderer::mac::IsVerticalWriting;
using mozc::renderer::mac::MacVerticalInfolistLayout;
using mozc::renderer::mac::MacViewUtil;
using mozc::renderer::mac::WritingDirection;

namespace {

constexpr CGFloat kVerticalCoreTextSafety = 4.0;

NSDictionary *VerticalFrameAttributes() {
  return @{
    (__bridge id)kCTFrameProgressionAttributeName :
        @(kCTFrameProgressionRightToLeft)
  };
}

NSAttributedString *MakeVerticalAttributedString(
    const std::string &text,
    const RendererStyle::TextStyle &style) {
  NSAttributedString *base =
      MacViewUtil::ToNSAttributedString(text, style);
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

struct VerticalEmMetrics {
  int inline_advance = 1;
  int cross_width = 1;
};

VerticalEmMetrics MeasureVerticalEm(
    const RendererStyle::TextStyle &style) {
  VerticalEmMetrics result;
  NSAttributedString *probe =
      MakeVerticalAttributedString("\u65e5", style);
  if (probe == nil || [probe length] == 0) {
    return result;
  }

  CTLineRef line = CTLineCreateWithAttributedString(
      (__bridge CFAttributedStringRef)probe);
  if (line == nullptr) {
    return result;
  }

  CGFloat ascent = 0.0;
  CGFloat descent = 0.0;
  CGFloat leading = 0.0;
  const double advance =
      CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
  result.inline_advance = std::max(
      1, static_cast<int>(std::ceil(std::max(1.0, advance))));
  result.cross_width = std::max(
      1, static_cast<int>(std::ceil(std::max<CGFloat>(
             1.0, ascent + descent + leading))));
  CFRelease(line);
  return result;
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

struct VerticalTextMeasurement {
  mozc::Size layout_size;
  int frame_width = 0;
};

VerticalTextMeasurement MeasureVerticalWrappedAttributedString(
    NSAttributedString *text, int maximum_height) {
  VerticalTextMeasurement result;
  if (text == nil || [text length] == 0 || maximum_height <= 0) {
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
      CGSizeMake(CGFLOAT_MAX, maximum_height), &fit_range);

  NSFont *font =
      [text attribute:NSFontAttributeName atIndex:0 effectiveRange:nullptr];
  const CGFloat line_floor =
      font != nil
          ? std::ceil(std::max<CGFloat>(
                1.0, [font ascender] - [font descender] + [font leading]))
          : 1.0;

  CGFloat frame_width = std::ceil(std::max<CGFloat>(
      line_floor, std::max<CGFloat>(1.0, suggested.width) + 1.0));
  const CFIndex total_length = static_cast<CFIndex>([text length]);

  if (VisibleVerticalStringLength(
          framesetter, frame_width, maximum_height) < total_length) {
    CGFloat low = frame_width;
    CGFloat high = frame_width;
    while (VisibleVerticalStringLength(
               framesetter, high, maximum_height) < total_length) {
      high *= 2.0;
      if (high > 20000.0) {
        break;
      }
    }
    for (int iteration = 0; iteration < 24; ++iteration) {
      const CGFloat middle = (low + high) / 2.0;
      if (VisibleVerticalStringLength(
              framesetter, middle, maximum_height) >= total_length) {
        high = middle;
      } else {
        low = middle;
      }
    }
    frame_width = std::ceil(high + 1.0);
  }

  const CGRect local_rect =
      CGRectMake(0, 0, frame_width, maximum_height);
  CGPathRef path = CGPathCreateWithRect(local_rect, nullptr);
  CTFrameRef frame =
      path != nullptr
          ? CTFramesetterCreateFrame(
                framesetter, CFRangeMake(0, 0), path,
                (__bridge CFDictionaryRef)VerticalFrameAttributes())
          : nullptr;

  int visual_width = 0;
  if (frame != nullptr) {
    NSDictionary *attributes =
        [text attributesAtIndex:0 effectiveRange:nullptr];
    CGFloat nominal_column_width = line_floor;
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

    CFArrayRef lines = CTFrameGetLines(frame);
    const CFIndex line_count =
        lines != nullptr ? CFArrayGetCount(lines) : 0;
    for (CFIndex i = 0; i < line_count; ++i) {
      CTLineRef line = reinterpret_cast<CTLineRef>(
          CFArrayGetValueAtIndex(lines, i));
      const CGRect glyph_bounds = CTLineGetBoundsWithOptions(
          line, kCTLineBoundsUseGlyphPathBounds |
                    kCTLineBoundsExcludeTypographicLeading);
      const CGFloat glyph_ink_cross_axis =
          std::ceil(std::max<CGFloat>(
              1.0, CGRectGetHeight(glyph_bounds)));
      visual_width += std::max(
          1, static_cast<int>(std::ceil(std::max(
                 nominal_column_width, glyph_ink_cross_axis))));
    }

    const CFRange visible = CTFrameGetVisibleStringRange(frame);
    if (visible.location != 0 || visible.length != total_length) {
      LOG(WARNING) << "Core Text vertical infolist measurement mismatch: "
                   << visible.location << "+" << visible.length
                   << " total=" << total_length;
    }
  }

  if (frame != nullptr) {
    CFRelease(frame);
  }
  if (path != nullptr) {
    CGPathRelease(path);
  }
  CFRelease(framesetter);

  if (visual_width <= 0) {
    visual_width = std::max(1, static_cast<int>(frame_width));
  }

  result.layout_size = mozc::Size(visual_width, maximum_height);
  result.frame_width =
      std::max(1, static_cast<int>(std::ceil(frame_width)));
  return result;
}

void DrawVerticalSingleColumnAttributedString(
    NSAttributedString *text, const NSRect &visual_rect,
    int technical_frame_width) {
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
      CGFloat min_x = origins[0].x;
      CGFloat max_x = origins[0].x;
      for (const CGPoint &origin : origins) {
        min_x = std::min(min_x, origin.x);
        max_x = std::max(max_x, origin.x);
      }
      const CGFloat baseline_group_center = (min_x + max_x) / 2.0;
      correction_x =
          draw_width / 2.0 - baseline_group_center;
    }
  }

  CGContextRef context =
      [[NSGraphicsContext currentContext] CGContext];
  if (context != nullptr && frame != nullptr) {
    // InfolistView is flipped. Core Text draws in Quartz coordinates, so
    // compensate locally while keeping Cocoa geometry in top-down coordinates.
    const CGFloat draw_x =
        NSMidX(visual_rect) - draw_width / 2.0 + correction_x;
    CGContextSaveGState(context);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    CGContextTranslateCTM(
        context, draw_x, NSMaxY(visual_rect));
    CGContextScaleCTM(context, 1.0, -1.0);
    CTFrameDraw(frame, context);
    CGContextRestoreGState(context);
  }

  if (frame != nullptr) {
    CFRelease(frame);
  }
  CGPathRelease(path);
  CFRelease(framesetter);
}


void DrawVerticalAttributedString(
    NSAttributedString *text, const NSRect &visual_rect,
    int technical_frame_width) {
  if (text == nil || [text length] == 0 ||
      visual_rect.size.width <= 0 || visual_rect.size.height <= 0 ||
      technical_frame_width <= 0) {
    return;
  }

  // Use one wide Core Text frame only to obtain the native vertical wrapping
  // boundaries. CTFrameDraw must not draw a multi-column frame directly:
  // Core Text's technical baseline spacing can be much wider than one CJK em
  // even though the glyph ink is compact. That technical spacing is required
  // for full-fit measurement, but is not appropriate visual column spacing.
  CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(
      (__bridge CFAttributedStringRef)text);
  if (framesetter == nullptr) {
    return;
  }

  const CGFloat frame_width =
      technical_frame_width + kVerticalCoreTextSafety;
  const CGRect local_rect =
      CGRectMake(0, 0, frame_width, visual_rect.size.height);
  CGPathRef path = CGPathCreateWithRect(local_rect, nullptr);
  CTFrameRef frame =
      path != nullptr
          ? CTFramesetterCreateFrame(
                framesetter, CFRangeMake(0, 0), path,
                (__bridge CFDictionaryRef)VerticalFrameAttributes())
          : nullptr;

  if (frame == nullptr) {
    if (path != nullptr) {
      CGPathRelease(path);
    }
    CFRelease(framesetter);
    return;
  }

  CFArrayRef lines = CTFrameGetLines(frame);
  const CFIndex line_count =
      lines != nullptr ? CFArrayGetCount(lines) : 0;
  if (line_count <= 1) {
    CFRelease(frame);
    CGPathRelease(path);
    CFRelease(framesetter);
    DrawVerticalSingleColumnAttributedString(
        text, visual_rect, technical_frame_width);
    return;
  }

  struct PackedColumn {
    NSAttributedString *text = nil;
    VerticalTextMeasurement measurement;
  };
  std::vector<PackedColumn> columns;
  columns.reserve(static_cast<size_t>(line_count));

  CFIndex covered_length = 0;
  for (CFIndex i = 0; i < line_count; ++i) {
    CTLineRef line = reinterpret_cast<CTLineRef>(
        CFArrayGetValueAtIndex(lines, i));
    if (line == nullptr) {
      continue;
    }

    const CFRange range = CTLineGetStringRange(line);
    if (range.location < 0 || range.length <= 0 ||
        range.location + range.length > [text length]) {
      continue;
    }

    NSAttributedString *column_text =
        [text attributedSubstringFromRange:
                  NSMakeRange(static_cast<NSUInteger>(range.location),
                              static_cast<NSUInteger>(range.length))];
    const VerticalTextMeasurement measurement =
        MeasureVerticalWrappedAttributedString(
            column_text,
            std::max(1, static_cast<int>(
                            std::floor(visual_rect.size.height))));
    if (measurement.layout_size.width <= 0 ||
        measurement.frame_width <= 0) {
      continue;
    }

    columns.push_back({column_text, measurement});
    covered_length += range.length;
  }

  const CFRange visible = CTFrameGetVisibleStringRange(frame);
  CFRelease(frame);
  CGPathRelease(path);
  CFRelease(framesetter);

  if (columns.empty() ||
      visible.location != 0 ||
      visible.length != [text length] ||
      covered_length != [text length]) {
    // Fail visually safe: if Core Text ever returns ranges that cannot be
    // reconstructed exactly, preserve the single-frame fallback rather than
    // silently dropping glyphs.
    DrawVerticalSingleColumnAttributedString(
        text, visual_rect, technical_frame_width);
    return;
  }

  CGFloat packed_width = 0.0;
  for (const PackedColumn &column : columns) {
    packed_width += column.measurement.layout_size.width;
  }

  // The geometry layer reserves the compact visual width measured from these
  // columns. Center for rounding tolerance, then place columns right-to-left.
  // Each column is drawn through a one-column CTFrame so shaping and vertical
  // forms stay Core Text-native without inheriting multi-column baseline gaps.
  CGFloat right =
      NSMidX(visual_rect) + packed_width / 2.0;
  for (const PackedColumn &column : columns) {
    const CGFloat column_width =
        column.measurement.layout_size.width;
    const NSRect column_rect =
        NSMakeRect(right - column_width,
                   visual_rect.origin.y,
                   column_width,
                   visual_rect.size.height);
    DrawVerticalSingleColumnAttributedString(
        column.text, column_rect, column.measurement.frame_width);
    right -= column_width;
  }
}

NSRect NSRectFromMozcRect(const mozc::Rect &rect) {
  return NSMakeRect(rect.Left(), rect.Top(), rect.Width(), rect.Height());
}

}  // namespace

// Private method declarations.
@interface InfolistView ()
- (void)reloadStyle;

// Draw the |row|-th horizontal row and return its height.
- (CGFloat)drawHorizontalRow:(int)row
                        ypos:(CGFloat)ypos
                   draw_flag:(bool)draw_flag;

// Draw the historical horizontal view.
- (NSSize)drawHorizontalView:(bool)draw_flag;

// Draw the native Japanese vertical infolist.
- (NSSize)drawVerticalView:(bool)draw_flag;

// Dispatch to the current writing direction.
- (NSSize)drawView:(bool)draw_flag;
@end

@implementation InfolistView
#pragma mark initialization

- (id)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    style_ = new (std::nothrow) RendererStyle;
    writingDirection_ = WritingDirection::kHorizontal;
    if (style_ != nullptr) {
      [self reloadStyle];
    }
  }

  if (!style_) {
    self = nil;
  }
  return self;
}

- (void)dealloc {
  delete style_;
  style_ = nullptr;
}

- (void)reloadStyle {
  if (style_ == nullptr) {
    return;
  }
  if (!RendererStyleHandler::GetRendererStyleForWindowType(
          RendererStyleHandler::RendererStyleType::kCandidate, style_)) {
    RendererStyleHandler::GetDefaultRendererStyle(style_);
  }
}

- (void)setWritingDirection:(WritingDirection)writing_direction {
  writingDirection_ = writing_direction;
}

- (void)setCandidateWindow:(const CandidateWindow *)candidate_window {
  candidate_window_.CopyFrom(*candidate_window);
  [self reloadStyle];
}

- (BOOL)isFlipped {
  return YES;
}

#pragma mark drawing
- (CGFloat)drawHorizontalRow:(int)row
                        ypos:(CGFloat)ypos
                   draw_flag:(bool)draw_flag {
  const RendererStyle::InfolistStyle &infostyle = style_->infolist_style();
  const InformationList &usages = candidate_window_.usages();
  const RendererStyle::TextStyle &title_style = infostyle.title_style();
  const RendererStyle::TextStyle &desc_style = infostyle.description_style();
  const int title_width = infostyle.window_width() - title_style.left_padding() -
                          title_style.right_padding() - infostyle.window_border() * 2 -
                          infostyle.row_rect_padding() * 2;
  const int desc_width = infostyle.window_width() - desc_style.left_padding() -
                         desc_style.right_padding() - infostyle.window_border() * 2 -
                         infostyle.row_rect_padding() * 2;
  const NSSize title_size = NSMakeSize(title_width, 1000);
  const NSSize desc_size = NSMakeSize(desc_width, 1000);
  const Information &info = usages.information(row);

  NSAttributedString *title_string = MacViewUtil::ToNSAttributedString(info.title(), title_style);
  NSAttributedString *desc_string =
      MacViewUtil::ToNSAttributedString(info.description(), desc_style);
  NSRect title_rect = [title_string boundingRectWithSize:title_size
                                                 options:NSStringDrawingUsesLineFragmentOrigin];
  NSRect desc_rect = [desc_string boundingRectWithSize:desc_size
                                               options:NSStringDrawingUsesLineFragmentOrigin];
  CGFloat height =
      title_rect.size.height + desc_rect.size.height + infostyle.row_rect_padding() * 2;

  if (!draw_flag) {
    return height;
  }
  title_rect.origin.x =
      infostyle.window_border() + infostyle.row_rect_padding() + title_style.left_padding();
  title_rect.origin.y = ypos + infostyle.row_rect_padding();
  desc_rect.origin.x =
      infostyle.window_border() + infostyle.row_rect_padding() + desc_style.left_padding();
  desc_rect.origin.y = ypos + infostyle.row_rect_padding() + title_rect.size.height;

  if (usages.has_focused_index() && (row == usages.focused_index())) {
    NSRect focused_rect = NSMakeRect(
        infostyle.window_border(), ypos, infostyle.window_width() - infostyle.window_border() * 2,
        title_rect.size.height + desc_rect.size.height + infostyle.row_rect_padding() * 2);
    [MacViewUtil::ToNSColor(infostyle.focused_background_color()) set];
    [NSBezierPath fillRect:focused_rect];
    [MacViewUtil::ToNSColor(infostyle.focused_border_color()) set];
    // Fix the border position.  Because a line should be drawn at the
    // middle point of the pixel, origin should be shifted by 0.5 unit
    // and the size should be shrinked by 1.0 unit.
    focused_rect.origin.x += 0.5;
    focused_rect.origin.y += 0.5;
    focused_rect.size.width -= 1.0;
    focused_rect.size.height -= 1.0;
    [NSBezierPath strokeRect:focused_rect];
  } else {
    if (title_style.has_background_color()) {
      NSRect rect = NSMakeRect(infostyle.window_border(), ypos,
                               infostyle.window_width() - infostyle.window_border() * 2,
                               title_rect.size.height + infostyle.row_rect_padding());
      [MacViewUtil::ToNSColor(title_style.background_color()) set];
      [NSBezierPath fillRect:rect];
    }
    if (desc_style.has_background_color()) {
      NSRect rect = NSMakeRect(infostyle.window_border(),
                               ypos + title_rect.size.height + infostyle.row_rect_padding(),
                               infostyle.window_width() - infostyle.window_border() * 2,
                               desc_rect.size.height + infostyle.row_rect_padding());
      [MacViewUtil::ToNSColor(desc_style.background_color()) set];
      [NSBezierPath fillRect:rect];
    }
  }
  [title_string drawWithRect:title_rect options:NSStringDrawingUsesLineFragmentOrigin];
  [desc_string drawWithRect:desc_rect options:NSStringDrawingUsesLineFragmentOrigin];
  return height;
}

- (NSSize)drawHorizontalView:(bool)draw_flag {
  if (!candidate_window_.has_usages()) {
    return NSMakeSize(0, 0);
  }

  const RendererStyle::InfolistStyle &infostyle = style_->infolist_style();
  const InformationList &usages = candidate_window_.usages();

  int ypos = infostyle.window_border();

  if (draw_flag && infostyle.has_caption_string()) {
    const RendererStyle::TextStyle &caption_style = infostyle.caption_style();
    const int caption_height = infostyle.caption_height();
    NSAttributedString *caption_string =
        MacViewUtil::ToNSAttributedString(infostyle.caption_string(), caption_style);
    NSRect rect =
        NSMakeRect(infostyle.window_border(), ypos,
                   infostyle.window_width() - infostyle.window_border() * 2, caption_height);
    [MacViewUtil::ToNSColor(infostyle.caption_background_color()) set];
    [NSBezierPath fillRect:rect];
    rect = NSMakeRect(
        infostyle.window_border() + infostyle.caption_padding() + caption_style.left_padding(),
        ypos + infostyle.caption_padding(),
        infostyle.window_width() - infostyle.window_border() * 2, caption_height);
    [caption_string drawWithRect:rect options:NSStringDrawingUsesLineFragmentOrigin];
  }
  ypos += infostyle.caption_height();
  for (int i = 0; i < usages.information_size(); ++i) {
    ypos += [self drawHorizontalRow:i ypos:ypos draw_flag:draw_flag];
  }
  ypos += infostyle.window_border();

  return NSMakeSize(infostyle.window_width(), ypos);
}

- (NSSize)drawVerticalView:(bool)draw_flag {
  if (!candidate_window_.has_usages()) {
    return NSMakeSize(0, 0);
  }

  const RendererStyle::InfolistStyle &infostyle =
      style_->infolist_style();
  const InformationList &usages = candidate_window_.usages();
  const RendererStyle::TextStyle &caption_style =
      infostyle.caption_style();
  const RendererStyle::TextStyle &title_style =
      infostyle.title_style();
  const RendererStyle::TextStyle &desc_style =
      infostyle.description_style();

  const int border = std::max(0, infostyle.window_border());
  const int style_row_padding =
      std::max(0, infostyle.row_rect_padding());
  const int window_height = std::max(1, infostyle.window_width());

  const VerticalEmMetrics description_em =
      MeasureVerticalEm(desc_style);
  const int row_padding =
      std::max(style_row_padding,
               std::max(1, (description_em.cross_width + 4) / 5));
  const int vertical_padding =
      std::max(style_row_padding,
               std::max(1, (description_em.inline_advance * 2 + 4) / 5));
  const int section_gap =
      std::max(1, (description_em.cross_width + 3) / 4);
  const int description_indent =
      std::max(1, description_em.inline_advance);

  const int content_height =
      std::max(1, window_height - border * 2);
  const int text_height =
      std::max(1, content_height - vertical_padding * 2);

  NSAttributedString *caption_string = nil;
  VerticalTextMeasurement caption_measurement;
  int caption_width = std::max(0, infostyle.caption_height());
  if (infostyle.has_caption_string()) {
    caption_string = MakeVerticalAttributedString(
        infostyle.caption_string(), caption_style);
    caption_measurement =
        MeasureVerticalWrappedAttributedString(
            caption_string, text_height);
    caption_width = std::max(
        caption_width,
        caption_measurement.layout_size.width +
            infostyle.caption_padding() * 2 +
            caption_style.left_padding() +
            caption_style.right_padding());
  }

  std::vector<MacVerticalInfolistLayout::ItemMetrics> metrics;
  std::vector<VerticalTextMeasurement> title_measurements;
  std::vector<VerticalTextMeasurement> description_measurements;

  metrics.reserve(usages.information_size());
  title_measurements.reserve(usages.information_size());
  description_measurements.reserve(usages.information_size());

  for (int i = 0; i < usages.information_size(); ++i) {
    const Information &info = usages.information(i);
    MacVerticalInfolistLayout::ItemMetrics item;

    NSAttributedString *title_string =
        MakeVerticalAttributedString(info.title(), title_style);
    const VerticalTextMeasurement title_measurement =
        MeasureVerticalWrappedAttributedString(
            title_string, text_height);
    item.title_size = title_measurement.layout_size;
    item.title_left_padding = title_style.left_padding();
    item.title_right_padding = title_style.right_padding();

    item.description_top_indent =
        info.title().empty()
            ? 0
            : std::clamp(
                  description_indent, 0,
                  std::max(0, text_height - 1));
    const int description_height =
        std::max(1, text_height - item.description_top_indent);

    NSAttributedString *description_string =
        MakeVerticalAttributedString(
            info.description(), desc_style);
    const VerticalTextMeasurement description_measurement =
        MeasureVerticalWrappedAttributedString(
            description_string, description_height);
    item.description_size = description_measurement.layout_size;
    item.description_left_padding = desc_style.left_padding();
    item.description_right_padding = desc_style.right_padding();

    metrics.push_back(item);
    title_measurements.push_back(title_measurement);
    description_measurements.push_back(description_measurement);
  }

  MacVerticalInfolistLayout::Parameters parameters;
  parameters.window_border = border;
  parameters.row_padding = row_padding;
  parameters.vertical_padding = vertical_padding;
  parameters.section_gap = section_gap;
  parameters.caption_width = caption_width;
  parameters.window_height = window_height;

  MacVerticalInfolistLayout layout;
  layout.Layout(metrics, parameters);
  const mozc::Size layout_size = layout.window_size();

  if (!draw_flag) {
    return NSMakeSize(layout_size.width, layout_size.height);
  }

  const mozc::Rect caption_rect = layout.caption_rect();
  if (!caption_rect.IsRectEmpty()) {
    const NSRect caption_ns_rect =
        NSRectFromMozcRect(caption_rect);
    [MacViewUtil::ToNSColor(
        infostyle.caption_background_color()) set];
    [NSBezierPath fillRect:caption_ns_rect];

    if (caption_string != nil &&
        [caption_string length] > 0 &&
        caption_measurement.frame_width > 0) {
      const int caption_padding =
          std::max(0, infostyle.caption_padding());
      const int caption_inline_padding =
          std::max(caption_padding, vertical_padding);
      const CGFloat left =
          caption_rect.Left() + caption_padding +
          std::max(0, caption_style.left_padding());
      const CGFloat right =
          caption_rect.Right() - caption_padding -
          std::max(0, caption_style.right_padding());
      const CGFloat top =
          caption_rect.Top() + caption_inline_padding;
      const CGFloat bottom =
          caption_rect.Bottom() - caption_inline_padding;
      if (right > left && bottom > top) {
        DrawVerticalAttributedString(
            caption_string,
            NSMakeRect(left, top, right - left, bottom - top),
            caption_measurement.frame_width);
      }
    }
  }

  for (int i = 0; i < usages.information_size(); ++i) {
    const mozc::Rect item_rect = layout.GetItemRect(i);
    const mozc::Rect title_rect = layout.GetTitleRect(i);
    const mozc::Rect description_rect =
        layout.GetDescriptionRect(i);

    if (usages.has_focused_index() &&
        i == usages.focused_index()) {
      NSRect focused_rect = NSRectFromMozcRect(item_rect);
      [MacViewUtil::ToNSColor(
          infostyle.focused_background_color()) set];
      [NSBezierPath fillRect:focused_rect];
      [MacViewUtil::ToNSColor(
          infostyle.focused_border_color()) set];
      focused_rect.origin.x += 0.5;
      focused_rect.origin.y += 0.5;
      focused_rect.size.width =
          std::max<CGFloat>(0.0, focused_rect.size.width - 1.0);
      focused_rect.size.height =
          std::max<CGFloat>(0.0, focused_rect.size.height - 1.0);
      [NSBezierPath strokeRect:focused_rect];
    }

    const Information &info = usages.information(i);
    NSAttributedString *title_string =
        MakeVerticalAttributedString(info.title(), title_style);
    if (!title_rect.IsRectEmpty() &&
        title_string != nil &&
        [title_string length] > 0 &&
        title_measurements[i].frame_width > 0) {
      DrawVerticalAttributedString(
          title_string, NSRectFromMozcRect(title_rect),
          title_measurements[i].frame_width);
    }

    NSAttributedString *description_string =
        MakeVerticalAttributedString(
            info.description(), desc_style);
    if (!description_rect.IsRectEmpty() &&
        description_string != nil &&
        [description_string length] > 0 &&
        description_measurements[i].frame_width > 0) {
      DrawVerticalAttributedString(
          description_string,
          NSRectFromMozcRect(description_rect),
          description_measurements[i].frame_width);
    }
  }

  return NSMakeSize(layout_size.width, layout_size.height);
}

- (NSSize)drawView:(bool)draw_flag {
  return IsVerticalWriting(writingDirection_)
             ? [self drawVerticalView:draw_flag]
             : [self drawHorizontalView:draw_flag];
}

- (NSSize)updateLayout {
  return [self drawView:false];
}

- (void)drawRect:(NSRect)rect {
  if (style_ == nullptr) {
    return;
  }

  NSRectFillUsingOperation(self.bounds, NSCompositingOperationClear);

  const RendererStyle::InfolistStyle &infostyle = style_->infolist_style();
  const CGFloat borderWidth =
      std::max<CGFloat>(1.0, infostyle.window_border());
  const NSRect pathBounds =
      NSInsetRect(self.bounds, borderWidth / 2.0, borderWidth / 2.0);
  const CGFloat configuredRadius = static_cast<CGFloat>(
      RendererStyleHandler::GetCandidateWindowCornerRadius(
          RendererStyleHandler::RendererStyleType::kCandidate));
  const CGFloat maximumRadius = std::max<CGFloat>(
      0.0, std::min(NSWidth(pathBounds), NSHeight(pathBounds)) / 2.0);
  const CGFloat effectiveRadius =
      std::min(std::max<CGFloat>(configuredRadius, 0.0), maximumRadius);
  NSBezierPath *windowPath =
      [NSBezierPath bezierPathWithRoundedRect:pathBounds
                                     xRadius:effectiveRadius
                                     yRadius:effectiveRadius];

  [NSGraphicsContext saveGraphicsState];
  [windowPath addClip];
  [NSBezierPath setDefaultLineWidth:infostyle.window_border()];
  [NSBezierPath setDefaultLineJoinStyle:NSLineJoinStyleMiter];

  if (infostyle.title_style().has_background_color()) {
    [MacViewUtil::ToNSColor(
        infostyle.title_style().background_color()) setFill];
  } else {
    [NSColor.whiteColor setFill];
  }
  [NSBezierPath fillRect:self.bounds];
  [self drawView:true];

  [NSGraphicsContext restoreGraphicsState];

  [MacViewUtil::ToNSColor(infostyle.border_color()) setStroke];
  [windowPath setLineWidth:borderWidth];
  [windowPath stroke];
}

@end
