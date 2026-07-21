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
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "base/clock.h"
#include "base/strings/unicode.h"
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
#include "session/zenz_named_pipe_client.h"
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

// Avoid teaching one-character particles or accidental raw commits as strong
// user segment history when the user cancels a conversion and immediately
// commits the restored hiragana preedit.
constexpr size_t kMinRerankedPreeditCommitCharsAfterConvertCancel = 2;

// Default live conversion debounce delay. The user-visible value is stored in
// Config::live_conversion_delay_msec.
constexpr uint32_t kDefaultLiveConversionDelayMillisec = 228;
constexpr uint32_t kMaxLiveConversionDelayMillisec = 1000;

// Default minimum composition length before live conversion is allowed.
// Single-character input is often a particle such as 「に」「を」「が」,
// and converting it to a kanji such as 「二」 too eagerly is noisy.
// The value is user-configurable via Config::live_conversion_min_key_length.
constexpr uint32_t kDefaultLiveConversionMinKeyLength = 2;
constexpr uint32_t kMinLiveConversionMinKeyLength = 1;
constexpr uint32_t kMaxLiveConversionMinKeyLength = 20;

constexpr uint32_t kDefaultZenzLiveCorrectionDelayMsec = 1000;
constexpr uint32_t kDefaultZenzLiveCorrectionTimeoutMsec = 180;
constexpr uint32_t kDefaultZenzLiveCorrectionPollMsec = 24;
constexpr uint32_t kDefaultZenzLiveCorrectionMinKeyLength = 2;
constexpr uint32_t kDefaultZenzLiveCorrectionLeftContextLength = 24;
constexpr uint32_t kDefaultZenzLiveCorrectionRightContextLength = 10;
constexpr uint32_t kMaxZenzLiveCorrectionRightContextLength = 128;
constexpr uint32_t kMaxZenzLiveCorrectionDelayMsec = 5000;
constexpr uint32_t kMaxZenzLiveCorrectionTimeoutMsec = 1000;

// This is not the model inference timeout.  It is the maximum time the session
// keeps polling the async worker after the request has been submitted.  Cold
// start may include scorer process launch, pipe creation, llama-server startup,
// ready probing, and then the actual completion request.  Since this path is
// async, a longer poll window does not block the IME thread.
constexpr uint32_t kZenzLiveCorrectionAsyncWaitMsec = 3000;

// These match the Fcitx ingress bounds.  The server independently enforces
// them because Context is an IPC input and must not trust one particular
// adapter implementation.
constexpr size_t kMaxGrimodexProgramBytes = 256;
constexpr size_t kMaxGrimodexFrontendBytes = 128;

int32_t GrimodexRevisionFromFocusEpoch(uint64_t focus_epoch) {
  const uint32_t revision = static_cast<uint32_t>(focus_epoch & 0x7FFFFFFFU);
  return static_cast<int32_t>(revision == 0 ? 1 : revision);
}

std::string DecodeLowerHexSha256(absl::string_view hex) {
  if (hex.size() != 64) {
    return "";
  }

  auto nibble = [](char c) -> int {
    if ('0' <= c && c <= '9') {
      return c - '0';
    }
    if ('a' <= c && c <= 'f') {
      return c - 'a' + 10;
    }
    return -1;
  };

  std::string bytes;
  bytes.reserve(32);
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int high = nibble(hex[i]);
    const int low = nibble(hex[i + 1]);
    if (high < 0 || low < 0) {
      return "";
    }
    bytes.push_back(static_cast<char>((high << 4) | low));
  }
  return bytes;
}

