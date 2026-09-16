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

#import "renderer/mac/CandidateView.h"

#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "client/client_interface.h"
#include "protocol/commands.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/mac/mac_vertical_candidate_layout.h"
#include "renderer/mac/mac_view_util.h"
#include "renderer/renderer_style_handler.h"
#include "renderer/table_layout.h"

using mozc::client::SendCommandInterface;
using mozc::commands::CandidateWindow;
using mozc::commands::Output;
using mozc::commands::SessionCommand;
using mozc::renderer::RendererStyleHandler;
using mozc::renderer::ColumnType;
using mozc::renderer::kColumnShortcut;
using mozc::renderer::kColumnGap1;
using mozc::renderer::kColumnCandidate;
using mozc::renderer::kColumnDescription;
using mozc::renderer::kNumberOfColumns;
using mozc::renderer::TableLayout;
using mozc::renderer::mac::MacVerticalCandidateLayout;
using mozc::renderer::mac::MacViewUtil;
using mozc::renderer::mac::WritingDirection;

namespace {

constexpr int kVerticalCrossAxisEdgePadding = 2;
constexpr int kVerticalCandidateGap = 1;
constexpr int kVerticalCardHorizontalPadding = 2;
constexpr int kVerticalCardVerticalPadding = 7;
constexpr int kVerticalShortcutBodyGap = 5;
constexpr int kVerticalValueDescriptionGap = 12;

constexpr int kVerticalSuggestionCrossAxisEdgePadding = 2;
constexpr int kVerticalSuggestionCandidateGap = 1;
constexpr int kVerticalSuggestionCardVerticalPadding = 4;
constexpr int kVerticalSuggestionShortcutBodyGap = 3;
constexpr int kVerticalSuggestionValueDescriptionGap = 7;

constexpr int kVerticalCoreTextSafety = 4;
constexpr int kVerticalInformationMarkerGap = 2;
constexpr int kVerticalInformationMarkerWidth = 14;
constexpr int kVerticalInformationMarkerHeight = 2;

NSDictionary *VerticalFrameAttributes() {
  return @{
    (__bridge id)kCTFrameProgressionAttributeName :
        @(kCTFrameProgressionRightToLeft)
  };
}

bool ShouldRenderVerticalFooterText(
    const CandidateWindow &candidate_window, const std::string &text) {
  return candidate_window.category() != mozc::commands::SUGGESTION ||
         text != "Tabキーで選択";
}

NSAttributedString *MakeVerticalAttributedString(
    const std::string &text,
    const mozc::renderer::RendererStyle::TextStyle &style) {
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

  NSColor *foreground = [base attribute:NSForegroundColorAttributeName
                                atIndex:0
                         effectiveRange:nullptr];
  if (foreground != nil) {
    NSColor *rgb = [foreground colorUsingColorSpace:
        [NSColorSpace deviceRGBColorSpace]];
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

struct VerticalTextMeasurement {
  mozc::Size layout_size;
  int frame_width = 0;
};

VerticalTextMeasurement MeasureVerticalAttributedString(
    const NSAttributedString *text) {
  if (text == nil || [text length] == 0) {
    return VerticalTextMeasurement();
  }

  CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(
      (__bridge CFAttributedStringRef)text);
  if (framesetter == nullptr) {
    return VerticalTextMeasurement();
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

  // Core Text needs this technical frame width for reliable vertical
  // full-fit. Keep it independent from the visible candidate-card width.
  const CGFloat frame_width = std::ceil(std::max<CGFloat>(
      line_floor, std::max<CGFloat>(1.0, suggested.width) + 1.0));

  CGFloat low = 1.0;
  CGFloat high = std::max({
      std::ceil(std::max<CGFloat>(1.0, suggested.height)) + 4.0,
      line_floor + 4.0,
      point_size * std::max<NSUInteger>(1, [text length]) * 2.5 + 32.0,
  });

  const CFIndex total_length = static_cast<CFIndex>([text length]);
  while (VisibleVerticalStringLength(framesetter, frame_width, high) <
         total_length) {
    high *= 2.0;
    if (high > 20000.0) {
      break;
    }
  }

  for (int iteration = 0; iteration < 24; ++iteration) {
    const CGFloat middle = (low + high) / 2.0;
    if (VisibleVerticalStringLength(framesetter, frame_width, middle) >=
        total_length) {
      high = middle;
    } else {
      low = middle;
    }
  }

  const CGFloat visible_fit_height = std::ceil(high + 1.0);
  const CGFloat frame_height = std::ceil(std::max({
      std::max<CGFloat>(1.0, suggested.height),
      visible_fit_height,
      line_floor,
  }));

  const CFIndex visible_length =
      VisibleVerticalStringLength(framesetter, frame_width, frame_height);

  // Keep the visible card width independent from Core Text's technical
  // typographic cross-axis.  In vertical mode ASCII can report about twice the
  // nominal CJK cross-axis even though its rasterized glyph ink is much
  // narrower.  Use one normal line box as the floor and expand only when the
  // actual glyph-path ink really exceeds it.  The full-fit technical frame
  // width above remains unchanged.
  CGFloat visual_width = line_floor;
  CTLineRef line = CTLineCreateWithAttributedString(
      (__bridge CFAttributedStringRef)text);
  if (line != nullptr) {
    const CGRect glyph_bounds =
        CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);
    if (std::isfinite(glyph_bounds.size.height)) {
      visual_width = std::ceil(std::max<CGFloat>(
          line_floor, std::max<CGFloat>(1.0, glyph_bounds.size.height)));
    }
    CFRelease(line);
  }

  CFRelease(framesetter);

  if (fit_range.location != 0 ||
      fit_range.length != total_length ||
      visible_length != total_length) {
    LOG(WARNING) << "Core Text vertical measurement mismatch: suggested="
                 << fit_range.location << "+" << fit_range.length
                 << " visible=" << visible_length
                 << " total=" << total_length;
  }

  return VerticalTextMeasurement{
      mozc::Size(
          std::max(1, static_cast<int>(visual_width)),
          std::max(1, static_cast<int>(frame_height))),
      std::max(1, static_cast<int>(frame_width)),
  };
}

void DrawVerticalAttributedString(const NSAttributedString *text,
                                  const NSRect &rect,
                                  CGFloat frame_width) {
  if (text == nil || [text length] == 0 ||
      rect.size.width <= 0 || rect.size.height <= 0) {
    return;
  }

  CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(
      (__bridge CFAttributedStringRef)text);
  if (framesetter == nullptr) {
    return;
  }

  // Keep the proven full-fit Core Text frame width even when the visible
  // layout/card is narrower. Center that technical frame over the visual rect,
  // then apply the existing direct CTFrame line-origin correction.
  const CGFloat draw_width =
      std::max<CGFloat>(1.0, frame_width) + kVerticalCoreTextSafety;
  const NSRect draw_rect = NSMakeRect(
      NSMidX(rect) - draw_width / 2.0, rect.origin.y,
      draw_width, rect.size.height);
  const CGRect local_rect =
      CGRectMake(0, 0, draw_rect.size.width, draw_rect.size.height);
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

      // kCTFrameProgressionRightToLeft places the vertical line off the
      // geometric center of the frame.  Center the actual CTFrame line origin
      // directly; adding or subtracting half a glyph width over-corrects it.
      correction_x = draw_rect.size.width / 2.0 - origins[0].x;
    }
    if (line_count != 1) {
      LOG(WARNING) << "Unexpected vertical CTFrame line count: "
                   << line_count;
    }
  }

  CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
  if (context != nullptr && frame != nullptr) {
    // CandidateView is flipped. Core Text draws in Quartz coordinates, so
    // compensate locally rather than changing the NSView's global geometry.
    CGContextSaveGState(context);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    CGContextTranslateCTM(
        context, NSMinX(draw_rect) + correction_x, NSMaxY(draw_rect));
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

}  // namespace

// Those constants and most rendering logic is as same as Windows
// native candidate window.
// TODO(mukai): integrate and share the code among Win and Mac.

// Private method declarations.
@interface CandidateView ()
- (void)initializeDefaultStyle;
- (void)reloadStyleForCandidateWindow;
- (void)updateStyleDependentResources;
- (NSSize)updateVerticalLayout;
- (NSSize)verticalFooterSize;
- (void)drawVerticalRect:(NSRect)rect;
- (void)drawVerticalCandidate:(int)row;
- (void)drawVerticalFooter;

// Draw the |row|-th row.
- (void)drawRow:(int)row;

// Draw footer
- (void)drawFooter;

// Draw scroll bar
- (void)drawVScrollBar;
@end

@implementation CandidateView {
  const NSImage *logoImage_;
  int columnMinimumWidth_;

  mozc::commands::CandidateWindow candidate_window_;
  mozc::renderer::TableLayout tableLayout_;
  mozc::renderer::mac::MacVerticalCandidateLayout verticalLayout_;
  mozc::renderer::RendererStyle style_;
  mozc::renderer::mac::WritingDirection writingDirection_;
  CGFloat cornerRadius_;

  // The row which has focused background.
  int focusedRow_;

  // Cache of attributed strings which is allocated at updateLayout.
  NSArray *candidateStringsCache_;

  // Core Text's technical vertical frame widths are intentionally wider than
  // the visual card widths. Keep them per candidate so drawing can preserve
  // full-fit while MacVerticalCandidateLayout stays purely visual.
  std::vector<int> verticalValueFrameWidths_;
  std::vector<int> verticalDescriptionFrameWidths_;

  // |command_sender_| holds a callback for mouse clicks.
  mozc::client::SendCommandInterface *command_sender_;
}

#pragma mark initialization

- (id)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    [self initializeDefaultStyle];
    focusedRow_ = -1;
    writingDirection_ = WritingDirection::kHorizontal;
  }
  return self;
}

- (void)initializeDefaultStyle {
  if (!RendererStyleHandler::GetRendererStyleForWindowType(
          RendererStyleHandler::RendererStyleType::kCandidate, &style_)) {
    RendererStyleHandler::GetDefaultRendererStyle(&style_);
  }
  cornerRadius_ = static_cast<CGFloat>(
      RendererStyleHandler::GetCandidateWindowCornerRadius(
          RendererStyleHandler::RendererStyleType::kCandidate));
  [self updateStyleDependentResources];

  // Default line width is specified as 1.0 pt, but the renderer expects
  // a one-pixel logical border.
  [NSBezierPath setDefaultLineWidth:1.0];
  [NSBezierPath setDefaultLineJoinStyle:NSLineJoinStyleMiter];
}

- (void)reloadStyleForCandidateWindow {
  const RendererStyleHandler::RendererStyleType style_type =
      RendererStyleHandler::GetRendererStyleTypeForCandidateWindow(
          candidate_window_);

  if (!RendererStyleHandler::GetRendererStyleForWindowType(style_type,
                                                            &style_)) {
    RendererStyleHandler::GetDefaultRendererStyle(&style_);
  }
  cornerRadius_ = static_cast<CGFloat>(
      RendererStyleHandler::GetCandidateWindowCornerRadius(style_type));

  [self updateStyleDependentResources];
}

- (void)updateStyleDependentResources {
  const std::string &logo_file_name = style_.logo_file_name();
  logoImage_ =
      [NSImage imageNamed:[NSString stringWithUTF8String:logo_file_name.c_str()]];

  if (logoImage_) {
    // Fix the image size. Sometimes the logical size can be smaller than the
    // underlying representation because of transparent margins.
    const NSArray *logoReps = [logoImage_ representations];
    if (logoReps && [logoReps count] > 0) {
      const NSImageRep *representation = [logoReps objectAtIndex:0];
      [logoImage_
          setSize:NSMakeSize([representation pixelsWide],
                             [representation pixelsHigh])];
    }
  }

  const NSAttributedString *minimumWidthText =
      MacViewUtil::ToNSAttributedString(
          style_.column_minimum_width_string(), style_.candidate_style());
  columnMinimumWidth_ =
      std::max(1, static_cast<int>([minimumWidthText size].width));
}

