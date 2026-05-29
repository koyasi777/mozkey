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

// Session class of Mozc server.

#include "session/session.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "base/clock.h"
#include "base/util.h"
#include "composer/composer.h"
#include "composer/key_event_util.h"
#include "composer/table.h"
#include "engine/engine_converter_interface.h"
#include "engine/engine_interface.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/ime_context.h"
#include "session/key_event_transformer.h"
#include "session/keymap.h"
#include "session/zenz_client_factory.h"
#include "session/zenz_prompt_builder.h"
#include "transliteration/transliteration.h"

#ifdef __APPLE__
#include <TargetConditionals.h>  // for TARGET_OS_IPHONE
#endif                           // __APPLE__

#if defined(_WIN32)
#include <windows.h>
#endif

namespace mozc {
namespace session {
namespace {

using ::mozc::engine::ConversionPreferences;
using ::mozc::engine::EngineConverterInterface;

#if defined(_WIN32) && defined(MOZC_LEFT_CONTEXT_DEBUG)
std::wstring Utf8ToWideForDebug(absl::string_view s) {
  if (s.empty()) {
    return std::wstring();
  }

  const int input_size = static_cast<int>(s.size());
  const int wide_size =
      ::MultiByteToWideChar(CP_UTF8, 0, s.data(), input_size, nullptr, 0);
  if (wide_size <= 0) {
    return L"<invalid utf8>";
  }

  std::wstring w(wide_size, L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), input_size, w.data(), wide_size);
  return w;
}

void MozcLeftContextDebugOutput(absl::string_view message) {
  std::wstring w = Utf8ToWideForDebug(message);
  w.push_back(L'\n');
  ::OutputDebugStringW(w.c_str());
}
#endif  // defined(_WIN32) && defined(MOZC_LEFT_CONTEXT_DEBUG)

#if defined(_WIN32)
std::wstring Utf8ToWideForZenzDebug(absl::string_view s) {
  if (s.empty()) {
    return std::wstring();
  }

  const int input_size = static_cast<int>(s.size());
  const int wide_size =
      ::MultiByteToWideChar(CP_UTF8, 0, s.data(), input_size, nullptr, 0);
  if (wide_size <= 0) {
    return L"<invalid utf8>";
  }

  std::wstring w(wide_size, L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), input_size, w.data(), wide_size);
  return w;
}

void ZenzDebugOutput(absl::string_view message) {
  std::wstring w = Utf8ToWideForZenzDebug(message);
  w.push_back(L'\n');
  ::OutputDebugStringW(w.c_str());
}
#else
void ZenzDebugOutput(absl::string_view) {}
#endif

std::string ZenzRedactedTextStats(absl::string_view label,
                                  absl::string_view text) {
  return absl::StrCat(
      label, "_bytes=", text.size(),
      " ", label, "_chars=", Util::CharsLen(text));
}

std::string ZenzBool(bool value) {
  return value ? "true" : "false";
}

std::string ZenzSafeDebugReason(absl::string_view debug) {
  if (debug.empty()) {
    return "";
  }

  // The scorer is a local helper process, but the response still crosses a
  // process boundary.  Never propagate arbitrary scorer-provided debug text to
  // DebugView or client-visible Output fields.  Keep only short symbolic ASCII
  // reason strings.  Anything else is collapsed to a generic marker.
  constexpr size_t kMaxSafeDebugReasonBytes = 80;

  if (debug.size() > kMaxSafeDebugReasonBytes) {
    return "external_debug";
  }

  for (const unsigned char c : debug) {
    const bool safe =
        ('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') ||
        c == '_' ||
        c == '-' ||
        c == '.';

    if (!safe) {
      return "external_debug";
    }
  }

  return std::string(debug);
}

// Maximum size of multiple undo stack.
const size_t kMultipleUndoMaxSize = 10;

// Default live conversion debounce delay. The user-visible value is stored in
// Config::live_conversion_delay_msec.
constexpr uint32_t kDefaultLiveConversionDelayMillisec = 228;
constexpr uint32_t kMaxLiveConversionDelayMillisec = 1000;

// Do not run live conversion for a single-character composition.
// Single-character input is often a particle such as 「に」「を」「が」,
// and converting it to a kanji such as 「二」 too eagerly is noisy.
constexpr size_t kMinLiveConversionCompositionLength = 2;

constexpr uint32_t kDefaultZenzLiveCorrectionDelayMsec = 1000;
constexpr uint32_t kDefaultZenzLiveCorrectionTimeoutMsec = 180;
constexpr uint32_t kDefaultZenzLiveCorrectionPollMsec = 24;
constexpr uint32_t kDefaultZenzLiveCorrectionMinKeyLength = 2;
constexpr uint32_t kDefaultZenzLiveCorrectionLeftContextLength = 24;
constexpr uint32_t kMaxZenzLiveCorrectionDelayMsec = 5000;
constexpr uint32_t kMaxZenzLiveCorrectionTimeoutMsec = 1000;

// This is not the model inference timeout.  It is the maximum time the session
// keeps polling the async worker after the request has been submitted.  Cold
// start may include scorer process launch, pipe creation, llama-server startup,
// ready probing, and then the actual completion request.  Since this path is
// async, a longer poll window does not block the IME thread.
constexpr uint32_t kZenzLiveCorrectionAsyncWaitMsec = 3000;

bool IsLiveConversionTrailingDecorativeSymbol(char32_t c) {
  switch (c) {
    case 0x007E:  // ~
    case 0xFF5E:  // ～
    case 0x301C:  // 〜
    case 0x30FC:  // ー
    case 0x2015:  // ―
    case 0x2026:  // …
    case 0x0021:  // !
    case 0xFF01:  // ！
    case 0x003F:  // ?
    case 0xFF1F:  // ？
    case 0x3002:  // 。
    case 0x3001:  // 、
    case 0x002C:  // ,
    case 0xFF0C:  // ，
    case 0x002E:  // .
    case 0xFF0E:  // ．
      return true;
    default:
      return false;
  }
}

bool ShouldSkipLiveConversionForCompositionKey(absl::string_view key) {
  if (Util::CharsLen(key) < kMinLiveConversionCompositionLength) {
    return true;
  }

  absl::string_view core = key;
  bool has_decorative_tail = false;

  while (!core.empty()) {
    absl::string_view rest;
    char32_t last = 0;
    if (!Util::SplitLastChar32(core, &rest, &last)) {
      break;
    }
    if (!IsLiveConversionTrailingDecorativeSymbol(last)) {
      break;
    }
    has_decorative_tail = true;
    core = rest;
  }

  if (!has_decorative_tail) {
    return false;
  }

  // Pure symbol sequences such as "~~" do not need live conversion.
  if (core.empty()) {
    return true;
  }

  // "え~", "へー", "ん？" should stay as kana while typing.
  // But longer readings such as "きょう~" may still be live-converted.
  if (Util::CharsLen(core) >= kMinLiveConversionCompositionLength) {
    return false;
  }

  return Util::IsScriptType(core, Util::HIRAGANA);
}

uint32_t GetLiveConversionDelayMillisec(const config::Config& config) {
  if (!config.has_live_conversion_delay_msec()) {
    return kDefaultLiveConversionDelayMillisec;
  }
  return std::min(config.live_conversion_delay_msec(),
                  kMaxLiveConversionDelayMillisec);
}

uint32_t GetZenzLiveCorrectionDelayMsec(const config::Config& config) {
  if (!config.has_zenz_live_correction_delay_msec()) {
    return kDefaultZenzLiveCorrectionDelayMsec;
  }
  return std::min(config.zenz_live_correction_delay_msec(),
                  kMaxZenzLiveCorrectionDelayMsec);
}

uint32_t GetZenzLiveCorrectionTimeoutMsec(const config::Config& config) {
  if (!config.has_zenz_live_correction_timeout_msec()) {
    return kDefaultZenzLiveCorrectionTimeoutMsec;
  }
  return std::min(config.zenz_live_correction_timeout_msec(),
                  kMaxZenzLiveCorrectionTimeoutMsec);
}

uint32_t GetZenzLiveCorrectionMinKeyLength(const config::Config& config) {
  if (!config.has_zenz_live_correction_min_key_length()) {
    return kDefaultZenzLiveCorrectionMinKeyLength;
  }
  return std::max<uint32_t>(2, config.zenz_live_correction_min_key_length());
}

uint32_t GetZenzLiveCorrectionLeftContextLength(
    const config::Config& config) {
  if (!config.has_zenz_live_correction_left_context_length()) {
    return kDefaultZenzLiveCorrectionLeftContextLength;
  }
  return config.zenz_live_correction_left_context_length();
}

bool UseZenzFeedbackLearning(const config::Config& config) {
  return config.use_zenz_feedback_learning();
}

bool ContainsAsciiAlphabet(absl::string_view s) {
  for (const unsigned char c : s) {
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) {
      return true;
    }
  }
  return false;
}

bool StartsWithString(absl::string_view text, absl::string_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

void AddPreeditSegment(absl::string_view key,
                       absl::string_view value,
                       commands::Preedit::Segment::Annotation annotation,
                       commands::Preedit* preedit) {
  commands::Preedit::Segment* segment = preedit->add_segment();
  segment->set_annotation(annotation);
  segment->set_key(std::string(key));
  segment->set_value(std::string(value));
  segment->set_value_length(Util::CharsLen(value));
}

bool IsPlainBackspaceKey(const commands::KeyEvent& key) {
  return key.has_special_key() &&
         key.special_key() == commands::KeyEvent::BACKSPACE &&
         key.modifier_keys_size() == 0;
}

bool IsPendingZenzFeedbackDiscardKey(const commands::KeyEvent& key) {
  if (!key.has_special_key()) {
    return false;
  }

  switch (key.special_key()) {
    case commands::KeyEvent::BACKSPACE:
    case commands::KeyEvent::ESCAPE:
      return true;
    default:
      return false;
  }
}

bool IsPendingDirectCommitLearningDiscardKey(
    const commands::KeyEvent& key) {
  if (!key.has_special_key()) {
    return false;
  }

  switch (key.special_key()) {
    case commands::KeyEvent::BACKSPACE:
    case commands::KeyEvent::ESCAPE:
      return true;
    default:
      return false;
  }
}

void ExtractPreeditKeyAndValue(const commands::Preedit& preedit,
                               std::string* key,
                               std::string* value) {
  key->clear();
  value->clear();

  for (int i = 0; i < preedit.segment_size(); ++i) {
    const commands::Preedit::Segment& segment = preedit.segment(i);
    value->append(segment.value());
    if (segment.has_key() && !segment.key().empty()) {
      key->append(segment.key());
    } else {
      key->append(segment.value());
    }
  }
}

// Set input mode if the current input mode is not the given mode.
void SwitchInputMode(const transliteration::TransliterationType mode,
                     composer::Composer* composer) {
  if (composer->GetInputMode() != mode) {
    composer->SetInputMode(mode);
  }
  composer->SetNewInput();
}

// Set input mode to the |composer| if the input mode of |composer| is not
// the given |mode|.
void ApplyCompositionMode(const commands::CompositionMode mode,
                          composer::Composer* composer) {
  switch (mode) {
    case commands::HIRAGANA:
      SwitchInputMode(transliteration::HIRAGANA, composer);
      break;
    case commands::FULL_KATAKANA:
      SwitchInputMode(transliteration::FULL_KATAKANA, composer);
      break;
    case commands::HALF_KATAKANA:
      SwitchInputMode(transliteration::HALF_KATAKANA, composer);
      break;
    case commands::FULL_ASCII:
      SwitchInputMode(transliteration::FULL_ASCII, composer);
      break;
    case commands::HALF_ASCII:
      SwitchInputMode(transliteration::HALF_ASCII, composer);
      break;
    default:
      LOG(DFATAL) << "ime on with invalid mode";
  }
}

// Return true if the specified key event consists of any modifier key only.
bool IsPureModifierKeyEvent(const commands::KeyEvent& key) {
  if (key.has_key_code()) {
    return false;
  }
  if (key.has_special_key()) {
    return false;
  }
  if (key.modifier_keys_size() == 0) {
    return false;
  }
  return true;
}

bool IsPureSpaceKey(const commands::KeyEvent& key) {
  if (key.has_key_code()) {
    return false;
  }
  if (key.modifier_keys_size() > 0) {
    return false;
  }
  if (!key.has_special_key()) {
    return false;
  }
  if (key.special_key() != commands::KeyEvent::SPACE) {
    return false;
  }
  return true;
}

// Set session state to the given state and also update related status.
void SetSessionState(const ImeContext::State state, ImeContext* context) {
  const ImeContext::State prev_state = context->state();
  context->set_state(state);
  switch (state) {
    case ImeContext::DIRECT:
    case ImeContext::PRECOMPOSITION:
      context->mutable_composer()->Reset();
      break;
    case ImeContext::CONVERSION:
      context->mutable_composer()->ResetInputMode();
      break;
    case ImeContext::COMPOSITION:
      if (prev_state == ImeContext::PRECOMPOSITION) {
        // Notify the start of composition to the converter so that internal
        // state can be refreshed by the client context (especially by
        // preceding text).
        context->mutable_converter()->OnStartComposition(
            context->client_context());
      }
      break;
    default:
      // Do nothing.
      break;
  }
}

void SetStateToPredompositionAndCancel(ImeContext* context) {
  SetSessionState(ImeContext::PRECOMPOSITION, context);
  // mutable_converter's internal state should be updated by calling Cancel().
  // Internal state contains:
  // - candidate list
  // - result text to commit
  if (!context->mutable_converter()->CheckState(
          EngineConverterInterface::COMPOSITION)) {
    context->mutable_converter()->Cancel();
  }
}

commands::CompositionMode ToCompositionMode(
    mozc::transliteration::TransliterationType type) {
  commands::CompositionMode mode = commands::HIRAGANA;
  switch (type) {
    case transliteration::HIRAGANA:
      mode = commands::HIRAGANA;
      break;
    case transliteration::FULL_KATAKANA:
      mode = commands::FULL_KATAKANA;
      break;
    case transliteration::HALF_KATAKANA:
      mode = commands::HALF_KATAKANA;
      break;
    case transliteration::FULL_ASCII:
      mode = commands::FULL_ASCII;
      break;
    case transliteration::HALF_ASCII:
      mode = commands::HALF_ASCII;
      break;
    default:
      LOG(ERROR) << "Unknown input mode: " << type;
      // use HIRAGANA as a default.
  }
  return mode;
}

ImeContext::State GetEffectiveStateForTestSendKey(const commands::KeyEvent& key,
                                                  ImeContext::State state) {
  if (!key.has_activated()) {
    return state;
  }
  if (state == ImeContext::DIRECT && key.activated()) {
    // Indirect IME On found.
    return ImeContext::PRECOMPOSITION;
  }
  if (state != ImeContext::DIRECT && !key.activated()) {
    // Indirect IME Off found.
    return ImeContext::DIRECT;
  }
  return state;
}

}  // namespace

Session::Session(const EngineInterface& engine)
    : context_(CreateContext(engine)) {}

std::unique_ptr<ImeContext> Session::CreateContext(
    const EngineInterface& engine) const {
  auto context = std::make_unique<ImeContext>(engine.CreateEngineConverter());
  context->set_create_time(Clock::GetAbslTime());

#ifdef _WIN32
  // On Windows session is started with direct mode.
  // FIXME(toshiyuki): Ditto for Mac after verifying on Mac.
  context->set_state(ImeContext::DIRECT);
#else   // _WIN32
  context->set_state(ImeContext::PRECOMPOSITION);
#endif  // _WIN32

  // TODO(team): Remove #if based behavior change for cascading window.
  // Tests for session layer (session_handler_scenario_test, etc) can be
  // unstable.
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__linux__) || \
    defined(__wasm__)
  context->mutable_converter()->set_use_cascading_window(false);
#endif  // TARGET_OS_IPHONE || __linux__ || __wasm__

  return context;
}

void Session::PushUndoContext() {
  // Copy the current context and push it to the undo stack.
  undo_contexts_.emplace_back(std::make_unique<ImeContext>(*context_));
  // If the stack size exceeds the limitation, purge the oldest entries.
  while (undo_contexts_.size() > kMultipleUndoMaxSize) {
    undo_contexts_.pop_front();
  }
  DCHECK_LE(undo_contexts_.size(), kMultipleUndoMaxSize);
}

void Session::PopUndoContext() {
  if (!HasUndoContext()) {
    return;
  }
  context_ = std::move(undo_contexts_.back());
  undo_contexts_.pop_back();
}

void Session::ClearUndoContext() { undo_contexts_.clear(); }

bool Session::HasUndoContext() const { return !undo_contexts_.empty(); }

bool Session::IsCancelKeyForCompositionOrConversion(
    const commands::KeyEvent& key) const {
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();

  keymap::CompositionState::Commands composition_command;
  if (keymap->GetCommandComposition(key, &composition_command) &&
      composition_command == keymap::CompositionState::CANCEL) {
    return true;
  }

  keymap::ConversionState::Commands conversion_command;
  if (keymap->GetCommandConversion(key, &conversion_command) &&
      conversion_command == keymap::ConversionState::CANCEL) {
    return true;
  }

  return false;
}

void Session::MaybeSetUndoStatus(commands::Command* command) const {
  if (HasUndoContext()) {
    command->mutable_output()->mutable_status()->set_undo_available(true);
  }
}

void Session::EnsureIMEIsOn() {
  if (context_->state() == ImeContext::DIRECT) {
    SetSessionState(ImeContext::PRECOMPOSITION, context_.get());
  }
}

bool Session::SendCommand(commands::Command* command) {
  UpdateTime();
  UpdatePreferences(command);
  if (!command->input().has_command()) {
    return false;
  }
  TransformInput(command->mutable_input());

  const commands::SessionCommand& session_command = command->input().command();
  HandlePendingDirectCommitLearningForSessionCommand(session_command.type());
  HandlePendingZenzFeedbackForSessionCommand(session_command.type());

  bool result = false;
  if (session_command.type() ==
      commands::SessionCommand::SWITCH_COMPOSITION_MODE) {
    if (!session_command.has_composition_mode()) {
      return false;
    }
    switch (session_command.composition_mode()) {
      case commands::DIRECT:
        // TODO(komatsu): Implement here.
        break;
      case commands::HIRAGANA:
        result = CompositionModeHiragana(command);
        break;
      case commands::FULL_KATAKANA:
        result = CompositionModeFullKatakana(command);
        break;
      case commands::HALF_ASCII:
        result = CompositionModeHalfASCII(command);
        break;
      case commands::FULL_ASCII:
        result = CompositionModeFullASCII(command);
        break;
      case commands::HALF_KATAKANA:
        result = CompositionModeHalfKatakana(command);
        break;
      default:
        LOG(ERROR) << "Unknown mode: " << session_command.composition_mode();
        break;
    }
    MaybeSetUndoStatus(command);
    return result;
  }

  DCHECK_EQ(false, result);
  switch (command->input().command().type()) {
    case commands::SessionCommand::NONE:
      result = DoNothing(command);
      break;
    case commands::SessionCommand::REVERT:
      result = Revert(command);
      break;
    case commands::SessionCommand::SUBMIT:
      result = Commit(command);
      break;
    case commands::SessionCommand::SELECT_CANDIDATE:
      result = SelectCandidate(command);
      break;
    case commands::SessionCommand::SUBMIT_CANDIDATE:
      result = CommitCandidate(command);
      break;
    case commands::SessionCommand::HIGHLIGHT_CANDIDATE:
      result = HighlightCandidate(command);
      break;
    case commands::SessionCommand::GET_STATUS:
      result = GetStatus(command);
      break;
    case commands::SessionCommand::CONVERT_REVERSE:
      result = ConvertReverse(command);
      break;
    case commands::SessionCommand::UNDO:
      result = Undo(command);
      break;
    case commands::SessionCommand::RESET_CONTEXT:
      result = ResetContext(command);
      break;
    case commands::SessionCommand::MOVE_CURSOR:
      result = MoveCursorTo(command);
      break;
    case commands::SessionCommand::SWITCH_INPUT_FIELD_TYPE:
      result = SwitchInputFieldType(command);
      break;
    case commands::SessionCommand::UNDO_OR_REWIND:
      result = UndoOrRewind(command);
      break;
    case commands::SessionCommand::COMMIT_RAW_TEXT:
      result = CommitRawText(command);
      break;
    case commands::SessionCommand::CONVERT_PREV_PAGE:
      result = ConvertPrevPage(command);
      break;
    case commands::SessionCommand::CONVERT_NEXT_PAGE:
      result = ConvertNextPage(command);
      break;
    case commands::SessionCommand::TURN_ON_IME:
      result = MakeSureIMEOn(command);
      break;
    case commands::SessionCommand::TURN_OFF_IME:
      result = MakeSureIMEOff(command);
      break;
    case commands::SessionCommand::DELETE_CANDIDATE_FROM_HISTORY:
      result = DeleteCandidateFromHistory(command);
      break;
    case commands::SessionCommand::STOP_KEY_TOGGLING:
      result = StopKeyToggling(command);
      break;
    case commands::SessionCommand::UPDATE_COMPOSITION:
      result = UpdateComposition(command);
      break;

    case commands::SessionCommand::APPLY_LIVE_CONVERSION:
      result = ApplyDelayedLiveConversion(command);
      break;

    case commands::SessionCommand::APPLY_ZENZ_LIVE_CORRECTION:
      result = ApplyZenzLiveCorrection(command);
      break;

    case commands::SessionCommand::REQUEST_NWP: {
      ConversionPreferences conversion_preferences =
          context_->converter().conversion_preferences();
      conversion_preferences.request_suggestion =
          command->input().request_suggestion();
      // Resets converter's state (e.g. previous segments).
      // NWP will be generated from surrounding text given by the client.
      context_->mutable_converter()->Reset();
      result = context_->mutable_converter()->SuggestWithPreferences(
          context_->composer(), command->input().context(),
          conversion_preferences);
      if (result) {
        Output(command);
      }
      break;
    }
    default:
      LOG(WARNING) << "Unknown command" << *command;
      result = DoNothing(command);
      break;
  }
  if (context_->state() != ImeContext::CONVERSION) {
    live_conversion_active_ = false;
    ClearZenzLiveCorrectionState();
  }

  MaybeSetUndoStatus(command);
  return result;
}