bool IsLiveConversionTrailingDecorativeSymbol(char32_t c) {
  switch (c) {
    case 0x007E:  // ~
    case 0xFF5E:  // ～
    case 0x301C:  // 〜
    case 0x30FC:  // ー
    case 0x2015:  // ―
    case 0x2025:  // ‥
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

enum class LiveConversionLeftBoundary {
  kKnownBoundary,
  kKnownNonBoundary,
  kUnknown,
};

struct LiveConversionAtom {
  absl::string_view text;
  bool is_entire_composition = false;
  LiveConversionLeftBoundary left_boundary =
      LiveConversionLeftBoundary::kUnknown;
};

bool ContainsStringView(std::initializer_list<absl::string_view> values,
                        absl::string_view target) {
  for (absl::string_view value : values) {
    if (value == target) {
      return true;
    }
  }
  return false;
}

bool IsLiveConversionAtomBoundaryChar(char32_t c) {
  switch (c) {
    case 0x0009:  // tab
    case 0x000A:  // LF
    case 0x000D:  // CR
    case 0x0020:  // space
    case 0x3000:  // ideographic space
    case 0x002C:  // ,
    case 0xFF0C:  // ，
    case 0x3001:  // 、
    case 0x002E:  // .
    case 0xFF0E:  // ．
    case 0x3002:  // 。
    case 0x0021:  // !
    case 0xFF01:  // ！
    case 0x003F:  // ?
    case 0xFF1F:  // ？
    case 0x2025:  // ‥
    case 0x2026:  // …
    case 0x003A:  // :
    case 0xFF1A:  // ：
    case 0x003B:  // ;
    case 0xFF1B:  // ；
    case 0x0028:  // (
    case 0xFF08:  // （
    case 0x005B:  // [
    case 0x007B:  // {
    case 0x300C:  // 「
    case 0x300D:  // 」
    case 0x300E:  // 『
    case 0x300F:  // 』
    case 0x3010:  // 【
    case 0x3011:  // 】
    case 0x3014:  // 〔
    case 0x3015:  // 〕
    case 0x201C:  // “
    case 0x201D:  // ”
    case 0x2018:  // ‘
    case 0x2019:  // ’
      return true;
    default:
      return false;
  }
}

bool IsLiveConversionTrailingSentenceTailSymbol(char32_t c) {
  switch (c) {
    case 0x0009:  // tab
    case 0x000A:  // LF
    case 0x000D:  // CR
    case 0x0020:  // space
    case 0x3000:  // ideographic space
    case 0x002C:  // ,
    case 0xFF0C:  // ，
    case 0x3001:  // 、
    case 0x002E:  // .
    case 0xFF0E:  // ．
    case 0x3002:  // 。
    case 0x0021:  // !
    case 0xFF01:  // ！
    case 0x003F:  // ?
    case 0xFF1F:  // ？
    case 0x2025:  // ‥
    case 0x2026:  // …
    case 0x003A:  // :
    case 0xFF1A:  // ：
    case 0x003B:  // ;
    case 0xFF1B:  // ；
    case 0x0029:  // )
    case 0xFF09:  // ）
    case 0x005D:  // ]
    case 0x007D:  // }
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0x3011:  // 】
    case 0x3015:  // 〕
    case 0x201D:  // ”
    case 0x2019:  // ’
      return true;
    default:
      return false;
  }
}

bool IsLiveConversionProlongationMark(char32_t c) {
  switch (c) {
    case 0x007E:  // ~
    case 0xFF5E:  // ～
    case 0x301C:  // 〜
    case 0x30FC:  // ー
    case 0x2015:  // ―
      return true;
    default:
      return false;
  }
}

absl::string_view StripTrailingLiveConversionSentenceTail(
    absl::string_view key) {
  absl::string_view core = key;

  while (!core.empty()) {
    absl::string_view rest;
    char32_t last = 0;
    if (!Util::SplitLastChar32(core, &rest, &last)) {
      break;
    }
    if (!IsLiveConversionTrailingSentenceTailSymbol(last)) {
      break;
    }
    core = rest;
  }

  return core;
}

absl::string_view StripTrailingLiveConversionProlongationMarks(
    absl::string_view key) {
  absl::string_view core = key;

  while (!core.empty()) {
    absl::string_view rest;
    char32_t last = 0;
    if (!Util::SplitLastChar32(core, &rest, &last)) {
      break;
    }
    if (!IsLiveConversionProlongationMark(last)) {
      break;
    }
    core = rest;
  }

  return core;
}

LiveConversionLeftBoundary GetLiveConversionCommittedLeftBoundary(
    const ImeContext& context) {
  if (!context.client_context().has_preceding_text()) {
    return LiveConversionLeftBoundary::kUnknown;
  }

  const std::string& preceding_text =
      context.client_context().preceding_text();
  if (preceding_text.empty()) {
    return LiveConversionLeftBoundary::kKnownBoundary;
  }

  absl::string_view rest;
  char32_t last = 0;
  if (!Util::SplitLastChar32(preceding_text, &rest, &last)) {
    return LiveConversionLeftBoundary::kUnknown;
  }

  return IsLiveConversionAtomBoundaryChar(last)
             ? LiveConversionLeftBoundary::kKnownBoundary
             : LiveConversionLeftBoundary::kKnownNonBoundary;
}

LiveConversionAtom ExtractTrailingLiveConversionAtom(
    absl::string_view core,
    LiveConversionLeftBoundary committed_left_boundary) {
  LiveConversionAtom atom;
  atom.text = core;
  atom.is_entire_composition = true;
  atom.left_boundary = committed_left_boundary;

  absl::string_view cursor = core;
  while (!cursor.empty()) {
    absl::string_view rest;
    char32_t last = 0;
    if (!Util::SplitLastChar32(cursor, &rest, &last)) {
      break;
    }

    if (IsLiveConversionAtomBoundaryChar(last)) {
      atom.text =
          absl::string_view(core.data() + cursor.size(),
                            core.size() - cursor.size());
      atom.is_entire_composition = false;
      atom.left_boundary = LiveConversionLeftBoundary::kKnownBoundary;
      return atom;
    }

    cursor = rest;
  }

  return atom;
}

bool IsStrongExpressiveKanaAtom(absl::string_view atom) {
  return ContainsStringView(
      {
          "あっ",
          "えっ",
          "おっ",
          "はっ",
          "へっ",
          "ちっ",
          "ちぇっ",
          "ほっ",
          "いてっ",
          "ぎゃっ",
          "ひゃっ",
      },
      atom);
}

bool IsBoundarySensitiveExpressiveKanaCoreAtom(absl::string_view atom) {
  return ContainsStringView(
      {
          "ふん",
          "くそ",
          "よう",
          "ふむ",
          "はて",
          "ふう",
          "ほう",
          "ほい",
          "ほいほい",
          "へい",
          "てへ",
          "くちゃ",
          "くちょ",
          "ぐちょ",
          "ぐちょぐちょ",
          "どろどろ",
          "つるつる",
          "はいはい",
          "うん",
          "うんうん",
          "そうそう",
          "いやいや",
          "まあまあ",
      },
      atom);
}

constexpr size_t kMaxRepeatedEInterjectionChars = 30;

bool IsRepeatedEInterjectionAtom(absl::string_view atom) {
  size_t count = 0;

  for (absl::string_view c : Utf8AsChars(atom)) {
    if (c != "え" && c != "ぇ") {
      return false;
    }
    ++count;
    if (count > kMaxRepeatedEInterjectionChars) {
      return false;
    }
  }

  return count >= 2;
}

bool IsHoFamilyExpressiveProsodyAtom(absl::string_view atom) {
  return ContainsStringView(
      {
          "ほー",
          "ほ〜",
          "ほ～",
          "ほお",
          "ほほう",
          "ほほお",
          "ほほー",
          "ほほ〜",
          "ほほ～",
          "ほほーん",
          "ほほ〜ん",
          "ほほ～ん",
          "ほっほ",
          "ほっほう",
          "ほっほお",
          "ほっほー",
          "ほっほ〜",
          "ほっほ～",
          "ほっほーん",
          "ほっほ〜ん",
          "ほっほ～ん",
      },
      atom);
}

constexpr size_t kMaxExpressiveSokuonCount = 10;
constexpr size_t kMaxEvaluativeSlangPrefixChars = 4;

bool StripRepeatedExpressiveSokuonTail(absl::string_view atom,
                                       absl::string_view* core) {
  size_t sokuon_count = 0;
  absl::string_view cursor = atom;

  while (!cursor.empty()) {
    absl::string_view rest;
    char32_t last = 0;
    if (!Util::SplitLastChar32(cursor, &rest, &last)) {
      return false;
    }

    if (last != U'っ') {
      break;
    }

    ++sokuon_count;
    if (sokuon_count > kMaxExpressiveSokuonCount) {
      return false;
    }

    cursor = rest;
  }

  if (sokuon_count == 0 || cursor.empty()) {
    return false;
  }

  *core = cursor;
  return true;
}

bool IsBoundarySensitiveExpressiveKanaAtom(absl::string_view atom) {
  if (IsBoundarySensitiveExpressiveKanaCoreAtom(atom)) {
    return true;
  }

  absl::string_view core;
  if (!StripRepeatedExpressiveSokuonTail(atom, &core)) {
    return false;
  }

  return IsBoundarySensitiveExpressiveKanaCoreAtom(core);
}

bool ConsumePrefixForLiveConversionMatcher(absl::string_view* text,
                                           absl::string_view prefix) {
  if (text->size() < prefix.size() ||
      text->substr(0, prefix.size()) != prefix) {
    return false;
  }
  *text = text->substr(prefix.size());
  return true;
}

bool ConsumeAnyPrefixForLiveConversionMatcher(
    absl::string_view* text,
    std::initializer_list<absl::string_view> prefixes) {
  for (absl::string_view prefix : prefixes) {
    absl::string_view rest = *text;
    if (ConsumePrefixForLiveConversionMatcher(&rest, prefix)) {
      *text = rest;
      return true;
    }
  }
  return false;
}

bool IsOnlyLiveConversionProlongationMarks(absl::string_view text) {
  if (text.empty()) {
    return false;
  }

  while (!text.empty()) {
    absl::string_view rest;
    char32_t last = 0;
    if (!Util::SplitLastChar32(text, &rest, &last)) {
      return false;
    }
    if (!IsLiveConversionProlongationMark(last)) {
      return false;
    }
    text = rest;
  }

  return true;
}

bool IsLiveConversionProlongationMarksWithOptionalN(
    absl::string_view text) {
  if (text == "ん") {
    return true;
  }

  absl::string_view rest;
  char32_t last = 0;
  if (!Util::SplitLastChar32(text, &rest, &last)) {
    return false;
  }

  return last == U'ん' && IsOnlyLiveConversionProlongationMarks(rest);
}

bool IsLiveConversionProlongationMarksWithOptionalI(
    absl::string_view text) {
  if (text == "い") {
    return true;
  }

  absl::string_view rest;
  char32_t last = 0;
  if (!Util::SplitLastChar32(text, &rest, &last)) {
    return false;
  }

  return last == U'い' && IsOnlyLiveConversionProlongationMarks(rest);
}

bool ConsumeRepeatedExpressiveSokuon(absl::string_view* text) {
  size_t sokuon_count = 0;

  while (ConsumePrefixForLiveConversionMatcher(text, "っ")) {
    ++sokuon_count;
    if (sokuon_count > kMaxExpressiveSokuonCount) {
      return false;
    }
  }

  return sokuon_count > 0;
}

bool MatchesSokuonEndingMimeticAtom(absl::string_view atom,
                                    absl::string_view stem) {
  absl::string_view rest = atom;

  if (!ConsumePrefixForLiveConversionMatcher(&rest, stem)) {
    return false;
  }

  if (!ConsumeRepeatedExpressiveSokuon(&rest)) {
    return false;
  }

  if (rest.empty()) {
    return true;
  }

  return IsOnlyLiveConversionProlongationMarks(rest);
}

bool IsSokuonEndingMimeticAtom(absl::string_view atom) {
  return MatchesSokuonEndingMimeticAtom(atom, "しゃ") ||
         MatchesSokuonEndingMimeticAtom(atom, "どろ") ||
         MatchesSokuonEndingMimeticAtom(atom, "とろ") ||
         MatchesSokuonEndingMimeticAtom(atom, "さわ") ||
         MatchesSokuonEndingMimeticAtom(atom, "つる");
}

bool MatchesRepeatedSokuonPrefix(absl::string_view atom,
                                 absl::string_view head) {
  absl::string_view rest = atom;

  if (!ConsumePrefixForLiveConversionMatcher(&rest, head)) {
    return false;
  }

  if (!ConsumeRepeatedExpressiveSokuon(&rest)) {
    return false;
  }

  return rest.empty();
}

constexpr size_t kMaxCasualGreetingProlongedTailChars = 30;

bool IsLiveConversionProlongationMarkText(absl::string_view text) {
  absl::string_view rest;
  char32_t c = 0;
  return Util::SplitLastChar32(text, &rest, &c) &&
         rest.empty() &&
         IsLiveConversionProlongationMark(c);
}

bool IsCasualGreetingProlongedTail(absl::string_view text) {
  size_t count = 0;

  for (absl::string_view c : Utf8AsChars(text)) {
    if (c != "い" && c != "ぃ" &&
        !IsLiveConversionProlongationMarkText(c)) {
      return false;
    }
    ++count;
    if (count > kMaxCasualGreetingProlongedTailChars) {
      return false;
    }
  }

  return true;
}

bool IsPendingRomanSForCasualSsuGreeting(absl::string_view text) {
  return text == "s" || text == "ss";
}

bool IsCasualGreetingProlongedTailWithPendingRomanS(
    absl::string_view text) {
  if (text.empty()) {
    return false;
  }

  if (IsPendingRomanSForCasualSsuGreeting(text)) {
    return true;
  }

  if (text.ends_with("ss")) {
    return IsCasualGreetingProlongedTail(
        text.substr(0, text.size() - 2));
  }

  if (text.ends_with("s")) {
    return IsCasualGreetingProlongedTail(
        text.substr(0, text.size() - 1));
  }

  return IsCasualGreetingProlongedTail(text);
}

bool MatchesCasualSsuGreetingAtom(absl::string_view atom,
                                  absl::string_view head) {
  absl::string_view rest = atom;

  if (!ConsumePrefixForLiveConversionMatcher(&rest, head)) {
    return false;
  }

  const size_t sokuon_pos = rest.find("っ");
  if (sokuon_pos == absl::string_view::npos) {
    return false;
  }

  if (!IsCasualGreetingProlongedTail(rest.substr(0, sokuon_pos))) {
    return false;
  }

  rest = rest.substr(sokuon_pos);

  if (!ConsumeRepeatedExpressiveSokuon(&rest)) {
    return false;
  }

  if (!ConsumePrefixForLiveConversionMatcher(&rest, "す")) {
    return false;
  }

  return rest.empty() || IsOnlyLiveConversionProlongationMarks(rest);
}

bool IsCasualSsuGreetingAtom(absl::string_view atom) {
  return MatchesCasualSsuGreetingAtom(atom, "ち") ||
         MatchesCasualSsuGreetingAtom(atom, "ちょ") ||
         MatchesCasualSsuGreetingAtom(atom, "ちょり");
}

bool MatchesCasualSsuGreetingPrefixAtom(absl::string_view atom,
                                        absl::string_view head,
                                        bool allow_bare_head) {
  absl::string_view rest = atom;

  if (!ConsumePrefixForLiveConversionMatcher(&rest, head)) {
    return false;
  }

  if (allow_bare_head && rest.empty()) {
    return true;
  }

  const size_t sokuon_pos = rest.find("っ");
  if (sokuon_pos == absl::string_view::npos) {
    return !rest.empty() &&
           IsCasualGreetingProlongedTailWithPendingRomanS(rest);
  }

  if (!IsCasualGreetingProlongedTail(rest.substr(0, sokuon_pos))) {
    return false;
  }

  rest = rest.substr(sokuon_pos);

  if (!ConsumeRepeatedExpressiveSokuon(&rest)) {
    return false;
  }

  return rest.empty() || IsPendingRomanSForCasualSsuGreeting(rest);
}

bool IsCasualSsuGreetingPrefixAtom(absl::string_view atom) {
  return MatchesCasualSsuGreetingPrefixAtom(
             atom, "ち", /*allow_bare_head=*/false) ||
         MatchesCasualSsuGreetingPrefixAtom(
             atom, "ちょ", /*allow_bare_head=*/true) ||
         atom == "ちょr" ||
         atom == "ちょri" ||
         MatchesCasualSsuGreetingPrefixAtom(
             atom, "ちょり", /*allow_bare_head=*/true);
}

enum class EvaluativeSlangTailType {
  kNone,
  kOptionalI,
  kOptionalEe,
  kRequiredIi,
};

struct RepeatedSokuonStemPattern {
  absl::string_view head;
  absl::string_view body;
  EvaluativeSlangTailType tail_type;
};

constexpr size_t kMaxEvaluativeSlangVowelTailChars = 30;

bool IsRepeatedKanaVowelTail(absl::string_view text,
                             absl::string_view large,
                             absl::string_view small_kana) {
  size_t count = 0;

  for (absl::string_view c : Utf8AsChars(text)) {
    if (c != large && c != small_kana) {
      return false;
    }
    ++count;
    if (count > kMaxEvaluativeSlangVowelTailChars) {
      return false;
    }
  }

  return count > 0;
}

bool MatchesEvaluativeSlangTail(absl::string_view rest,
                                EvaluativeSlangTailType tail_type) {
  switch (tail_type) {
    case EvaluativeSlangTailType::kNone:
      return rest.empty();

    case EvaluativeSlangTailType::kOptionalI:
      return rest.empty() || rest == "い";

    case EvaluativeSlangTailType::kOptionalEe:
      return rest.empty() || rest == "ー" ||
             IsRepeatedKanaVowelTail(rest, "え", "ぇ");

    case EvaluativeSlangTailType::kRequiredIi:
      return rest == "ー" || IsRepeatedKanaVowelTail(rest, "い", "ぃ");
  }

  return false;
}

bool MatchesRepeatedSokuonStemPattern(
    absl::string_view atom,
    const RepeatedSokuonStemPattern& pattern) {
  absl::string_view rest = atom;

  if (!ConsumePrefixForLiveConversionMatcher(&rest, pattern.head)) {
    return false;
  }

  if (!ConsumeRepeatedExpressiveSokuon(&rest)) {
    return false;
  }

  if (!ConsumePrefixForLiveConversionMatcher(&rest, pattern.body)) {
    return false;
  }

  return MatchesEvaluativeSlangTail(rest, pattern.tail_type);
}

const RepeatedSokuonStemPattern kEvaluativeSlangPatterns[] = {
    // やばい / やべぇ
    {"や", "ば", EvaluativeSlangTailType::kOptionalI},
    {"や", "べ", EvaluativeSlangTailType::kOptionalEe},

    // すごい / すげぇ / 少ない口語形 / 酸っぱい口語形
    {"す", "ご", EvaluativeSlangTailType::kOptionalI},
    {"す", "げ", EvaluativeSlangTailType::kOptionalEe},
    {"す", "くな", EvaluativeSlangTailType::kOptionalI},
    {"す", "くね", EvaluativeSlangTailType::kOptionalEe},
    {"す", "ぱ", EvaluativeSlangTailType::kNone},
    {"す", "ぺ", EvaluativeSlangTailType::kOptionalEe},

    // 怖い / 強い / 辛い
    {"こ", "わ", EvaluativeSlangTailType::kOptionalI},
    {"つ", "よ", EvaluativeSlangTailType::kOptionalI},
    {"つ", "ら", EvaluativeSlangTailType::kOptionalI},
    {"つ", "れ", EvaluativeSlangTailType::kOptionalEe},

    // でかい / 長い / 高い / 高ぇ
    {"で", "か", EvaluativeSlangTailType::kOptionalI},
    {"な", "が", EvaluativeSlangTailType::kOptionalI},
    {"た", "か", EvaluativeSlangTailType::kOptionalI},
    {"た", "け", EvaluativeSlangTailType::kOptionalEe},

    // 小さい / 小っちゃい / 小せぇ
    {"ち", "さ", EvaluativeSlangTailType::kOptionalI},
    {"ち", "ちゃ", EvaluativeSlangTailType::kOptionalI},
    {"ち", "せ", EvaluativeSlangTailType::kOptionalEe},

    // 低い / 広い
    {"ひ", "く", EvaluativeSlangTailType::kOptionalI},
    {"ひ", "ろ", EvaluativeSlangTailType::kOptionalI},

    // 寒い / 寒ぃ
    {"さ", "む", EvaluativeSlangTailType::kOptionalI},
    {"さ", "み", EvaluativeSlangTailType::kRequiredIi},

    // 暑い / 熱い / あっちぃ
    {"あ", "つ", EvaluativeSlangTailType::kOptionalI},
    {"あ", "ち", EvaluativeSlangTailType::kRequiredIi},

    // うまい / うめぇ / うざい / うぜぇ / 薄い
    {"う", "ま", EvaluativeSlangTailType::kOptionalI},
    {"う", "め", EvaluativeSlangTailType::kOptionalEe},
    {"う", "ざ", EvaluativeSlangTailType::kOptionalI},
    {"う", "ぜ", EvaluativeSlangTailType::kOptionalEe},
    {"う", "す", EvaluativeSlangTailType::kOptionalI},

    // 軽い
    {"か", "る", EvaluativeSlangTailType::kOptionalI},

    // きつい / きちぃ / きもい / きれい
    {"き", "つ", EvaluativeSlangTailType::kOptionalI},
    {"き", "ち", EvaluativeSlangTailType::kRequiredIi},
    {"き", "も", EvaluativeSlangTailType::kOptionalI},
    {"き", "れい", EvaluativeSlangTailType::kNone},

    // だるい / だりぃ / ださい / だせぇ
    {"だ", "る", EvaluativeSlangTailType::kOptionalI},
    {"だ", "り", EvaluativeSlangTailType::kRequiredIi},
    {"だ", "さ", EvaluativeSlangTailType::kOptionalI},
    {"だ", "せ", EvaluativeSlangTailType::kOptionalEe},

    // えぐい
    {"え", "ぐ", EvaluativeSlangTailType::kOptionalI},

    // 臭い / くせぇ / 黒い
    {"く", "さ", EvaluativeSlangTailType::kOptionalI},
    {"く", "せ", EvaluativeSlangTailType::kOptionalEe},
    {"く", "ろ", EvaluativeSlangTailType::kOptionalI},

    // まぶしい
    {"ま", "ぶし", EvaluativeSlangTailType::kOptionalI},

    // 重い / 遅い / 早い
    {"お", "も", EvaluativeSlangTailType::kOptionalI},
    {"お", "そ", EvaluativeSlangTailType::kOptionalI},
    {"は", "や", EvaluativeSlangTailType::kOptionalI},

    // めっちゃ / もっと
    {"め", "ちゃ", EvaluativeSlangTailType::kNone},
    {"も", "と", EvaluativeSlangTailType::kNone},

    // 眠い / 細い / 狭い / 短い / 白い
    {"ね", "む", EvaluativeSlangTailType::kOptionalI},
    {"ほ", "そ", EvaluativeSlangTailType::kOptionalI},
    {"せ", "ま", EvaluativeSlangTailType::kOptionalI},
    {"み", "じか", EvaluativeSlangTailType::kOptionalI},
    {"し", "ろ", EvaluativeSlangTailType::kOptionalI},
};

bool IsEvaluativeSlangAdjectiveAtom(absl::string_view atom) {
  for (const RepeatedSokuonStemPattern& pattern :
       kEvaluativeSlangPatterns) {
    if (MatchesRepeatedSokuonStemPattern(atom, pattern)) {
      return true;
    }
  }

  return false;
}

bool IsEvaluativeSlangAdjectivePrefixAtom(absl::string_view atom) {
  for (const RepeatedSokuonStemPattern& pattern :
       kEvaluativeSlangPatterns) {
    if (MatchesRepeatedSokuonPrefix(atom, pattern.head)) {
      return true;
    }
  }

  return false;
}

bool MatchesUsoOrKusoFamilyExpressiveProsodyAtom(
    absl::string_view atom,
    absl::string_view head,
    bool allow_n_tail) {
  absl::string_view rest = atom;

  if (!ConsumePrefixForLiveConversionMatcher(&rest, head)) {
    return false;
  }

  if (!ConsumeRepeatedExpressiveSokuon(&rest)) {
    return false;
  }

  if (!ConsumePrefixForLiveConversionMatcher(&rest, "そ")) {
    return false;
  }

  if (rest.empty()) {
    return true;
  }

  if (IsOnlyLiveConversionProlongationMarks(rest)) {
    return true;
  }

  if (allow_n_tail &&
      IsLiveConversionProlongationMarksWithOptionalN(rest)) {
    return true;
  }

  return false;
}

bool IsUsoOrKusoFamilyExpressiveProsodyAtom(absl::string_view atom) {
  // Match 「うっそ」「うっっそ」「うっそー」「うっそん」 etc.
  // Do not match bare 「うそ」 or 「うっそう」 because 「嘘」 and 「鬱蒼」
  // are useful live-conversion targets.
  if (MatchesUsoOrKusoFamilyExpressiveProsodyAtom(
          atom, "う", /*allow_n_tail=*/true)) {
    return true;
  }

  // Match 「くっそ」「くっっそ」「くっそー」 etc.  Keep bare 「くそ」
  // in the boundary-sensitive lexical list, and do not match 「くそう」.
  return MatchesUsoOrKusoFamilyExpressiveProsodyAtom(
      atom, "く", /*allow_n_tail=*/false);
}

bool IsUsoOrKusoFamilyExpressiveProsodyPrefixAtom(
    absl::string_view atom) {
  return MatchesRepeatedSokuonPrefix(atom, "う") ||
         MatchesRepeatedSokuonPrefix(atom, "く");
}

bool MatchesUhyoFamilyExpressiveProsodyAtom(absl::string_view atom) {
  absl::string_view rest = atom;

  if (!ConsumePrefixForLiveConversionMatcher(&rest, "う")) {
    return false;
  }

  // Accept both 「うひょ」/「うひゃ」 and emphasized forms such as
  // 「うっひょ」, 「うっひゃ」, 「うっっひょ」 and 「うっっひゃ」.
  if (!ConsumeAnyPrefixForLiveConversionMatcher(&rest, {"ひょ", "ひゃ"})) {
    if (!ConsumeRepeatedExpressiveSokuon(&rest)) {
      return false;
    }
    if (!ConsumeAnyPrefixForLiveConversionMatcher(&rest, {"ひょ", "ひゃ"})) {
      return false;
    }
  }

  if (rest.empty()) {
    return true;
  }

  if (IsOnlyLiveConversionProlongationMarks(rest)) {
    return true;
  }

  if (IsLiveConversionProlongationMarksWithOptionalN(rest)) {
    return true;
  }

  if (IsLiveConversionProlongationMarksWithOptionalI(rest)) {
    return true;
  }

  return false;
}

bool IsUhyoFamilyExpressiveProsodyAtom(absl::string_view atom) {
  return MatchesUhyoFamilyExpressiveProsodyAtom(atom);
}

bool IsUhyoFamilyExpressiveProsodyPrefixAtom(absl::string_view atom) {
  absl::string_view rest = atom;

  if (!ConsumePrefixForLiveConversionMatcher(&rest, "う")) {
    return false;
  }

  if (rest == "ひ") {
    return true;
  }

  if (!ConsumeRepeatedExpressiveSokuon(&rest)) {
    return false;
  }

  return rest == "ひ";
}

bool FindEvaluativeSlangAdjectiveOrPrefixSuffix(
    absl::string_view text,
    absl::string_view* suffix) {
  size_t offset = 0;
  for (absl::string_view c : Utf8AsChars(text)) {
    const absl::string_view candidate = text.substr(offset);
    if (IsEvaluativeSlangAdjectiveAtom(candidate) ||
        IsEvaluativeSlangAdjectivePrefixAtom(candidate)) {
      *suffix = candidate;
      return true;
    }
    offset += c.size();
  }

  return false;
}

bool HasShortPrefixBeforeSuffix(absl::string_view text,
                                absl::string_view suffix,
                                size_t max_prefix_chars) {
  if (suffix.data() < text.data() ||
      suffix.data() + suffix.size() > text.data() + text.size()) {
    return false;
  }

  const size_t prefix_bytes = suffix.data() - text.data();
  return Util::CharsLen(text.substr(0, prefix_bytes)) <= max_prefix_chars;
}

bool ShouldHoldEvaluativeSlangAdjectiveForLiveConversion(
    const LiveConversionAtom& atom,
    absl::string_view lexical_core) {
  if (IsEvaluativeSlangAdjectiveAtom(atom.text) ||
      IsEvaluativeSlangAdjectivePrefixAtom(atom.text)) {
    // Evaluative slang such as 「やっば」「なっがい」 is often typed after
    // already-committed context, e.g. 「これ」 + 「やっば」.  Keep the atom
    // as kana when the whole current composition is the slang atom, or when
    // it appears after a clear in-composition boundary.
    return atom.is_entire_composition ||
           atom.left_boundary == LiveConversionLeftBoundary::kKnownBoundary;
  }

  absl::string_view suffix;
  if (!FindEvaluativeSlangAdjectiveOrPrefixSuffix(lexical_core, &suffix)) {
    return false;
  }

  // Also protect short colloquial phrases such as 「これやっば」 and
  // 「まじでなっがい」, including their typing prefixes such as 「これやっ」.
  // Avoid suppressing live conversion for long sentences whose tail merely
  // happens to be evaluative slang.
  return HasShortPrefixBeforeSuffix(
      lexical_core, suffix, kMaxEvaluativeSlangPrefixChars);
}

bool CanHoldLowAmbiguityExpressiveAtom(const LiveConversionAtom& atom) {
  return atom.is_entire_composition ||
         atom.left_boundary == LiveConversionLeftBoundary::kKnownBoundary;
}

bool CanHoldBoundarySensitiveExpressiveAtom(const LiveConversionAtom& atom) {
  if (atom.left_boundary == LiveConversionLeftBoundary::kKnownNonBoundary) {
    return false;
  }

  return atom.is_entire_composition ||
         atom.left_boundary == LiveConversionLeftBoundary::kKnownBoundary;
}

bool ShouldHoldExpressiveKanaAtomForLiveConversion(
    const LiveConversionAtom& atom,
    absl::string_view expressive_core) {
  if (atom.text.empty()) {
    return false;
  }

  if (IsHoFamilyExpressiveProsodyAtom(atom.text) ||
      IsUhyoFamilyExpressiveProsodyAtom(atom.text) ||
      IsUhyoFamilyExpressiveProsodyPrefixAtom(atom.text) ||
      IsUsoOrKusoFamilyExpressiveProsodyAtom(atom.text) ||
      IsUsoOrKusoFamilyExpressiveProsodyPrefixAtom(atom.text) ||
      IsCasualSsuGreetingAtom(atom.text) ||
      IsCasualSsuGreetingPrefixAtom(atom.text)) {
    return CanHoldLowAmbiguityExpressiveAtom(atom);
  }

  const absl::string_view lexical_atom =
      StripTrailingLiveConversionProlongationMarks(atom.text);
  const absl::string_view lexical_core =
      StripTrailingLiveConversionProlongationMarks(expressive_core);

  if (!lexical_atom.empty() &&
      Util::IsScriptType(lexical_atom, Util::HIRAGANA)) {
    if (IsRepeatedEInterjectionAtom(lexical_atom)) {
      return CanHoldLowAmbiguityExpressiveAtom(atom);
    }

    if (IsStrongExpressiveKanaAtom(lexical_atom)) {
      return CanHoldLowAmbiguityExpressiveAtom(atom);
    }

    if (IsSokuonEndingMimeticAtom(lexical_atom)) {
      return CanHoldBoundarySensitiveExpressiveAtom(atom);
    }

    if (IsBoundarySensitiveExpressiveKanaAtom(lexical_atom)) {
      return CanHoldBoundarySensitiveExpressiveAtom(atom);
    }
  }

  if (!lexical_core.empty() &&
      Util::IsScriptType(lexical_core, Util::HIRAGANA) &&
      ShouldHoldEvaluativeSlangAdjectiveForLiveConversion(atom,
                                                          lexical_core)) {
    return true;
  }

  return false;
}

bool FindEvaluativeSlangAdjectivePrefixSuffix(
    absl::string_view text,
    absl::string_view* suffix) {
  size_t offset = 0;
  for (absl::string_view c : Utf8AsChars(text)) {
    const absl::string_view candidate = text.substr(offset);
    if (IsEvaluativeSlangAdjectivePrefixAtom(candidate)) {
      *suffix = candidate;
      return true;
    }
    offset += c.size();
  }

  return false;
}

bool ShouldHoldEvaluativeSlangAdjectivePrefixForLiveConversion(
    const LiveConversionAtom& atom,
    absl::string_view lexical_core) {
  if (IsEvaluativeSlangAdjectivePrefixAtom(atom.text)) {
    return atom.is_entire_composition ||
           atom.left_boundary == LiveConversionLeftBoundary::kKnownBoundary;
  }

  absl::string_view suffix;
  if (!FindEvaluativeSlangAdjectivePrefixSuffix(lexical_core, &suffix)) {
    return false;
  }

  // Protect short colloquial phrase prefixes such as 「これやっ」 or
  // 「まじでなっ」, but let completed forms like 「これやっば」 reach the
  // converter so user history and user dictionary preferences can apply.
  return HasShortPrefixBeforeSuffix(
      lexical_core, suffix, kMaxEvaluativeSlangPrefixChars);
}

bool ShouldHoldExpressiveKanaTypingPrefixForLiveConversion(
    const LiveConversionAtom& atom,
    absl::string_view expressive_core) {
  if (atom.text.empty()) {
    return false;
  }

  if (IsUhyoFamilyExpressiveProsodyPrefixAtom(atom.text) ||
      IsUsoOrKusoFamilyExpressiveProsodyPrefixAtom(atom.text) ||
      IsCasualSsuGreetingPrefixAtom(atom.text)) {
    return CanHoldLowAmbiguityExpressiveAtom(atom);
  }

  const absl::string_view lexical_atom =
      StripTrailingLiveConversionProlongationMarks(atom.text);
  const absl::string_view lexical_core =
      StripTrailingLiveConversionProlongationMarks(expressive_core);

  if (!lexical_atom.empty() &&
      Util::IsScriptType(lexical_atom, Util::HIRAGANA) &&
      ShouldHoldEvaluativeSlangAdjectivePrefixForLiveConversion(
          atom, lexical_core)) {
    return true;
  }

  return false;
}

bool ShouldSkipLiveConversionForCompositionKey(
    absl::string_view key,
    LiveConversionLeftBoundary committed_left_boundary,
    size_t min_key_length) {
  if (Util::CharsLen(key) < min_key_length) {
    return true;
  }

  // Keep only unfinished expressive typing prefixes out of live conversion.
  // Completed expressive words should reach the converter so that normal
  // dictionary ranking, user history, and user dictionary entries can decide
  // between hiragana and katakana spellings.
  const absl::string_view expressive_core =
      StripTrailingLiveConversionSentenceTail(key);
  if (!expressive_core.empty()) {
    const LiveConversionAtom atom =
        ExtractTrailingLiveConversionAtom(expressive_core,
                                          committed_left_boundary);
    if (ShouldHoldExpressiveKanaTypingPrefixForLiveConversion(
            atom, expressive_core)) {
      return true;
    }
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
  if (Util::CharsLen(core) >= min_key_length) {
    return false;
  }

  return Util::IsScriptType(core, Util::HIRAGANA);
}

bool ShouldKeepPendingLiveConversionForTransientSokuon(absl::string_view key) {
  if (key.empty() || !Util::IsScriptType(key, Util::HIRAGANA)) {
    return false;
  }

  absl::string_view rest;
  char32_t last = 0;
  if (!Util::SplitLastChar32(key, &rest, &last) || last != U'っ') {
    return false;
  }

  if (Util::CharsLen(rest) < 2) {
    return false;
  }

  absl::string_view before_rest;
  char32_t previous = 0;
  if (Util::SplitLastChar32(rest, &before_rest, &previous) &&
      previous == U'っ') {
    return false;
  }

  // This is aimed at transient sokuon forms that commonly recover on the
  // next kana, e.g. 「おもっ」→「おもって」 and 「くさっ」→「くさって」.
  // Keep this list narrow so expressive atoms such as 「やっっ」 and 「ふんっ」
  // continue to use the existing composition-only behavior.
  const auto has_transient_stem_suffix = [&](absl::string_view stem) {
    if (!absl::EndsWith(rest, stem)) {
      return false;
    }

    const absl::string_view prefix = rest.substr(0, rest.size() - stem.size());
    if (prefix.empty()) {
      return true;
    }

    // Allow common short functional prefixes within the same composition,
    // e.g. 「とおもっ」→「と思って」 and 「をさわっ」→「を触って」.
    return ContainsStringView({
        "と", "を", "に", "が", "は", "も", "で", "へ",
        "から", "まで", "より",
    }, prefix);
  };

  return has_transient_stem_suffix("おも") ||  // 思っ...
         has_transient_stem_suffix("くさ") ||  // 腐っ...
         has_transient_stem_suffix("さわ") ||  // 触っ...
         has_transient_stem_suffix("かえ") ||  // 帰っ...
         has_transient_stem_suffix("わら") ||  // 笑っ...
         has_transient_stem_suffix("おこ") ||  // 怒っ...
         has_transient_stem_suffix("まよ") ||  // 迷っ...
         has_transient_stem_suffix("のこ") ||  // 残っ...
         has_transient_stem_suffix("ひろ") ||  // 拾っ...
         has_transient_stem_suffix("ふと") ||  // 太っ...
         has_transient_stem_suffix("あた") ||  // 当たっ...
         has_transient_stem_suffix("あら") ||  // 洗っ...
         has_transient_stem_suffix("うつ") ||  // 打っ...
         has_transient_stem_suffix("うた") ||  // 歌っ...
         has_transient_stem_suffix("けず") ||  // 削っ...
         has_transient_stem_suffix("しぼ") ||  // 絞っ...
         has_transient_stem_suffix("すわ") ||  // 座っ...
         has_transient_stem_suffix("とま") ||  // 止まっ...
         has_transient_stem_suffix("なお") ||  // 直っ...
         has_transient_stem_suffix("のぼ") ||  // 登っ...
         has_transient_stem_suffix("はし") ||  // 走っ...
         has_transient_stem_suffix("まわ");    // 回っ...
}

bool IsAsciiIdentityChar(const unsigned char c) {
  return (('0' <= c) && (c <= '9')) || (('A' <= c) && (c <= 'Z')) ||
         (('a' <= c) && (c <= 'z')) || c == '_' || c == '-' || c == '.' ||
         c == '+' || c == '#';
}

bool HasAsciiLetterOrDigit(absl::string_view value) {
  for (const unsigned char c : value) {
    if ((('0' <= c) && (c <= '9')) || (('A' <= c) && (c <= 'Z')) ||
        (('a' <= c) && (c <= 'z'))) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> ExtractAsciiIdentitySurfaces(
    absl::string_view value) {
  std::vector<std::string> surfaces;
  size_t start = absl::string_view::npos;

  auto flush = [&](size_t end) {
    if (start == absl::string_view::npos || end <= start) {
      start = absl::string_view::npos;
      return;
    }
    const absl::string_view surface = value.substr(start, end - start);
    if (HasAsciiLetterOrDigit(surface)) {
      surfaces.emplace_back(surface);
    }
    start = absl::string_view::npos;
  };

  for (size_t i = 0; i < value.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if (IsAsciiIdentityChar(c)) {
      if (start == absl::string_view::npos) {
        start = i;
      }
    } else {
      flush(i);
    }
  }
  flush(value.size());

  return surfaces;
}

std::string FindPreeditKeyForValue(const commands::Preedit& preedit,
                                   absl::string_view value) {
  for (int i = 0; i < preedit.segment_size(); ++i) {
    const commands::Preedit::Segment& segment = preedit.segment(i);
    if (segment.value() == value) {
      return segment.key();
    }
  }
  return std::string();
}

ProtectedConversionSpan* FindProtectedSurface(
    std::vector<ProtectedConversionSpan>* protected_spans,
    absl::string_view value) {
  if (protected_spans == nullptr) {
    return nullptr;
  }
  for (ProtectedConversionSpan& span : *protected_spans) {
    if (span.value == value) {
      return &span;
    }
  }
  return nullptr;
}

size_t CountSurfaceOccurrences(absl::string_view value,
                               absl::string_view surface) {
  if (surface.empty()) {
    return 0;
  }

  size_t count = 0;
  size_t pos = 0;
  while ((pos = value.find(surface, pos)) != absl::string_view::npos) {
    ++count;
    pos += surface.size();
  }
  return count;
}

struct ZenzReverseLearningProjection {
  std::vector<std::pair<std::string, std::string>> changed_segments;
  std::vector<ZenzProjectedLearningSegment> projected_segments;
};

ZenzReverseLearningProjection BuildZenzReverseLearningSegmentsFromPreedit(
    const commands::Preedit& preedit,
    absl::string_view full_key,
    absl::string_view full_value) {
  constexpr int kMaxReverseLearningPairs = 4;

  const int segment_size = preedit.segment_size();
  if (segment_size <= 0 || full_key.empty() || full_value.empty()) {
    return {};
  }

  std::string concatenated_key;
  std::vector<std::string> keys;
  std::vector<std::string> mozc_values;
  keys.reserve(segment_size);
  mozc_values.reserve(segment_size);

  for (int i = 0; i < segment_size; ++i) {
    const commands::Preedit::Segment& segment = preedit.segment(i);
    if (segment.key().empty() || segment.value().empty()) {
      return {};
    }
    keys.push_back(segment.key());
    mozc_values.push_back(segment.value());
    concatenated_key.append(segment.key());
  }

  // The snapshot must describe the same full Zenz request.  Otherwise it may
  // belong to an older live-conversion generation and must not be used.
  if (concatenated_key != full_key) {
    return {};
  }

  struct Anchor {
    int index;
    size_t begin;
    size_t end;
  };

  std::vector<Anchor> anchors;
  anchors.reserve(segment_size);
  for (int i = 0; i < segment_size; ++i) {
    const std::string& value = mozc_values[i];
    if (CountSurfaceOccurrences(full_value, value) != 1) {
      continue;
    }
    const size_t begin = full_value.find(value);
    if (begin == absl::string_view::npos) {
      return {};
    }
    anchors.push_back(Anchor{i, begin, begin + value.size()});
  }

  size_t previous_anchor_end = 0;
  for (const Anchor& anchor : anchors) {
    if (anchor.begin < previous_anchor_end) {
      return {};
    }
    previous_anchor_end = anchor.end;
  }

  std::vector<std::string> projected_values(segment_size);
  int left_index = -1;
  size_t left_value_end = 0;

  auto assign_gap = [&](int first_index, int last_index,
                        absl::string_view value) -> bool {
    const int gap_size = last_index - first_index + 1;
    if (gap_size <= 0) {
      return value.empty();
    }

    if (gap_size == 1) {
      if (value.empty()) {
        return false;
      }
      projected_values[first_index] = std::string(value);
      return true;
    }

    std::string original_gap_value;
    for (int i = first_index; i <= last_index; ++i) {
      original_gap_value.append(mozc_values[i]);
    }
    if (original_gap_value != value) {
      return false;
    }
    for (int i = first_index; i <= last_index; ++i) {
      projected_values[i] = mozc_values[i];
    }
    return true;
  };

  for (const Anchor& anchor : anchors) {
    if (!assign_gap(left_index + 1, anchor.index - 1,
                    full_value.substr(left_value_end,
                                      anchor.begin - left_value_end))) {
      return {};
    }
    projected_values[anchor.index] = mozc_values[anchor.index];
    left_index = anchor.index;
    left_value_end = anchor.end;
  }

  if (!assign_gap(left_index + 1, segment_size - 1,
                  full_value.substr(left_value_end))) {
    return {};
  }

  std::string reconstructed_value;
  for (const std::string& value : projected_values) {
    if (value.empty()) {
      return {};
    }
    reconstructed_value.append(value);
  }
  if (reconstructed_value != full_value) {
    return {};
  }

  ZenzReverseLearningProjection result;
  result.projected_segments.reserve(segment_size);
  for (int i = 0; i < segment_size; ++i) {
    result.projected_segments.push_back(
        {keys[i], projected_values[i],
         projected_values[i] != mozc_values[i]});
  }

  for (int i = 0; i < segment_size; ++i) {
    // Full-sequence learning already covers the whole accepted result.  The
    // reverse path records only segments that Zenz actually changed relative to
    // the visible Mozc live-conversion result.
    if (projected_values[i] == mozc_values[i]) {
      continue;
    }
    if (keys[i] == full_key && projected_values[i] == full_value) {
      continue;
    }
    result.changed_segments.push_back({keys[i], projected_values[i]});
    if (result.changed_segments.size() > kMaxReverseLearningPairs) {
      return {};
    }
  }

  // If the correction rewrites too many independent segments, it is likely a
  // style-level rewrite rather than a stable local conversion preference.
  if (result.changed_segments.empty()) {
    result.projected_segments.clear();
  }
  return result;
}


std::string InferProtectedKeyForEmbeddedSurface(absl::string_view segment_key,
                                                absl::string_view segment_value,
                                                absl::string_view surface) {
  if (segment_key.empty() || segment_value.empty() || surface.empty()) {
    return std::string();
  }

  if (segment_value == surface) {
    return std::string(segment_key);
  }

  // Keep this inference deliberately narrow.  It is safe to infer the protected
  // reading when the protected surface starts the segment and the remaining
  // value suffix appears literally at the end of the segment key, e.g.
  //   key:   もずきーを
  //   value: Mozkeyを
  //   span:  もずきー -> Mozkey
  // For converted suffixes such as "Mozkeyを使用しています" vs
  // "もずきーをしようしています", this returns empty and leaves the span
  // reject-only instead of guessing a boundary.
  if (!absl::StartsWith(segment_value, surface)) {
    return std::string();
  }

  const absl::string_view suffix = segment_value.substr(surface.size());
  if (suffix.empty() || suffix.size() >= segment_key.size()) {
    return std::string();
  }
  if (!absl::EndsWith(segment_key, suffix)) {
    return std::string();
  }

  return std::string(segment_key.substr(0, segment_key.size() - suffix.size()));
}

std::string FindPreeditKeyForProtectedSurface(
    const commands::Preedit& preedit, absl::string_view surface,
    absl::string_view candidate_key) {
  if (surface.empty()) {
    return std::string();
  }

  for (int i = 0; i < preedit.segment_size(); ++i) {
    const commands::Preedit::Segment& segment = preedit.segment(i);
    if (segment.value().empty()) {
      continue;
    }

    const std::string segment_key =
        segment.has_key() ? segment.key() : std::string();
    const std::string protected_key = InferProtectedKeyForEmbeddedSurface(
        segment_key, segment.value(), surface);
    if (!protected_key.empty()) {
      return protected_key;
    }

    // Some live-conversion preedit segments can be larger than the protected
    // word, e.g. "じしょごのてんてきです" -> "辞書語の点滴です".
    // In that case the visible suffix is converted, so literal suffix matching
    // cannot infer the boundary.  If the user-dictionary key and value both
    // start the same preedit segment, use the candidate key as the protected
    // reading.
    if (!candidate_key.empty() && absl::StartsWith(segment.value(), surface) &&
        absl::StartsWith(segment_key, candidate_key)) {
      return std::string(candidate_key);
    }
  }
  return std::string();
}

void AddOrUpdateProtectedSpan(
    absl::string_view key, absl::string_view value,
    ProtectedConversionSpan::Tier tier, bool repairable,
    absl::string_view mozc_value,
    std::vector<ProtectedConversionSpan>* protected_spans) {
  if (protected_spans == nullptr || value.empty() ||
      !absl::StrContains(mozc_value, value)) {
    return;
  }

  size_t required_occurrences = CountSurfaceOccurrences(mozc_value, value);
  if (required_occurrences == 0) {
    required_occurrences = 1;
  }

  if (ProtectedConversionSpan* existing =
          FindProtectedSurface(protected_spans, value)) {
    if (existing->required_occurrences < required_occurrences) {
      existing->required_occurrences = required_occurrences;
    }
    if (existing->key.empty() && !key.empty()) {
      existing->key = std::string(key);
    }
    if (tier == ProtectedConversionSpan::Tier::kIdentityCritical) {
      existing->tier = tier;
    }
    if (!existing->repairable && repairable && !key.empty()) {
      existing->repairable = true;
    }
    return;
  }

  ProtectedConversionSpan span;
  span.key = !key.empty() ? std::string(key) : std::string();
  span.value = std::string(value);
  span.tier = tier;
  span.repairable = repairable && !span.key.empty();
  span.required_occurrences = required_occurrences;
  protected_spans->push_back(std::move(span));
}

void AddPreeditIdentityProtectedSpans(
    const commands::Preedit& preedit, absl::string_view mozc_value,
    std::vector<ProtectedConversionSpan>* protected_spans) {
  for (int i = 0; i < preedit.segment_size(); ++i) {
    const commands::Preedit::Segment& segment = preedit.segment(i);
    if (segment.value().empty()) {
      continue;
    }

    const std::string segment_key =
        segment.has_key() ? segment.key() : std::string();
    const std::vector<std::string> identity_surfaces =
        ExtractAsciiIdentitySurfaces(segment.value());
    for (const std::string& surface : identity_surfaces) {
      const std::string surface_key = InferProtectedKeyForEmbeddedSurface(
          segment_key, segment.value(), surface);
      AddOrUpdateProtectedSpan(
          surface_key, surface, ProtectedConversionSpan::Tier::kIdentityCritical,
          !surface_key.empty(), mozc_value, protected_spans);
    }
  }
}


bool IsSafeDirectUserDictionaryProtectedEntry(absl::string_view key,
                                              absl::string_view value) {
  if (key.empty() || value.empty()) {
    return false;
  }

  // Very short entries are too ambiguous for direct global matching. They can
  // still be protected through candidate provenance when the converter exposes
  // the selected user-dictionary candidate.
  if (Util::CharsLen(key) < 2 || Util::CharsLen(value) < 2) {
    return false;
  }

  return true;
}

std::vector<absl::string_view> GetUtf8SuffixesForUserDictionaryLookup(
    absl::string_view key) {
  std::vector<absl::string_view> suffixes;
  if (key.empty()) {
    return suffixes;
  }

  suffixes.reserve(Util::CharsLen(key));
  for (size_t i = 0; i < key.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(key[i]);
    if (i != 0 && (c & 0xC0) == 0x80) {
      continue;
    }
    suffixes.push_back(key.substr(i));
  }
  return suffixes;
}

void AddDirectUserDictionaryEntryProtectedSpans(
    const engine::EngineConverterInterface& converter,
    absl::string_view live_key, absl::string_view mozc_value,
    std::vector<ProtectedConversionSpan>* protected_spans) {
  if (protected_spans == nullptr || live_key.empty() || mozc_value.empty()) {
    return;
  }

  for (const absl::string_view suffix :
       GetUtf8SuffixesForUserDictionaryLookup(live_key)) {
    std::vector<UserDictionaryLookupResult> entries;
    converter.LookupUserDictionaryPrefixEntries(suffix, &entries);
    for (const UserDictionaryLookupResult& entry : entries) {
      const absl::string_view entry_key(entry.key);
      const absl::string_view entry_value(entry.value);
      if (!IsSafeDirectUserDictionaryProtectedEntry(entry_key, entry_value)) {
        continue;
      }
      if (!absl::StartsWith(suffix, entry_key) ||
          !absl::StrContains(mozc_value, entry_value)) {
        continue;
      }

      AddOrUpdateProtectedSpan(
          entry_key, entry_value, ProtectedConversionSpan::Tier::kUserPreferred,
          false, mozc_value, protected_spans);
    }
  }
}

std::vector<ProtectedConversionSpan> BuildZenzProtectedConversionSpans(
    const engine::EngineConverterInterface& converter,
    const commands::Output& output, absl::string_view live_key,
    absl::string_view mozc_value) {
  std::vector<ProtectedConversionSpan> protected_spans;

  if (output.has_all_candidate_words()) {
    const commands::CandidateList& candidate_list = output.all_candidate_words();
    const uint32_t focused_index = candidate_list.has_focused_index()
                                     ? candidate_list.focused_index()
                                     : 0;

    for (int i = 0; i < candidate_list.candidates_size(); ++i) {
      const commands::CandidateWord& candidate = candidate_list.candidates(i);
      const bool focused_candidate = candidate.index() == focused_index;

      if (!IsZenzProtectedDictionaryCandidate(candidate)) {
        continue;
      }

      if (candidate.value().empty()) {
        continue;
      }

      std::string candidate_key =
          candidate.has_key() ? candidate.key() : std::string();
      if (candidate_key.empty() && output.has_preedit()) {
        candidate_key =
            FindPreeditKeyForValue(output.preedit(), candidate.value());
      }

      std::string preedit_protected_key;
      if (output.has_preedit()) {
        preedit_protected_key = FindPreeditKeyForProtectedSurface(
            output.preedit(), candidate.value(), candidate_key);
      }

      // For non-focused candidates, protect only when the same surface is
      // actually visible in the current preedit.  This recovers embedded
      // user-dictionary words such as "じしょごの" -> "辞書語の" without pinning
      // unrelated user-dictionary candidates that merely exist in the candidate
      // list.
      if (!focused_candidate && preedit_protected_key.empty()) {
        continue;
      }

      const std::string protected_key = !preedit_protected_key.empty()
                                            ? preedit_protected_key
                                            : candidate_key;

      const std::vector<std::string> identity_surfaces =
          ExtractAsciiIdentitySurfaces(candidate.value());
      if (!identity_surfaces.empty()) {
        for (const std::string& surface : identity_surfaces) {
          const std::string surface_key = InferProtectedKeyForEmbeddedSurface(
              protected_key, candidate.value(), surface);
          AddOrUpdateProtectedSpan(
              surface_key, surface, ProtectedConversionSpan::Tier::kIdentityCritical,
              !surface_key.empty(), mozc_value, &protected_spans);
        }
        continue;
      }

      AddOrUpdateProtectedSpan(
          protected_key, candidate.value(),
          ProtectedConversionSpan::Tier::kUserPreferred, false, mozc_value,
          &protected_spans);
    }
  }

  if (output.has_preedit()) {
    AddPreeditIdentityProtectedSpans(output.preedit(), mozc_value,
                                     &protected_spans);
  }

  AddDirectUserDictionaryEntryProtectedSpans(converter, live_key, mozc_value,
                                             &protected_spans);

  return protected_spans;
}

uint32_t GetLiveConversionDelayMillisec(const config::Config& config) {
  if (!config.has_live_conversion_delay_msec()) {
    return kDefaultLiveConversionDelayMillisec;
  }
  return std::min(config.live_conversion_delay_msec(),
                  kMaxLiveConversionDelayMillisec);
}

size_t GetLiveConversionMinKeyLength(const config::Config& config) {
  const uint32_t value = config.has_live_conversion_min_key_length()
                             ? config.live_conversion_min_key_length()
                             : kDefaultLiveConversionMinKeyLength;
  return static_cast<size_t>(
      std::clamp(value,
                 kMinLiveConversionMinKeyLength,
                 kMaxLiveConversionMinKeyLength));
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

uint32_t GetZenzLiveCorrectionRightContextLength(
    const config::Config& config) {
  if (!config.use_zenz_live_correction_right_context()) {
    return 0;
  }

  const uint32_t length =
      config.has_zenz_live_correction_right_context_length()
          ? config.zenz_live_correction_right_context_length()
          : kDefaultZenzLiveCorrectionRightContextLength;
  return std::min<uint32_t>(length,
                            kMaxZenzLiveCorrectionRightContextLength);
}

bool UseZenzFeedbackLearning(const config::Config& config) {
  return config.use_zenz_feedback_learning();
}

ZenzFeedbackAutoBlockPolicy GetZenzFeedbackAutoBlockPolicy(
    const config::Config& config) {
  ZenzFeedbackAutoBlockPolicy policy;
  policy.enabled = config.use_zenz_auto_block_rejected_correction();
  policy.reject_threshold =
      static_cast<int>(config.zenz_auto_block_reject_threshold());
  return policy;
}

bool IsExplicitZenzHardRejectReason(absl::string_view reason) {
  return reason == "hard_reject" ||
         reason == "user_hard_reject" ||
         reason == "manual_hard_reject" ||
         reason == "explicit_hard_reject";
}

bool StartsWithString(absl::string_view text, absl::string_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

constexpr size_t kMaxZenzLiveCorrectionKeyChars = 64;
constexpr size_t kMaxZenzLiveCorrectionValueChars = 128;

struct ZenzTextPrivacyDecision {
  bool allow = false;
  const char* reason = "unspecified";
};

bool IsAsciiAlpha(unsigned char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

bool IsAsciiDigit(unsigned char c) {
  return '0' <= c && c <= '9';
}

bool IsAsciiAlnum(unsigned char c) {
  return IsAsciiAlpha(c) || IsAsciiDigit(c);
}

bool IsAsciiControl(unsigned char c) {
  return c < 0x20 || c == 0x7f;
}

std::string ToLowerAscii(absl::string_view text) {
  std::string result;
  result.reserve(text.size());

  for (const unsigned char c : text) {
    if ('A' <= c && c <= 'Z') {
      result.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      result.push_back(static_cast<char>(c));
    }
  }

  return result;
}

bool ContainsAsciiSubstring(const std::string& text,
                            absl::string_view needle) {
  if (needle.empty()) {
    return true;
  }

  return text.find(needle.data(), 0, needle.size()) != std::string::npos;
}

bool ContainsAsciiControl(absl::string_view text) {
  for (const unsigned char c : text) {
    if (IsAsciiControl(c)) {
      return true;
    }
  }
  return false;
}

bool IsJapaneseScriptSignal(char32_t c) {
  // Hiragana
  if (0x3040 <= c && c <= 0x309F) {
    return true;
  }

  // Katakana
  if (0x30A0 <= c && c <= 0x30FF) {
    return true;
  }

  // Halfwidth Katakana
  if (0xFF66 <= c && c <= 0xFF9F) {
    return true;
  }

  // CJK Unified Ideographs
  if (0x4E00 <= c && c <= 0x9FFF) {
    return true;
  }

  // CJK Unified Ideographs Extension A
  if (0x3400 <= c && c <= 0x4DBF) {
    return true;
  }

  // CJK Compatibility Ideographs
  if (0xF900 <= c && c <= 0xFAFF) {
    return true;
  }

  // CJK extensions outside BMP.
  if ((0x20000 <= c && c <= 0x2A6DF) ||
      (0x2A700 <= c && c <= 0x2B73F) ||
      (0x2B740 <= c && c <= 0x2B81F) ||
      (0x2B820 <= c && c <= 0x2CEAF) ||
      (0x30000 <= c && c <= 0x3134F)) {
    return true;
  }

  return false;
}

bool ContainsJapaneseScriptSignal(absl::string_view text) {
  for (ConstChar32Iterator iter(text); !iter.Done(); iter.Next()) {
    if (IsJapaneseScriptSignal(iter.Get())) {
      return true;
    }
  }
  return false;
}

bool LooksLikeUrlOrDomain(absl::string_view text) {
  const std::string lower = ToLowerAscii(text);

  if (lower.find("://") != std::string::npos ||
      lower.find("www.") != std::string::npos) {
    return true;
  }

  constexpr absl::string_view kDomainSuffixes[] = {
      ".com",
      ".net",
      ".org",
      ".jp",
      ".co.jp",
      ".io",
      ".dev",
      ".app",
      ".local",
      ".localhost",
  };

  for (const absl::string_view suffix : kDomainSuffixes) {
    if (ContainsAsciiSubstring(lower, suffix)) {
      return true;
    }
  }

  return false;
}

bool LooksLikeEmail(absl::string_view text) {
  // Broad by design. In Japanese composition, raw '@' almost always means an
  // address-like or handle-like token. Keep it out of the local model path.
  return text.find('@') != absl::string_view::npos;
}

bool LooksLikePath(absl::string_view text) {
  const std::string lower = ToLowerAscii(text);

  if (text.find('\\') != absl::string_view::npos) {
    return true;
  }

  if (lower.size() >= 3 &&
      IsAsciiAlpha(static_cast<unsigned char>(lower[0])) &&
      lower[1] == ':' &&
      (lower[2] == '\\' || lower[2] == '/')) {
    return true;
  }

  if (StartsWithString(lower, "/") ||
      StartsWithString(lower, "~/") ||
      lower.find("../") != std::string::npos ||
      lower.find("./") != std::string::npos) {
    return true;
  }

  return false;
}

bool IsAsciiTokenChar(unsigned char c) {
  return IsAsciiAlnum(c) || c == '_' || c == '-' || c == '.';
}

bool IsAsciiHexDigit(unsigned char c) {
  return IsAsciiDigit(c) ||
         ('a' <= c && c <= 'f') ||
         ('A' <= c && c <= 'F');
}

bool IsIpv4LikeAsciiToken(absl::string_view token) {
  size_t i = 0;
  int group_count = 0;

  while (i < token.size()) {
    if (group_count >= 4) {
      return false;
    }

    const size_t start = i;
    int value = 0;

    while (i < token.size() &&
           IsAsciiDigit(static_cast<unsigned char>(token[i]))) {
      value = value * 10 + (token[i] - '0');
      if (value > 255) {
        return false;
      }
      ++i;
    }

    if (i == start) {
      return false;
    }

    ++group_count;

    if (group_count == 4) {
      break;
    }

    if (i >= token.size() || token[i] != '.') {
      return false;
    }

    ++i;
  }

  return group_count == 4 && i == token.size();
}

bool IsVersionLikeAsciiToken(absl::string_view token) {
  if (token.empty()) {
    return false;
  }

  size_t i = 0;

  if (i < token.size() && (token[i] == 'v' || token[i] == 'V')) {
    ++i;
  }

  const auto consume_digits = [&token, &i]() {
    const size_t start = i;
    while (i < token.size() &&
           IsAsciiDigit(static_cast<unsigned char>(token[i]))) {
      ++i;
    }
    return i > start;
  };

  if (!consume_digits()) {
    return false;
  }

  size_t numeric_group_count = 1;
  bool saw_dot = false;

  while (i < token.size() && token[i] == '.') {
    saw_dot = true;
    ++i;

    if (!consume_digits()) {
      return false;
    }

    ++numeric_group_count;
    if (numeric_group_count > 4) {
      return false;
    }
  }

  // Require at least one dot so plain "v12345678" is not treated as a
  // harmless version string.
  if (!saw_dot) {
    return false;
  }

  if (i < token.size() && token[i] == '-') {
    ++i;

    const size_t suffix_start = i;
    while (i < token.size() &&
           IsAsciiAlpha(static_cast<unsigned char>(token[i]))) {
      ++i;
    }

    if (i == suffix_start) {
      return false;
    }

    while (i < token.size() &&
           IsAsciiDigit(static_cast<unsigned char>(token[i]))) {
      ++i;
    }
  }

  return i == token.size();
}

bool LooksLikeLongAsciiToken(absl::string_view text) {
  size_t token_start = 0;
  bool in_token = false;

  const auto check_token = [&](size_t start, size_t end) {
    if (end <= start) {
      return false;
    }

    const absl::string_view token = text.substr(start, end - start);

    if (IsIpv4LikeAsciiToken(token)) {
      return true;
    }

    if (IsVersionLikeAsciiToken(token)) {
      return false;
    }

    size_t longest_digit_run = 0;
    size_t current_digit_run = 0;

    bool has_alpha = false;
    bool has_digit = false;
    bool has_symbol = false;
    bool all_hex = true;

    for (const unsigned char c : token) {
      if (IsAsciiDigit(c)) {
        has_digit = true;
        ++current_digit_run;
        longest_digit_run = std::max(longest_digit_run, current_digit_run);
      } else {
        current_digit_run = 0;
      }

      if (IsAsciiAlpha(c)) {
        has_alpha = true;
      }

      if (c == '_' || c == '-' || c == '.') {
        has_symbol = true;
      }

      if (!IsAsciiHexDigit(c)) {
        all_hex = false;
      }
    }

    const size_t len = token.size();

    // Long digit runs are often phone numbers, account IDs, ticket IDs,
    // verification codes, order numbers, or other sensitive identifiers.
    //
    // This intentionally rejects some harmless numbers.  For live correction,
    // falling back to Mozc is safer than sending opaque numeric IDs to Zenz.
    if (longest_digit_run >= 8) {
      return true;
    }

    // Separator-bearing mixed identifiers:
    //   ghb_741298790561977834
    //   abc-1234567890
    //   user_12345678
    //
    // Keep version-like strings such as v0.7.0 and v1.0.0-alpha out of this
    // branch by checking IsVersionLikeAsciiToken() first.
    if (len >= 12 && has_alpha && has_digit && has_symbol) {
      return true;
    }

    // Mixed alphanumeric opaque identifiers without separators:
    //   AKIAIOSFODNN7EXAMPLE
    //   a1b2c3d4e5f6g7h8
    if (len >= 16 && has_alpha && has_digit) {
      return true;
    }

    // Long hex-like values:
    //   deadbeefcafebabe
    //   0123456789abcdef
    if (len >= 16 && all_hex && has_alpha) {
      return true;
    }

    // Very long ASCII words / identifiers are poor live-correction targets and
    // may be generated IDs, slugs, or copied tokens.
    if (len >= 24 && has_alpha) {
      return true;
    }

    if (len >= 32) {
      return true;
    }

    return false;
  };

  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);

    if (IsAsciiTokenChar(c)) {
      if (!in_token) {
        token_start = i;
        in_token = true;
      }
      continue;
    }

    if (in_token) {
      if (check_token(token_start, i)) {
        return true;
      }
      in_token = false;
    }
  }

  if (in_token && check_token(token_start, text.size())) {
    return true;
  }

  return false;
}

bool LooksLikeKnownSecretPrefix(absl::string_view text) {
  const std::string lower = ToLowerAscii(text);

  constexpr absl::string_view kPrefixes[] = {
      "ghp_",
      "github_pat_",
      "glpat-",
      "sk-",
      "xoxb-",
      "xoxp-",
      "ya29.",
      "akia",
      "bearer ",
  };

  for (const absl::string_view prefix : kPrefixes) {
    if (ContainsAsciiSubstring(lower, prefix)) {
      return true;
    }
  }

  return false;
}

bool ContainsSensitiveCredentialWord(absl::string_view text) {
  const std::string lower = ToLowerAscii(text);

  constexpr absl::string_view kAsciiWords[] = {
      "password",
      "passwd",
      "passphrase",
      "secret",
      "token",
      "apikey",
      "api_key",
      "credential",
      "privatekey",
      "private_key",
      "authorization",
  };

  for (const absl::string_view word : kAsciiWords) {
    if (ContainsAsciiSubstring(lower, word)) {
      return true;
    }
  }

  constexpr absl::string_view kJapaneseWords[] = {
      "パスワード",
      "暗証番号",
      "認証コード",
      "認証番号",
      "秘密鍵",
      "秘密キー",
      "トークン",
      "アクセストークン",
      "APIキー",
      "apiキー",
  };

  for (const absl::string_view word : kJapaneseWords) {
    if (text.find(word) != absl::string_view::npos) {
      return true;
    }
  }

  return false;
}

ZenzTextPrivacyDecision EvaluateZenzLiveKeyPrivacy(
    absl::string_view key) {
  if (key.empty()) {
    return {false, "empty_key"};
  }

  if (!Util::IsValidUtf8(key)) {
    return {false, "invalid_utf8"};
  }

  if (ContainsAsciiControl(key)) {
    return {false, "control_char"};
  }

  if (Util::CharsLen(key) > kMaxZenzLiveCorrectionKeyChars) {
    return {false, "key_too_long"};
  }

  // Main difference from the old ContainsAsciiAlphabet() gate:
  // ASCII is allowed only when the composition has a Japanese script signal.
  if (!ContainsJapaneseScriptSignal(key)) {
    return {false, "no_japanese_signal"};
  }

  if (LooksLikeEmail(key)) {
    return {false, "email_like"};
  }

  if (LooksLikeUrlOrDomain(key)) {
    return {false, "url_or_domain_like"};
  }

  if (LooksLikePath(key)) {
    return {false, "path_like"};
  }

  if (LooksLikeKnownSecretPrefix(key)) {
    return {false, "secret_prefix"};
  }

  if (LooksLikeLongAsciiToken(key)) {
    return {false, "token_like"};
  }

  if (ContainsSensitiveCredentialWord(key)) {
    return {false, "credential_word"};
  }

  return {true, "allow"};
}

ZenzTextPrivacyDecision EvaluateZenzLiveValuePrivacy(
    absl::string_view value) {
  if (value.empty()) {
    return {false, "empty_value"};
  }

  if (!Util::IsValidUtf8(value)) {
    return {false, "invalid_utf8"};
  }

  if (ContainsAsciiControl(value)) {
    return {false, "control_char"};
  }

  if (Util::CharsLen(value) > kMaxZenzLiveCorrectionValueChars) {
    return {false, "value_too_long"};
  }

  // Do not require a Japanese signal for value.
  // Example: key=ぎっとはぶ, value=GitHub should remain valid.
  if (LooksLikeEmail(value)) {
    return {false, "email_like"};
  }

  if (LooksLikeUrlOrDomain(value)) {
    return {false, "url_or_domain_like"};
  }

  if (LooksLikePath(value)) {
    return {false, "path_like"};
  }

  if (LooksLikeKnownSecretPrefix(value)) {
    return {false, "secret_prefix"};
  }

  if (LooksLikeLongAsciiToken(value)) {
    return {false, "token_like"};
  }

  if (ContainsSensitiveCredentialWord(value)) {
    return {false, "credential_word"};
  }

  return {true, "allow"};
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
void RestorePreeditSegmentKeysForSymbolStyle(
    absl::string_view symbol_style_source,
    commands::Preedit* preedit) {
  if (symbol_style_source.empty() || preedit == nullptr ||
      preedit->segment_size() == 0) {
    return;
  }

  // Restore only display keys.  The converter-owned internal key, candidate
  // value, and feedback key remain unchanged.  Multi-segment preedit is handled
  // by restoring the concatenated display key and then distributing it back by
  // the original segment key character lengths.  This is safe for wave-dash
  // restoration because ASCII '~', FULLWIDTH TILDE '～', and WAVE DASH '〜' are
  // all single Unicode scalar values.
  std::string full_key;
  std::string full_value;
  std::vector<size_t> segment_key_char_lengths;
  segment_key_char_lengths.reserve(preedit->segment_size());

  for (int i = 0; i < preedit->segment_size(); ++i) {
    const commands::Preedit::Segment& segment = preedit->segment(i);
    const absl::string_view segment_key =
        segment.has_key() && !segment.key().empty()
            ? absl::string_view(segment.key())
            : absl::string_view(segment.value());

    full_key.append(segment_key.data(), segment_key.size());
    full_value.append(segment.value());
    segment_key_char_lengths.push_back(Util::CharsLen(segment_key));
  }

  const std::string restored_key =
      ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
          symbol_style_source, full_value, full_key);

  if (restored_key == full_key ||
      Util::CharsLen(restored_key) != Util::CharsLen(full_key)) {
    return;
  }

  size_t offset = 0;
  for (int i = 0; i < preedit->segment_size(); ++i) {
    const size_t segment_chars = segment_key_char_lengths[i];
    preedit->mutable_segment(i)->set_key(
        std::string(Util::Utf8SubString(restored_key,
                                        offset,
                                        segment_chars)));
    offset += segment_chars;
  }
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

bool ShouldCommitLiveConversionBeforeShiftAsciiInput(
    const config::Config& config,
    const composer::Composer& composer,
    const commands::KeyEvent& key_event) {
  if (config.shift_key_mode_switch() !=
      config::Config::ASCII_INPUT_MODE) {
    return false;
  }

  if (key_event.input_style() != commands::KeyEvent::FOLLOW_MODE) {
    return false;
  }

  if (key_event.has_special_key()) {
    return false;
  }

  if (composer.GetInputFieldType() == commands::Context::PASSWORD) {
    return false;
  }

  const transliteration::TransliterationType input_mode =
      composer.GetInputMode();
  if (input_mode == transliteration::HALF_ASCII ||
      input_mode == transliteration::FULL_ASCII) {
    return false;
  }

  const size_t length = composer.GetLength();
  if (length == 0 || length != composer.GetCursor()) {
    return false;
  }

  std::string input;
  if (!key_event.key_string().empty()) {
    if (key_event.key_string().size() != 1) {
      return false;
    }
    input = key_event.key_string();
  } else if (key_event.has_key_code()) {
    if (key_event.key_code() > 0x7f) {
      return false;
    }
    input.push_back(static_cast<char>(key_event.key_code()));
  } else {
    return false;
  }

  const uint32_t modifiers = KeyEventUtil::GetModifiers(key_event);
  if (KeyEventUtil::HasCtrl(modifiers) ||
      KeyEventUtil::HasAlt(modifiers)) {
    return false;
  }

  const bool caps_locked = KeyEventUtil::HasCaps(modifiers);
  const char key = input[0];

  return (!caps_locked && ('A' <= key && key <= 'Z')) ||
         (caps_locked && ('a' <= key && key <= 'z'));
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
      if (prev_state == ImeContext::COMPOSITION ||
          prev_state == ImeContext::CONVERSION) {
        context->mutable_converter()->OnEndComposition();
      }
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

void MergeCommandResult(const commands::Result& step_result,
                        commands::Result* accumulated_result) {
  if (!accumulated_result->has_key() && !accumulated_result->has_value()) {
    *accumulated_result = step_result;
    return;
  }

  if (step_result.has_key()) {
    accumulated_result->set_key(
        absl::StrCat(accumulated_result->key(), step_result.key()));
  }
  if (step_result.has_value()) {
    accumulated_result->set_value(
        absl::StrCat(accumulated_result->value(), step_result.value()));
  }
}

void AccumulateCommandOutput(const commands::Output& step_output,
                             bool* consumed,
                             bool* has_accumulated_result,
                             commands::Result* accumulated_result,
                             commands::Output* final_output) {
  *consumed = *consumed || step_output.consumed();
  *final_output = step_output;

  if (step_output.has_result()) {
    MergeCommandResult(step_output.result(), accumulated_result);
    *has_accumulated_result = true;
  }

  final_output->set_consumed(*consumed);
  if (*has_accumulated_result) {
    *final_output->mutable_result() = *accumulated_result;
  }
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

bool Session::IsGrimodexSecureInput() const {
  return grimodex_domain_.initialized && grimodex_domain_.secure_input;
}

void Session::ApplyCurrentGrimodexDomainToContext(
    commands::Context* context, bool clear_surrounding_text) const {
  DCHECK(context);
  if (!grimodex_domain_.initialized) {
    return;
  }

  commands::GrimodexClientContext* grimodex = context->mutable_grimodex();
  grimodex->set_program(grimodex_domain_.program);
  grimodex->set_frontend(grimodex_domain_.frontend);
  grimodex->set_secure_input(grimodex_domain_.secure_input);
  grimodex->set_focus_epoch(grimodex_domain_.focus_epoch);
  context->set_revision(
      GrimodexRevisionFromFocusEpoch(grimodex_domain_.focus_epoch));

  if (clear_surrounding_text || grimodex_domain_.secure_input) {
    context->clear_preceding_text();
    context->clear_following_text();
  }
  if (grimodex_domain_.secure_input) {
    context->set_input_field_type(commands::Context::PASSWORD);
  } else if (context->input_field_type() == commands::Context::PASSWORD) {
    // A stale secure callback cannot keep the replacement non-secure domain in
    // password mode after the current epoch has explicitly left that domain.
    context->set_input_field_type(commands::Context::NORMAL);
  }
}

void Session::PurgeForGrimodexDomainTransition(bool secure_input) {
  // None of these pending states may be confirmed as a consequence of a focus
  // or security transition.  In particular, direct-commit learning is reverted
  // and Zenz feedback is discarded instead of being written to either store.
  DiscardPendingDirectCommitLearning("grimodex_domain_transition");
  DiscardPendingZenzFeedback("grimodex_domain_transition");
  ClearPendingRerankedPreeditCommitAfterConvertCancel();
  ClearLiveConversionState();
  ClearUndoContext();

  engine::EngineConverterInterface* converter =
      context_->mutable_converter();

  // Toggling through secure=true is the EngineConverter's fail-closed purge
  // API.  It drops both latest and pinned snapshots.  The second call selects
  // the new domain mode, but secure=false intentionally cannot resurrect the
  // released generation.
  converter->SetProjectDictionarySecureInput(true);
  converter->Reset();
  converter->SetProjectDictionarySecureInput(secure_input);

  context_->mutable_composer()->Reset();
  context_->mutable_composer()->SetInputFieldType(
      secure_input ? commands::Context::PASSWORD
                   : commands::Context::NORMAL);
  if (context_->state() != ImeContext::DIRECT) {
    context_->set_state(ImeContext::PRECOMPOSITION);
  }
  context_->mutable_client_context()->Clear();
  context_->mutable_output()->Clear();
}

void Session::UpdateGrimodexContext(commands::Command* command) {
  DCHECK(command);
  if (!command->has_input()) {
    return;
  }

  commands::Input* input = command->mutable_input();
  const bool has_context = input->has_context();
  commands::Context* wire_context =
      has_context ? input->mutable_context() : nullptr;
  const bool has_grimodex =
      wire_context != nullptr && wire_context->has_grimodex();

  if (!has_grimodex) {
    // Ordinary Mozc clients remain untouched.  Once a session is managed,
    // however, a contextless command is treated as a delayed callback from the
    // current domain, never as permission to leave secure input.
    if (!grimodex_domain_.initialized) {
      return;
    }
    if (wire_context != nullptr &&
        wire_context->input_field_type() == commands::Context::PASSWORD &&
        !grimodex_domain_.secure_input) {
      PurgeForGrimodexDomainTransition(/*secure_input=*/true);
      grimodex_domain_.secure_input = true;
    }
    wire_context = input->mutable_context();
    ApplyCurrentGrimodexDomainToContext(
        wire_context, /*clear_surrounding_text=*/true);
    if (grimodex_domain_.secure_input) {
      *context_->mutable_client_context() = *wire_context;
    }
    return;
  }

  const commands::GrimodexClientContext& wire = wire_context->grimodex();
  const bool requested_secure =
      wire.secure_input() ||
      wire_context->input_field_type() == commands::Context::PASSWORD;
  const bool valid_epoch = wire.has_focus_epoch() && wire.focus_epoch() != 0;
  const bool bounded_metadata =
      wire.program().size() <= kMaxGrimodexProgramBytes &&
      wire.frontend().size() <= kMaxGrimodexFrontendBytes;
  const bool valid_metadata_utf8 =
      strings::IsValidUtf8(wire.program()) &&
      strings::IsValidUtf8(wire.frontend());

  if (!valid_epoch || !bounded_metadata || !valid_metadata_utf8) {
    // Malformed metadata cannot select a new domain.  An explicit secure bit
    // still fails closed, while a false/missing epoch cannot unlock an already
    // secure session.
    if (requested_secure &&
        (!grimodex_domain_.initialized ||
         !grimodex_domain_.secure_input)) {
      if (!grimodex_domain_.initialized) {
        grimodex_domain_.initialized = true;
        // A malformed identifier is not retained even when its secure bit is
        // honored fail-closed.  Empty identifiers are valid opaque domain
        // metadata and cannot contain a truncated UTF-8 scalar.
        grimodex_domain_.program =
            bounded_metadata && valid_metadata_utf8 ? wire.program() : "";
        grimodex_domain_.frontend =
            bounded_metadata && valid_metadata_utf8 ? wire.frontend() : "";
        grimodex_domain_.focus_epoch = 0;
      }
      PurgeForGrimodexDomainTransition(/*secure_input=*/true);
      grimodex_domain_.secure_input = true;
    }
    if (grimodex_domain_.initialized) {
      ApplyCurrentGrimodexDomainToContext(
          wire_context, /*clear_surrounding_text=*/true);
      if (grimodex_domain_.secure_input) {
        *context_->mutable_client_context() = *wire_context;
      }
    } else {
      // Do not retain invalid typed metadata as an ordinary Mozc client
      // context.  It is neither a usable project domain nor safe surrounding
      // text for Zenz.
      wire_context->clear_grimodex();
      wire_context->clear_preceding_text();
      wire_context->clear_following_text();
    }
    return;
  }

  if (grimodex_domain_.initialized &&
      wire.focus_epoch() < grimodex_domain_.focus_epoch) {
    // A delayed callback is allowed to keep operating only inside the current
    // domain.  It cannot replace metadata, surrounding text, or secure state.
    // Conversely an explicit secure=true remains fail-closed even if stale.
    if (requested_secure && !grimodex_domain_.secure_input) {
      PurgeForGrimodexDomainTransition(/*secure_input=*/true);
      grimodex_domain_.secure_input = true;
    }
    ApplyCurrentGrimodexDomainToContext(
        wire_context, /*clear_surrounding_text=*/true);
    if (grimodex_domain_.secure_input) {
      *context_->mutable_client_context() = *wire_context;
    }
    return;
  }

  if (grimodex_domain_.initialized &&
      wire.focus_epoch() == grimodex_domain_.focus_epoch &&
      (wire.program() != grimodex_domain_.program ||
       wire.frontend() != grimodex_domain_.frontend ||
       requested_secure != grimodex_domain_.secure_input)) {
    // An epoch names exactly one security-domain tuple.  Only a strictly newer
    // epoch may replace it.  A same-epoch mismatch is a delayed/forged
    // callback and is canonicalized to the current tuple.  secure=true remains
    // fail-closed even in that case, but never adopts the mismatching metadata.
    if (requested_secure && !grimodex_domain_.secure_input) {
      PurgeForGrimodexDomainTransition(/*secure_input=*/true);
      grimodex_domain_.secure_input = true;
    }
    ApplyCurrentGrimodexDomainToContext(
        wire_context, /*clear_surrounding_text=*/true);
    if (grimodex_domain_.secure_input) {
      *context_->mutable_client_context() = *wire_context;
    }
    return;
  }

  const bool first_domain = !grimodex_domain_.initialized;
  const bool domain_transition =
      !first_domain &&
      wire.focus_epoch() > grimodex_domain_.focus_epoch;

  if (first_domain || domain_transition) {
    grimodex_domain_.initialized = true;
    grimodex_domain_.program = wire.program();
    grimodex_domain_.frontend = wire.frontend();
    grimodex_domain_.focus_epoch = wire.focus_epoch();
    grimodex_domain_.secure_input = requested_secure;

    if (domain_transition || requested_secure) {
      PurgeForGrimodexDomainTransition(requested_secure);
    } else {
      context_->mutable_converter()->SetProjectDictionarySecureInput(false);
    }
  }

  ApplyCurrentGrimodexDomainToContext(
      wire_context, /*clear_surrounding_text=*/false);
  if (domain_transition || requested_secure ||
      !context_->client_context().has_grimodex()) {
    *context_->mutable_client_context() = *wire_context;
  }
}

void Session::AttachGrimodexSessionStatus(commands::Command* command) const {
  DCHECK(command);
  if (!grimodex_domain_.initialized) {
    return;
  }

  commands::GrimodexSessionStatus* output_status =
      command->mutable_output()->mutable_grimodex_session_status();
  output_status->Clear();

  const dictionary::ProjectDictionaryRegistry::Status registry_status =
      context_->converter().GetProjectDictionaryStatus();
  const std::shared_ptr<const dictionary::ProjectDictionarySnapshot> pinned =
      context_->converter().GetPinnedProjectDictionary();

  uint64_t sequence = 0;
  if (registry_status.pinned_generation.has_value()) {
    sequence = *registry_status.pinned_generation;
  } else if (registry_status.latest_generation.has_value()) {
    sequence = *registry_status.latest_generation;
  }
  output_status->set_registry_sequence(sequence);

  if (registry_status.secure_input || IsGrimodexSecureInput()) {
    output_status->set_scope(
        commands::GrimodexSessionStatus::SECURE_REVOKED);
    return;
  }

  if (pinned == nullptr) {
    output_status->set_scope(commands::GrimodexSessionStatus::OFF);
    return;
  }

  output_status->set_scope(commands::GrimodexSessionStatus::PROJECT);
  const std::string digest =
      DecodeLowerHexSha256(pinned->metadata().payload_sha256);
  if (!digest.empty()) {
    output_status->set_pinned_payload_sha256(digest);
  }
}

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
  UpdateGrimodexContext(command);
  if (!command->input().has_command()) {
    AttachGrimodexSessionStatus(command);
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
      AttachGrimodexSessionStatus(command);
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
    AttachGrimodexSessionStatus(command);
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

    case commands::SessionCommand::RECONVERT_SELECTION_OR_INSERT_SPACE:
      // This command is a client-side callback command.  It should normally be
      // handled by the TSF client from Output::Callback.  If it reaches the
      // server through SendCommand unexpectedly, consume it without changing the
      // current composition.
      result = DoNothing(command);
      break;

    case commands::SessionCommand::REQUEST_NWP: {
      if (IsGrimodexSecureInput()) {
        result = DoNothing(command);
        break;
      }
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
  AttachGrimodexSessionStatus(command);
  return result;
}

bool Session::TestSendKey(commands::Command* command) {
  UpdateTime();
  UpdatePreferences(command);
  UpdateGrimodexContext(command);
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

      if (pending_direct_commit_learning_.pending &&
          IsCancelKeyForCompositionOrConversion(key)) {
        // A direct-commit punctuation/symbol may have already sent text to the
        // application without creating Mozc's undo context.  In that case a
        // cancel-like key such as Ctrl+Z should still cancel Mozkey's pending
        // learning, while the key itself must be echoed back so that the
        // application can decide how to undo the visible text.
        DiscardPendingDirectCommitLearning(
            "precomposition_cancel_key_after_direct_commit_learning");
        return EchoBackAndClearUndoContext(command);
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
      case keymap::PrecompositionState::RECONVERT_SELECTION_OR_INSERT_SPACE:
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
  UpdateGrimodexContext(command);
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
  AttachGrimodexSessionStatus(command);
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

bool Session::ExecuteCommandSequence(
    const keymap::CommandSequence& command_sequence,
    commands::Command* command) {
  return ExecuteCommandSequenceWithInitialOutput(command_sequence, nullptr,
                                                 command);
}

bool Session::ExecuteCommandSequenceWithInitialOutput(
    const keymap::CommandSequence& command_sequence,
    const commands::Output* initial_output,
    commands::Command* command) {
  bool executed = false;
  bool consumed = false;
  bool has_accumulated_result = false;
  commands::Result accumulated_result;
  commands::Output final_output;

  if (initial_output != nullptr) {
    AccumulateCommandOutput(*initial_output,
                            &consumed,
                            &has_accumulated_result,
                            &accumulated_result,
                            &final_output);
    executed = true;
  }

  for (const std::string& command_name : command_sequence) {
    if (command_name.empty()) {
      continue;
    }

    command->mutable_output()->Clear();

    if (!ExecuteCommandName(command_name, command)) {
      if (!executed) {
        return DoNothing(command);
      }

      final_output.set_consumed(consumed);
      if (has_accumulated_result) {
        *final_output.mutable_result() = accumulated_result;
      }
      *command->mutable_output() = final_output;
      return true;
    }

    executed = true;

    const commands::Output step_output = command->output();
    AccumulateCommandOutput(step_output,
                            &consumed,
                            &has_accumulated_result,
                            &accumulated_result,
                            &final_output);
  }

  if (!executed) {
    return DoNothing(command);
  }

  final_output.set_consumed(consumed);
  if (has_accumulated_result) {
    *final_output.mutable_result() = accumulated_result;
  }
  *command->mutable_output() = final_output;
  return true;
}

bool Session::ExecuteCommandName(const std::string& command_name,
                                 commands::Command* command) {
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();

  switch (context_->state()) {
    case ImeContext::DIRECT: {
      keymap::DirectInputState::Commands key_command;
      if (!keymap->ResolveDirectCommandName(command_name, &key_command)) {
        return false;
      }
      return ExecuteDirectInputCommand(key_command, command);
    }

    case ImeContext::PRECOMPOSITION: {
      keymap::PrecompositionState::Commands key_command;
      if (!keymap->ResolvePrecompositionCommandName(command_name,
                                                    &key_command)) {
        return false;
      }
      return ExecutePrecompositionCommand(key_command, command);
    }

    case ImeContext::COMPOSITION: {
      keymap::CompositionState::Commands key_command;
      if (!keymap->ResolveCompositionCommandName(command_name, &key_command)) {
        return false;
      }
      return ExecuteCompositionCommand(key_command, command);
    }

    case ImeContext::CONVERSION: {
      keymap::ConversionState::Commands key_command;
      if (!keymap->ResolveConversionCommandName(command_name, &key_command)) {
        return false;
      }
      return ExecuteConversionCommand(key_command, command);
    }

    case ImeContext::NONE:
      return false;
  }

  return false;
}

bool Session::ExecuteDirectInputCommand(
    keymap::DirectInputState::Commands key_command,
    commands::Command* command) {
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

bool Session::ExecutePrecompositionCommand(
    keymap::PrecompositionState::Commands key_command,
    commands::Command* command) {
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

    case keymap::PrecompositionState::CANCEL:
      return EditCancel(command);
    case keymap::PrecompositionState::CANCEL_AND_IME_OFF:
      return EditCancelAndIMEOff(command);
    case keymap::PrecompositionState::COMMIT_FIRST_SUGGESTION:
      return CommitFirstSuggestion(command);
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
    case keymap::PrecompositionState::RECONVERT_SELECTION_OR_INSERT_SPACE:
      return RequestReconvertSelectionOrInsertSpace(command);

    case keymap::PrecompositionState::IME_ACTION:
      return ImeAction(command);
  }

  return false;
}

bool Session::ExecuteCompositionCommand(
    keymap::CompositionState::Commands key_command,
    commands::Command* command) {
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

bool Session::ExecuteConversionCommand(
    keymap::ConversionState::Commands key_command,
    commands::Command* command) {
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

    case keymap::ConversionState::CYCLE_SEGMENTATION:
      return CycleSegmentation(command);

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

bool Session::SendKeyDirectInputState(commands::Command* command) {
  keymap::CommandSequence command_sequence;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  if (!keymap->GetCommandSequenceDirect(command->input().key(),
                                        &command_sequence)) {
    return EchoBackAndClearUndoContext(command);
  }

  return ExecuteCommandSequence(command_sequence, command);
}

bool Session::SendKeyPrecompositionState(commands::Command* command) {
  keymap::CommandSequence command_sequence;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  const bool result =
      context_->converter().CheckState(EngineConverterInterface::SUGGESTION)
          ? keymap->GetCommandSequenceZeroQuerySuggestion(
                command->input().key(), &command_sequence)
          : keymap->GetCommandSequencePrecomposition(command->input().key(),
                                                     &command_sequence);

  if (!result) {
    if (HasUndoContext() &&
        IsCancelKeyForCompositionOrConversion(command->input().key())) {
      return Revert(command);
    }

    if (pending_direct_commit_learning_.pending &&
        IsCancelKeyForCompositionOrConversion(command->input().key())) {
      // A direct-commit punctuation/symbol may have already sent text to the
      // application without creating Mozc's undo context.  In that case a
      // cancel-like key such as Ctrl+Z should still cancel Mozkey's pending
      // learning, while the key itself must be echoed back so that the
      // application can decide how to undo the visible text.
      DiscardPendingDirectCommitLearning(
          "precomposition_cancel_key_after_direct_commit_learning");
      return EchoBackAndClearUndoContext(command);
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

  return ExecuteCommandSequence(command_sequence, command);
}

bool Session::SendKeyCompositionState(commands::Command* command) {
  keymap::CommandSequence command_sequence;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  const bool result =
      context_->converter().CheckState(EngineConverterInterface::SUGGESTION)
          ? keymap->GetCommandSequenceSuggestion(command->input().key(),
                                                 &command_sequence)
          : keymap->GetCommandSequenceComposition(command->input().key(),
                                                  &command_sequence);

  if (!result) {
    return DoNothing(command);
  }

  return ExecuteCommandSequence(command_sequence, command);
}

bool Session::SendKeyConversionState(commands::Command* command) {
  keymap::CommandSequence command_sequence;
  const keymap::KeyMapManager* keymap = &context_->GetKeyMapManager();
  const bool result =
      context_->converter().CheckState(EngineConverterInterface::PREDICTION)
          ? keymap->GetCommandSequencePrediction(command->input().key(),
                                                 &command_sequence)
          : keymap->GetCommandSequenceConversion(command->input().key(),
                                                 &command_sequence);

  if (!result || command_sequence.empty()) {
    return DoNothing(command);
  }

  keymap::ConversionState::Commands key_command;
  if (!keymap->ResolveConversionCommandName(command_sequence.front(),
                                            &key_command)) {
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

      if (!CommitZenzLiveCorrectionResult(command)) {
        return false;
      }

      if (command_sequence.size() == 1) {
        return true;
      }

      const commands::Output zenz_commit_output = command->output();

      keymap::CommandSequence remaining_sequence(
          command_sequence.begin() + 1, command_sequence.end());
      return ExecuteCommandSequenceWithInitialOutput(
          remaining_sequence, &zenz_commit_output, command);
    }

    // While a zenz correction is visible, plain Space is an explicit
    // candidate-change operation.  Reject the speculative zenz layer and
    // restore the underlying Mozc conversion, but promote it to ordinary
    // conversion state so the next text input commits it instead of extending
    // the same live-conversion composition.
    if (key_command == keymap::ConversionState::CONVERT_NEXT &&
        IsPureSpaceKey(input_key) &&
        HasVisibleZenzLiveCorrection()) {
      return RevertZenzLiveCorrectionToNormalConversion(command);
    }

    // A prediction key such as Tab should focus prediction candidates even while
    // live conversion is active.  Do this before the generic live-conversion
    // promotion below, otherwise PredictAndConvert() would see an ordinary
    // CONVERSION state and fall back to ConvertNext().  Other conversion
    // commands still follow their existing keymap-defined conversion path.
    if (key_command == keymap::ConversionState::PREDICT_AND_CONVERT) {
      if (!PredictAndConvertFromLiveConversion(command)) {
        return false;
      }

      if (command_sequence.size() == 1) {
        return true;
      }

      const commands::Output prediction_output = command->output();
      keymap::CommandSequence remaining_sequence(
          command_sequence.begin() + 1, command_sequence.end());
      return ExecuteCommandSequenceWithInitialOutput(
          remaining_sequence, &prediction_output, command);
    }

    // Explicit conversion operations such as Space, candidate movement, or Cancel
    // promote live conversion back to normal conversion behavior.  When a visible
    // zenz layer exists, plain Space is consumed above to peel off that layer first.
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

  return ExecuteCommandSequence(command_sequence, command);
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
  ConfirmPendingDirectCommitLearning("ime_off_after_direct_commit_learning");

  command->mutable_output()->set_consumed(true);
  ClearUndoContext();

  Commit(command);
  ConfirmPendingZenzFeedback();

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
  ConfirmPendingDirectCommitLearning(
      "make_sure_ime_off_after_direct_commit_learning");

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
  ConfirmPendingZenzFeedback();
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

  ClearPendingRerankedPreeditCommitAfterConvertCancel();

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

  ClearPendingRerankedPreeditCommitAfterConvertCancel();

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
  // Reverse conversion asks the client for selected application text.  A
  // managed secure domain must never request that text, even if a delayed or
  // forged key command reaches this handler after the domain transition.
  if (IsGrimodexSecureInput()) {
    return DoNothing(command);
  }

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

bool Session::RequestReconvertSelectionOrInsertSpace(
    commands::Command* command) {
  if (context_->state() != ImeContext::PRECOMPOSITION) {
    return DoNothing(command);
  }

  // Build the normal InsertSpace output first.  The TSF client will apply this
  // output only when the application has no selected text.  When selected text
  // exists, the callback is handled before the fallback output is applied.
  if (!InsertSpace(command)) {
    return false;
  }

  commands::SessionCommand* session_command =
      command->mutable_output()->mutable_callback()->mutable_session_command();
  session_command->set_type(
      commands::SessionCommand::RECONVERT_SELECTION_OR_INSERT_SPACE);
  return true;
}

bool Session::ConvertReverse(commands::Command* command) {
  if (IsGrimodexSecureInput()) {
    return DoNothing(command);
  }

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

  ClearPendingRerankedPreeditCommitAfterConvertCancel();

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
  pending_live_conversion_input_.Clear();
  pending_live_conversion_suggestion_candidate_window_.Clear();
  CancelPendingZenzLiveCorrection();
}

void Session::ClearLiveConversionState() {
  ++live_conversion_generation_;

  live_conversion_active_ = false;
  live_conversion_pending_ = false;
  pending_live_conversion_generation_ = 0;
  pending_live_conversion_key_.clear();
  pending_live_conversion_input_.Clear();
  pending_live_conversion_suggestion_candidate_window_.Clear();
  live_conversion_suggestion_candidate_window_.Clear();

  live_conversion_key_.clear();
  live_conversion_preedit_.clear();
  live_conversion_value_.clear();
  live_conversion_preedit_output_.Clear();
  live_conversion_protected_spans_.clear();
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

namespace {
bool ShouldSuppressShiftedAsciiAutoSuggestion(
    const config::Config& config,
    const composer::Composer& composer);
}  // namespace

bool Session::MaybeStartLiveConversion(commands::Command* command) {
  if (!context_->GetConfig().use_live_conversion()) {
    return false;
  }

  if (context_->state() != ImeContext::COMPOSITION) {
    return false;
  }

  if (IsGrimodexSecureInput() ||
      context_->composer().GetInputFieldType() == commands::Context::PASSWORD) {
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

  if (ShouldSkipLiveConversionForCompositionKey(
          live_conversion_key,
          GetLiveConversionCommittedLeftBoundary(*context_),
          GetLiveConversionMinKeyLength(context_->GetConfig())) ||
      length != context_->composer().GetCursor()) {
    return false;
  }

  // Capture the raw composition before Convert(). These strings are the stable
  // source for the next pending-suffix display.
  const std::string live_conversion_preedit =
      context_->composer().GetStringForPreedit();

  // Delayed live-conversion callbacks are SEND_COMMAND inputs and may not carry
  // the same request_suggestion/context data as the original SEND_KEY input.
  // Keep the original input for passive suggestion generation so the suggestion
  // window does not disappear when the delayed live conversion materializes.
  const bool use_pending_live_conversion_input =
      live_conversion_pending_ &&
      pending_live_conversion_key_ == live_conversion_key &&
      pending_live_conversion_input_.type() != commands::Input::NONE;
  const commands::Input live_conversion_suggestion_input =
      use_pending_live_conversion_input ? pending_live_conversion_input_
                                        : command->input();
  const commands::CandidateWindow
      pending_live_conversion_suggestion_candidate_window =
          pending_live_conversion_suggestion_candidate_window_;

  live_conversion_pending_ = false;
  pending_live_conversion_generation_ = 0;
  pending_live_conversion_key_.clear();
  pending_live_conversion_input_.Clear();
  pending_live_conversion_suggestion_candidate_window_.Clear();

  if (!context_->mutable_converter()->Convert(context_->composer())) {
    if (ShouldKeepPendingLiveConversionForTransientSokuon(live_conversion_key) &&
        OutputPendingLiveConversion(command)) {
      ClearZenzLiveCorrectionState();
      return true;
    }

    OutputComposition(command);
    return true;
  }

  SetSessionState(ImeContext::CONVERSION, context_.get());
  live_conversion_active_ = true;

  // Keep the candidate list visible internally so that the Windows renderer is
  // updated.
  context_->mutable_converter()->SetCandidateListVisible(true);

  Output(command);

  if (command->output().has_preedit()) {
    RestorePreeditSegmentKeysForSymbolStyle(
        live_conversion_preedit,
        command->mutable_output()->mutable_preedit());
  }

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

  live_conversion_protected_spans_.clear();
  if (context_->GetConfig().use_zenz_live_correction()) {
    live_conversion_protected_spans_ = BuildZenzProtectedConversionSpans(
        context_->converter(), command->output(), live_conversion_key_,
        live_conversion_value_);
  }

  ClearZenzLiveCorrectionState();

  if (MaybeApplyZenzFeedbackLiveCorrection(command)) {
    return true;
  }

  const bool should_suppress_shifted_ascii_suggestion =
      ShouldSuppressShiftedAsciiAutoSuggestion(context_->GetConfig(),
                                               context_->composer());

  if (should_suppress_shifted_ascii_suggestion) {
    live_conversion_suggestion_candidate_window_.Clear();
    pending_live_conversion_suggestion_candidate_window_.Clear();
    command->mutable_output()->clear_candidate_window();
  } else if (
      !AttachLiveConversionSuggestionCandidateWindow(
          live_conversion_suggestion_input, command->mutable_output()) &&
      pending_live_conversion_suggestion_candidate_window.candidate_size() > 0) {
    live_conversion_suggestion_candidate_window_ =
        pending_live_conversion_suggestion_candidate_window;
    *command->mutable_output()->mutable_candidate_window() =
        live_conversion_suggestion_candidate_window_;
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

    if (command->output().has_preedit()) {
      RestorePreeditSegmentKeysForSymbolStyle(
          raw_preedit,
          command->mutable_output()->mutable_preedit());
    }

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

  RestorePreeditSegmentKeysForSymbolStyle(raw_preedit, preedit);

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

  if (IsGrimodexSecureInput() ||
      context_->composer().GetInputFieldType() == commands::Context::PASSWORD) {
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

  const bool should_skip_live_conversion =
      ShouldSkipLiveConversionForCompositionKey(
          key,
          GetLiveConversionCommittedLeftBoundary(*context_),
          GetLiveConversionMinKeyLength(context_->GetConfig()));
  const bool cursor_at_end = length == context_->composer().GetCursor();

  if (should_skip_live_conversion || !cursor_at_end) {
    if (should_skip_live_conversion && cursor_at_end &&
        ShouldKeepPendingLiveConversionForTransientSokuon(key)) {
      // Inputs such as 「おもっ」 and 「くさっ」 can be transient sokuon
      // prefixes before 「て」/「た」.  They may be skipped before Convert()
      // is attempted, so keep the live conversion overlay as pending instead
      // of falling back to plain composition output.
      ++live_conversion_generation_;
      live_conversion_pending_ = true;
      pending_live_conversion_generation_ = live_conversion_generation_;
      pending_live_conversion_key_ = key;
      pending_live_conversion_input_ = command->input();
      pending_live_conversion_suggestion_candidate_window_.Clear();

      if (OutputPendingLiveConversion(command)) {
        AttachDelayedLiveConversionCallback(command);
        return true;
      }

      CancelPendingLiveConversion();
    }

    CancelPendingLiveConversion();
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
  pending_live_conversion_input_ = command->input();
  pending_live_conversion_suggestion_candidate_window_.Clear();

  if (!OutputPendingLiveConversion(command)) {
    // Avoid showing raw hiragana fallback. If pending display cannot be built
    // from the stable converted prefix, materialize live conversion immediately.
    return MaybeStartLiveConversion(command);
  }

  if (AttachLiveConversionSuggestionCandidateWindow(command->input(),
                                                    command->mutable_output())) {
    pending_live_conversion_suggestion_candidate_window_ =
        command->output().candidate_window();
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
    AttachCachedLiveConversionSuggestionCandidateWindow(
        command->mutable_output());
    return true;
  }

  if (live_conversion_active_ && context_->state() == ImeContext::CONVERSION) {
    Output(command);

    if (command->output().has_preedit()) {
      RestorePreeditSegmentKeysForSymbolStyle(
          live_conversion_preedit_.empty()
              ? live_conversion_key_
              : live_conversion_preedit_,
          command->mutable_output()->mutable_preedit());
    }

    command->mutable_output()->set_live_conversion(true);
    command->mutable_output()->set_live_conversion_pending(false);
    AttachCachedLiveConversionSuggestionCandidateWindow(
        command->mutable_output());
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
  const bool should_skip_live_conversion =
      ShouldSkipLiveConversionForCompositionKey(
          current_key,
          GetLiveConversionCommittedLeftBoundary(*context_),
          GetLiveConversionMinKeyLength(context_->GetConfig()));
  const bool cursor_at_end = length == context_->composer().GetCursor();

  if (should_skip_live_conversion || !cursor_at_end) {
    if (should_skip_live_conversion && cursor_at_end &&
        ShouldKeepPendingLiveConversionForTransientSokuon(current_key)) {
      OutputPendingLiveConversion(command);
      AttachCachedLiveConversionSuggestionCandidateWindow(
          command->mutable_output());
      return true;
    }

    CancelPendingLiveConversion();
    OutputFromState(command);
    return true;
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
      ShouldSkipLiveConversionForCompositionKey(
          current_key,
          GetLiveConversionCommittedLeftBoundary(*context_),
          GetLiveConversionMinKeyLength(context_->GetConfig()))) {
    CancelPendingLiveConversion();
    return false;
  }

  commands::Command dummy_command;
  return MaybeStartLiveConversion(&dummy_command);
}

std::string Session::ExtractZenzLeftContext(uint32_t max_chars) const {
  if (IsGrimodexSecureInput()) {
    return "";
  }

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

std::string Session::ExtractZenzRightContext(uint32_t max_chars) const {
  if (IsGrimodexSecureInput()) {
    return "";
  }

  if (max_chars == 0) {
    return "";
  }

  if (!context_->client_context().has_following_text()) {
    return "";
  }

  const std::string& following_text =
      context_->client_context().following_text();

  // Right context should describe the continuation of the current line.
  // Do not let text from following lines leak into the Zenz prompt, because
  // multi-line editors often expose the rest of the document as following_text.
  const size_t line_break_pos = following_text.find_first_of("\r\n");
  const std::string current_line =
      line_break_pos == std::string::npos
          ? following_text
          : following_text.substr(0, line_break_pos);

  const size_t len = Util::CharsLen(current_line);
  if (len <= max_chars) {
    return current_line;
  }

  return std::string(Util::Utf8SubString(current_line, 0, max_chars));
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
  if (IsGrimodexSecureInput()) {
    return;
  }

  if (!UseZenzFeedbackLearning(context_->GetConfig())) {
    return;
  }

  if (key.empty() || value.empty()) {
    LOG(ERROR) << "[zenz-feedback] skip accepted empty key/value";
    return;
  }

  const ZenzTextPrivacyDecision key_privacy =
      EvaluateZenzLiveKeyPrivacy(key);
  if (!key_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] skip accepted key_privacy reason=",
        key_privacy.reason,
        " ",
        ZenzRedactedTextStats("key", key)));
    return;
  }

  const ZenzTextPrivacyDecision value_privacy =
      EvaluateZenzLiveValuePrivacy(value);
  if (!value_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] skip accepted value_privacy reason=",
        value_privacy.reason,
        " ",
        ZenzRedactedTextStats("value", value)));
    return;
  }

  const std::string context_class =
      BuildZenzFeedbackContextClass(left_context);

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] accepted ",
      ZenzRedactedTextStats("key", key),
      " ", ZenzRedactedTextStats("value", value),
      " context_class=", context_class));

  // ZenzFeedbackStore owns only the full request/response pair.  More general
  // learning from an accepted correction is handled separately through Mozc
  // history below.
  zenz_feedback_store_.RecordAccepted(key, context_class, value);

  ZenzDebugOutput("[zenz-feedback] RecordAccepted returned");
}

bool Session::MaybeLearnZenzCandidateToMozcHistory(
    absl::string_view key,
    absl::string_view value) {
  if (IsGrimodexSecureInput()) {
    return false;
  }

  if (!UseZenzFeedbackLearning(context_->GetConfig())) {
    return false;
  }

  // Mozc history learning is intentionally separate from ZenzFeedbackStore.
  // The feedback TSV remains full-sequence scoped, while Mozc history can learn
  // the accepted external conversion result using converter/history semantics.
  if (key.empty() || value.empty()) {
    return false;
  }

  if (context_->composer().GetInputFieldType() ==
      commands::Context::PASSWORD) {
    return false;
  }

  const ZenzTextPrivacyDecision key_privacy =
      EvaluateZenzLiveKeyPrivacy(key);
  if (!key_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] skip mozc history key_privacy reason=",
        key_privacy.reason,
        " ",
        ZenzRedactedTextStats("key", key)));
    return false;
  }

  const ZenzTextPrivacyDecision value_privacy =
      EvaluateZenzLiveValuePrivacy(value);
  if (!value_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] skip mozc history value_privacy reason=",
        value_privacy.reason,
        " ",
        ZenzRedactedTextStats("value", value)));
    return false;
  }

  return context_->mutable_converter()->LearnExternalConversionResult(
      key, value, context_->client_context());
}

int Session::MaybeLearnZenzReverseSegmentsToMozcHistory(
    const std::vector<std::pair<std::string, std::string>>& segments) {
  int learned_count = 0;
  for (const auto& [key, value] : segments) {
    if (MaybeLearnZenzCandidateToMozcHistory(key, value)) {
      ++learned_count;
    }
  }
  return learned_count;
}

int Session::MaybeLearnZenzProjectedSegmentsToMozcHistory(
    const std::vector<ZenzProjectedLearningSegment>& segments) {
  if (IsGrimodexSecureInput()) {
    return 0;
  }

  if (!UseZenzFeedbackLearning(context_->GetConfig())) {
    return 0;
  }
  if (segments.size() < 2) {
    return 0;
  }
  if (context_->composer().GetInputFieldType() ==
      commands::Context::PASSWORD) {
    return 0;
  }

  std::vector<ExternalConversionSegment> external_segments;
  external_segments.reserve(segments.size());
  for (const ZenzProjectedLearningSegment& segment : segments) {
    const std::string& key = segment.key;
    const std::string& value = segment.value;
    if (key.empty() || value.empty()) {
      return 0;
    }

    const ZenzTextPrivacyDecision key_privacy =
        EvaluateZenzLiveKeyPrivacy(key);
    if (!key_privacy.allow) {
      ZenzDebugOutput(absl::StrCat(
          "[zenz-feedback] skip projected mozc history key_privacy reason=",
          key_privacy.reason,
          " ",
          ZenzRedactedTextStats("key", key)));
      return 0;
    }

    const ZenzTextPrivacyDecision value_privacy =
        EvaluateZenzLiveValuePrivacy(value);
    if (!value_privacy.allow) {
      ZenzDebugOutput(absl::StrCat(
          "[zenz-feedback] skip projected mozc history value_privacy reason=",
          value_privacy.reason,
          " ",
          ZenzRedactedTextStats("value", value)));
      return 0;
    }

    external_segments.push_back({key, value, segment.is_reranked});
  }

  if (!context_->mutable_converter()->LearnExternalConversionSegments(
          external_segments, context_->client_context())) {
    return 0;
  }
  return static_cast<int>(external_segments.size());
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
  if (IsGrimodexSecureInput()) {
    return;
  }

  if (!UseZenzFeedbackLearning(context_->GetConfig())) {
    return;
  }

  if (key.empty() || value.empty()) {
    return;
  }

  const ZenzTextPrivacyDecision key_privacy =
      EvaluateZenzLiveKeyPrivacy(key);
  if (!key_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] skip pending accepted key_privacy reason=",
        key_privacy.reason,
        " ",
        ZenzRedactedTextStats("key", key)));
    return;
  }

  const ZenzTextPrivacyDecision value_privacy =
      EvaluateZenzLiveValuePrivacy(value);
  if (!value_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] skip pending accepted value_privacy reason=",
        value_privacy.reason,
        " ",
        ZenzRedactedTextStats("value", value)));
    return;
  }

  pending_zenz_feedback_.pending = true;
  pending_zenz_feedback_.action = PendingZenzFeedback::Action::kAccepted;
  pending_zenz_feedback_.key = std::string(key);
  pending_zenz_feedback_.context_class =
      context_class.empty() ? "empty" : std::string(context_class);
  pending_zenz_feedback_.value = std::string(value);
  pending_zenz_feedback_.reason.clear();
  pending_zenz_feedback_.has_final_committed_value = false;
  pending_zenz_feedback_.final_committed_value.clear();
  const ZenzReverseLearningProjection reverse_learning_projection =
      BuildZenzReverseLearningSegmentsFromPreedit(
          live_conversion_preedit_output_, key, value);
  pending_zenz_feedback_.reverse_learning_segments =
      reverse_learning_projection.changed_segments;
  pending_zenz_feedback_.reverse_projected_learning_segments =
      reverse_learning_projection.projected_segments;

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] pending accepted ",
      ZenzRedactedTextStats("key", key),
      " ", ZenzRedactedTextStats("value", value),
      " context_class=", pending_zenz_feedback_.context_class));
}

