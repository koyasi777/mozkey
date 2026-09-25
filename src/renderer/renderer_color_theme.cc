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