bool Session::TestSendKey(commands::Command* command) {
  UpdateTime();
  UpdatePreferences(command);
  TransformInput(command->mutable_input());

  if (context_->state() == ImeContext::NONE) {
    // This must be an error.
    LOG(ERROR) << "Invalid state: NONE";
    return false;
  }

  const commands::KeyEvent& key = command->input().key();

  // To support indirect IME on/off by using KeyEvent::activated, use effective
  // state instead of directly using context_->state().
  const ImeContext::State state =
      GetEffectiveStateForTestSendKey(key, context_->state());

  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();

  // Direct input
  if (state == ImeContext::DIRECT) {
    keymap::DirectInputState::Commands key_command;
    if (!keymap->GetCommandDirect(key, &key_command) ||
        key_command == keymap::DirectInputState::NONE) {
      return EchoBack(command);
    }
    return DoNothing(command);
  }

  // Precomposition
  if (state == ImeContext::PRECOMPOSITION) {
    keymap::PrecompositionState::Commands key_command;
    const bool is_suggestion =
        context_->converter().CheckState(EngineConverterInterface::SUGGESTION);
    const bool result =
        is_suggestion ? keymap->GetCommandZeroQuerySuggestion(key, &key_command)
                      : keymap->GetCommandPrecomposition(key, &key_command);
    if (!result || key_command == keymap::PrecompositionState::NONE) {
      if (HasUndoContext() && IsCancelKeyForCompositionOrConversion(key)) {
        return Revert(command);
      }

      // Clear undo context just in case. b/5529702.
      // Note that the undo context will not be cleared in
      // EchoBackAndClearUndoContext if the key event consists of modifier keys
      // only.
      return EchoBackAndClearUndoContext(command);
    }
    // If the input_style is DIRECT_INPUT, KeyEvent is not consumed
    // and done echo back.  It works only when key_string is equal to
    // key_code.  We should fix this limitation when the as_is flag is
    // used for rather than numpad characters.
    if (key_command == keymap::PrecompositionState::INSERT_CHARACTER &&
        key.input_style() == commands::KeyEvent::DIRECT_INPUT) {
      return EchoBack(command);
    }

    // TODO(komatsu): This is a hack to work around the problem with
    // the inconsistency between TestSendKey and SendKey.
    switch (key_command) {
      case keymap::PrecompositionState::INSERT_SPACE:
        if (!IsFullWidthInsertSpace(command->input()) && IsPureSpaceKey(key)) {
          return EchoBackAndClearUndoContext(command);
        }
        return DoNothing(command);
      case keymap::PrecompositionState::INSERT_ALTERNATE_SPACE:
        if (IsFullWidthInsertSpace(command->input()) && IsPureSpaceKey(key)) {
          return EchoBackAndClearUndoContext(command);
        }
        return DoNothing(command);
      case keymap::PrecompositionState::INSERT_HALF_SPACE:
        if (IsPureSpaceKey(key)) {
          return EchoBackAndClearUndoContext(command);
        }
        return DoNothing(command);
      case keymap::PrecompositionState::INSERT_FULL_SPACE:
        return DoNothing(command);
      default:
        // Do nothing.
        break;
    }

    if (key_command == keymap::PrecompositionState::REVERT) {
      return Revert(command);
    }

    // If undo context is empty, echoes back the key event so that it can be
    // handled by the application. b/5553298
    if (key_command == keymap::PrecompositionState::UNDO && !HasUndoContext()) {
      return EchoBack(command);
    }

    return DoNothing(command);
  }

  // Do nothing.
  return DoNothing(command);
}

bool Session::SendKey(commands::Command* command) {
  UpdateTime();
  UpdatePreferences(command);
  TransformInput(command->mutable_input());
  // To support indirect IME on/off by using KeyEvent::activated, use effective
  // state instead of directly using context_->state().
  HandleIndirectImeOnOff(command);

  bool result = false;
  switch (context_->state()) {
    case ImeContext::DIRECT:
      result = SendKeyDirectInputState(command);
      break;

    case ImeContext::PRECOMPOSITION:
      result = SendKeyPrecompositionState(command);
      break;

    case ImeContext::COMPOSITION:
      result = SendKeyCompositionState(command);
      break;

    case ImeContext::CONVERSION:
      result = SendKeyConversionState(command);
      break;

    case ImeContext::NONE:
      result = false;
      break;
  }

  if (context_->state() != ImeContext::CONVERSION) {
    live_conversion_active_ = false;
    ClearZenzLiveCorrectionState();
  }

  MaybeSetUndoStatus(command);
  return result;
}

bool Session::UpdateCompositionInternal(commands::Command* command) {
  command->mutable_output()->set_consumed(true);

  context_->mutable_composer()->Reset();
  // Use the top entry for now.
  context_->mutable_composer()->SetCompositionsForHandwriting(
      command->input().command().composition_events());
  ClearUndoContext();
  SetSessionState(ImeContext::COMPOSITION, context_.get());

  if (Suggest(command->input())) {
    Output(command);
    return true;
  }

  OutputComposition(command);
  return true;
}

bool Session::UpdateComposition(commands::Command* command) {
  bool result = false;
  switch (context_->state()) {
    case ImeContext::DIRECT:
      result = EchoBackAndClearUndoContext(command);
      break;

    case ImeContext::PRECOMPOSITION:
      [[fallthrough]];
    case ImeContext::COMPOSITION:
      result = UpdateCompositionInternal(command);
      break;

    case ImeContext::CONVERSION:
      result = false;
      break;

    case ImeContext::NONE:
      result = false;
      break;
  }
  return result;
}

bool Session::SendKeyDirectInputState(commands::Command* command) {
  keymap::DirectInputState::Commands key_command;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  if (!keymap->GetCommandDirect(command->input().key(), &key_command)) {
    return EchoBackAndClearUndoContext(command);
  }

  switch (key_command) {
    case keymap::DirectInputState::IME_ON:
      return IMEOn(command);
    case keymap::DirectInputState::COMPOSITION_MODE_HIRAGANA:
      return CompositionModeHiragana(command);
    case keymap::DirectInputState::COMPOSITION_MODE_FULL_KATAKANA:
      return CompositionModeFullKatakana(command);
    case keymap::DirectInputState::COMPOSITION_MODE_HALF_KATAKANA:
      return CompositionModeHalfKatakana(command);
    case keymap::DirectInputState::COMPOSITION_MODE_FULL_ALPHANUMERIC:
      return CompositionModeFullASCII(command);
    case keymap::DirectInputState::COMPOSITION_MODE_HALF_ALPHANUMERIC:
      return CompositionModeHalfASCII(command);
    case keymap::DirectInputState::NONE:
      return EchoBackAndClearUndoContext(command);
    case keymap::DirectInputState::RECONVERT:
      return RequestConvertReverse(command);
  }
  return false;
}

bool Session::SendKeyPrecompositionState(commands::Command* command) {
  keymap::PrecompositionState::Commands key_command;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  const bool result =
      context_->converter().CheckState(EngineConverterInterface::SUGGESTION)
          ? keymap->GetCommandZeroQuerySuggestion(command->input().key(),
                                                  &key_command)
          : keymap->GetCommandPrecomposition(command->input().key(),
                                             &key_command);

  if (!result) {
    if (HasUndoContext() &&
        IsCancelKeyForCompositionOrConversion(command->input().key())) {
      return Revert(command);
    }
    return EchoBackAndClearUndoContext(command);
  }

  // Update the client context (if any) for later use. Note that the client
  // context is updated only here. In other words, we will stop updating the
  // client context once a conversion starts (mainly for performance reasons).
  if (command->has_input() && command->input().has_context()) {
    *context_->mutable_client_context() = command->input().context();

#if defined(_WIN32) && defined(MOZC_LEFT_CONTEXT_DEBUG)
    const commands::Context& client_context = command->input().context();
    if (client_context.has_preceding_text()) {
      MozcLeftContextDebugOutput(absl::StrCat(
          "[mozc-left-context] session preceding_text=[",
          client_context.preceding_text(), "]"));
    } else {
      MozcLeftContextDebugOutput(
          "[mozc-left-context] session context has no preceding_text");
    }
#endif  // defined(_WIN32) && defined(MOZC_LEFT_CONTEXT_DEBUG)

  } else {
    context_->mutable_client_context()->Clear();

#if defined(_WIN32) && defined(MOZC_LEFT_CONTEXT_DEBUG)
    MozcLeftContextDebugOutput("[mozc-left-context] session no context");
#endif  // defined(_WIN32) && defined(MOZC_LEFT_CONTEXT_DEBUG)
  }

  switch (key_command) {
    case keymap::PrecompositionState::INSERT_CHARACTER:
      return InsertCharacter(command);
    case keymap::PrecompositionState::INSERT_SPACE:
      return InsertSpace(command);
    case keymap::PrecompositionState::INSERT_ALTERNATE_SPACE:
      return InsertSpaceToggled(command);
    case keymap::PrecompositionState::INSERT_HALF_SPACE:
      return InsertSpaceHalfWidth(command);
    case keymap::PrecompositionState::INSERT_FULL_SPACE:
      return InsertSpaceFullWidth(command);
    case keymap::PrecompositionState::TOGGLE_ALPHANUMERIC_MODE:
      return ToggleAlphanumericMode(command);
    case keymap::PrecompositionState::REVERT:
      return Revert(command);
    case keymap::PrecompositionState::UNDO:
      return RequestUndo(command);
    case keymap::PrecompositionState::IME_OFF:
      return IMEOff(command);
    case keymap::PrecompositionState::IME_ON:
      return DoNothing(command);

    case keymap::PrecompositionState::COMPOSITION_MODE_HIRAGANA:
      return CompositionModeHiragana(command);
    case keymap::PrecompositionState::COMPOSITION_MODE_FULL_KATAKANA:
      return CompositionModeFullKatakana(command);
    case keymap::PrecompositionState::COMPOSITION_MODE_HALF_KATAKANA:
      return CompositionModeHalfKatakana(command);
    case keymap::PrecompositionState::COMPOSITION_MODE_FULL_ALPHANUMERIC:
      return CompositionModeFullASCII(command);
    case keymap::PrecompositionState::COMPOSITION_MODE_HALF_ALPHANUMERIC:
      return CompositionModeHalfASCII(command);
    case keymap::PrecompositionState::COMPOSITION_MODE_SWITCH_KANA_TYPE:
      return CompositionModeSwitchKanaType(command);

    case keymap::PrecompositionState::LAUNCH_CONFIG_DIALOG:
      return LaunchConfigDialog(command);
    case keymap::PrecompositionState::LAUNCH_DICTIONARY_TOOL:
      return LaunchDictionaryTool(command);
    case keymap::PrecompositionState::LAUNCH_WORD_REGISTER_DIALOG:
      return LaunchWordRegisterDialog(command);

    // For zero query suggestion
    case keymap::PrecompositionState::CANCEL:
      // It is a little kind of abuse of the EditCancel command.  It
      // would be nice to make a new command when EditCancel is
      // extended or the requirement of this command is added.
      return EditCancel(command);
    case keymap::PrecompositionState::CANCEL_AND_IME_OFF:
      // The same to keymap::PrecompositionState::CANCEL.
      return EditCancelAndIMEOff(command);
    // For zero query suggestion
    case keymap::PrecompositionState::COMMIT_FIRST_SUGGESTION:
      return CommitFirstSuggestion(command);
    // For zero query suggestion
    case keymap::PrecompositionState::PREDICT_AND_CONVERT:
      return PredictAndConvert(command);

    case keymap::PrecompositionState::NONE:
      if (HasUndoContext() &&
          IsCancelKeyForCompositionOrConversion(command->input().key())) {
        return Revert(command);
      }
      return EchoBackAndClearUndoContext(command);
    case keymap::PrecompositionState::RECONVERT:
      return RequestConvertReverse(command);

    case keymap::PrecompositionState::IME_ACTION:
      return ImeAction(command);
  }
  return false;
}

bool Session::SendKeyCompositionState(commands::Command* command) {
  keymap::CompositionState::Commands key_command;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  const bool result =
      context_->converter().CheckState(EngineConverterInterface::SUGGESTION)
          ? keymap->GetCommandSuggestion(command->input().key(), &key_command)
          : keymap->GetCommandComposition(command->input().key(), &key_command);

  if (!result) {
    return DoNothing(command);
  }

  switch (key_command) {
    case keymap::CompositionState::INSERT_CHARACTER:
      return InsertCharacter(command);

    case keymap::CompositionState::COMMIT:
      return Commit(command);

    case keymap::CompositionState::COMMIT_FIRST_SUGGESTION:
      return CommitFirstSuggestion(command);

    case keymap::CompositionState::CONVERT:
      return Convert(command);

    case keymap::CompositionState::CONVERT_WITHOUT_HISTORY:
      return ConvertWithoutHistory(command);

    case keymap::CompositionState::PREDICT_AND_CONVERT:
      return PredictAndConvert(command);

    case keymap::CompositionState::DEL:
      return Delete(command);

    case keymap::CompositionState::BACKSPACE:
      return Backspace(command);

    case keymap::CompositionState::INSERT_SPACE:
      return InsertSpace(command);

    case keymap::CompositionState::INSERT_ALTERNATE_SPACE:
      return InsertSpaceToggled(command);

    case keymap::CompositionState::INSERT_HALF_SPACE:
      return InsertSpaceHalfWidth(command);

    case keymap::CompositionState::INSERT_FULL_SPACE:
      return InsertSpaceFullWidth(command);

    case keymap::CompositionState::MOVE_CURSOR_LEFT:
      return MoveCursorLeft(command);

    case keymap::CompositionState::MOVE_CURSOR_RIGHT:
      return MoveCursorRight(command);

    case keymap::CompositionState::MOVE_CURSOR_TO_BEGINNING:
      return MoveCursorToBeginning(command);

    case keymap::CompositionState::MOVE_MOVE_CURSOR_TO_END:
      return MoveCursorToEnd(command);

    case keymap::CompositionState::CANCEL:
      return EditCancel(command);

    case keymap::CompositionState::CANCEL_AND_IME_OFF:
      return EditCancelAndIMEOff(command);

    case keymap::CompositionState::UNDO:
      return RequestUndo(command);

    case keymap::CompositionState::IME_OFF:
      return IMEOff(command);

    case keymap::CompositionState::IME_ON:
      return DoNothing(command);

    case keymap::CompositionState::CONVERT_TO_HIRAGANA:
      return ConvertToHiragana(command);

    case keymap::CompositionState::CONVERT_TO_FULL_KATAKANA:
      return ConvertToFullKatakana(command);

    case keymap::CompositionState::CONVERT_TO_HALF_KATAKANA:
      return ConvertToHalfKatakana(command);

    case keymap::CompositionState::CONVERT_TO_HALF_WIDTH:
      return ConvertToHalfWidth(command);

    case keymap::CompositionState::CONVERT_TO_FULL_ALPHANUMERIC:
      return ConvertToFullASCII(command);

    case keymap::CompositionState::CONVERT_TO_HALF_ALPHANUMERIC:
      return ConvertToHalfASCII(command);

    case keymap::CompositionState::SWITCH_KANA_TYPE:
      return SwitchKanaType(command);

    case keymap::CompositionState::DISPLAY_AS_HIRAGANA:
      return DisplayAsHiragana(command);

    case keymap::CompositionState::DISPLAY_AS_FULL_KATAKANA:
      return DisplayAsFullKatakana(command);

    case keymap::CompositionState::DISPLAY_AS_HALF_KATAKANA:
      return DisplayAsHalfKatakana(command);

    case keymap::CompositionState::TRANSLATE_HALF_WIDTH:
      return TranslateHalfWidth(command);

    case keymap::CompositionState::TRANSLATE_FULL_ASCII:
      return TranslateFullASCII(command);

    case keymap::CompositionState::TRANSLATE_HALF_ASCII:
      return TranslateHalfASCII(command);

    case keymap::CompositionState::TOGGLE_ALPHANUMERIC_MODE:
      return ToggleAlphanumericMode(command);

    case keymap::CompositionState::COMPOSITION_MODE_HIRAGANA:
      return CompositionModeHiragana(command);

    case keymap::CompositionState::COMPOSITION_MODE_FULL_KATAKANA:
      return CompositionModeFullKatakana(command);

    case keymap::CompositionState::COMPOSITION_MODE_HALF_KATAKANA:
      return CompositionModeHalfKatakana(command);

    case keymap::CompositionState::COMPOSITION_MODE_FULL_ALPHANUMERIC:
      return CompositionModeFullASCII(command);

    case keymap::CompositionState::COMPOSITION_MODE_HALF_ALPHANUMERIC:
      return CompositionModeHalfASCII(command);

    case keymap::CompositionState::NONE:
      return DoNothing(command);
  }
  return false;
}

