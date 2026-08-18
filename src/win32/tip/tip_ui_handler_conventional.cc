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

#include "win32/tip/tip_ui_handler_conventional.h"

#include <msctf.h>
#include <wil/com.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "absl/log/check.h"
#include "base/win32/com.h"
#include "base/win32/wide_char.h"
#include "base/win32/win_util.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/win32/win32_renderer_client.h"
#include "win32/base/input_state.h"
#include "win32/tip/tip_composition_util.h"
#include "win32/tip/tip_dll_module.h"
#include "win32/tip/tip_input_mode_manager.h"
#include "win32/tip/tip_private_context.h"
#include "win32/tip/tip_range_util.h"
#include "win32/tip/tip_text_service.h"
#include "win32/tip/tip_thread_context.h"
#include "win32/tip/tip_ui_element_manager.h"
#include "win32/tip/tip_writing_direction.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

using ::mozc::commands::CompositionMode;
using ::mozc::commands::Preedit;
using ::mozc::renderer::win32::Win32RendererClient;
using Segment = ::mozc::commands::Preedit_Segment;
using Annotation = ::mozc::commands::Preedit_Segment::Annotation;
using IndicatorInfo = ::mozc::commands::RendererCommand_IndicatorInfo;
using RendererCommand = ::mozc::commands::RendererCommand;
using ApplicationInfo = ::mozc::commands::RendererCommand::ApplicationInfo;

bool FillRubyPreeditRectangleFromGuiCaret(HWND target_window,
                                          const RECT& text_rect,
                                          bool vertical_writing,
                                          RendererCommand* command) {
  if (target_window == nullptr || !::IsWindow(target_window) ||
      vertical_writing || command == nullptr) {
    return false;
  }

  const int text_height = text_rect.bottom - text_rect.top;
  if (text_height <= 0) {
    return false;
  }

  DWORD target_process_id = 0;
  const DWORD target_thread_id =
      ::GetWindowThreadProcessId(target_window, &target_process_id);
  if (target_thread_id == 0 ||
      target_process_id != ::GetCurrentProcessId()) {
    return false;
  }

  GUITHREADINFO gui_info = {};
  gui_info.cbSize = sizeof(gui_info);
  if (!::GetGUIThreadInfo(target_thread_id, &gui_info) ||
      gui_info.hwndCaret == nullptr || !::IsWindow(gui_info.hwndCaret)) {
    return false;
  }

  DWORD caret_process_id = 0;
  if (::GetWindowThreadProcessId(gui_info.hwndCaret, &caret_process_id) == 0 ||
      caret_process_id != target_process_id) {
    return false;
  }

  POINT caret_top_left = {gui_info.rcCaret.left, gui_info.rcCaret.top};
  POINT caret_bottom_right = {gui_info.rcCaret.right,
                              gui_info.rcCaret.bottom};
  if (!::ClientToScreen(gui_info.hwndCaret, &caret_top_left) ||
      !::ClientToScreen(gui_info.hwndCaret, &caret_bottom_right)) {
    return false;
  }

  const RECT caret_rect = {caret_top_left.x, caret_top_left.y,
                           caret_bottom_right.x, caret_bottom_right.y};
  const int caret_height = caret_rect.bottom - caret_rect.top;

  // Some frameworks expose only a one- or two-pixel caret stroke instead of
  // the input line box. Such a rectangle must not drive ruby placement.
  constexpr int kMinimumCaretHeight = 8;
  if (caret_height < kMinimumCaretHeight) {
    return false;
  }

  // Accept only a substantial but plausible reduction. This corrects cases
  // such as Notepad's anomalous 49px first-line TSF rectangle with a 25px GUI
  // caret, while preserving the existing geometry for Word, Excel, ordinary
  // editor lines, and thin Qt carets.
  constexpr int kMinimumCaretHeightPercent = 40;
  constexpr int kMaximumCaretHeightPercentForCorrection = 75;
  constexpr int kMinimumHeightReduction = 4;
  if (static_cast<int64_t>(caret_height) * 100 <
          static_cast<int64_t>(text_height) *
              kMinimumCaretHeightPercent ||
      static_cast<int64_t>(caret_height) * 100 >
          static_cast<int64_t>(text_height) *
              kMaximumCaretHeightPercentForCorrection ||
      text_height - caret_height < kMinimumHeightReduction) {
    return false;
  }

  // The target anomaly contains excess space above the actual line while
  // sharing its lower edge. Reject unrelated or stale caret rectangles.
  const int bottom_delta =
      caret_rect.bottom >= text_rect.bottom
          ? caret_rect.bottom - text_rect.bottom
          : text_rect.bottom - caret_rect.bottom;
  const int bottom_tolerance = text_height > 16 ? text_height / 4 : 4;
  if (bottom_delta > bottom_tolerance ||
      caret_rect.top < text_rect.top - bottom_tolerance ||
      caret_rect.top >= text_rect.bottom) {
    return false;
  }

  RendererCommand::Rectangle* preedit_rect =
      command->mutable_preedit_rectangle();
  preedit_rect->set_left(text_rect.left);
  preedit_rect->set_top(caret_rect.top);
  preedit_rect->set_right(text_rect.right);
  preedit_rect->set_bottom(caret_rect.bottom);
  return true;
}

