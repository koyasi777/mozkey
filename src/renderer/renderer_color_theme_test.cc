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

#include "renderer/renderer_color_theme.h"

#include "protocol/config.pb.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace {

using ColorTheme = config::Config::RendererWindowColorTheme;

TEST(RendererColorThemeTest, ConfigDefaultsUseAuto) {
  const config::Config config;

  EXPECT_FALSE(config.has_candidate_window_color_theme());
  EXPECT_FALSE(config.has_suggest_window_color_theme());
  EXPECT_FALSE(config.has_ruby_window_color_theme());
  EXPECT_EQ(config::Config::RENDERER_WINDOW_COLOR_AUTO,
            config.candidate_window_color_theme());
  EXPECT_EQ(config::Config::RENDERER_WINDOW_COLOR_AUTO,
            config.suggest_window_color_theme());
  EXPECT_EQ(config::Config::RENDERER_WINDOW_COLOR_AUTO,
            config.ruby_window_color_theme());
}

TEST(RendererColorThemeTest, ModeIndicatorConfigDefaultsUseSystemTheme) {
  const config::Config config;

  EXPECT_FALSE(config.has_windows_mode_indicator_theme());
  EXPECT_EQ(config::Config::WINDOWS_MODE_INDICATOR_THEME_SYSTEM,
            config.windows_mode_indicator_theme());
  EXPECT_FALSE(config.has_windows_mode_indicator_custom_style());

  const auto& style = config.windows_mode_indicator_custom_style();
  EXPECT_EQ(0xf5f8fcu, style.ascii_background_color());
  EXPECT_EQ(0x24968cu, style.hiragana_border_color());
  EXPECT_EQ(0x303469u, style.katakana_text_color());
  EXPECT_EQ(36u, style.width());
  EXPECT_EQ(28u, style.height());
  EXPECT_EQ(8u, style.corner_radius());
  EXPECT_EQ(12u, style.label_size());
  EXPECT_EQ(1u, style.border_thickness());
  EXPECT_EQ(4u, style.shadow_blur());
  EXPECT_EQ(22u, style.shadow_opacity_percent());
  EXPECT_EQ(0, style.shadow_offset_x());
  EXPECT_EQ(1, style.shadow_offset_y());
}

TEST(RendererColorThemeTest, ModeIndicatorStyleDefaultsRemainNormalized) {
  const config::Config config;
  const WindowsModeIndicatorStyle style =
      GetWindowsModeIndicatorStyle(config);

  EXPECT_EQ(0xf5f8fcu, style.ascii_background_color);
  EXPECT_EQ(0x24968cu, style.hiragana_border_color);
  EXPECT_EQ(0x303469u, style.katakana_text_color);
  EXPECT_EQ(36u, style.width);
  EXPECT_EQ(28u, style.height);
  EXPECT_EQ(8u, style.corner_radius);
  EXPECT_EQ(12u, style.label_size);
  EXPECT_EQ(1u, style.border_thickness);
  EXPECT_EQ(4u, style.shadow_blur);
  EXPECT_EQ(22u, style.shadow_opacity_percent);
  EXPECT_EQ(0, style.shadow_offset_x);
  EXPECT_EQ(1, style.shadow_offset_y);
}

TEST(RendererColorThemeTest, ModeIndicatorStyleClampsUntrustedConfig) {
  config::Config config;
  auto* proto = config.mutable_windows_mode_indicator_custom_style();
  proto->set_ascii_background_color(0xff123456u);
  proto->set_hiragana_border_color(0xab654321u);
  proto->set_katakana_text_color(0x7fabcdefu);
  proto->set_width(1);
  proto->set_height(999);
  proto->set_corner_radius(999);
  proto->set_label_size(1);
  proto->set_border_thickness(999);
  proto->set_shadow_blur(999);
  proto->set_shadow_opacity_percent(999);
  proto->set_shadow_offset_x(-999);
  proto->set_shadow_offset_y(999);

  const WindowsModeIndicatorStyle style =
      GetWindowsModeIndicatorStyle(config);

  EXPECT_EQ(0x123456u, style.ascii_background_color);
  EXPECT_EQ(0x654321u, style.hiragana_border_color);
  EXPECT_EQ(0xabcdefu, style.katakana_text_color);
  EXPECT_EQ(20u, style.width);
  EXPECT_EQ(96u, style.height);
  EXPECT_EQ(10u, style.corner_radius);
  EXPECT_EQ(8u, style.label_size);
  EXPECT_EQ(6u, style.border_thickness);
  EXPECT_EQ(6u, style.shadow_blur);
  EXPECT_EQ(100u, style.shadow_opacity_percent);
  EXPECT_EQ(-24, style.shadow_offset_x);
  EXPECT_EQ(24, style.shadow_offset_y);
}

