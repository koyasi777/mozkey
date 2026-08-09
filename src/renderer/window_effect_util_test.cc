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
#include <cstdint>
#include <vector>

#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace {

TEST(WindowEffectUtilTest, ComputesScreenSpaceShadowDirections) {
  const WindowShadowGeometry right =
      ComputeWindowShadowGeometry(100, 50, 12, 6, 0, 6);
  EXPECT_EQ(6, right.shadow_dx);
  EXPECT_EQ(0, right.shadow_dy);
  EXPECT_EQ(12, right.left_margin);
  EXPECT_EQ(18, right.right_margin);
  EXPECT_EQ(12, right.top_margin);
  EXPECT_EQ(12, right.bottom_margin);

  const WindowShadowGeometry down =
      ComputeWindowShadowGeometry(100, 50, 12, 6, 90, 6);
  EXPECT_EQ(0, down.shadow_dx);
  EXPECT_EQ(6, down.shadow_dy);
  EXPECT_EQ(12, down.left_margin);
  EXPECT_EQ(12, down.right_margin);
  EXPECT_EQ(12, down.top_margin);
  EXPECT_EQ(18, down.bottom_margin);

  const WindowShadowGeometry up_left =
      ComputeWindowShadowGeometry(100, 50, 12, 6, 225, 6);
  EXPECT_EQ(-4, up_left.shadow_dx);
  EXPECT_EQ(-4, up_left.shadow_dy);
  EXPECT_EQ(16, up_left.left_margin);
  EXPECT_EQ(12, up_left.right_margin);
  EXPECT_EQ(16, up_left.top_margin);
  EXPECT_EQ(12, up_left.bottom_margin);
}

TEST(WindowEffectUtilTest, GeometryEqualityCoversRenderingInputs) {
  const WindowShadowGeometry first =
      ComputeWindowShadowGeometry(100, 50, 12, 6, 45, 8);
  WindowShadowGeometry same = first;
  EXPECT_EQ(first, same);

  same.shadow_dx += 1;
  EXPECT_NE(first, same);
}

TEST(WindowEffectUtilTest, ZeroDistanceKeepsEvenMargins) {
  const WindowShadowGeometry geometry =
      ComputeWindowShadowGeometry(80, 40, 10, 0, 271, 100);
  EXPECT_TRUE(geometry.enabled());
  EXPECT_EQ(10, geometry.left_margin);
  EXPECT_EQ(10, geometry.right_margin);
  EXPECT_EQ(10, geometry.top_margin);
  EXPECT_EQ(10, geometry.bottom_margin);
  EXPECT_EQ(20, geometry.corner_radius);
}

TEST(WindowEffectUtilTest, DisabledShadowHasNoBitmapExtent) {
  const WindowShadowGeometry geometry =
      ComputeWindowShadowGeometry(80, 40, 0, 20, 45, 8);
  EXPECT_FALSE(geometry.enabled());
  EXPECT_EQ(0, geometry.width);
  EXPECT_EQ(0, geometry.height);
}

TEST(WindowEffectUtilTest, ShadowMaskNeverCoversOwnerBody) {
  const WindowShadowGeometry geometry =
      ComputeWindowShadowGeometry(30, 20, 8, 4, 45, 5);
  ASSERT_TRUE(geometry.enabled());

  std::vector<uint8_t> alpha(
      static_cast<size_t>(geometry.width) * geometry.height);
  ASSERT_TRUE(RenderWindowShadowAlphaMask(
      geometry, 60, alpha.data(), alpha.size()));

  for (int y = geometry.owner_top;
       y < geometry.owner_top + geometry.owner_height; ++y) {
    for (int x = geometry.owner_left;
         x < geometry.owner_left + geometry.owner_width; ++x) {
      const double owner_x =
          static_cast<double>(x - geometry.owner_left) + 0.5;
      const double owner_y =
          static_cast<double>(y - geometry.owner_top) + 0.5;
      if (SignedDistanceFromRoundedRect(
              owner_x, owner_y, geometry.owner_width, geometry.owner_height,
              geometry.corner_radius) <= 0.0) {
        EXPECT_EQ(0, alpha[static_cast<size_t>(y) * geometry.width + x]);
      }
    }
  }
  EXPECT_GT(*std::max_element(alpha.begin(), alpha.end()), 0);
}

TEST(WindowEffectUtilTest, OpacityScalesShadowAlpha) {
  const WindowShadowGeometry geometry =
      ComputeWindowShadowGeometry(40, 24, 10, 0, 0, 4);
  ASSERT_TRUE(geometry.enabled());

  uint8_t max_100 = 0;
  uint8_t max_50 = 0;
  for (int y = 0; y < geometry.height; ++y) {
    for (int x = 0; x < geometry.width; ++x) {
      max_100 = std::max(
          max_100, ComputeWindowShadowAlpha(geometry, x, y, 100));
      max_50 = std::max(
          max_50, ComputeWindowShadowAlpha(geometry, x, y, 50));
    }
  }
  EXPECT_GE(max_100, 254);
  EXPECT_NEAR(127, max_50, 1);
}

TEST(WindowEffectUtilTest, RoundedRectCoverageIsClamped) {
  EXPECT_DOUBLE_EQ(1.0, RoundedRectCoverage(10.5, 10.5, 20, 20, 4));
  EXPECT_DOUBLE_EQ(0.0, RoundedRectCoverage(-10.0, -10.0, 20, 20, 4));
}

}  // namespace
}  // namespace renderer
}  // namespace mozc
