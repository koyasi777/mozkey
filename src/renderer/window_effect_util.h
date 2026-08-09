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

#ifndef MOZC_RENDERER_WINDOW_EFFECT_UTIL_H_
#define MOZC_RENDERER_WINDOW_EFFECT_UTIL_H_

#include <cstddef>
#include <cstdint>

namespace mozc {
namespace renderer {

inline constexpr uint32_t kMaxWindowShadowSize = 96;
inline constexpr uint32_t kMaxWindowShadowDistance = 96;
inline constexpr uint32_t kMaxWindowShadowOpacityPercent = 100;

// Pixel-space geometry for a renderer shadow. Coordinates use screen-space
// orientation: positive X points right and positive Y points down.
struct WindowShadowGeometry {
  int owner_width = 0;
  int owner_height = 0;
  int shadow_size = 0;
  int shadow_dx = 0;
  int shadow_dy = 0;
  int left_margin = 0;
  int right_margin = 0;
  int top_margin = 0;
  int bottom_margin = 0;
  int owner_left = 0;
  int owner_top = 0;
  int corner_radius = 0;
  int width = 0;
  int height = 0;

  bool enabled() const {
    return owner_width > 0 && owner_height > 0 && shadow_size > 0 &&
           width > 0 && height > 0;
  }

  bool operator==(const WindowShadowGeometry &other) const {
    return owner_width == other.owner_width &&
           owner_height == other.owner_height &&
           shadow_size == other.shadow_size && shadow_dx == other.shadow_dx &&
           shadow_dy == other.shadow_dy &&
           left_margin == other.left_margin &&
           right_margin == other.right_margin &&
           top_margin == other.top_margin &&
           bottom_margin == other.bottom_margin &&
           owner_left == other.owner_left && owner_top == other.owner_top &&
           corner_radius == other.corner_radius && width == other.width &&
           height == other.height;
  }

  bool operator!=(const WindowShadowGeometry &other) const {
    return !(*this == other);
  }
};

// Signed distance from a rounded rectangle. Negative means inside the shape;
// positive means outside it.
double SignedDistanceFromRoundedRect(double x, double y, double width,
                                     double height, double radius);

// Converts signed distance at a pixel center to one-pixel-wide antialias
// coverage in [0, 1].
double RoundedRectCoverage(double x, double y, double width, double height,
                           double radius);

// Computes the exact shadow bitmap geometry used by renderer platforms.
// |shadow_size|, |shadow_distance|, and |owner_corner_radius| are already
// scaled to physical pixels by the caller. The angle is screen-space degrees:
// 0=right, 45=down-right, 90=down.
WindowShadowGeometry ComputeWindowShadowGeometry(
    int owner_width, int owner_height, int shadow_size, int shadow_distance,
    uint32_t angle_degrees, int owner_corner_radius);

// Returns the alpha for one pixel in a shadow bitmap. The owner body itself is
// always transparent in the shadow bitmap so a shadow window can safely sit
// behind the corresponding renderer body.
uint8_t ComputeWindowShadowAlpha(const WindowShadowGeometry &geometry, int x,
                                 int y, uint32_t opacity_percent);

// Renders a tightly packed alpha mask of size geometry.width*geometry.height.
// The destination must contain at least |destination_size| bytes. Returns false
// for invalid parameters. Disabled geometry produces an all-zero mask.
bool RenderWindowShadowAlphaMask(const WindowShadowGeometry &geometry,
                                 uint32_t opacity_percent,
                                 uint8_t *destination,
                                 size_t destination_size);

}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WINDOW_EFFECT_UTIL_H_
