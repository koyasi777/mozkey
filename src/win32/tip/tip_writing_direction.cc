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
#include "win32/tip/tip_writing_direction.h"

#include <cstdint>
#include <optional>

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

WritingDirection DirectionFromOrientation(int32_t orientation_tenths) {
  constexpr int32_t kFullTurn = 3600;
  int32_t normalized = orientation_tenths % kFullTurn;
  if (normalized < 0) {
    normalized += kFullTurn;
  }

  switch (normalized) {
    case 0:
    case 1800:
      return WritingDirection::kHorizontal;
    case 900:
    case 2700:
      return WritingDirection::kVertical;
    default:
      return WritingDirection::kUnknown;
  }
}

}  // namespace

WritingDirection ResolveWritingDirection(
    std::optional<bool> vertical_writing,
    std::optional<int32_t> orientation_tenths) {
  if (vertical_writing.has_value()) {
    return *vertical_writing ? WritingDirection::kVertical
                             : WritingDirection::kHorizontal;
  }
  if (orientation_tenths.has_value()) {
    return DirectionFromOrientation(*orientation_tenths);
  }
  return WritingDirection::kUnknown;
}

WritingDirection InferWritingDirectionFromExtentGrowth(
    int base_width, int base_height, int expanded_width, int expanded_height) {
  if (base_width <= 0 || base_height <= 0 || expanded_width <= 0 ||
      expanded_height <= 0 || expanded_width < base_width ||
      expanded_height < base_height) {
    return WritingDirection::kUnknown;
  }

  const int width_growth = expanded_width - base_width;
  const int height_growth = expanded_height - base_height;
  constexpr int kMinimumPrimaryGrowth = 2;

  if (width_growth >= kMinimumPrimaryGrowth &&
      height_growth <= width_growth / 3) {
    return WritingDirection::kHorizontal;
  }
  if (height_growth >= kMinimumPrimaryGrowth &&
      width_growth <= height_growth / 3) {
    return WritingDirection::kVertical;
  }
  return WritingDirection::kUnknown;
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc