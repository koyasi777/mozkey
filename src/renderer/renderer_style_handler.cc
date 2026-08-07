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

#include "renderer/renderer_style_handler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "absl/base/no_destructor.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "base/protobuf/text_format.h"
#include "base/protobuf_util.h"
#include "base/text_normalizer.h"
#include "protocol/renderer_style.pb.h"

namespace mozc {
namespace renderer {
namespace {

// absl::string_view kStyleTextProto is defined in renderer_style.inc.
#include "renderer/renderer_style.inc"

void SetColor(RendererStyle::RGBAColor* color, int r, int g, int b) {
  color->set_r(r);
  color->set_g(g);
  color->set_b(b);
}

void SetRgbColor(RendererStyle::RGBAColor* color, uint32_t rgb) {
  SetColor(color, static_cast<int>((rgb >> 16) & 0xff),
           static_cast<int>((rgb >> 8) & 0xff),
           static_cast<int>(rgb & 0xff));
}

uint32_t ToRgb(const RendererStyle::RGBAColor& color, uint32_t fallback) {
  if (!color.IsInitialized()) {
    return fallback;
  }
  return ((static_cast<uint32_t>(color.r()) & 0xff) << 16) |
         ((static_cast<uint32_t>(color.g()) & 0xff) << 8) |
         (static_cast<uint32_t>(color.b()) & 0xff);
}

int ScaleIntegerMetric(int value, uint32_t percent) {
  if (value <= 0) {
    return 0;
  }
  return std::max(1, static_cast<int>(std::lround(
                         static_cast<double>(value) * percent / 100.0)));
}

double ScaleFontSize(double value, uint32_t percent) {
  if (value <= 0) {
    return value;
  }
  return static_cast<double>(value) * percent / 100.0;
}

void ScaleTextStyle(RendererStyle::TextStyle* style, uint32_t percent) {
  if (style == nullptr) {
    return;
  }
  if (style->has_font_size()) {
    style->set_font_size(ScaleFontSize(style->font_size(), percent));
  }
  if (style->has_left_padding()) {
    style->set_left_padding(
        ScaleIntegerMetric(style->left_padding(), percent));
  }
  if (style->has_right_padding()) {
    style->set_right_padding(
        ScaleIntegerMetric(style->right_padding(), percent));
  }
}

RendererStyleHandler::RubyWindowStyle RubyStyleFromRendererStyle(
    const RendererStyle& style) {
  RendererStyleHandler::RubyWindowStyle ruby_style;
  if (style.has_border_color()) {
    ruby_style.border_color =
        ToRgb(style.border_color(), ruby_style.border_color);
  }
  if (style.candidate_style().has_background_color()) {
    ruby_style.background_color =
        ToRgb(style.candidate_style().background_color(),
              ruby_style.background_color);
  }
  if (style.candidate_style().has_foreground_color()) {
    ruby_style.text_color = ToRgb(style.candidate_style().foreground_color(),
                                  ruby_style.text_color);
  }
  if (style.candidate_style().has_font_weight()) {
    ruby_style.font_weight = static_cast<uint32_t>(std::clamp(
        style.candidate_style().font_weight(), 100, 900));
  }
  return ruby_style;
}

void ApplyLightCandidateWindowTheme(RendererStyle* style) {
  SetColor(style->mutable_border_color(), 0x96, 0x96, 0x96);

  SetColor(style->mutable_shortcut_style()->mutable_foreground_color(),
           0x77, 0x77, 0x77);
  SetColor(style->mutable_shortcut_style()->mutable_background_color(),
           0xf3, 0xf4, 0xff);

  SetColor(style->mutable_gap1_style()->mutable_background_color(),
           0xff, 0xff, 0xff);

  SetColor(style->mutable_candidate_style()->mutable_foreground_color(),
           0x00, 0x00, 0x00);
  SetColor(style->mutable_candidate_style()->mutable_background_color(),
           0xff, 0xff, 0xff);

  SetColor(style->mutable_description_style()->mutable_foreground_color(),
           0x88, 0x88, 0x88);
  SetColor(style->mutable_description_style()->mutable_background_color(),
           0xff, 0xff, 0xff);

  SetColor(style->mutable_footer_style()->mutable_foreground_color(),
           0x4c, 0x4c, 0x4c);
  SetColor(style->mutable_footer_sub_label_style()->mutable_foreground_color(),
           0xa7, 0xa7, 0xa7);

  if (style->footer_border_colors_size() == 0) {
    style->add_footer_border_colors();
  }
  SetColor(style->mutable_footer_border_colors(0), 0x60, 0x60, 0x60);

  SetColor(style->mutable_footer_top_color(), 0xff, 0xff, 0xff);
  SetColor(style->mutable_footer_bottom_color(), 0xee, 0xee, 0xee);

  SetColor(style->mutable_focused_background_color(), 0xd1, 0xea, 0xff);
  SetColor(style->mutable_focused_border_color(), 0x7f, 0xac, 0xdd);

  SetColor(style->mutable_scrollbar_background_color(), 0xe0, 0xe0, 0xe0);
  SetColor(style->mutable_scrollbar_indicator_color(), 0x75, 0x90, 0xb8);

  RendererStyle::InfolistStyle* infostyle = style->mutable_infolist_style();
  SetColor(infostyle->mutable_caption_style()->mutable_foreground_color(),
           0x00, 0x00, 0x00);
  SetColor(infostyle->mutable_title_style()->mutable_foreground_color(),
           0x00, 0x00, 0x00);
  SetColor(infostyle->mutable_title_style()->mutable_background_color(),
           0xff, 0xff, 0xff);
  SetColor(infostyle->mutable_description_style()->mutable_foreground_color(),
           0x33, 0x33, 0x33);
  SetColor(infostyle->mutable_description_style()->mutable_background_color(),
           0xff, 0xff, 0xff);
  SetColor(infostyle->mutable_border_color(), 0x96, 0x96, 0x96);
  SetColor(infostyle->mutable_caption_background_color(), 0xec, 0xf0, 0xfa);
  SetColor(infostyle->mutable_focused_background_color(), 0xd1, 0xea, 0xff);
  SetColor(infostyle->mutable_focused_border_color(), 0x7f, 0xac, 0xdd);
}

void ApplyDarkCandidateWindowTheme(RendererStyle* style) {
  SetColor(style->mutable_border_color(), 0x32, 0x38, 0x40);

  SetColor(style->mutable_shortcut_style()->mutable_foreground_color(),
           0x96, 0xa0, 0xaa);
  SetColor(style->mutable_shortcut_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);

  SetColor(style->mutable_gap1_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);

  SetColor(style->mutable_candidate_style()->mutable_foreground_color(),
           0xe6, 0xed, 0xf3);
  SetColor(style->mutable_candidate_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);

  SetColor(style->mutable_description_style()->mutable_foreground_color(),
           0x8b, 0x94, 0x9e);
  SetColor(style->mutable_description_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);

  SetColor(style->mutable_footer_style()->mutable_foreground_color(),
           0xb7, 0xc0, 0xc9);
  SetColor(style->mutable_footer_sub_label_style()->mutable_foreground_color(),
           0x77, 0x80, 0x8a);

  if (style->footer_border_colors_size() == 0) {
    style->add_footer_border_colors();
  }
  SetColor(style->mutable_footer_border_colors(0), 0x2a, 0x30, 0x37);

  SetColor(style->mutable_footer_top_color(), 0x16, 0x1a, 0x1f);
  SetColor(style->mutable_footer_bottom_color(), 0x16, 0x1a, 0x1f);

  SetColor(style->mutable_focused_background_color(), 0x24, 0x2b, 0x34);
  SetColor(style->mutable_focused_border_color(), 0x3f, 0x4b, 0x59);

  SetColor(style->mutable_scrollbar_background_color(), 0x1d, 0x22, 0x28);
  SetColor(style->mutable_scrollbar_indicator_color(), 0x4b, 0x57, 0x66);

  RendererStyle::InfolistStyle* infostyle = style->mutable_infolist_style();
  SetColor(infostyle->mutable_caption_style()->mutable_foreground_color(),
           0xe6, 0xed, 0xf3);
  SetColor(infostyle->mutable_title_style()->mutable_foreground_color(),
           0xe6, 0xed, 0xf3);
  SetColor(infostyle->mutable_title_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);
  SetColor(infostyle->mutable_description_style()->mutable_foreground_color(),
           0xc9, 0xd1, 0xd9);
  SetColor(infostyle->mutable_description_style()->mutable_background_color(),
           0x18, 0x1b, 0x20);
  SetColor(infostyle->mutable_border_color(), 0x32, 0x38, 0x40);
  SetColor(infostyle->mutable_caption_background_color(), 0x1a, 0x1e, 0x24);
  SetColor(infostyle->mutable_focused_background_color(), 0x24, 0x2b, 0x34);
  SetColor(infostyle->mutable_focused_border_color(), 0x3f, 0x4b, 0x59);
}

void SanitizeRendererStyleStrings(RendererStyle* style) {
  if (style == nullptr) {
    return;
  }
  protobuf_util::SanitizeMessageStrings(*style, [](absl::string_view src) {
    // Limit the length of the string to 100 bytes and remove ill-formed
    // UTF-8 sequences and ASCII control characters.
    return TextNormalizer::SanitizeText(src, 100);
  });
}

class RendererStyleHandlerImpl {
 public:
  RendererStyleHandlerImpl();
  ~RendererStyleHandlerImpl() = default;

