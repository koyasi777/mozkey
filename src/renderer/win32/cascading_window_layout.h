// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
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
#ifndef MOZC_RENDERER_WIN32_CASCADING_WINDOW_LAYOUT_H_
#define MOZC_RENDERER_WIN32_CASCADING_WINDOW_LAYOUT_H_

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace win32 {

enum class CascadingWindowLayoutMode {
  kHorizontal,
  kVertical,
};

CascadingWindowLayoutMode GetCascadingWindowLayoutMode(
    bool vertical_writing);

// Routes placement to the existing horizontal or vertical geometry policy.
// selected_row_with_window_border is used only by the horizontal policy.
Rect GetCascadingWindowRect(
    CascadingWindowLayoutMode layout_mode,
    const Rect& selected_candidate,
    const Rect& selected_row_with_window_border,
    const Rect& candidate_rect,
    const Size& window_size,
    const Point& zero_point_offset,
    const Rect& avoid_rect,
    const Rect& working_area);

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_CASCADING_WINDOW_LAYOUT_H_
