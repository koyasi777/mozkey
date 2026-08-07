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

#include <cstdint>

#include "protocol/renderer_style.pb.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {

TEST(RendererStyleHandlerTest, GetRendererStyle) {
  RendererStyle style;
  RendererStyleHandler::GetRendererStyle(&style);
  EXPECT_TRUE(style.has_window_border());
  EXPECT_TRUE(style.has_infolist_style());
  EXPECT_TRUE(style.infolist_style().has_focused_border_color());
  EXPECT_EQ(400, style.candidate_style().font_weight());
}

TEST(RendererStyleHandlerTest, ApplyCandidateRubyFont) {
  RendererStyle style;
  RendererStyleHandler::GetDefaultRendererStyle(&style);

  RendererStyleHandler::ApplyCandidateRubyFont("Yu Gothic UI", &style);

  EXPECT_FALSE(style.shortcut_style().has_font_name());
  EXPECT_EQ("Yu Gothic UI", style.candidate_style().font_name());
  EXPECT_EQ("Yu Gothic UI", style.description_style().font_name());
  EXPECT_FALSE(style.footer_style().has_font_name());
  EXPECT_FALSE(style.footer_sub_label_style().has_font_name());
  EXPECT_EQ("Yu Gothic UI",
            style.infolist_style().caption_style().font_name());
  EXPECT_EQ("Yu Gothic UI", style.infolist_style().title_style().font_name());
  EXPECT_EQ("Yu Gothic UI",
            style.infolist_style().description_style().font_name());
}

TEST(RendererStyleHandlerTest, ApplyCandidateRubyFontSkipsEmptyFontName) {
  RendererStyle style;
  RendererStyleHandler::GetDefaultRendererStyle(&style);

  RendererStyleHandler::ApplyCandidateRubyFont("", &style);

  EXPECT_FALSE(style.candidate_style().has_font_name());
  EXPECT_FALSE(style.footer_style().has_font_name());
  EXPECT_FALSE(style.infolist_style().title_style().has_font_name());
}


namespace {
uint32_t Rgb(const RendererStyle::RGBAColor& color) {
  return ((static_cast<uint32_t>(color.r()) & 0xff) << 16) |
         ((static_cast<uint32_t>(color.g()) & 0xff) << 8) |
         (static_cast<uint32_t>(color.b()) & 0xff);
}
}  // namespace

TEST(RendererStyleHandlerTest, ApplyCandidateWindowCustomColors) {
  RendererStyle style;
  RendererStyleHandler::GetDefaultRendererStyle(&style);

  RendererStyleHandler::ApplyCandidateWindowCustomColors(
      0x010203, 0x040506, 0x070809, 0x0a0b0c, 0x0d0e0f,
      0x101112, 0x131415, 0x161718, 0x191a1b, 0x1c1d1e,
      0x1f2021, 0x222324, 0x252627, &style);

  EXPECT_EQ(0x010203, Rgb(style.candidate_style().background_color()));
  EXPECT_EQ(0x040506, Rgb(style.candidate_style().foreground_color()));
  EXPECT_EQ(0x070809, Rgb(style.focused_background_color()));
  EXPECT_EQ(0x0a0b0c, Rgb(style.focused_border_color()));
  EXPECT_EQ(0x0d0e0f, Rgb(style.border_color()));
  EXPECT_EQ(0x101112, Rgb(style.shortcut_style().foreground_color()));
  EXPECT_EQ(0x131415, Rgb(style.shortcut_style().background_color()));
  EXPECT_EQ(0x010203, Rgb(style.gap1_style().background_color()));
  EXPECT_EQ(0x161718, Rgb(style.description_style().foreground_color()));
  EXPECT_EQ(0x191a1b, Rgb(style.footer_style().foreground_color()));
  EXPECT_EQ(0x1c1d1e, Rgb(style.footer_top_color()));
  EXPECT_EQ(0x1f2021, Rgb(style.footer_border_colors(0)));
  EXPECT_EQ(0x222324, Rgb(style.scrollbar_background_color()));
  EXPECT_EQ(0x252627, Rgb(style.scrollbar_indicator_color()));
}


TEST(RendererStyleHandlerTest,
     ApplyCandidateWindowThemeSetsGapBackground) {
  RendererStyle style;
  RendererStyleHandler::GetDefaultRendererStyle(&style);

  RendererStyleHandler::ApplyCandidateWindowTheme(false, &style);
  EXPECT_TRUE(style.gap1_style().has_background_color());
  EXPECT_EQ(Rgb(style.candidate_style().background_color()),
            Rgb(style.gap1_style().background_color()));

  RendererStyleHandler::ApplyCandidateWindowTheme(true, &style);
  EXPECT_TRUE(style.gap1_style().has_background_color());
  EXPECT_EQ(Rgb(style.candidate_style().background_color()),
            Rgb(style.gap1_style().background_color()));
}

TEST(RendererStyleHandlerTest, ApplyCandidateWindowSize) {
  RendererStyle style;
  RendererStyleHandler::GetDefaultRendererStyle(&style);

  RendererStyleHandler::ApplyCandidateWindowSize(150, &style);

  EXPECT_DOUBLE_EQ(21, style.candidate_style().font_size());
  EXPECT_DOUBLE_EQ(18, style.description_style().font_size());
  EXPECT_EQ(6, style.scrollbar_width());
  EXPECT_EQ(450, style.infolist_style().window_width());
}