- (void)setCandidateWindow:(const CandidateWindow *)candidate_window {
  candidate_window_ = *candidate_window;
  [self reloadStyleForCandidateWindow];
}

- (void)setWritingDirection:(WritingDirection)writing_direction {
  if (writingDirection_ == writing_direction) {
    return;
  }
  writingDirection_ = writing_direction;
  candidateStringsCache_ = nil;
}

- (void)setSendCommandInterface:(SendCommandInterface *)command_sender {
  command_sender_ = command_sender;
}

// Override of NSView.
- (BOOL)isFlipped {
  return YES;
}

- (void)dealloc {
  candidateStringsCache_ = nil;
}

- (const TableLayout *)tableLayout {
  return &tableLayout_;
}

- (mozc::Point)candidateAnchorOffset {
  if (writingDirection_ == WritingDirection::kVertical) {
    if (verticalLayout_.candidate_count() == 0) {
      return mozc::Point();
    }
    const mozc::Rect value_rect = verticalLayout_.GetValueRect(0);
    if (!value_rect.IsRectEmpty()) {
      return mozc::Point(value_rect.Left(), value_rect.Top());
    }
    const mozc::Rect candidate_rect = verticalLayout_.GetCandidateRect(0);
    return mozc::Point(candidate_rect.Left(), candidate_rect.Top());
  }

  return mozc::Point(
      tableLayout_.GetColumnRect(kColumnCandidate).Left(), 0);
}

- (mozc::Rect)cascadingAnchorRectForRow:(size_t)row {
  if (writingDirection_ == WritingDirection::kVertical) {
    return verticalLayout_.GetCandidateRect(row);
  }

  if (row >= static_cast<size_t>(tableLayout_.number_of_rows())) {
    return mozc::Rect();
  }
  mozc::Rect rect = tableLayout_.GetRowRect(static_cast<int>(row));
  // Preserve CandidateController's historical behavior: cascading placement
  // treats the scrollbar as part of the selected horizontal row.
  rect.size.width += tableLayout_.GetVScrollBarRect().Width();
  return rect;
}

#pragma mark drawing

- (NSSize)updateLayout {
  if (writingDirection_ == WritingDirection::kVertical) {
    return [self updateVerticalLayout];
  }

  candidateStringsCache_ = nil;
  verticalValueFrameWidths_.clear();
  verticalDescriptionFrameWidths_.clear();
  tableLayout_.Initialize(candidate_window_.candidate_size(), kNumberOfColumns);
  tableLayout_.SetWindowBorder(style_.window_border());

  // calculating focusedRow_
  if (candidate_window_.has_focused_index() && candidate_window_.candidate_size() > 0) {
    const int focusedIndex = candidate_window_.focused_index();
    focusedRow_ = focusedIndex - candidate_window_.candidate(0).index();
  } else {
    focusedRow_ = -1;
  }

  // Reserve footer space.
  if (candidate_window_.has_footer()) {
    NSSize footerSize = NSZeroSize;

    const mozc::commands::Footer &footer = candidate_window_.footer();

    if (footer.has_label()) {
      const NSAttributedString *footerLabel =
          MacViewUtil::ToNSAttributedString(footer.label(), style_.footer_style());
      const NSSize footerLabelSize =
          MacViewUtil::applyTheme([footerLabel size], style_.footer_style());
      footerSize.width += footerLabelSize.width;
      footerSize.height = std::max(footerSize.height, footerLabelSize.height);
    }

    if (footer.has_sub_label()) {
      const NSAttributedString *footerSubLabel =
          MacViewUtil::ToNSAttributedString(footer.sub_label(), style_.footer_sub_label_style());
      const NSSize footerSubLabelSize =
          MacViewUtil::applyTheme([footerSubLabel size], style_.footer_sub_label_style());
      footerSize.width += footerSubLabelSize.width;
      footerSize.height = std::max(footerSize.height, footerSubLabelSize.height);
    }

    if (footer.logo_visible() && logoImage_) {
      const NSSize logoSize = [logoImage_ size];
      footerSize.width += logoSize.width;
      footerSize.height = std::max(footerSize.height, logoSize.height);
    }

    if (footer.index_visible()) {
      const int focusedIndex = candidate_window_.focused_index();
      const int totalItems = candidate_window_.size();
      const NSString *footerIndex =
          [NSString stringWithFormat:@"%d/%d", focusedIndex + 1, totalItems];
      const NSAttributedString *footerAttributedIndex =
          MacViewUtil::ToNSAttributedString([footerIndex UTF8String], style_.footer_style());
      const NSSize footerIndexSize =
          MacViewUtil::applyTheme([footerAttributedIndex size], style_.footer_style());
      footerSize.width += footerIndexSize.width;
      footerSize.height = std::max(footerSize.height, footerIndexSize.height);
    }

    footerSize.height += style_.footer_border_colors_size();
    tableLayout_.EnsureFooterSize(MacViewUtil::ToSize(footerSize));
  }

  tableLayout_.SetRowRectPadding(style_.row_rect_padding());
  if (candidate_window_.candidate_size() < candidate_window_.size()) {
    tableLayout_.SetVScrollBar(style_.scrollbar_width());
  }

  const NSAttributedString *gap1 =
      MacViewUtil::ToNSAttributedString(" ", style_.gap1_style());
  tableLayout_.EnsureCellSize(kColumnGap1, MacViewUtil::ToSize([gap1 size]));

  NSMutableArray *newCache = [[NSMutableArray array] init];
  for (int i = 0; i < candidate_window_.candidate_size(); ++i) {
    const CandidateWindow::Candidate &candidate = candidate_window_.candidate(i);
    const NSAttributedString *shortcut = MacViewUtil::ToNSAttributedString(
        candidate.annotation().shortcut(), style_.shortcut_style());
    std::string value = candidate.value();
    if (candidate.annotation().has_prefix()) {
      value.insert(0, candidate.annotation().prefix());  // Prepend the prefix() to value.
    }
    if (candidate.annotation().has_suffix()) {
      value.append(candidate.annotation().suffix());
    }
    if (!value.empty()) {
      value.append("  ");
    }

    const NSAttributedString *candidateValue =
        MacViewUtil::ToNSAttributedString(value, style_.candidate_style());
    const NSAttributedString *description = MacViewUtil::ToNSAttributedString(
        candidate.annotation().description(), style_.description_style());
    if ([shortcut length] > 0) {
      const NSSize shortcutSize =
          MacViewUtil::applyTheme([shortcut size], style_.shortcut_style());
      tableLayout_.EnsureCellSize(kColumnShortcut, MacViewUtil::ToSize(shortcutSize));
    }
    if ([candidateValue length] > 0) {
      const NSSize valueSize =
          MacViewUtil::applyTheme([candidateValue size], style_.candidate_style());
      tableLayout_.EnsureCellSize(kColumnCandidate, MacViewUtil::ToSize(valueSize));
    }
    if ([description length] > 0) {
      const NSSize descriptionSize =
          MacViewUtil::applyTheme([description size], style_.description_style());
      tableLayout_.EnsureCellSize(kColumnDescription, MacViewUtil::ToSize(descriptionSize));
    }

    [newCache
        addObject:[NSArray arrayWithObjects:shortcut, gap1, candidateValue, description, nil]];
  }

  tableLayout_.EnsureColumnsWidth(kColumnCandidate, kColumnDescription, columnMinimumWidth_);

  candidateStringsCache_ = newCache;
  tableLayout_.FreezeLayout();
  return MacViewUtil::ToNSSize(tableLayout_.GetTotalSize());
}