void Session::SetPendingZenzFeedbackRejected(absl::string_view reason) {
  if (IsGrimodexSecureInput()) {
    return;
  }

  if (!UseZenzFeedbackLearning(context_->GetConfig())) {
    return;
  }

  if (!HasVisibleZenzLiveCorrection()) {
    return;
  }

  const ZenzTextPrivacyDecision key_privacy =
      EvaluateZenzLiveKeyPrivacy(zenz_live_key_);
  if (!key_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] skip pending rejected key_privacy reason=",
        key_privacy.reason,
        " ",
        ZenzRedactedTextStats("key", zenz_live_key_)));
    return;
  }

  const ZenzTextPrivacyDecision value_privacy =
      EvaluateZenzLiveValuePrivacy(zenz_live_value_);
  if (!value_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] skip pending rejected value_privacy reason=",
        value_privacy.reason,
        " ",
        ZenzRedactedTextStats("value", zenz_live_value_)));
    return;
  }

  pending_zenz_feedback_.pending = true;
  pending_zenz_feedback_.action = PendingZenzFeedback::Action::kRejected;
  pending_zenz_feedback_.key = zenz_live_key_;
  pending_zenz_feedback_.context_class =
      zenz_live_context_class_.empty() ? "empty" : zenz_live_context_class_;
  pending_zenz_feedback_.value = zenz_live_value_;
  pending_zenz_feedback_.reason = std::string(reason);
  pending_zenz_feedback_.has_final_committed_value = false;
  pending_zenz_feedback_.final_committed_value.clear();
  pending_zenz_feedback_.reverse_learning_segments.clear();
  pending_zenz_feedback_.reverse_projected_learning_segments.clear();

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] pending rejected ",
      ZenzRedactedTextStats("key", pending_zenz_feedback_.key),
      " ", ZenzRedactedTextStats("value", pending_zenz_feedback_.value),
      " context_class=", pending_zenz_feedback_.context_class,
      " reason=", pending_zenz_feedback_.reason));
}