bool Session::SendKeyConversionState(commands::Command* command) {
  keymap::ConversionState::Commands key_command;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  const bool result =
      context_->converter().CheckState(EngineConverterInterface::PREDICTION)
          ? keymap->GetCommandPrediction(command->input().key(), &key_command)
          : keymap->GetCommandConversion(command->input().key(), &key_command);

  if (!result) {
    return DoNothing(command);
  }

  const commands::KeyEvent& input_key = command->input().key();

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] SendKeyConversionState"
      " key_command=", static_cast<int>(key_command),
      " live_conversion_active=", ZenzBool(live_conversion_active_),
      " ", ZenzRedactedTextStats("live_key", live_conversion_key_),
      " ", ZenzRedactedTextStats("live_value", live_conversion_value_),
      " pending_zenz_pending=", ZenzBool(pending_zenz_live_.pending),
      " pending_zenz_gen=", pending_zenz_live_.generation,
      " pending_context_class=", pending_zenz_live_.context_class,
      " ", ZenzRedactedTextStats("pending_key", pending_zenz_live_.key),
      " ", ZenzRedactedTextStats("pending_value",
                                  pending_zenz_live_.mozc_value),
      " ", ZenzRedactedTextStats("zenz_key", zenz_live_key_),
      " ", ZenzRedactedTextStats("zenz_value", zenz_live_value_),
      " state=", static_cast<int>(context_->state()),
      " has_special_key=", ZenzBool(input_key.has_special_key()),
      " special_key=",
      input_key.has_special_key()
          ? static_cast<int>(input_key.special_key())
          : -1,
      " has_key_code=", ZenzBool(input_key.has_key_code()),
      " key_code=", input_key.has_key_code() ? input_key.key_code() : 0,
      " has_key_string=", ZenzBool(input_key.has_key_string()),
      " key_string_bytes=",
      input_key.has_key_string() ? input_key.key_string().size() : 0,
      " modifier_count=", input_key.modifier_keys_size(),
      " has_mode=", ZenzBool(input_key.has_mode()),
      " mode=", input_key.has_mode() ? static_cast<int>(input_key.mode()) : -1,
      " input_style=", static_cast<int>(input_key.input_style())));

  if (live_conversion_active_) {
    // During live conversion, Backspace should edit the underlying
    // composition instead of cancelling conversion.
    if (IsPlainBackspaceKey(command->input().key())) {
      DiscardPendingZenzFeedback("backspace_after_zenz");
      ClearZenzLiveCorrectionState();
      return Backspace(command);
    }

    // If a zenz correction is visible, Enter should commit the zenz value
    // immediately. Do not route this through normal Commit(), because some client
    // paths may promote the live conversion state before Commit() observes the
    // zenz state.
    if (key_command == keymap::ConversionState::COMMIT &&
        HasVisibleZenzLiveCorrection()) {
      ZenzDebugOutput(absl::StrCat(
          "[zenz-feedback] commit key while zenz visible ",
          ZenzRedactedTextStats("key", zenz_live_key_),
          " ", ZenzRedactedTextStats("value", zenz_live_value_)));

      return CommitZenzLiveCorrectionResult(command);
    }

    // Explicit conversion operations such as Space, candidate movement, or Cancel
    // promote live conversion back to normal conversion behavior.
    if (key_command != keymap::ConversionState::INSERT_CHARACTER) {
      if (key_command != keymap::ConversionState::COMMIT) {
        if (key_command == keymap::ConversionState::CANCEL ||
            key_command == keymap::ConversionState::CANCEL_AND_IME_OFF ||
            key_command == keymap::ConversionState::UNDO) {
          DiscardPendingZenzFeedback("cancel_after_zenz");
        } else if (HasVisibleZenzLiveCorrection()) {
          SetPendingZenzFeedbackRejected("explicit_conversion_after_zenz");
        }
        ClearZenzLiveCorrectionState();
      }
      live_conversion_active_ = false;
      context_->mutable_converter()->SetCandidateListVisible(true);
    }
  }

  switch (key_command) {
    case keymap::ConversionState::INSERT_CHARACTER:
      return InsertCharacter(command);

    case keymap::ConversionState::INSERT_SPACE:
      return InsertSpace(command);

    case keymap::ConversionState::INSERT_ALTERNATE_SPACE:
      return InsertSpaceToggled(command);

    case keymap::ConversionState::INSERT_HALF_SPACE:
      return InsertSpaceHalfWidth(command);

    case keymap::ConversionState::INSERT_FULL_SPACE:
      return InsertSpaceFullWidth(command);

    case keymap::ConversionState::COMMIT:
      return Commit(command);

    case keymap::ConversionState::COMMIT_SEGMENT:
      return CommitSegment(command);

    case keymap::ConversionState::CONVERT_NEXT:
      return ConvertNext(command);

    case keymap::ConversionState::CONVERT_PREV:
      return ConvertPrev(command);

    case keymap::ConversionState::CONVERT_NEXT_PAGE:
      return ConvertNextPage(command);

    case keymap::ConversionState::CONVERT_PREV_PAGE:
      return ConvertPrevPage(command);

    case keymap::ConversionState::PREDICT_AND_CONVERT:
      return PredictAndConvert(command);

    case keymap::ConversionState::SEGMENT_FOCUS_LEFT:
      return SegmentFocusLeft(command);

    case keymap::ConversionState::SEGMENT_FOCUS_RIGHT:
      return SegmentFocusRight(command);

    case keymap::ConversionState::SEGMENT_FOCUS_FIRST:
      return SegmentFocusLeftEdge(command);

    case keymap::ConversionState::SEGMENT_FOCUS_LAST:
      return SegmentFocusLast(command);

    case keymap::ConversionState::SEGMENT_WIDTH_EXPAND:
      return SegmentWidthExpand(command);

    case keymap::ConversionState::SEGMENT_WIDTH_SHRINK:
      return SegmentWidthShrink(command);

    case keymap::ConversionState::CANCEL:
      return ConvertCancel(command);

    case keymap::ConversionState::CANCEL_AND_IME_OFF:
      return EditCancelAndIMEOff(command);

    case keymap::ConversionState::UNDO:
      return RequestUndo(command);

    case keymap::ConversionState::IME_OFF:
      return IMEOff(command);

    case keymap::ConversionState::IME_ON:
      return DoNothing(command);

    case keymap::ConversionState::CONVERT_TO_HIRAGANA:
      return ConvertToHiragana(command);

    case keymap::ConversionState::CONVERT_TO_FULL_KATAKANA:
      return ConvertToFullKatakana(command);

    case keymap::ConversionState::CONVERT_TO_HALF_KATAKANA:
      return ConvertToHalfKatakana(command);

    case keymap::ConversionState::CONVERT_TO_HALF_WIDTH:
      return ConvertToHalfWidth(command);

    case keymap::ConversionState::CONVERT_TO_FULL_ALPHANUMERIC:
      return ConvertToFullASCII(command);

    case keymap::ConversionState::CONVERT_TO_HALF_ALPHANUMERIC:
      return ConvertToHalfASCII(command);

    case keymap::ConversionState::SWITCH_KANA_TYPE:
      return SwitchKanaType(command);

    case keymap::ConversionState::DISPLAY_AS_HIRAGANA:
      return DisplayAsHiragana(command);

    case keymap::ConversionState::DISPLAY_AS_FULL_KATAKANA:
      return DisplayAsFullKatakana(command);

    case keymap::ConversionState::DISPLAY_AS_HALF_KATAKANA:
      return DisplayAsHalfKatakana(command);

    case keymap::ConversionState::TRANSLATE_HALF_WIDTH:
      return TranslateHalfWidth(command);

    case keymap::ConversionState::TRANSLATE_FULL_ASCII:
      return TranslateFullASCII(command);

    case keymap::ConversionState::TRANSLATE_HALF_ASCII:
      return TranslateHalfASCII(command);

    case keymap::ConversionState::TOGGLE_ALPHANUMERIC_MODE:
      return ToggleAlphanumericMode(command);

    case keymap::ConversionState::COMPOSITION_MODE_HIRAGANA:
      return CompositionModeHiragana(command);

    case keymap::ConversionState::COMPOSITION_MODE_FULL_KATAKANA:
      return CompositionModeFullKatakana(command);

    case keymap::ConversionState::COMPOSITION_MODE_HALF_KATAKANA:
      return CompositionModeHalfKatakana(command);

    case keymap::ConversionState::COMPOSITION_MODE_FULL_ALPHANUMERIC:
      return CompositionModeFullASCII(command);

    case keymap::ConversionState::COMPOSITION_MODE_HALF_ALPHANUMERIC:
      return CompositionModeHalfASCII(command);

    case keymap::ConversionState::REPORT_BUG:
      return ReportBug(command);

    case keymap::ConversionState::DELETE_SELECTED_CANDIDATE:
      return DeleteCandidateFromHistory(command);

    case keymap::ConversionState::NONE:
      return DoNothing(command);
  }
  return false;
}

void Session::UpdatePreferences(commands::Command* command) {
  DCHECK(command);
  const config::Config& config = command->input().config();
  if (command->input().has_capability()) {
    *context_->mutable_client_capability() = command->input().capability();
  }

  // Update config values modified temporarily.
  // TODO(team): Stop using config for temporary modification.
  if (config.has_selection_shortcut()) {
    context_->mutable_converter()->set_selection_shortcut(
        config.selection_shortcut());
  }

#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__linux__) || \
    defined(__wasm__)
  context_->mutable_converter()->set_use_cascading_window(false);
#else   // TARGET_OS_IPHONE || __linux__ || __wasm__
  if (config.has_use_cascading_window()) {
    context_->mutable_converter()->set_use_cascading_window(
        config.use_cascading_window());
  }
#endif  // TARGET_OS_IPHONE || __linux__ || __wasm__
}

bool Session::IMEOn(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  ClearUndoContext();

  SetSessionState(ImeContext::PRECOMPOSITION, context_.get());
  if (command->input().has_key() && command->input().key().has_mode()) {
    ApplyCompositionMode(command->input().key().mode(),
                         context_->mutable_composer());
  }
  OutputMode(command);
  return true;
}

bool Session::IMEOff(commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "ime_off_after_direct_commit_learning");
  DiscardPendingZenzFeedback("ime_off_after_pending_feedback");

  command->mutable_output()->set_consumed(true);
  ClearUndoContext();

  Commit(command);

  // Reset the context.
  context_->mutable_converter()->Reset();

  SetSessionState(ImeContext::DIRECT, context_.get());
  OutputMode(command);
  return true;
}

bool Session::MakeSureIMEOn(mozc::commands::Command* command) {
  if (command->input().has_command() &&
      command->input().command().has_composition_mode() &&
      (command->input().command().composition_mode() == commands::DIRECT)) {
    // This is invalid and unsupported usage.
    return false;
  }

  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::DIRECT) {
    ClearUndoContext();
    SetSessionState(ImeContext::PRECOMPOSITION, context_.get());
  }
  if (command->input().has_command() &&
      command->input().command().has_composition_mode()) {
    ApplyCompositionMode(command->input().command().composition_mode(),
                         context_->mutable_composer());
  }
  OutputMode(command);
  return true;
}

bool Session::MakeSureIMEOff(mozc::commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "make_sure_ime_off_after_direct_commit_learning");
  DiscardPendingZenzFeedback("make_sure_ime_off_after_pending_feedback");

  if (command->input().has_command() &&
      command->input().command().has_composition_mode() &&
      (command->input().command().composition_mode() == commands::DIRECT)) {
    // This is invalid and unsupported usage.
    return false;
  }

  command->mutable_output()->set_consumed(true);
  if (context_->state() != ImeContext::DIRECT) {
    ClearUndoContext();
    Commit(command);
    // Reset the context.
    context_->mutable_converter()->Reset();
    SetSessionState(ImeContext::DIRECT, context_.get());
  }
  if (command->input().has_command() &&
      command->input().command().has_composition_mode()) {
    ApplyCompositionMode(command->input().command().composition_mode(),
                         context_->mutable_composer());
  }
  OutputMode(command);
  return true;
}

bool Session::EchoBack(commands::Command* command) {
  command->mutable_output()->set_consumed(false);
  context_->mutable_converter()->Reset();
  OutputKey(command);
  return true;
}

bool Session::EchoBackAndClearUndoContext(commands::Command* command) {
  command->mutable_output()->set_consumed(false);

  // Don't clear undo context when KeyEvent has a modifier key only.
  // TODO(hsumita): A modifier key may be assigned to another functions.
  //                ex) InsertSpace
  //                We need to check it outside of this function.
  const commands::KeyEvent& key_event = command->input().key();

  if (IsPendingDirectCommitLearningDiscardKey(key_event)) {
    DiscardPendingDirectCommitLearning(
        "echo_back_discard_key_after_direct_commit");
  }

  if (IsPendingZenzFeedbackDiscardKey(key_event)) {
    DiscardPendingZenzFeedback("echo_back_discard_key");
  }

  if (!IsPureModifierKeyEvent(key_event)) {
    ClearUndoContext();
  }

  return EchoBack(command);
}

bool Session::DoNothing(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  // Quick hack for zero query suggestion.
  // Caveats: Resetting converter causes b/8703702 on Windows.
  // Basically we should not *do* something in DoNothing.
  // TODO(komatsu): Fix this.
  if (context_->GetRequest().zero_query_suggestion() &&
      context_->converter().IsActive() &&
      (context_->state() == ImeContext::PRECOMPOSITION)) {
    context_->mutable_converter()->Reset();
    Output(command);
  }
  if (context_->state() & (ImeContext::COMPOSITION | ImeContext::CONVERSION)) {
    Output(command);
  }
  return true;
}

bool Session::Revert(commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "revert_after_direct_commit_learning");
  DiscardPendingZenzFeedback("revert_after_pending_feedback");

  if (context_->state() == ImeContext::PRECOMPOSITION) {
    context_->mutable_converter()->Revert();
    return EchoBackAndClearUndoContext(command);
  }

  if (!(context_->state() &
        (ImeContext::COMPOSITION | ImeContext::CONVERSION))) {
    return DoNothing(command);
  }

  command->mutable_output()->set_consumed(true);
  ClearUndoContext();

  SetStateToPredompositionAndCancel(context_.get());
  ClearLiveConversionState();
  Output(command);
  return true;
}

bool Session::ResetContext(commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "reset_context_after_direct_commit_learning");
  DiscardPendingZenzFeedback("reset_context_after_pending_feedback");

  if (context_->state() == ImeContext::PRECOMPOSITION) {
    context_->mutable_converter()->Reset();
    return EchoBackAndClearUndoContext(command);
  }

  command->mutable_output()->set_consumed(true);
  ClearUndoContext();

  context_->mutable_converter()->Reset();

  SetStateToPredompositionAndCancel(context_.get());
  ClearLiveConversionState();
  Output(command);
  return true;
}

void Session::SetTable(std::shared_ptr<const composer::Table> table) {
  if (!table) {
    return;
  }
  ClearUndoContext();
  context_->mutable_composer()->SetTable(std::move(table));
}

void Session::SetConfig(std::shared_ptr<const config::Config> config) {
  DCHECK(config);
  ClearUndoContext();
  context_->SetConfig(std::move(config));
}

void Session::SetRequest(std::shared_ptr<const commands::Request> request) {
  DCHECK(request);
  ClearUndoContext();
  context_->SetRequest(std::move(request));
}

void Session::SetKeyMapManager(
    std::shared_ptr<const mozc::keymap::KeyMapManager> key_map_manager) {
  DCHECK(key_map_manager);
  context_->SetKeyMapManager(key_map_manager);
}

bool Session::GetStatus(commands::Command* command) {
  OutputMode(command);
  return true;
}

bool Session::RequestConvertReverse(commands::Command* command) {
  if (context_->state() != ImeContext::PRECOMPOSITION &&
      context_->state() != ImeContext::DIRECT) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  Output(command);

  // Fill callback message.
  commands::SessionCommand* session_command =
      command->mutable_output()->mutable_callback()->mutable_session_command();
  session_command->set_type(commands::SessionCommand::CONVERT_REVERSE);
  return true;
}

bool Session::ConvertReverse(commands::Command* command) {
  if (context_->state() != ImeContext::PRECOMPOSITION &&
      context_->state() != ImeContext::DIRECT) {
    return DoNothing(command);
  }

  const std::string& composition = command->input().command().text();

  // Validate before requesting reverse conversion
  if (!Util::IsValidUtf8(composition)) {
    DLOG(INFO) << "Input is not valid text as utf8";
    return DoNothing(command);
  }
  for (ConstChar32Iterator iter(composition); !iter.Done(); iter.Next()) {
    if (!Util::IsAcceptableCharacterAsCandidate(iter.Get())) {
      DLOG(INFO)
          << "Input contains characters not suitable for reverse conversion";
      return DoNothing(command);
    }
  }

  std::string reading;
  if (!context_->mutable_converter()->GetReadingText(composition, &reading)) {
    LOG(ERROR) << "Failed to get reading text";
    return DoNothing(command);
  }

  composer::Composer* composer = context_->mutable_composer();
  composer->Reset();
  ClearUndoContext();
  std::vector<std::string> reading_characters;
  composer->InsertCharacterPreedit(reading);
  composer->set_source_text(composition);
  // start conversion here.
  if (!context_->mutable_converter()->Convert(*composer)) {
    LOG(ERROR) << "Failed to start conversion for reverse conversion";
    return false;
  }

  command->mutable_output()->set_consumed(true);

  SetSessionState(ImeContext::CONVERSION, context_.get());
  context_->mutable_converter()->SetCandidateListVisible(true);
  Output(command);
  return true;
}

bool Session::RequestUndo(commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "undo_after_direct_commit_learning");
  DiscardPendingZenzFeedback("undo_after_pending_feedback");

  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::CONVERSION |
         ImeContext::COMPOSITION))) {
    return DoNothing(command);
  }

  // If undo context is empty, echoes back the key event so that it can be
  // handled by the application. b/5553298
  if (context_->state() == ImeContext::PRECOMPOSITION && !HasUndoContext()) {
    return EchoBack(command);
  }

  command->mutable_output()->set_consumed(true);
  Output(command);

  // Fill callback message.
  commands::SessionCommand* session_command =
      command->mutable_output()->mutable_callback()->mutable_session_command();
  session_command->set_type(commands::SessionCommand::UNDO);
  return true;
}

bool Session::Undo(commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "undo_command_after_direct_commit_learning");
  DiscardPendingZenzFeedback("undo_command_after_pending_feedback");

  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::CONVERSION |
         ImeContext::COMPOSITION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  // Check the undo context
  if (!HasUndoContext()) {
    return DoNothing(command);
  }

  // Rollback the last user history.
  context_->mutable_converter()->Revert();

  size_t result_size = 0;
  int32_t cursor_offset = 0;
  if (context_->output().has_result()) {
    // Check the client's capability
    if (!(context_->client_capability().text_deletion() &
          commands::Capability::DELETE_PRECEDING_TEXT)) {
      return DoNothing(command);
    }
    result_size = Util::CharsLen(context_->output().result().value());
    cursor_offset = context_->output().result().cursor_offset();
  }

  PopUndoContext();

  if (result_size > 0) {
    commands::DeletionRange* range =
        command->mutable_output()->mutable_deletion_range();
    range->set_offset(-(static_cast<int32_t>(result_size) + cursor_offset));
    range->set_length(result_size);
  }

  Output(command);
  return true;
}

bool Session::SelectCandidateInternal(commands::Command* command) {
  // If the current state is not conversion, composition or
  // precomposition, the candidate window should not be shown.  (On
  // composition or precomposition, the window is able to be shown as
  // a suggestion window).
  if (!(context_->state() & (ImeContext::CONVERSION | ImeContext::COMPOSITION |
                             ImeContext::PRECOMPOSITION))) {
    return false;
  }
  if (!command->input().has_command() || !command->input().command().has_id()) {
    LOG(WARNING) << "input.command or input.command.id did not exist.";
    return false;
  }
  if (!context_->converter().IsActive()) {
    LOG(WARNING) << "converter is not active. (no candidates)";
    return false;
  }

  command->mutable_output()->set_consumed(true);

  context_->mutable_converter()->CandidateMoveToId(
      command->input().command().id(), context_->composer());
  SetSessionState(ImeContext::CONVERSION, context_.get());

  return true;
}

bool Session::SelectCandidate(commands::Command* command) {
  if (!SelectCandidateInternal(command)) {
    return DoNothing(command);
  }
  Output(command);
  return true;
}

bool Session::CommitCandidate(commands::Command* command) {
  if (!(context_->state() & (ImeContext::COMPOSITION | ImeContext::CONVERSION |
                             ImeContext::PRECOMPOSITION))) {
    return false;
  }
  const commands::Input& input = command->input();
  if (!input.has_command() || !input.command().has_id()) {
    LOG(WARNING) << "input.command or input.command.id did not exist.";
    return false;
  }
  if (!context_->converter().IsActive()) {
    LOG(WARNING) << "converter is not active. (no candidates)";
    return false;
  }
  command->mutable_output()->set_consumed(true);

  PushUndoContext();

  if (context_->state() & ImeContext::CONVERSION) {
    // There is a focused candidate so just select a candidate based on
    // input message and commit first segment.
    context_->mutable_converter()->CandidateMoveToId(input.command().id(),
                                                     context_->composer());
    CommitHeadToFocusedSegmentsInternal(command->input().context());
  } else {
    // No candidate is focused.
    size_t consumed_key_size = 0;
    if (context_->mutable_converter()->CommitSuggestionById(
            input.command().id(), context_->composer(),
            command->input().context(), &consumed_key_size)) {
      if (consumed_key_size < context_->composer().GetLength()) {
        // partial suggestion was committed.
        context_->mutable_composer()->DeleteRange(0, consumed_key_size);
        // Don't clear the undo context, which we've just updated.
        MoveCursorToEndInternal(command, false);
        // Copy the previous output for Undo.
        *context_->mutable_output() = command->output();
        return true;
      }
    }
  }

  if (!context_->converter().IsActive()) {
    // If the converter is not active (ie. the segment size was one.),
    // the state should be switched to precomposition.
    SetSessionState(ImeContext::PRECOMPOSITION, context_.get());

    // Get suggestion if zero_query_suggestion is set.
    // zero_query_suggestion is usually set where the client is a
    // mobile.
    if (context_->GetRequest().zero_query_suggestion()) {
      Suggest(command->input());
    }
  }
  Output(command);
  // Copy the previous output for Undo.
  *context_->mutable_output() = command->output();
  return true;
}

bool Session::HighlightCandidate(commands::Command* command) {
  if (!SelectCandidateInternal(command)) {
    return false;
  }
  context_->mutable_converter()->SetCandidateListVisible(true);
  Output(command);
  return true;
}

bool Session::MaybeSelectCandidate(commands::Command* command) {
  if (context_->state() != ImeContext::CONVERSION) {
    return false;
  }
  // When using special romaji table (== The key event is from a virtual
  // keyboard), don't consume it as a shortcut selection operation.
  if (context_->GetRequest().special_romanji_table() !=
      commands::Request::DEFAULT_TABLE) {
    return false;
  }

  // Note that SHORTCUT_ASDFGHJKL should be handled even when the CapsLock is
  // enabled. This is why we need to normalize the key event here.
  // See b/5655743.
  commands::KeyEvent normalized_keyevent;
  KeyEventUtil::NormalizeModifiers(command->input().key(),
                                   &normalized_keyevent);

  // Check if the input character is in the shortcut.
  // TODO(komatsu): Support non ASCII characters such as Unicode and
  // special keys.
  const char shortcut = static_cast<char>(normalized_keyevent.key_code());
  return context_->mutable_converter()->CandidateMoveToShortcut(shortcut);
}