size_t GetTargetPos(const commands::Output& output) {
  if (!output.has_candidate_window() ||
      !output.candidate_window().has_category()) {
    return 0;
  }
  switch (output.candidate_window().category()) {
    case commands::PREDICTION:
    case commands::SUGGESTION:
      return 0;
    case commands::CONVERSION: {
      const Preedit& preedit = output.preedit();
      size_t offset = 0;
      for (int i = 0; i < preedit.segment_size(); ++i) {
        const Segment& segment = preedit.segment(i);
        const Annotation& annotation = segment.annotation();
        if (annotation == Segment::HIGHLIGHT) {
          return offset;
        }
        offset += WideCharsLen(segment.value());
      }
      return offset;
    }
    default:
      return 0;
  }
}

bool FillVisibility(ITfUIElementMgr* ui_element_manager,
                    TipPrivateContext* private_context,
                    RendererCommand* command) {
  command->set_visible(false);

  if (private_context == nullptr) {
    return false;
  }

  const bool show_suggest_window =
      private_context->GetUiElementManager()->IsVisible(
          ui_element_manager, TipUiElementManager::kSuggestWindow);
  const bool show_candidate_window =
      private_context->GetUiElementManager()->IsVisible(
          ui_element_manager, TipUiElementManager::kCandidateWindow);

  const commands::Output& output = private_context->last_output();

  bool suggest_window_visible = false;
  bool candidate_window_visible = false;
  bool ruby_window_visible = false;

  // Live conversion uses the renderer for the ruby overlay even when there is
  // no candidate_window. Pending live conversion intentionally clears
  // candidate_window, so visibility must not depend only on candidate_window.
  if (output.live_conversion() && output.has_preedit()) {
    ruby_window_visible = true;
  }

  // Check if suggest window and candidate window are actually visible.
  if (output.has_candidate_window() && output.candidate_window().has_category()) {
    switch (output.candidate_window().category()) {
      case commands::SUGGESTION:
        suggest_window_visible = show_suggest_window;
        break;
      case commands::CONVERSION:
      case commands::PREDICTION:
        candidate_window_visible = show_candidate_window;
        break;
      default:
        // do nothing.
        break;
    }
  }

  if (candidate_window_visible || suggest_window_visible ||
      ruby_window_visible) {
    command->set_visible(true);
  }

  ApplicationInfo* app_info = command->mutable_application_info();

  int visibility = ApplicationInfo::ShowUIDefault;
  if (show_candidate_window) {
    // Note that |ApplicationInfo::ShowCandidateWindow| represents that the
    // application does not mind the IME showing its own candidate window.
    // This bit does not mean that |command| requires the suggest window.
    visibility |= ApplicationInfo::ShowCandidateWindow;
  }
  if (show_suggest_window) {
    // Note that |ApplicationInfo::ShowCandidateWindow| represents that the
    // application does not mind the IME showing its own candidate window.
    // This bit does not mean that |command| requires the suggest window.
    visibility |= ApplicationInfo::ShowSuggestWindow;
  }
  app_info->set_ui_visibilities(visibility);

  return true;
}

