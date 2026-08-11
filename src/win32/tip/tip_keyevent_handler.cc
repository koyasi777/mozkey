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

#include "win32/tip/tip_keyevent_handler.h"

#include <msctf.h>
#include <wil/com.h>
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "base/win32/wide_char.h"
#include "client/client_interface.h"
#include "protocol/commands.pb.h"
#include "session/key_info_util.h"
#include "win32/base/conversion_mode_util.h"
#include "win32/base/deleter.h"
#include "win32/base/input_state.h"
#include "win32/base/keyboard.h"
#include "win32/base/keyevent_handler.h"
#include "win32/base/surrogate_pair_observer.h"
#include "win32/tip/tip_edit_session.h"
#include "win32/tip/tip_input_mode_manager.h"
#include "win32/tip/tip_private_context.h"
#include "win32/tip/tip_status.h"
#include "win32/tip/tip_surrounding_text.h"
#include "win32/tip/tip_zenz_context_request.h"
#include "win32/tip/tip_text_service.h"
#include "win32/tip/tip_thread_context.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

using ::mozc::commands::CompositionMode;
using ::mozc::commands::Context;
using ::mozc::commands::SessionCommand;

// Defined in the following white paper.
// http://msdn.microsoft.com/en-us/library/windows/apps/hh967425.aspx
constexpr UINT kTouchKeyboardNextPage = 0xf003;
constexpr UINT kTouchKeyboardPreviousPage = 0xf004;

// Unlike IMM32 Mozc which is marked as IME_PROP_ACCEPT_WIDE_VKEY,
// TSF Mozc cannot always receive a VK_PACKET keyevent whose high word consists
// of a Unicode character. To retrieve the underlaying Unicode character,
// use ToUnicode API as documented in the following white paper.
// http://msdn.microsoft.com/en-us/library/windows/apps/hh967425.aspx
VirtualKey GetVK(WPARAM wparam, const KeyboardStatus& keyboad_status) {
  if (LOWORD(wparam) != VK_PACKET) {
    return VirtualKey::FromVirtualKey(wparam);
  }

  const UINT scan_code = ::MapVirtualKey(wparam, MAPVK_VK_TO_VSC);
  wchar_t buffer[4] = {};
  if (::ToUnicode(wparam, scan_code, keyboad_status.status(), buffer,
                  std::size(buffer), 0) != 1) {
    return VirtualKey::FromVirtualKey(wparam);
  }
  const uint32_t ucs2 = buffer[0];
  if (ucs2 == L' ') {
    return VirtualKey::FromVirtualKey(VK_SPACE);
  }
  if (L'0' <= ucs2 && ucs2 <= L'9') {
    return VirtualKey::FromVirtualKey(ucs2);
  }
  if (L'a' <= ucs2 && ucs2 <= L'z') {
    return VirtualKey::FromVirtualKey(L'z' - ucs2 + L'A');
  }
  if (L'A' <= ucs2 && ucs2 <= L'Z') {
    return VirtualKey::FromVirtualKey(ucs2);
  }

  // Emulate IME_PROP_ACCEPT_WIDE_VKEY.
  return VirtualKey::FromCombinedVirtualKey(ucs2 << 16 | VK_PACKET);
}

void ShowModeIndicatorAndUpdateUI(TipTextService* text_service,
                                  TipInputModeManager* input_mode_manager) {
  const TipInputModeManager::Action show_action =
      input_mode_manager->ShowModeIndicator();
  if (show_action == TipInputModeManager::Action::kUpdateUI) {
    text_service->PostUIUpdateMessage();
  }
}

bool IsNoOpModeIndicatorKey(const InputBehavior& behavior,
                            const InputState& current_state,
                            const KeyEventHandlerResult& result) {
  if (!result.has_key_information) {
    return false;
  }

  if (current_state.open) {
    return KeyInfoUtil::ContainsKeyInformation(
        behavior.active_mode_ime_on_keys, result.key_information);
  }

  return KeyInfoUtil::ContainsKeyInformation(
      behavior.direct_mode_ime_off_keys, result.key_information);
}