void Session::CancelPendingLiveConversion() {
  ++live_conversion_generation_;
  live_conversion_pending_ = false;
  pending_live_conversion_generation_ = 0;
  pending_live_conversion_key_.clear();
  CancelPendingZenzLiveCorrection();
}

void Session::ClearLiveConversionState() {
  ++live_conversion_generation_;

  live_conversion_active_ = false;
  live_conversion_pending_ = false;
  pending_live_conversion_generation_ = 0;
  pending_live_conversion_key_.clear();

  live_conversion_key_.clear();
  live_conversion_preedit_.clear();
  live_conversion_value_.clear();
  live_conversion_preedit_output_.Clear();
  ClearZenzLiveCorrectionState();
}

void Session::CancelLiveConversionForEditing() {
  CancelPendingLiveConversion();
  ClearZenzLiveCorrectionState();

  if (!live_conversion_active_) {
    return;
  }

  live_conversion_active_ = false;
  SetSessionState(ImeContext::COMPOSITION, context_.get());
  context_->mutable_converter()->Cancel();
}

bool Session::MaybeStartLiveConversion(commands::Command* command) {
  if (!context_->GetConfig().use_live_conversion()) {
    return false;
  }

  if (context_->state() != ImeContext::COMPOSITION) {
    return false;
  }

  if (context_->composer().GetInputFieldType() == commands::Context::PASSWORD) {
    return false;
  }

  const transliteration::TransliterationType input_mode =
      context_->composer().GetInputMode();
  if (input_mode == transliteration::HALF_ASCII ||
      input_mode == transliteration::FULL_ASCII) {
    return false;
  }

  const size_t length = context_->composer().GetLength();
  const std::string live_conversion_key =
      context_->composer().GetQueryForConversion();

  if (ShouldSkipLiveConversionForCompositionKey(live_conversion_key) ||
      length != context_->composer().GetCursor()) {
    return false;
  }

  // Capture the raw composition before Convert(). These strings are the stable
  // source for the next pending-suffix display.
  const std::string live_conversion_preedit =
      context_->composer().GetStringForPreedit();

  live_conversion_pending_ = false;
  pending_live_conversion_generation_ = 0;
  pending_live_conversion_key_.clear();

  if (!context_->mutable_converter()->Convert(context_->composer())) {
    OutputComposition(command);
    return true;
  }

  SetSessionState(ImeContext::CONVERSION, context_.get());
  live_conversion_active_ = true;

  // Keep the candidate list visible internally so that the Windows renderer is
  // updated.
  context_->mutable_converter()->SetCandidateListVisible(true);

  Output(command);
  command->mutable_output()->set_live_conversion(true);
  command->mutable_output()->set_live_conversion_pending(false);

  live_conversion_key_ = live_conversion_key;
  live_conversion_preedit_ = live_conversion_preedit;

  if (command->output().has_preedit()) {
    live_conversion_preedit_output_ = command->output().preedit();

    std::string unused_key;
    ExtractPreeditKeyAndValue(command->output().preedit(),
                              &unused_key,
                              &live_conversion_value_);
  } else {
    live_conversion_preedit_output_.Clear();
    live_conversion_value_ = context_->composer().GetStringForSubmission();
  }

  ClearZenzLiveCorrectionState();

  if (MaybeApplyZenzFeedbackLiveCorrection(command)) {
    return true;
  }

  MaybeScheduleZenzLiveCorrection(command);
  return true;
}

bool Session::OutputPendingLiveConversion(commands::Command* command) const {
  const std::string current_key = context_->composer().GetQueryForConversion();
  const std::string raw_preedit = context_->composer().GetStringForPreedit();

  const bool has_stable_live_conversion =
      !live_conversion_key_.empty() &&
      !live_conversion_preedit_.empty() &&
      !live_conversion_value_.empty() &&
      live_conversion_preedit_output_.segment_size() > 0;

  // First composition after starting IME has no stable converted prefix yet.
  // In that case, raw pending display is expected and should still be debounced.
  if (!has_stable_live_conversion) {
    OutputComposition(command);
    commands::Output* output = command->mutable_output();
    output->clear_candidate_window();
    output->set_live_conversion(true);
    output->set_live_conversion_pending(true);
    return true;
  }

  // If stable-prefix composition cannot be built safely, do not fall back to
  // raw hiragana. The caller should immediately run live conversion instead.
  if (!StartsWithString(current_key, live_conversion_key_) ||
      !StartsWithString(raw_preedit, live_conversion_preedit_)) {
    return false;
  }

  const std::string suffix_key =
      current_key.substr(live_conversion_key_.size());
  const std::string suffix_value =
      raw_preedit.substr(live_conversion_preedit_.size());

  OutputComposition(command);

  commands::Output* output = command->mutable_output();
  output->clear_candidate_window();
  output->set_live_conversion(true);
  output->set_live_conversion_pending(true);

  commands::Preedit* preedit = output->mutable_preedit();
  preedit->Clear();

  // Reuse the exact segment structure and annotations from the latest real
  // live conversion. This avoids flickering between UNDERLINE and HIGHLIGHT
  // display attributes.
  for (int i = 0; i < live_conversion_preedit_output_.segment_size(); ++i) {
    *preedit->add_segment() = live_conversion_preedit_output_.segment(i);
  }

  if (!suffix_value.empty()) {
    AddPreeditSegment(suffix_key.empty() ? suffix_value : suffix_key,
                      suffix_value,
                      commands::Preedit::Segment::UNDERLINE,
                      preedit);
  }

  preedit->set_cursor(Util::CharsLen(live_conversion_value_) +
                      Util::CharsLen(suffix_value));

  return true;
}

void Session::AttachDelayedLiveConversionCallback(
    commands::Command* command) const {
  commands::Output::Callback* callback =
      command->mutable_output()->mutable_callback();

  commands::SessionCommand* session_command =
      callback->mutable_session_command();

  session_command->set_type(
      commands::SessionCommand::APPLY_LIVE_CONVERSION);
  session_command->set_live_conversion_generation(
      pending_live_conversion_generation_);
  session_command->set_live_conversion_key(pending_live_conversion_key_);

  callback->set_delay_millisec(
    GetLiveConversionDelayMillisec(context_->GetConfig()));
}

bool Session::MaybeScheduleLiveConversion(commands::Command* command) {
  if (!context_->GetConfig().use_live_conversion()) {
    return false;
  }

  if (context_->state() != ImeContext::COMPOSITION) {
    return false;
  }

  if (context_->composer().GetInputFieldType() == commands::Context::PASSWORD) {
    return false;
  }

  const transliteration::TransliterationType input_mode =
      context_->composer().GetInputMode();
  if (input_mode == transliteration::HALF_ASCII ||
      input_mode == transliteration::FULL_ASCII) {
    return false;
  }

  const size_t length = context_->composer().GetLength();
  const std::string key = context_->composer().GetQueryForConversion();

  if (ShouldSkipLiveConversionForCompositionKey(key) ||
      length != context_->composer().GetCursor()) {
    return false;
  }

  const uint32_t delay_msec =
      GetLiveConversionDelayMillisec(context_->GetConfig());
  if (delay_msec == 0) {
    return MaybeStartLiveConversion(command);
  }

  ++live_conversion_generation_;
  live_conversion_pending_ = true;
  pending_live_conversion_generation_ = live_conversion_generation_;
  pending_live_conversion_key_ = key;

  if (!OutputPendingLiveConversion(command)) {
    // Avoid showing raw hiragana fallback. If pending display cannot be built
    // from the stable converted prefix, materialize live conversion immediately.
    return MaybeStartLiveConversion(command);
  }

  AttachDelayedLiveConversionCallback(command);

  return true;
}

bool Session::IgnoreStaleDelayedLiveConversion(commands::Command* command) {
  command->mutable_output()->set_consumed(true);

  // A stale delayed callback must not return an empty Output. In TSF, an empty
  // consumed Output may clear the visible composition even though the server-side
  // composer still has text.
  if (live_conversion_pending_) {
    OutputPendingLiveConversion(command);
    return true;
  }

  if (live_conversion_active_ && context_->state() == ImeContext::CONVERSION) {
    Output(command);
    command->mutable_output()->set_live_conversion(true);
    command->mutable_output()->set_live_conversion_pending(false);
    return true;
  }

  OutputFromState(command);
  return true;
}

bool Session::ApplyDelayedLiveConversion(commands::Command* command) {
  command->mutable_output()->set_consumed(true);

  const commands::SessionCommand& session_command =
      command->input().command();

  if (!live_conversion_pending_) {
    return IgnoreStaleDelayedLiveConversion(command);
  }

  if (!session_command.has_live_conversion_generation() ||
      session_command.live_conversion_generation() !=
          pending_live_conversion_generation_) {
    return IgnoreStaleDelayedLiveConversion(command);
  }

  if (!session_command.has_live_conversion_key() ||
      session_command.live_conversion_key() != pending_live_conversion_key_) {
    return IgnoreStaleDelayedLiveConversion(command);
  }

  if (context_->state() != ImeContext::COMPOSITION) {
    return IgnoreStaleDelayedLiveConversion(command);
  }

  const std::string current_key = context_->composer().GetQueryForConversion();
  if (current_key != pending_live_conversion_key_) {
    return IgnoreStaleDelayedLiveConversion(command);
  }

  const size_t length = context_->composer().GetLength();
  if (ShouldSkipLiveConversionForCompositionKey(current_key) ||
      length != context_->composer().GetCursor()) {
    return IgnoreStaleDelayedLiveConversion(command);
  }

  return MaybeStartLiveConversion(command);
}

bool Session::FlushPendingLiveConversion() {
  if (!live_conversion_pending_) {
    return false;
  }

  if (context_->state() != ImeContext::COMPOSITION) {
    CancelPendingLiveConversion();
    return false;
  }

  const std::string current_key = context_->composer().GetQueryForConversion();
  if (current_key != pending_live_conversion_key_ ||
      ShouldSkipLiveConversionForCompositionKey(current_key)) {
    CancelPendingLiveConversion();
    return false;
  }

  commands::Command dummy_command;
  return MaybeStartLiveConversion(&dummy_command);
}

std::string Session::ExtractZenzLeftContext(uint32_t max_chars) const {
  if (max_chars == 0) {
    return "";
  }

  if (!context_->client_context().has_preceding_text()) {
    return "";
  }

  const std::string& preceding_text =
      context_->client_context().preceding_text();
  const size_t len = Util::CharsLen(preceding_text);
  if (len <= max_chars) {
    return preceding_text;
  }

  return std::string(
    Util::Utf8SubString(preceding_text, len - max_chars, max_chars));
}

std::string Session::BuildZenzFeedbackContextClass(
    absl::string_view left_context) const {
  const ZenzContextSanitizationResult result =
      zenz_context_sanitizer_.SanitizeForZenz(
          left_context, GetZenzLiveCorrectionLeftContextLength(
                            context_->GetConfig()));

  // Do not persist raw context or reversible context snippets.  Feedback uses
  // only a coarse non-reversible class.
  return result.context_class.empty() ? "empty" : result.context_class;
}

void Session::RecordZenzLiveCorrectionAccepted(
    absl::string_view key,
    absl::string_view left_context,
    absl::string_view value) {
  if (!UseZenzFeedbackLearning(context_->GetConfig())) {
    return;
  }

  if (key.empty() || value.empty()) {
    LOG(ERROR) << "[zenz-feedback] skip accepted empty key/value";
    return;
  }

  const std::string context_class =
      BuildZenzFeedbackContextClass(left_context);

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] accepted ",
      ZenzRedactedTextStats("key", key),
      " ", ZenzRedactedTextStats("value", value),
      " context_class=", context_class));

  zenz_feedback_store_.RecordAccepted(key, context_class, value);

  ZenzDebugOutput("[zenz-feedback] RecordAccepted returned");
}

bool Session::MaybeLearnZenzCandidateToMozcHistory(
    absl::string_view key,
    absl::string_view value) {
  if (!UseZenzFeedbackLearning(context_->GetConfig())) {
    return false;
  }

  if (key.empty() || value.empty()) {
    return false;
  }

  if (context_->composer().GetInputFieldType() ==
      commands::Context::PASSWORD) {
    return false;
  }

  return context_->mutable_converter()->LearnExternalConversionResult(
      key, value, context_->client_context());
}

bool Session::HasVisibleZenzLiveCorrection() const {
  if (zenz_live_visible_generation_ == 0 ||
      zenz_live_key_.empty() ||
      zenz_live_value_.empty() ||
      zenz_live_mozc_value_.empty()) {
    return false;
  }

  if (!live_conversion_active_) {
    return false;
  }

  if (context_->state() != ImeContext::CONVERSION) {
    return false;
  }

  if (live_conversion_key_ != zenz_live_key_) {
    return false;
  }

  if (live_conversion_value_ != zenz_live_mozc_value_) {
    return false;
  }

  return true;
}

void Session::SetPendingZenzFeedbackAccepted(
    absl::string_view key,
    absl::string_view context_class,
    absl::string_view value) {
  if (!UseZenzFeedbackLearning(context_->GetConfig())) {
    return;
  }

  if (key.empty() || value.empty()) {
    return;
  }

  pending_zenz_feedback_.pending = true;
  pending_zenz_feedback_.action = PendingZenzFeedback::Action::kAccepted;
  pending_zenz_feedback_.key = std::string(key);
  pending_zenz_feedback_.context_class =
      context_class.empty() ? "empty" : std::string(context_class);
  pending_zenz_feedback_.value = std::string(value);
  pending_zenz_feedback_.reason.clear();

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] pending accepted ",
      ZenzRedactedTextStats("key", key),
      " ", ZenzRedactedTextStats("value", value),
      " context_class=", pending_zenz_feedback_.context_class));
}

void Session::SetPendingZenzFeedbackRejected(absl::string_view reason) {
  if (!UseZenzFeedbackLearning(context_->GetConfig())) {
    return;
  }

  if (!HasVisibleZenzLiveCorrection()) {
    return;
  }

  pending_zenz_feedback_.pending = true;
  pending_zenz_feedback_.action = PendingZenzFeedback::Action::kRejected;
  pending_zenz_feedback_.key = zenz_live_key_;
  pending_zenz_feedback_.context_class =
      zenz_live_context_class_.empty() ? "empty" : zenz_live_context_class_;
  pending_zenz_feedback_.value = zenz_live_value_;
  pending_zenz_feedback_.reason = std::string(reason);

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] pending rejected ",
      ZenzRedactedTextStats("key", pending_zenz_feedback_.key),
      " ", ZenzRedactedTextStats("value", pending_zenz_feedback_.value),
      " context_class=", pending_zenz_feedback_.context_class,
      " reason=", pending_zenz_feedback_.reason));
}

void Session::ConfirmPendingZenzFeedback() {
  if (!pending_zenz_feedback_.pending) {
    return;
  }

  if (!UseZenzFeedbackLearning(context_->GetConfig())) {
    pending_zenz_feedback_ = PendingZenzFeedback();
    return;
  }

  if (pending_zenz_feedback_.action ==
      PendingZenzFeedback::Action::kAccepted) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] confirm pending accepted ",
        ZenzRedactedTextStats("key", pending_zenz_feedback_.key),
        " ", ZenzRedactedTextStats("value", pending_zenz_feedback_.value),
        " context_class=", pending_zenz_feedback_.context_class));

    zenz_feedback_store_.RecordAccepted(
        pending_zenz_feedback_.key,
        pending_zenz_feedback_.context_class,
        pending_zenz_feedback_.value);

    const bool learned_to_mozc_history =
        MaybeLearnZenzCandidateToMozcHistory(
            pending_zenz_feedback_.key,
            pending_zenz_feedback_.value);

    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] mozc history learning ",
        ZenzBool(learned_to_mozc_history),
        " ", ZenzRedactedTextStats("key", pending_zenz_feedback_.key),
        " ", ZenzRedactedTextStats("value", pending_zenz_feedback_.value),
        " context_class=", pending_zenz_feedback_.context_class));
  } else if (pending_zenz_feedback_.action ==
             PendingZenzFeedback::Action::kRejected) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] confirm pending rejected ",
        ZenzRedactedTextStats("key", pending_zenz_feedback_.key),
        " ", ZenzRedactedTextStats("value", pending_zenz_feedback_.value),
        " context_class=", pending_zenz_feedback_.context_class,
        " reason=", pending_zenz_feedback_.reason));

    zenz_feedback_store_.RecordRejected(
        pending_zenz_feedback_.key,
        pending_zenz_feedback_.context_class,
        pending_zenz_feedback_.value,
        pending_zenz_feedback_.reason);
  }

  pending_zenz_feedback_ = PendingZenzFeedback();
}

void Session::DiscardPendingZenzFeedback(absl::string_view reason) {
  if (!pending_zenz_feedback_.pending) {
    return;
  }

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] discard pending feedback reason=", reason,
      " ", ZenzRedactedTextStats("key", pending_zenz_feedback_.key),
      " ", ZenzRedactedTextStats("value", pending_zenz_feedback_.value),
      " context_class=", pending_zenz_feedback_.context_class));

  pending_zenz_feedback_ = PendingZenzFeedback();
}

bool Session::SetPendingDirectCommitLearningFromCommittedResult(
    const commands::Command& command,
    absl::string_view reason) {
  if (!command.output().has_result()) {
    return false;
  }

  const commands::Result& result = command.output().result();
  if (result.key().empty() || result.value().empty()) {
    return false;
  }

  if (context_->composer().GetInputFieldType() ==
      commands::Context::PASSWORD) {
    return false;
  }

  pending_direct_commit_learning_.pending = true;
  pending_direct_commit_learning_.key = result.key();
  pending_direct_commit_learning_.value = result.value();
  pending_direct_commit_learning_.reason = std::string(reason);

  // Keep a snapshot of the converter state immediately after the normal
  // conversion commit.  The current converter will be reset later by
  // CommitStringDirectly(), so delayed cancellation must use this snapshot to
  // revert the original rich Mozc learning.
  pending_direct_commit_learning_.revert_context =
      std::make_unique<ImeContext>(*context_);

  ZenzDebugOutput(absl::StrCat(
      "[direct-commit-learning] pending delayed revert reason=", reason,
      " ", ZenzRedactedTextStats("key",
                                 pending_direct_commit_learning_.key),
      " ", ZenzRedactedTextStats("value",
                                 pending_direct_commit_learning_.value)));

  return true;
}

void Session::ConfirmPendingDirectCommitLearning(absl::string_view reason) {
  if (!pending_direct_commit_learning_.pending) {
    return;
  }

  ZenzDebugOutput(absl::StrCat(
      "[direct-commit-learning] confirm reason=", reason,
      " original_reason=", pending_direct_commit_learning_.reason,
      " ", ZenzRedactedTextStats("key",
                                 pending_direct_commit_learning_.key),
      " ", ZenzRedactedTextStats("value",
                                 pending_direct_commit_learning_.value)));

  pending_direct_commit_learning_ = PendingDirectCommitLearning();
}

void Session::DiscardPendingDirectCommitLearning(absl::string_view reason) {
  if (!pending_direct_commit_learning_.pending) {
    return;
  }

  if (pending_direct_commit_learning_.revert_context != nullptr) {
    pending_direct_commit_learning_.revert_context
        ->mutable_converter()
        ->Revert();
  }

  ZenzDebugOutput(absl::StrCat(
      "[direct-commit-learning] discard and revert reason=", reason,
      " original_reason=", pending_direct_commit_learning_.reason,
      " ", ZenzRedactedTextStats("key",
                                 pending_direct_commit_learning_.key),
      " ", ZenzRedactedTextStats("value",
                                 pending_direct_commit_learning_.value)));

  pending_direct_commit_learning_ = PendingDirectCommitLearning();
}

void Session::HandlePendingDirectCommitLearningForKeyEvent(
    const commands::KeyEvent& key) {
  if (!pending_direct_commit_learning_.pending) {
    return;
  }

  if (IsPendingDirectCommitLearningDiscardKey(key)) {
    DiscardPendingDirectCommitLearning("discard_key_after_direct_commit");
    return;
  }

  if (IsPureModifierKeyEvent(key)) {
    return;
  }

  ConfirmPendingDirectCommitLearning("next_real_key_after_direct_commit");
}

void Session::HandlePendingDirectCommitLearningForSessionCommand(
    commands::SessionCommand::CommandType type) {
  if (!pending_direct_commit_learning_.pending) {
    return;
  }

  switch (type) {
    case commands::SessionCommand::REVERT:
    case commands::SessionCommand::RESET_CONTEXT:
    case commands::SessionCommand::UNDO:
    case commands::SessionCommand::TURN_OFF_IME:
      DiscardPendingDirectCommitLearning(
          "session_command_discard_after_direct_commit");
      break;
    default:
      break;
  }
}