bool FillWindowHandle(ITfContext* context, ApplicationInfo* app_info) {
  wil::com_ptr_nothrow<ITfContextView> context_view;
  if (FAILED(context->GetActiveView(&context_view)) || !context_view) {
    return false;
  }

  HWND window_handle = nullptr;
  if (FAILED(context_view->GetWnd(&window_handle))) {
    return false;
  }
  app_info->set_target_window_handle(
      WinUtil::EncodeWindowHandle(window_handle));
  return true;
}

wil::com_ptr_nothrow<ITfRange> GetCompositionRange(ITfContext* context,
                                                   TfEditCookie read_cookie) {
  wil::com_ptr_nothrow<ITfCompositionView> composition_view =
      TipCompositionUtil::GetCompositionView(context, read_cookie);
  if (!composition_view) {
    return nullptr;
  }

  wil::com_ptr_nothrow<ITfRange> composition_range;
  if (FAILED(composition_view->GetRange(&composition_range))) {
    return nullptr;
  }
  return composition_range;
}

wil::com_ptr_nothrow<ITfRange> GetSelectionRange(ITfContext* context,
                                                 TfEditCookie read_cookie) {
  wil::com_ptr_nothrow<ITfRange> selection_range;
  TfActiveSelEnd sel_end = TF_AE_NONE;
  if (FAILED(TipRangeUtil::GetDefaultSelection(context, read_cookie,
                                               &selection_range, &sel_end))) {
    return nullptr;
  }
  return selection_range;
}

WritingDirection ProbeExpandedRangeDirection(
    ITfContextView* context_view, TfEditCookie read_cookie,
    ITfRange* composition_range, ITfRange* target_range,
    const RECT& target_rect, bool forward) {
  if (context_view == nullptr || composition_range == nullptr ||
      target_range == nullptr) {
    return WritingDirection::kUnknown;
  }

  wil::com_ptr_nothrow<ITfRange> probe_range;
  if (FAILED(target_range->Clone(&probe_range)) || !probe_range) {
    return WritingDirection::kUnknown;
  }

  LONG shifted = 0;
  const HRESULT shift_result =
      forward ? probe_range->ShiftEnd(read_cookie, 1, &shifted, nullptr)
              : probe_range->ShiftStart(read_cookie, -1, &shifted, nullptr);
  const LONG expected_shift = forward ? 1 : -1;
  if (FAILED(shift_result) || shifted != expected_shift) {
    return WritingDirection::kUnknown;
  }

  // Never cross the Mozc composition boundary merely to infer geometry.
  if (!TipRangeUtil::IsRangeCovered(read_cookie, probe_range.get(),
                                    composition_range)) {
    return WritingDirection::kUnknown;
  }

  RECT expanded_rect = {};
  bool expanded_clipped = false;
  if (FAILED(TipRangeUtil::GetTextExt(context_view, read_cookie,
                                      probe_range.get(), &expanded_rect,
                                      &expanded_clipped)) ||
      expanded_clipped) {
    return WritingDirection::kUnknown;
  }

  return InferWritingDirectionFromExtentGrowth(
      target_rect.right - target_rect.left,
      target_rect.bottom - target_rect.top,
      expanded_rect.right - expanded_rect.left,
      expanded_rect.bottom - expanded_rect.top);
}