bool UpdateNoOpModeIndicatorKeyState(
    TipTextService* text_service, TipPrivateContext* private_context,
    TipInputModeManager* input_mode_manager, bool is_key_down,
    const InputBehavior& behavior, const InputState& current_state,
    const KeyEventHandlerResult& result, bool is_on_key) {
  if (!result.has_key_information) {
    if (is_key_down) {
      private_context->ClearPendingModeIndicatorKey();
    }
    return false;
  }

  if (is_key_down) {
    if (IsNoOpModeIndicatorKey(behavior, current_state, result)) {
      private_context->SetPendingModeIndicatorKey(result.key_information);
    } else {
      private_context->ClearPendingModeIndicatorKey();
    }
    return false;
  }

  if (!private_context->IsPendingModeIndicatorKey(result.key_information)) {
    return false;
  }

  if (!is_on_key) {
    ShowModeIndicatorAndUpdateUI(text_service, input_mode_manager);
    private_context->MarkPendingModeIndicatorShownOnTestKey();
    return true;
  }

  if (!private_context->IsPendingModeIndicatorShownOnTestKey()) {
    ShowModeIndicatorAndUpdateUI(text_service, input_mode_manager);
  }

  private_context->ClearPendingModeIndicatorKey();
  return true;
}

bool GetOpenAndMode(TipTextService* text_service, ITfContext* context,
                    bool* open, uint32_t* logical_mode,
                    uint32_t* visible_mode) {
  DCHECK(text_service);
  DCHECK(context);
  DCHECK(open);
  DCHECK(logical_mode);
  DCHECK(visible_mode);
  const TipInputModeManager* input_mode_manager =
      text_service->GetThreadContext()->GetInputModeManager();
  const bool is_open = input_mode_manager->GetEffectiveOpenClose();
  *open = (!TipStatus::IsDisabledContext(context) && is_open);

  bool prefer_kana_input = false;
  TipPrivateContext* private_context = text_service->GetPrivateContext(context);
  if (private_context) {
    prefer_kana_input = private_context->input_behavior().prefer_kana_input;
  }
  const CompositionMode tsf_mode =
      static_cast<CompositionMode>(input_mode_manager->GetTsfConversionMode());
  const CompositionMode effective_mode = static_cast<CompositionMode>(
      input_mode_manager->GetEffectiveConversionMode());

  const bool has_valid_logical_mode = ConversionModeUtil::ToNativeMode(
      tsf_mode, prefer_kana_input, logical_mode);
  const bool has_valid_visible_mode = ConversionModeUtil::ToNativeMode(
      effective_mode, prefer_kana_input, visible_mode);
  return has_valid_logical_mode && has_valid_visible_mode;
}

void FillMozcContextCommon(TipTextService* text_service, ITfContext* context,
                           Context* mozc_context) {
  if (mozc_context == nullptr) {
    return;
  }
  mozc_context->set_revision(
      text_service->GetThreadContext()->GetFocusRevision());
  wil::com_ptr_nothrow<ITfContextView> context_view;
  if (FAILED(context->GetActiveView(&context_view))) {
    return;
  }
  if (context_view == nullptr) {
    return;
  }
  HWND attached_window = nullptr;
  if (FAILED(context_view->GetWnd(&attached_window))) {
    return;
  }
}