void Session::ObservePendingZenzFeedbackCommittedResult(
    const commands::Command& command,
    absl::string_view reason) {
  if (!pending_zenz_feedback_.pending ||
      pending_zenz_feedback_.action != PendingZenzFeedback::Action::kRejected) {
    return;
  }

  // Only a fully committed conversion/direct commit should resolve pending
  // rejected feedback. Partial segment commits stay in CONVERSION and must not
  // be interpreted as the user's final full-sequence decision.
  if (context_->state() != ImeContext::PRECOMPOSITION) {
    return;
  }

  if (!command.output().has_result() ||
      !command.output().result().has_value()) {
    return;
  }

  pending_zenz_feedback_.has_final_committed_value = true;
  pending_zenz_feedback_.final_committed_value =
      command.output().result().value();

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] observed final committed value reason=", reason,
      " ", ZenzRedactedTextStats("key", pending_zenz_feedback_.key),
      " ", ZenzRedactedTextStats("zenz_value", pending_zenz_feedback_.value),
      " ", ZenzRedactedTextStats("final_value",
                                  pending_zenz_feedback_.final_committed_value),
      " context_class=", pending_zenz_feedback_.context_class,
      " pending_reason=", pending_zenz_feedback_.reason));
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

    // Accepted feedback stored in the TSV remains full-sequence scoped.
    // Any broader generalization is delegated to Mozc history learning below.
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

    const int projected_segment_learning_count =
        MaybeLearnZenzProjectedSegmentsToMozcHistory(
            pending_zenz_feedback_.reverse_projected_learning_segments);

    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] projected segment mozc history learning count=",
        projected_segment_learning_count,
        " context_class=", pending_zenz_feedback_.context_class));

    int reverse_segment_learning_count = 0;
    if (projected_segment_learning_count == 0) {
      reverse_segment_learning_count =
          MaybeLearnZenzReverseSegmentsToMozcHistory(
              pending_zenz_feedback_.reverse_learning_segments);
    }

    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] reverse segment mozc history learning count=",
        reverse_segment_learning_count,
        " context_class=", pending_zenz_feedback_.context_class));
  } else if (pending_zenz_feedback_.action ==
             PendingZenzFeedback::Action::kRejected) {
    if (!IsExplicitZenzHardRejectReason(pending_zenz_feedback_.reason) &&
        !pending_zenz_feedback_.has_final_committed_value) {
      ZenzDebugOutput(absl::StrCat(
          "[zenz-feedback] neutralize pending rejected without final commit ",
          ZenzRedactedTextStats("key", pending_zenz_feedback_.key),
          " ", ZenzRedactedTextStats("value", pending_zenz_feedback_.value),
          " context_class=", pending_zenz_feedback_.context_class,
          " reason=", pending_zenz_feedback_.reason));
    } else if (!IsExplicitZenzHardRejectReason(pending_zenz_feedback_.reason) &&
               pending_zenz_feedback_.final_committed_value ==
                   pending_zenz_feedback_.value) {
      ZenzDebugOutput(absl::StrCat(
          "[zenz-feedback] neutralize pending rejected same final value ",
          ZenzRedactedTextStats("key", pending_zenz_feedback_.key),
          " ", ZenzRedactedTextStats("value", pending_zenz_feedback_.value),
          " context_class=", pending_zenz_feedback_.context_class,
          " reason=", pending_zenz_feedback_.reason));
    } else {
      ZenzDebugOutput(absl::StrCat(
          "[zenz-feedback] confirm pending rejected ",
          ZenzRedactedTextStats("key", pending_zenz_feedback_.key),
          " ", ZenzRedactedTextStats("value", pending_zenz_feedback_.value),
          " context_class=", pending_zenz_feedback_.context_class,
          " reason=", pending_zenz_feedback_.reason));

      // Rejected feedback is full-sequence scoped too.  A final mismatch after
      // Space revert should not create segment-local negative evidence.
      zenz_feedback_store_.RecordRejected(
          pending_zenz_feedback_.key,
          pending_zenz_feedback_.context_class,
          pending_zenz_feedback_.value,
          pending_zenz_feedback_.reason);
    }
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

bool Session::SetPendingDirectCommitLearning(
    absl::string_view key,
    absl::string_view value,
    absl::string_view reason) {
  if (key.empty() || value.empty()) {
    return false;
  }

  if (context_->composer().GetInputFieldType() ==
      commands::Context::PASSWORD) {
    return false;
  }

  pending_direct_commit_learning_.pending = true;
  pending_direct_commit_learning_.key = std::string(key);
  pending_direct_commit_learning_.value = std::string(value);
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

bool Session::SetPendingDirectCommitLearningFromCommittedResult(
    const commands::Command& command,
    absl::string_view reason) {
  if (!command.output().has_result()) {
    return false;
  }

  const commands::Result& result = command.output().result();
  return SetPendingDirectCommitLearning(result.key(), result.value(), reason);
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
  zenz_live_display_key_.clear();
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

  if (IsGrimodexSecureInput() ||
      context_->composer().GetInputFieldType() == commands::Context::PASSWORD) {
    return false;
  }

  if (live_conversion_key_.empty() || live_conversion_value_.empty()) {
    return false;
  }

  // Single-segment feedback is handled by ZenzFeedbackCandidateRewriter in the
  // converter rewriter chain, before UserSegmentHistoryRewriter.  Do not replay
  // it again here as a session-level fast path; otherwise stale Zenz feedback
  // can override a newer explicit user-history selection.
  //
  // Multi-segment live conversions cannot be safely represented by
  // ZenzFeedbackCandidateRewriter without collapsing converter-owned segment
  // boundaries.  Keep the fast path only for those full-phrase corrections.
  if (live_conversion_preedit_output_.segment_size() <= 1) {
    return false;
  }

  const ZenzTextPrivacyDecision key_privacy =
      EvaluateZenzLiveKeyPrivacy(live_conversion_key_);
  if (!key_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] fast path skip key_privacy reason=",
        key_privacy.reason,
        " ",
        ZenzRedactedTextStats("key", live_conversion_key_)));
    return false;
  }

  const ZenzTextPrivacyDecision mozc_value_privacy =
      EvaluateZenzLiveValuePrivacy(live_conversion_value_);
  if (!mozc_value_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] fast path skip mozc_value_privacy reason=",
        mozc_value_privacy.reason,
        " ",
        ZenzRedactedTextStats("value", live_conversion_value_)));
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
          live_conversion_key_, context_class,
          GetZenzFeedbackAutoBlockPolicy(config));

  if (feedback_candidates.empty()) {
    return false;
  }

  for (const ZenzFeedbackCandidate& feedback_candidate :
       feedback_candidates) {
    const std::string& feedback_value = feedback_candidate.value;

    const ZenzTextPrivacyDecision feedback_value_privacy =
        EvaluateZenzLiveValuePrivacy(feedback_value);
    if (!feedback_value_privacy.allow) {
      ZenzDebugOutput(absl::StrCat(
          "[zenz-feedback] fast path candidate rejected reason=value_privacy_",
          feedback_value_privacy.reason,
          " ",
          ZenzRedactedTextStats("key", live_conversion_key_),
          " ",
          ZenzRedactedTextStats("value", feedback_value),
          " context_class=", context_class,
          " accepted_count=", feedback_candidate.accepted_count,
          " rejected_count=", feedback_candidate.rejected_count));
      continue;
    }

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

    ZenzAdoptionInput adoption_input;
    adoption_input.key = live_conversion_key_;
    adoption_input.mozc_value = live_conversion_value_;
    adoption_input.zenz_value = feedback_value;
    adoption_input.protected_spans = live_conversion_protected_spans_;

    const ZenzAdoptionResult adoption =
        zenz_adoption_policy_.Decide(adoption_input);
    if (adoption.action == ZenzAdoptionResult::Action::kReject) {
      ZenzDebugOutput(absl::StrCat(
          "[zenz-feedback] fast path candidate rejected reason=",
          adoption.reason,
          " ", ZenzRedactedTextStats("key", live_conversion_key_),
          " ", ZenzRedactedTextStats("value", feedback_value),
          " context_class=", context_class,
          " accepted_count=", feedback_candidate.accepted_count,
          " rejected_count=", feedback_candidate.rejected_count));
      continue;
    }

    const std::string adopted_feedback_value = adoption.value;

    ++zenz_live_generation_;
    pending_zenz_live_ = PendingZenzLiveCorrection();

    zenz_live_visible_generation_ = zenz_live_generation_;
    zenz_live_key_ = live_conversion_key_;
    zenz_live_display_key_ = live_conversion_preedit_.empty()
                                 ? live_conversion_key_
                                 : live_conversion_preedit_;
    zenz_live_value_ = adopted_feedback_value;
    zenz_live_mozc_value_ = live_conversion_value_;
    zenz_live_context_class_ = context_class;
    zenz_live_left_context_ = left_context_for_validation;

    ZenzDebugOutput(absl::StrCat(
        "[zenz-feedback] fast path applied ",
        ZenzRedactedTextStats("key", zenz_live_key_),
        " ", ZenzRedactedTextStats("value", zenz_live_value_),
        " ", ZenzRedactedTextStats("mozc_value", zenz_live_mozc_value_),
        " adoption=", adoption.reason,
        " context_class=", zenz_live_context_class_,
        " accepted_count=", feedback_candidate.accepted_count,
        " rejected_count=", feedback_candidate.rejected_count));

    return OutputZenzLiveCorrection(adopted_feedback_value, command);
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

  if (IsGrimodexSecureInput() ||
      context_->composer().GetInputFieldType() == commands::Context::PASSWORD) {
    return false;
  }

  if (live_conversion_key_.empty() || live_conversion_value_.empty()) {
    return false;
  }

  const ZenzTextPrivacyDecision key_privacy =
      EvaluateZenzLiveKeyPrivacy(live_conversion_key_);
  if (!key_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] skip key_privacy reason=",
        key_privacy.reason,
        " ",
        ZenzRedactedTextStats("key", live_conversion_key_)));
    return false;
  }

  const ZenzTextPrivacyDecision mozc_value_privacy =
      EvaluateZenzLiveValuePrivacy(live_conversion_value_);
  if (!mozc_value_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] skip mozc_value_privacy reason=",
        mozc_value_privacy.reason,
        " ",
        ZenzRedactedTextStats("value", live_conversion_value_)));
    return false;
  }

  const uint32_t min_key_len = GetZenzLiveCorrectionMinKeyLength(config);
  if (Util::CharsLen(live_conversion_key_) < min_key_len) {
    return false;
  }

  const uint32_t left_context_len =
      GetZenzLiveCorrectionLeftContextLength(config);
  const uint32_t right_context_len =
      GetZenzLiveCorrectionRightContextLength(config);

  const std::string raw_left_context =
      ExtractZenzLeftContext(left_context_len);
  const ZenzContextSanitizationResult context_result =
      zenz_context_sanitizer_.SanitizeForZenz(
          raw_left_context, left_context_len);

  const std::string left_context_for_prompt =
      context_result.allowed_for_prompt
          ? context_result.sanitized_context
          : std::string();

  const std::string raw_right_context =
      ExtractZenzRightContext(right_context_len);
  const ZenzContextSanitizationResult right_context_result =
      zenz_context_sanitizer_.SanitizeForZenz(
          raw_right_context, right_context_len);

  const std::string right_context_for_prompt =
      right_context_result.allowed_for_prompt
          ? right_context_result.sanitized_context
          : std::string();

  ZenzPromptOptions prompt_options;
  prompt_options.left_context = left_context_for_prompt;
  prompt_options.right_context = right_context_for_prompt;
  prompt_options.profile = config.zenz_live_correction_profile();
  prompt_options.topic = config.zenz_live_correction_topic();
  prompt_options.style = config.zenz_live_correction_style();
  prompt_options.settings = config.zenz_live_correction_settings();

  // Conditions are carried by the exact immutable dictionary snapshot pinned
  // for this composition.  Reading them here prevents a new project reload
  // from mixing prompt metadata with an older native dictionary generation.
  if (const std::shared_ptr<const dictionary::ProjectDictionarySnapshot>
          project = context_->converter().GetPinnedProjectDictionary();
      project != nullptr) {
    const dictionary::ProjectDictionaryMetadata& metadata =
        project->metadata();
    if (metadata.topic.has_value()) {
      prompt_options.topic = *metadata.topic;
    }
    if (metadata.style.has_value()) {
      prompt_options.style = *metadata.style;
    }
    if (metadata.preference.has_value()) {
      prompt_options.settings = *metadata.preference;
    }
  }

  ZenzProtectedPromptInput protected_prompt_input;
  protected_prompt_input.key = live_conversion_key_;
  protected_prompt_input.protected_spans = live_conversion_protected_spans_;
  const ZenzProtectedPromptResult protected_prompt =
      zenz_adoption_policy_.ProtectPromptKey(protected_prompt_input);

  ZenzPromptBuilder prompt_builder;
  const std::string prompt =
      prompt_builder.Build(protected_prompt.key, prompt_options);

  ++zenz_live_generation_;

  pending_zenz_live_.generation = zenz_live_generation_;
  pending_zenz_live_.key = live_conversion_key_;
  pending_zenz_live_.left_context = left_context_for_prompt;
  pending_zenz_live_.right_context = right_context_for_prompt;
  pending_zenz_live_.context_class = context_result.context_class;
  pending_zenz_live_.mozc_value = live_conversion_value_;
  pending_zenz_live_.symbol_style_source =
      live_conversion_preedit_.empty() ? live_conversion_key_
                                       : live_conversion_preedit_;
  pending_zenz_live_.prompt = prompt;
  pending_zenz_live_.protected_spans = protected_prompt.protected_spans;
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
      " context_reason=", context_result.reason,
      " right_context_allowed=",
      ZenzBool(right_context_result.allowed_for_prompt),
      " right_context_reason=", right_context_result.reason,
      " protected_prompt_replacements=",
      protected_prompt.placeholder_count));

  const uint32_t delay_msec = GetZenzLiveCorrectionDelayMsec(config);
  if (delay_msec == 0) {
    ZenzDebugOutput("[zenz] start immediately");
    // The current command already contains the freshly generated live
    // conversion output from MaybeStartLiveConversion().  Do not call
    // Output() again in the same key-event path, because PopOutput() is
    // destructive and a second output refresh can momentarily duplicate the
    // composition text on some TSF clients.
    return AdvancePendingZenzLiveCorrection(
        command, /*refresh_output_on_submit=*/false);
  }

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

    if (command->output().has_preedit()) {
      RestorePreeditSegmentKeysForSymbolStyle(
          live_conversion_preedit_.empty()
              ? live_conversion_key_
              : live_conversion_preedit_,
          command->mutable_output()->mutable_preedit());
    }

    commands::Output* output = command->mutable_output();
    output->set_live_conversion(true);
    output->set_live_conversion_pending(false);
    output->set_zenz_live_correction_pending(true);
    AttachCachedLiveConversionSuggestionCandidateWindow(output);
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

    if (command->output().has_preedit()) {
      RestorePreeditSegmentKeysForSymbolStyle(
          live_conversion_preedit_.empty()
              ? live_conversion_key_
              : live_conversion_preedit_,
          command->mutable_output()->mutable_preedit());
    }

    commands::Output* output = command->mutable_output();
    output->set_live_conversion(true);
    output->set_live_conversion_pending(false);
    output->set_zenz_live_correction_pending(false);
    if (!debug.empty()) {
      output->set_zenz_live_correction_debug(std::string(debug));
    }
    AttachCachedLiveConversionSuggestionCandidateWindow(output);
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
        std::make_unique<ZenzNamedPipeClient>());
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

  return AdvancePendingZenzLiveCorrection(
      command, /*refresh_output_on_submit=*/true);
}