WritingDirection InferCompositionWritingDirectionFromGeometry(
    ITfContextView* context_view, TfEditCookie read_cookie,
    ITfRange* composition_range, ITfRange* target_range,
    const RECT& target_rect) {
  WritingDirection direction = ProbeExpandedRangeDirection(
      context_view, read_cookie, composition_range, target_range, target_rect,
      /*forward=*/true);
  if (direction != WritingDirection::kUnknown) {
    return direction;
  }
  return ProbeExpandedRangeDirection(
      context_view, read_cookie, composition_range, target_range, target_rect,
      /*forward=*/false);
}

// This function updates RendererCommand::CharacterPosition to emulate
// IMM32-based client. Ideally we'd better to define new field for TSF Mozc
// into which the result of ITfContextView::GetTextExt is stored.
// TODO(yukawa): Replace FillCharPosition with new one designed for TSF.
bool FillCharPosition(TipPrivateContext* private_context, ITfContext* context,
                      TfEditCookie read_cookie, bool has_composition,
                      RendererCommand* command, bool* no_layout) {
  if (private_context == nullptr || command == nullptr) {
    return false;
  }
  ApplicationInfo* app_info = command->mutable_application_info();
  bool dummy_no_layout = false;
  if (no_layout == nullptr) {
    no_layout = &dummy_no_layout;
  }
  *no_layout = false;

  if (!app_info->has_target_window_handle()) {
    return false;
  }

  const HWND target_window =
      WinUtil::DecodeWindowHandle(app_info->target_window_handle());

  wil::com_ptr_nothrow<ITfRange> range =
      has_composition ? GetCompositionRange(context, read_cookie)
                      : GetSelectionRange(context, read_cookie);
  if (!range) {
    return false;
  }
  wil::com_ptr_nothrow<ITfRange> target_range;
  if (FAILED(range->Clone(&target_range))) {
    return false;
  }
  if (!target_range) {
    return false;
  }

  const commands::Output& output = private_context->last_output();
  LONG shifted = 0;
  if (FAILED(target_range->Collapse(read_cookie, TF_ANCHOR_START))) {
    return false;
  }
  const size_t target_pos = GetTargetPos(output);
  if (FAILED(target_range->ShiftStart(read_cookie, target_pos, &shifted,
                                      nullptr))) {
    return false;
  }
  if (FAILED(target_range->ShiftEnd(read_cookie, target_pos + 1, &shifted,
                                    nullptr))) {
    return false;
  }

  wil::com_ptr_nothrow<ITfContextView> context_view;
  if (FAILED(context->GetActiveView(&context_view)) || !context_view) {
    return false;
  }

  RECT document_rect = {};
  if (FAILED(context_view->GetScreenExt(&document_rect))) {
    return false;
  }

  RECT text_rect = {};
  bool clipped = false;
  const HRESULT hr =
      TipRangeUtil::GetTextExt(context_view.get(), read_cookie,
                               target_range.get(), &text_rect, &clipped);
  if (hr == TF_E_NOLAYOUT) {
    // This is not a critical error but the layout information is not available.
    *no_layout = true;
    return true;
  }
  if (FAILED(hr)) {
    // Any other errors are unexpected.
    return false;
  }

  RendererCommand::CharacterPosition* composition_target =
      app_info->mutable_composition_target();
  composition_target->set_position(0);

  const WritingDirection snapshot_direction =
      has_composition ? private_context->composition_writing_direction()
                      : WritingDirection::kUnknown;

  WritingDirection attribute_direction = WritingDirection::kUnknown;
  TipRangeUtil::GetWritingDirection(target_range.get(), read_cookie,
                                    &attribute_direction);

  WritingDirection writing_direction = WritingDirection::kUnknown;
  if (snapshot_direction == WritingDirection::kVertical) {
    // Preserve a positive pre-composition vertical signal. Some TSF hosts
    // replace both VerticalWriting and Orientation with horizontal values on
    // the range created by StartComposition.
    writing_direction = WritingDirection::kVertical;
  } else if (attribute_direction == WritingDirection::kVertical) {
    // Never let a horizontal pre-composition snapshot suppress a current,
    // explicit vertical signal.
    writing_direction = WritingDirection::kVertical;
  } else if (snapshot_direction == WritingDirection::kHorizontal) {
    writing_direction = WritingDirection::kHorizontal;
  } else if (has_composition && !clipped) {
    // No reliable pre-composition snapshot exists. Strong adjacent-range
    // growth may then override a composition-local horizontal default. This
    // deliberately compares growth direction rather than a glyph's aspect
    // ratio.
    const WritingDirection geometry_direction =
        InferCompositionWritingDirectionFromGeometry(
            context_view.get(), read_cookie, range.get(), target_range.get(),
            text_rect);
    writing_direction =
        geometry_direction != WritingDirection::kUnknown
            ? geometry_direction
            : attribute_direction;
  } else {
    writing_direction = attribute_direction;
  }

  const bool vertical_writing =
      writing_direction == WritingDirection::kVertical;
  if (writing_direction != WritingDirection::kUnknown) {
    composition_target->set_vertical_writing(vertical_writing);
  }

  RendererCommand::Point* point = composition_target->mutable_top_left();
  if (vertical_writing) {
    // [Vertical Writing]
    //    |
    //    +-----< (pt)
    //    |     |
    //    |-----+
    //    | (cLineHeight)
    //    |
    //    |
    //    v
    //   (Base Line)
    point->set_x(text_rect.right);
    point->set_y(text_rect.top);
    composition_target->set_line_height(text_rect.right - text_rect.left);
  } else {
    // [Horizontal Writing]
    //    (pt)
    //     v_____
    //     |     |
    //     |     | (cLineHeight)
    //     |     |
    //   --+-----+---------->  (Base Line)
    point->set_x(text_rect.left);
    point->set_y(text_rect.top);
    composition_target->set_line_height(text_rect.bottom - text_rect.top);
  }

  RendererCommand::Rectangle* area =
      composition_target->mutable_document_area();
  area->set_left(document_rect.left);
  area->set_top(document_rect.top);
  area->set_right(document_rect.right);
  area->set_bottom(document_rect.bottom);

  if (output.live_conversion() && output.has_preedit()) {
    FillRubyPreeditRectangleFromGuiCaret(target_window, text_rect,
                                         vertical_writing, command);
  }

  return true;
}