void Session::HandlePendingZenzFeedbackForKeyEvent(
    const commands::KeyEvent& key) {
  if (!pending_zenz_feedback_.pending) {
    return;
  }

  // This function is called only from InsertCharacter(), i.e. after the keymap
  // has already classified the event as text insertion.  Do not re-classify the
  // event here by key_string/key_code; some real romaji input events may have no
  // useful key_string.
  //
  // While still in conversion, keys such as Space, Enter, candidate movement,
  // and candidate shortcut selection are still part of deciding the current
  // conversion result. They must not confirm pending zenz feedback.
  if (context_->state() == ImeContext::CONVERSION) {
    return;
  }

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] confirm pending by InsertCharacter"
      " state=", static_cast<int>(context_->state()),
      " has_key_string=", ZenzBool(key.has_key_string()),
      " key_string_bytes=",
      key.has_key_string() ? key.key_string().size() : 0,
      " has_key_code=", ZenzBool(key.has_key_code()),
      " key_code=", key.has_key_code() ? key.key_code() : 0));

  ConfirmPendingZenzFeedback();
}

void Session::HandlePendingZenzFeedbackForSessionCommand(
    commands::SessionCommand::CommandType type) {
  switch (type) {
    case commands::SessionCommand::REVERT:
    case commands::SessionCommand::RESET_CONTEXT:
    case commands::SessionCommand::UNDO:
    case commands::SessionCommand::TURN_OFF_IME:
      DiscardPendingZenzFeedback("session_command_discard");
      break;
    default:
      break;
  }
}

void Session::CancelPendingZenzLiveCorrection() {
  ++zenz_live_generation_;
  pending_zenz_live_ = PendingZenzLiveCorrection();

  if (zenz_live_corrector_ != nullptr) {
    zenz_live_corrector_->CancelPending();
  }
}

void Session::ClearZenzLiveCorrectionState() {
  ++zenz_live_generation_;
  pending_zenz_live_ = PendingZenzLiveCorrection();

  if (zenz_live_corrector_ != nullptr) {
    zenz_live_corrector_->CancelPending();
  }

  zenz_live_visible_generation_ = 0;
  zenz_live_key_.clear();
  zenz_live_value_.clear();
  zenz_live_mozc_value_.clear();
  zenz_live_context_class_.clear();
  zenz_live_left_context_.clear();
  zenz_live_preedit_output_.Clear();
}

bool Session::MaybeApplyZenzFeedbackLiveCorrection(
    commands::Command* command) {
  const config::Config& config = context_->GetConfig();

  if (!UseZenzFeedbackLearning(config)) {
    return false;
  }

  if (!config.use_zenz_live_correction()) {
    return false;
  }

  if (!config.use_live_conversion()) {
    return false;
  }

  if (!live_conversion_active_) {
    return false;
  }

  if (context_->state() != ImeContext::CONVERSION) {
    return false;
  }

  if (context_->composer().GetInputFieldType() == commands::Context::PASSWORD) {
    return false;
  }

  if (live_conversion_key_.empty() || live_conversion_value_.empty()) {
    return false;
  }

  if (ContainsAsciiAlphabet(live_conversion_key_)) {
    return false;
  }

  const uint32_t min_key_len = GetZenzLiveCorrectionMinKeyLength(config);
  if (Util::CharsLen(live_conversion_key_) < min_key_len) {
    return false;
  }

  const uint32_t left_context_len =
      GetZenzLiveCorrectionLeftContextLength(config);

  const std::string raw_left_context =
      ExtractZenzLeftContext(left_context_len);
  const ZenzContextSanitizationResult context_result =
      zenz_context_sanitizer_.SanitizeForZenz(
          raw_left_context, left_context_len);

  const std::string left_context_for_validation =
      context_result.allowed_for_prompt
          ? context_result.sanitized_context
          : std::string();

  const std::string context_class =
      context_result.context_class.empty()
          ? std::string("empty")
          : context_result.context_class;

  const std::vector<ZenzFeedbackCandidate> feedback_candidates =
      zenz_feedback_store_.GetAcceptedCandidates(
          live_conversion_key_, context_class);

  if (feedback_candidates.empty()) {
    return false;
  }

  for (const ZenzFeedbackCandidate& feedback_candidate :
       feedback_candidates) {
    const std::string& feedback_value = feedback_candidate.value;

    ZenzValidationInput validation_input;
    validation_input.key = live_conversion_key_;
    validation_input.mozc_value = live_conversion_value_;
    validation_input.zenz_value = feedback_value;
    validation_input.left_context = left_context_for_validation;
    validation_input.min_key_length = min_key_len;
    validation_input.allow_synthetic_candidate =
        config.use_zenz_synthetic_candidate();

    const ZenzValidationResult validation =
        zenz_output_validator_.Validate(validation_input);

    if (!validation.accept) {
      ZenzDebugOutput(absl::StrCat(
          "[zenz-feedback] fast path candidate rejected reason=",
          validation.reason,
          " ", ZenzRedactedTextStats("key", live_conversion_key_),
          " ", ZenzRedactedTextStats("value", feedback_value),
          " context_class=", context_class,
          " accepted_count=", feedback_candidate.accepted_count,
          " rejected_count=", feedback_candidate.rejected_count));
      continue;
    }

    ++zenz_live_generation_;
    pending_zenz_live_ = PendingZenzLiveCorrection();

    zenz_live_visible_generation_ = zenz_live_generation_;
    zenz_live_key_ = live_conversion_key_;
    zenz_live_value_ = feedback_value;
    zenz_live_mozc_value_ = live_conversion_value_;
    zenz_live_context_class_ = context_class;
    zenz_live_left_context_ = left_context_for_validation;

    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] fast path applied ",
        ZenzRedactedTextStats("key", zenz_live_key_),
        " ", ZenzRedactedTextStats("value", zenz_live_value_),
        " ", ZenzRedactedTextStats("mozc_value", zenz_live_mozc_value_),
        " context_class=", zenz_live_context_class_,
        " accepted_count=", feedback_candidate.accepted_count,
        " rejected_count=", feedback_candidate.rejected_count));

    return OutputZenzLiveCorrection(feedback_value, command);
  }

  return false;
}

bool Session::MaybeScheduleZenzLiveCorrection(commands::Command* command) {
  const config::Config& config = context_->GetConfig();

  if (!config.use_zenz_live_correction()) {
    return false;
  }

  if (!config.use_live_conversion()) {
    return false;
  }

  if (!live_conversion_active_) {
    return false;
  }

  if (context_->state() != ImeContext::CONVERSION) {
    return false;
  }

  if (context_->composer().GetInputFieldType() == commands::Context::PASSWORD) {
    return false;
  }

  if (live_conversion_key_.empty() || live_conversion_value_.empty()) {
    return false;
  }

  if (ContainsAsciiAlphabet(live_conversion_key_)) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] skip ascii ",
        ZenzRedactedTextStats("key", live_conversion_key_)));
    return false;
  }

  const uint32_t min_key_len = GetZenzLiveCorrectionMinKeyLength(config);
  if (Util::CharsLen(live_conversion_key_) < min_key_len) {
    return false;
  }

  const uint32_t left_context_len =
      GetZenzLiveCorrectionLeftContextLength(config);

  const std::string raw_left_context =
      ExtractZenzLeftContext(left_context_len);
  const ZenzContextSanitizationResult context_result =
      zenz_context_sanitizer_.SanitizeForZenz(
          raw_left_context, left_context_len);

  const std::string left_context_for_prompt =
      context_result.allowed_for_prompt
          ? context_result.sanitized_context
          : std::string();

  ZenzPromptBuilder prompt_builder;
  const std::string prompt =
      prompt_builder.Build(left_context_for_prompt, live_conversion_key_);

  ++zenz_live_generation_;

  pending_zenz_live_.generation = zenz_live_generation_;
  pending_zenz_live_.key = live_conversion_key_;
  pending_zenz_live_.left_context = left_context_for_prompt;
  pending_zenz_live_.context_class = context_result.context_class;
  pending_zenz_live_.mozc_value = live_conversion_value_;
  pending_zenz_live_.prompt = prompt;
  pending_zenz_live_.issued_at = Clock::GetAbslTime();
  pending_zenz_live_.pending = true;
  pending_zenz_live_.submitted = false;
  pending_zenz_live_.poll_count = 0;

  ZenzDebugOutput(absl::StrCat(
      "[zenz] scheduled ",
      ZenzRedactedTextStats("key", live_conversion_key_),
      " ", ZenzRedactedTextStats("mozc_value", live_conversion_value_),
      " context_class=", context_result.context_class,
      " context_allowed=", ZenzBool(context_result.allowed_for_prompt),
      " context_reason=", context_result.reason));

  AttachZenzLiveCorrectionStartCallback(command);
  command->mutable_output()->set_zenz_live_correction_pending(true);
  return true;
}

void Session::AttachZenzLiveCorrectionStartCallback(
    commands::Command* command) const {
  commands::Output::Callback* callback =
      command->mutable_output()->mutable_callback();
  commands::SessionCommand* session_command =
      callback->mutable_session_command();

  session_command->set_type(
      commands::SessionCommand::APPLY_ZENZ_LIVE_CORRECTION);
  session_command->set_live_conversion_generation(
      pending_zenz_live_.generation);
  session_command->set_live_conversion_key(pending_zenz_live_.key);

  callback->set_delay_millisec(
      GetZenzLiveCorrectionDelayMsec(context_->GetConfig()));
}

void Session::AttachZenzLiveCorrectionPollCallback(
    commands::Command* command) const {
  commands::Output::Callback* callback =
      command->mutable_output()->mutable_callback();
  commands::SessionCommand* session_command =
      callback->mutable_session_command();

  session_command->set_type(
      commands::SessionCommand::APPLY_ZENZ_LIVE_CORRECTION);
  session_command->set_live_conversion_generation(
      pending_zenz_live_.generation);
  session_command->set_live_conversion_key(pending_zenz_live_.key);

  callback->set_delay_millisec(kDefaultZenzLiveCorrectionPollMsec);
}

bool Session::IsCurrentZenzLiveCorrectionCallback(
    const commands::Command& command) const {
  if (!pending_zenz_live_.pending) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] stale reason=no_pending_zenz",
        " ", ZenzRedactedTextStats("live_key", live_conversion_key_),
        " ", ZenzRedactedTextStats("live_value", live_conversion_value_),
        " ", ZenzRedactedTextStats("zenz_key", zenz_live_key_),
        " ", ZenzRedactedTextStats("zenz_value", zenz_live_value_),
        " state=", static_cast<int>(context_->state())));
    return false;
  }

  if (!command.input().has_command()) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] stale reason=no_input_command"
        " pending_gen=", pending_zenz_live_.generation,
        " ", ZenzRedactedTextStats("pending_key", pending_zenz_live_.key),
        " ", ZenzRedactedTextStats("live_key", live_conversion_key_),
        " ", ZenzRedactedTextStats("live_value", live_conversion_value_),
        " context_class=", pending_zenz_live_.context_class,
        " state=", static_cast<int>(context_->state())));
    return false;
  }

  const commands::SessionCommand& session_command = command.input().command();

  if (!session_command.has_live_conversion_generation() ||
      session_command.live_conversion_generation() !=
          pending_zenz_live_.generation) {
    const std::string callback_gen =
        session_command.has_live_conversion_generation()
            ? absl::StrCat(session_command.live_conversion_generation())
            : "(none)";
    const std::string callback_key =
        session_command.has_live_conversion_key()
            ? session_command.live_conversion_key()
            : std::string();

    ZenzDebugOutput(absl::StrCat(
        "[zenz] stale reason=generation_mismatch"
        " callback_gen=", callback_gen,
        " pending_gen=", pending_zenz_live_.generation,
        " ", ZenzRedactedTextStats("callback_key", callback_key),
        " ", ZenzRedactedTextStats("pending_key", pending_zenz_live_.key),
        " ", ZenzRedactedTextStats("live_key", live_conversion_key_),
        " ", ZenzRedactedTextStats("live_value", live_conversion_value_),
        " ", ZenzRedactedTextStats("pending_value",
                                    pending_zenz_live_.mozc_value),
        " context_class=", pending_zenz_live_.context_class,
        " state=", static_cast<int>(context_->state())));
    return false;
  }

  if (!session_command.has_live_conversion_key() ||
      session_command.live_conversion_key() != pending_zenz_live_.key) {
    const std::string callback_key =
        session_command.has_live_conversion_key()
            ? session_command.live_conversion_key()
            : std::string();

    ZenzDebugOutput(absl::StrCat(
        "[zenz] stale reason=callback_key_mismatch"
        " ", ZenzRedactedTextStats("callback_key", callback_key),
        " ", ZenzRedactedTextStats("pending_key", pending_zenz_live_.key),
        " pending_gen=", pending_zenz_live_.generation,
        " ", ZenzRedactedTextStats("live_key", live_conversion_key_),
        " ", ZenzRedactedTextStats("live_value", live_conversion_value_),
        " ", ZenzRedactedTextStats("pending_value",
                                    pending_zenz_live_.mozc_value),
        " context_class=", pending_zenz_live_.context_class,
        " state=", static_cast<int>(context_->state())));
    return false;
  }

  if (context_->state() != ImeContext::CONVERSION) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] stale reason=state_not_conversion"
        " state=", static_cast<int>(context_->state()),
        " pending_gen=", pending_zenz_live_.generation,
        " ", ZenzRedactedTextStats("pending_key", pending_zenz_live_.key),
        " ", ZenzRedactedTextStats("live_key", live_conversion_key_),
        " ", ZenzRedactedTextStats("live_value", live_conversion_value_),
        " ", ZenzRedactedTextStats("pending_value",
                                    pending_zenz_live_.mozc_value),
        " context_class=", pending_zenz_live_.context_class));
    return false;
  }

  if (!live_conversion_active_) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] stale reason=live_conversion_not_active"
        " pending_gen=", pending_zenz_live_.generation,
        " ", ZenzRedactedTextStats("pending_key", pending_zenz_live_.key),
        " ", ZenzRedactedTextStats("live_key", live_conversion_key_),
        " ", ZenzRedactedTextStats("live_value", live_conversion_value_),
        " ", ZenzRedactedTextStats("pending_value",
                                    pending_zenz_live_.mozc_value),
        " context_class=", pending_zenz_live_.context_class,
        " state=", static_cast<int>(context_->state())));
    return false;
  }

  if (live_conversion_key_ != pending_zenz_live_.key) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] stale reason=live_key_mismatch",
        " pending_gen=", pending_zenz_live_.generation,
        " ", ZenzRedactedTextStats("live_key", live_conversion_key_),
        " ", ZenzRedactedTextStats("pending_key", pending_zenz_live_.key),
        " ", ZenzRedactedTextStats("live_value", live_conversion_value_),
        " ", ZenzRedactedTextStats("pending_value",
                                    pending_zenz_live_.mozc_value),
        " context_class=", pending_zenz_live_.context_class,
        " state=", static_cast<int>(context_->state())));
    return false;
  }

  if (live_conversion_value_ != pending_zenz_live_.mozc_value) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] stale reason=live_value_mismatch"
        " ", ZenzRedactedTextStats("live_key", live_conversion_key_),
        " ", ZenzRedactedTextStats("pending_key", pending_zenz_live_.key),
        " pending_gen=", pending_zenz_live_.generation,
        " ", ZenzRedactedTextStats("live_value", live_conversion_value_),
        " ", ZenzRedactedTextStats("pending_value",
                                    pending_zenz_live_.mozc_value),
        " context_class=", pending_zenz_live_.context_class,
        " state=", static_cast<int>(context_->state())));
    return false;
  }

  return true;
}

bool Session::OutputCurrentLiveConversionWithZenzPending(
    commands::Command* command) {
  command->mutable_output()->set_consumed(true);

  if (live_conversion_active_ && context_->state() == ImeContext::CONVERSION) {
    Output(command);
    commands::Output* output = command->mutable_output();
    output->set_live_conversion(true);
    output->set_live_conversion_pending(false);
    output->set_zenz_live_correction_pending(true);
    return true;
  }

  OutputFromState(command);
  return true;
}

bool Session::OutputCurrentLiveConversionAfterZenzStop(
    commands::Command* command,
    absl::string_view debug) {
  command->mutable_output()->set_consumed(true);

  if (live_conversion_active_ && context_->state() == ImeContext::CONVERSION) {
    Output(command);

    commands::Output* output = command->mutable_output();
    output->set_live_conversion(true);
    output->set_live_conversion_pending(false);
    output->set_zenz_live_correction_pending(false);
    if (!debug.empty()) {
      output->set_zenz_live_correction_debug(std::string(debug));
    }
    return true;
  }

  OutputFromState(command);
  if (!debug.empty()) {
    command->mutable_output()->set_zenz_live_correction_debug(
        std::string(debug));
  }
  return true;
}

ZenzLiveCorrector* Session::EnsureZenzLiveCorrector() {
  if (zenz_live_corrector_ == nullptr) {
    zenz_live_corrector_ = std::make_unique<ZenzLiveCorrector>(
        CreateDefaultZenzClient());
  }
  return zenz_live_corrector_.get();
}

bool Session::ApplyZenzLiveCorrection(commands::Command* command) {
  ZenzDebugOutput("[zenz] ApplyZenzLiveCorrection called");
  command->mutable_output()->set_consumed(true);

  if (!IsCurrentZenzLiveCorrectionCallback(*command)) {
    ZenzDebugOutput("[zenz] stale zenz callback");
    return IgnoreStaleDelayedLiveConversion(command);
  }

  const config::Config& config = context_->GetConfig();
  const absl::Time now = Clock::GetAbslTime();
  const uint32_t timeout_msec = GetZenzLiveCorrectionTimeoutMsec(config);

  if (!pending_zenz_live_.submitted) {
    pending_zenz_live_.issued_at = now;
    pending_zenz_live_.submitted = true;
    pending_zenz_live_.poll_count = 0;

    ZenzLiveRequest request;
    request.generation = pending_zenz_live_.generation;
    request.key = pending_zenz_live_.key;
    request.prompt = pending_zenz_live_.prompt;
    request.left_context = pending_zenz_live_.left_context;
    request.mozc_value = pending_zenz_live_.mozc_value;
    request.pipe_name = config.zenz_live_correction_pipe_name();
    request.timeout_msec = timeout_msec;
    request.max_output_chars = 256;
    request.issued_at = pending_zenz_live_.issued_at;

    ZenzPromptBuilder prompt_builder;
    request.reading_katakana =
        prompt_builder.HiraganaToKatakana(pending_zenz_live_.key);

    ZenzDebugOutput(absl::StrCat(
        "[zenz] async submit pipe_configured=",
        config.zenz_live_correction_pipe_name().empty() ? "false" : "true",
        " generation=", pending_zenz_live_.generation,
        " ", ZenzRedactedTextStats("key", pending_zenz_live_.key),
        " ", ZenzRedactedTextStats("mozc_value",
                                    pending_zenz_live_.mozc_value),
        " context_class=", pending_zenz_live_.context_class,
        " ", ZenzRedactedTextStats("context",
                                    pending_zenz_live_.left_context)));

    EnsureZenzLiveCorrector()->Submit(std::move(request));

    const bool result = OutputCurrentLiveConversionWithZenzPending(command);
    AttachZenzLiveCorrectionPollCallback(command);
    return result;
  }

  if (zenz_live_corrector_ == nullptr) {
    ZenzDebugOutput("[zenz] async corrector missing");
    CancelPendingZenzLiveCorrection();
    return OutputCurrentLiveConversionAfterZenzStop(
        command, "zenz_async_corrector_missing");
  }

  std::optional<ZenzLiveResponse> response =
      zenz_live_corrector_->TakeResult(pending_zenz_live_.generation);

  if (response.has_value()) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] async response ok=", ZenzBool(response->ok),
        " timeout=", ZenzBool(response->timeout),
        " generation=", response->generation,
        " ", ZenzRedactedTextStats("value", response->value),
        " ", ZenzRedactedTextStats("debug", response->debug)));

    return ApplyZenzLiveCorrectionResult(*response, command);
  }

  ++pending_zenz_live_.poll_count;

  const uint32_t async_wait_msec =
      std::max<uint32_t>(timeout_msec, kZenzLiveCorrectionAsyncWaitMsec);

  const uint32_t max_poll_count =
      std::max<uint32_t>(
          1,
          async_wait_msec / kDefaultZenzLiveCorrectionPollMsec + 2);

  const bool timed_out =
      now - pending_zenz_live_.issued_at >=
      absl::Milliseconds(async_wait_msec);

  if (timed_out || pending_zenz_live_.poll_count >= max_poll_count) {
    const std::string reason =
        timed_out ? "zenz_async_timeout" : "zenz_async_poll_exhausted";

    ZenzDebugOutput(absl::StrCat(
        "[zenz] async no result reason=", reason,
        " generation=", pending_zenz_live_.generation,
        " poll_count=", pending_zenz_live_.poll_count,
        " timeout_msec=", timeout_msec,
        " async_wait_msec=", async_wait_msec,
        " ", ZenzRedactedTextStats("key", pending_zenz_live_.key),
        " ", ZenzRedactedTextStats("mozc_value",
                                    pending_zenz_live_.mozc_value),
        " context_class=", pending_zenz_live_.context_class));

    CancelPendingZenzLiveCorrection();
    return OutputCurrentLiveConversionAfterZenzStop(command, reason);
  }

  ZenzDebugOutput(absl::StrCat(
      "[zenz] async pending generation=", pending_zenz_live_.generation,
      " poll_count=", pending_zenz_live_.poll_count,
      " ", ZenzRedactedTextStats("key", pending_zenz_live_.key),
      " context_class=", pending_zenz_live_.context_class));

  const bool result = OutputCurrentLiveConversionWithZenzPending(command);
  AttachZenzLiveCorrectionPollCallback(command);
  return result;
}

