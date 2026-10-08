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

#include "gui/config_dialog/keybinding_editor.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QString>
#include <QTableWidget>
#include <memory>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "base/container/flat_map.h"
#include "gui/base/util.h"

#if defined(__ANDROID__) || defined(__wasm__)
#error "This platform is not supported."
#endif  // __ANDROID__ || __wasm__

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <imm.h>
#include <ime.h>
// clang-format on
#endif  // _WIN32

namespace mozc {
namespace gui {

namespace {
// LINT.IfChange
constexpr auto kQtKeyModifierNonRequiredTable =
    CreateFlatMap<int, absl::string_view>({
        {Qt::Key_Escape, "Escape"},
        {Qt::Key_Tab, "Tab"},
        {Qt::Key_Backtab, "Tab"},  // Qt handles Tab + Shift as a special key
        {Qt::Key_Backspace, "Backspace"},
        {Qt::Key_Return, "Enter"},
        {Qt::Key_Enter, "Enter"},
        {Qt::Key_Insert, "Insert"},
        {Qt::Key_Delete, "Delete"},
        {Qt::Key_Home, "Home"},
        {Qt::Key_End, "End"},
        {Qt::Key_Left, "Left"},
        {Qt::Key_Up, "Up"},
        {Qt::Key_Right, "Right"},
        {Qt::Key_Down, "Down"},
        {Qt::Key_PageUp, "PageUp"},
        {Qt::Key_PageDown, "PageDown"},
        {Qt::Key_Space, "Space"},
        {Qt::Key_F1, "F1"},
        {Qt::Key_F2, "F2"},
        {Qt::Key_F3, "F3"},
        {Qt::Key_F4, "F4"},
        {Qt::Key_F5, "F5"},
        {Qt::Key_F6, "F6"},
        {Qt::Key_F7, "F7"},
        {Qt::Key_F8, "F8"},
        {Qt::Key_F9, "F9"},
        {Qt::Key_F10, "F10"},
        {Qt::Key_F11, "F11"},
        {Qt::Key_F12, "F12"},
        {Qt::Key_F13, "F13"},
        {Qt::Key_F14, "F14"},
        {Qt::Key_F15, "F15"},
        {Qt::Key_F16, "F16"},
        {Qt::Key_F17, "F17"},
        {Qt::Key_F18, "F18"},
        {Qt::Key_F19, "F19"},
        {Qt::Key_F20, "F20"},
        {Qt::Key_F21, "F21"},
        {Qt::Key_F22, "F22"},
        {Qt::Key_F23, "F23"},
        {Qt::Key_F24, "F24"},

        {Qt::Key_Muhenkan, "Muhenkan"},
        {Qt::Key_Henkan, "Henkan"},
        {Qt::Key_Hiragana, "Hiragana"},
        {Qt::Key_Katakana, "Katakana"},
        // We need special hack for Hiragana_Katakana key. For the detail,
        // please see KeyBindingFilter::AddKey implementation.
        {Qt::Key_Hiragana_Katakana, "Hiragana"},
        {Qt::Key_Eisu_toggle, "Eisu"},
        {Qt::Key_Zenkaku_Hankaku, "Hankaku/Zenkaku"},
#ifdef __linux__
        // On Linux (X / Wayland), Hangul and Hanja are identical with
        // ImeOn and ImeOff.
        // https://github.com/google/mozc/issues/552
        //
        // Hangul == Lang1 (USB HID) / ImeOn (Windows) / Kana (macOS)
        {Qt::Key_Hangul, "ON"},
        // Hanja == Lang2 (USB HID) / ImeOff (Windows) / Eisu (macOS)
        {Qt::Key_Hangul_Hanja, "OFF"},
#endif  // __linux__
    });
// LINT.ThenChange(//composer/key_parser_test.cc)

#ifdef _WIN32
constexpr auto kWinVirtualKeyModifierNonRequiredTable = CreateFlatMap<
    DWORD, absl::string_view>({
    //  { VK_DBE_HIRAGANA, "Kana" },       // Kana
    // "Hiragana" and "Kana" are the same key on Mozc
    {VK_DBE_HIRAGANA, "Hiragana"},  // Hiragana
    {VK_DBE_KATAKANA, "Katakana"},  // Ktakana
    {VK_DBE_ALPHANUMERIC, "Eisu"},  // Eisu
    // TODO(taku): better to support Romaji key
    // { VK_DBE_ROMAN, "Romaji" },           // Romaji
    // { VK_DBE_NOROMAN, "Romaji" },         // Romaji
    {VK_NONCONVERT, "Muhenkan"},  // Muhenkan
    {VK_CONVERT, "Henkan"},       // Henkan
    // JP109's Hankaku/Zenkaku key has two V_KEY for toggling IME-On and Off.
    // Althogh these are visible keys on 109JP, Mozc doesn't support them.
    {VK_DBE_SBCSCHAR, "Hankaku/Zenkaku"},  // Zenkaku/hankaku
    {VK_DBE_DBCSCHAR, "Hankaku/Zenkaku"},  // Zenkaku/hankaku
    // { VK_KANJI, "Kanji" },  // Do not support Kanji

    // VK_IME_ON and VK_IME_OFF
    // https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/keyboard-japan-ime
    // Those variables may not be declared yet in sone build environments.
    {0x16, "ON"},   // 0x16 = VK_IME_ON
    {0x1A, "OFF"},  // 0x1A = VK_IME_OFF
});
#endif  // _WIN32

#ifdef _WIN32
constexpr quint32 kLeftShiftScanCode = 0x2A;
constexpr quint32 kRightShiftScanCode = 0x36;

QString GetCtrlKeyName(const QKeyEvent &key_event) {
  const DWORD virtual_key = key_event.nativeVirtualKey();
  if (virtual_key == VK_LCONTROL) {
    return QStringLiteral("LeftCtrl");
  }
  if (virtual_key == VK_RCONTROL) {
    return QStringLiteral("RightCtrl");
  }

  const quint32 scan_code = key_event.nativeScanCode();

  // Left Ctrl:  0x1D
  // Right Ctrl: 0xE01D on Windows extended scan code paths.
  if ((scan_code & 0xFF) == 0x1D) {
    if ((scan_code & 0xFF00) == 0xE000) {
      return QStringLiteral("RightCtrl");
    }
    return QStringLiteral("LeftCtrl");
  }

  return QString();
}

QString GetShiftKeyName(const QKeyEvent &key_event) {
  const quint32 scan_code = key_event.nativeScanCode() & 0xFF;
  switch (scan_code) {
    case kLeftShiftScanCode:
      return QStringLiteral("LeftShift");
    case kRightShiftScanCode:
      return QStringLiteral("RightShift");
    default:
      break;
  }

  const DWORD virtual_key = key_event.nativeVirtualKey();
  if (virtual_key == VK_LSHIFT) {
    return QStringLiteral("LeftShift");
  }
  if (virtual_key == VK_RSHIFT) {
    return QStringLiteral("RightShift");
  }
  return QString();
}
#endif  // _WIN32

// On Windows Hiragana/Eisu keys only emits KEY_DOWN event.
// for these keys we don't handle auto-key repeat.
bool IsDownOnlyKey(const QKeyEvent &key_event) {
#ifdef _WIN32
  const DWORD virtual_key = key_event.nativeVirtualKey();
  return (virtual_key == VK_DBE_ALPHANUMERIC ||
          virtual_key == VK_DBE_HIRAGANA || virtual_key == VK_DBE_KATAKANA);
#else   // _WIN32
  return false;
#endif  // _WIN32
}

bool IsAlphabet(const char key) { return (key >= 'a' && key <= 'z'); }

#ifdef _WIN32
constexpr int kBothSpecificSidesIndex = -2;
constexpr int kEitherSideIndex = 0;
constexpr int kLeftOnlyIndex = 1;
constexpr int kRightOnlyIndex = 2;

int ModifierSideIndexFromBinding(const QStringList &tokens,
                                 const QString &generic_token,
                                 const QString &left_token,
                                 const QString &right_token) {
  bool has_generic = false;
  bool has_left = false;
  bool has_right = false;
  for (const QString &token : tokens) {
    has_generic |= token == generic_token;
    has_left |= token == left_token;
    has_right |= token == right_token;
  }

  // An externally imported binding can explicitly require both physical sides
  // of one modifier family.  The editor intentionally exposes only
  // Either/Left/Right, so report this separately and preserve it as-is.
  if (has_left && has_right) {
    return kBothSpecificSidesIndex;
  }
  if (has_left) {
    return kLeftOnlyIndex;
  }
  if (has_right) {
    return kRightOnlyIndex;
  }
  if (has_generic) {
    return kEitherSideIndex;
  }
  return -1;
}

QString ModifierTokenForSideIndex(const QString &generic_token,
                                  const QString &left_token,
                                  const QString &right_token, int index) {
  switch (index) {
    case kLeftOnlyIndex:
      return left_token;
    case kRightOnlyIndex:
      return right_token;
    default:
      return generic_token;
  }
}

void RewriteModifierSide(QStringList *tokens, const QString &generic_token,
                         const QString &left_token, const QString &right_token,
                         int index) {
  CHECK(tokens);

  int insert_index = -1;
  for (int i = tokens->size() - 1; i >= 0; --i) {
    const QString &token = tokens->at(i);
    if (token == generic_token || token == left_token || token == right_token) {
      insert_index = i;
      tokens->removeAt(i);
    }
  }

  if (insert_index < 0) {
    return;
  }

  if (insert_index > tokens->size()) {
    insert_index = tokens->size();
  }
  tokens->insert(insert_index, ModifierTokenForSideIndex(
                                   generic_token, left_token, right_token,
                                   index));
}
#endif  // _WIN32
}  // namespace

namespace key_binding_editor_internal {

KeyBindingFilter::KeyBindingFilter(QLineEdit *line_edit, QPushButton *ok_button)
    : committed_(false),
      ctrl_pressed_(false),
      alt_pressed_(false),
      shift_pressed_(false),
      ctrl_key_name_(),
      shift_key_name_(),
      line_edit_(line_edit),
      ok_button_(ok_button) {
  Reset();
}

void KeyBindingFilter::Reset() {
  ctrl_pressed_ = false;
  alt_pressed_ = false;
  shift_pressed_ = false;
  shift_key_name_.clear();
  ctrl_key_name_.clear();
  modifier_required_key_.clear();
  modifier_non_required_key_.clear();
  unknown_key_.clear();
  committed_ = true;
  ok_button_->setEnabled(false);
}

KeyBindingFilter::KeyState KeyBindingFilter::Encode(QString *result) const {
  CHECK(result);

  // We don't accept any modifier keys for Hiragana, Eisu, Hankaku/Zenkaku keys.
  // On Windows, KEY_UP event is not raised for Hiragana/Eisu keys
  // until alternative keys (e.g., Eisu for Hiragana and Hiragana for Eisu)
  // are pressed. If Hiragana/Eisu key is pressed, we assume that
  // the key is already released at the same time.
  // Hankaku/Zenkaku key is preserved key and modifier keys are ignored.
  if (modifier_non_required_key_ == QLatin1String("Hiragana") ||
      modifier_non_required_key_ == QLatin1String("Katakana") ||
      modifier_non_required_key_ == QLatin1String("Eisu") ||
      modifier_non_required_key_ == QLatin1String("Hankaku/Zenkaku")) {
    *result = modifier_non_required_key_;
    return KeyBindingFilter::SUBMIT_KEY;
  }

  QStringList results;

  if (ctrl_pressed_) {
    if (!ctrl_key_name_.isEmpty()) {
      results << ctrl_key_name_;
    } else {
      results << QStringLiteral("Ctrl");
    }
  }

  if (shift_pressed_) {
    if (!shift_key_name_.isEmpty()) {
      results << shift_key_name_;
    } else {
      results << QStringLiteral("Shift");
    }
  }

  if (alt_pressed_) {
#ifdef __APPLE__
    results << QStringLiteral("Option");
#else   // __APPLE__
    // Do not support and show keybindings with alt for Windows
    // results << "Alt";
#endif  // __APPLE__
  }

  const bool has_modifier = !results.isEmpty();

  if (!modifier_non_required_key_.isEmpty()) {
    results << modifier_non_required_key_;
  }

  if (!modifier_required_key_.isEmpty()) {
    results << modifier_required_key_;
  }

  // in release binary, unknown_key_ is hidden
#ifndef NDEBUG
  if (!unknown_key_.isEmpty()) {
    results << unknown_key_;
  }
#endif  // NDEBUG

  KeyBindingFilter::KeyState result_state = KeyBindingFilter::ACCEPT_KEY;

  if (!unknown_key_.isEmpty()) {
    result_state = KeyBindingFilter::DENY_KEY;
  }

  const char key = modifier_required_key_.isEmpty()
                       ? 0
                       : modifier_required_key_[0].toLatin1();

  // Alt or Ctrl or these combinations
  const bool has_non_modifier_key =
      !modifier_non_required_key_.isEmpty() ||
      !modifier_required_key_.isEmpty();

#ifdef _WIN32
  // Modifier-only Ctrl / Shift chords are valid Windows key bindings.  They
  // are required so a captured side-specific chord such as
  // "LeftCtrl LeftShift" can be generalized to "LeftCtrl Shift" in the UI.
  // Alt remains unsupported by the Windows keybinding editor.
  if (!has_non_modifier_key && alt_pressed_) {
    result_state = KeyBindingFilter::DENY_KEY;
  }
#else   // _WIN32
  const bool is_explicit_left_or_right_ctrl =
      (ctrl_key_name_ == QLatin1String("LeftCtrl") ||
       ctrl_key_name_ == QLatin1String("RightCtrl"));

  const bool ctrl_only =
      ctrl_pressed_ && !alt_pressed_ && !shift_pressed_ &&
      !has_non_modifier_key;

  // Keep rejecting Alt-only, Ctrl+Shift-only, Ctrl+Alt-only, etc.
  // Allow only explicit LeftCtrl/RightCtrl single-key bindings.
  if (!has_non_modifier_key && (alt_pressed_ || ctrl_pressed_)) {
    if (!(ctrl_only && is_explicit_left_or_right_ctrl)) {
      result_state = KeyBindingFilter::DENY_KEY;
    }
  }
#endif  // _WIN32

  // TODO(taku) Shift + 3 ("#" on US-keyboard) is also valid
  // keys, but we disable it for now, since we have no way
  // to get the original key "3" from "#" only with Qt layer.
  // need to see platform dependent scan code here.

#ifndef _WIN32
  const bool shift_only = shift_pressed_ && !ctrl_pressed_ && !alt_pressed_ &&
                          modifier_required_key_.isEmpty() &&
                          modifier_non_required_key_.isEmpty();

  const bool is_explicit_left_or_right_shift =
      (shift_key_name_ == QLatin1String("LeftShift") ||
       shift_key_name_ == QLatin1String("RightShift"));

  // Keep rejecting generic "Shift" only, but allow explicit
  // "LeftShift" / "RightShift" only.
  if (shift_only && !is_explicit_left_or_right_shift) {
    result_state = KeyBindingFilter::DENY_KEY;
  }
#endif  // !_WIN32

  // Don't support Shift + 'a' only
  if (shift_pressed_ && !ctrl_pressed_ && !alt_pressed_ &&
      !modifier_required_key_.isEmpty() && IsAlphabet(key)) {
    result_state = KeyBindingFilter::DENY_KEY;
  }

  // Don't support Shift + Ctrl + '@'
  if (shift_pressed_ && !modifier_required_key_.isEmpty() && !IsAlphabet(key)) {
    result_state = KeyBindingFilter::DENY_KEY;
  }

  // no modifier for modifier_required_key
  if (!has_modifier && !modifier_required_key_.isEmpty()) {
    result_state = KeyBindingFilter::DENY_KEY;
  }

  // modifier_required_key and modifier_non_required_key
  // cannot co-exist
  if (!modifier_required_key_.isEmpty() &&
      !modifier_non_required_key_.isEmpty()) {
    result_state = KeyBindingFilter::DENY_KEY;
  }

  // no valid key
  if (results.empty()) {
    result_state = KeyBindingFilter::DENY_KEY;
  }

  *result = results.join(" ");

  return result_state;
}

KeyBindingFilter::KeyState KeyBindingFilter::AddKey(const QKeyEvent &key_event,
                                                    QString *result) {
  CHECK(result);
  result->clear();

  const int qt_key = key_event.key();

#ifdef _WIN32
  const DWORD virtual_key = key_event.nativeVirtualKey();
#endif  // _WIN32

  // modifier keys
  switch (qt_key) {
#ifdef __APPLE__
    case Qt::Key_Meta:
      ctrl_pressed_ = true;
      return Encode(result);
    case Qt::Key_Alt:  // Option key
                       //    case Qt::Key_Control:  Command key
      alt_pressed_ = true;
      return Encode(result);
#else   // __APPLE__
    case Qt::Key_Control:
      ctrl_pressed_ = true;
#ifdef _WIN32
    ctrl_key_name_ = GetCtrlKeyName(key_event);
#else   // _WIN32
    ctrl_key_name_.clear();
#endif  // _WIN32
      return Encode(result);
      //    case Qt::Key_Meta:  // Windows key
    case Qt::Key_Alt:
      alt_pressed_ = true;
      return Encode(result);
#endif  // __APPLE__
    case Qt::Key_Shift:
      shift_pressed_ = true;
#ifdef _WIN32
      shift_key_name_ = GetShiftKeyName(key_event);
#else   // _WIN32
      shift_key_name_.clear();
#endif  // _WIN32
      return Encode(result);
    default:
      break;
  }

  // non-printable command, which doesn't require modifier keys
  if (const absl::string_view *key =
          kQtKeyModifierNonRequiredTable.FindOrNull(qt_key);
      key != nullptr) {
    modifier_non_required_key_ = QLatin1String(key->data(), key->size());
    return Encode(result);
  }

#ifdef _WIN32
  // Handle JP109's Muhenkan/Henkan/katakana-hiragana and Zenkaku/Hankaku
  if (const absl::string_view *key =
          kWinVirtualKeyModifierNonRequiredTable.FindOrNull(virtual_key);
      key != nullptr) {
    modifier_non_required_key_ = QLatin1String(key->data(), key->size());
    return Encode(result);
  }

#elif __linux__
  // The XKB defines three types of logical key code: "xkb::Hiragana",
  // "xkb::Katakana" and "xkb::Hiragana_Katakana".
  // On most of Linux distributions, any key event against physical
  // "ひらがな/カタカナ" key is likely to be mapped into
  // "xkb::Hiragana_Katakana" regardless of the state of shift modifier. This
  // means that you are likely to receive "Shift + xkb::Hiragana_Katakana"
  // rather than "xkb::Katakana" when you physically press Shift +
  // "ひらがな/カタカナ".
  // On the other hand, Mozc protocol expects that Shift + "ひらがな/カタカナ"
  // key event is always interpret as "{special_key: KeyEvent::KATAKANA}"
  // without shift modifier. This is why we have the following special treatment
  // against "shift + XK_Hiragana_Katakana". See b/6087341 for the background
  // information.
  // We use |key_event.modifiers()| instead of |shift_pressed_| because
  // |shift_pressed_| is no longer valid in the following scenario.
  //   1. Press "Shift"
  //   2. Press "Hiragana/Katakana"  (shift_pressed_ == true)
  //   3. Press "Hiragana/Katakana"  (shift_pressed_ == false)
  const bool with_shift = (key_event.modifiers() & Qt::ShiftModifier) != 0;
  if (with_shift && (qt_key == Qt::Key_Hiragana_Katakana)) {
    modifier_non_required_key_ = QLatin1String("Katakana");
    return Encode(result);
  }
#endif  // _WIN32, __linux__

  if (qt_key == Qt::Key_yen) {
    // Japanese Yen mark, treat it as backslash for compatibility
    modifier_non_required_key_ = QLatin1String("\\");
    return Encode(result);
  }

  // printable command, which requires modifier keys
  if ((qt_key >= 0x21 && qt_key <= 0x60) ||
      (qt_key >= 0x7B && qt_key <= 0x7E)) {
    const char key_char = static_cast<char>(qt_key);
    if (qt_key >= 0x41 && qt_key <= 0x5A) {
      modifier_required_key_ = QLatin1Char(key_char - 'A' + 'a');
    } else {
      modifier_required_key_ = QLatin1Char(key_char);
    }
    return Encode(result);
  }

  unknown_key_.asprintf("<UNK:0x%x 0x%x 0x%x>", key_event.key(),
                        key_event.nativeScanCode(),
                        key_event.nativeVirtualKey());

  return Encode(result);
}

bool KeyBindingFilter::eventFilter(QObject *obj, QEvent *event) {
  if ((event->type() == QEvent::KeyPress ||
       event->type() == QEvent::KeyRelease) &&
      !IsDownOnlyKey(*static_cast<QKeyEvent *>(event)) &&
      static_cast<QKeyEvent *>(event)->isAutoRepeat()) {
    // ignores auto key repeat. just eat the event
    return true;
  }

  // TODO(taku): the following sequence doesn't work as once
  // user release any of keys, the statues goes to "submitted"
  // 1. Press Ctrl + a
  // 2. Release a, but keep pressing Ctrl
  // 3. Press b  (the result should be "Ctrl + b").

  if (event->type() == QEvent::KeyPress) {
    // when the state is committed, reset the internal key binding
    if (committed_) {
      Reset();
      line_edit_->clear();
    }
    committed_ = false;
    const QKeyEvent *key_event = static_cast<QKeyEvent *>(event);
    QString result;
    const KeyBindingFilter::KeyState state = AddKey(*key_event, &result);
    ok_button_->setEnabled(state != KeyBindingFilter::DENY_KEY);
    line_edit_->setText(result);
    line_edit_->setCursorPosition(0);
    line_edit_->setAttribute(Qt::WA_InputMethodEnabled, false);
    if (state == KeyBindingFilter::SUBMIT_KEY) {
      committed_ = true;
    }
    return true;
  } else if (event->type() == QEvent::KeyRelease) {
    // when any of key is released, change the state "committed";
    line_edit_->setCursorPosition(0);
    committed_ = true;
    return true;
  }

  return QObject::eventFilter(obj, event);
}

}  // namespace key_binding_editor_internal

KeyBindingEditor::KeyBindingEditor(QWidget *parent, QWidget *trigger_parent)
    : QDialog(parent), trigger_parent_(trigger_parent) {
  setupUi(this);
#if defined(__linux__)
  // Workaround for the issue https://github.com/google/mozc/issues/9
  // Seems that even after clicking the button for the keybinding dialog,
  // the edit is not raised. This might be a bug of setFocusProxy.
  setWindowFlags(Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint |
                 Qt::Tool | Qt::WindowStaysOnTopHint);
#else   // __linux__
  setWindowFlags(Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint |
                 Qt::Tool);
#endif  // __linux__

  QPushButton *ok_button =
      KeyBindingEditorbuttonBox->button(QDialogButtonBox::Ok);
  CHECK(ok_button != nullptr);

  filter_ = std::make_unique<key_binding_editor_internal::KeyBindingFilter>(
      KeyBindingLineEdit, ok_button);
  KeyBindingLineEdit->installEventFilter(filter_.get());

  // no right click
  KeyBindingLineEdit->setContextMenuPolicy(Qt::NoContextMenu);
  KeyBindingLineEdit->setMaxLength(32);
  KeyBindingLineEdit->setAttribute(Qt::WA_InputMethodEnabled, false);

#ifdef _WIN32
  ::ImmAssociateContext(reinterpret_cast<HWND>(KeyBindingLineEdit->winId()), 0);

  // Replace the spacer under the key capture field with per-modifier
  // left/right matching controls.  Capturing a physical key keeps the current
  // side-specific behavior by default; users can independently generalize
  // Ctrl and Shift to either side.
  gridLayout->removeItem(verticalSpacer);
  delete verticalSpacer;
  verticalSpacer = nullptr;

  auto *modifier_side_layout = new QHBoxLayout();
  modifier_side_layout->setContentsMargins(0, 0, 0, 0);

  auto *ctrl_label = new QLabel(QStringLiteral("Ctrl:"), this);
  ctrl_side_combo_ = new QComboBox(this);
  ctrl_side_combo_->setObjectName(QStringLiteral("CtrlSideComboBox"));

  auto *shift_label = new QLabel(QStringLiteral("Shift:"), this);
  shift_side_combo_ = new QComboBox(this);
  shift_side_combo_->setObjectName(QStringLiteral("ShiftSideComboBox"));

  for (QComboBox *combo : {ctrl_side_combo_, shift_side_combo_}) {
    // These controls are created in C++, while the existing keybinding-editor
    // strings in the translation catalog use the .ui context
    // "KeyBindingEditor".  Use that context explicitly so Japanese builds do
    // not fall back to the English source text.
    combo->addItem(
        QCoreApplication::translate("KeyBindingEditor", "Either side"));
    combo->addItem(
        QCoreApplication::translate("KeyBindingEditor", "Left only"));
    combo->addItem(
        QCoreApplication::translate("KeyBindingEditor", "Right only"));
    combo->setEnabled(false);
  }

  modifier_side_layout->addWidget(ctrl_label);
  modifier_side_layout->addWidget(ctrl_side_combo_);
  modifier_side_layout->addSpacing(8);
  modifier_side_layout->addWidget(shift_label);
  modifier_side_layout->addWidget(shift_side_combo_);
  modifier_side_layout->addStretch(1);
  gridLayout->addLayout(modifier_side_layout, 2, 0, 1, 3);

  setMinimumSize(QSize(430, 140));
  setMaximumSize(QSize(430, 140));
  resize(430, 140);

  QObject::connect(
      ctrl_side_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
      [this](int) { ApplyModifierSideControlsToBinding(); });
  QObject::connect(
      shift_side_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
      [this](int) { ApplyModifierSideControlsToBinding(); });
  QObject::connect(
      KeyBindingLineEdit, &QLineEdit::textChanged, this,
      [this](const QString &) { SyncModifierSideControlsFromBinding(); });
#endif  // _WIN32

  QObject::connect(KeyBindingEditorbuttonBox,
                   SIGNAL(clicked(QAbstractButton *)), this,
                   SLOT(Clicked(QAbstractButton *)));

  GuiUtil::ReplaceWidgetLabels(this);

  setFocusProxy(KeyBindingLineEdit);
}

void KeyBindingEditor::Clicked(QAbstractButton *button) {
  switch (KeyBindingEditorbuttonBox->buttonRole(button)) {
    case QDialogButtonBox::AcceptRole:
      QDialog::accept();
      break;
    default:
      QDialog::reject();
      break;
  }
}

QString KeyBindingEditor::GetBinding() const {
  return KeyBindingLineEdit->text();
}

void KeyBindingEditor::SyncModifierSideControlsFromBinding() {
#ifdef _WIN32
  if (updating_modifier_side_controls_ || ctrl_side_combo_ == nullptr ||
      shift_side_combo_ == nullptr) {
    return;
  }

  const QStringList tokens =
      KeyBindingLineEdit->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);