void UpdateCommand(TipTextService* text_service, ITfContext* context,
                   TfEditCookie read_cookie, RendererCommand* command,
                   bool* no_layout) {
  command->Clear();
  command->set_type(RendererCommand::UPDATE);

  TipPrivateContext* private_context = text_service->GetPrivateContext(context);
  if (private_context != nullptr) {
    *command->mutable_output() = private_context->last_output();
    private_context->GetUiElementManager()->OnUpdate(text_service, context);
  }

  ApplicationInfo* app_info = command->mutable_application_info();
  app_info->set_input_framework(ApplicationInfo::TSF);
  app_info->set_process_id(::GetCurrentProcessId());
  app_info->set_thread_id(::GetCurrentThreadId());
  app_info->set_receiver_handle(WinUtil::EncodeWindowHandle(
      text_service->renderer_callback_window_handle()));

  auto ui_element_manager =
      ComQuery<ITfUIElementMgr>(text_service->GetThreadManager());
  DCHECK(ui_element_manager);
  FillVisibility(ui_element_manager.get(), private_context, command);
  FillWindowHandle(context, app_info);
  FillCharPosition(private_context, context, read_cookie,
                   command->output().has_preedit(), command, no_layout);

  if (private_context != nullptr) {
    const TipInputModeManager* input_mode_manager =
        text_service->GetThreadContext()->GetInputModeManager();
    if (private_context->input_behavior().use_mode_indicator &&
        input_mode_manager->IsIndicatorVisible()) {
      command->set_visible(true);
      IndicatorInfo* info = app_info->mutable_indicator_info();
      info->mutable_status()->set_activated(
          input_mode_manager->GetEffectiveOpenClose());
      info->mutable_status()->set_mode(static_cast<CompositionMode>(
          input_mode_manager->GetEffectiveConversionMode()));
    }
  }

  // Regardless of the value of |command->visible()| here, we should hide
  // all the UI elements whenever the current threads is not focused.
  BOOL thread_focus = FALSE;
  const HRESULT hr =
      text_service->GetThreadManager()->IsThreadFocus(&thread_focus);
  if (SUCCEEDED(hr) && (thread_focus == FALSE)) {
    command->set_visible(false);
  }
}