- (NSSize)verticalFooterSize {
  if (!candidate_window_.has_footer()) {
    return NSZeroSize;
  }

  NSSize footerSize = NSZeroSize;
  const mozc::commands::Footer &footer = candidate_window_.footer();

  if (footer.has_label() &&
      ShouldRenderVerticalFooterText(candidate_window_, footer.label())) {
    const NSAttributedString *footerLabel =
        MacViewUtil::ToNSAttributedString(footer.label(), style_.footer_style());
    const NSSize size =
        MacViewUtil::applyTheme([footerLabel size], style_.footer_style());
    footerSize.width += size.width;
    footerSize.height = std::max(footerSize.height, size.height);
  }

  if (footer.has_sub_label() &&
      ShouldRenderVerticalFooterText(candidate_window_, footer.sub_label())) {
    const NSAttributedString *footerSubLabel =
        MacViewUtil::ToNSAttributedString(footer.sub_label(),
                                          style_.footer_sub_label_style());
    const NSSize size = MacViewUtil::applyTheme(
        [footerSubLabel size], style_.footer_sub_label_style());
    footerSize.width += size.width;
    footerSize.height = std::max(footerSize.height, size.height);
  }

  if (footer.logo_visible() && logoImage_) {
    const NSSize logoSize = [logoImage_ size];
    footerSize.width += logoSize.width;
    footerSize.height = std::max(footerSize.height, logoSize.height);
  }

  if (footer.index_visible()) {
    const NSString *footerIndex = [NSString
        stringWithFormat:@"%d/%d", candidate_window_.focused_index() + 1,
                         candidate_window_.size()];
    const NSAttributedString *footerAttributedIndex =
        MacViewUtil::ToNSAttributedString([footerIndex UTF8String],
                                          style_.footer_style());
    const NSSize size = MacViewUtil::applyTheme(
        [footerAttributedIndex size], style_.footer_style());
    footerSize.width += size.width;
    footerSize.height = std::max(footerSize.height, size.height);
  }

  if (footerSize.height > 0) {
    footerSize.height += style_.footer_border_colors_size();
  }
  return footerSize;
}

