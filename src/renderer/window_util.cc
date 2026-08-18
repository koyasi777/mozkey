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

#include "renderer/window_util.h"

#include <algorithm>

#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace {
Rect GetWindowRectForMainWindowFromPreeditRectHorizontal(
    const Point& target_point, const Rect& preedit_rect,
    const Size& window_size, const Point& zero_point_offset,
    const Rect& working_area) {
  Rect window_rect(target_point, window_size);
  window_rect.origin.x -= zero_point_offset.x;
  window_rect.origin.y -= zero_point_offset.y;

  // If monitor_rect has erroneous value, it returns window_rect.
  if (working_area.Height() == 0 || working_area.Width() == 0) {
    return window_rect;
  }

  // If the working area below the preedit does not have enough vertical space
  // to display the candidate window, put the candidate window above
  // the preedit.
  if (working_area.Bottom() < window_rect.Bottom()) {
    window_rect.origin.y -= (window_rect.Height() + preedit_rect.Height());
    // We add zero_point_offset.y twice to keep the same distance
    // above the preedit_rect.
    window_rect.origin.y += zero_point_offset.y * 2;
  }

  if (working_area.Bottom() < window_rect.Bottom()) {
    window_rect.origin.y -= (window_rect.Bottom() - working_area.Bottom());
  }

  if (window_rect.Top() < working_area.Top()) {
    window_rect.origin.y += (working_area.Top() - window_rect.Top());
  }

  if (working_area.Right() < window_rect.Right()) {
    window_rect.origin.x -= (window_rect.Right() - working_area.Right());
  }

  if (window_rect.Left() < working_area.Left()) {
    window_rect.origin.x += (working_area.Left() - window_rect.Left());
  }

  return window_rect;
}

Rect GetWindowRectForMainWindowFromPreeditRectVertical(
    const Point& target_point, const Rect& preedit_rect,
    const Size& window_size, const Point& zero_point_offset,
    const Rect& working_area) {
  // Japanese vertical writing proceeds in columns from right to left.  Keep the
  // first candidate column next to the composition by preferring the left side
  // of the preedit.  The vertical zero point is the start of the first
  // candidate text and is aligned to the target segment's top.
  const int aligned_top = target_point.y - zero_point_offset.y;
  Rect preferred_left(preedit_rect.Left() - window_size.width, aligned_top,
                      window_size.width, window_size.height);

  // If the working area is unavailable, preserve the preferred side and the
  // text anchor.  There is no reliable basis for choosing a fallback side.
  if (working_area.Height() == 0 || working_area.Width() == 0) {
    return preferred_left;
  }

  const auto fits_horizontally = [&](const Rect& rect) {
    return working_area.Left() <= rect.Left() &&
           rect.Right() <= working_area.Right();
  };

  Rect window_rect = preferred_left;

  // If the preferred left side does not fit, try the right side of the
  // preedit.  Keep the same vertical text anchor on both sides.
  if (!fits_horizontally(window_rect)) {
    const Rect fallback_right(preedit_rect.Right(), aligned_top,
                              window_size.width, window_size.height);
    if (fits_horizontally(fallback_right)) {
      window_rect = fallback_right;
    }
  }

  // If neither side fits completely, keep the preferred/fallback result but
  // clamp it into the working area as far as the window size allows.
  const int max_left =
      std::max(working_area.Left(), working_area.Right() - window_size.width);
  window_rect.origin.x =
      std::clamp(window_rect.Left(), working_area.Left(), max_left);

  const int max_top =
      std::max(working_area.Top(), working_area.Bottom() - window_size.height);
  window_rect.origin.y =
      std::clamp(window_rect.Top(), working_area.Top(), max_top);

  return window_rect;
}
}  // namespace

Rect WindowUtil::GetWindowRectForMainWindowFromPreeditRect(
    const Rect& preedit_rect, const Size& window_size,
    const Point& zero_point_offset, const Rect& working_area) {
  const Point preedit_bottom_left(preedit_rect.Left(), preedit_rect.Bottom());

  return GetWindowRectForMainWindowFromPreeditRectHorizontal(
      preedit_bottom_left, preedit_rect, window_size, zero_point_offset,
      working_area);
}

Rect WindowUtil::GetWindowRectForMainWindowFromTargetPoint(
    const Point& target_point, const Size& window_size,
    const Point& zero_point_offset, const Rect& working_area) {
  Rect window_rect(target_point, window_size);
  window_rect.origin.x -= zero_point_offset.x;
  window_rect.origin.y -= zero_point_offset.y;

  // If monitor_rect has erroneous value, it returns window_rect.
  if (working_area.Height() == 0 || working_area.Width() == 0) {
    return window_rect;
  }

  if (working_area.Bottom() < window_rect.Bottom()) {
    window_rect.origin.y -= (window_rect.Bottom() - working_area.Bottom());
  }

  if (window_rect.Top() < working_area.Top()) {
    window_rect.origin.y += (working_area.Top() - window_rect.Top());
  }

  if (working_area.Right() < window_rect.Right()) {
    window_rect.origin.x -= (window_rect.Right() - working_area.Right());
  }

  if (window_rect.Left() < working_area.Left()) {
    window_rect.origin.x += (working_area.Left() - window_rect.Left());
  }

  return window_rect;
}