// This class is an implementation class for the ITfEditSession classes, which
// is an observer for exclusively read the date from the text store.
class UpdateUiEditSessionImpl final : public TipComImplements<ITfEditSession> {
 public:
  // The ITfEditSession interface method.
  // This function is called back by the TSF thread manager when an edit
  // request is granted.
  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    RendererCommand command;
    bool no_layout = false;
    UpdateCommand(text_service_.get(), context_.get(), edit_cookie, &command,
                  &no_layout);
    if (!no_layout || !command.visible()) {
      Win32RendererClient::OnUpdated(command);
    }
    return S_OK;
  }

  static bool BeginRequest(TipTextService* text_service, ITfContext* context) {
    // When RequestEditSession fails, it does not maintain the reference count.
    // So we need to ensure that AddRef/Release should be called at least once
    // per object.
    wil::com_ptr_nothrow<ITfEditSession> edit_session(
        new UpdateUiEditSessionImpl(text_service, context));

    HRESULT edit_session_result = S_OK;
    const HRESULT result = context->RequestEditSession(
        text_service->GetClientID(), edit_session.get(),
        TF_ES_ASYNCDONTCARE | TF_ES_READ, &edit_session_result);
    return SUCCEEDED(result);
  }

 private:
  UpdateUiEditSessionImpl(wil::com_ptr_nothrow<TipTextService> text_service,
                          wil::com_ptr_nothrow<ITfContext> context)
      : text_service_(std::move(text_service)), context_(std::move(context)) {}

  wil::com_ptr_nothrow<TipTextService> text_service_;
  wil::com_ptr_nothrow<ITfContext> context_;
};

}  // namespace

void TipUiHandlerConventional::OnActivate(TipTextService* text_service) {
  ITfThreadMgr* thread_mgr = text_service->GetThreadManager();
  wil::com_ptr_nothrow<ITfDocumentMgr> document;
  if (FAILED(thread_mgr->GetFocus(&document))) {
    return;
  }
  OnFocusChange(text_service, document.get());
}

void TipUiHandlerConventional::OnDeactivate() {
  Win32RendererClient::OnUIThreadUninitialized();
}

void TipUiHandlerConventional::OnFocusChange(
    TipTextService* text_service, ITfDocumentMgr* focused_document_manager) {
  if (!focused_document_manager) {
    // Empty document. Hide the renderer.
    RendererCommand command;
    command.set_type(RendererCommand::UPDATE);
    command.set_visible(false);
    Win32RendererClient::OnUpdated(command);
    return;
  }

  wil::com_ptr_nothrow<ITfContext> context;
  if (FAILED(focused_document_manager->GetBase(&context))) {
    return;
  }
  if (!context) {
    return;
  }
  UpdateUiEditSessionImpl::BeginRequest(text_service, context.get());
}

bool TipUiHandlerConventional::Update(TipTextService* text_service,
                                      ITfContext* context,
                                      TfEditCookie read_cookie) {
  RendererCommand command;
  bool no_layout = false;
  UpdateCommand(text_service, context, read_cookie, &command, &no_layout);
  if (!no_layout || !command.visible()) {
    Win32RendererClient::OnUpdated(command);
  }
  return true;
}

bool TipUiHandlerConventional::OnDllProcessAttach(HINSTANCE module_handle,
                                                  bool static_loading) {
  Win32RendererClient::OnModuleLoaded(module_handle);
  return true;
}

void TipUiHandlerConventional::OnDllProcessDetach(HINSTANCE module_handle,
                                                  bool process_shutdown) {
  Win32RendererClient::OnModuleUnloaded();
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