TEST(RendererColorThemeTest, ModeIndicatorSystemThemeUsesSystemAppearance) {
  EXPECT_EQ(SystemColorTheme::kLight,
            ResolveWindowsModeIndicatorTheme(
                config::Config::WINDOWS_MODE_INDICATOR_THEME_SYSTEM,
                SystemColorTheme::kLight));
  EXPECT_EQ(SystemColorTheme::kDark,
            ResolveWindowsModeIndicatorTheme(
                config::Config::WINDOWS_MODE_INDICATOR_THEME_SYSTEM,
                SystemColorTheme::kDark));
}

TEST(RendererColorThemeTest, ModeIndicatorExplicitThemeOverridesSystem) {
  EXPECT_EQ(SystemColorTheme::kDark,
            ResolveWindowsModeIndicatorTheme(
                config::Config::WINDOWS_MODE_INDICATOR_THEME_DARK,
                SystemColorTheme::kLight));
  EXPECT_EQ(SystemColorTheme::kLight,
            ResolveWindowsModeIndicatorTheme(
                config::Config::WINDOWS_MODE_INDICATOR_THEME_LIGHT,
                SystemColorTheme::kDark));
}

TEST(RendererColorThemeTest, ModeIndicatorCustomKeepsFallbackSystemAppearance) {
  EXPECT_EQ(SystemColorTheme::kLight,
            ResolveWindowsModeIndicatorTheme(
                config::Config::WINDOWS_MODE_INDICATOR_THEME_CUSTOM,
                SystemColorTheme::kLight));
  EXPECT_EQ(SystemColorTheme::kDark,
            ResolveWindowsModeIndicatorTheme(
                config::Config::WINDOWS_MODE_INDICATOR_THEME_CUSTOM,
                SystemColorTheme::kDark));
}

TEST(RendererColorThemeTest, ResolvesAutoFromSystemTheme) {
  EXPECT_EQ(config::Config::RENDERER_WINDOW_COLOR_LIGHT,
            ResolveRendererWindowColorTheme(
                config::Config::RENDERER_WINDOW_COLOR_AUTO,
                SystemColorTheme::kLight));
  EXPECT_EQ(config::Config::RENDERER_WINDOW_COLOR_DARK,
            ResolveRendererWindowColorTheme(
                config::Config::RENDERER_WINDOW_COLOR_AUTO,
                SystemColorTheme::kDark));
}

TEST(RendererColorThemeTest, KeepsExplicitThemeUnchanged) {
  for (const ColorTheme theme : {
           config::Config::RENDERER_WINDOW_COLOR_LIGHT,
           config::Config::RENDERER_WINDOW_COLOR_DARK,
           config::Config::RENDERER_WINDOW_COLOR_CUSTOM,
       }) {
    EXPECT_EQ(theme,
              ResolveRendererWindowColorTheme(theme, SystemColorTheme::kDark));
  }
}

TEST(RendererColorThemeTest, FollowCandidateKeepsResolvedCandidateTheme) {
  EXPECT_EQ(config::Config::RENDERER_WINDOW_COLOR_CUSTOM,
            ResolveDependentRendererWindowColorTheme(
                config::Config::RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE,
                config::Config::RENDERER_WINDOW_COLOR_CUSTOM,
                SystemColorTheme::kLight));
  EXPECT_EQ(config::Config::RENDERER_WINDOW_COLOR_DARK,
            ResolveDependentRendererWindowColorTheme(
                config::Config::RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE,
                config::Config::RENDERER_WINDOW_COLOR_DARK,
                SystemColorTheme::kLight));
}

TEST(RendererColorThemeTest, DependentAutoUsesSystemTheme) {
  EXPECT_EQ(config::Config::RENDERER_WINDOW_COLOR_DARK,
            ResolveDependentRendererWindowColorTheme(
                config::Config::RENDERER_WINDOW_COLOR_AUTO,
                config::Config::RENDERER_WINDOW_COLOR_LIGHT,
                SystemColorTheme::kDark));
}

}  // namespace
}  // namespace renderer
}  // namespace mozc