Rect WindowUtil::GetWindowRectForMainWindowFromTargetPointAndPreedit(
    const Point& target_point, const Rect& preedit_rect,
    const Size& window_size, const Point& zero_point_offset,
    const Rect& working_area, bool vertical) {
  if (vertical) {
    return GetWindowRectForMainWindowFromPreeditRectVertical(
        target_point, preedit_rect, window_size, zero_point_offset,
        working_area);
  }

  return GetWindowRectForMainWindowFromPreeditRectHorizontal(
      target_point, preedit_rect, window_size, zero_point_offset, working_area);
}

Rect WindowUtil::GetWindowRectForCascadingWindow(const Rect& selected_row,
                                                 const Size& window_size,
                                                 const Point& zero_point_offset,
                                                 const Rect& working_area) {
  const Point row_top_right(selected_row.Right(), selected_row.Top());

  Rect window_rect(row_top_right, window_size);
  window_rect.origin.x -= zero_point_offset.x;
  window_rect.origin.y -= zero_point_offset.y;

  if (working_area.Height() == 0 || working_area.Width() == 0) {
    return window_rect;
  }

  // If the working area right to the specified candidate window does not have
  // enough horizontal space to display the cascading window,
  // put the cascading window left to the candidate window.
  if (working_area.Right() < window_rect.Right()) {
    window_rect.origin.x -= (window_rect.Width() + selected_row.Width());
    // We add zero_point_offset.x twice to keep the same distance
    // left of the selected_row.
    window_rect.origin.x += zero_point_offset.x * 2;
  }

  if (working_area.Bottom() < window_rect.Bottom()) {
    window_rect.origin.y -= (window_rect.Bottom() - working_area.Bottom());
  }

  if (window_rect.Top() < working_area.Top()) {
    window_rect.origin.y += (working_area.Top() - window_rect.Top());
  }

  if (window_rect.Left() < working_area.Left()) {
    window_rect.origin.x += (working_area.Left() - window_rect.Left());
  }

  return window_rect;
}

bool WindowUtil::GetRubyWindowRect(
    const Rect& preedit_rect, const Size& window_size, int gap,
    const Rect& working_area, const Rect* avoid_rect, Rect* window_rect) {
  if (window_rect == nullptr || window_size.width <= 0 ||
      window_size.height <= 0 || working_area.Width() <= 0 ||
      working_area.Height() <= 0) {
    return false;
  }

  const int normalized_gap = std::max(0, gap);

  int left = preedit_rect.Left();
  if (preedit_rect.Width() > window_size.width) {
    left += (preedit_rect.Width() - window_size.width) / 2;
  }

  const int max_left = std::max(
      working_area.Left(), working_area.Right() - window_size.width);
  left = std::clamp(left, working_area.Left(), max_left);

  const auto intersects = [](const Rect& lhs, const Rect& rhs) {
    return lhs.Left() < rhs.Right() && rhs.Left() < lhs.Right() &&
           lhs.Top() < rhs.Bottom() && rhs.Top() < lhs.Bottom();
  };

  const auto is_usable = [&](const Rect& rect) {
    if (rect.Left() < working_area.Left() ||
        rect.Right() > working_area.Right() ||
        rect.Top() < working_area.Top() ||
        rect.Bottom() > working_area.Bottom()) {
      return false;
    }

    if (avoid_rect != nullptr && intersects(rect, *avoid_rect)) {
      return false;
    }

    return true;
  };

  const int above_top =
      preedit_rect.Top() - window_size.height - normalized_gap;
  const Rect above_rect(
      left, above_top, window_size.width, window_size.height);

  if (is_usable(above_rect)) {
    *window_rect = above_rect;
    return true;
  }

  const int below_top = preedit_rect.Bottom() + normalized_gap;
  const Rect below_rect(
      left, below_top, window_size.width, window_size.height);

  if (is_usable(below_rect)) {
    *window_rect = below_rect;
    return true;
  }

  return false;
}