HRESULT OnTestKey(TipTextService* text_service, ITfContext* context,
                  bool is_key_down, WPARAM wparam, LPARAM lparam, BOOL* eaten) {
  DCHECK(text_service);
  DCHECK(eaten);
  TipPrivateContext* private_context = text_service->GetPrivateContext(context);
  if (private_context == nullptr) {
    *eaten = FALSE;
    return S_OK;
  }

  // A TestKey path can return before reaching the server. Reset first so a
  // request from an older physical key can never leak into this key's OnKey.
  private_context->mutable_zenz_context_request_state()->Reset();

  BYTE key_state[256] = {};
  if (!::GetKeyboardState(key_state)) {
    *eaten = FALSE;
    return S_OK;
  }

  bool open = false;
  uint32_t logical_mode = 0;
  uint32_t visible_mode = 0;
  if (!GetOpenAndMode(text_service, context, &open, &logical_mode,
                      &visible_mode)) {
    *eaten = FALSE;
    return S_OK;
  }

  const KeyboardStatus keyboard_status(key_state);
  const LParamKeyInfo key_info(lparam);
  VirtualKey vk = GetVK(wparam, keyboard_status);

  if (open) {
    // Check if this key event is handled by VKBackBasedDeleter to support
    // *deletion_range* rule.
    const ClientAction vk_back_action =
        private_context->GetDeleter()->OnKeyEvent(
            vk.virtual_key(), key_info.IsKeyDownInImeProcessKey(), true);
    switch (vk_back_action) {
      case ClientAction::DO_DEFAULT_ACTION:
        // do nothing.
        break;
      case ClientAction::CALL_END_DELETION_THEN_DO_DEFAULT_ACTION:
        private_context->GetDeleter()->EndDeletion();
        break;
      case ClientAction::SEND_KEY_TO_APPLICATION:
        *eaten = FALSE;  // Do not consume this key.
        return S_OK;
      case ClientAction::CONSUME_KEY_BUT_NEVER_SEND_TO_SERVER:
        *eaten = TRUE;  // Consume this key but do not send this key to server.
        return S_OK;
      case ClientAction::CALL_END_DELETION_BUT_NEVER_SEND_TO_SERVER:
      case ClientAction::APPLY_PENDING_STATUS:
      default:
        DLOG(FATAL) << "this action is not applicable to OnTestKey.";
        *eaten = FALSE;
        return E_UNEXPECTED;
    }

    const SurrogatePairObserver::ClientAction surrogate_action =
        private_context->GetSurrogatePairObserver()->OnTestKeyEvent(
            vk, is_key_down);
    switch (surrogate_action.type) {
      case SurrogatePairObserver::DO_DEFAULT_ACTION:
        break;
      case SurrogatePairObserver::DO_DEFAULT_ACTION_WITH_RETURNED_UCS4:
        vk = VirtualKey::FromUnicode(surrogate_action.codepoint);
        break;
      case SurrogatePairObserver::CONSUME_KEY_BUT_NEVER_SEND_TO_SERVER:
        *eaten = TRUE;
        return S_OK;  // Consume this key but do not send this key to server.
      default:
        DLOG(FATAL) << "this action is not applicable to OnTestKey.";
        break;
    }

    // Handle NextPage/PrevPage button on the on-screen keyboard.
    if (key_info.IsKeyDownInImeProcessKey() &&
        ((vk.wide_char() == kTouchKeyboardNextPage) ||
         (vk.wide_char() == kTouchKeyboardPreviousPage))) {
      *eaten = TRUE;
      return S_OK;
    }
  }

  // Make an immutable snapshot of |private_context->ime_behavior_|, which
  // cannot be substituted for by const reference.
  InputBehavior behavior = private_context->input_behavior();
  Context mozc_context;
  FillMozcContextCommon(text_service, context, &mozc_context);

  // Update On/Off mode and conversion mode.
  InputState input_state;
  input_state.last_down_key = private_context->last_down_key();
  input_state.logical_conversion_mode = logical_mode;
  input_state.visible_conversion_mode = visible_mode;
  input_state.open = open;

  InputState next_state;
  commands::Output temporal_output;
  std::unique_ptr<Win32KeyboardInterface> keyboard(
      Win32KeyboardInterface::CreateDefault());

  const KeyEventHandlerResult result = KeyEventHandler::ImeProcessKey(
      vk, key_info.GetScanCodeForMapVirtualKey(), is_key_down, keyboard_status,
      behavior, input_state, mozc_context, private_context->GetClient(),
      keyboard.get(), &next_state, &temporal_output);
  if (!result.succeeded) {
    *eaten = FALSE;
    return S_OK;
  }

  // Missing request fields mean 0/0, so this also keeps non-server and
  // old-server behavior safely disabled after the reset above.
  private_context->mutable_zenz_context_request_state()->UpdateFromOutput(
      temporal_output);

  *private_context->mutable_last_down_key() = next_state.last_down_key;

  if (result.should_be_sent_to_server && temporal_output.has_consumed()) {
    *private_context->mutable_last_output() = temporal_output;
  }

  TipInputModeManager* input_mode_manager =
      text_service->GetThreadContext()->GetInputModeManager();
  const TipInputModeManager::Action action =
      input_mode_manager->OnTestKey(vk, is_key_down, result.should_be_eaten);
  if (action == TipInputModeManager::kUpdateUI) {
    text_service->PostUIUpdateMessage();
  }

  if (UpdateNoOpModeIndicatorKeyState(
          text_service, private_context, input_mode_manager, is_key_down,
          behavior, input_state, result,
          /*is_on_key=*/false)) {
    *eaten = TRUE;
    return S_OK;
  }

  *eaten = result.should_be_eaten ? TRUE : FALSE;
  return S_OK;
}