- (NSSize)updateVerticalLayout {
  candidateStringsCache_ = nil;
  verticalValueFrameWidths_.clear();
  verticalDescriptionFrameWidths_.clear();
  verticalValueFrameWidths_.reserve(candidate_window_.candidate_size());
  verticalDescriptionFrameWidths_.reserve(candidate_window_.candidate_size());

  if (candidate_window_.has_focused_index() &&
      candidate_window_.candidate_size() > 0) {
    const int focusedIndex = candidate_window_.focused_index();
    focusedRow_ = focusedIndex - candidate_window_.candidate(0).index();
  } else {
    focusedRow_ = -1;
  }

  std::vector<MacVerticalCandidateLayout::CandidateMetrics> metrics;
  metrics.reserve(candidate_window_.candidate_size());

  NSMutableArray *newCache = [[NSMutableArray array] init];
  const NSAttributedString *empty =
      [[NSAttributedString alloc] initWithString:@""];

  for (int i = 0; i < candidate_window_.candidate_size(); ++i) {
    const CandidateWindow::Candidate &candidate = candidate_window_.candidate(i);

    const NSAttributedString *shortcut = MacViewUtil::ToNSAttributedString(
        candidate.annotation().shortcut(), style_.shortcut_style());

    std::string value = candidate.value();
    if (candidate.annotation().has_prefix()) {
      value.insert(0, candidate.annotation().prefix());
    }
    if (candidate.annotation().has_suffix()) {
      value.append(candidate.annotation().suffix());
    }

    const NSAttributedString *candidateValue = MakeVerticalAttributedString(
        value, style_.candidate_style());
    const NSAttributedString *description = MakeVerticalAttributedString(
        candidate.annotation().description(), style_.description_style());

    const NSSize shortcutSize = [shortcut length] > 0
        ? MacViewUtil::applyTheme([shortcut size], style_.shortcut_style())
        : NSZeroSize;
    const VerticalTextMeasurement valueMeasurement =
        MeasureVerticalAttributedString(candidateValue);
    const VerticalTextMeasurement descriptionMeasurement =
        MeasureVerticalAttributedString(description);
    const mozc::Size informationMarkerSize =
        candidate.has_information_id()
            ? mozc::Size(kVerticalInformationMarkerWidth,
                         kVerticalInformationMarkerHeight)
            : mozc::Size();

    metrics.push_back(MacVerticalCandidateLayout::CandidateMetrics{
        MacViewUtil::ToSize(shortcutSize), valueMeasurement.layout_size,
        descriptionMeasurement.layout_size, informationMarkerSize});
    verticalValueFrameWidths_.push_back(valueMeasurement.frame_width);
    verticalDescriptionFrameWidths_.push_back(
        descriptionMeasurement.frame_width);

    // Preserve the existing four-column cache indexing so horizontal and
    // vertical drawing share the same semantic string slots. The gap entry is
    // intentionally empty because the vertical layout owns spacing as geometry.
    [newCache addObject:[NSArray arrayWithObjects:shortcut, empty, candidateValue,
                                                   description, nil]];
  }

  const bool isPassiveSuggestion =
      candidate_window_.category() == mozc::commands::SUGGESTION;

  MacVerticalCandidateLayout::Parameters parameters;
  parameters.window_border = style_.window_border();
  parameters.cross_axis_edge_padding =
      isPassiveSuggestion ? kVerticalSuggestionCrossAxisEdgePadding
                          : kVerticalCrossAxisEdgePadding;
  parameters.candidate_gap =
      isPassiveSuggestion ? kVerticalSuggestionCandidateGap
                          : kVerticalCandidateGap;
  parameters.card_horizontal_padding = kVerticalCardHorizontalPadding;
  parameters.card_vertical_padding =
      isPassiveSuggestion ? kVerticalSuggestionCardVerticalPadding
                          : kVerticalCardVerticalPadding;
  parameters.shortcut_body_gap =
      isPassiveSuggestion ? kVerticalSuggestionShortcutBodyGap
                          : kVerticalShortcutBodyGap;
  parameters.value_description_gap =
      isPassiveSuggestion ? kVerticalSuggestionValueDescriptionGap
                          : kVerticalValueDescriptionGap;
  parameters.information_marker_gap = kVerticalInformationMarkerGap;
  parameters.footer_size = MacViewUtil::ToSize([self verticalFooterSize]);

  verticalLayout_.Initialize(metrics, parameters);
  candidateStringsCache_ = newCache;
  return MacViewUtil::ToNSSize(verticalLayout_.GetTotalSize());
}

- (void)drawRect:(NSRect)rect {
  if (!Category_IsValid(candidate_window_.category())) {
    LOG(WARNING) << "Unknown candidates category: "
                 << candidate_window_.category();
    return;
  }

  if (writingDirection_ == WritingDirection::kVertical) {
    [self drawVerticalRect:rect];
    return;
  }

  // The NSPanel is transparent so the rounded silhouette must explicitly
  // clear stale pixels before drawing a new frame.
  NSRectFillUsingOperation(self.bounds, NSCompositingOperationClear);

  const NSRect pathBounds = NSInsetRect(self.bounds, 0.5, 0.5);
  const CGFloat maximumRadius = std::max<CGFloat>(
      0.0, std::min(NSWidth(pathBounds), NSHeight(pathBounds)) / 2.0);
  const CGFloat effectiveRadius =
      std::min(std::max<CGFloat>(cornerRadius_, 0.0), maximumRadius);
  NSBezierPath *windowPath =
      [NSBezierPath bezierPathWithRoundedRect:pathBounds
                                     xRadius:effectiveRadius
                                     yRadius:effectiveRadius];

  [NSGraphicsContext saveGraphicsState];
  [windowPath addClip];

  // Paint the complete view before drawing individual cells. Otherwise,
  // column gaps and unused layout regions would remain transparent.
  if (style_.candidate_style().has_background_color()) {
    [MacViewUtil::ToNSColor(
        style_.candidate_style().background_color()) set];
  } else {
    [NSColor.windowBackgroundColor set];
  }
  [NSBezierPath fillRect:self.bounds];

  for (int i = 0; i < candidate_window_.candidate_size(); ++i) {
    [self drawRow:i];
  }

  if (candidate_window_.candidate_size() < candidate_window_.size()) {
    [self drawVScrollBar];
  }
  [self drawFooter];

  [NSGraphicsContext restoreGraphicsState];

  // Draw the rounded outer border last so it remains crisp at the clip edge.
  [MacViewUtil::ToNSColor(style_.border_color()) setStroke];
  [windowPath setLineWidth:1.0];
  [windowPath stroke];
}

- (void)drawVerticalRect:(NSRect)rect {
  (void)rect;
  // The outer silhouette intentionally mirrors the proven horizontal path.
  NSRectFillUsingOperation(self.bounds, NSCompositingOperationClear);

  const NSRect pathBounds = NSInsetRect(self.bounds, 0.5, 0.5);
  const CGFloat maximumRadius = std::max<CGFloat>(
      0.0, std::min(NSWidth(pathBounds), NSHeight(pathBounds)) / 2.0);
  const CGFloat effectiveRadius =
      std::min(std::max<CGFloat>(cornerRadius_, 0.0), maximumRadius);
  NSBezierPath *windowPath =
      [NSBezierPath bezierPathWithRoundedRect:pathBounds
                                     xRadius:effectiveRadius
                                     yRadius:effectiveRadius];

  [NSGraphicsContext saveGraphicsState];
  [windowPath addClip];

  if (style_.candidate_style().has_background_color()) {
    [MacViewUtil::ToNSColor(
        style_.candidate_style().background_color()) set];
  } else {
    [NSColor.windowBackgroundColor set];
  }
  [NSBezierPath fillRect:self.bounds];

  for (int i = 0; i < candidate_window_.candidate_size(); ++i) {
    [self drawVerticalCandidate:i];
  }
  [self drawVerticalFooter];

  [NSGraphicsContext restoreGraphicsState];

  [MacViewUtil::ToNSColor(style_.border_color()) setStroke];
  [windowPath setLineWidth:1.0];
  [windowPath stroke];
}

