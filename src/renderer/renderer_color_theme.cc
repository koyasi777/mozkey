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

#include <algorithm>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif  // _WIN32

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include "renderer/mac/renderer_color_theme_macos.h"
#endif  // TARGET_OS_OSX
#endif  // __APPLE__

namespace mozc {
namespace renderer {

SystemColorTheme GetSystemColorTheme() {
#ifdef _WIN32
  // Windows Settings -> Personalization -> Colors -> app mode.
  // AppsUseLightTheme: 0 = Dark, nonzero = Light.
  DWORD apps_use_light_theme = 1;
  DWORD size = sizeof(apps_use_light_theme);
  const LONG result = ::RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &apps_use_light_theme,
      &size);
  if (result != ERROR_SUCCESS) {
    return SystemColorTheme::kLight;
  }
  return apps_use_light_theme == 0 ? SystemColorTheme::kDark
                                   : SystemColorTheme::kLight;
#elif defined(__APPLE__) && TARGET_OS_OSX
  return mac::IsSystemDarkColorTheme() ? SystemColorTheme::kDark
                                       : SystemColorTheme::kLight;
#else
  return SystemColorTheme::kLight;
#endif
}

SystemColorTheme ResolveWindowsModeIndicatorTheme(
    config::Config::WindowsModeIndicatorTheme theme,
    SystemColorTheme system_theme) {
  switch (theme) {
    case config::Config::WINDOWS_MODE_INDICATOR_THEME_DARK:
      return SystemColorTheme::kDark;
    case config::Config::WINDOWS_MODE_INDICATOR_THEME_LIGHT:
      return SystemColorTheme::kLight;
    case config::Config::WINDOWS_MODE_INDICATOR_THEME_CUSTOM:
    case config::Config::WINDOWS_MODE_INDICATOR_THEME_SYSTEM:
    default:
      return system_theme;
  }
}

namespace {

// BalloonImage applies a direct 2-D Gaussian convolution. Keep the user-facing
// upper bound close to the default (4 px) so a custom style cannot create an
// unexpectedly large convolution kernel, especially on high-DPI displays.
constexpr uint32_t kMaxWindowsModeIndicatorShadowBlur = 6;

}  // namespace

bool WindowsModeIndicatorStyle::operator==(
    const WindowsModeIndicatorStyle& other) const {
  return ascii_background_color == other.ascii_background_color &&
         ascii_border_color == other.ascii_border_color &&
         ascii_text_color == other.ascii_text_color &&
         ascii_shadow_color == other.ascii_shadow_color &&
         hiragana_background_color == other.hiragana_background_color &&
         hiragana_border_color == other.hiragana_border_color &&
         hiragana_text_color == other.hiragana_text_color &&
         hiragana_shadow_color == other.hiragana_shadow_color &&
         katakana_background_color == other.katakana_background_color &&
         katakana_border_color == other.katakana_border_color &&
         katakana_text_color == other.katakana_text_color &&
         katakana_shadow_color == other.katakana_shadow_color &&
         width == other.width && height == other.height &&
         corner_radius == other.corner_radius &&
         label_size == other.label_size &&
         border_thickness == other.border_thickness &&
         shadow_blur == other.shadow_blur &&
         shadow_opacity_percent == other.shadow_opacity_percent &&
         shadow_offset_x == other.shadow_offset_x &&
         shadow_offset_y == other.shadow_offset_y;
}

bool WindowsModeIndicatorStyle::operator!=(
    const WindowsModeIndicatorStyle& other) const {
  return !(*this == other);
}

WindowsModeIndicatorStyle GetWindowsModeIndicatorStyle(
    const config::Config& config) {
  const auto& style = config.windows_mode_indicator_custom_style();
  const auto color = [](uint32_t value) { return value & 0x00ffffffu; };
  const uint32_t width = std::clamp<uint32_t>(style.width(), 20, 96);
  const uint32_t height = std::clamp<uint32_t>(style.height(), 20, 96);
  const uint32_t corner_radius =
      std::min(std::clamp<uint32_t>(style.corner_radius(), 0, 32),
               std::min(width, height) / 2);

  return {
      color(style.ascii_background_color()),
      color(style.ascii_border_color()),
      color(style.ascii_text_color()),
      color(style.ascii_shadow_color()),
      color(style.hiragana_background_color()),
      color(style.hiragana_border_color()),
      color(style.hiragana_text_color()),
      color(style.hiragana_shadow_color()),
      color(style.katakana_background_color()),
      color(style.katakana_border_color()),
      color(style.katakana_text_color()),
      color(style.katakana_shadow_color()),
      width,
      height,
      corner_radius,
      std::clamp<uint32_t>(style.label_size(), 8, 32),
      std::clamp<uint32_t>(style.border_thickness(), 0, 6),
      std::clamp<uint32_t>(style.shadow_blur(), 0,
                           kMaxWindowsModeIndicatorShadowBlur),
      std::clamp<uint32_t>(style.shadow_opacity_percent(), 0, 100),
      std::clamp<int32_t>(style.shadow_offset_x(), -24, 24),
      std::clamp<int32_t>(style.shadow_offset_y(), -24, 24),
  };
}

config::Config::RendererWindowColorTheme ResolveRendererWindowColorTheme(
    config::Config::RendererWindowColorTheme theme,
    SystemColorTheme system_theme) {
  if (theme != config::Config::RENDERER_WINDOW_COLOR_AUTO) {
    return theme;
  }
  return system_theme == SystemColorTheme::kDark
             ? config::Config::RENDERER_WINDOW_COLOR_DARK
             : config::Config::RENDERER_WINDOW_COLOR_LIGHT;
}

config::Config::RendererWindowColorTheme
ResolveDependentRendererWindowColorTheme(
    config::Config::RendererWindowColorTheme theme,
    config::Config::RendererWindowColorTheme candidate_theme,
    SystemColorTheme system_theme) {
  if (theme == config::Config::RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE) {
    return candidate_theme;
  }
  return ResolveRendererWindowColorTheme(theme, system_theme);
}

}  // namespace renderer
}  // namespace mozc