void FillMozcContextForOnKey(
    TipTextService* text_service, ITfContext* context,
    const TipSurroundingTextInfo* generic_info,
    const TipZenzContextRequest& zenz_context_request, Context* mozc_context) {
  FillMozcContextCommon(text_service, context, mozc_context);

  // The legacy generic surrounding-text read is performed by OnKey before the
  // fallback TestSendKey decision. Reuse that exact result here so moving the
  // read earlier does not add another synchronous TSF edit session.
  if (generic_info == nullptr) {
    return;
  }

  // Password scope is detected inside the same TSF read edit session before
  // any surrounding text is retrieved. Propagate the field type to the server
  // as a second privacy layer and never attach generic or extended text.
  if (generic_info->is_password_input_scope) {
    mozc_context->set_input_field_type(Context::PASSWORD);
    return;
  }

  std::string generic_preceding;
  if (generic_info->has_preceding_text) {
    generic_preceding = WideToUtf8(generic_info->preceding_text);
    mozc_context->set_preceding_text(generic_preceding);
  }

  std::string generic_following;
  if (generic_info->has_following_text) {
    generic_following = WideToUtf8(generic_info->following_text);
    mozc_context->set_following_text(generic_following);
  }

  // The IMM32 document-feed fallback has no TSF InputScope signal. Preserve
  // its generic Mozc context for compatibility, but explicitly prevent Zenz
  // from falling back to those generic fields.
  if (generic_info->used_legacy_imm32_fallback) {
    SetZenzContextUnavailable(mozc_context);
    return;
  }

  if (zenz_context_request.empty()) {
    return;
  }

  const bool needs_extended_acquisition =
      (zenz_context_request.preceding_length > 0 &&
       (!generic_info->has_preceding_text ||
        !HasAtLeastZenzContextCharacters(
            generic_preceding, zenz_context_request.preceding_length))) ||
      (zenz_context_request.following_length > 0 &&
       (!generic_info->has_following_text ||
        !HasAtLeastZenzContextCharacters(
            generic_following, zenz_context_request.following_length)));

  TipSurroundingTextInfo extended_info;
  const bool has_extended_info =
      needs_extended_acquisition &&
      TipSurroundingText::GetWithMaxSurroundingLength(
          text_service, context,
          GetZenzTsfNativeAcquisitionLength(zenz_context_request),
          &extended_info);
  const bool has_trusted_extended_info =
      has_extended_info && !extended_info.used_legacy_imm32_fallback;

  if (zenz_context_request.preceding_length > 0) {
    const bool use_extended_preceding =
        has_trusted_extended_info && extended_info.has_preceding_text;
    if (use_extended_preceding || generic_info->has_preceding_text) {
      const std::string source =
          use_extended_preceding
              ? WideToUtf8(extended_info.preceding_text)
              : generic_preceding;
      mozc_context->set_zenz_preceding_text(
          TakeTrailingZenzContextCharacters(
              source, zenz_context_request.preceding_length));
    }
  }

  if (zenz_context_request.following_length > 0) {
    const bool use_extended_following =
        has_trusted_extended_info && extended_info.has_following_text;
    if (use_extended_following || generic_info->has_following_text) {
      const std::string source =
          use_extended_following
              ? WideToUtf8(extended_info.following_text)
              : generic_following;
      mozc_context->set_zenz_following_text(
          TakeLeadingZenzContextCharacters(
              source, zenz_context_request.following_length));
    }
  }
}

