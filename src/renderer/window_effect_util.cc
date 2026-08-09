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

#include "renderer/window_effect_util.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace mozc {
namespace renderer {
namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

double SignedDistanceFromRoundedRect(double x, double y, double width,
                                     double height, double radius) {
  radius = std::max(0.0, std::min(radius, std::min(width, height) / 2.0));
  const double half_width = width / 2.0;
  const double half_height = height / 2.0;
  const double qx = std::abs(x - half_width) - (half_width - radius);
  const double qy = std::abs(y - half_height) - (half_height - radius);
  const double outside_x = std::max(qx, 0.0);
  const double outside_y = std::max(qy, 0.0);
  double outside_distance = 0.0;
  if (outside_x > 0.0 || outside_y > 0.0) {
    outside_distance =
        std::sqrt(outside_x * outside_x + outside_y * outside_y);
  }
  const double inside_distance = std::min(std::max(qx, qy), 0.0);
  return outside_distance + inside_distance - radius;
}

double RoundedRectCoverage(double x, double y, double width, double height,
                           double radius) {
  return std::clamp(
      0.5 - SignedDistanceFromRoundedRect(x, y, width, height, radius), 0.0,
      1.0);
}

WindowShadowGeometry ComputeWindowShadowGeometry(
    int owner_width, int owner_height, int shadow_size, int shadow_distance,
    uint32_t angle_degrees, int owner_corner_radius) {
  WindowShadowGeometry geometry;
  geometry.owner_width = std::max(owner_width, 0);
  geometry.owner_height = std::max(owner_height, 0);
  geometry.shadow_size = std::max(shadow_size, 0);
  geometry.corner_radius =
      std::clamp(owner_corner_radius, 0,
                 std::min(geometry.owner_width, geometry.owner_height) / 2);

  if (geometry.owner_width <= 0 || geometry.owner_height <= 0 ||
      geometry.shadow_size <= 0) {
    return geometry;
  }

  const int distance = std::max(shadow_distance, 0);
  const double radians =
      static_cast<double>(angle_degrees % 360u) * kPi / 180.0;
  geometry.shadow_dx =
      static_cast<int>(std::lround(std::cos(radians) * distance));
  geometry.shadow_dy =
      static_cast<int>(std::lround(std::sin(radians) * distance));

  geometry.left_margin =
      geometry.shadow_size + std::max(-geometry.shadow_dx, 0);
  geometry.right_margin =
      geometry.shadow_size + std::max(geometry.shadow_dx, 0);
  geometry.top_margin =
      geometry.shadow_size + std::max(-geometry.shadow_dy, 0);
  geometry.bottom_margin =
      geometry.shadow_size + std::max(geometry.shadow_dy, 0);
  geometry.owner_left = geometry.left_margin;
  geometry.owner_top = geometry.top_margin;

  const int64_t width = static_cast<int64_t>(geometry.owner_width) +
                        geometry.left_margin + geometry.right_margin;
  const int64_t height = static_cast<int64_t>(geometry.owner_height) +
                         geometry.top_margin + geometry.bottom_margin;
  if (width <= 0 || height <= 0 ||
      width > std::numeric_limits<int>::max() ||
      height > std::numeric_limits<int>::max()) {
    return WindowShadowGeometry();
  }
  geometry.width = static_cast<int>(width);
  geometry.height = static_cast<int>(height);
  return geometry;
}

uint8_t ComputeWindowShadowAlpha(const WindowShadowGeometry &geometry, int x,
                                 int y, uint32_t opacity_percent) {
  if (!geometry.enabled() || x < 0 || y < 0 || x >= geometry.width ||
      y >= geometry.height || opacity_percent == 0) {
    return 0;
  }

  const double owner_x =
      static_cast<double>(x - geometry.owner_left) + 0.5;
  const double owner_y =
      static_cast<double>(y - geometry.owner_top) + 0.5;
  const double owner_distance = SignedDistanceFromRoundedRect(
      owner_x, owner_y, geometry.owner_width, geometry.owner_height,
      geometry.corner_radius);
  if (owner_distance <= 0.0) {
    return 0;
  }

  const double shadow_x =
      static_cast<double>(x - geometry.owner_left - geometry.shadow_dx) + 0.5;
  const double shadow_y =
      static_cast<double>(y - geometry.owner_top - geometry.shadow_dy) + 0.5;
  const double shadow_distance = SignedDistanceFromRoundedRect(
      shadow_x, shadow_y, geometry.owner_width, geometry.owner_height,
      geometry.corner_radius);

  double normalized = 0.0;
  if (shadow_distance > 0.0) {
    normalized = shadow_distance / static_cast<double>(geometry.shadow_size);
  }
  if (normalized > 1.0) {
    return 0;
  }

  const double t = 1.0 - std::clamp(normalized, 0.0, 1.0);
  const uint32_t max_alpha =
      std::clamp(opacity_percent, 0u, kMaxWindowShadowOpacityPercent) * 255u / 100u;
  return static_cast<uint8_t>(std::clamp(
      std::lround(static_cast<double>(max_alpha) * t * t * (3.0 - 2.0 * t)),
      0l, 255l));
}

bool RenderWindowShadowAlphaMask(const WindowShadowGeometry &geometry,
                                 uint32_t opacity_percent,
                                 uint8_t *destination,
                                 size_t destination_size) {
  if (destination == nullptr || geometry.width < 0 || geometry.height < 0) {
    return false;
  }
  const int64_t required =
      static_cast<int64_t>(geometry.width) * geometry.height;
  if (required < 0 || static_cast<uint64_t>(required) > destination_size) {
    return false;
  }
  if (required == 0) {
    return true;
  }

  std::memset(destination, 0, static_cast<size_t>(required));
  if (!geometry.enabled() || opacity_percent == 0) {
    return true;
  }

  for (int y = 0; y < geometry.height; ++y) {
    for (int x = 0; x < geometry.width; ++x) {
      destination[static_cast<size_t>(y) * geometry.width + x] =
          ComputeWindowShadowAlpha(geometry, x, y, opacity_percent);
    }
  }
  return true;
}

}  // namespace renderer
}  // namespace mozc