bool Session::AdvancePendingZenzLiveCorrection(
    commands::Command* command,
    const bool refresh_output_on_submit) {
  command->mutable_output()->set_consumed(true);

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
    request.backend_device = config.zenz_live_correction_device();
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
                                    pending_zenz_live_.left_context),
        " ", ZenzRedactedTextStats("right_context",
                                    pending_zenz_live_.right_context)));

    EnsureZenzLiveCorrector()->Submit(std::move(request));

    bool result = true;
    if (refresh_output_on_submit) {
      result = OutputCurrentLiveConversionWithZenzPending(command);
    } else {
      commands::Output* output = command->mutable_output();
      output->set_live_conversion(true);
      output->set_live_conversion_pending(false);
      output->set_zenz_live_correction_pending(true);
      AttachCachedLiveConversionSuggestionCandidateWindow(output);
    }
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

    if (command->output().has_preedit()) {
      RestorePreeditSegmentKeysForSymbolStyle(
          live_conversion_preedit_.empty()
              ? live_conversion_key_
              : live_conversion_preedit_,
          command->mutable_output()->mutable_preedit());
    }

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

  const std::string zenz_value_before_placeholder_restore = zenz_value;
  zenz_value = zenz_adoption_policy_.RestorePlaceholders(
      zenz_value, pending_zenz_live_.protected_spans);
  if (zenz_value != zenz_value_before_placeholder_restore) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] restored protected placeholders ",
        ZenzRedactedTextStats("value", zenz_value),
        " ", ZenzRedactedTextStats(
                 "raw_value", zenz_value_before_placeholder_restore),
        " context_class=", pending_zenz_live_.context_class));
  }

  const absl::string_view zenz_symbol_style_source =
      pending_zenz_live_.symbol_style_source.empty()
          ? pending_zenz_live_.key
          : pending_zenz_live_.symbol_style_source;

  const std::string zenz_value_before_symbol_restore = zenz_value;
  zenz_value = ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
      zenz_symbol_style_source, pending_zenz_live_.mozc_value, zenz_value);

  const std::string zenz_display_key =
      ZenzOutputValidator::RestoreUserVisibleSymbolStyle(
          zenz_symbol_style_source,
          pending_zenz_live_.mozc_value,
          pending_zenz_live_.key);

  if (zenz_value != zenz_value_before_symbol_restore) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] restored symbol style ",
        ZenzRedactedTextStats("value", zenz_value),
        " ", ZenzRedactedTextStats("raw_value",
                                   zenz_value_before_symbol_restore),
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

    if (command->output().has_preedit()) {
      RestorePreeditSegmentKeysForSymbolStyle(
          live_conversion_preedit_.empty()
              ? live_conversion_key_
              : live_conversion_preedit_,
          command->mutable_output()->mutable_preedit());
    }

    command->mutable_output()->set_live_conversion(true);
    command->mutable_output()->set_live_conversion_pending(false);
    command->mutable_output()->set_zenz_live_correction_pending(false);
    command->mutable_output()->set_zenz_live_correction_debug(
        validation.reason);
    return true;
  }

  const ZenzTextPrivacyDecision key_privacy =
      EvaluateZenzLiveKeyPrivacy(pending_zenz_live_.key);
  if (!key_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] validation rejected reason=key_privacy_",
        key_privacy.reason,
        " ",
        ZenzRedactedTextStats("key", pending_zenz_live_.key),
        " ",
        ZenzRedactedTextStats("mozc_value",
                              pending_zenz_live_.mozc_value),
        " context_class=", context_class));

    CancelPendingZenzLiveCorrection();
    Output(command);

    if (command->output().has_preedit()) {
      RestorePreeditSegmentKeysForSymbolStyle(
          live_conversion_preedit_.empty()
              ? live_conversion_key_
              : live_conversion_preedit_,
          command->mutable_output()->mutable_preedit());
    }

    command->mutable_output()->set_live_conversion(true);
    command->mutable_output()->set_live_conversion_pending(false);
    command->mutable_output()->set_zenz_live_correction_pending(false);
    command->mutable_output()->set_zenz_live_correction_debug(
        absl::StrCat("key_privacy_", key_privacy.reason));
    return true;
  }

  const ZenzTextPrivacyDecision value_privacy =
      EvaluateZenzLiveValuePrivacy(zenz_value);
  if (!value_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] validation rejected reason=value_privacy_",
        value_privacy.reason,
        " ",
        ZenzRedactedTextStats("value", zenz_value),
        " ",
        ZenzRedactedTextStats("mozc_value",
                              pending_zenz_live_.mozc_value),
        " context_class=", context_class));

    CancelPendingZenzLiveCorrection();
    Output(command);

    if (command->output().has_preedit()) {
      RestorePreeditSegmentKeysForSymbolStyle(
          live_conversion_preedit_.empty()
              ? live_conversion_key_
              : live_conversion_preedit_,
          command->mutable_output()->mutable_preedit());
    }

    command->mutable_output()->set_live_conversion(true);
    command->mutable_output()->set_live_conversion_pending(false);
    command->mutable_output()->set_zenz_live_correction_pending(false);
    command->mutable_output()->set_zenz_live_correction_debug(
        absl::StrCat("value_privacy_", value_privacy.reason));
    return true;
  }

  ZenzAdoptionInput adoption_input;
  adoption_input.key = pending_zenz_live_.key;
  adoption_input.mozc_value = pending_zenz_live_.mozc_value;
  adoption_input.zenz_value = zenz_value;
  adoption_input.protected_spans = pending_zenz_live_.protected_spans;

  const ZenzAdoptionResult adoption =
      zenz_adoption_policy_.Decide(adoption_input);
  if (adoption.action == ZenzAdoptionResult::Action::kReject) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] adoption rejected reason=", adoption.reason,
        " ", ZenzRedactedTextStats("value", zenz_value),
        " ", ZenzRedactedTextStats("mozc_value",
                                    pending_zenz_live_.mozc_value),
        " context_class=", context_class));

    CancelPendingZenzLiveCorrection();
    Output(command);

    if (command->output().has_preedit()) {
      RestorePreeditSegmentKeysForSymbolStyle(
          live_conversion_preedit_.empty()
              ? live_conversion_key_
              : live_conversion_preedit_,
          command->mutable_output()->mutable_preedit());
    }

    command->mutable_output()->set_live_conversion(true);
    command->mutable_output()->set_live_conversion_pending(false);
    command->mutable_output()->set_zenz_live_correction_pending(false);
    command->mutable_output()->set_zenz_live_correction_debug(adoption.reason);
    return true;
  }

  zenz_value = adoption.value;

  const ZenzTextPrivacyDecision adopted_value_privacy =
      EvaluateZenzLiveValuePrivacy(zenz_value);
  if (!adopted_value_privacy.allow) {
    ZenzDebugOutput(absl::StrCat(
        "[zenz] adoption rejected reason=adopted_value_privacy_",
        adopted_value_privacy.reason,
        " ", ZenzRedactedTextStats("value", zenz_value),
        " context_class=", context_class));

    CancelPendingZenzLiveCorrection();
    Output(command);
    command->mutable_output()->set_live_conversion(true);
    command->mutable_output()->set_live_conversion_pending(false);
    command->mutable_output()->set_zenz_live_correction_pending(false);
    command->mutable_output()->set_zenz_live_correction_debug(
        absl::StrCat("adopted_value_privacy_",
                     adopted_value_privacy.reason));
    return true;
  }

  std::string feedback_reason = "feedback_learning_disabled";

  if (UseZenzFeedbackLearning(config)) {
    const ZenzFeedbackDecision feedback_decision =
        zenz_feedback_store_.Decide(
            pending_zenz_live_.key, context_class, zenz_value,
            GetZenzFeedbackAutoBlockPolicy(config));

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
      " adoption=", adoption.reason,
      " feedback=", feedback_reason));

  zenz_live_visible_generation_ = pending_zenz_live_.generation;
  zenz_live_key_ = pending_zenz_live_.key;
  zenz_live_display_key_ = zenz_display_key;
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

  const absl::string_view display_key =
      zenz_live_display_key_.empty() ? zenz_live_key_ : zenz_live_display_key_;

  AddPreeditSegment(
      display_key,
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

  AttachCachedLiveConversionSuggestionCandidateWindow(output);
  return true;
}