bool Session::ApplyZenzLiveCorrectionResult(
    const ZenzLiveResponse& response,
    commands::Command* command) {
  const config::Config& config = context_->GetConfig();

  if (!response.ok || response.timeout) {
    const std::string debug =
        response.debug.empty()
            ? "zenz_response_not_ok"
            : ZenzSafeDebugReason(response.debug);

    CancelPendingZenzLiveCorrection();
    Output(command);
    command->mutable_output()->set_live_conversion(true);
    command->mutable_output()->set_live_conversion_pending(false);
    command->mutable_output()->set_zenz_live_correction_pending(false);
    command->mutable_output()->set_zenz_live_correction_debug(debug);
    return true;
  }

  std::string zenz_value = response.value;

  // zenz sometimes returns the sanitized left context together with the current
  // conversion result. The preedit should contain only the current composition
  // result, so strip the already-known context prefix.
  if (!pending_zenz_live_.left_context.empty() &&
      StartsWithString(zenz_value, pending_zenz_live_.left_context)) {
    zenz_value.erase(0, pending_zenz_live_.left_context.size());
    ZenzDebugOutput(absl::StrCat(
        "[zenz] stripped left_context prefix ",
        ZenzRedactedTextStats("value", zenz_value),
        " context_class=", pending_zenz_live_.context_class));
  }

  const std::string context_class =
      pending_zenz_live_.context_class.empty()
          ? BuildZenzFeedbackContextClass(pending_zenz_live_.left_context)
          : pending_zenz_live_.context_class;

  ZenzValidationInput validation_input;
  validation_input.key = pending_zenz_live_.key;
  validation_input.mozc_value = pending_zenz_live_.mozc_value;
  validation_input.zenz_value = zenz_value;
  validation_input.left_context = pending_zenz_live_.left_context;
  validation_input.min_key_length = GetZenzLiveCorrectionMinKeyLength(config);
  validation_input.allow_synthetic_candidate =
      config.use_zenz_synthetic_candidate();

  const ZenzValidationResult validation =
      zenz_output_validator_.Validate(validation_input);

  if (!validation.accept) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] validation rejected reason=", validation.reason,
        " ", ZenzRedactedTextStats("value", zenz_value),
        " ", ZenzRedactedTextStats("mozc_value",
                                    pending_zenz_live_.mozc_value),
        " context_class=", context_class));

    CancelPendingZenzLiveCorrection();
    Output(command);
    command->mutable_output()->set_live_conversion(true);
    command->mutable_output()->set_live_conversion_pending(false);
    command->mutable_output()->set_zenz_live_correction_pending(false);
    command->mutable_output()->set_zenz_live_correction_debug(
        validation.reason);
    return true;
  }

  std::string feedback_reason = "feedback_learning_disabled";

  if (UseZenzFeedbackLearning(config)) {
    const ZenzFeedbackDecision feedback_decision =
        zenz_feedback_store_.Decide(
            pending_zenz_live_.key, context_class, zenz_value);

    feedback_reason = feedback_decision.reason;

    if (feedback_decision.action == ZenzFeedbackAction::kReject) {
      ZenzDebugOutput(absl::StrCat(
          "[zenz] feedback rejected reason=", feedback_decision.reason,
          " ", ZenzRedactedTextStats("value", zenz_value),
          " context_class=", context_class,
          " accepted_count=", feedback_decision.accepted_count,
          " rejected_count=", feedback_decision.rejected_count));

      CancelPendingZenzLiveCorrection();
      Output(command);
      command->mutable_output()->set_live_conversion(true);
      command->mutable_output()->set_live_conversion_pending(false);
      command->mutable_output()->set_zenz_live_correction_pending(false);
      command->mutable_output()->set_zenz_live_correction_debug(
          feedback_decision.reason);
      return true;
    }
  }

  ZenzDebugOutput(absl::StrCat(
      "[zenz] validation accepted ",
      ZenzRedactedTextStats("value", zenz_value),
      " ", ZenzRedactedTextStats("old_mozc_value",
                                  pending_zenz_live_.mozc_value),
      " context_class=", context_class,
      " feedback=", feedback_reason));

  zenz_live_visible_generation_ = pending_zenz_live_.generation;
  zenz_live_key_ = pending_zenz_live_.key;
  zenz_live_value_ = zenz_value;
  zenz_live_mozc_value_ = pending_zenz_live_.mozc_value;
  zenz_live_context_class_ = context_class.empty() ? "empty" : context_class;
  zenz_live_left_context_ = pending_zenz_live_.left_context;
  pending_zenz_live_.pending = false;

  return OutputZenzLiveCorrection(zenz_value, command);
}

bool Session::OutputZenzLiveCorrection(
    absl::string_view value,
    commands::Command* command) {
  command->mutable_output()->set_consumed(true);

  commands::Output* output = command->mutable_output();
  output->clear_candidate_window();
  output->set_live_conversion(true);
  output->set_live_conversion_pending(false);
  output->set_zenz_live_correction_pending(false);
  output->set_zenz_live_correction_applied(true);

  commands::Preedit* preedit = output->mutable_preedit();
  preedit->Clear();

  AddPreeditSegment(
      zenz_live_key_,
      value,
      commands::Preedit::Segment::HIGHLIGHT,
      preedit);

  preedit->set_cursor(Util::CharsLen(value));

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] output zenz correction ",
      ZenzRedactedTextStats("key", zenz_live_key_),
      " ", ZenzRedactedTextStats("value", value),
      " context_class=", zenz_live_context_class_));

  // Do not record acceptance here. Displaying a zenz correction is not the same
  // as user acceptance. Acceptance must be recorded only when the user commits
  // the visible zenz result.
  zenz_live_preedit_output_ = *preedit;
  return true;
}

bool Session::CommitZenzLiveCorrectionResult(commands::Command* command) {
  if (!HasVisibleZenzLiveCorrection()) {
    return false;
  }

  if (!(context_->state() &
        (ImeContext::COMPOSITION | ImeContext::CONVERSION))) {
    return false;
  }

  const std::string key = zenz_live_key_;
  const std::string value = zenz_live_value_;
  const std::string context_class =
      zenz_live_context_class_.empty() ? "empty" : zenz_live_context_class_;

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] CommitZenzLiveCorrectionResult ",
      ZenzRedactedTextStats("key", key),
      " ", ZenzRedactedTextStats("value", value),
      " context_class=", context_class,
      " visible_generation=", zenz_live_visible_generation_));

  SetPendingZenzFeedbackAccepted(key, context_class, value);

  ClearLiveConversionState();
  CommitStringDirectly(key, value, command);
  return true;
}

bool Session::CommitLiveConversionResult(commands::Command* command) {
  if (context_->state() != ImeContext::COMPOSITION) {
    return false;
  }

  if (live_conversion_value_.empty()) {
    CommitCompositionDirectly(command);
    return true;
  }

  const size_t length = context_->composer().GetLength();
  if (length == 0) {
    CommitCompositionDirectly(command);
    return true;
  }

  const std::string preedit = context_->composer().GetStringForPreedit();
  const std::string last_char(
      Util::Utf8SubString(preedit, length - 1, 1));

  std::string key = context_->composer().GetQueryForConversion();
  std::string value = live_conversion_value_;
  value.append(last_char);

  ClearLiveConversionState();

  CommitStringDirectly(key, value, command);
  return true;
}

bool Session::CommitPendingLiveConversionDisplayDirectly(
    commands::Command* command) {
  const std::string key = context_->composer().GetQueryForConversion();
  const std::string raw_preedit = context_->composer().GetStringForPreedit();

  std::string value;

  const bool has_stable_live_conversion =
      !live_conversion_key_.empty() &&
      !live_conversion_preedit_.empty() &&
      !live_conversion_value_.empty() &&
      live_conversion_preedit_output_.segment_size() > 0;

  if (has_stable_live_conversion &&
      StartsWithString(key, live_conversion_key_) &&
      StartsWithString(raw_preedit, live_conversion_preedit_)) {
    const std::string suffix_value =
        raw_preedit.substr(live_conversion_preedit_.size());

    value = live_conversion_value_;
    value.append(suffix_value);
  } else {
    value = context_->composer().GetStringForSubmission();
  }

  ClearLiveConversionState();
  CommitStringDirectly(key, value, command);
  return true;
}

void Session::set_client_capability(commands::Capability capability) {
  *context_->mutable_client_capability() = std::move(capability);
}

void Session::set_application_info(commands::ApplicationInfo application_info) {
  *context_->mutable_application_info() = std::move(application_info);
}

const commands::ApplicationInfo& Session::application_info() const {
  return context_->application_info();
}

absl::Time Session::create_session_time() const {
  return context_->create_time();
}

absl::Time Session::last_command_time() const {
  return context_->last_command_time();
}

bool Session::InsertCharacter(commands::Command* command) {
  if (!command->input().has_key()) {
    LOG(ERROR) << "No key event: " << command->input();
    return false;
  }

  const commands::KeyEvent& key = command->input().key();

  // A pending direct-commit learning entry is finalized only when the next real
  // text input starts. If the next key is Backspace/Escape, it is discarded.
  HandlePendingDirectCommitLearningForKeyEvent(key);

  // A pending zenz feedback entry is finalized only when the next real text
  // input starts. This prevents learning immediately on Enter/Space, while still
  // learning once the user continues typing after the committed result.
  HandlePendingZenzFeedbackForKeyEvent(key);

  if (key.input_style() == commands::KeyEvent::DIRECT_INPUT &&
      context_->state() == ImeContext::PRECOMPOSITION) {
    // If the key event represents a half width ascii character (ie.
    // key_code is equal to key_string), that key event is not
    // consumed and done echo back.
    // We must not call |EchoBackAndClearUndoContext| for a half-width space
    // here because it should be done in Session::TestSendKey or
    // Session::InsertSpaceHalfWidth. Note that the |key| comes from
    // Session::InsertSpaceHalfWidth and Session::InsertSpaceFullWidth is
    // different from the original key event.
    // For example, when the client sends a key command like
    //   {key.special_key(): HENKAN, key.modifier_keys(): [SHIFT]},
    // Session::InsertSpaceHalfWidth replaces it with
    //   {key.key_string(): " ", key.key_code(): ' '}
    // when you assign [Shift+HENKAN] to [InsertSpaceHalfWidth].
    // So |key.key_code() == ' '| does not always mean that the original key is
    // a space key w/o any modifier.
    // This is why we cannot call |EchoBackAndClearUndoContext| when
    // |key.key_code() == ' '|. This issue was found in b/5872031.
    if (key.key_string().size() == 1 && key.key_code() == key.key_string()[0] &&
        key.key_code() != ' ') {
      return EchoBackAndClearUndoContext(command);
    }

    context_->mutable_composer()->InsertCharacterKeyEvent(key);
    CommitCompositionDirectly(command);
    ClearUndoContext();  // UndoContext must be invalidated.
    return true;
  }

  command->mutable_output()->set_consumed(true);

  // If a direct-commit punctuation/symbol is typed while delayed live conversion
  // is pending, commit the currently visible pending preedit. Do not materialize
  // the pending conversion here, because that would commit a conversion result
  // that has not been shown to the user yet.
  if (live_conversion_pending_ &&
      CanDirectCommitPendingLiveConversionBeforeInsert(key)) {
    context_->mutable_composer()->InsertCharacterKeyEvent(key);
    ClearUndoContext();

    if (CanDirectCommitAfterPunctuation(key)) {
      return CommitPendingLiveConversionDisplayDirectly(command);
    }

    // Defensive fallback. The pre-insert predicate accepted the key as a direct
    // commit trigger, so this path should normally not be reached. Avoid inserting
    // the same key twice.
    CancelPendingLiveConversion();
    OutputComposition(command);
    return true;
  }

  const bool was_live_conversion = live_conversion_active_;

  // Preserve the visible zenz correction before editing cancels the temporary
  // live conversion state.
  //
  // Continuing to type after a visible zenz correction is not necessarily an
  // explicit acceptance.  Therefore do not record positive feedback here.
  // Positive feedback is recorded only on explicit commit paths such as Enter
  // and direct-commit punctuation.
  const bool had_visible_zenz_correction =
      HasVisibleZenzLiveCorrection();

  const std::string zenz_key_before_edit = zenz_live_key_;
  const std::string zenz_value_before_edit = zenz_live_value_;
  const std::string zenz_context_class_before_edit =
      zenz_live_context_class_.empty() ? "empty" : zenz_live_context_class_;

  // If the current conversion was started by live conversion, ordinary
  // character input should continue editing the composition.  So cancel
  // the temporary conversion before handling candidate shortcuts.
  CancelLiveConversionForEditing();

  // Handle shortcut keys selecting a candidate from a list.
  if (MaybeSelectCandidate(command)) {
    Output(command);
    return true;
  }

  const std::string composition = context_->composer().GetQueryForConversion();
  bool should_commit = (context_->state() == ImeContext::CONVERSION);

  if (context_->GetRequest().space_on_alphanumeric() ==
          commands::Request::SPACE_OR_CONVERT_COMMITTING_COMPOSITION &&
      context_->state() == ImeContext::COMPOSITION &&
      // TODO(komatsu): Support FullWidthSpace
      composition.ends_with(' ')) {
    should_commit = true;
  }

  bool committed_conversion_before_insert = false;

  if (should_commit) {
    CommitNotTriggeringZeroQuerySuggest(command);
    committed_conversion_before_insert = true;

    if (key.input_style() == commands::KeyEvent::DIRECT_INPUT) {
      // Do ClearUndoContext() because it is a direct input.
      ClearUndoContext();
      context_->mutable_composer()->InsertCharacterKeyEvent(key);
      CommitCompositionDirectly(command);
      return true;
    }
  }

  context_->mutable_composer()->InsertCharacterKeyEvent(key);
  ClearUndoContext();

  if (!was_live_conversion && !live_conversion_pending_ &&
      context_->composer().GetLength() == 1) {
    // A new composition must not reuse the stable prefix from a previous
    // live conversion.
    live_conversion_key_.clear();
    live_conversion_preedit_.clear();
    live_conversion_value_.clear();
    live_conversion_preedit_output_.Clear();
    ClearZenzLiveCorrectionState();
  }

  if (CanDirectCommitAfterPunctuation(key)) {
    if (had_visible_zenz_correction) {
      const size_t length = context_->composer().GetLength();
      const std::string preedit = context_->composer().GetStringForPreedit();
      const std::string last_char(
          Util::Utf8SubString(preedit, length - 1, 1));

      std::string commit_key = context_->composer().GetQueryForConversion();
      std::string commit_value = zenz_value_before_edit;
      commit_value.append(last_char);

      ZenzDebugOutput(absl::StrCat(
          "[zenz-feedback] direct commit zenz with punctuation ",
          ZenzRedactedTextStats("key", zenz_key_before_edit),
          " ", ZenzRedactedTextStats("value", zenz_value_before_edit),
          " suffix_chars=", Util::CharsLen(last_char)));

      // Direct-commit punctuation is an explicit commit path, but keep the
      // feedback pending until the next real text input.  If the next action is
      // Backspace/Escape, the feedback is discarded.
      SetPendingZenzFeedbackAccepted(
          zenz_key_before_edit,
          zenz_context_class_before_edit,
          zenz_value_before_edit);

      ClearLiveConversionState();
      CommitStringDirectly(commit_key, commit_value, command);
      return true;
    }

    if (was_live_conversion && CommitLiveConversionResult(command)) {
      return true;
    }

    if (committed_conversion_before_insert) {
      SetPendingDirectCommitLearningFromCommittedResult(
          *command,
          "normal_conversion_direct_commit_punctuation");
    }

    CommitCompositionDirectly(command);
    return true;
  }

  if (context_->mutable_composer()->ShouldCommit()) {
    CommitCompositionDirectly(command);
    return true;
  }
  size_t length_to_commit = 0;
  if (context_->composer().ShouldCommitHead(&length_to_commit)) {
    return CommitHead(length_to_commit, command);
  }

  SetSessionState(ImeContext::COMPOSITION, context_.get());
  if (CanStartAutoConversion(key)) {
    CancelPendingLiveConversion();
    return Convert(command);
  }

  if (MaybeScheduleLiveConversion(command)) {
    return true;
  }

  if (Suggest(command->input())) {
    Output(command);
    return true;
  }

  OutputComposition(command);
  return true;
}

bool Session::IsFullWidthInsertSpace(const commands::Input& input) const {
  // If IME is off, any space has to be half-width.
  if (context_->state() == ImeContext::DIRECT) {
    return false;
  }

  // In this method, we should not update the actual input mode stored in
  // the composer even when |input| has a new input mode. Note that this
  // method can be called from TestSendKey, where internal input mode is
  // is not expected to be changed. This is one of the reasons why this
  // method is a const method.
  // On the other hand, this method should behave as if the new input mode
  // in |input| was applied. For example, this method should behave as if
  // the current input mode was HALF_KATAKANA in the following situation.
  //   composer's input mode: HIRAGANA
  //   input.key().mode()   : HALF_KATAKANA
  // To achieve this, we create a temporary composer object to which the
  // new input mode will be stored when |input| has a new input mode.
  auto get_input_mode = [this, &input]() {
    const bool has_mode = (input.has_key() && input.key().has_mode());
    if (!has_mode) {
      return context_->composer().GetInputMode();
    }

    // Copy the current composer state just in case.
    composer::Composer temporary_composer = context_->composer();
    ApplyCompositionMode(input.key().mode(), &temporary_composer);
    // Refer to this temporary composer in this method.
    return temporary_composer.GetInputMode();
  };

  // Check the current config and the current input status.
  bool is_full_width = false;
  switch (context_->GetConfig().space_character_form()) {
    case config::Config::FUNDAMENTAL_INPUT_MODE: {
      const transliteration::TransliterationType input_mode = get_input_mode();
      if (transliteration::T13n::IsInHalfAsciiTypes(input_mode) ||
          transliteration::T13n::IsInHalfKatakanaTypes(input_mode)) {
        is_full_width = false;
      } else {
        is_full_width = true;
      }
      break;
    }
    case config::Config::FUNDAMENTAL_FULL_WIDTH:
      is_full_width = true;
      break;
    case config::Config::FUNDAMENTAL_HALF_WIDTH:
      is_full_width = false;
      break;
    default:
      LOG(WARNING) << "Unknown input mode";
      is_full_width = false;
      break;
  }

  return is_full_width;
}

bool Session::InsertSpace(commands::Command* command) {
  if (IsFullWidthInsertSpace(command->input())) {
    return InsertSpaceFullWidth(command);
  } else {
    return InsertSpaceHalfWidth(command);
  }
}

bool Session::InsertSpaceToggled(commands::Command* command) {
  if (IsFullWidthInsertSpace(command->input())) {
    return InsertSpaceHalfWidth(command);
  } else {
    return InsertSpaceFullWidth(command);
  }
}

bool Session::InsertSpaceHalfWidth(commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::COMPOSITION |
         ImeContext::CONVERSION))) {
    return DoNothing(command);
  }

  if (context_->state() == ImeContext::PRECOMPOSITION) {
    // TODO(komatsu): This is a hack to work around the problem with
    // the inconsistency between TestSendKey and SendKey.
    if (IsPureSpaceKey(command->input().key())) {
      return EchoBackAndClearUndoContext(command);
    }
    // UndoContext will be cleared in |InsertCharacter| in this case.
  }

  const bool has_mode = command->input().key().has_mode();
  const commands::CompositionMode mode = command->input().key().mode();
  command->mutable_input()->clear_key();
  commands::KeyEvent* key_event = command->mutable_input()->mutable_key();
  key_event->set_key_code(' ');
  key_event->set_key_string(" ");
  key_event->set_input_style(commands::KeyEvent::DIRECT_INPUT);
  if (has_mode) {
    key_event->set_mode(mode);
  }
  return InsertCharacter(command);
}