- (void)drawVerticalCandidate:(int)row {
  if (row < 0 ||
      static_cast<size_t>(row) >= verticalLayout_.candidate_count()) {
    return;
  }

  NSRect candidateRect =
      MacViewUtil::ToNSRect(verticalLayout_.GetCandidateRect(row));

  if (row == focusedRow_) {
    [MacViewUtil::ToNSColor(style_.focused_background_color()) set];
    [NSBezierPath fillRect:candidateRect];
    [MacViewUtil::ToNSColor(style_.focused_border_color()) set];
    NSRect borderRect = candidateRect;
    borderRect.origin.x += 0.5;
    borderRect.origin.y += 0.5;
    borderRect.size.width -= 1.0;
    borderRect.size.height -= 1.0;
    if (borderRect.size.width > 0 && borderRect.size.height > 0) {
      [NSBezierPath strokeRect:borderRect];
    }
  } else {
    auto drawBackground = [&](const mozc::Rect &logical_rect,
                              const mozc::renderer::RendererStyle::TextStyle
                                  &text_style) {
      if (!logical_rect.IsRectEmpty() &&
          text_style.has_background_color()) {
        [MacViewUtil::ToNSColor(text_style.background_color()) set];
        [NSBezierPath fillRect:MacViewUtil::ToNSRect(logical_rect)];
      }
    };
    drawBackground(verticalLayout_.GetShortcutRect(row),
                   style_.shortcut_style());
    drawBackground(verticalLayout_.GetValueRect(row),
                   style_.candidate_style());
    drawBackground(verticalLayout_.GetDescriptionRect(row),
                   style_.description_style());
  }

  NSArray<NSAttributedString *> *candidate =
      [candidateStringsCache_ objectAtIndex:row];

  const NSAttributedString *shortcut =
      [candidate objectAtIndex:kColumnShortcut];
  const NSRect shortcutRect =
      MacViewUtil::ToNSRect(verticalLayout_.GetShortcutRect(row));
  if ([shortcut length] > 0 && shortcutRect.size.width > 0 &&
      shortcutRect.size.height > 0) {
    NSPoint position = shortcutRect.origin;
    position.x += style_.shortcut_style().left_padding();
    position.y += (shortcutRect.size.height - [shortcut size].height) / 2;
    [shortcut drawAtPoint:position];
  }

  const size_t verticalRow = static_cast<size_t>(row);
  const CGFloat valueFrameWidth =
      verticalRow < verticalValueFrameWidths_.size()
          ? verticalValueFrameWidths_[verticalRow]
          : verticalLayout_.GetValueRect(verticalRow).Width();
  const CGFloat descriptionFrameWidth =
      verticalRow < verticalDescriptionFrameWidths_.size()
          ? verticalDescriptionFrameWidths_[verticalRow]
          : verticalLayout_.GetDescriptionRect(verticalRow).Width();

  DrawVerticalAttributedString(
      [candidate objectAtIndex:kColumnCandidate],
      MacViewUtil::ToNSRect(verticalLayout_.GetValueRect(verticalRow)),
      valueFrameWidth);
  DrawVerticalAttributedString(
      [candidate objectAtIndex:kColumnDescription],
      MacViewUtil::ToNSRect(verticalLayout_.GetDescriptionRect(verticalRow)),
      descriptionFrameWidth);

  const mozc::Rect informationRect =
      verticalLayout_.GetInformationMarkerRect(row);
  if (!informationRect.IsRectEmpty()) {
    [MacViewUtil::ToNSColor(style_.focused_border_color()) set];
    [NSBezierPath fillRect:MacViewUtil::ToNSRect(informationRect)];
  }
}

- (void)drawVerticalFooter {
  const mozc::Rect footerLogicalRect = verticalLayout_.GetFooterRect();
  if (!candidate_window_.has_footer() || footerLogicalRect.IsRectEmpty()) {
    return;
  }
  const mozc::commands::Footer &footer = candidate_window_.footer();
  NSRect footerRect = MacViewUtil::ToNSRect(footerLogicalRect);

  for (int i = 0; i < style_.footer_border_colors_size(); ++i) {
    [MacViewUtil::ToNSColor(style_.footer_border_colors(i)) set];
    const NSPoint fromPoint =
        NSMakePoint(footerRect.origin.x, footerRect.origin.y + 0.5);
    const NSPoint toPoint =
        NSMakePoint(footerRect.origin.x + footerRect.size.width,
                    footerRect.origin.y + 0.5);
    [NSBezierPath strokeLineFromPoint:fromPoint toPoint:toPoint];
    footerRect.origin.y += 1;
  }

  const NSGradient *footerBackground = [[NSGradient alloc]
      initWithStartingColor:MacViewUtil::ToNSColor(style_.footer_top_color())
                endingColor:MacViewUtil::ToNSColor(style_.footer_bottom_color())];
  [footerBackground drawInRect:footerRect angle:90.0];

  if (footer.logo_visible() && logoImage_) {
    const NSPoint logoPoint = footerRect.origin;
    const NSSize logoSize = logoImage_.size;
    const NSRect logoRect = NSMakeRect(
        logoPoint.x, logoPoint.y, logoSize.width, logoSize.height);
    [logoImage_ drawInRect:logoRect
                  fromRect:NSZeroRect
                 operation:NSCompositingOperationSourceOver
                  fraction:1.0
            respectFlipped:YES
                     hints:nil];
    footerRect.origin.x += logoSize.width;
    footerRect.size.width -= logoSize.width;
  }

  if (footer.has_label() &&
      ShouldRenderVerticalFooterText(candidate_window_, footer.label())) {
    const NSAttributedString *footerLabel =
        MacViewUtil::ToNSAttributedString(footer.label(), style_.footer_style());
    footerRect.origin.x += style_.footer_style().left_padding();
    const NSSize labelSize = [footerLabel size];
    NSPoint labelPosition = footerRect.origin;
    labelPosition.y += (footerRect.size.height - labelSize.height) / 2;
    [footerLabel drawAtPoint:labelPosition];
  }

  if (footer.has_sub_label() &&
      ShouldRenderVerticalFooterText(candidate_window_, footer.sub_label())) {
    const NSAttributedString *footerSubLabel =
        MacViewUtil::ToNSAttributedString(footer.sub_label(),
                                          style_.footer_sub_label_style());
    footerRect.origin.x += style_.footer_sub_label_style().left_padding();
    const NSSize subLabelSize = [footerSubLabel size];
    NSPoint subLabelPosition = footerRect.origin;
    subLabelPosition.y +=
        (footerRect.size.height - subLabelSize.height) / 2;
    [footerSubLabel drawAtPoint:subLabelPosition];
  }

  if (footer.index_visible()) {
    const std::string footerIndex =
        absl::StrFormat("%d/%d", candidate_window_.focused_index() + 1,
                        candidate_window_.size());
    const NSAttributedString *footerAttributedIndex =
        MacViewUtil::ToNSAttributedString(footerIndex, style_.footer_style());
    const NSSize footerSize = [footerAttributedIndex size];
    NSPoint footerPosition = footerRect.origin;
    footerPosition.x = footerPosition.x + footerRect.size.width -
                       footerSize.width -
                       style_.footer_style().right_padding();
    [footerAttributedIndex drawAtPoint:footerPosition];
  }
}