bool Session::RevertZenzLiveCorrectionToNormalConversion(
    commands::Command* command) {
  if (!HasVisibleZenzLiveCorrection()) {
    return false;
  }

  ZenzDebugOutput(absl::StrCat(
      "[zenz-feedback] revert zenz correction to mozc normal conversion ",
      ZenzRedactedTextStats("key", zenz_live_key_),
      " ", ZenzRedactedTextStats("zenz_value", zenz_live_value_),
      " ", ZenzRedactedTextStats("mozc_value", zenz_live_mozc_value_),
      " visible_generation=", zenz_live_visible_generation_));

  SetPendingZenzFeedbackRejected("space_revert_zenz_to_mozc");

  const commands::Preedit live_preedit = live_conversion_preedit_output_;
  const std::string live_value = live_conversion_value_;

  // This Space is already a conversion operation.  After peeling off the zenz
  // layer, keep the converter's current segments but leave live conversion
  // mode.  This makes the next character input follow normal conversion
  // semantics: commit the restored Mozc result first, then start a new
  // composition.
  ClearLiveConversionState();
  context_->mutable_converter()->SetCandidateListVisible(false);

  command->mutable_output()->set_consumed(true);
  OutputMode(command);

  commands::Output* output = command->mutable_output();
  output->clear_candidate_window();

  if (live_preedit.segment_size() > 0) {
    *output->mutable_preedit() = live_preedit;
    output->mutable_preedit()->set_cursor(Util::CharsLen(live_value));
  } else {
    Output(command);
    output = command->mutable_output();
    output->clear_candidate_window();
  }

  output->set_live_conversion(false);
  output->set_live_conversion_pending(false);
  output->set_zenz_live_correction_pending(false);
  output->set_zenz_live_correction_applied(false);

  return true;
}