HRESULT OnKey(TipTextService* text_service, ITfContext* context,
              bool is_key_down, WPARAM wparam, LPARAM lparam, BOOL* eaten) {
  DCHECK(text_service);
  DCHECK(eaten);
  TipPrivateContext* private_context = text_service->GetPrivateContext(context);
  if (private_context == nullptr) {
    *eaten = FALSE;
    return S_OK;
  }

  // Consume the TestKey result immediately. Any subsequent early return must
  // not leave it available for a later, unrelated key event. Presence is
  // separate from the request values because an authoritative server result
  // can legitimately request 0/0.
  TipZenzContextRequestState* zenz_context_request_state =
      private_context->mutable_zenz_context_request_state();
  const bool has_test_key_result = zenz_context_request_state->has_result();
  TipZenzContextRequest zenz_context_request =
      zenz_context_request_state->Take();

  BYTE key_state[256] = {};
  if (!::GetKeyboardState(key_state)) {
    *eaten = FALSE;
    return S_OK;
  }

  bool open = false;
  uint32_t logical_mode = 0;
  uint32_t visible_mode = 0;
  if (!GetOpenAndMode(text_service, context, &open, &logical_mode,
                      &visible_mode)) {
    *eaten = FALSE;
    return S_OK;
  }

  const LParamKeyInfo key_info(lparam);
  const KeyboardStatus keyboard_status(key_state);
  VirtualKey vk = GetVK(wparam, keyboard_status);

  const ClientAction vk_back_action = private_context->GetDeleter()->OnKeyEvent(
      vk.virtual_key(), is_key_down, false);

  // Check if this key event is handled by VKBackBasedDeleter to support
  // *deletion_range* rule.
  bool use_pending_output = false;
  bool ignore_this_keyevent = false;
  if (open) {
    switch (vk_back_action) {
      case ClientAction::DO_DEFAULT_ACTION:
        // do nothing.
        break;
      case ClientAction::CALL_END_DELETION_THEN_DO_DEFAULT_ACTION:
        private_context->GetDeleter()->EndDeletion();
        break;
      case ClientAction::APPLY_PENDING_STATUS:
        use_pending_output = true;
        break;
      case ClientAction::CONSUME_KEY_BUT_NEVER_SEND_TO_SERVER:
        ignore_this_keyevent = true;
        break;
      case ClientAction::CALL_END_DELETION_BUT_NEVER_SEND_TO_SERVER:
        ignore_this_keyevent = true;
        private_context->GetDeleter()->EndDeletion();
        break;
      case ClientAction::SEND_KEY_TO_APPLICATION:
      default:
        DLOG(FATAL) << "this action is not applicable to OnKey.";
        break;
    }
    if (ignore_this_keyevent) {
      *eaten = TRUE;
      return S_OK;
    }

    const SurrogatePairObserver::ClientAction surrogate_action =
        private_context->GetSurrogatePairObserver()->OnKeyEvent(vk,
                                                                is_key_down);
    switch (surrogate_action.type) {
      case SurrogatePairObserver::DO_DEFAULT_ACTION:
        break;
      case SurrogatePairObserver::DO_DEFAULT_ACTION_WITH_RETURNED_UCS4:
        vk = VirtualKey::FromUnicode(surrogate_action.codepoint);
        break;
      case SurrogatePairObserver::CONSUME_KEY_BUT_NEVER_SEND_TO_SERVER:
        ignore_this_keyevent = true;
        break;
      default:
        DLOG(FATAL) << "this action is not applicable to OnKey.";
        ignore_this_keyevent = true;
        break;
    }

    if (ignore_this_keyevent) {
      *eaten = TRUE;
      return S_OK;
    }
  }

  commands::Output temporal_output;
  if (use_pending_output) {
    // In this case, we have a pending output. So no need to call
    // KeyEventHandler::ImeToAsciiEx.
    temporal_output = private_context->GetDeleter()->pending_output();
  } else if (open && is_key_down &&
             (vk.wide_char() == kTouchKeyboardPreviousPage)) {
    // Handle PrevPage button on the on-screen keyboard.
    SessionCommand command;
    command.set_type(SessionCommand::CONVERT_PREV_PAGE);
    if (!private_context->GetClient()->SendCommand(command, &temporal_output)) {
      *eaten = FALSE;
      return E_FAIL;
    }
    ignore_this_keyevent = false;
  } else if (open && is_key_down &&
             (vk.wide_char() == kTouchKeyboardNextPage)) {
    // Handle NextPage button on the on-screen keyboard.
    SessionCommand command;
    command.set_type(SessionCommand::CONVERT_NEXT_PAGE);
    if (!private_context->GetClient()->SendCommand(command, &temporal_output)) {
      *eaten = FALSE;
      return E_FAIL;
    }
    ignore_this_keyevent = false;
  } else {
    InputBehavior behavior = private_context->input_behavior();

    // Update On/Off state a conversion mode.
    InputState ime_state;
    ime_state.logical_conversion_mode = logical_mode;
    ime_state.visible_conversion_mode = visible_mode;
    ime_state.open = open;
    ime_state.last_down_key = private_context->last_down_key();

    // Acquire the legacy generic context once before the fallback decision.
    // For TSF, this same read also reports whether an ITfComposition currently
    // exists. This is a stronger boundary signal than cached server output.
    TipSurroundingTextInfo generic_info;
    const bool has_generic_info =
        TipSurroundingText::Get(text_service, context, &generic_info);

    // Some TSF hosts deliver OnKey without the matching OnTestKey. Synthesize
    // TestSendKey only for trusted TSF surrounding text outside an existing
    // composition. Legacy IMM32 document-feed text is never acquired for Zenz,
    // so it does not need a synthetic context-length request.
    if (!generic_info.is_password_input_scope &&
        ShouldRunZenzContextRequestFallback(
            has_test_key_result, has_generic_info,
            has_generic_info && generic_info.in_composition,
            generic_info.used_legacy_imm32_fallback)) {
      Context test_mozc_context;
      FillMozcContextCommon(text_service, context, &test_mozc_context);

      InputState test_next_state;
      commands::Output test_output;
      std::unique_ptr<Win32KeyboardInterface> test_keyboard(
          Win32KeyboardInterface::CreateDefault());

      const KeyEventHandlerResult test_result = KeyEventHandler::ImeProcessKey(
          vk, key_info.GetScanCodeForMapVirtualKey(), is_key_down,
          keyboard_status, behavior, ime_state, test_mozc_context,
          private_context->GetClient(), test_keyboard.get(), &test_next_state,
          &test_output);
      if (test_result.succeeded) {
        TipZenzContextRequestState fallback_request_state;
        fallback_request_state.UpdateFromOutput(test_output);
        zenz_context_request = fallback_request_state.Take();
      }
    }

    // This call is placed in OnKey instead on OnTestKey because VK_DBE_ROMAN
    // and VK_DBE_NOROMAN are handled as preserved keys in TSF Mozc.
    // See b/3118905 for why this is necessary.
    KeyEventHandler::UpdateBehaviorInImeProcessKey(
        vk, is_key_down, ime_state, private_context->mutable_input_behavior());

    std::unique_ptr<Win32KeyboardInterface> keyboard(
        Win32KeyboardInterface::CreateDefault());

    Context mozc_context;
    FillMozcContextForOnKey(
        text_service, context, has_generic_info ? &generic_info : nullptr,
        zenz_context_request, &mozc_context);

    InputState next_state;
    const KeyEventHandlerResult result = KeyEventHandler::ImeToAsciiEx(
        vk, key_info.GetScanCodeForMapVirtualKey(), is_key_down, keyboard_status,
        behavior, ime_state, mozc_context, private_context->GetClient(),
        keyboard.get(), &next_state, &temporal_output);

    if (!result.succeeded) {
      // no message generated.
      *eaten = FALSE;
      return S_OK;
    }

    *private_context->mutable_last_down_key() = next_state.last_down_key;

    TipInputModeManager* input_mode_manager =
        text_service->GetThreadContext()->GetInputModeManager();
    const TipInputModeManager::Action action =
        input_mode_manager->OnKey(vk, is_key_down, result.should_be_eaten);
    if (action == TipInputModeManager::Action::kUpdateUI) {
      text_service->PostUIUpdateMessage();
    }

    const bool handled_noop_mode_indicator_key =
        UpdateNoOpModeIndicatorKeyState(
            text_service, private_context, input_mode_manager, is_key_down,
            behavior, ime_state, result,
            /*is_on_key=*/true);

    if (!result.should_be_sent_to_server) {
      // no message generated.
      *eaten = handled_noop_mode_indicator_key ? TRUE : FALSE;
      return S_OK;
    }

    ignore_this_keyevent = handled_noop_mode_indicator_key
                               ? false
                               : !result.should_be_eaten;
  }

  // TSF spec guarantees that key event handling can always be a synchronous
  // operation.
  TipEditSession::OnOutputReceivedSync(text_service, context, temporal_output);
  *eaten = !ignore_this_keyevent ? TRUE : FALSE;

  return S_OK;
}

constexpr bool kKeyDown = true;
constexpr bool kKeyUp = false;

}  // namespace

HRESULT TipKeyeventHandler::OnTestKeyDown(TipTextService* text_service,
                                          ITfContext* context, WPARAM wparam,
                                          LPARAM lparam, BOOL* eaten) {
  return OnTestKey(text_service, context, kKeyDown, wparam, lparam, eaten);
}

HRESULT TipKeyeventHandler::OnTestKeyUp(TipTextService* text_service,
                                        ITfContext* context, WPARAM wparam,
                                        LPARAM lparam, BOOL* eaten) {
  return OnTestKey(text_service, context, kKeyUp, wparam, lparam, eaten);
}

HRESULT TipKeyeventHandler::OnKeyDown(TipTextService* text_service,
                                      ITfContext* context, WPARAM wparam,
                                      LPARAM lparam, BOOL* eaten) {
  return OnKey(text_service, context, kKeyDown, wparam, lparam, eaten);
}

HRESULT TipKeyeventHandler::OnKeyUp(TipTextService* text_service,
                                    ITfContext* context, WPARAM wparam,
                                    LPARAM lparam, BOOL* eaten) {
  return OnKey(text_service, context, kKeyUp, wparam, lparam, eaten);
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