#pragma mark drawing aux methods

- (void)drawRow:(int)row {
  if (row == focusedRow_) {
    // Draw focused background
    NSRect focusedRect = MacViewUtil::ToNSRect(tableLayout_.GetRowRect(focusedRow_));
    [MacViewUtil::ToNSColor(style_.focused_background_color()) set];
    [NSBezierPath fillRect:focusedRect];
    [MacViewUtil::ToNSColor(style_.focused_border_color()) set];
    // Fix the border position.  Because a line should be drawn at the
    // middle point of the pixel, origin should be shifted by 0.5 unit
    // and the size should be shrinked by 1.0 unit.
    focusedRect.origin.x += 0.5;
    focusedRect.origin.y += 0.5;
    focusedRect.size.width -= 1.0;
    focusedRect.size.height -= 1.0;
    [NSBezierPath strokeRect:focusedRect];
  } else {
    // Draw normal background
    const mozc::Rect rowRect = tableLayout_.GetRowRect(row);
    auto drawBackground = [&](ColumnType type,
                              const mozc::renderer::RendererStyle::TextStyle &text_style) {
      mozc::Rect cellRect = tableLayout_.GetCellRect(row, type);
      cellRect.origin.y = rowRect.origin.y;
      cellRect.size.height = rowRect.size.height;
      if (cellRect.size.width > 0 && cellRect.size.height > 0 &&
          text_style.has_background_color()) {
        [MacViewUtil::ToNSColor(text_style.background_color()) set];
        [NSBezierPath fillRect:MacViewUtil::ToNSRect(cellRect)];
      }
    };
    drawBackground(kColumnShortcut, style_.shortcut_style());
    drawBackground(kColumnGap1, style_.gap1_style());
    drawBackground(kColumnCandidate, style_.candidate_style());
    drawBackground(kColumnDescription, style_.description_style());
  }

  NSArray<NSAttributedString *> *candidate = [candidateStringsCache_ objectAtIndex:row];

  auto drawText = [&](ColumnType type,
                      const mozc::renderer::RendererStyle::TextStyle& text_style) {
    const NSAttributedString *text = [candidate objectAtIndex:type];
    NSRect cellRect = MacViewUtil::ToNSRect(tableLayout_.GetCellRect(row, type));
    NSPoint position = cellRect.origin;
    position.x += text_style.left_padding();
    position.y += (cellRect.size.height - [text size].height) / 2;
    [text drawAtPoint:position];
  };

  drawText(kColumnShortcut, style_.shortcut_style());
  drawText(kColumnGap1, style_.gap1_style());
  drawText(kColumnCandidate, style_.candidate_style());
  drawText(kColumnDescription, style_.description_style());

  if (candidate_window_.candidate(row).has_information_id()) {
    NSRect rect = MacViewUtil::ToNSRect(tableLayout_.GetRowRect(row));
    [MacViewUtil::ToNSColor(style_.focused_border_color()) set];
    rect.origin.x += rect.size.width - 6.0;
    rect.size.width = 4.0;
    rect.origin.y += 2.0;
    rect.size.height -= 4.0;
    [NSBezierPath fillRect:rect];
  }
}

