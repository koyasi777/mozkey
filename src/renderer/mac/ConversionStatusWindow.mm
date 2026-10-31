#include "renderer/mac/ConversionStatusWindow.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>

#include "config/config_handler.h"
#include "protocol/renderer_command.pb.h"

using mozc::renderer::ConversionDisplayStatus;
using mozc::renderer::ConversionStatusColor;
using mozc::renderer::ConversionStatusVerticalFullLabelFontReduction;
namespace config = mozc::config;
namespace {
NSColor* Color(uint32_t rgb) {
  return [NSColor colorWithCalibratedRed:((rgb >> 16) & 255) / 255.0
                                  green:((rgb >> 8) & 255) / 255.0
                                   blue:(rgb & 255) / 255.0 alpha:1.0];
}
NSFont* StatusFont(
    const config::Config::ConversionStatusIndicatorStyle& style,
    bool vertical) {
  // Cocoa sizes are in logical points; do not modify the stored font size.
  const CGFloat size = std::max<CGFloat>(
      8, std::clamp<CGFloat>(style.font_size(), 8, 24) -
             ConversionStatusVerticalFullLabelFontReduction(style, vertical));
  if (!style.font_family().empty()) {
    NSString* family = [NSString stringWithUTF8String:style.font_family().c_str()];
    if (family != nil) {
      NSFont* selected = [[NSFontManager sharedFontManager]
          fontWithFamily:family traits:0 weight:5 size:size];
      if (selected != nil) return selected;
    }
  }
  return [NSFont systemFontOfSize:size weight:NSFontWeightMedium];
}

NSString* Label(ConversionDisplayStatus status,
                config::Config::ConversionStatusIndicatorLabel mode) {
  if (mode == config::Config::CONVERSION_STATUS_COLOR_ONLY) return @"";
  if (mode == config::Config::CONVERSION_STATUS_SYMBOL) {
    switch (status) {
      case ConversionDisplayStatus::kMozc: return @"M";
      case ConversionDisplayStatus::kZenzPending: return @"…";
      case ConversionDisplayStatus::kZenz: return @"Z";
      case ConversionDisplayStatus::kReading: return @"あ";
      default: return @"";
    }
  }
  switch (status) {
    case ConversionDisplayStatus::kMozc: return @"Mozc";
    case ConversionDisplayStatus::kZenzPending: return @"Zenz…";
    case ConversionDisplayStatus::kZenz: return @"Zenz";
    case ConversionDisplayStatus::kReading: return @"入力中";
    default: return @"";
  }
}
}  // namespace

@interface MozkeyStatusView : NSView
- (void)configure:(NSString*)title
       background:(NSColor*)background
       foreground:(NSColor*)foreground
           border:(NSColor*)border
           radius:(CGFloat)radius
      borderWidth:(CGFloat)borderWidth
        font:(NSFont*)font
      vertical:(BOOL)vertical;