bool Session::CommitZenzLiveCorrectionResult(commands::Command* command) {
  if (IsGrimodexSecureInput()) {
    return false;
  }

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

void Session::ClearPendingRerankedPreeditCommitAfterConvertCancel() {
  pending_reranked_preedit_commit_after_convert_cancel_ = false;
  pending_reranked_preedit_commit_key_.clear();
  pending_reranked_preedit_commit_value_.clear();
  pending_reranked_preedit_commit_segment_keys_.clear();
}

void Session::MaybeSetPendingRerankedPreeditCommitAfterConvertCancel() {
  ClearPendingRerankedPreeditCommitAfterConvertCancel();

  const std::string key = context_->composer().GetQueryForConversion();
  const std::string value = context_->composer().GetStringForSubmission();
  if (key.empty() || value.empty()) {
    return;
  }

  pending_reranked_preedit_commit_after_convert_cancel_ = true;
  pending_reranked_preedit_commit_key_ = key;
  pending_reranked_preedit_commit_value_ = value;

  // Snapshot the visible conversion segment keys before converter->Cancel()
  // clears them.  If the user commits the restored hiragana unchanged, these
  // boundaries let user segment history learn each original conversion segment
  // separately instead of learning only the whole restored preedit string.
  std::vector<std::string> segment_keys;
  if (!context_->converter().GetConversionSegmentKeys(&segment_keys)) {
    return;
  }

  std::string joined_key;
  for (const std::string& segment_key : segment_keys) {
    if (segment_key.empty()) {
      segment_keys.clear();
      break;
    }
    joined_key.append(segment_key);
  }
  if (!segment_keys.empty() && joined_key == key) {
    pending_reranked_preedit_commit_segment_keys_ = std::move(segment_keys);
  }
}

bool Session::ShouldMarkPreeditCommitAsRerankedAfterConvertCancel() const {
  return ShouldMarkPreeditCommitAsRerankedAfterConvertCancel(
      context_->composer());
}

bool Session::ShouldMarkPreeditCommitAsRerankedAfterConvertCancel(
    const composer::Composer& composer) const {
  if (!pending_reranked_preedit_commit_after_convert_cancel_) {
    return false;
  }

  if (composer.GetInputFieldType() == commands::Context::PASSWORD) {
    return false;
  }

  const std::string key = composer.GetQueryForConversion();
  const std::string value = composer.GetStringForSubmission();
  if (key != pending_reranked_preedit_commit_key_ ||
      value != pending_reranked_preedit_commit_value_) {
    return false;
  }

  if (key != value) {
    return false;
  }

  if (Util::CharsLen(key) <
      kMinRerankedPreeditCommitCharsAfterConvertCancel) {
    return false;
  }

  return Util::IsScriptType(key, Util::HIRAGANA);
}

bool Session::CommitPendingRerankedPreeditAfterConvertCancelForDirectCommit(
    const composer::Composer& composer,
    const commands::Context& context,
    absl::string_view reason) {
  if (!pending_reranked_preedit_commit_after_convert_cancel_) {
    return false;
  }

  if (!ShouldMarkPreeditCommitAsRerankedAfterConvertCancel(composer)) {
    ClearPendingRerankedPreeditCommitAfterConvertCancel();
    return false;
  }

  const std::string key = composer.GetQueryForConversion();
  const std::string value = composer.GetStringForSubmission();

  const std::vector<std::string> segment_keys =
      pending_reranked_preedit_commit_segment_keys_;
  context_->mutable_converter()->CommitPreedit(
      composer, context, true, segment_keys);
  SetPendingDirectCommitLearning(key, value, reason);
  ClearPendingRerankedPreeditCommitAfterConvertCancel();
  return true;
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

    ClearPendingRerankedPreeditCommitAfterConvertCancel();
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
    const composer::Composer composer_before_insert = context_->composer();
    context_->mutable_composer()->InsertCharacterKeyEvent(key);
    ClearUndoContext();

    if (CanDirectCommitAfterPunctuation(key)) {
      const auto [direct_commit_key, direct_commit_value] =
          GetDirectCommitStringsWithDirectCommitSuffixFallback(
              composer_before_insert, key);
      const bool learned_reranked_preedit_after_cancel =
          CommitPendingRerankedPreeditAfterConvertCancelForDirectCommit(
              composer_before_insert, command->input().context(),
              "convert_cancel_direct_commit_punctuation");
      if (learned_reranked_preedit_after_cancel) {
        ClearLiveConversionState();
        CommitStringDirectly(direct_commit_key, direct_commit_value, command);
        return true;
      }
      return CommitPendingLiveConversionDisplayDirectly(command);
    }

    ClearPendingRerankedPreeditCommitAfterConvertCancel();

    // The physical key looked like a direct-commit trigger before insertion,
    // but the romaji table may have turned it into a different character.
    // Example: "v." -> "…" or "v," -> "‥".
    //
    // In that case, do not fall back to raw composition, because it discards
    // the stable converted prefix shown by live conversion, e.g. "今日は".
    // Treat it as ordinary continued composition and schedule live conversion
    // for the updated preedit.
    CancelPendingLiveConversion();
    if (MaybeScheduleLiveConversion(command)) {
      return true;
    }

    OutputComposition(command);
    return true;
  }

  const bool was_live_conversion = live_conversion_active_;
  const bool was_pending_live_conversion = live_conversion_pending_;

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

  if ((live_conversion_active_ || live_conversion_pending_) &&
      ShouldCommitLiveConversionBeforeShiftAsciiInput(
          context_->GetConfig(), context_->composer(), key)) {
    ClearPendingRerankedPreeditCommitAfterConvertCancel();
    if (had_visible_zenz_correction) {
      CommitZenzLiveCorrectionResult(command);
    } else if (live_conversion_active_) {
      const std::string live_key =
          live_conversion_key_.empty()
              ? context_->composer().GetQueryForConversion()
              : live_conversion_key_;
      const std::string live_value =
          live_conversion_value_.empty()
              ? context_->composer().GetStringForSubmission()
              : live_conversion_value_;

      ClearLiveConversionState();
      CommitStringDirectly(live_key, live_value, command);
    } else if (live_conversion_pending_) {
      CommitPendingLiveConversionDisplayDirectly(command);
    }

    context_->mutable_composer()->InsertCharacterKeyEvent(key);
    ClearUndoContext();
    SetSessionState(ImeContext::COMPOSITION, context_.get());
    OutputComposition(command);
    return true;
  }

  // If the current conversion was started by live conversion, ordinary
  // character input should continue editing the composition.  So cancel
  // the temporary conversion before handling candidate shortcuts.
  CancelLiveConversionForEditing();

  // Handle shortcut keys selecting a candidate from a list.
  if (MaybeSelectCandidate(command)) {
    ClearPendingRerankedPreeditCommitAfterConvertCancel();
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

    // HandlePendingZenzFeedbackForKeyEvent() intentionally does not confirm
    // feedback while the session is still in CONVERSION, because conversion
    // keys may still be part of selecting the result.  An ordinary text input
    // that reaches this point has already committed the current conversion, so
    // it is now the next real text input after the zenz decision.
    ConfirmPendingZenzFeedback();

    if (key.input_style() == commands::KeyEvent::DIRECT_INPUT) {
      // Do ClearUndoContext() because it is a direct input.
      ClearPendingRerankedPreeditCommitAfterConvertCancel();
      ClearUndoContext();
      context_->mutable_composer()->InsertCharacterKeyEvent(key);
      CommitCompositionDirectly(command);
      return true;
    }
  }

  const composer::Composer composer_before_insert = context_->composer();
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
    const auto [direct_commit_key, direct_commit_value] =
        GetDirectCommitStringsWithDirectCommitSuffixFallback(
            composer_before_insert, key);
    const bool learned_reranked_preedit_after_cancel =
        CommitPendingRerankedPreeditAfterConvertCancelForDirectCommit(
            composer_before_insert, command->input().context(),
            "convert_cancel_direct_commit_punctuation");

    if (!learned_reranked_preedit_after_cancel && had_visible_zenz_correction) {
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

    if (!learned_reranked_preedit_after_cancel &&
        (live_conversion_pending_ || was_pending_live_conversion)) {
      return CommitPendingLiveConversionDisplayDirectly(command);
    }

    if (!learned_reranked_preedit_after_cancel && was_live_conversion &&
        CommitLiveConversionResult(command)) {
      return true;
    }

    if (learned_reranked_preedit_after_cancel) {
      ClearLiveConversionState();
      CommitStringDirectly(direct_commit_key, direct_commit_value, command);
      return true;
    }

    if (committed_conversion_before_insert) {
      SetPendingDirectCommitLearningFromCommittedResult(
          *command,
          "normal_conversion_direct_commit_punctuation");
    }

    // Most direct-commit punctuation paths can commit the current Composer
    // contents as-is.  However,
    // GetDirectCommitStringsWithDirectCommitSuffixFallback() may explicitly
    // append a suffix derived from the key event when Composer has not yet
    // reflected the trigger.  Use the rewritten strings only when they differ
    // from the current Composer contents, keeping the ordinary path unchanged.
    if (direct_commit_key != context_->composer().GetQueryForConversion() ||
        direct_commit_value != context_->composer().GetStringForSubmission()) {
      CommitStringDirectly(direct_commit_key, direct_commit_value, command);
      return true;
    }

    CommitCompositionDirectly(command);
    return true;
  }

  ClearPendingRerankedPreeditCommitAfterConvertCancel();

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

  ClearPendingRerankedPreeditCommitAfterConvertCancel();

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

  ClearPendingRerankedPreeditCommitAfterConvertCancel();

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

  // EngineConverter's ordinary Commit/CommitPreedit paths call
  // FinishConversion and therefore learn.  Managed secure input must instead
  // commit the composer's literal submission and reset all converter state.
  // This check is session-owned rather than dependent on the keymap's password
  // behavior, so SEND_COMMAND::SUBMIT is covered as well as Enter.
  if (IsGrimodexSecureInput()) {
    ClearPendingRerankedPreeditCommitAfterConvertCancel();
    DiscardPendingDirectCommitLearning("grimodex_secure_commit");
    DiscardPendingZenzFeedback("grimodex_secure_commit");
    ClearLiveConversionState();
    ClearUndoContext();
    CommitCompositionDirectly(command);
    return true;
  }

  command->mutable_output()->set_consumed(true);

  PushUndoContext();

  if (context_->state() == ImeContext::COMPOSITION) {
    const bool mark_preedit_as_reranked =
        ShouldMarkPreeditCommitAsRerankedAfterConvertCancel();
    std::vector<std::string> segment_keys;
    if (mark_preedit_as_reranked) {
      segment_keys = pending_reranked_preedit_commit_segment_keys_;
    }
    ClearPendingRerankedPreeditCommitAfterConvertCancel();
    context_->mutable_converter()->CommitPreedit(
        context_->composer(), command->input().context(),
        mark_preedit_as_reranked, segment_keys);
  } else {  // ImeContext::CONVERSION
    ClearPendingRerankedPreeditCommitAfterConvertCancel();
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

  if (IsGrimodexSecureInput()) {
    return CommitInternal(command, /*trigger_zero_query_suggest=*/false);
  }

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

bool IsAsciiUpperAlpha(const char c) {
  return 'A' <= c && c <= 'Z';
}

bool IsAsciiLowerAlpha(const char c) {
  return 'a' <= c && c <= 'z';
}

bool LooksLikeShiftedAsciiRomanizedSuggestionContext(
    const composer::Composer& composer) {
  const transliteration::TransliterationType input_mode =
      composer.GetInputMode();
  if (input_mode == transliteration::HALF_ASCII ||
      input_mode == transliteration::FULL_ASCII) {
    return false;
  }

  const std::string raw = composer.GetRawString();
  if (raw.size() < 3 || !IsAsciiUpperAlpha(raw[0]) ||
      !IsAsciiUpperAlpha(raw[1])) {
    return false;
  }

  bool has_lower_alpha = false;
  for (const char c : raw) {
    if (static_cast<unsigned char>(c) >= 0x80) {
      return false;
    }
    if (IsAsciiLowerAlpha(c)) {
      has_lower_alpha = true;
    }
  }
  if (!has_lower_alpha) {
    return false;
  }

  // Example:
  //   raw     = "AIde"
  //   preedit = "AI + Japanese text"
  //
  // This is the ambiguity caused by the shifted ASCII sequence being reverted
  // to Japanese input.  When dictionary suggest is disabled, do not surface the
  // raw ASCII rescue candidate automatically.
  return raw != composer.GetStringForPreedit();
}

bool ShouldSuppressShiftedAsciiAutoSuggestion(
    const config::Config& config,
    const composer::Composer& composer) {
  if (config.use_dictionary_suggest()) {
    return false;
  }
  return composer.is_in_shifted_ascii_revert_context() ||
         LooksLikeShiftedAsciiRomanizedSuggestionContext(composer);
}
}  // namespace

bool Session::Suggest(const commands::Input& input) {
  if (IsGrimodexSecureInput()) {
    return false;
  }

  if (SuppressSuggestion(input)) {
    return false;
  }

  if (ShouldSuppressShiftedAsciiAutoSuggestion(context_->GetConfig(),
                                               context_->composer())) {
    live_conversion_suggestion_candidate_window_.Clear();
    pending_live_conversion_suggestion_candidate_window_.Clear();
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

bool Session::AttachLiveConversionSuggestionCandidateWindow(
    const commands::Input& input, commands::Output* output) {
  DCHECK(output);

  // Output() from live conversion may contain the real CONVERSION candidate
  // window.  Only the passive SUGGESTION window built below is allowed to
  // remain on live-conversion output; otherwise the renderer would show the
  // ordinary conversion candidate list without an explicit Space/Down action.
  live_conversion_suggestion_candidate_window_.Clear();
  output->clear_candidate_window();

  if (SuppressSuggestion(input)) {
    return false;
  }

  if (ShouldSuppressShiftedAsciiAutoSuggestion(context_->GetConfig(),
                                               context_->composer())) {
    live_conversion_suggestion_candidate_window_.Clear();
    pending_live_conversion_suggestion_candidate_window_.Clear();
    return false;
  }

  // Live conversion intentionally keeps the real converter in CONVERSION state
  // so that Space/Down can enter the normal conversion candidate list with a
  // single key press.  EngineConverter::Suggest(), however, is defined only for
  // COMPOSITION/SUGGESTION states and resets converter state while generating
  // candidates.  Build the passive suggestion window on a cloned context and
  // copy only candidate_window to the live-conversion output.  The real
  // converter state, live_conversion_active_, selected segment, and Zenz state
  // are left untouched.
  ImeContext suggestion_context(*context_);
  if (suggestion_context.converter().IsActive()) {
    suggestion_context.mutable_converter()->Cancel();
  }
  suggestion_context.set_state(ImeContext::COMPOSITION);

  bool has_suggestion = false;
  if (input.has_request_suggestion() &&
      input.type() == commands::Input::SEND_KEY) {
    ConversionPreferences conversion_preferences =
        suggestion_context.converter().conversion_preferences();
    conversion_preferences.request_suggestion = input.request_suggestion();
    has_suggestion =
        suggestion_context.mutable_converter()->SuggestWithPreferences(
            suggestion_context.composer(), input.context(),
            conversion_preferences);
  } else {
    has_suggestion = suggestion_context.mutable_converter()->Suggest(
        suggestion_context.composer(), input.context());
  }

  if (!has_suggestion) {
    return false;
  }

  commands::Output suggestion_output;
  suggestion_context.mutable_converter()->PopOutput(
      suggestion_context.composer(), &suggestion_output);

  if (!suggestion_output.has_candidate_window() ||
      suggestion_output.candidate_window().candidate_size() == 0) {
    return false;
  }

  live_conversion_suggestion_candidate_window_ =
      suggestion_output.candidate_window();
  *output->mutable_candidate_window() =
      live_conversion_suggestion_candidate_window_;
  return true;
}

bool Session::AttachCachedLiveConversionSuggestionCandidateWindow(
    commands::Output* output) {
  DCHECK(output);

  output->clear_candidate_window();

  if (ShouldSuppressShiftedAsciiAutoSuggestion(context_->GetConfig(),
                                               context_->composer())) {
    live_conversion_suggestion_candidate_window_.Clear();
    pending_live_conversion_suggestion_candidate_window_.Clear();
    return false;
  }

  const commands::CandidateWindow* candidate_window = nullptr;
  if (live_conversion_suggestion_candidate_window_.has_category() &&
      live_conversion_suggestion_candidate_window_.category() ==
          commands::SUGGESTION &&
      live_conversion_suggestion_candidate_window_.candidate_size() > 0 &&
      !live_conversion_suggestion_candidate_window_.has_focused_index()) {
    candidate_window = &live_conversion_suggestion_candidate_window_;
  } else if (
      pending_live_conversion_suggestion_candidate_window_.has_category() &&
      pending_live_conversion_suggestion_candidate_window_.category() ==
          commands::SUGGESTION &&
      pending_live_conversion_suggestion_candidate_window_.candidate_size() >
          0 &&
      !pending_live_conversion_suggestion_candidate_window_
           .has_focused_index()) {
    candidate_window = &pending_live_conversion_suggestion_candidate_window_;
  }

  if (candidate_window == nullptr) {
    return false;
  }

  *output->mutable_candidate_window() = *candidate_window;
  return true;
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
  if (IsGrimodexSecureInput()) {
    return DoNothing(command);
  }

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
  if (IsGrimodexSecureInput()) {
    return DoNothing(command);
  }

  CancelPendingLiveConversion();
  command->mutable_output()->set_consumed(true);
  const std::string composition = context_->composer().GetQueryForConversion();
  const bool should_show_candidate_window_on_initial_conversion =
      context_->state() == ImeContext::COMPOSITION &&
      context_->GetConfig().show_candidate_window_on_initial_conversion() &&
      !context_->GetConfig().use_live_conversion();

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
  if (should_show_candidate_window_on_initial_conversion) {
    context_->mutable_converter()->SetCandidateListVisible(true);
  }
  Output(command);
  return true;
}

bool Session::ConvertWithoutHistory(commands::Command* command) {
  if (IsGrimodexSecureInput()) {
    return DoNothing(command);
  }

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
  ClearPendingRerankedPreeditCommitAfterConvertCancel();

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
  ClearPendingRerankedPreeditCommitAfterConvertCancel();

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

bool Session::CycleSegmentation(commands::Command* command) {
  if (!(context_->state() & (ImeContext::CONVERSION))) {
    return DoNothing(command);
  }
  if (IsGrimodexSecureInput()) {
    return DoNothing(command);
  }

  CancelPendingLiveConversion();
  ClearLiveConversionState();
  command->mutable_output()->set_consumed(true);
  context_->mutable_converter()->CycleSegmentation(context_->composer());
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

  MaybeSetPendingRerankedPreeditCommitAfterConvertCancel();
  ClearLiveConversionState();

  SetSessionState(ImeContext::COMPOSITION, context_.get());
  context_->mutable_converter()->Cancel();
  if (Suggest(command->input())) {
    Output(command);
  } else {
    OutputComposition(command);
  }
  return true;
}

bool Session::PredictAndConvertFromLiveConversion(commands::Command* command) {
  DCHECK(command);

  CancelPendingLiveConversion();
  command->mutable_output()->set_consumed(true);

  if (context_->state() != ImeContext::CONVERSION || !live_conversion_active_) {
    return PredictAndConvert(command);
  }

  // Selecting prediction candidates is an explicit user override of the
  // currently visible live conversion result.  If a zenz correction is visible,
  // remember it as rejected before removing the speculative layer.
  if (HasVisibleZenzLiveCorrection()) {
    SetPendingZenzFeedbackRejected("predict_after_zenz");
  }
  ClearZenzLiveCorrectionState();

  // Passive suggestions shown during live conversion are generated from a cloned
  // context and are not the real converter's current suggestion state.  Reset the
  // real converter before entering prediction selection so stale previous
  // suggestions from an older prefix, e.g. "ふ" before "ふる", are not reused.
  ClearLiveConversionState();
  SetSessionState(ImeContext::COMPOSITION, context_.get());
  context_->mutable_converter()->Cancel();

  if (context_->mutable_converter()->Predict(context_->composer())) {
    SetSessionState(ImeContext::CONVERSION, context_.get());
    Output(command);
  } else {
    // EngineConverter::Predict() resets its internal state on a first-prediction
    // failure.  Keep Session and EngineConverter aligned and fall back to the
    // same composition output that ordinary PredictAndConvert() uses on failure.
    OutputComposition(command);
  }
  return true;
}

bool Session::PredictAndConvert(commands::Command* command) {
  if (IsGrimodexSecureInput()) {
    return DoNothing(command);
  }

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
  ObservePendingZenzFeedbackCommittedResult(*command, "output_result");
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

bool SymbolMethodUsesMiddleDot(const config::Config& config) {
  return config.symbol_method() == config::Config::CORNER_BRACKET_MIDDLE_DOT ||
         config.symbol_method() == config::Config::SQUARE_BRACKET_MIDDLE_DOT;
}

std::string DirectCommitFallbackSuffixFromKeyEvent(
    const config::Config& config, const commands::KeyEvent& key_event) {
  if (MatchesString(key_event.key_string(),
                    {"。", "｡", "．", "、", "､", "，", "？", "！",
                     "（", "）", "「", "」", "［", "］", "・", "･"})) {
    return key_event.key_string();
  }

  if (MatchesString(key_event.key_string(), {".", "．"})) {
    return "。";
  }
  if (MatchesString(key_event.key_string(), {",", "，"})) {
    return "、";
  }
  if (key_event.key_string() == "?") {
    return "？";
  }
  if (key_event.key_string() == "!") {
    return "！";
  }
  if (key_event.key_string() == "(") {
    return "（";
  }
  if (key_event.key_string() == ")") {
    return "）";
  }
  if (MatchesString(key_event.key_string(), {"[", "［"})) {
    return "「";
  }
  if (MatchesString(key_event.key_string(), {"]", "］"})) {
    return "」";
  }
  if (SymbolMethodUsesMiddleDot(config) &&
      MatchesString(key_event.key_string(), {"/", "／"})) {
    return "・";
  }

  switch (key_event.key_code()) {
    case static_cast<uint32_t>('.'):
      return "。";
    case static_cast<uint32_t>(','):
      return "、";
    case static_cast<uint32_t>('?'):
      return "？";
    case static_cast<uint32_t>('!'):
      return "！";
    case static_cast<uint32_t>('('):
      return "（";
    case static_cast<uint32_t>(')'):
      return "）";
    case static_cast<uint32_t>('['):
      return "「";
    case static_cast<uint32_t>(']'):
      return "」";
    case static_cast<uint32_t>('/'):
      if (SymbolMethodUsesMiddleDot(config)) {
        return "・";
      }
      break;
    default:
      break;
  }

  return "";
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
           config::Config::DIRECT_COMMIT_CLOSE_BRACKET)) ||
         ((MatchesString(key_event.key_string(), {"・", "･"}) ||
           (SymbolMethodUsesMiddleDot(config) &&
            MatchesKeyEvent(key_event, static_cast<uint32_t>('/'),
                            {"/", "／"}))) &&
          (config.direct_commit_key() &
           config::Config::DIRECT_COMMIT_MIDDLE_DOT));
}

bool IsValidDirectCommitChar(const config::Config& config,
                             absl::string_view last_char) {
  return
      (MatchesString(last_char, {".", "．", "。", "｡"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_KUTEN)) ||

      (MatchesString(last_char, {",", "，", "、", "､"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_TOUTEN)) ||

      (MatchesString(last_char, {"?", "？"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_QUESTION_MARK)) ||

      (MatchesString(last_char, {"!", "！"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_EXCLAMATION_MARK)) ||

      (MatchesString(last_char, {"(", "（"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_OPEN_PARENTHESIS)) ||

      (MatchesString(last_char, {")", "）"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_CLOSE_PARENTHESIS)) ||

      (MatchesString(last_char, {"[", "［", "「"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_OPEN_BRACKET)) ||

      (MatchesString(last_char, {"]", "］", "」"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_CLOSE_BRACKET)) ||

      (MatchesString(last_char, {"・", "･"}) &&
       (config.direct_commit_key() &
        config::Config::DIRECT_COMMIT_MIDDLE_DOT));
}

}  // namespace

std::pair<std::string, std::string>
Session::GetDirectCommitStringsWithDirectCommitSuffixFallback(
    const composer::Composer& composer_before_insert,
    const commands::KeyEvent& key) const {
  const std::string key_before_insert =
      composer_before_insert.GetQueryForConversion();
  const std::string value_before_insert =
      composer_before_insert.GetStringForSubmission();

  std::string key_to_commit = context_->composer().GetQueryForConversion();
  std::string value_to_commit = context_->composer().GetStringForSubmission();

  const std::string suffix = DirectCommitFallbackSuffixFromKeyEvent(
      context_->GetConfig(), key);

  // Some test and client paths provide the direct-commit trigger as the key
  // event itself while leaving the restored preedit unchanged in Composer.
  // In that case, explicitly append the trigger suffix so that the visible
  // commit remains `きょう。`, while the strong learning target remains the
  // suffix-free `きょう` captured before insertion.
  if (value_to_commit == value_before_insert) {
    if (!suffix.empty()) {
      key_to_commit = absl::StrCat(key_before_insert, suffix);
      value_to_commit = absl::StrCat(value_before_insert, suffix);
    }
  }

  return {key_to_commit, value_to_commit};
}

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

  return IsValidDirectCommitChar(config, last_char);
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
  const commands::Context::InputFieldType input_field_type =
      command->input().context().input_field_type();
  context_->mutable_composer()->SetInputFieldType(input_field_type);
  context_->mutable_converter()->SetProjectDictionarySecureInput(
      input_field_type == commands::Context::PASSWORD);
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
