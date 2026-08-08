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

#ifndef MOZC_RENDERER_RENDERER_STYLE_HANDLER_H_
#define MOZC_RENDERER_RENDERER_STYLE_HANDLER_H_

#include <cstdint>
#include <string>

#include "protocol/renderer_style.pb.h"

namespace mozc {
namespace renderer {

// this is pure static class
class RendererStyleHandler {
 public:
  RendererStyleHandler() = delete;

  enum class RendererStyleType {
    kCandidate,
    kSuggestion,
  };

  struct WindowShadowStyle {
    uint32_t size = 12;
    uint32_t opacity_percent = 30;
    uint32_t angle_degrees = 45;
    uint32_t distance = 6;
  };

  struct CandidateWindowEffectStyle {
    uint32_t opacity_percent = 100;
    WindowShadowStyle shadow;
  };

  struct RubyWindowStyle {
    bool enabled = true;
    uint32_t background_color = 0xffffff;
    uint32_t text_color = 0x000000;
    uint32_t border_color = 0x969696;
    uint32_t corner_radius = 9;
    uint32_t size_percent = 100;
    uint32_t font_weight = 400;
    uint32_t opacity_percent = 90;
    uint32_t horizontal_padding = 14;
    uint32_t vertical_padding = 6;
    uint32_t composition_gap = 4;
    WindowShadowStyle shadow;
  };

  // return current Style
  static bool GetRendererStyle(RendererStyle* style);
  // set Style
  static bool SetRendererStyle(const RendererStyle& style);
  // get default Style
  static void GetDefaultRendererStyle(RendererStyle* style);

  // return the style for a concrete renderer window class.
  // GetRendererStyle() remains as candidate-window compatibility API.
  static bool GetRendererStyleForWindowType(RendererStyleType type,
                                            RendererStyle* style);

  // Atomically updates the candidate/suggestion/ruby appearance.
  static bool SetRendererWindowStyles(
      const RendererStyle& candidate_style, const RendererStyle& suggestion_style,
      const RubyWindowStyle& ruby_style, uint32_t candidate_corner_radius,
      uint32_t suggestion_corner_radius,
      const CandidateWindowEffectStyle& candidate_effect_style,
      const CandidateWindowEffectStyle& suggestion_effect_style);

  // Returns the logical corner radius for a candidate-like window. The caller
  // scales this value for DPI and converts radius to GDI diameter if needed.
  static uint32_t GetCandidateWindowCornerRadius(RendererStyleType type);

  // Returns the candidate-like window opacity and custom shadow settings.
  static CandidateWindowEffectStyle GetCandidateWindowEffectStyle(
      RendererStyleType type);

  // Returns the ruby-window appearance. Colors are 0xRRGGBB. Corner radius is
  // a logical pixel radius at 100% DPI. Spacing values use the ruby window's
  // 150% display design baseline and are scaled by size_percent at rendering.
  static RubyWindowStyle GetRubyWindowStyle();

  // Applies candidate window theme options to the given style.
  static void ApplyCandidateWindowTheme(bool use_dark_mode,
                                        RendererStyle* style);

  // Applies custom candidate-like window colors to the given style.
  // Color values are 0xRRGGBB, not Windows COLORREF.
  static void ApplyCandidateWindowCustomColors(
      uint32_t background_color, uint32_t text_color,
      uint32_t selected_background_color, uint32_t selected_border_color,
      uint32_t border_color, uint32_t shortcut_text_color,
      uint32_t shortcut_background_color, uint32_t description_text_color,
      uint32_t footer_text_color, uint32_t footer_background_color,
      uint32_t footer_border_color, uint32_t scrollbar_background_color,
      uint32_t scrollbar_indicator_color, RendererStyle* style);

  // Applies a candidate-like window size percentage to font sizes and layout
  // metrics. 100 means the default size.
  static void ApplyCandidateWindowSize(uint32_t size_percent,
                                       RendererStyle* style);

  // Applies candidate/ruby font options to the given style.
  static void ApplyCandidateRubyFont(const std::string& font_name,
                                     RendererStyle* style);
};

}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_RENDERER_STYLE_HANDLER_H_
