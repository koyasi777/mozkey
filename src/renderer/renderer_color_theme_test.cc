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
