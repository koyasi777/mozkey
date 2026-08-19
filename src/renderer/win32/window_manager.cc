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

#include "renderer/win32/window_manager.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "absl/log/check.h"
#include "base/coordinates.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/win32/candidate_window.h"
#include "renderer/win32/indicator_window.h"
#include "renderer/win32/infolist_window.h"
#include "renderer/win32/win32_dpi_util.h"
#include "renderer/win32/win32_renderer_util.h"
#include "renderer/window_util.h"

namespace mozc {
namespace renderer {
namespace win32 {

namespace {

constexpr uint32_t kHideWindowDelay = 500;  // msec
constexpr int kDefaultDpi = 96;
constexpr int kVerticalCandidateMinimumHalfClearanceDip = 24;
const POINT kInvalidMousePosition = {-65535, -65535};

RECT ToWinRect(const Rect& rect) {
  return RECT{rect.Left(), rect.Top(), rect.Right(), rect.Bottom()};
}

enum class SideRelationToPreedit {
  kBefore,
  kAfter,
  kOverlapping,
};

SideRelationToPreedit GetSideRelationToPreedit(
    const RECT& rect, const Rect& preedit_rect, bool vertical_writing) {
  if (vertical_writing) {
    if (rect.right <= preedit_rect.Left()) {
      return SideRelationToPreedit::kBefore;
    }
    if (rect.left >= preedit_rect.Right()) {
      return SideRelationToPreedit::kAfter;
    }
    return SideRelationToPreedit::kOverlapping;
  }

  if (rect.bottom <= preedit_rect.Top()) {
    return SideRelationToPreedit::kBefore;
  }
  if (rect.top >= preedit_rect.Bottom()) {
    return SideRelationToPreedit::kAfter;
  }
  return SideRelationToPreedit::kOverlapping;
}

bool IsOppositeSideOfPreedit(const RECT& lhs, const RECT& rhs,
                             const Rect& preedit_rect,
                             bool vertical_writing) {
  const SideRelationToPreedit lhs_relation =
      GetSideRelationToPreedit(lhs, preedit_rect, vertical_writing);
  const SideRelationToPreedit rhs_relation =
      GetSideRelationToPreedit(rhs, preedit_rect, vertical_writing);
  return (lhs_relation == SideRelationToPreedit::kBefore &&
          rhs_relation == SideRelationToPreedit::kAfter) ||
         (lhs_relation == SideRelationToPreedit::kAfter &&
          rhs_relation == SideRelationToPreedit::kBefore);
}

bool HasRenderableCandidateMainText(
    const commands::CandidateWindow& candidate_window) {
  for (int i = 0; i < candidate_window.candidate_size(); ++i) {
    const commands::CandidateWindow::Candidate& candidate =
        candidate_window.candidate(i);

    if (candidate.has_value() && !candidate.value().empty()) {
      return true;
    }

    if (!candidate.has_annotation()) {
      continue;
    }
    const commands::Annotation& annotation = candidate.annotation();
    if ((annotation.has_prefix() && !annotation.prefix().empty()) ||
        (annotation.has_suffix() && !annotation.suffix().empty())) {
      return true;
    }
  }
  return false;
}

}  // namespace

WindowManager::WindowManager()
    : main_window_(std::make_unique<CandidateWindow>()),
      cascading_window_(std::make_unique<CandidateWindow>()),
      indicator_window_(std::make_unique<IndicatorWindow>()),
      infolist_window_(std::make_unique<InfolistWindow>()),
      ruby_window_(std::make_unique<RubyWindow>()),
      layout_manager_(std::make_unique<LayoutManager>()),
      send_command_interface_(nullptr),
      last_position_(kInvalidMousePosition),
      last_live_conversion_passive_suggestion_visible_(false),
      last_live_conversion_passive_suggestion_rect_(),
      has_last_live_conversion_passive_suggestion_rect_(false),
      candidates_finger_print_(0),
      thread_id_(0) {}

WindowManager::~WindowManager() {}

void WindowManager::Initialize() {
  DCHECK(!main_window_->IsWindow());
  DCHECK(!cascading_window_->IsWindow());
  DCHECK(!infolist_window_->IsWindow());

  main_window_->Create(nullptr);
  main_window_->HideWithEffects();
  cascading_window_->Create(nullptr);
  cascading_window_->HideWithEffects();
  indicator_window_->Initialize();
  infolist_window_->Create(nullptr);
  infolist_window_->ShowWindow(SW_HIDE);
  ruby_window_->Initialize();
}

void WindowManager::AsyncHideAllWindows() {
  last_live_conversion_passive_suggestion_visible_ = false;
  has_last_live_conversion_passive_suggestion_rect_ = false;
  cascading_window_->ShowWindowAsync(SW_HIDE);
  main_window_->ShowWindowAsync(SW_HIDE);
  infolist_window_->ShowWindowAsync(SW_HIDE);
  ruby_window_->Hide();
}

void WindowManager::AsyncQuitAllWindows() {
  last_live_conversion_passive_suggestion_visible_ = false;
  has_last_live_conversion_passive_suggestion_rect_ = false;
  cascading_window_->PostMessage(WM_CLOSE, 0, 0);
  main_window_->PostMessage(WM_CLOSE, 0, 0);
  infolist_window_->PostMessage(WM_CLOSE, 0, 0);
  ruby_window_->Destroy();
}

void WindowManager::DestroyAllWindows() {
  last_live_conversion_passive_suggestion_visible_ = false;
  has_last_live_conversion_passive_suggestion_rect_ = false;
  if (main_window_->IsWindow()) {
    main_window_->DestroyWindow();
  }
  if (cascading_window_->IsWindow()) {
    cascading_window_->DestroyWindow();
  }
  indicator_window_->Destroy();
  if (infolist_window_->IsWindow()) {
    infolist_window_->DestroyWindow();
  }
  ruby_window_->Destroy();
}

void WindowManager::HideAllWindows() {
  last_live_conversion_passive_suggestion_visible_ = false;
  has_last_live_conversion_passive_suggestion_rect_ = false;
  main_window_->HideWithEffects();
  cascading_window_->HideWithEffects();
  indicator_window_->Hide();
  infolist_window_->DelayHide(0);
  ruby_window_->Hide();
}

// TODO(yukawa): Refactor this method by making a new method in LayoutManager
//   with unit tests so that LayoutManager can handle both composition windows
//   and candidate windows.
void WindowManager::UpdateLayout(const commands::RendererCommand& command) {
  typedef mozc::commands::RendererCommand::ApplicationInfo ApplicationInfo;

  // Hide all UI elements if |command.visible()| is false.
  if (!command.visible()) {
    last_live_conversion_passive_suggestion_visible_ = false;
    has_last_live_conversion_passive_suggestion_rect_ = false;
    cascading_window_->HideWithEffects();
    main_window_->HideWithEffects();
    indicator_window_->Hide();
    infolist_window_->DelayHide(0);
    ruby_window_->Hide();
    return;
  }

  // We assume |output| exists in the renderer command
  // for all |RendererCommand::UPDATE| renderer messages.
  DCHECK(command.has_output());
  const commands::Output& output = command.output();

  // Live conversion normally uses only the ruby overlay and should keep the
  // ordinary candidate windows hidden.  However, Mozkey may attach a passive
  // SUGGESTION candidate_window to live-conversion output so users can see
  // suggestions while preserving Space/Down as normal conversion operations.
  // In that case, do not return here; let the normal candidate-window path draw
  // the non-focused suggestion window alongside the ruby overlay.
  const bool has_passive_suggestion_window =
      output.has_candidate_window() && output.candidate_window().has_category() &&
      output.candidate_window().category() == commands::SUGGESTION &&
      output.candidate_window().candidate_size() > 0 &&
      HasRenderableCandidateMainText(output.candidate_window()) &&
      !output.candidate_window().has_focused_index();
  const bool is_live_conversion_passive_suggestion =
      output.live_conversion() && has_passive_suggestion_window;
  const bool should_keep_previous_live_conversion_passive_suggestion =
      output.live_conversion() && !is_live_conversion_passive_suggestion &&
      last_live_conversion_passive_suggestion_visible_ &&
      (output.zenz_live_correction_pending() ||
       output.zenz_live_correction_applied() ||
       output.has_zenz_live_correction_debug());

  const bool should_defer_ruby_update =
      is_live_conversion_passive_suggestion;
  const RECT* ruby_avoid_rect = nullptr;
  if (should_keep_previous_live_conversion_passive_suggestion &&
      has_last_live_conversion_passive_suggestion_rect_) {
    ruby_avoid_rect = &last_live_conversion_passive_suggestion_rect_;
  }
  if (!should_defer_ruby_update) {
    ruby_window_->OnUpdate(command, *layout_manager_, ruby_avoid_rect);
  }

  if (output.live_conversion() && !is_live_conversion_passive_suggestion) {
    cascading_window_->HideWithEffects();
    if (!should_keep_previous_live_conversion_passive_suggestion) {
      main_window_->HideWithEffects();
      last_live_conversion_passive_suggestion_visible_ = false;
      has_last_live_conversion_passive_suggestion_rect_ = false;
    }
    indicator_window_->Hide();
    infolist_window_->DelayHide(0);
    return;
  }
  if (!output.live_conversion()) {
    last_live_conversion_passive_suggestion_visible_ = false;
    has_last_live_conversion_passive_suggestion_rect_ = false;
  }

  // We assume |application_info| exists in the renderer command
  // for all |RendererCommand::UPDATE| renderer messages.
  DCHECK(command.has_application_info());

  const commands::RendererCommand::ApplicationInfo& app_info =
      command.application_info();

  (void)app_info.target_window_handle();
  bool show_candidate =
      ((app_info.ui_visibilities() & ApplicationInfo::ShowCandidateWindow) ==
       ApplicationInfo::ShowCandidateWindow);
  bool show_suggest =
      is_live_conversion_passive_suggestion ||
      ((app_info.ui_visibilities() & ApplicationInfo::ShowSuggestWindow) ==
       ApplicationInfo::ShowSuggestWindow);

  CandidateWindowLayout candidate_layout;

  bool is_suggest = false;
  bool is_convert_or_predict = false;
  if (output.has_candidate_window() &&
      output.candidate_window().has_category()) {
    switch (output.candidate_window().category()) {
      case commands::SUGGESTION:
        is_suggest = true;
        break;
      case commands::CONVERSION:
      case commands::PREDICTION:
        is_convert_or_predict = true;
        break;
      default:
        // do nothing.
        break;
    }
  }

  // Currently the indicator will be displayed if and only if no other window
  // (suggestion, prediction, nor conversion) is not displayed.
  if (is_suggest || is_convert_or_predict) {
    indicator_window_->Hide();
  } else if (app_info.has_indicator_info()) {
    indicator_window_->OnUpdate(command, layout_manager_.get());
  }

  if (!output.has_candidate_window()) {
    // Hide candidate windows because there is no candidate to be displayed.
    cascading_window_->HideWithEffects();
    main_window_->HideWithEffects();
    infolist_window_->DelayHide(0);
    return;
  }

  if (is_suggest && !show_suggest) {
    // The candidate list is for suggestion but the visibility bit is off.
    cascading_window_->HideWithEffects();
    main_window_->HideWithEffects();
    infolist_window_->DelayHide(0);
    return;
  }

  if (is_convert_or_predict && !show_candidate) {
    // The candidate list is for conversion or prediction but the visibility
    // bit is off.
    cascading_window_->HideWithEffects();
    main_window_->HideWithEffects();
    infolist_window_->DelayHide(0);
    return;
  }

  const commands::CandidateWindow& candidate_window = output.candidate_window();
  if (candidate_window.candidate_size() == 0) {
    cascading_window_->HideWithEffects();
    main_window_->HideWithEffects();
    infolist_window_->DelayHide(0);
    return;
  }

  if (!candidate_layout.initialized()) {
    candidate_layout.Clear();
    layout_manager_->LayoutCandidateWindow(app_info, &candidate_layout);
  }

  if (!candidate_layout.initialized()) {
    cascading_window_->HideWithEffects();
    main_window_->HideWithEffects();
    infolist_window_->DelayHide(0);
    last_live_conversion_passive_suggestion_visible_ = false;
    has_last_live_conversion_passive_suggestion_rect_ = false;
    if (should_defer_ruby_update) {
      ruby_window_->OnUpdate(command, *layout_manager_);
    }
    return;
  }

  // Currently, we do not use finger print.
  bool candidate_changed = true;

  const Point target_point(candidate_layout.position().x,
                           candidate_layout.position().y);

  // Sync both windows to the DPI of the monitor where the candidate window
  // is about to be placed. This makes their first-frame layout correct when
  // the target app is on a different-DPI monitor than where the windows were
  // last shown - WM_DPICHANGED would otherwise fire only during the
  // subsequent SetWindowPos, after the size has already been computed at
  // the stale DPI.
  const uint32_t target_dpi = GetDpiForPoint(target_point.x, target_point.y);
  main_window_->UpdateDpi(target_dpi);
  infolist_window_->UpdateDpi(target_dpi);

  const bool vertical = (LayoutManager::GetWritingDirection(app_info) ==
                         LayoutManager::VERTICAL_WRITING);

  if (candidate_changed &&
      (candidate_window.display_type() == commands::MAIN)) {
    main_window_->UpdateLayout(
        candidate_window,
        vertical ? CandidateWindow::LayoutMode::kVertical
                 : CandidateWindow::LayoutMode::kHorizontal);
  }
  const Size main_window_size = main_window_->GetLayoutSize();

  // Obtain the monitor's working area
  Rect working_area;
  {
    CRect area;
    if (GetWorkingAreaFromPoint(CPoint(target_point.x, target_point.y),
                                &area)) {
      working_area = Rect(area.left, area.top, area.Width(), area.Height());
    }
  }

  // Horizontal writing aligns the left edge of candidate text with the
  // preedit.  Vertical writing places the window beside the preedit and aligns
  // the top of the first candidate text with the vertical target position.
  const Rect main_candidate_text_rect =
      main_window_->GetCandidateColumnInClientCord();
  const Point main_window_zero_point =
      vertical ? Point(0, main_candidate_text_rect.Top())
               : Point(main_candidate_text_rect.Left(), 0);

  Rect main_window_rect;
  Rect preedit_rect_for_transition;
  {
    // Equating |exclusion_area| with |preedit_rect| generally works well and
    // makes most of users happy.
    const CRect rect(candidate_layout.exclude_region());
    const Rect preedit_rect(rect.left, rect.top, rect.Width(), rect.Height());

    // Keep the host-provided geometry for transition and collision semantics.
    // Candidate-only visual clearance must not enlarge the composition itself.
    preedit_rect_for_transition = preedit_rect;

    Rect placement_preedit_rect = preedit_rect;
    if (vertical) {
      // Some TSF hosts report a narrow vertical line box. Because the vertical
      // candidate is otherwise placed flush against that box, enforce a
      // DPI-scaled minimum half-line clearance around its center while leaving
      // already-wide host geometry unchanged.
      const int minimum_half_clearance =
          ::MulDiv(kVerticalCandidateMinimumHalfClearanceDip,
                   static_cast<int>(target_dpi), kDefaultDpi);
      placement_preedit_rect =
          WindowUtil::GetVerticalCandidatePlacementPreeditRect(
              preedit_rect, minimum_half_clearance);
    }

    // Horizontal candidate placement uses the bottom of the exclusion area.
    // Vertical placement intentionally keeps LayoutManager's top-left target
    // so the first vertical candidate text can align with the segment top.
    Point new_target_point = target_point;
    if (!vertical) {
      new_target_point.y = preedit_rect.Bottom();
    }
    main_window_rect =
        WindowUtil::GetWindowRectForMainWindowFromTargetPointAndPreedit(
            new_target_point, placement_preedit_rect, main_window_size,
            main_window_zero_point, working_area, vertical);
  }

  RECT next_live_conversion_passive_suggestion_rect = {};
  if (should_defer_ruby_update) {
    next_live_conversion_passive_suggestion_rect = ToWinRect(main_window_rect);

    // If the passive suggestion flips across the preedit line, the old
    // suggestion window can still occupy the side where ruby should stay.
    // Hide the old passive suggestion first and keep ruby visible instead of
    // expanding ruby's avoidance area to old+new rectangles.
    if (last_live_conversion_passive_suggestion_visible_ &&
        has_last_live_conversion_passive_suggestion_rect_ &&
        IsOppositeSideOfPreedit(
            last_live_conversion_passive_suggestion_rect_,
            next_live_conversion_passive_suggestion_rect,
            preedit_rect_for_transition, vertical)) {
      main_window_->HideWithEffects();
    }
    ruby_window_->OnUpdate(command, *layout_manager_,
                           &next_live_conversion_passive_suggestion_rect);
  }

  const DWORD set_windows_pos_flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
  main_window_->SetWindowPos(HWND_TOPMOST, main_window_rect.Left(),
                             main_window_rect.Top(), main_window_rect.Width(),
                             main_window_rect.Height(), set_windows_pos_flags);
  // CandidateWindow is a per-pixel-alpha layered window. Present the complete
  // cached surface immediately for every candidate-like UI, not only passive
  // live suggestions, so WM_PAINT scheduling cannot expose an empty/stale
  // first frame.
  main_window_->PresentCachedBitmapImmediately();

  // The main candidate body is the bottom-most body in this renderer family.
  // Secondary windows keep their shadows behind it, so no custom shadow can
  // cover another renderer body while candidate UI is being updated.
  const HWND main_window_handle = main_window_->GetWindowHandle();
  infolist_window_->SetShadowZOrderAnchor(main_window_handle);
  cascading_window_->SetShadowZOrderAnchor(main_window_handle);
  // Ruby can be updated before the main candidate window. Reassert it now so
  // main_window_ is the lowest visible renderer body before any secondary
  // shadow is presented.
  ruby_window_->RaiseToTopmostWithoutActivation();

  if (is_live_conversion_passive_suggestion) {
    last_live_conversion_passive_suggestion_visible_ = true;
    last_live_conversion_passive_suggestion_rect_ =
        next_live_conversion_passive_suggestion_rect;
    has_last_live_conversion_passive_suggestion_rect_ = true;
  }
  // This trick ensures that the window is certainly shown as 'inactivated'
  // in terms of visual effect on DWM-enabled desktop.
  main_window_->SendMessageW(WM_NCACTIVATE, FALSE);

  bool cascading_visible = false;

  if (candidate_window.has_sub_candidate_window() &&
      candidate_window.sub_candidate_window().display_type() ==
          commands::CASCADE) {
    (void)candidate_window.sub_candidate_window();
    cascading_visible = true;
  }

  bool infolist_visible = false;
  if (command.output().has_candidate_window() &&
      command.output().candidate_window().has_usages() &&
      command.output().candidate_window().usages().information_size() > 0) {
    infolist_visible = true;
  }

  if (infolist_visible && !cascading_visible) {
    if (candidate_changed) {
      infolist_window_->UpdateLayout(
          candidate_window,
          vertical ? InfolistWindow::LayoutMode::kVertical
                   : InfolistWindow::LayoutMode::kHorizontal);
    }

    // Horizontal writing keeps the legacy right/left placement contract.
    // Vertical writing treats the active preedit as an obstacle: the candidate
    // window is normally on its left, so the infolist should continue outward
    // instead of opening back across the composition.
    const Size infolist_size = infolist_window_->GetLayoutSize();
    const Rect infolist_rect =
        vertical && !preedit_rect_for_transition.IsRectEmpty()
            ? WindowUtil::GetWindowRectForInfolistWindowAvoidingRect(
                  infolist_size, main_window_rect,
                  preedit_rect_for_transition, working_area)
            : WindowUtil::GetWindowRectForInfolistWindow(
                  infolist_size, main_window_rect, working_area);
    // InfolistWindow is permanently layered. Its cached PBGRA surface is
    // presented explicitly by UpdateEffectWindows(), so moving/resizing must
    // not trigger the legacy WM_PAINT path.
    infolist_window_->MoveWindow(infolist_rect.Left(), infolist_rect.Top(),
                                 infolist_rect.Width(), infolist_rect.Height(),
                                 FALSE);
    infolist_window_->SetWindowPos(
        HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    infolist_window_->UpdateEffectWindows();

    if (candidate_window.has_focused_index() &&
        candidate_window.candidate_size() > 0) {
      const int focused_row = candidate_window.focused_index() -
                              candidate_window.candidate(0).index();
      if (candidate_window.candidate_size() >= focused_row &&
          candidate_window.candidate(focused_row).has_information_id()) {
        const uint32_t delay =
            std::max(static_cast<uint32_t>(0),
                     command.output().candidate_window().usages().delay());
        infolist_window_->DelayShow(delay);
      } else {
        infolist_window_->DelayHide(kHideWindowDelay);
      }
    } else {
      infolist_window_->DelayHide(kHideWindowDelay);
    }
  } else {
    // Hide infolist window immediately.
    infolist_window_->DelayHide(0);
  }

  if (cascading_visible) {
    const commands::CandidateWindow& sub_candidate_window =
        candidate_window.sub_candidate_window();

    if (candidate_changed) {
      cascading_window_->UpdateLayout(sub_candidate_window);
    }

    // Put the cascading window right to the selected row of this candidate
    // window.
    const Rect selected_row = main_window_->GetSelectionRectInScreenCord();
    const Rect selected_row_with_window_border(
        Point(main_window_rect.Left(), selected_row.Top()),
        Size(main_window_rect.Right() - main_window_rect.Left(),
             selected_row.Top() - selected_row.Bottom()));

    // We prefer the top of client area of the cascading window is
    // aligned to the top of selected candidate in the candidate window.
    const Point cascading_window_zero_point(
        0, cascading_window_->GetFirstRowInClientCord().Top());

    const Size cascading_window_size = cascading_window_->GetLayoutSize();

    // cascading window should be in the same working area as the main window.
    const Rect cascading_window_rect =
        WindowUtil::GetWindowRectForCascadingWindow(
            selected_row_with_window_border, cascading_window_size,
            cascading_window_zero_point, working_area);

    cascading_window_->SetWindowPos(
        HWND_TOPMOST, cascading_window_rect.Left(), cascading_window_rect.Top(),
        cascading_window_rect.Width(), cascading_window_rect.Height(),
        set_windows_pos_flags);
    cascading_window_->PresentCachedBitmapImmediately();
    cascading_window_->UpdateEffectWindows();
    // This trick ensures that the window is certainly shown as 'inactivated'
    // in terms of visual effect on DWM-enabled desktop.
    cascading_window_->SendMessageW(WM_NCACTIVATE, FALSE);
  } else {
    // no cascading window
    cascading_window_->HideWithEffects();
  }

  // Present the main shadow only after every secondary body has been positioned.
  // main_window_ is the common Z-order anchor, so every candidate-family
  // shadow stays below Infolist, cascade, and ruby surfaces.
  main_window_->UpdateEffectWindows();
}

bool WindowManager::IsAvailable() const {
  return main_window_->IsWindow() && cascading_window_->IsWindow() &&
         infolist_window_->IsWindow();
}

void WindowManager::SetSendCommandInterface(
    client::SendCommandInterface* send_command_interface) {
  main_window_->SetSendCommandInterface(send_command_interface);
  cascading_window_->SetSendCommandInterface(send_command_interface);
  infolist_window_->SetSendCommandInterface(send_command_interface);
}

void WindowManager::PreTranslateMessage(const MSG& message) {
  if (message.message != WM_MOUSEMOVE) {
    return;
  }

  // Window manager sometimes generates WM_MOUSEMOVE message when the contents
  // under the mouse cursor has been changed (e.g. the window is moved) so that
  // the mouse handler can update its cursor image based on the contents to
  // which the cursor is newly pointing.
  // See http://blogs.msdn.com/b/oldnewthing/archive/2003/10/01/55108.aspx for
  // details about such kind of phantom WM_MOUSEMOVE.  See also b/3104996.
  // Here we compares the screen coordinate of the mouse cursor with the last
  // one to determine if this WM_MOUSEMOVE is an artificial one or not.
  // If the coordinate is the same, this is an artificial WM_MOUSEMOVE.
  bool is_moving = true;
  const CPoint cursor_pos_in_client_coords(GET_X_LPARAM(message.lParam),
                                           GET_Y_LPARAM(message.lParam));
  CPoint cursor_pos_in_logical_coords;
  if (layout_manager_->ClientPointToScreen(message.hwnd,
                                           cursor_pos_in_client_coords,
                                           &cursor_pos_in_logical_coords)) {
    // Since the renderer process is DPI-aware, we can safely use this
    // (logical) coordinates as if it is real (physical) screen coordinates.
    if (cursor_pos_in_logical_coords == last_position_) {
      is_moving = false;
    }
    last_position_ = cursor_pos_in_logical_coords;
  }

  // Notify candidate windows if the cursor is moving or not so that they can
  // filter unnecessary WM_MOUSEMOVE events.
  main_window_->set_mouse_moving(is_moving);
  cascading_window_->set_mouse_moving(is_moving);
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