  bool GetRendererStyle(RendererStyle* style);
  bool GetRendererStyleForWindowType(
      RendererStyleHandler::RendererStyleType type, RendererStyle* style);
  bool SetRendererStyle(const RendererStyle& style);
  bool SetRendererWindowStyles(
      const RendererStyle& candidate_style, const RendererStyle& suggestion_style,
      const RendererStyleHandler::RubyWindowStyle& ruby_style,
      uint32_t candidate_corner_radius, uint32_t suggestion_corner_radius,
      const RendererStyleHandler::CandidateWindowEffectStyle&
          candidate_effect_style,
      const RendererStyleHandler::CandidateWindowEffectStyle&
          suggestion_effect_style);
  void GetDefaultRendererStyle(RendererStyle* style);
  uint32_t GetCandidateWindowCornerRadius(
      RendererStyleHandler::RendererStyleType type) const;
  RendererStyleHandler::CandidateWindowEffectStyle
  GetCandidateWindowEffectStyle(
      RendererStyleHandler::RendererStyleType type) const;
  RendererStyleHandler::RubyWindowStyle GetRubyWindowStyle() const;

 private:
  RendererStyle candidate_style_;
  RendererStyle suggestion_style_;
  RendererStyleHandler::RubyWindowStyle ruby_style_;
  uint32_t candidate_corner_radius_ = 6;
  uint32_t suggestion_corner_radius_ = 6;
  RendererStyleHandler::CandidateWindowEffectStyle candidate_effect_style_;
  RendererStyleHandler::CandidateWindowEffectStyle suggestion_effect_style_;
};

RendererStyleHandlerImpl* GetRendererStyleHandlerImpl() {
  static absl::NoDestructor<RendererStyleHandlerImpl> handler;
  return handler.get();
}

RendererStyleHandlerImpl::RendererStyleHandlerImpl() {
  GetDefaultRendererStyle(&candidate_style_);
  suggestion_style_ = candidate_style_;
  ruby_style_ = RubyStyleFromRendererStyle(candidate_style_);
}

bool RendererStyleHandlerImpl::GetRendererStyle(RendererStyle* style) {
  if (style == nullptr) {
    return false;
  }
  *style = candidate_style_;
  return true;
}

bool RendererStyleHandlerImpl::GetRendererStyleForWindowType(
    RendererStyleHandler::RendererStyleType type, RendererStyle* style) {
  if (style == nullptr) {
    return false;
  }
  switch (type) {
    case RendererStyleHandler::RendererStyleType::kCandidate:
      *style = candidate_style_;
      return true;
    case RendererStyleHandler::RendererStyleType::kSuggestion:
      *style = suggestion_style_;
      return true;
  }
  return false;
}

bool RendererStyleHandlerImpl::SetRendererStyle(const RendererStyle& style) {
  candidate_style_ = style;
  suggestion_style_ = style;
  ruby_style_ = RubyStyleFromRendererStyle(style);
  candidate_corner_radius_ = 6;
  suggestion_corner_radius_ = 6;
  candidate_effect_style_ = RendererStyleHandler::CandidateWindowEffectStyle();
  suggestion_effect_style_ = RendererStyleHandler::CandidateWindowEffectStyle();
  return true;
}

bool RendererStyleHandlerImpl::SetRendererWindowStyles(
    const RendererStyle& candidate_style, const RendererStyle& suggestion_style,
    const RendererStyleHandler::RubyWindowStyle& ruby_style,
    uint32_t candidate_corner_radius, uint32_t suggestion_corner_radius,
    const RendererStyleHandler::CandidateWindowEffectStyle&
        candidate_effect_style,
    const RendererStyleHandler::CandidateWindowEffectStyle&
        suggestion_effect_style) {
  candidate_style_ = candidate_style;
  suggestion_style_ = suggestion_style;
  ruby_style_ = ruby_style;
  candidate_corner_radius_ = candidate_corner_radius;
  suggestion_corner_radius_ = suggestion_corner_radius;
  candidate_effect_style_ = candidate_effect_style;
  suggestion_effect_style_ = suggestion_effect_style;
  return true;
}

void RendererStyleHandlerImpl::GetDefaultRendererStyle(RendererStyle* style) {
  CHECK(style != nullptr);
  style->Clear();
  CHECK(mozc::protobuf::TextFormat::ParseFromString(kStyleTextProto, style));
  ApplyLightCandidateWindowTheme(style);
  SanitizeRendererStyleStrings(style);
}

uint32_t RendererStyleHandlerImpl::GetCandidateWindowCornerRadius(
    RendererStyleHandler::RendererStyleType type) const {
  switch (type) {
    case RendererStyleHandler::RendererStyleType::kCandidate:
      return candidate_corner_radius_;
    case RendererStyleHandler::RendererStyleType::kSuggestion:
      return suggestion_corner_radius_;
  }
  return candidate_corner_radius_;
}

RendererStyleHandler::CandidateWindowEffectStyle
RendererStyleHandlerImpl::GetCandidateWindowEffectStyle(
    RendererStyleHandler::RendererStyleType type) const {
  switch (type) {
    case RendererStyleHandler::RendererStyleType::kCandidate:
      return candidate_effect_style_;
    case RendererStyleHandler::RendererStyleType::kSuggestion:
      return suggestion_effect_style_;
  }
  return candidate_effect_style_;
}

RendererStyleHandler::RubyWindowStyle
RendererStyleHandlerImpl::GetRubyWindowStyle() const {
  return ruby_style_;
}

}  // namespace

bool RendererStyleHandler::GetRendererStyle(RendererStyle* style) {
  return GetRendererStyleHandlerImpl()->GetRendererStyle(style);
}

bool RendererStyleHandler::SetRendererStyle(const RendererStyle& style) {
  return GetRendererStyleHandlerImpl()->SetRendererStyle(style);
}

bool RendererStyleHandler::GetRendererStyleForWindowType(
    RendererStyleType type, RendererStyle* style) {
  return GetRendererStyleHandlerImpl()->GetRendererStyleForWindowType(type,
                                                                      style);
}

bool RendererStyleHandler::SetRendererWindowStyles(
    const RendererStyle& candidate_style, const RendererStyle& suggestion_style,
    const RubyWindowStyle& ruby_style, uint32_t candidate_corner_radius,
    uint32_t suggestion_corner_radius,
    const CandidateWindowEffectStyle& candidate_effect_style,
    const CandidateWindowEffectStyle& suggestion_effect_style) {
  return GetRendererStyleHandlerImpl()->SetRendererWindowStyles(
      candidate_style, suggestion_style, ruby_style, candidate_corner_radius,
      suggestion_corner_radius, candidate_effect_style, suggestion_effect_style);
}

uint32_t RendererStyleHandler::GetCandidateWindowCornerRadius(
    RendererStyleType type) {
  return GetRendererStyleHandlerImpl()->GetCandidateWindowCornerRadius(type);
}

RendererStyleHandler::CandidateWindowEffectStyle
RendererStyleHandler::GetCandidateWindowEffectStyle(RendererStyleType type) {
  return GetRendererStyleHandlerImpl()->GetCandidateWindowEffectStyle(type);
}

RendererStyleHandler::RubyWindowStyle RendererStyleHandler::GetRubyWindowStyle() {
  return GetRendererStyleHandlerImpl()->GetRubyWindowStyle();
}

void RendererStyleHandler::GetDefaultRendererStyle(RendererStyle* style) {
  return GetRendererStyleHandlerImpl()->GetDefaultRendererStyle(style);
}

void RendererStyleHandler::ApplyCandidateWindowTheme(bool use_dark_mode,
                                                     RendererStyle* style) {
  if (style == nullptr) {
    return;
  }
  if (use_dark_mode) {
    ApplyDarkCandidateWindowTheme(style);
  } else {
    ApplyLightCandidateWindowTheme(style);
  }
}

void RendererStyleHandler::ApplyCandidateWindowCustomColors(
    uint32_t background_color, uint32_t text_color,
    uint32_t selected_background_color, uint32_t selected_border_color,
    uint32_t border_color, uint32_t shortcut_text_color,
    uint32_t shortcut_background_color, uint32_t description_text_color,
    uint32_t footer_text_color, uint32_t footer_background_color,
    uint32_t footer_border_color, uint32_t scrollbar_background_color,
    uint32_t scrollbar_indicator_color, RendererStyle* style) {
  if (style == nullptr) {
    return;
  }

  SetRgbColor(style->mutable_border_color(), border_color);
  SetRgbColor(style->mutable_shortcut_style()->mutable_foreground_color(),
              shortcut_text_color);
  SetRgbColor(style->mutable_shortcut_style()->mutable_background_color(),
              shortcut_background_color);
  SetRgbColor(style->mutable_gap1_style()->mutable_background_color(),
              background_color);
  SetRgbColor(style->mutable_candidate_style()->mutable_foreground_color(),
              text_color);
  SetRgbColor(style->mutable_candidate_style()->mutable_background_color(),
              background_color);
  SetRgbColor(style->mutable_description_style()->mutable_foreground_color(),
              description_text_color);
  SetRgbColor(style->mutable_description_style()->mutable_background_color(),
              background_color);
  SetRgbColor(style->mutable_footer_style()->mutable_foreground_color(),
              footer_text_color);
  SetRgbColor(style->mutable_footer_sub_label_style()->mutable_foreground_color(),
              footer_text_color);
  if (style->footer_border_colors_size() == 0) {
    style->add_footer_border_colors();
  }
  SetRgbColor(style->mutable_footer_border_colors(0), footer_border_color);
  SetRgbColor(style->mutable_footer_top_color(), footer_background_color);
  SetRgbColor(style->mutable_footer_bottom_color(), footer_background_color);
  SetRgbColor(style->mutable_focused_background_color(),
              selected_background_color);
  SetRgbColor(style->mutable_focused_border_color(), selected_border_color);
  SetRgbColor(style->mutable_scrollbar_background_color(),
              scrollbar_background_color);
  SetRgbColor(style->mutable_scrollbar_indicator_color(),
              scrollbar_indicator_color);

  RendererStyle::InfolistStyle* infostyle = style->mutable_infolist_style();
  SetRgbColor(infostyle->mutable_caption_style()->mutable_foreground_color(),
              text_color);
  SetRgbColor(infostyle->mutable_title_style()->mutable_foreground_color(),
              text_color);
  SetRgbColor(infostyle->mutable_title_style()->mutable_background_color(),
              background_color);
  SetRgbColor(infostyle->mutable_description_style()->mutable_foreground_color(),
              description_text_color);
  SetRgbColor(infostyle->mutable_description_style()->mutable_background_color(),
              background_color);
  SetRgbColor(infostyle->mutable_border_color(), border_color);
  SetRgbColor(infostyle->mutable_caption_background_color(),
              shortcut_background_color);
  SetRgbColor(infostyle->mutable_focused_background_color(),
              selected_background_color);
  SetRgbColor(infostyle->mutable_focused_border_color(),
              selected_border_color);
}

void RendererStyleHandler::ApplyCandidateWindowSize(uint32_t size_percent,
                                                    RendererStyle* style) {
  if (style == nullptr || size_percent == 100) {
    return;
  }

  ScaleTextStyle(style->mutable_shortcut_style(), size_percent);
  ScaleTextStyle(style->mutable_gap1_style(), size_percent);
  ScaleTextStyle(style->mutable_candidate_style(), size_percent);
  ScaleTextStyle(style->mutable_description_style(), size_percent);
  ScaleTextStyle(style->mutable_footer_style(), size_percent);
  ScaleTextStyle(style->mutable_footer_sub_label_style(), size_percent);

  style->set_scrollbar_width(
      ScaleIntegerMetric(style->scrollbar_width(), size_percent));
  style->set_row_rect_padding(
      ScaleIntegerMetric(style->row_rect_padding(), size_percent));

  RendererStyle::InfolistStyle* infostyle = style->mutable_infolist_style();
  infostyle->set_caption_height(
      ScaleIntegerMetric(infostyle->caption_height(), size_percent));
  infostyle->set_caption_padding(
      ScaleIntegerMetric(infostyle->caption_padding(), size_percent));
  infostyle->set_row_rect_padding(
      ScaleIntegerMetric(infostyle->row_rect_padding(), size_percent));
  infostyle->set_window_width(
      ScaleIntegerMetric(infostyle->window_width(), size_percent));
  ScaleTextStyle(infostyle->mutable_caption_style(), size_percent);
  ScaleTextStyle(infostyle->mutable_title_style(), size_percent);
  ScaleTextStyle(infostyle->mutable_description_style(), size_percent);
}

void RendererStyleHandler::ApplyCandidateRubyFont(
    const std::string& font_name, RendererStyle* style) {
  if (style == nullptr || font_name.empty()) {
    return;
  }

  style->mutable_candidate_style()->set_font_name(font_name);
  style->mutable_description_style()->set_font_name(font_name);

  RendererStyle::InfolistStyle* infostyle = style->mutable_infolist_style();
  infostyle->mutable_caption_style()->set_font_name(font_name);
  infostyle->mutable_title_style()->set_font_name(font_name);
  infostyle->mutable_description_style()->set_font_name(font_name);
}

}  // namespace renderer
}  // namespace mozc