@end
@implementation MozkeyStatusView {
  NSString* title_;
  NSColor* background_;
  NSColor* foreground_;
  NSColor* border_;
  CGFloat radius_;
  CGFloat borderWidth_;
  NSFont* font_;
  BOOL vertical_;
}
- (void)configure:(NSString*)title background:(NSColor*)background
       foreground:(NSColor*)foreground border:(NSColor*)border
           radius:(CGFloat)radius borderWidth:(CGFloat)borderWidth
         font:(NSFont*)font vertical:(BOOL)vertical {
  title_ = [title copy];
  background_ = background;
  foreground_ = foreground;
  border_ = border;
  radius_ = radius;
  borderWidth_ = borderWidth;
  font_ = font;
  vertical_ = vertical;
  [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  NSRect r = [self bounds];
  NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:r
                                              xRadius:radius_ yRadius:radius_];
  NSGradient* gradient = [[NSGradient alloc]
      initWithStartingColor:[background_ blendedColorWithFraction:0.11
                                                       ofColor:NSColor.whiteColor]
                endingColor:background_];
  if (radius_ >= r.size.height / 2 &&
      r.size.width <= r.size.height + 1) {
    [background_ setFill];
    [path fill];
  } else {
    [gradient drawInBezierPath:path angle:270.0];
  }
  if (borderWidth_ > 0) {
    [border_ setStroke];
    [path setLineWidth:borderWidth_];
    [path stroke];
  }
  if ([title_ length] > 0) {
    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    [paragraph setAlignment:NSTextAlignmentCenter];
    NSDictionary* attributes = @{
      NSFontAttributeName: font_,
      NSForegroundColorAttributeName: foreground_,
      NSParagraphStyleAttributeName: paragraph,
    };
    NSSize textSize = [title_ sizeWithAttributes:attributes];
    const BOOL usesDot = vertical_
        ? (r.size.height > r.size.width + 4 && [title_ length] > 1)
        : (r.size.width > r.size.height + 4 && [title_ length] > 1);
    if (vertical_ && r.size.height > r.size.width + 4) {
      const CGFloat step = std::ceil([font_ ascender] - [font_ descender] + 3);
      const CGFloat dotGap = usesDot ? 18 : 0;
      const CGFloat textHeight = [title_ length] * step;
      const CGFloat top = std::max<CGFloat>(0,
          (r.size.height - textHeight - dotGap) / 2);
      if (usesDot) {
        NSRect dot = NSMakeRect((r.size.width - 5) / 2,
            r.size.height - top - 12, 5, 5);
        [[foreground_ colorWithAlphaComponent:0.85] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
      }
      for (NSUInteger i = 0; i < [title_ length]; ++i) {
        NSString* glyph = [title_ substringWithRange:NSMakeRange(i, 1)];
        NSSize glyphSize = [glyph sizeWithAttributes:attributes];
        const CGFloat y = r.size.height - top - dotGap - (i + 1) * step;
        NSRect glyphRect = NSMakeRect(
            (r.size.width - glyphSize.width) / 2,
            y + (step - glyphSize.height) / 2,
            glyphSize.width, glyphSize.height);
        [glyph drawInRect:glyphRect withAttributes:attributes];
      }
    } else {
      NSRect labelRect = NSMakeRect(0, (r.size.height - textSize.height) / 2,
                                   r.size.width, textSize.height);
      if (usesDot) {
        NSRect dot = NSMakeRect(9, (r.size.height - 5) / 2, 5, 5);
        [[foreground_ colorWithAlphaComponent:0.85] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
        labelRect.origin.x += 17;
        labelRect.size.width -= 20;
      }
      [title_ drawInRect:labelRect withAttributes:attributes];
    }
  }
}
@end

namespace mozc::renderer::mac {
void ConversionStatusWindow::ResetView() {
  view_ = [[MozkeyStatusView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
}
bool ConversionStatusWindow::Update(const commands::RendererCommand& command,
                                   bool vertical) {
  const auto settings = config::ConfigHandler::GetSharedConfig();
  if (!command.visible() || !command.has_output() ||
      !settings->show_conversion_status_indicator()) return false;
  ConversionDisplayStatus state = ResolveConversionDisplayStatus(command.output());
  if (state == ConversionDisplayStatus::kHidden) return false;
  const auto& style = settings->conversion_status_indicator_style();
  if (!window_) InitWindow();
  if (!window_) return false;
  if (last_status_ == state && last_vertical_ == vertical &&
      last_style_.SerializeAsString() == style.SerializeAsString()) {
    return true;
  }
  [window_ setOpaque:NO];
  [window_ setBackgroundColor:NSColor.clearColor];
  const CGFloat thickness = std::clamp<CGFloat>(style.height(), 12, 100);
  CGFloat h = thickness;
  CGFloat w = style.shape() == config::Config::CONVERSION_STATUS_CIRCLE
      ? thickness : std::clamp<CGFloat>(style.width(), 12, 240);
  const auto display_mode =
      style.shape() == config::Config::CONVERSION_STATUS_CIRCLE &&
      style.label() == config::Config::CONVERSION_STATUS_FULL_LABEL
          ? config::Config::CONVERSION_STATUS_SYMBOL : style.label();
  NSString* displayed_title = Label(state, display_mode);
  NSFont* display_font = StatusFont(style, vertical);
  if (style.shape() != config::Config::CONVERSION_STATUS_CIRCLE &&
      [displayed_title length] > 0) {
    NSDictionary* attrs = @{
      NSFontAttributeName: display_font,
    };
    const CGFloat padding = display_mode == config::Config::CONVERSION_STATUS_FULL_LABEL
                                ? 28 : 16;
    w = std::min<CGFloat>(360, std::max(w,
        [displayed_title sizeWithAttributes:attrs].width + padding));
  }
  if (vertical && style.shape() != config::Config::CONVERSION_STATUS_CIRCLE) {
    h = std::clamp<CGFloat>(style.width(), 12, 240);
    w = thickness;
    if ([displayed_title length] > 0) {
      const CGFloat step = std::ceil([display_font ascender] -
                                     [display_font descender] + 3);
      const CGFloat padding = display_mode == config::Config::CONVERSION_STATUS_FULL_LABEL
                                  ? 30 : 12;
      h = std::min<CGFloat>(360, std::max(h,
          [displayed_title length] * step + padding));
    }
  }
  CGFloat radius = style.shape() == config::Config::CONVERSION_STATUS_CIRCLE ||
                   style.shape() == config::Config::CONVERSION_STATUS_PILL
      ? std::min(w, h) / 2
      : std::clamp<CGFloat>(style.corner_radius(), 0, std::min(w, h) / 2);
  [(MozkeyStatusView*)view_ configure:displayed_title
        background:Color(ConversionStatusColor(state, style))
        foreground:Color(style.text_color()) border:Color(style.border_color())
        radius:radius borderWidth:std::clamp<CGFloat>(style.border_width(), 0, 5)
        font:display_font vertical:vertical];
  ResizeWindow(static_cast<int32_t>(std::ceil(w)),
               static_cast<int32_t>(std::ceil(h)));
  RendererStyleHandler::WindowShadowStyle shadow;
  shadow.size = std::clamp(style.shadow_size(), 0u, 24u);
  shadow.opacity_percent = std::clamp(style.shadow_opacity_percent(), 0u, 100u);
  shadow.angle_degrees = style.shadow_angle_degrees() % 360u;
  shadow.distance = std::clamp(style.shadow_distance(), 0u, 24u);
  SetWindowEffects(std::clamp(style.opacity_percent(), 20u, 100u), radius,
                   shadow);
  last_status_ = state;
  last_style_.CopyFrom(style);
  last_vertical_ = vertical;
  return true;
}
}  // namespace mozc::renderer::mac