bool Session::InsertSpaceFullWidth(commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::COMPOSITION |
         ImeContext::CONVERSION))) {
    return DoNothing(command);
  }

  if (context_->state() == ImeContext::PRECOMPOSITION) {
    // UndoContext will be cleared in |InsertCharacter| in this case.

    // TODO(komatsu): make sure if
    // |context_->mutable_converter()->Reset()| is necessary here.
    context_->mutable_converter()->Reset();
  }

  const bool has_mode = command->input().key().has_mode();
  const commands::CompositionMode mode = command->input().key().mode();
  command->mutable_input()->clear_key();
  commands::KeyEvent* key_event = command->mutable_input()->mutable_key();
  key_event->set_key_code(' ');
  key_event->set_key_string("　");  // full-width space
  key_event->set_input_style(commands::KeyEvent::DIRECT_INPUT);
  if (has_mode) {
    key_event->set_mode(mode);
  }
  return InsertCharacter(command);
}

bool Session::TryCancelConvertReverse(commands::Command* command) {
  // If source_text is set, it usually means this session started by a
  // reverse conversion.
  if (context_->composer().source_text().empty()) {
    return false;
  }
  CommitSourceTextDirectly(command);
  return true;
}

bool Session::EditCancelOnPasswordField(commands::Command* command) {
  if (context_->composer().GetInputFieldType() != commands::Context::PASSWORD) {
    return false;
  }

  // In password mode, we should commit preedit and close keyboard
  // on Android.
  // TODO(matsuzakit): Remove this trick. b/5955618
  if (context_->composer().source_text().empty()) {
    CommitCompositionDirectly(command);
  } else {
    // Commits original text of reverse conversion.
    CommitSourceTextDirectly(command);
  }
  // Passes the key event through to MozcService.java
  // to continue the processes which are invoked by cancel operation.
  command->mutable_output()->set_consumed(false);

  return true;
}

bool Session::EditCancel(commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "edit_cancel_after_direct_commit_learning");
  DiscardPendingZenzFeedback("edit_cancel_after_pending_feedback");

  if (EditCancelOnPasswordField(command)) {
    return true;
  }

  command->mutable_output()->set_consumed(true);

  TryCancelConvertReverse(command);

  SetStateToPredompositionAndCancel(context_.get());
  ClearLiveConversionState();
  Output(command);
  return true;
}

bool Session::EditCancelAndIMEOff(commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "edit_cancel_and_ime_off_after_direct_commit_learning");
  DiscardPendingZenzFeedback("edit_cancel_and_ime_off_after_pending_feedback");

  if (EditCancelOnPasswordField(command)) {
    return true;
  }

  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::COMPOSITION |
         ImeContext::CONVERSION))) {
    return DoNothing(command);
  }

  command->mutable_output()->set_consumed(true);

  TryCancelConvertReverse(command);

  ClearUndoContext();

  // Reset the context.
  context_->mutable_converter()->Reset();

  SetSessionState(ImeContext::DIRECT, context_.get());
  ClearLiveConversionState();
  Output(command);
  return true;
}

bool Session::CommitInternal(commands::Command* command,
                             bool trigger_zero_query_suggest) {
  if (!(context_->state() &
        (ImeContext::COMPOSITION | ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  PushUndoContext();

  if (context_->state() == ImeContext::COMPOSITION) {
    context_->mutable_converter()->CommitPreedit(context_->composer(),
                                                 command->input().context());
  } else {  // ImeContext::CONVERSION
    context_->mutable_converter()->Commit(context_->composer(),
                                          command->input().context());
  }

  SetSessionState(ImeContext::PRECOMPOSITION, context_.get());

  if (trigger_zero_query_suggest) {
    Suggest(command->input());
  }

  Output(command);
  // Copy the previous output for Undo.
  *context_->mutable_output() = command->output();

  ClearLiveConversionState();
  return true;
}

bool Session::Commit(commands::Command* command) {
  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] Commit entered live_conversion_active=",
      ZenzBool(live_conversion_active_),
      " ", ZenzRedactedTextStats("zenz_key", zenz_live_key_),
      " ", ZenzRedactedTextStats("zenz_value", zenz_live_value_),
      " state=", static_cast<int>(context_->state())));

  if (CommitZenzLiveCorrectionResult(command)) {
    return true;
  }

  FlushPendingLiveConversion();
  return CommitInternal(command,
                        context_->GetRequest().zero_query_suggestion());
}

bool Session::CommitNotTriggeringZeroQuerySuggest(commands::Command* command) {
  return CommitInternal(command, false);
}

bool Session::CommitHead(size_t count, commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::COMPOSITION | ImeContext::PRECOMPOSITION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  // TODO(yamaguchi): Support undo feature.
  ClearUndoContext();

  size_t committed_size;
  context_->mutable_converter()->CommitHead(count, context_->composer(),
                                            &committed_size);
  context_->mutable_composer()->DeleteRange(0, committed_size);
  Output(command);
  return true;
}

bool Session::CommitFirstSuggestion(commands::Command* command) {
  if (!(context_->state() == ImeContext::COMPOSITION ||
        context_->state() == ImeContext::PRECOMPOSITION)) {
    return DoNothing(command);
  }
  if (!context_->converter().IsActive()) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  PushUndoContext();

  constexpr int kFirstIndex = 0;
  size_t committed_key_size = 0;
  context_->mutable_converter()->CommitSuggestionByIndex(
      kFirstIndex, context_->composer(), command->input().context(),
      &committed_key_size);

  SetSessionState(ImeContext::PRECOMPOSITION, context_.get());

  // Get suggestion if zero_query_suggestion is set.
  // zero_query_suggestion is usually set where the client is a mobile.
  if (context_->GetRequest().zero_query_suggestion()) {
    Suggest(command->input());
  }

  Output(command);
  // Copy the previous output for Undo.
  *context_->mutable_output() = command->output();
  return true;
}

bool Session::CommitSegment(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  PushUndoContext();

  size_t size;
  context_->mutable_converter()->CommitFirstSegment(
      context_->composer(), command->input().context(), &size);
  if (size > 0) {
    // Delete the key characters of the first segment from the preedit.
    context_->mutable_composer()->DeleteRange(0, size);
    // The number of segments should be more than one.
    DCHECK_GT(context_->composer().GetLength(), 0);
  }

  if (!context_->converter().IsActive()) {
    // If the converter is not active (ie. the segment size was one.),
    // the state should be switched to precomposition.
    SetSessionState(ImeContext::PRECOMPOSITION, context_.get());

    // Get suggestion if zero_query_suggestion is set.
    // zero_query_suggestion is usually set where the client is a mobile.
    if (context_->GetRequest().zero_query_suggestion()) {
      Suggest(command->input());
    }
  }
  Output(command);
  // Copy the previous output for Undo.
  *context_->mutable_output() = command->output();
  return true;
}

void Session::CommitHeadToFocusedSegmentsInternal(
    const commands::Context& context) {
  size_t size;
  context_->mutable_converter()->CommitHeadToFocusedSegments(
      context_->composer(), context, &size);
  if (size > 0) {
    // Delete the key characters of the first segment from the preedit.
    context_->mutable_composer()->DeleteRange(0, size);
    // The number of segments should be more than one.
    DCHECK_GT(context_->composer().GetLength(), 0);
  }
}

void Session::CommitCompositionDirectly(commands::Command* command) {
  const std::string composition = context_->composer().GetQueryForConversion();
  const std::string conversion = context_->composer().GetStringForSubmission();
  CommitStringDirectly(composition, conversion, command);
}

void Session::CommitSourceTextDirectly(commands::Command* command) {
  // We cannot use a reference since composer will be cleared on
  // CommitStringDirectly.
  absl::string_view copied_source_text = context_->composer().source_text();
  CommitStringDirectly(copied_source_text, copied_source_text, command);
}

void Session::CommitRawTextDirectly(commands::Command* command) {
  const std::string raw_text = context_->composer().GetRawString();
  CommitStringDirectly(raw_text, raw_text, command);
}

void Session::CommitStringDirectly(absl::string_view key,
                                   absl::string_view preedit,
                                   commands::Command* command) {
  if (key.empty() || preedit.empty()) {
    return;
  }

  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->Reset();

  commands::Result* result = command->mutable_output()->mutable_result();
  DCHECK(result != nullptr);
  result->set_type(commands::Result::STRING);
  result->mutable_key()->append(key);
  result->mutable_value()->append(preedit);
  SetSessionState(ImeContext::PRECOMPOSITION, context_.get());

  // Get suggestion if zero_query_suggestion is set.
  // zero_query_suggestion is usually set where the client is a mobile.
  if (context_->GetRequest().zero_query_suggestion()) {
    Suggest(command->input());
  }

  Output(command);
}

namespace {
bool SuppressSuggestion(const commands::Input& input) {
  if (!input.has_context()) {
    return false;
  }
  if (input.context().has_suppress_suggestion() &&
      input.context().suppress_suggestion()) {
    return true;
  }
  // If the target input field is in Chrome's Omnibox or Google
  // search box, the suggest window is hidden.
  for (size_t i = 0; i < input.context().experimental_features_size(); ++i) {
    const std::string& feature = input.context().experimental_features(i);
    if (feature == "chrome_omnibox" || feature == "google_search_box") {
      return true;
    }
  }
  return false;
}
}  // namespace

bool Session::Suggest(const commands::Input& input) {
  if (SuppressSuggestion(input)) {
    return false;
  }

  // |request_suggestion| is not supposed to always ensure suppressing
  // suggestion since this field is used for performance improvement
  // by skipping interim suggestions.  However, the implementation of
  // EngineConverter::SuggestWithPreferences does not perform suggest
  // whenever this flag is on.  So the caller should consider whether
  // this flag should be set or not.  Because the original logic was
  // implemented in Session::InserCharacter, we check the input.type()
  // is SEND_KEY assuming SEND_KEY results InsertCharacter (in most
  // cases).
  //
  // TODO(komatsu): Move the logic into EngineConverter.
  if (input.has_request_suggestion() &&
      input.type() == commands::Input::SEND_KEY) {
    ConversionPreferences conversion_preferences =
        context_->converter().conversion_preferences();
    conversion_preferences.request_suggestion = input.request_suggestion();
    return context_->mutable_converter()->SuggestWithPreferences(
        context_->composer(), input.context(), conversion_preferences);
  }

  return context_->mutable_converter()->Suggest(context_->composer(),
                                                input.context());
}

bool Session::ConvertToTransliteration(
    commands::Command* command,
    const transliteration::TransliterationType type) {
  if (!(context_->state() &
        (ImeContext::CONVERSION | ImeContext::COMPOSITION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  if (!context_->mutable_converter()->ConvertToTransliteration(
          context_->composer(), type)) {
    return false;
  }
  SetSessionState(ImeContext::CONVERSION, context_.get());
  Output(command);
  return true;
}

bool Session::ConvertToHiragana(commands::Command* command) {
  return ConvertToTransliteration(command, transliteration::HIRAGANA);
}

bool Session::ConvertToFullKatakana(commands::Command* command) {
  return ConvertToTransliteration(command, transliteration::FULL_KATAKANA);
}

bool Session::ConvertToHalfKatakana(commands::Command* command) {
  return ConvertToTransliteration(command, transliteration::HALF_KATAKANA);
}

bool Session::ConvertToFullASCII(commands::Command* command) {
  return ConvertToTransliteration(command, transliteration::FULL_ASCII);
}

bool Session::ConvertToHalfASCII(commands::Command* command) {
  return ConvertToTransliteration(command, transliteration::HALF_ASCII);
}

bool Session::SwitchKanaType(commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::CONVERSION | ImeContext::COMPOSITION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  if (!context_->mutable_converter()->SwitchKanaType(context_->composer())) {
    return false;
  }
  SetSessionState(ImeContext::CONVERSION, context_.get());
  Output(command);
  return true;
}

bool Session::DisplayAsHiragana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToHiragana(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    context_->mutable_composer()->SetOutputMode(transliteration::HIRAGANA);
    OutputComposition(command);
    return true;
  }
}

bool Session::DisplayAsFullKatakana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToFullKatakana(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    context_->mutable_composer()->SetOutputMode(transliteration::FULL_KATAKANA);
    OutputComposition(command);
    return true;
  }
}

bool Session::DisplayAsHalfKatakana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToHalfKatakana(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    context_->mutable_composer()->SetOutputMode(transliteration::HALF_KATAKANA);
    OutputComposition(command);
    return true;
  }
}

bool Session::TranslateFullASCII(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToFullASCII(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    context_->mutable_composer()->SetOutputMode(
        transliteration::T13n::ToggleFullAsciiTypes(
            context_->composer().GetOutputMode()));
    OutputComposition(command);
    return true;
  }
}

bool Session::TranslateHalfASCII(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToHalfASCII(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    context_->mutable_composer()->SetOutputMode(
        transliteration::T13n::ToggleHalfAsciiTypes(
            context_->composer().GetOutputMode()));
    OutputComposition(command);
    return true;
  }
}