  const auto sync_combo = [&tokens](QComboBox *combo,
                                    const QString &generic_token,
                                    const QString &left_token,
                                    const QString &right_token) {
    const int index = ModifierSideIndexFromBinding(
        tokens, generic_token, left_token, right_token);
    const QSignalBlocker blocker(combo);

    // Do not silently rewrite imported bindings such as
    // "LeftCtrl RightCtrl ...".  The three-state selector cannot represent a
    // both-sides-required condition, so disable only that family and leave its
    // original tokens untouched.  The other modifier family remains editable.
    if (index == kBothSpecificSidesIndex) {
      combo->setEnabled(false);
      combo->setCurrentIndex(-1);
      return;
    }

    combo->setEnabled(index >= 0);
    combo->setCurrentIndex(index >= 0 ? index : kEitherSideIndex);
  };

  sync_combo(ctrl_side_combo_, QStringLiteral("Ctrl"),
             QStringLiteral("LeftCtrl"), QStringLiteral("RightCtrl"));
  sync_combo(shift_side_combo_, QStringLiteral("Shift"),
             QStringLiteral("LeftShift"), QStringLiteral("RightShift"));
#endif  // _WIN32
}

void KeyBindingEditor::ApplyModifierSideControlsToBinding() {
#ifdef _WIN32
  if (updating_modifier_side_controls_ || ctrl_side_combo_ == nullptr ||
      shift_side_combo_ == nullptr) {
    return;
  }

  updating_modifier_side_controls_ = true;

  QStringList tokens =
      KeyBindingLineEdit->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);

