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

// WindowUtil: OS-independent utility functions to manage candidate windows

#ifndef MOZC_RENDERER_WINDOW_UTIL_H_
#define MOZC_RENDERER_WINDOW_UTIL_H_

#include "base/coordinates.h"

namespace mozc {
namespace renderer {

class WindowUtil {
 public:
  // Returns the appropriate candidate window position in the screen
  // coordinate.  |zero_point_offset| is the point in the candidate
  // window which should be aligned to the preedit.
  // |working_area| is the available area in the current monitor.  If
  // caller fails to obtain |working_area|, set its width or height as
  // 0.  Then it doesn't care the monitor.
  static Rect GetWindowRectForMainWindowFromPreeditRect(
      const Rect& preedit_rect, const Size& window_size,
      const Point& zero_point_offset, const Rect& working_area);

  // Returns the appropriate candidate window position in the screen
  // coordinate.  |zero_point_offset| is the point in the candidate
  // window which should be aligned to the target point.
  // |working_area| is the available area in the current monitor.  If
  // caller fails to obtain |working_area|, set its width or height as
  // 0.  Then it doesn't care the monitor.
  static Rect GetWindowRectForMainWindowFromTargetPoint(
      const Point& target_point, const Size& window_size,
      const Point& zero_point_offset, const Rect& working_area);

  // Returns the appropriate candidate window position in the screen
  // coordinate.  For horizontal writing, |zero_point_offset| is aligned to
  // |target_point| as before.  For vertical writing, its y-coordinate aligns
  // the first candidate text with |target_point.y| while horizontal placement
  // is adjacent to |preedit_rect|: the left side is preferred and the right
  // side is the fallback.
  // |working_area| is the available area in the current monitor.  If
  // caller fails to obtain |working_area|, set its width or height as
  // 0.  Then it doesn't care the monitor.
  static Rect GetWindowRectForMainWindowFromTargetPointAndPreedit(
      const Point& target_point, const Rect& preedit_rect,
      const Size& window_size, const Point& zero_point_offset,
      const Rect& working_area, bool vertical);

  // Returns the appropriate cascading window position in the screen
  // coordinate.  |zero_point_offset| is the point in the cascading
  // window which should be aligned to the selected row in the
  // candidate window.
  // |working_area| is the available area in the current monitor.  If
  // caller fails to obtain |working_area|, set its width or height as
  // 0.  Then it doesn't care the monitor.
  static Rect GetWindowRectForCascadingWindow(const Rect& selected_row,
                                              const Size& window_size,
                                              const Point& zero_point_offset,
                                              const Rect& working_area);

  // Returns a ruby-window rectangle that stays inside |working_area| and
  // does not intersect |avoid_rect|. Placement above the preedit is preferred;
  // below is used as the fallback. Returns false if neither side is usable.
  static bool GetRubyWindowRect(const Rect& preedit_rect,
                                const Size& window_size, int gap,
                                const Rect& working_area,
                                const Rect* avoid_rect,
                                Rect* window_rect);

  // Returns a vertical-writing ruby rectangle. |composition_right_top| is the
  // right-top anchor of the active composition column and |composition_width|
  // is that column's physical width. |text_top_offset| is the vertical inset
  // from the ruby window edge to its first text glyph. Japanese vertical ruby
  // is placed on the right side first and on the left side as a fallback. The
  // first ruby glyph stays aligned with the active text unless the monitor work
  // area requires a vertical clamp.
  static bool GetRubyWindowRectForVerticalWriting(
      const Point& composition_right_top, int composition_width,
      const Size& window_size, int text_top_offset, int gap,
      const Rect& working_area, const Rect* avoid_rect, Rect* window_rect);

  // Returns the appropriate infolist window position in the screen
  // coordinate.  |window_size| is the size of the infolist window.
  // |candidate_rect| is the rect of the candidate window.
  // |working_area| is the available area in the current monitor.  If
  // caller fails to obtain |working_area|, set its width or height as
  // 0.  Then it doesn't care the monitor.
  static Rect GetWindowRectForInfolistWindow(const Size& window_size,
                                             const Rect& candidate_rect,
                                             const Rect& working_area);

  // Returns an infolist rectangle that avoids both |candidate_rect| and
  // |avoid_rect| whenever the working area allows it.  The side opposite
  // |avoid_rect| is preferred, so a vertical candidate window placed left of
  // the preedit also puts its infolist farther left.  The other horizontal
  // side and then above/below are used as fallbacks.
  static Rect GetWindowRectForInfolistWindowAvoidingRect(
      const Size& window_size, const Rect& candidate_rect,
      const Rect& avoid_rect, const Rect& working_area);
};
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WINDOW_UTIL_H_