TEST(RendererStyleHandlerTest, SetRendererWindowStylesSeparatesSuggestion) {
  RendererStyle candidate_style;
  RendererStyle suggestion_style;
  RendererStyleHandler::GetDefaultRendererStyle(&candidate_style);
  RendererStyleHandler::GetDefaultRendererStyle(&suggestion_style);
  RendererStyleHandler::ApplyCandidateWindowCustomColors(
      0x111111, 0x222222, 0x333333, 0x444444, 0x555555,
      0x666666, 0x777777, 0x888888, 0x999999, 0xaaaaaa,
      0xbbbbbb, 0xcccccc, 0xdddddd, &suggestion_style);

  RendererStyleHandler::RubyWindowStyle ruby_style;
  ruby_style.background_color = 0xabcdef;
  ruby_style.text_color = 0x123456;
  ruby_style.border_color = 0x654321;
  ruby_style.corner_radius = 12;
  ruby_style.font_weight = 700;
  ruby_style.horizontal_padding = 20;
  ruby_style.vertical_padding = 8;
  ruby_style.composition_gap = 7;

  RendererStyleHandler::CandidateWindowEffectStyle candidate_effect;
  candidate_effect.opacity_percent = 95;
  candidate_effect.shadow.size = 8;
  candidate_effect.shadow.opacity_percent = 40;
  candidate_effect.shadow.angle_degrees = 45;
  candidate_effect.shadow.distance = 6;
  RendererStyleHandler::CandidateWindowEffectStyle suggestion_effect;
  suggestion_effect.opacity_percent = 80;
  suggestion_effect.shadow.size = 10;
  suggestion_effect.shadow.opacity_percent = 50;
  suggestion_effect.shadow.angle_degrees = 90;
  suggestion_effect.shadow.distance = 4;

  RendererStyleHandler::SetRendererWindowStyles(
      candidate_style, suggestion_style, ruby_style, 6, 10, candidate_effect,
      suggestion_effect);

  RendererStyle actual_candidate_style;
  RendererStyle actual_suggestion_style;
  EXPECT_TRUE(RendererStyleHandler::GetRendererStyleForWindowType(
      RendererStyleHandler::RendererStyleType::kCandidate,
      &actual_candidate_style));
  EXPECT_TRUE(RendererStyleHandler::GetRendererStyleForWindowType(
      RendererStyleHandler::RendererStyleType::kSuggestion,
      &actual_suggestion_style));

  EXPECT_NE(Rgb(actual_candidate_style.candidate_style().background_color()),
            Rgb(actual_suggestion_style.candidate_style().background_color()));
  EXPECT_EQ(0x111111,
            Rgb(actual_suggestion_style.candidate_style().background_color()));
  EXPECT_EQ(6u, RendererStyleHandler::GetCandidateWindowCornerRadius(
                    RendererStyleHandler::RendererStyleType::kCandidate));
  EXPECT_EQ(10u, RendererStyleHandler::GetCandidateWindowCornerRadius(
                     RendererStyleHandler::RendererStyleType::kSuggestion));
  EXPECT_EQ(0xabcdef,
            RendererStyleHandler::GetRubyWindowStyle().background_color);
  EXPECT_EQ(12u, RendererStyleHandler::GetRubyWindowStyle().corner_radius);
  EXPECT_EQ(700u, RendererStyleHandler::GetRubyWindowStyle().font_weight);
  EXPECT_EQ(20u,
            RendererStyleHandler::GetRubyWindowStyle().horizontal_padding);
  EXPECT_EQ(8u, RendererStyleHandler::GetRubyWindowStyle().vertical_padding);
  EXPECT_EQ(7u, RendererStyleHandler::GetRubyWindowStyle().composition_gap);
  EXPECT_EQ(95u, RendererStyleHandler::GetCandidateWindowEffectStyle(
                      RendererStyleHandler::RendererStyleType::kCandidate)
                      .opacity_percent);
  EXPECT_EQ(80u, RendererStyleHandler::GetCandidateWindowEffectStyle(
                      RendererStyleHandler::RendererStyleType::kSuggestion)
                      .opacity_percent);
  EXPECT_EQ(8u, RendererStyleHandler::GetCandidateWindowEffectStyle(
                     RendererStyleHandler::RendererStyleType::kCandidate)
                     .shadow.size);
  EXPECT_EQ(40u, RendererStyleHandler::GetCandidateWindowEffectStyle(
                      RendererStyleHandler::RendererStyleType::kCandidate)
                      .shadow.opacity_percent);
  EXPECT_EQ(45u, RendererStyleHandler::GetCandidateWindowEffectStyle(
                      RendererStyleHandler::RendererStyleType::kCandidate)
                      .shadow.angle_degrees);
  EXPECT_EQ(6u, RendererStyleHandler::GetCandidateWindowEffectStyle(
                     RendererStyleHandler::RendererStyleType::kCandidate)
                     .shadow.distance);
  EXPECT_EQ(10u, RendererStyleHandler::GetCandidateWindowEffectStyle(
                      RendererStyleHandler::RendererStyleType::kSuggestion)
                      .shadow.size);
  EXPECT_EQ(90u, RendererStyleHandler::GetCandidateWindowEffectStyle(
                      RendererStyleHandler::RendererStyleType::kSuggestion)
                      .shadow.angle_degrees);
  EXPECT_EQ(4u, RendererStyleHandler::GetCandidateWindowEffectStyle(
                     RendererStyleHandler::RendererStyleType::kSuggestion)
                     .shadow.distance);
  EXPECT_EQ(50u, RendererStyleHandler::GetCandidateWindowEffectStyle(
                      RendererStyleHandler::RendererStyleType::kSuggestion)
                      .shadow.opacity_percent);
}

}  // namespace renderer
}  // namespace mozc
