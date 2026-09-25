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

#ifndef MOZC_RENDERER_RENDERER_COLOR_THEME_H_
#define MOZC_RENDERER_RENDERER_COLOR_THEME_H_

#include <cstdint>

#include "protocol/config.pb.h"

namespace mozc {
namespace renderer {

enum class SystemColorTheme {
  kLight,
  kDark,
};

// Returns the current platform application appearance. Windows follows the
// user's app mode (AppsUseLightTheme); macOS follows NSApplication's effective
// appearance. Other platforms use Light as a conservative fallback.
SystemColorTheme GetSystemColorTheme();

// Resolves the Windows input-mode indicator preference to an effective
// light/dark appearance. SYSTEM preserves |system_theme|; explicit DARK/LIGHT
// override it. CUSTOM is rendered separately and preserves |system_theme| here
// as a conservative fallback for callers that still require light/dark.
SystemColorTheme ResolveWindowsModeIndicatorTheme(
    config::Config::WindowsModeIndicatorTheme theme,
    SystemColorTheme system_theme);

// Sanitized renderer-side representation of the custom input-mode indicator
// style. Config values are normalized before they reach GDI/BalloonImage.
struct WindowsModeIndicatorStyle {
  uint32_t ascii_background_color;
  uint32_t ascii_border_color;
  uint32_t ascii_text_color;
  uint32_t ascii_shadow_color;
  uint32_t hiragana_background_color;
  uint32_t hiragana_border_color;
  uint32_t hiragana_text_color;
  uint32_t hiragana_shadow_color;
  uint32_t katakana_background_color;
  uint32_t katakana_border_color;
  uint32_t katakana_text_color;
  uint32_t katakana_shadow_color;
  uint32_t width;
  uint32_t height;
  uint32_t corner_radius;
  uint32_t label_size;
  uint32_t border_thickness;
  uint32_t shadow_blur;
  uint32_t shadow_opacity_percent;
  int32_t shadow_offset_x;
  int32_t shadow_offset_y;

  bool operator==(const WindowsModeIndicatorStyle& other) const;
  bool operator!=(const WindowsModeIndicatorStyle& other) const;
};

WindowsModeIndicatorStyle GetWindowsModeIndicatorStyle(
    const config::Config& config);

config::Config::RendererWindowColorTheme ResolveRendererWindowColorTheme(
    config::Config::RendererWindowColorTheme theme,
    SystemColorTheme system_theme);

config::Config::RendererWindowColorTheme
ResolveDependentRendererWindowColorTheme(
    config::Config::RendererWindowColorTheme theme,
    config::Config::RendererWindowColorTheme candidate_theme,
    SystemColorTheme system_theme);

}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_RENDERER_COLOR_THEME_H_