bool WindowUtil::GetRubyWindowRectForVerticalWriting(
    const Point& composition_right_top, int composition_width,
    const Size& window_size, int text_top_offset, int gap,
    const Rect& working_area, const Rect* avoid_rect, Rect* window_rect) {
  if (window_rect == nullptr || composition_width <= 0 ||
      window_size.width <= 0 || window_size.height <= 0 ||
      working_area.Width() <= 0 || working_area.Height() <= 0 ||
      window_size.width > working_area.Width() ||
      window_size.height > working_area.Height()) {
    return false;
  }

  const int normalized_gap = std::max(0, gap);
  const int normalized_text_top_offset = std::max(0, text_top_offset);
  const int max_top = working_area.Bottom() - window_size.height;
  const int aligned_top =
      composition_right_top.y - normalized_text_top_offset;
  const int top =
      std::clamp(aligned_top, working_area.Top(), max_top);

  const auto intersects = [](const Rect& lhs, const Rect& rhs) {
    return lhs.Left() < rhs.Right() && rhs.Left() < lhs.Right() &&
           lhs.Top() < rhs.Bottom() && rhs.Top() < lhs.Bottom();
  };

  const auto is_usable = [&](const Rect& rect) {
    if (rect.Left() < working_area.Left() ||
        rect.Right() > working_area.Right() ||
        rect.Top() < working_area.Top() ||
        rect.Bottom() > working_area.Bottom()) {
      return false;
    }
    return avoid_rect == nullptr || !intersects(rect, *avoid_rect);
  };

  const Rect right_rect(composition_right_top.x + normalized_gap, top,
                        window_size.width, window_size.height);
  if (is_usable(right_rect)) {
    *window_rect = right_rect;
    return true;
  }

  const Rect left_rect(composition_right_top.x - composition_width -
                           normalized_gap - window_size.width,
                       top, window_size.width, window_size.height);
  if (is_usable(left_rect)) {
    *window_rect = left_rect;
    return true;
  }

  return false;
}

Rect WindowUtil::GetWindowRectForInfolistWindow(const Size& window_size,
                                                const Rect& candidate_rect,
                                                const Rect& working_area) {
  Point infolist_pos;

  if (working_area.Height() == 0 || working_area.Width() == 0) {
    infolist_pos.x = candidate_rect.Left() + candidate_rect.Width();
    infolist_pos.y = candidate_rect.Top();
    return Rect(infolist_pos, window_size);
  }
  if (candidate_rect.Left() + candidate_rect.Width() + window_size.width >
      working_area.Right()) {
    infolist_pos.x = candidate_rect.Left() - window_size.width;
  } else {
    infolist_pos.x = candidate_rect.Left() + candidate_rect.Width();
  }
  if (candidate_rect.Top() + window_size.height > working_area.Bottom()) {
    infolist_pos.y = working_area.Bottom() - window_size.height;
  } else {
    infolist_pos.y = candidate_rect.Top();
  }
  return Rect(infolist_pos, window_size);
}

Rect WindowUtil::GetWindowRectForInfolistWindowAvoidingRect(
    const Size& window_size, const Rect& candidate_rect,
    const Rect& avoid_rect, const Rect& working_area) {
  const int group_left = std::min(candidate_rect.Left(), avoid_rect.Left());
  const int group_right = std::max(candidate_rect.Right(), avoid_rect.Right());
  const int group_top = std::min(candidate_rect.Top(), avoid_rect.Top());
  const int group_bottom =
      std::max(candidate_rect.Bottom(), avoid_rect.Bottom());

  const int candidate_center =
      candidate_rect.Left() + candidate_rect.Width() / 2;
  const int avoid_center = avoid_rect.Left() + avoid_rect.Width() / 2;
  const bool prefer_left = avoid_center >= candidate_center;

  // Without a reliable working area we cannot evaluate fallbacks.  Still keep
  // the infolist on the outside of the candidate/preedit pair.
  if (working_area.Height() == 0 || working_area.Width() == 0) {
    const int x =
        prefer_left ? group_left - window_size.width : group_right;
    return Rect(x, candidate_rect.Top(), window_size.width, window_size.height);
  }

  const int max_top =
      std::max(working_area.Top(), working_area.Bottom() - window_size.height);
  const int side_top =
      std::clamp(candidate_rect.Top(), working_area.Top(), max_top);

  const Rect left_rect(group_left - window_size.width, side_top,
                       window_size.width, window_size.height);
  const Rect right_rect(group_right, side_top, window_size.width,
                        window_size.height);

  const auto fits = [&](const Rect& rect) {
    return working_area.Left() <= rect.Left() &&
           rect.Right() <= working_area.Right() &&
           working_area.Top() <= rect.Top() &&
           rect.Bottom() <= working_area.Bottom();
  };

  const Rect& preferred_side = prefer_left ? left_rect : right_rect;
  const Rect& other_side = prefer_left ? right_rect : left_rect;
  if (fits(preferred_side)) {
    return preferred_side;
  }
  if (fits(other_side)) {
    return other_side;
  }

  // If neither horizontal side is available, try above and below the combined
  // candidate/preedit obstacle. This prevents a large usage window from
  // covering the active vertical composition just because horizontal space is
  // tight.
  const int max_left =
      std::max(working_area.Left(), working_area.Right() - window_size.width);
  const int vertical_left =
      std::clamp(candidate_rect.Left(), working_area.Left(), max_left);

  const Rect above_rect(vertical_left, group_top - window_size.height,
                        window_size.width, window_size.height);
  if (fits(above_rect)) {
    return above_rect;
  }

  const Rect below_rect(vertical_left, group_bottom, window_size.width,
                        window_size.height);
  if (fits(below_rect)) {
    return below_rect;
  }

  // Degenerate case: there is not enough room around the obstacle. Preserve
  // the legacy working-area behavior rather than returning an off-screen rect.
  return GetWindowRectForInfolistWindow(window_size, candidate_rect,
                                        working_area);
}
}  // namespace renderer
}  // namespace mozc