- (void)drawFooter {
  if (!candidate_window_.has_footer()) {
    return;
  }
  const mozc::commands::Footer &footer = candidate_window_.footer();
  NSRect footerRect = MacViewUtil::ToNSRect(tableLayout_.GetFooterRect());

  // Draw footer border
  for (int i = 0; i < style_.footer_border_colors_size(); ++i) {
    [MacViewUtil::ToNSColor(style_.footer_border_colors(i)) set];
    const NSPoint fromPoint = NSMakePoint(footerRect.origin.x, footerRect.origin.y + 0.5);
    const NSPoint toPoint =
        NSMakePoint(footerRect.origin.x + footerRect.size.width, footerRect.origin.y + 0.5);
    [NSBezierPath strokeLineFromPoint:fromPoint toPoint:toPoint];
    footerRect.origin.y += 1;
  }

  // Draw Footer background and data if necessary
  const NSGradient *footerBackground = [[NSGradient alloc]
      initWithStartingColor:MacViewUtil::ToNSColor(style_.footer_top_color())
                endingColor:MacViewUtil::ToNSColor(style_.footer_bottom_color())];
  [footerBackground drawInRect:footerRect angle:90.0];

  // Draw logo
  if (footer.logo_visible() && logoImage_) {
    const NSPoint logoPoint = footerRect.origin;
    const NSSize logoSize = logoImage_.size;
    const NSRect logoRect = NSMakeRect(logoPoint.x, logoPoint.y, logoSize.width, logoSize.height);
    [logoImage_ drawInRect:logoRect
                    fromRect:NSZeroRect   // Draw the entire image
                  operation:NSCompositingOperationSourceOver
                    fraction:1.0  // Opacity
              respectFlipped:YES
                      hints:nil];
    footerRect.origin.x += logoSize.width;
    footerRect.size.width -= logoSize.width;
  }

  // Draw label
  if (footer.has_label()) {
    const NSAttributedString *footerLabel =
        MacViewUtil::ToNSAttributedString(footer.label(), style_.footer_style());
    footerRect.origin.x += style_.footer_style().left_padding();
    const NSSize labelSize = [footerLabel size];
    NSPoint labelPosition = footerRect.origin;
    labelPosition.y += (footerRect.size.height - labelSize.height) / 2;
    [footerLabel drawAtPoint:labelPosition];
  }

  // Draw sub_label
  if (footer.has_sub_label()) {
    const NSAttributedString *footerSubLabel =
        MacViewUtil::ToNSAttributedString(footer.sub_label(), style_.footer_sub_label_style());
    footerRect.origin.x += style_.footer_sub_label_style().left_padding();
    const NSSize subLabelSize = [footerSubLabel size];
    NSPoint subLabelPosition = footerRect.origin;
    subLabelPosition.y += (footerRect.size.height - subLabelSize.height) / 2;
    [footerSubLabel drawAtPoint:subLabelPosition];
  }

  // Draw footer index (e.g. "10/120")
  if (footer.index_visible()) {
    const std::string footerIndex =
        absl::StrFormat("%d/%d",
                        candidate_window_.focused_index() + 1,  // +1 to 1-origin from 0-origin.
                        candidate_window_.size());
    const NSAttributedString *footerAttributedIndex =
        MacViewUtil::ToNSAttributedString(footerIndex, style_.footer_style());
    const NSSize footerSize = [footerAttributedIndex size];
    NSPoint footerPosition = footerRect.origin;
    footerPosition.x = footerPosition.x + footerRect.size.width - footerSize.width -
                        style_.footer_style().right_padding();
    [footerAttributedIndex drawAtPoint:footerPosition];
  }
}

- (void)drawVScrollBar {
  const mozc::Rect vscrollRect = tableLayout_.GetVScrollBarRect();
  if (vscrollRect.IsRectEmpty() || candidate_window_.candidate_size() <= 0) {
    return;
  }

  const int beginIndex = candidate_window_.candidate(0).index();
  const int candidatesTotal = candidate_window_.size();
  const int endIndex = candidate_window_.candidate(candidate_window_.candidate_size() - 1).index();

  [MacViewUtil::ToNSColor(style_.scrollbar_background_color()) set];
  [NSBezierPath fillRect:MacViewUtil::ToNSRect(vscrollRect)];

  const mozc::Rect indicatorRect =
      tableLayout_.GetVScrollIndicatorRect(beginIndex, endIndex, candidatesTotal);
  [MacViewUtil::ToNSColor(style_.scrollbar_indicator_color()) set];
  [NSBezierPath fillRect:MacViewUtil::ToNSRect(indicatorRect)];
}

#pragma mark event handling callbacks

- (void)mouseDown:(NSEvent *)event {
  const mozc::Point localPos = MacViewUtil::ToPoint([self convertPoint:[event locationInWindow]
                                                              fromView:nil]);
  int clickedRow = -1;
  if (writingDirection_ == WritingDirection::kVertical) {
    for (size_t i = 0; i < verticalLayout_.candidate_count(); ++i) {
      if (verticalLayout_.GetCandidateRect(i).PtrInRect(localPos)) {
        clickedRow = static_cast<int>(i);
        break;
      }
    }
  } else {
    for (int i = 0; i < tableLayout_.number_of_rows(); ++i) {
      const mozc::Rect rowRect = tableLayout_.GetRowRect(i);
      if (rowRect.PtrInRect(localPos)) {
        clickedRow = i;
        break;
      }
    }
  }

  if (clickedRow >= 0 && clickedRow != focusedRow_) {
    focusedRow_ = clickedRow;
    [self setNeedsDisplay:YES];
  }
}

- (void)mouseUp:(NSEvent *)event {
  const mozc::Point localPos = MacViewUtil::ToPoint([self convertPoint:[event locationInWindow]
                                                              fromView:nil]);
  if (command_sender_ == nullptr) {
    return;
  }
  if (writingDirection_ == WritingDirection::kVertical) {
    if (candidate_window_.candidate_size() <
        static_cast<int>(verticalLayout_.candidate_count())) {
      return;
    }
    for (size_t i = 0; i < verticalLayout_.candidate_count(); ++i) {
      if (verticalLayout_.GetCandidateRect(i).PtrInRect(localPos)) {
        SessionCommand command;
        command.set_type(SessionCommand::SELECT_CANDIDATE);
        command.set_id(
            candidate_window_.candidate(static_cast<int>(i)).id());
        Output dummy_output;
        command_sender_->SendCommand(command, &dummy_output);
        break;
      }
    }
    return;
  }

  if (candidate_window_.candidate_size() < tableLayout_.number_of_rows()) {
    return;
  }
  for (int i = 0; i < tableLayout_.number_of_rows(); ++i) {
    const mozc::Rect rowRect = tableLayout_.GetRowRect(i);
    if (rowRect.PtrInRect(localPos)) {
      SessionCommand command;
      command.set_type(SessionCommand::SELECT_CANDIDATE);
      command.set_id(candidate_window_.candidate(i).id());
      Output dummy_output;
      command_sender_->SendCommand(command, &dummy_output);
      break;
    }
  }
}

- (void)mouseDragged:(NSEvent *)event {
  [self mouseDown:event];
}
@end