bool Session::CompositionModeHiragana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  // The temporary mode should not be overridden.
  SwitchInputMode(transliteration::HIRAGANA, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::CompositionModeFullKatakana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  // The temporary mode should not be overridden.
  SwitchInputMode(transliteration::FULL_KATAKANA, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::CompositionModeHalfKatakana(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  // The temporary mode should not be overridden.
  SwitchInputMode(transliteration::HALF_KATAKANA, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::CompositionModeFullASCII(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  // The temporary mode should not be overridden.
  SwitchInputMode(transliteration::FULL_ASCII, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::CompositionModeHalfASCII(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  EnsureIMEIsOn();
  // The temporary mode should not be overridden.
  SwitchInputMode(transliteration::HALF_ASCII, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::CompositionModeSwitchKanaType(commands::Command* command) {
  if (context_->state() != ImeContext::PRECOMPOSITION) {
    return DoNothing(command);
  }

  command->mutable_output()->set_consumed(true);

  transliteration::TransliterationType current_type =
      context_->composer().GetInputMode();
  transliteration::TransliterationType next_type;

  switch (current_type) {
    case transliteration::HIRAGANA:
      next_type = transliteration::FULL_KATAKANA;
      break;

    case transliteration::FULL_KATAKANA:
      next_type = transliteration::HALF_KATAKANA;
      break;

    case transliteration::HALF_KATAKANA:
      next_type = transliteration::HIRAGANA;
      break;

    case transliteration::HALF_ASCII:
    case transliteration::FULL_ASCII:
      next_type = current_type;
      break;

    default:
      LOG(ERROR) << "Unknown input mode: " << current_type;
      // don't change input mode
      next_type = current_type;
      break;
  }

  // The temporary mode should not be overridden.
  SwitchInputMode(next_type, context_->mutable_composer());
  OutputFromState(command);
  return true;
}

bool Session::ConvertToHalfWidth(commands::Command* command) {
  if (!(context_->state() &
        (ImeContext::CONVERSION | ImeContext::COMPOSITION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);

  if (!context_->mutable_converter()->ConvertToHalfWidth(
          context_->composer())) {
    return false;
  }
  SetSessionState(ImeContext::CONVERSION, context_.get());
  Output(command);
  return true;
}

bool Session::TranslateHalfWidth(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertToHalfWidth(command);
  } else {  // context_->state() == ImeContext::COMPOSITION
    const transliteration::TransliterationType type =
        context_->composer().GetOutputMode();
    if (type == transliteration::HIRAGANA ||
        type == transliteration::FULL_KATAKANA ||
        type == transliteration::HALF_KATAKANA) {
      context_->mutable_composer()->SetOutputMode(
          transliteration::HALF_KATAKANA);
    } else if (type == transliteration::FULL_ASCII) {
      context_->mutable_composer()->SetOutputMode(transliteration::HALF_ASCII);
    } else if (type == transliteration::FULL_ASCII_UPPER) {
      context_->mutable_composer()->SetOutputMode(
          transliteration::HALF_ASCII_UPPER);
    } else if (type == transliteration::FULL_ASCII_LOWER) {
      context_->mutable_composer()->SetOutputMode(
          transliteration::HALF_ASCII_LOWER);
    } else if (type == transliteration::FULL_ASCII_CAPITALIZED) {
      context_->mutable_composer()->SetOutputMode(
          transliteration::HALF_ASCII_CAPITALIZED);
    } else {
      // transliteration::HALF_ASCII_something
      return TranslateHalfASCII(command);
    }
    OutputComposition(command);
    return true;
  }
}

bool Session::LaunchConfigDialog(commands::Command* command) {
  command->mutable_output()->set_launch_tool_mode(
      commands::Output::CONFIG_DIALOG);
  return DoNothing(command);
}

bool Session::LaunchDictionaryTool(commands::Command* command) {
  command->mutable_output()->set_launch_tool_mode(
      commands::Output::DICTIONARY_TOOL);
  return DoNothing(command);
}

bool Session::LaunchWordRegisterDialog(commands::Command* command) {
  command->mutable_output()->set_launch_tool_mode(
      commands::Output::WORD_REGISTER_DIALOG);
  return DoNothing(command);
}

bool Session::UndoOrRewind(commands::Command* command) {
  // Undo is prioritized over rewind otherwise the undo operation for
  // partial commit doesn't work (rewind always consumes the event).
  if (HasUndoContext()) {
    return Undo(command);
  }

  // Rewind if the state is in composition.
  if (!(context_->state() & ImeContext::COMPOSITION)) {
    // Mozc decoder doesn't do anything for UNDO_OR_REWIND.
    // Echo back the event to the client to give it a chance to delegate
    // undo operation to the app.
    return EchoBack(command);
  }

  command->mutable_output()->set_consumed(true);
  context_->mutable_composer()->InsertCommandCharacter(
      composer::Composer::REWIND);
  ClearUndoContext();

  // InsertCommandCharacter method updates the preedit text
  // so we need to update suggest candidates.
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::StopKeyToggling(commands::Command* command) {
  if (!(context_->state() & ImeContext::COMPOSITION)) {
    return DoNothing(command);
  }

  command->mutable_output()->set_consumed(true);
  context_->mutable_composer()->InsertCommandCharacter(
      composer::Composer::STOP_KEY_TOGGLING);
  ClearUndoContext();

  // Since the output should not be changed on STOP_KEY_TOGGLING,
  // The last output is used instead of calling the converter operations.
  Output(command);
  return true;
}

bool Session::ToggleAlphanumericMode(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  context_->mutable_composer()->ToggleInputMode();

  OutputFromState(command);
  return true;
}

bool Session::DeleteCandidateFromHistory(commands::Command* command) {
  std::optional<int> id = std::nullopt;
  if (command->input().has_command() && command->input().command().has_id()) {
    id = command->input().command().id();
  }
  if (!context_->mutable_converter()->DeleteCandidateFromHistory(id)) {
    return DoNothing(command);
  }
  return ConvertCancel(command);
}

bool Session::Convert(commands::Command* command) {
  CancelPendingLiveConversion();
  command->mutable_output()->set_consumed(true);
  const std::string composition = context_->composer().GetQueryForConversion();

  // TODO(komatsu): Make a function like ConvertOrSpace.
  // Handle a space key on the ASCII composition mode.
  if (context_->state() == ImeContext::COMPOSITION &&
      (context_->composer().GetInputMode() == transliteration::HALF_ASCII ||
       context_->composer().GetInputMode() == transliteration::FULL_ASCII) &&
      command->input().key().has_special_key() &&
      command->input().key().special_key() == commands::KeyEvent::SPACE) {
    // TODO(komatsu): Consider FullWidth Space too.
    if (!composition.ends_with(' ') ||
        context_->composer().GetLength() != context_->composer().GetCursor()) {
      if (context_->GetRequest().space_on_alphanumeric() ==
          commands::Request::COMMIT) {
        // Space is committed with the composition
        context_->mutable_composer()->InsertCharacterPreedit(" ");
        // Don't push the context to the undo context here.
        // It'll be done in Commit() below.
        return Commit(command);
      } else {
        // SPACE_OR_CONVERT_KEEPING_COMPOSITION or
        // SPACE_OR_CONVERT_COMMITTING_COMPOSITION.

        // If the last character is not space, space is inserted to the
        // composition.
        command->mutable_input()->mutable_key()->set_key_code(' ');
        return InsertCharacter(command);
      }
    }

    if (!composition.empty()) {
      DCHECK_EQ(' ', composition[composition.size() - 1]);
      // Delete the last space.
      context_->mutable_composer()->Backspace();
      ClearUndoContext();
    }
  }

  if (!context_->mutable_converter()->Convert(context_->composer())) {
    LOG(ERROR) << "Conversion failed for some reasons.";
    OutputComposition(command);
    return true;
  }

  SetSessionState(ImeContext::CONVERSION, context_.get());
  Output(command);
  return true;
}

bool Session::ConvertWithoutHistory(commands::Command* command) {
  CancelPendingLiveConversion();
  command->mutable_output()->set_consumed(true);

  ConversionPreferences preferences =
      context_->converter().conversion_preferences();
  preferences.use_history = false;
  if (!context_->mutable_converter()->ConvertWithPreferences(
          context_->composer(), preferences)) {
    LOG(ERROR) << "Conversion failed for some reasons.";
    OutputComposition(command);
    return true;
  }

  SetSessionState(ImeContext::CONVERSION, context_.get());
  Output(command);
  return true;
}

bool Session::CommitIfPassword(commands::Command* command) {
  if (context_->composer().GetInputFieldType() == commands::Context::PASSWORD) {
    CommitCompositionDirectly(command);
    return true;
  }
  return false;
}

bool Session::MoveCursorRight(commands::Command* command) {
  // In future, we may want to change the strategy of committing, to support
  // more flexible behavior.
  // - If the composing text has some "pending toggling character(s) at the
  //   end", we'd like to "fix" the toggling state, but not to commit.
  // - Otherwise (i.e. if there is no such character(s)), we'd like to commit
  //   (considering the use cases, probably we'd like to apply it only for
  //   alphabet mode).
  // Before supporting it, we'll need to support auto fixing by waiting
  // a period. Also, it is necessary to support displaying the current toggling
  // state (otherwise, users would be confused).
  // So, to keep users out from such confusion, we only commit if the current
  // composing mode doesn't has toggling state. Clients has the responsibility
  // to check if the keyboard has toggling state or not. Note that the server
  // should know the current table has toggling state or not. However,
  // a client may NOT want to auto committing even if the composition mode
  // doesn't have the toggling state, so the server just relies on the flag
  // passed from the client.
  // TODO(hidehiko): Support it, when it is prioritized.
  if (context_->GetRequest().crossing_edge_behavior() ==
          commands::Request::COMMIT_WITHOUT_CONSUMING &&
      context_->composer().GetLength() == context_->composer().GetCursor()) {
    Commit(command);

    // Do not consume.
    command->mutable_output()->set_consumed(false);
    return true;
  }

  command->mutable_output()->set_consumed(true);
  if (CommitIfPassword(command)) {
    return true;
  }
  context_->mutable_composer()->MoveCursorRight();
  ClearUndoContext();
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::MoveCursorLeft(commands::Command* command) {
  if (context_->GetRequest().crossing_edge_behavior() ==
          commands::Request::COMMIT_WITHOUT_CONSUMING &&
      context_->composer().GetCursor() == 0) {
    CommitNotTriggeringZeroQuerySuggest(command);

    // Move the cursor to the beginning of the values.
    command->mutable_output()->mutable_result()->set_cursor_offset(
        -static_cast<int32_t>(
            Util::CharsLen(command->output().result().value())));

    // Do not consume.
    command->mutable_output()->set_consumed(false);
    return true;
  }

  command->mutable_output()->set_consumed(true);
  if (CommitIfPassword(command)) {
    return true;
  }
  context_->mutable_composer()->MoveCursorLeft();
  ClearUndoContext();
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::MoveCursorToEnd(commands::Command* command) {
  return MoveCursorToEndInternal(command, true);
}

bool Session::MoveCursorToEndInternal(commands::Command* command,
                                      bool clear_undo) {
  command->mutable_output()->set_consumed(true);
  if (CommitIfPassword(command)) {
    return true;
  }
  context_->mutable_composer()->MoveCursorToEnd();
  if (clear_undo) {
    ClearUndoContext();
  }
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::MoveCursorTo(commands::Command* command) {
  // This method moves the cursor *inside* the composition text.
  // Therefore on PRECOMPOSITION state, where there is no composition text,
  // this method shouldn't consume the event but send back it to the client.
  if (context_->state() == ImeContext::PRECOMPOSITION) {
    return EchoBack(command);
  }
  if (context_->state() != ImeContext::COMPOSITION) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  if (CommitIfPassword(command)) {
    return true;
  }
  context_->mutable_composer()->MoveCursorTo(
      command->input().command().cursor_position());
  ClearUndoContext();
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::MoveCursorToBeginning(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  if (CommitIfPassword(command)) {
    return true;
  }
  context_->mutable_composer()->MoveCursorToBeginning();
  ClearUndoContext();
  if (Suggest(command->input())) {
    Output(command);
    return true;
  }
  OutputComposition(command);
  return true;
}

bool Session::Delete(commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "delete_after_direct_commit_learning");
  DiscardPendingZenzFeedback("delete_after_pending_feedback");

  command->mutable_output()->set_consumed(true);
  CancelLiveConversionForEditing();
  context_->mutable_composer()->Delete();
  ClearUndoContext();
  if (context_->mutable_composer()->Empty()) {
    SetStateToPredompositionAndCancel(context_.get());
    Output(command);
  } else if (MaybeStartLiveConversion(command)) {
    return true;
  } else if (Suggest(command->input())) {
    Output(command);
  } else {
    OutputComposition(command);
  }
  return true;
}

bool Session::Backspace(commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "backspace_after_direct_commit_learning");
  DiscardPendingZenzFeedback("backspace_after_pending_feedback");

  command->mutable_output()->set_consumed(true);
  CancelLiveConversionForEditing();
  context_->mutable_composer()->Backspace();
  ClearUndoContext();
  if (context_->mutable_composer()->Empty()) {
    SetStateToPredompositionAndCancel(context_.get());
    Output(command);
  } else if (MaybeStartLiveConversion(command)) {
    return true;
  } else if (Suggest(command->input())) {
    Output(command);
  } else {
    OutputComposition(command);
  }
  return true;
}

bool Session::SegmentFocusRight(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentFocusRight();
  Output(command);
  return true;
}

bool Session::SegmentFocusLast(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentFocusLast();
  Output(command);
  return true;
}

bool Session::SegmentFocusLeft(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentFocusLeft();
  Output(command);
  return true;
}

bool Session::SegmentFocusLeftEdge(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentFocusLeftEdge();
  Output(command);
  return true;
}

bool Session::SegmentWidthExpand(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentWidthExpand(context_->composer());
  Output(command);
  return true;
}

bool Session::SegmentWidthShrink(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->SegmentWidthShrink(context_->composer());
  Output(command);
  return true;
}

bool Session::ReportBug(commands::Command* command) {
  return DoNothing(command);
}

bool Session::ConvertNext(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->CandidateNext(context_->composer());
  Output(command);
  return true;
}

bool Session::ConvertNextPage(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->CandidateNextPage();
  Output(command);
  return true;
}

bool Session::ConvertPrev(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->CandidatePrev();
  Output(command);
  return true;
}

bool Session::ConvertPrevPage(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->CandidatePrevPage();
  Output(command);
  return true;
}

bool Session::ConvertCancel(commands::Command* command) {
  DiscardPendingDirectCommitLearning(
      "convert_cancel_after_direct_commit_learning");
  DiscardPendingZenzFeedback("convert_cancel_after_pending_feedback");

  command->mutable_output()->set_consumed(true);

  SetSessionState(ImeContext::COMPOSITION, context_.get());
  context_->mutable_converter()->Cancel();
  if (Suggest(command->input())) {
    Output(command);
  } else {
    OutputComposition(command);
  }
  return true;
}

bool Session::PredictAndConvert(commands::Command* command) {
  CancelPendingLiveConversion();

  if (context_->state() == ImeContext::CONVERSION) {
    return ConvertNext(command);
  }

  command->mutable_output()->set_consumed(true);
  if (context_->mutable_converter()->Predict(context_->composer())) {
    SetSessionState(ImeContext::CONVERSION, context_.get());
    Output(command);
  } else {
    OutputComposition(command);
  }
  return true;
}

void Session::OutputFromState(commands::Command* command) {
  if (context_->state() == ImeContext::DIRECT) {
    OutputMode(command);
    return;
  }
  Output(command);
}

void Session::Output(commands::Command* command) {
  OutputMode(command);
  context_->mutable_converter()->PopOutput(context_->composer(),
                                           command->mutable_output());
}

void Session::OutputMode(commands::Command* command) const {
  const commands::CompositionMode mode =
      ToCompositionMode(context_->composer().GetInputMode());
  const commands::CompositionMode comeback_mode =
      ToCompositionMode(context_->composer().GetComebackInputMode());

  commands::Output* output = command->mutable_output();
  commands::Status* status = output->mutable_status();
  if (context_->state() == ImeContext::DIRECT) {
    output->set_mode(commands::DIRECT);
    status->set_activated(false);
  } else {
    output->set_mode(mode);
    status->set_activated(true);
  }
  status->set_mode(mode);
  status->set_comeback_mode(comeback_mode);
}

void Session::OutputComposition(commands::Command* command) const {
  OutputMode(command);
  context_->converter().FillPreedit(
      context_->composer(), command->mutable_output()->mutable_preedit());
}

void Session::OutputKey(commands::Command* command) const {
  OutputMode(command);
  commands::KeyEvent* key = command->mutable_output()->mutable_key();
  *key = command->input().key();
}

namespace {

bool MatchesKeyEvent(const commands::KeyEvent& key_event,
                     const uint32_t key_code,
                     std::initializer_list<absl::string_view> key_strings) {
  if (key_event.key_code() == key_code && key_event.key_string().empty()) {
    return true;
  }

  for (const absl::string_view s : key_strings) {
    if (key_event.key_string() == s) {
      return true;
    }
  }
  return false;
}

bool MatchesString(absl::string_view value,
                   std::initializer_list<absl::string_view> candidates) {
  for (const absl::string_view s : candidates) {
    if (value == s) {
      return true;
    }
  }
  return false;
}

// Auto conversion helper.
// NOTE: This checks the last character in preedit, not key_event.key_string().
bool IsValidAutoConversionKey(const config::Config& config,
                              const uint32_t key_code,
                              absl::string_view last_char) {
  return (((key_code == static_cast<uint32_t>('.') && last_char.empty()) ||
           last_char == "." || last_char == "．" || last_char == "。" ||
           last_char == "｡") &&
          (config.auto_conversion_key() &
           config::Config::AUTO_CONVERSION_KUTEN)) ||
         (((key_code == static_cast<uint32_t>(',') && last_char.empty()) ||
           last_char == "," || last_char == "，" || last_char == "、" ||
           last_char == "､") &&
          (config.auto_conversion_key() &
           config::Config::AUTO_CONVERSION_TOUTEN)) ||
         (((key_code == static_cast<uint32_t>('?') && last_char.empty()) ||
           last_char == "?" || last_char == "？") &&
          (config.auto_conversion_key() &
           config::Config::AUTO_CONVERSION_QUESTION_MARK)) ||
         (((key_code == static_cast<uint32_t>('!') && last_char.empty()) ||
           last_char == "!" || last_char == "！") &&
          (config.auto_conversion_key() &
           config::Config::AUTO_CONVERSION_EXCLAMATION_MARK));
}

bool IsValidDirectCommitTriggerKey(const config::Config& config,
                                   const commands::KeyEvent& key_event) {
  return (MatchesKeyEvent(key_event, static_cast<uint32_t>('.'),
                          {".", "．", "。", "｡"}) &&
          (config.direct_commit_key() &
           config::Config::DIRECT_COMMIT_KUTEN)) ||
         (MatchesKeyEvent(key_event, static_cast<uint32_t>(','),
                          {",", "，", "、", "､"}) &&
          (config.direct_commit_key() &
           config::Config::DIRECT_COMMIT_TOUTEN)) ||
         (MatchesKeyEvent(key_event, static_cast<uint32_t>('?'),
                          {"?", "？"}) &&
          (config.direct_commit_key() &
           config::Config::DIRECT_COMMIT_QUESTION_MARK)) ||
         (MatchesKeyEvent(key_event, static_cast<uint32_t>('!'),
                          {"!", "！"}) &&
          (config.direct_commit_key() &
           config::Config::DIRECT_COMMIT_EXCLAMATION_MARK)) ||
         (MatchesKeyEvent(key_event, static_cast<uint32_t>('('),
                          {"(", "（"}) &&
          (config.direct_commit_key() &
           config::Config::DIRECT_COMMIT_OPEN_PARENTHESIS)) ||
         (MatchesKeyEvent(key_event, static_cast<uint32_t>(')'),
                          {")", "）"}) &&
          (config.direct_commit_key() &
           config::Config::DIRECT_COMMIT_CLOSE_PARENTHESIS)) ||
         (MatchesKeyEvent(key_event, static_cast<uint32_t>('['),
                          {"[", "［", "「"}) &&
          (config.direct_commit_key() &
           config::Config::DIRECT_COMMIT_OPEN_BRACKET)) ||
         (MatchesKeyEvent(key_event, static_cast<uint32_t>(']'),
                          {"]", "］", "」"}) &&
          (config.direct_commit_key() &
           config::Config::DIRECT_COMMIT_CLOSE_BRACKET));
}

bool IsValidDirectCommitKey(const config::Config& config,
                            const commands::KeyEvent& key_event,
                            absl::string_view last_char) {
  return
      (MatchesKeyEvent(key_event, static_cast<uint32_t>('.'),
                       {".", "．", "。", "｡"}) &&
       MatchesString(last_char, {".", "．", "。", "｡"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_KUTEN)) ||

      (MatchesKeyEvent(key_event, static_cast<uint32_t>(','),
                       {",", "，", "、", "､"}) &&
       MatchesString(last_char, {",", "，", "、", "､"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_TOUTEN)) ||

      (MatchesKeyEvent(key_event, static_cast<uint32_t>('?'),
                       {"?", "？"}) &&
       MatchesString(last_char, {"?", "？"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_QUESTION_MARK)) ||

      (MatchesKeyEvent(key_event, static_cast<uint32_t>('!'),
                       {"!", "！"}) &&
       MatchesString(last_char, {"!", "！"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_EXCLAMATION_MARK)) ||

      (MatchesKeyEvent(key_event, static_cast<uint32_t>('('),
                       {"(", "（"}) &&
       MatchesString(last_char, {"(", "（"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_OPEN_PARENTHESIS)) ||

      (MatchesKeyEvent(key_event, static_cast<uint32_t>(')'),
                       {")", "）"}) &&
       MatchesString(last_char, {")", "）"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_CLOSE_PARENTHESIS)) ||

      (MatchesKeyEvent(key_event, static_cast<uint32_t>('['),
                       {"[", "［", "「"}) &&
       MatchesString(last_char, {"[", "［", "「"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_OPEN_BRACKET)) ||

      (MatchesKeyEvent(key_event, static_cast<uint32_t>(']'),
                       {"]", "］", "」"}) &&
       MatchesString(last_char, {"]", "］", "」"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_CLOSE_BRACKET));
}

}  // namespace

bool Session::CanStartAutoConversion(
    const commands::KeyEvent& key_event) const {
  if (!context_->GetConfig().use_auto_conversion()) {
    return false;
  }

  // Disable if the input comes from non-standard user keyboards, like numpad.
  if (key_event.input_style() != commands::KeyEvent::FOLLOW_MODE) {
    return false;
  }

  // We simply disable the auto conversion feature if the mode is ASCII.
  // We conclude that disabling this feature is better in this situation.
  // TODO(taku): fix the behavior. Converter module needs to be fixed.
  if (key_event.mode() == commands::HALF_ASCII ||
      key_event.mode() == commands::FULL_ASCII) {
    return false;
  }

  // We should NOT check key_string.
  // http://b/issue?id=3217992
  // Auto conversion is not triggered if the composition is empty or
  // only one character, or the cursor is not in the end of the
  // composition.
  const size_t length = context_->composer().GetLength();
  if (length <= 1 || length != context_->composer().GetCursor()) {
    return false;
  }

  const uint32_t key_code = key_event.key_code();
  const std::string preedit = context_->composer().GetStringForPreedit();
  const absl::string_view last_char = Util::Utf8SubString(preedit, length - 1, 1);
  if (last_char.empty()) {
    return false;
  }

  if (!IsValidAutoConversionKey(context_->GetConfig(), key_code, last_char)) {
    return false;
  }

  // Check the previous character of last_character.
  // when |last_prev_char| is number, we don't invoke auto_conversion
  // if the same invoke key is repeated, do not conversion.
  // http://b/issue?id=2932118
  const absl::string_view last_prev_char =
      Util::Utf8SubString(preedit, length - 2, 1);
  if (last_prev_char.empty() || last_prev_char == last_char ||
      Util::NUMBER == Util::GetScriptType(last_prev_char)) {
    return false;
  }
  return true;
}

bool Session::CanDirectCommitPendingLiveConversionBeforeInsert(
    const commands::KeyEvent& key_event) const {
  const config::Config& config = context_->GetConfig();

  if (!config.use_direct_commit()) {
    return false;
  }

  // Mutual exclusion guard. Even if both are accidentally enabled in config,
  // direct commit is disabled here.
  if (config.use_auto_conversion()) {
    return false;
  }

  if (context_->state() != ImeContext::COMPOSITION) {
    return false;
  }

  // Disable if the input comes from non-standard user keyboards, like numpad.
  if (key_event.input_style() != commands::KeyEvent::FOLLOW_MODE) {
    return false;
  }

  // Disable in ASCII mode.
  if (key_event.mode() == commands::HALF_ASCII ||
      key_event.mode() == commands::FULL_ASCII) {
    return false;
  }

  const size_t length = context_->composer().GetLength();
  if (length == 0 || length != context_->composer().GetCursor()) {
    return false;
  }

  return IsValidDirectCommitTriggerKey(config, key_event);
}

bool Session::CanDirectCommitAfterPunctuation(
    const commands::KeyEvent& key_event) const {
  const config::Config& config = context_->GetConfig();

  if (!config.use_direct_commit()) {
    return false;
  }

  // Mutual exclusion guard. Even if both are accidentally enabled in config,
  // direct commit is disabled here.
  if (config.use_auto_conversion()) {
    return false;
  }

  if (!(context_->state() &
        (ImeContext::PRECOMPOSITION | ImeContext::COMPOSITION))) {
    return false;
  }

  // Disable if the input comes from non-standard user keyboards, like numpad.
  if (key_event.input_style() != commands::KeyEvent::FOLLOW_MODE) {
    return false;
  }

  // Disable in ASCII mode.
  if (key_event.mode() == commands::HALF_ASCII ||
      key_event.mode() == commands::FULL_ASCII) {
    return false;
  }

  const size_t length = context_->composer().GetLength();
  if (length == 0 || length != context_->composer().GetCursor()) {
    return false;
  }

  const std::string preedit = context_->composer().GetStringForPreedit();
  const absl::string_view last_char =
      Util::Utf8SubString(preedit, length - 1, 1);
  if (last_char.empty()) {
    return false;
  }

  return IsValidDirectCommitKey(config, key_event, last_char);
}

void Session::UpdateTime() {
  context_->set_last_command_time(Clock::GetAbslTime());
}

void Session::TransformInput(commands::Input* input) {
  if (input->has_key()) {
    context_->key_event_transformer().TransformKeyEvent(input->mutable_key());
  }
}

bool Session::SwitchInputFieldType(commands::Command* command) {
  command->mutable_output()->set_consumed(true);
  context_->mutable_composer()->SetInputFieldType(
      command->input().context().input_field_type());
  Output(command);
  return true;
}

bool Session::HandleIndirectImeOnOff(commands::Command* command) {
  const commands::KeyEvent& key = command->input().key();
  if (!key.has_activated()) {
    return true;
  }
  const ImeContext::State state = context_->state();
  if (state == ImeContext::DIRECT && key.activated()) {
    // Indirect IME On found.
    commands::Command on_command;
    on_command = *command;
    if (!IMEOn(&on_command)) {
      return false;
    }
  } else if (state != ImeContext::DIRECT && !key.activated()) {
    // Indirect IME Off found.
    commands::Command off_command;
    off_command = *command;
    if (!IMEOff(&off_command)) {
      return false;
    }
  }
  return true;
}

bool Session::ImeAction(commands::Command* command) {
  if (context_->state() != ImeContext::PRECOMPOSITION ||
      context_->composer().GetInputFieldType() != commands::Context::NORMAL) {
    return false;
  }

  // ImeAction is triggered when the mobile-specific IME action buttons such as
  // Search, Go, or Next, are tapped. After a user finishes an IME session, they
  // might still perform manual edits using the Backspace key. To sync these
  // edits with the converter, we call CommitContext. Typical use case is the
  // partial-revert on user history training.
  context_->mutable_converter()->CommitContext(context_->composer(),
                                               command->input().context());

  return true;
}

bool Session::CommitRawText(commands::Command* command) {
  if (context_->composer().GetLength() == 0) {
    return false;
  }
  CommitRawTextDirectly(command);
  return true;
}

// TODO(komatsu): delete this function.
composer::Composer* Session::get_internal_composer_only_for_unittest() {
  return context_->mutable_composer();
}

const ImeContext& Session::context() const { return *context_; }

}  // namespace session
}  // namespace mozc