  if (ctrl_side_combo_->isEnabled()) {
    RewriteModifierSide(&tokens, QStringLiteral("Ctrl"),
                        QStringLiteral("LeftCtrl"),
                        QStringLiteral("RightCtrl"),
                        ctrl_side_combo_->currentIndex());
  }
  if (shift_side_combo_->isEnabled()) {
    RewriteModifierSide(&tokens, QStringLiteral("Shift"),
                        QStringLiteral("LeftShift"),
                        QStringLiteral("RightShift"),
                        shift_side_combo_->currentIndex());
  }

  const QString binding = tokens.join(QLatin1Char(' '));
  KeyBindingLineEdit->setText(binding);
  KeyBindingLineEdit->setCursorPosition(0);

  // Side generalization does not change whether the captured key shape is
  // otherwise valid.  Preserve the filter's current OK-button state instead
  // of accidentally making rejected combinations valid.
  updating_modifier_side_controls_ = false;
#endif  // _WIN32
}

void KeyBindingEditor::SetBinding(const QString &binding) {
  KeyBindingLineEdit->setText(binding);
  KeyBindingLineEdit->setCursorPosition(0);
#ifdef _WIN32
  SyncModifierSideControlsFromBinding();

  if (QPushButton *ok_button =
          KeyBindingEditorbuttonBox->button(QDialogButtonBox::Ok);
      ok_button != nullptr) {
    ok_button->setEnabled(!binding.trimmed().isEmpty());
  }
#endif  // _WIN32
}
}  // namespace gui
}  // namespace mozc
