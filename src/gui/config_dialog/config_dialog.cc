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

// Qt component of configure dialog for Mozc
#include "gui/config_dialog/config_dialog.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdint>
#include <istream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <msctf.h>
#include <objbase.h>
#endif  // _WIN32

#include <QColor>
#include <QColorDialog>
#include <QScrollArea>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "base/config_file_stream.h"
#include "client/client.h"
#include "config/config_handler.h"
#include "gui/base/util.h"
#include "gui/config_dialog/keymap_editor.h"
#include "gui/config_dialog/roman_table_editor.h"
#include "protocol/config.pb.h"
#include "session/keymap.h"
#include "session/zenz_feedback_store.h"

#if defined(__ANDROID__) || defined(__wasm__)
#error "This platform is not supported."
#endif  // __ANDROID__ || __wasm__

#ifdef _WIN32
// clang-format off
#include <shellapi.h>
#include <windows.h>
#include <QGuiApplication>
// clang-format on

#include "base/run_level.h"
#include "gui/base/win_util.h"
#include "win32/base/imm_util.h"
#endif  // _WIN32

#ifdef __APPLE__
#include "base/mac/mac_util.h"
#endif  // __APPLE__

namespace {
template <typename T>
void Connect(const QList<T *> &objects, const char *signal,
             const QObject *receiver, const char *slot) {
  for (typename QList<T *>::const_iterator itr = objects.begin();
       itr != objects.end(); ++itr) {
    QObject::connect(*itr, signal, receiver, slot);
  }
}
}  // namespace

namespace mozc {
namespace gui {

namespace {
#ifdef _WIN32
void NotifyDisplayAttributeUpdate();
#endif  // _WIN32
}  // namespace

ConfigDialog::ConfigDialog()
    : client_(client::ClientFactory::NewClient()),
      initial_preedit_method_(0),
      initial_use_keyboard_to_change_preedit_method_(false),
      initial_use_mode_indicator_(true),
      initial_use_dark_mode_candidate_window_(false) {
  setupUi(this);

  // QScrollArea has its own viewport, and the viewport may paint a different
  // background from ordinary tab pages.  Do not paint it with a palette color
  // and do not use stylesheets here, because stylesheets can interfere with
  // native QCheckBox and QFrame line rendering.  Make only the scroll area
  // surface transparent so the underlying tab page paints the background.
  inputSupportScrollArea->setFrameShape(QFrame::NoFrame);

  inputSupportScrollArea->setAutoFillBackground(false);
  inputSupportScrollArea->viewport()->setAutoFillBackground(false);
  inputSupportScrollAreaWidgetContents->setAutoFillBackground(false);

  inputSupportScrollArea->setAttribute(Qt::WA_StyledBackground, false);
  inputSupportScrollArea->viewport()->setAttribute(Qt::WA_StyledBackground,
                                                   false);
  inputSupportScrollAreaWidgetContents->setAttribute(Qt::WA_StyledBackground,
                                                     false);

  inputSupportScrollArea->viewport()->setAttribute(
      Qt::WA_TranslucentBackground, true);
  inputSupportScrollAreaWidgetContents->setAttribute(
      Qt::WA_TranslucentBackground, true);

  inputSupportScrollArea->viewport()->setAttribute(Qt::WA_NoSystemBackground,
                                                   true);
  inputSupportScrollAreaWidgetContents->setAttribute(
      Qt::WA_NoSystemBackground, true);

  inputSupportScrollArea->setStyleSheet(QString());
  inputSupportScrollArea->viewport()->setStyleSheet(QString());
  inputSupportScrollAreaWidgetContents->setStyleSheet(QString());

  setWindowFlags(Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
  setWindowModality(Qt::NonModal);

#ifdef _WIN32
  miscStartupWidget->setVisible(false);
#endif  // _WIN32

#ifdef __APPLE__
  miscDefaultIMEWidget->setVisible(false);
  miscAdministrationWidget->setVisible(false);
  setWindowTitle(tr("%1 Preferences").arg(GuiUtil::ProductName()));
#endif  // __APPLE__

#if defined(__linux__)
  miscDefaultIMEWidget->setVisible(false);
  miscAdministrationWidget->setVisible(false);
  miscStartupWidget->setVisible(false);
#endif  // __linux__

#ifdef NDEBUG
  // disable logging options
  miscLoggingWidget->setVisible(false);

#if defined(__linux__)
  // The last "misc" tab has no valid configs on Linux
  constexpr int kMiscTabIndex = 6;
  configDialogTabWidget->removeTab(kMiscTabIndex);
#endif  // __linux__
#endif  // NDEBUG

  suggestionsSizeSpinBox->setRange(1, 9);

  liveConversionDelaySpinBox->setRange(0, 1000);
  liveConversionDelaySpinBox->setSingleStep(5);
  liveConversionDelaySpinBox->setSuffix(QString::fromUtf8(" ms"));
  liveConversionDelaySpinBox->setSpecialValueText(QString::fromUtf8("即時"));

  zenzLiveCorrectionDelaySpinBox->setRange(0, 5000);
  zenzLiveCorrectionDelaySpinBox->setSingleStep(100);
  zenzLiveCorrectionDelaySpinBox->setSuffix(QString::fromUtf8(" ms"));
  zenzLiveCorrectionDelaySpinBox->setSpecialValueText(QString::fromUtf8("即時"));

  zenzLiveCorrectionMinKeyLengthSpinBox->setRange(2, 20);
  zenzLiveCorrectionMinKeyLengthSpinBox->setSingleStep(1);
  zenzLiveCorrectionMinKeyLengthSpinBox->setSuffix(QString::fromUtf8(" 文字"));

  punctuationsSettingComboBox->addItem(QString::fromUtf8("、。"));
  punctuationsSettingComboBox->addItem(QString::fromUtf8("，．"));
  punctuationsSettingComboBox->addItem(QString::fromUtf8("、．"));
  punctuationsSettingComboBox->addItem(QString::fromUtf8("，。"));

  symbolsSettingComboBox->addItem(QString::fromUtf8("「」・"));
  symbolsSettingComboBox->addItem(QString::fromUtf8("[]／"));
  symbolsSettingComboBox->addItem(QString::fromUtf8("「」／"));
  symbolsSettingComboBox->addItem(QString::fromUtf8("[]・"));

  keymapSettingComboBox->addItem(tr("Custom keymap"));
  keymapSettingComboBox->addItem(tr("ATOK"));
  keymapSettingComboBox->addItem(tr("MS-IME"));
  keymapSettingComboBox->addItem(tr("Kotoeri"));

  keymapname_sessionkeymap_map_[tr("ATOK")] = config::Config::ATOK;
  keymapname_sessionkeymap_map_[tr("MS-IME")] = config::Config::MSIME;
  keymapname_sessionkeymap_map_[tr("Kotoeri")] = config::Config::KOTOERI;

  inputModeComboBox->addItem(tr("Romaji"));
  inputModeComboBox->addItem(tr("Kana"));
#ifdef _WIN32
  // These options changing the preedit method by a hot key are only
  // supported by Windows.
  inputModeComboBox->addItem(tr("Romaji (switchable)"));
  inputModeComboBox->addItem(tr("Kana (switchable)"));
#endif  // _WIN32

  spaceCharacterFormComboBox->addItem(tr("Follow input mode"));
  spaceCharacterFormComboBox->addItem(tr("Fullwidth"));
  spaceCharacterFormComboBox->addItem(tr("Halfwidth"));

  selectionShortcutModeComboBox->addItem(tr("No shortcut"));
  selectionShortcutModeComboBox->addItem(tr("1 -- 9"));
  selectionShortcutModeComboBox->addItem(tr("A -- L"));

  historyLearningLevelComboBox->addItem(tr("Yes"));
  historyLearningLevelComboBox->addItem(tr("Yes (don't record new data)"));
  historyLearningLevelComboBox->addItem(tr("No"));

  shiftKeyModeSwitchComboBox->addItem(tr("Off"));
  shiftKeyModeSwitchComboBox->addItem(tr("Alphanumeric"));
  shiftKeyModeSwitchComboBox->addItem(tr("Katakana"));

  numpadCharacterFormComboBox->addItem(tr("Follow input mode"));
  numpadCharacterFormComboBox->addItem(tr("Fullwidth"));
  numpadCharacterFormComboBox->addItem(tr("Halfwidth"));
  numpadCharacterFormComboBox->addItem(tr("Direct input"));

  verboseLevelComboBox->addItem(tr("0"));
  verboseLevelComboBox->addItem(tr("1"));
  verboseLevelComboBox->addItem(tr("2"));

  yenSignComboBox->addItem(tr("Yen Sign ¥"));
  yenSignComboBox->addItem(tr("Backslash \\"));

#ifndef __APPLE__
  // On Windows/Linux, yenSignCombBox can be hidden.
  yenSignLabel->hide();
  yenSignComboBox->hide();
  // On Windows/Linux, useJapaneseLayout checkbox should be invisible.
  useJapaneseLayout->hide();
#endif  // !__APPLE__

#ifndef _WIN32
  // Mode indicator is available only on Windows.
  useModeIndicator->hide();

  // Candidate window dark mode is available only on Windows.
  useDarkModeCandidateWindow->hide();

  // Preedit display color customization is available only on Windows TSF.
  preeditDisplayColorGroupBox->hide();
#endif  // !_WIN32

  // Reset texts explicitly for translations.
  configDialogButtonBox->button(QDialogButtonBox::Ok)->setText(tr("  Ok  "));
  configDialogButtonBox->button(QDialogButtonBox::Cancel)
      ->setText(tr("Cancel"));
  configDialogButtonBox->button(QDialogButtonBox::Apply)->setText(tr("Apply"));

  // signal/slot
  QObject::connect(configDialogButtonBox, SIGNAL(clicked(QAbstractButton *)),
                   this, SLOT(clicked(QAbstractButton *)));
  QObject::connect(clearUserHistoryButton, SIGNAL(clicked()), this,
                   SLOT(ClearUserHistory()));
  QObject::connect(clearUserPredictionButton, SIGNAL(clicked()), this,
                   SLOT(ClearUserPrediction()));
  QObject::connect(clearUnusedUserPredictionButton, SIGNAL(clicked()), this,
                   SLOT(ClearUnusedUserPrediction()));
  QObject::connect(editZenzFeedbackButton, SIGNAL(clicked()), this,
                   SLOT(EditZenzFeedback()));
  QObject::connect(editUserDictionaryButton, SIGNAL(clicked()), this,
                   SLOT(EditUserDictionary()));
  QObject::connect(editKeymapButton, SIGNAL(clicked()), this,
                   SLOT(EditKeymap()));
  QObject::connect(resetToDefaultsButton, SIGNAL(clicked()), this,
                   SLOT(ResetToDefaults()));
  QObject::connect(editRomanTableButton, SIGNAL(clicked()), this,
                   SLOT(EditRomanTable()));
  QObject::connect(inputModeComboBox, SIGNAL(currentIndexChanged(int)), this,
                   SLOT(SelectInputModeSetting(int)));
  QObject::connect(liveConversionCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectLiveConversionSetting(int)));
  QObject::connect(zenzLiveCorrectionCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectZenzLiveCorrectionSetting(int)));
  QObject::connect(useAutoConversion, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectAutoConversionSetting(int)));
  QObject::connect(useDirectCommit, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectDirectCommitSetting(int)));
  QObject::connect(historySuggestCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectSuggestionSetting(int)));
  QObject::connect(dictionarySuggestCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectSuggestionSetting(int)));
  QObject::connect(realtimeConversionCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectSuggestionSetting(int)));
  QObject::connect(launchAdministrationDialogButton, SIGNAL(clicked()), this,
                   SLOT(LaunchAdministrationDialog()));
  QObject::connect(setDefaultImeButton, SIGNAL(clicked()), this,
                   SLOT(SetMozkeyAsDefaultIme()));
  QObject::connect(restoreDefaultImeButton, SIGNAL(clicked()), this,
                   SLOT(RestorePreviousDefaultImeSetting()));

  QObject::connect(inputPreeditTextColorButton, SIGNAL(clicked()), this,
                   SLOT(SelectPreeditColor()));
  QObject::connect(inputPreeditBackgroundColorButton, SIGNAL(clicked()), this,
                   SLOT(SelectPreeditColor()));
  QObject::connect(inputPreeditUnderlineColorButton, SIGNAL(clicked()), this,
                   SLOT(SelectPreeditColor()));
  QObject::connect(targetPreeditTextColorButton, SIGNAL(clicked()), this,
                   SLOT(SelectPreeditColor()));
  QObject::connect(targetPreeditBackgroundColorButton, SIGNAL(clicked()), this,
                   SLOT(SelectPreeditColor()));
  QObject::connect(targetPreeditUnderlineColorButton, SIGNAL(clicked()), this,
                   SLOT(SelectPreeditColor()));

  QObject::connect(inputPreeditTextColorCheckBox, SIGNAL(toggled(bool)),
                   inputPreeditTextColorButton, SLOT(setEnabled(bool)));
  QObject::connect(inputPreeditBackgroundColorCheckBox, SIGNAL(toggled(bool)),
                   inputPreeditBackgroundColorButton, SLOT(setEnabled(bool)));
  QObject::connect(inputPreeditUnderlineColorCheckBox, SIGNAL(toggled(bool)),
                   inputPreeditUnderlineColorButton, SLOT(setEnabled(bool)));
  QObject::connect(targetPreeditTextColorCheckBox, SIGNAL(toggled(bool)),
                   targetPreeditTextColorButton, SLOT(setEnabled(bool)));
  QObject::connect(targetPreeditBackgroundColorCheckBox, SIGNAL(toggled(bool)),
                   targetPreeditBackgroundColorButton, SLOT(setEnabled(bool)));
  QObject::connect(targetPreeditUnderlineColorCheckBox, SIGNAL(toggled(bool)),
                   targetPreeditUnderlineColorButton, SLOT(setEnabled(bool)));

  // Event handlers to enable 'Apply' button.
  Connect(findChildren<QPushButton *>(), SIGNAL(clicked()), this,
          SLOT(EnableApplyButton()));
  Connect(findChildren<QCheckBox *>(), SIGNAL(clicked()), this,
          SLOT(EnableApplyButton()));
  Connect(findChildren<QComboBox *>(), SIGNAL(activated(int)), this,
          SLOT(EnableApplyButton()));
  Connect(findChildren<QSpinBox *>(), SIGNAL(editingFinished()), this,
          SLOT(EnableApplyButton()));
  // 'Apply' button is disabled on launching.
  configDialogButtonBox->button(QDialogButtonBox::Apply)->setEnabled(false);

  // When clicking these messages, CheckBoxs corresponding
  // to them should be toggled.
  // We cannot use connect/slot as QLabel doesn't define
  // clicked slot by default.
  incognitoModeMessage->installEventFilter(this);

#ifndef _WIN32
  defaultImeButtonsWidget->setVisible(false);
  checkDefaultLine->setVisible(false);
  checkDefaultLabel->setVisible(false);
#endif  // !_WIN32

#ifdef _WIN32
  launchAdministrationDialogButton->setEnabled(true);
  // if the current application is not elevated by UAC,
  // add a shield icon
  if (!mozc::RunLevel::IsElevatedByUAC()) {
    const QIcon &vista_shield_icon =
        QApplication::style()->standardIcon(QStyle::SP_VistaShield);
    launchAdministrationDialogButton->setIcon(vista_shield_icon);
  }

#else   // _WIN32
  launchAdministrationDialogButton->setEnabled(false);
  launchAdministrationDialogButton->setVisible(false);
  administrationLine->setVisible(false);
  administrationLabel->setVisible(false);
  dictionaryPreloadingAndUACLabel->setVisible(false);
#endif  // _WIN32

  GuiUtil::ReplaceWidgetLabels(this);

  Reload();

#ifdef _WIN32
  IMEHotKeyDisabledCheckBox->setChecked(WinUtil::GetIMEHotKeyDisabled());
#else   // _WIN32
  IMEHotKeyDisabledCheckBox->setVisible(false);
#endif  // _WIN32

}

bool ConfigDialog::SetConfig(const config::Config &config) {
  if (!client_->CheckVersionOrRestartServer()) {
    LOG(ERROR) << "CheckVersionOrRestartServer failed";
    return false;
  }

  if (!client_->SetConfig(config)) {
    LOG(ERROR) << "SetConfig failed";
    return false;
  }

  return true;
}

bool ConfigDialog::GetConfig(config::Config *config) {
  if (!client_->CheckVersionOrRestartServer()) {
    LOG(ERROR) << "CheckVersionOrRestartServer failed";
    return false;
  }

  if (!client_->GetConfig(config)) {
    LOG(ERROR) << "GetConfig failed";
    return false;
  }

  return true;
}

void ConfigDialog::Reload() {
  config::Config config;
  if (!GetConfig(&config)) {
    QMessageBox::critical(this, windowTitle(),
                          tr("Failed to get current config values."));
  }
  ConvertFromProto(config);

  SelectLiveConversionSetting(
      static_cast<int>(liveConversionCheckBox->isChecked()));
  SelectAutoConversionSetting(static_cast<int>(useAutoConversion->isChecked()));
  SelectDirectCommitSetting(static_cast<int>(useDirectCommit->isChecked()));
  initial_preedit_method_ = static_cast<int>(config.preedit_method());
  initial_use_keyboard_to_change_preedit_method_ =
      config.use_keyboard_to_change_preedit_method();
  initial_use_mode_indicator_ = config.use_mode_indicator();
  initial_use_dark_mode_candidate_window_ =
      config.use_dark_mode_candidate_window();

  initial_use_custom_preedit_text_color_ =
      config.use_custom_preedit_text_color();
  initial_preedit_text_color_ = config.preedit_text_color();

  initial_use_custom_preedit_background_color_ =
      config.use_custom_preedit_background_color();
  initial_preedit_background_color_ = config.preedit_background_color();

  initial_use_custom_preedit_underline_color_ =
      config.use_custom_preedit_underline_color();
  initial_preedit_underline_color_ = config.preedit_underline_color();

  initial_use_custom_preedit_target_text_color_ =
      config.use_custom_preedit_target_text_color();
  initial_preedit_target_text_color_ = config.preedit_target_text_color();

  initial_use_custom_preedit_target_background_color_ =
      config.use_custom_preedit_target_background_color();
  initial_preedit_target_background_color_ =
      config.preedit_target_background_color();

  initial_use_custom_preedit_target_underline_color_ =
      config.use_custom_preedit_target_underline_color();
  initial_preedit_target_underline_color_ =
      config.preedit_target_underline_color();
}

bool ConfigDialog::Update() {
  config::Config config;
  ConvertToProto(&config);

  if (config.session_keymap() == config::Config::CUSTOM &&
      config.custom_keymap_table().empty()) {
    QMessageBox::warning(this, windowTitle(),
                         tr("The current custom keymap table is empty. "
                            "When custom keymap is selected, "
                            "you must customize it."));
    return false;
  }

  const bool preedit_setting_changed =
      (initial_preedit_method_ != static_cast<int>(config.preedit_method())) ||
      (initial_use_keyboard_to_change_preedit_method_ !=
       config.use_keyboard_to_change_preedit_method());

  const bool use_mode_indicator_changed =
      (initial_use_mode_indicator_ != config.use_mode_indicator());

  const bool use_dark_mode_candidate_window_changed =
      (initial_use_dark_mode_candidate_window_ !=
       config.use_dark_mode_candidate_window());

  const bool preedit_display_color_changed =
      initial_use_custom_preedit_text_color_ !=
          config.use_custom_preedit_text_color() ||
      initial_preedit_text_color_ != config.preedit_text_color() ||
      initial_use_custom_preedit_background_color_ !=
          config.use_custom_preedit_background_color() ||
      initial_preedit_background_color_ !=
          config.preedit_background_color() ||
      initial_use_custom_preedit_underline_color_ !=
          config.use_custom_preedit_underline_color() ||
      initial_preedit_underline_color_ !=
          config.preedit_underline_color() ||
      initial_use_custom_preedit_target_text_color_ !=
          config.use_custom_preedit_target_text_color() ||
      initial_preedit_target_text_color_ !=
          config.preedit_target_text_color() ||
      initial_use_custom_preedit_target_background_color_ !=
          config.use_custom_preedit_target_background_color() ||
      initial_preedit_target_background_color_ !=
          config.preedit_target_background_color() ||
      initial_use_custom_preedit_target_underline_color_ !=
          config.use_custom_preedit_target_underline_color() ||
      initial_preedit_target_underline_color_ !=
          config.preedit_target_underline_color();

  if (!SetConfig(config)) {
    QMessageBox::critical(this, windowTitle(), tr("Failed to update config"));
    return false;
  }

#if defined(_WIN32)
  if (preedit_setting_changed) {
    QMessageBox::information(this, windowTitle(),
                             tr("Romaji/Kana setting is enabled from"
                                " new applications."));
    initial_preedit_method_ = static_cast<int>(config.preedit_method());
    initial_use_keyboard_to_change_preedit_method_ =
        config.use_keyboard_to_change_preedit_method();
  }
#endif  // _WIN32

#ifdef _WIN32
  if (use_mode_indicator_changed) {
    QMessageBox::information(this, windowTitle(),
                             tr("Input mode indicator setting is enabled from"
                                " new applications."));
    initial_use_mode_indicator_ = config.use_mode_indicator();
  }

  if (use_dark_mode_candidate_window_changed) {
    QMessageBox::information(
        this, windowTitle(),
        tr("Candidate window and ruby overlay dark mode setting is enabled from"
           " new applications."));
    initial_use_dark_mode_candidate_window_ =
        config.use_dark_mode_candidate_window();
  }

  if (preedit_display_color_changed) {
    NotifyDisplayAttributeUpdate();

    initial_use_custom_preedit_text_color_ =
        config.use_custom_preedit_text_color();
    initial_preedit_text_color_ = config.preedit_text_color();

    initial_use_custom_preedit_background_color_ =
        config.use_custom_preedit_background_color();
    initial_preedit_background_color_ = config.preedit_background_color();

    initial_use_custom_preedit_underline_color_ =
        config.use_custom_preedit_underline_color();
    initial_preedit_underline_color_ = config.preedit_underline_color();

    initial_use_custom_preedit_target_text_color_ =
        config.use_custom_preedit_target_text_color();
    initial_preedit_target_text_color_ = config.preedit_target_text_color();

    initial_use_custom_preedit_target_background_color_ =
        config.use_custom_preedit_target_background_color();
    initial_preedit_target_background_color_ =
        config.preedit_target_background_color();

    initial_use_custom_preedit_target_underline_color_ =
        config.use_custom_preedit_target_underline_color();
    initial_preedit_target_underline_color_ =
        config.preedit_target_underline_color();
  }
#endif  // _WIN32

#ifdef _WIN32
  if (!WinUtil::SetIMEHotKeyDisabled(IMEHotKeyDisabledCheckBox->isChecked())) {
    // Do not show any dialog here, since this operation will not fail
    // in almost all cases.
    // TODO(taku): better to show dialog?
    LOG(ERROR) << "Failed to update IME HotKey status";
  }
#endif  // _WIN32

#ifdef __APPLE__
  if (startupCheckBox->isChecked()) {
    if (!MacUtil::CheckPrelauncherLoginItemStatus()) {
      MacUtil::AddPrelauncherLoginItem();
    }
  } else {
    if (MacUtil::CheckPrelauncherLoginItemStatus()) {
      MacUtil::RemovePrelauncherLoginItem();
    }
  }
#endif  // __APPLE__

  return true;
}

#define SET_COMBOBOX(combobox, enumname, field)                    \
  do {                                                             \
    (combobox)->setCurrentIndex(static_cast<int>(config.field())); \
  } while (0)

#define SET_CHECKBOX(checkbox, field)       \
  do {                                      \
    (checkbox)->setChecked(config.field()); \
  } while (0)

#define GET_COMBOBOX(combobox, enumname, field)                              \
  do {                                                                       \
    config->set_##field(                                                     \
        static_cast<config::Config_##enumname>((combobox)->currentIndex())); \
  } while (0)

#define GET_CHECKBOX(checkbox, field)             \
  do {                                            \
    config->set_##field((checkbox)->isChecked()); \
  } while (0)

namespace {

static constexpr int kPreeditMethodSize = 2;

constexpr uint32_t kDefaultLiveConversionDelayMsec = 228;
constexpr uint32_t kMaxLiveConversionDelayMsec = 1000;
constexpr uint32_t kDefaultZenzLiveCorrectionDelayMsec = 1000;
constexpr uint32_t kMaxZenzLiveCorrectionDelayMsec = 5000;
constexpr uint32_t kDefaultZenzLiveCorrectionMinKeyLength = 2;
constexpr uint32_t kMinZenzLiveCorrectionMinKeyLength = 2;
constexpr uint32_t kMaxZenzLiveCorrectionMinKeyLength = 20;

constexpr uint32_t kDefaultInputPreeditTextColor = 0xff5000;
constexpr uint32_t kDefaultInputPreeditBackgroundColor = 0xffffcc;
constexpr uint32_t kDefaultInputPreeditUnderlineColor = 0xff0000;

constexpr uint32_t kDefaultTargetPreeditTextColor = 0x000000;
constexpr uint32_t kDefaultTargetPreeditBackgroundColor = 0xddeeff;
constexpr uint32_t kDefaultTargetPreeditUnderlineColor = 0x0066ff;

QColor RgbHexToQColor(const uint32_t rgb) {
  return QColor((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

uint32_t QColorToRgbHex(const QColor &color) {
  return (static_cast<uint32_t>(color.red()) << 16) |
         (static_cast<uint32_t>(color.green()) << 8) |
         static_cast<uint32_t>(color.blue());
}

QString RgbHexText(const uint32_t rgb) {
  return QString("#%1").arg(rgb, 6, 16, QLatin1Char('0')).toUpper();
}

void SetColorButton(QPushButton *button, const uint32_t rgb) {
  if (button == nullptr) {
    return;
  }

  const QString background = RgbHexText(rgb);

  const int r = static_cast<int>((rgb >> 16) & 0xff);
  const int g = static_cast<int>((rgb >> 8) & 0xff);
  const int b = static_cast<int>(rgb & 0xff);
  const int luminance = (r * 299 + g * 587 + b * 114) / 1000;
  const QString foreground =
      luminance < 128 ? QStringLiteral("#ffffff") : QStringLiteral("#000000");

  button->setProperty("rgb", rgb);
  button->setText(background);
  button->setStyleSheet(
      QString("background-color: %1; color: %2;")
          .arg(background, foreground));
}

uint32_t GetColorButtonRgb(const QPushButton *button,
                           const uint32_t default_rgb) {
  if (button == nullptr) {
    return default_rgb;
  }

  const QVariant value = button->property("rgb");
  if (!value.isValid()) {
    return default_rgb;
  }

  return value.toUInt();
}

QString ToQString(absl::string_view s) {
  return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

QString FeedbackReasonLabel(absl::string_view reason) {
  if (reason == "feedback_preferred") {
    return QString::fromUtf8("優先");
  }
  if (reason == "feedback_rejected") {
    return QString::fromUtf8("却下優勢");
  }
  return QString::fromUtf8("中立");
}

void SetTableItem(QTableWidget* table,
                  int row,
                  int column,
                  const QString& text) {
  QTableWidgetItem* item = new QTableWidgetItem(text);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  table->setItem(row, column, item);
}

void ShowJapaneseInformation(QWidget* parent,
                             const QString& title,
                             const QString& text) {
  QMessageBox message_box(parent);
  message_box.setWindowTitle(title);
  message_box.setIcon(QMessageBox::Information);
  message_box.setText(text);

  QPushButton* ok_button =
      message_box.addButton(QString::fromUtf8("OK"),
                            QMessageBox::AcceptRole);
  message_box.setDefaultButton(ok_button);
  message_box.exec();
}

void ShowJapaneseCritical(QWidget* parent,
                          const QString& title,
                          const QString& text) {
  QMessageBox message_box(parent);
  message_box.setWindowTitle(title);
  message_box.setIcon(QMessageBox::Critical);
  message_box.setText(text);

  QPushButton* ok_button =
      message_box.addButton(QString::fromUtf8("OK"),
                            QMessageBox::AcceptRole);
  message_box.setDefaultButton(ok_button);
  message_box.exec();
}

#ifdef _WIN32
int RunPowerShellScript(const std::wstring& script) {
  const std::wstring parameters =
      L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command \"" +
      script + L"\"";

  SHELLEXECUTEINFOW execute_info = {};
  execute_info.cbSize = sizeof(execute_info);
  execute_info.fMask = SEE_MASK_NOCLOSEPROCESS;
  execute_info.lpVerb = L"open";
  execute_info.lpFile = L"powershell.exe";
  execute_info.lpParameters = parameters.c_str();
  execute_info.nShow = SW_HIDE;

  if (!::ShellExecuteExW(&execute_info) ||
      execute_info.hProcess == nullptr) {
    return -1;
  }

  ::WaitForSingleObject(execute_info.hProcess, INFINITE);

  DWORD exit_code = 1;
  if (!::GetExitCodeProcess(execute_info.hProcess, &exit_code)) {
    ::CloseHandle(execute_info.hProcess);
    return -1;
  }

  ::CloseHandle(execute_info.hProcess);
  return static_cast<int>(exit_code);
}

std::wstring BuildSetDefaultImeScript(const std::wstring& mozkey_input_tip) {
  return std::wstring(
             L"$ErrorActionPreference='Stop';"
             L"$path='HKCU:\\Software\\Mozkey\\DefaultImeOverrideBackup';"

             // Read the current Windows default input method override.
             L"$current=Get-WinDefaultInputMethodOverride;"
             L"$currentTip='';"
             L"if ($null -ne $current -and "
             L"    -not [string]::IsNullOrWhiteSpace($current.InputMethodTip)) {"
             L"  $currentTip=[string]$current.InputMethodTip;"
             L"};"
             L"$mozkeyInputTip='") +
         mozkey_input_tip +
         std::wstring(
             L"';"

             // Do not overwrite an active backup.  This preserves the
             // original restore point even if the user presses the button
             // multiple times.
             L"$backupActive=0;"
             L"if (Test-Path $path) {"
             L"  $backup=Get-ItemProperty -Path $path;"
             L"  if ($backup.BackupActive -eq 1) { $backupActive=1 }"
             L"};"

             L"$alreadyMozkey=($currentTip -eq $mozkeyInputTip);"

             L"if ($backupActive -eq 1) {"
             L"  if (-not $alreadyMozkey) {"
             L"    Set-WinDefaultInputMethodOverride -InputTip $mozkeyInputTip;"
             L"  }"
             L"  exit 0;"
             L"};"

             // If Mozkey is already the override and there is no active
             // backup, do not create a pointless restore point whose restore
             // target is Mozkey itself.
             L"if ($alreadyMozkey) { exit 0 };"

             L"if (!(Test-Path $path)) {"
             L"  New-Item -Path $path -Force | Out-Null;"
             L"};"

             // Save the current Windows default input method override.
             L"if ([string]::IsNullOrWhiteSpace($currentTip)) {"
             L"  New-ItemProperty -Path $path -Name WasEmpty -PropertyType DWord "
             L"    -Value 1 -Force | Out-Null;"
             L"  Remove-ItemProperty -Path $path -Name InputTip "
             L"    -ErrorAction SilentlyContinue;"
             L"} else {"
             L"  New-ItemProperty -Path $path -Name WasEmpty -PropertyType DWord "
             L"    -Value 0 -Force | Out-Null;"
             L"  New-ItemProperty -Path $path -Name InputTip -PropertyType String "
             L"    -Value $currentTip -Force | Out-Null;"
             L"};"

             // Save the current ja InputMethodTips order because
             // Set-WinDefaultInputMethodOverride may reorder the list.
             L"$list=Get-WinUserLanguageList;"
             L"$ja=$list | Where-Object { $_.LanguageTag -eq 'ja' } | "
             L"  Select-Object -First 1;"
             L"if ($null -ne $ja) {"
             L"  $tips=@($ja.InputMethodTips) -join '|';"
             L"  New-ItemProperty -Path $path -Name JapaneseInputMethodTips "
             L"    -PropertyType String -Value $tips -Force | Out-Null;"
             L"};"

             L"New-ItemProperty -Path $path -Name BackupActive "
             L"  -PropertyType DWord -Value 1 -Force | Out-Null;"

             // Set Mozkey as the Windows default input method override.
             L"Set-WinDefaultInputMethodOverride -InputTip $mozkeyInputTip;");
}

std::wstring BuildRestoreDefaultImeScript() {
  return
      L"$ErrorActionPreference='Stop';"
      L"$path='HKCU:\\Software\\Mozkey\\DefaultImeOverrideBackup';"
      L"if (!(Test-Path $path)) { exit 2 };"
      L"$backup=Get-ItemProperty -Path $path;"
      L"if ($backup.BackupActive -ne 1) { exit 2 };"

      // Restore the previous Windows default input method override.
      L"if ($backup.WasEmpty -eq 1 -or "
      L"    [string]::IsNullOrWhiteSpace($backup.InputTip)) {"
      L"  Set-WinDefaultInputMethodOverride;"
      L"} else {"
      L"  $savedInputTip=[string]$backup.InputTip;"
      L"  Set-WinDefaultInputMethodOverride -InputTip $savedInputTip;"
      L"};"

      // Restore the previous ja InputMethodTips order if it was saved.
      L"if (-not [string]::IsNullOrWhiteSpace("
      L"    $backup.JapaneseInputMethodTips)) {"
      L"  $tips=$backup.JapaneseInputMethodTips -split '\\|';"
      L"  $list=Get-WinUserLanguageList;"
      L"  $ja=$list | Where-Object { $_.LanguageTag -eq 'ja' } | "
      L"    Select-Object -First 1;"
      L"  if ($null -ne $ja) {"
      L"    $ja.InputMethodTips.Clear();"
      L"    foreach ($tip in $tips) {"
      L"      if (-not [string]::IsNullOrWhiteSpace($tip)) {"
      L"        $ja.InputMethodTips.Add($tip) | Out-Null;"
      L"      }"
      L"    }"
      L"    Set-WinUserLanguageList $list -Force;"
      L"  }"
      L"};"

      // The restore point has been consumed.  Remove it so the next
      // Set action captures the then-current state as a new restore point.
      L"Remove-Item $path -Recurse -Force -ErrorAction SilentlyContinue;";
}
#endif  // _WIN32

void ShowZenzFeedbackManagementDialog(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setWindowTitle(QString::fromUtf8("Zenz 学習データの管理"));
  dialog.resize(760, 440);

  session::ZenzFeedbackStore store;

  QVBoxLayout* root_layout = new QVBoxLayout(&dialog);

  QLabel* description_label = new QLabel(
      QString::fromUtf8(
          "Zenz 補正結果のローカル学習データを管理します。"
          "TSV ファイルを直接開かず、安全な操作だけを行います。"),
      &dialog);
  description_label->setWordWrap(true);
  root_layout->addWidget(description_label);

  QHBoxLayout* search_layout = new QHBoxLayout;
  QLabel* search_label = new QLabel(QString::fromUtf8("検索:"), &dialog);
  QLineEdit* search_edit = new QLineEdit(&dialog);
  search_edit->setPlaceholderText(
      QString::fromUtf8("読み、候補、文脈クラスで絞り込み"));
  search_layout->addWidget(search_label);
  search_layout->addWidget(search_edit);
  root_layout->addLayout(search_layout);

  QTableWidget* table = new QTableWidget(&dialog);
  table->setColumnCount(6);
  table->setHorizontalHeaderLabels(QStringList()
                                   << QString::fromUtf8("読み")
                                   << QString::fromUtf8("候補")
                                   << QString::fromUtf8("文脈クラス")
                                   << QString::fromUtf8("採用")
                                   << QString::fromUtf8("却下")
                                   << QString::fromUtf8("判定"));
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->horizontalHeader()->setStretchLastSection(true);
  root_layout->addWidget(table);

  QLabel* status_label = new QLabel(&dialog);
  root_layout->addWidget(status_label);

  QHBoxLayout* button_layout = new QHBoxLayout;
  QPushButton* import_button =
      new QPushButton(QString::fromUtf8("インポート..."), &dialog);
  QPushButton* export_button =
      new QPushButton(QString::fromUtf8("エクスポート..."), &dialog);
  QPushButton* delete_button =
      new QPushButton(QString::fromUtf8("選択項目を削除"), &dialog);
  QPushButton* clear_button =
      new QPushButton(QString::fromUtf8("すべて削除"), &dialog);
  QDialogButtonBox* close_buttons =
      new QDialogButtonBox(QDialogButtonBox::Close, &dialog);

  if (QPushButton* close_button =
          close_buttons->button(QDialogButtonBox::Close)) {
    close_button->setText(QString::fromUtf8("閉じる"));
  }

  button_layout->addWidget(import_button);
  button_layout->addWidget(export_button);
  button_layout->addWidget(delete_button);
  button_layout->addWidget(clear_button);
  button_layout->addStretch();
  button_layout->addWidget(close_buttons);
  root_layout->addLayout(button_layout);

  auto reload_table = [&]() {
    const QString filter = search_edit->text();
    const std::vector<session::ZenzFeedbackEntry> entries =
        store.ListEntries();

    table->setRowCount(0);

    int visible_count = 0;
    for (const session::ZenzFeedbackEntry& entry : entries) {
      const QString key = ToQString(entry.key);
      const QString value = ToQString(entry.value);
      const QString context_class = ToQString(entry.context_class);

      if (!filter.isEmpty() &&
          !key.contains(filter, Qt::CaseInsensitive) &&
          !value.contains(filter, Qt::CaseInsensitive) &&
          !context_class.contains(filter, Qt::CaseInsensitive)) {
        continue;
      }

      const int row = table->rowCount();
      table->insertRow(row);

      SetTableItem(table, row, 0, key);
      SetTableItem(table, row, 1, value);
      SetTableItem(table, row, 2, context_class);
      SetTableItem(table, row, 3, QString::number(entry.accepted_count));
      SetTableItem(table, row, 4, QString::number(entry.rejected_count));
      SetTableItem(table, row, 5, FeedbackReasonLabel(entry.reason));

      table->item(row, 0)->setData(Qt::UserRole, key);
      table->item(row, 1)->setData(Qt::UserRole, value);
      table->item(row, 2)->setData(Qt::UserRole, context_class);

      ++visible_count;
    }

    table->resizeColumnsToContents();

    status_label->setText(
        QString::fromUtf8("表示 %1 件 / 全 %2 件")
            .arg(visible_count)
            .arg(static_cast<int>(entries.size())));

    const bool has_visible_row = table->rowCount() > 0;
    delete_button->setEnabled(has_visible_row);
    export_button->setEnabled(!entries.empty());
    clear_button->setEnabled(!entries.empty());
  };

  QObject::connect(search_edit, &QLineEdit::textChanged,
                   &dialog, [&](const QString&) {
                     reload_table();
                   });

  QObject::connect(close_buttons, &QDialogButtonBox::rejected,
                   &dialog, &QDialog::reject);

  QObject::connect(export_button, &QPushButton::clicked,
                   &dialog, [&]() {
                     const QString path = QFileDialog::getSaveFileName(
                         &dialog,
                         QString::fromUtf8("Zenz 学習データをエクスポート"),
                         QStringLiteral("zenz_feedback.tsv"),
                         QString::fromUtf8(
                             "TSV ファイル (*.tsv);;すべてのファイル (*)"));
                     if (path.isEmpty()) {
                       return;
                     }

                     if (!store.ExportToFile(path.toStdWString())) {
                       ShowJapaneseCritical(
                           &dialog, dialog.windowTitle(),
                           QString::fromUtf8(
                               "Zenz 学習データをエクスポートできませんでした。"));
                       return;
                     }

                     ShowJapaneseInformation(
                         &dialog, dialog.windowTitle(),
                         QString::fromUtf8(
                             "Zenz 学習データをエクスポートしました。"));
                   });

  QObject::connect(import_button, &QPushButton::clicked,
                   &dialog, [&]() {
                     const QString path = QFileDialog::getOpenFileName(
                         &dialog,
                         QString::fromUtf8("Zenz 学習データをインポート"),
                         QString(),
                         QString::fromUtf8(
                             "TSV ファイル (*.tsv);;すべてのファイル (*)"));
                     if (path.isEmpty()) {
                       return;
                     }

                     QMessageBox message_box(&dialog);
                     message_box.setWindowTitle(dialog.windowTitle());
                     message_box.setIcon(QMessageBox::Question);
                     message_box.setText(
                         QString::fromUtf8("Zenz 学習データをインポートします。"));
                     message_box.setInformativeText(
                         QString::fromUtf8(
                             "既存の Zenz 学習データに追加しますか？\n\n"
                             "「追加」: 既存データに追加\n"
                             "「置き換え」: 既存データを削除してから取り込み\n"
                             "「キャンセル」: 中止"));

                     QPushButton* append_button =
                         message_box.addButton(QString::fromUtf8("追加"),
                                               QMessageBox::AcceptRole);
                     QPushButton* replace_button =
                         message_box.addButton(QString::fromUtf8("置き換え"),
                                               QMessageBox::DestructiveRole);
                     QPushButton* cancel_button =
                         message_box.addButton(QString::fromUtf8("キャンセル"),
                                               QMessageBox::RejectRole);

                     message_box.setDefaultButton(append_button);
                     message_box.exec();

                     if (message_box.clickedButton() == cancel_button) {
                       return;
                     }

                     const session::ZenzFeedbackImportMode import_mode =
                         message_box.clickedButton() == replace_button
                             ? session::ZenzFeedbackImportMode::kReplace
                             : session::ZenzFeedbackImportMode::kAppend;

                     if (!store.ImportFromFile(path.toStdWString(),
                                               import_mode)) {
                       ShowJapaneseCritical(
                           &dialog, dialog.windowTitle(),
                           QString::fromUtf8(
                               "Zenz 学習データをインポートできませんでした。\n"
                               "ファイル形式が壊れているか、未対応の行が含まれています。"));
                       return;
                     }

                     reload_table();

                     ShowJapaneseInformation(
                         &dialog, dialog.windowTitle(),
                         QString::fromUtf8(
                             "Zenz 学習データをインポートしました。"));
                   });

  QObject::connect(delete_button, &QPushButton::clicked,
                   &dialog, [&]() {
                     const int row = table->currentRow();
                     if (row < 0) {
                       return;
                     }

                     QTableWidgetItem* key_item = table->item(row, 0);
                     QTableWidgetItem* value_item = table->item(row, 1);
                     QTableWidgetItem* context_item = table->item(row, 2);
                     if (key_item == nullptr ||
                         value_item == nullptr ||
                         context_item == nullptr) {
                       return;
                     }

                     const QString key =
                         key_item->data(Qt::UserRole).toString();
                     const QString value =
                         value_item->data(Qt::UserRole).toString();
                     const QString context_class =
                         context_item->data(Qt::UserRole).toString();

                     QMessageBox message_box(&dialog);
                     message_box.setWindowTitle(dialog.windowTitle());
                     message_box.setIcon(QMessageBox::Warning);
                     message_box.setText(
                         QString::fromUtf8("選択した Zenz 学習エントリを削除しますか？"));
                     message_box.setInformativeText(
                         QString::fromUtf8("読み: %1\n候補: %2\n文脈クラス: %3")
                             .arg(key, value, context_class));

                     QPushButton* delete_confirm_button =
                         message_box.addButton(QString::fromUtf8("削除"),
                                               QMessageBox::DestructiveRole);
                     QPushButton* cancel_button =
                         message_box.addButton(QString::fromUtf8("キャンセル"),
                                               QMessageBox::RejectRole);

                     message_box.setDefaultButton(cancel_button);
                     message_box.exec();

                     if (message_box.clickedButton() != delete_confirm_button) {
                       return;
                     }

                     if (!store.DeleteEntry(
                             key.toUtf8().constData(),
                             context_class.toUtf8().constData(),
                             value.toUtf8().constData())) {
                       ShowJapaneseCritical(
                           &dialog, dialog.windowTitle(),
                           QString::fromUtf8(
                               "Zenz 学習エントリを削除できませんでした。"));
                       return;
                     }

                     reload_table();
                   });

  QObject::connect(clear_button, &QPushButton::clicked,
                   &dialog, [&]() {
                     QMessageBox message_box(&dialog);
                     message_box.setWindowTitle(dialog.windowTitle());
                     message_box.setIcon(QMessageBox::Warning);
                     message_box.setText(
                         QString::fromUtf8("Zenz 学習データをすべて削除しますか？"));
                     message_box.setInformativeText(
                         QString::fromUtf8("この操作は元に戻せません。"));

                     QPushButton* clear_confirm_button =
                         message_box.addButton(QString::fromUtf8("すべて削除"),
                                              QMessageBox::DestructiveRole);
                     QPushButton* cancel_button =
                         message_box.addButton(QString::fromUtf8("キャンセル"),
                                               QMessageBox::RejectRole);

                     message_box.setDefaultButton(cancel_button);
                     message_box.exec();

                     if (message_box.clickedButton() != clear_confirm_button) {
                       return;
                     }

                     if (!store.ClearAll()) {
                       ShowJapaneseCritical(
                           &dialog, dialog.windowTitle(),
                           QString::fromUtf8(
                               "Zenz 学習データを削除できませんでした。"));
                       return;
                     }

                     reload_table();
                   });

  reload_table();
  dialog.exec();
}

#ifdef _WIN32
void NotifyDisplayAttributeUpdate() {
  HRESULT coinit_result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool should_uninitialize =
      (coinit_result == S_OK || coinit_result == S_FALSE);

  if (SUCCEEDED(coinit_result) || coinit_result == RPC_E_CHANGED_MODE) {
    ITfDisplayAttributeMgr *display_attribute_mgr = nullptr;
    const HRESULT hr = ::CoCreateInstance(
        CLSID_TF_DisplayAttributeMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfDisplayAttributeMgr,
        reinterpret_cast<void **>(&display_attribute_mgr));

    if (SUCCEEDED(hr) && display_attribute_mgr != nullptr) {
      display_attribute_mgr->OnUpdateInfo();
      display_attribute_mgr->Release();
    }
  }

  if (should_uninitialize) {
    ::CoUninitialize();
  }
}
#endif  // _WIN32

void SetComboboxForPreeditMethod(const config::Config &config,
                                 QComboBox *combobox) {
  int index = static_cast<int>(config.preedit_method());
#ifdef _WIN32
  if (config.use_keyboard_to_change_preedit_method()) {
    index += kPreeditMethodSize;
  }
#endif  // _WIN32
  combobox->setCurrentIndex(index);
}

void GetComboboxForPreeditMethod(const QComboBox *combobox,
                                 config::Config *config) {
  int index = combobox->currentIndex();
  if (index >= kPreeditMethodSize) {
    // |use_keyboard_to_change_preedit_method| should be true and
    // |index| should be adjusted to smaller than kPreeditMethodSize.
    config->set_preedit_method(
        static_cast<config::Config_PreeditMethod>(index - kPreeditMethodSize));
    config->set_use_keyboard_to_change_preedit_method(true);
  } else {
    config->set_preedit_method(
        static_cast<config::Config_PreeditMethod>(index));
    config->set_use_keyboard_to_change_preedit_method(false);
  }
}
}  // namespace

// TODO(taku)
// Actually ConvertFromProto and ConvertToProto are almost the same.
// The difference only SET_ and GET_. We would like to unify the twos.
void ConfigDialog::ConvertFromProto(const config::Config &config) {
  base_config_ = config;
  // tab1
  SetComboboxForPreeditMethod(config, inputModeComboBox);
  SET_COMBOBOX(punctuationsSettingComboBox, PunctuationMethod,
               punctuation_method);
  SET_COMBOBOX(symbolsSettingComboBox, SymbolMethod, symbol_method);
  SET_COMBOBOX(spaceCharacterFormComboBox, FundamentalCharacterForm,
               space_character_form);
  SET_COMBOBOX(selectionShortcutModeComboBox, SelectionShortcut,
               selection_shortcut);
  SET_COMBOBOX(numpadCharacterFormComboBox, NumpadCharacterForm,
               numpad_character_form);
  SET_COMBOBOX(keymapSettingComboBox, SessionKeymap, session_keymap);

  custom_keymap_table_ = config.custom_keymap_table();
  custom_roman_table_ = config.custom_roman_table();

  // tab2
  SET_COMBOBOX(historyLearningLevelComboBox, HistoryLearningLevel,
               history_learning_level);
  SET_CHECKBOX(singleKanjiConversionCheckBox, use_single_kanji_conversion);
  SET_CHECKBOX(symbolConversionCheckBox, use_symbol_conversion);
  SET_CHECKBOX(emoticonConversionCheckBox, use_emoticon_conversion);
  SET_CHECKBOX(dateConversionCheckBox, use_date_conversion);
  SET_CHECKBOX(emojiConversionCheckBox, use_emoji_conversion);
  SET_CHECKBOX(numberConversionCheckBox, use_number_conversion);
  SET_CHECKBOX(calculatorCheckBox, use_calculator);
  SET_CHECKBOX(t13nConversionCheckBox, use_t13n_conversion);
  SET_CHECKBOX(zipcodeConversionCheckBox, use_zip_code_conversion);
  SET_CHECKBOX(spellingCorrectionCheckBox, use_spelling_correction);

  // InfoListConfig
  localUsageDictionaryCheckBox->setChecked(
      config.information_list_config().use_local_usage_dictionary());

  // tab3
  SET_CHECKBOX(autoSwitchCompositionMode, auto_switch_composition_mode);

  SET_CHECKBOX(liveConversionCheckBox, use_live_conversion);

  const uint32_t live_conversion_delay_msec =
      config.has_live_conversion_delay_msec()
          ? config.live_conversion_delay_msec()
          : kDefaultLiveConversionDelayMsec;
  liveConversionDelaySpinBox->setValue(
      static_cast<int>(
          std::clamp(live_conversion_delay_msec,
                     0u,
                     kMaxLiveConversionDelayMsec)));

  SET_CHECKBOX(zenzLiveCorrectionCheckBox, use_zenz_live_correction);

  const uint32_t zenz_live_correction_delay_msec =
      config.has_zenz_live_correction_delay_msec()
          ? config.zenz_live_correction_delay_msec()
          : kDefaultZenzLiveCorrectionDelayMsec;
  zenzLiveCorrectionDelaySpinBox->setValue(
      static_cast<int>(
          std::clamp(zenz_live_correction_delay_msec,
                     0u,
                     kMaxZenzLiveCorrectionDelayMsec)));

  const uint32_t zenz_live_correction_min_key_length =
      config.has_zenz_live_correction_min_key_length()
          ? config.zenz_live_correction_min_key_length()
          : kDefaultZenzLiveCorrectionMinKeyLength;
  zenzLiveCorrectionMinKeyLengthSpinBox->setValue(
      static_cast<int>(
          std::clamp(zenz_live_correction_min_key_length,
                     kMinZenzLiveCorrectionMinKeyLength,
                     kMaxZenzLiveCorrectionMinKeyLength)));

  SET_CHECKBOX(zenzFeedbackLearningCheckBox, use_zenz_feedback_learning);

  SET_CHECKBOX(useAutoConversion, use_auto_conversion);
  kutenCheckBox->setChecked(config.auto_conversion_key() &
                            config::Config::AUTO_CONVERSION_KUTEN);
  toutenCheckBox->setChecked(config.auto_conversion_key() &
                             config::Config::AUTO_CONVERSION_TOUTEN);
  questionMarkCheckBox->setChecked(
      config.auto_conversion_key() &
      config::Config::AUTO_CONVERSION_QUESTION_MARK);
  exclamationMarkCheckBox->setChecked(
      config.auto_conversion_key() &
      config::Config::AUTO_CONVERSION_EXCLAMATION_MARK);

  SET_CHECKBOX(useDirectCommit, use_direct_commit);
  directCommitKutenCheckBox->setChecked(
      config.direct_commit_key() &
      config::Config::DIRECT_COMMIT_KUTEN);
  directCommitToutenCheckBox->setChecked(
      config.direct_commit_key() &
      config::Config::DIRECT_COMMIT_TOUTEN);
  directCommitQuestionMarkCheckBox->setChecked(
      config.direct_commit_key() &
      config::Config::DIRECT_COMMIT_QUESTION_MARK);
  directCommitExclamationMarkCheckBox->setChecked(
      config.direct_commit_key() &
      config::Config::DIRECT_COMMIT_EXCLAMATION_MARK);
  directCommitOpenParenthesisCheckBox->setChecked(
      config.direct_commit_key() &
      config::Config::DIRECT_COMMIT_OPEN_PARENTHESIS);
  directCommitCloseParenthesisCheckBox->setChecked(
      config.direct_commit_key() &
      config::Config::DIRECT_COMMIT_CLOSE_PARENTHESIS);
  directCommitOpenBracketCheckBox->setChecked(
      config.direct_commit_key() &
      config::Config::DIRECT_COMMIT_OPEN_BRACKET);
  directCommitCloseBracketCheckBox->setChecked(
      config.direct_commit_key() &
      config::Config::DIRECT_COMMIT_CLOSE_BRACKET);

  SET_COMBOBOX(shiftKeyModeSwitchComboBox, ShiftKeyModeSwitch,
               shift_key_mode_switch);

  SET_CHECKBOX(useJapaneseLayout, use_japanese_layout);

  SET_CHECKBOX(useModeIndicator, use_mode_indicator);

  SET_CHECKBOX(useDarkModeCandidateWindow, use_dark_mode_candidate_window);

  SET_CHECKBOX(inputPreeditTextColorCheckBox, use_custom_preedit_text_color);
  SetColorButton(inputPreeditTextColorButton, config.preedit_text_color());
  inputPreeditTextColorButton->setEnabled(
      config.use_custom_preedit_text_color());

  SET_CHECKBOX(inputPreeditBackgroundColorCheckBox,
              use_custom_preedit_background_color);
  SetColorButton(inputPreeditBackgroundColorButton,
                config.preedit_background_color());
  inputPreeditBackgroundColorButton->setEnabled(
      config.use_custom_preedit_background_color());

  SET_CHECKBOX(inputPreeditUnderlineColorCheckBox,
              use_custom_preedit_underline_color);
  SetColorButton(inputPreeditUnderlineColorButton,
                config.preedit_underline_color());
  inputPreeditUnderlineColorButton->setEnabled(
      config.use_custom_preedit_underline_color());

  SET_CHECKBOX(targetPreeditTextColorCheckBox,
              use_custom_preedit_target_text_color);
  SetColorButton(targetPreeditTextColorButton,
                config.preedit_target_text_color());
  targetPreeditTextColorButton->setEnabled(
      config.use_custom_preedit_target_text_color());

  SET_CHECKBOX(targetPreeditBackgroundColorCheckBox,
              use_custom_preedit_target_background_color);
  SetColorButton(targetPreeditBackgroundColorButton,
                config.preedit_target_background_color());
  targetPreeditBackgroundColorButton->setEnabled(
      config.use_custom_preedit_target_background_color());

  SET_CHECKBOX(targetPreeditUnderlineColorCheckBox,
              use_custom_preedit_target_underline_color);
  SetColorButton(targetPreeditUnderlineColorButton,
                config.preedit_target_underline_color());
  targetPreeditUnderlineColorButton->setEnabled(
      config.use_custom_preedit_target_underline_color());

  // tab4
  SET_CHECKBOX(historySuggestCheckBox, use_history_suggest);
  SET_CHECKBOX(dictionarySuggestCheckBox, use_dictionary_suggest);
  SET_CHECKBOX(realtimeConversionCheckBox, use_realtime_conversion);

  suggestionsSizeSpinBox->setValue(
      std::clamp<int>(config.suggestions_size(), 1, 9));

  // tab5
  SET_CHECKBOX(incognitoModeCheckBox, incognito_mode);
  SET_CHECKBOX(presentationModeCheckBox, presentation_mode);

  // tab6
  SET_COMBOBOX(verboseLevelComboBox, int, verbose_level);
  SET_COMBOBOX(yenSignComboBox, YenSignCharacter, yen_sign_character);

  characterFormEditor->Load(config);

#ifdef __APPLE__
  startupCheckBox->setChecked(MacUtil::CheckPrelauncherLoginItemStatus());
#endif  // __APPLE__
}

void ConfigDialog::ConvertToProto(config::Config *config) const {
  *config = base_config_;

  // tab1
  GetComboboxForPreeditMethod(inputModeComboBox, config);
  GET_COMBOBOX(punctuationsSettingComboBox, PunctuationMethod,
               punctuation_method);
  GET_COMBOBOX(symbolsSettingComboBox, SymbolMethod, symbol_method);
  GET_COMBOBOX(spaceCharacterFormComboBox, FundamentalCharacterForm,
               space_character_form);
  GET_COMBOBOX(selectionShortcutModeComboBox, SelectionShortcut,
               selection_shortcut);
  GET_COMBOBOX(numpadCharacterFormComboBox, NumpadCharacterForm,
               numpad_character_form);
  GET_COMBOBOX(keymapSettingComboBox, SessionKeymap, session_keymap);

  config->set_custom_keymap_table(custom_keymap_table_);

  config->clear_custom_roman_table();
  if (!custom_roman_table_.empty()) {
    config->set_custom_roman_table(custom_roman_table_);
  }

  // tab2
  GET_COMBOBOX(historyLearningLevelComboBox, HistoryLearningLevel,
               history_learning_level);
  GET_CHECKBOX(singleKanjiConversionCheckBox, use_single_kanji_conversion);
  GET_CHECKBOX(symbolConversionCheckBox, use_symbol_conversion);
  GET_CHECKBOX(emoticonConversionCheckBox, use_emoticon_conversion);
  GET_CHECKBOX(dateConversionCheckBox, use_date_conversion);
  GET_CHECKBOX(emojiConversionCheckBox, use_emoji_conversion);
  GET_CHECKBOX(numberConversionCheckBox, use_number_conversion);
  GET_CHECKBOX(calculatorCheckBox, use_calculator);
  GET_CHECKBOX(t13nConversionCheckBox, use_t13n_conversion);
  GET_CHECKBOX(zipcodeConversionCheckBox, use_zip_code_conversion);
  GET_CHECKBOX(spellingCorrectionCheckBox, use_spelling_correction);

  // InformationListConfig
  config->mutable_information_list_config()->set_use_local_usage_dictionary(
      localUsageDictionaryCheckBox->isChecked());

  // tab3
  GET_CHECKBOX(autoSwitchCompositionMode, auto_switch_composition_mode);

  GET_CHECKBOX(liveConversionCheckBox, use_live_conversion);
  config->set_live_conversion_delay_msec(
      static_cast<uint32_t>(liveConversionDelaySpinBox->value()));

  GET_CHECKBOX(zenzLiveCorrectionCheckBox, use_zenz_live_correction);
  config->set_zenz_live_correction_delay_msec(
      static_cast<uint32_t>(zenzLiveCorrectionDelaySpinBox->value()));
  config->set_zenz_live_correction_min_key_length(
      static_cast<uint32_t>(
          zenzLiveCorrectionMinKeyLengthSpinBox->value()));

  GET_CHECKBOX(zenzFeedbackLearningCheckBox, use_zenz_feedback_learning);

  GET_CHECKBOX(useAutoConversion, use_auto_conversion);
  GET_CHECKBOX(useDirectCommit, use_direct_commit);
  GET_CHECKBOX(useJapaneseLayout, use_japanese_layout);

  GET_CHECKBOX(useModeIndicator, use_mode_indicator);

  GET_CHECKBOX(useDarkModeCandidateWindow, use_dark_mode_candidate_window);

  GET_CHECKBOX(inputPreeditTextColorCheckBox,
              use_custom_preedit_text_color);
  config->set_preedit_text_color(
      GetColorButtonRgb(inputPreeditTextColorButton,
                        kDefaultInputPreeditTextColor));

  GET_CHECKBOX(inputPreeditBackgroundColorCheckBox,
              use_custom_preedit_background_color);
  config->set_preedit_background_color(
      GetColorButtonRgb(inputPreeditBackgroundColorButton,
                        kDefaultInputPreeditBackgroundColor));

  GET_CHECKBOX(inputPreeditUnderlineColorCheckBox,
              use_custom_preedit_underline_color);
  config->set_preedit_underline_color(
      GetColorButtonRgb(inputPreeditUnderlineColorButton,
                        kDefaultInputPreeditUnderlineColor));

  GET_CHECKBOX(targetPreeditTextColorCheckBox,
              use_custom_preedit_target_text_color);
  config->set_preedit_target_text_color(
      GetColorButtonRgb(targetPreeditTextColorButton,
                        kDefaultTargetPreeditTextColor));

  GET_CHECKBOX(targetPreeditBackgroundColorCheckBox,
              use_custom_preedit_target_background_color);
  config->set_preedit_target_background_color(
      GetColorButtonRgb(targetPreeditBackgroundColorButton,
                        kDefaultTargetPreeditBackgroundColor));

  GET_CHECKBOX(targetPreeditUnderlineColorCheckBox,
              use_custom_preedit_target_underline_color);
  config->set_preedit_target_underline_color(
      GetColorButtonRgb(targetPreeditUnderlineColorButton,
                        kDefaultTargetPreeditUnderlineColor));

  uint32_t auto_conversion_key = 0;
  if (kutenCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_KUTEN;
  }
  if (toutenCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_TOUTEN;
  }
  if (questionMarkCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_QUESTION_MARK;
  }
  if (exclamationMarkCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_EXCLAMATION_MARK;
  }
  config->set_auto_conversion_key(auto_conversion_key);

  uint32_t direct_commit_key = 0;
  if (directCommitKutenCheckBox->isChecked()) {
    direct_commit_key |= config::Config::DIRECT_COMMIT_KUTEN;
  }
  if (directCommitToutenCheckBox->isChecked()) {
    direct_commit_key |= config::Config::DIRECT_COMMIT_TOUTEN;
  }
  if (directCommitQuestionMarkCheckBox->isChecked()) {
    direct_commit_key |= config::Config::DIRECT_COMMIT_QUESTION_MARK;
  }
  if (directCommitExclamationMarkCheckBox->isChecked()) {
    direct_commit_key |= config::Config::DIRECT_COMMIT_EXCLAMATION_MARK;
  }
  if (directCommitOpenParenthesisCheckBox->isChecked()) {
    direct_commit_key |= config::Config::DIRECT_COMMIT_OPEN_PARENTHESIS;
  }
  if (directCommitCloseParenthesisCheckBox->isChecked()) {
    direct_commit_key |= config::Config::DIRECT_COMMIT_CLOSE_PARENTHESIS;
  }
  if (directCommitOpenBracketCheckBox->isChecked()) {
    direct_commit_key |= config::Config::DIRECT_COMMIT_OPEN_BRACKET;
  }
  if (directCommitCloseBracketCheckBox->isChecked()) {
    direct_commit_key |= config::Config::DIRECT_COMMIT_CLOSE_BRACKET;
  }
  config->set_direct_commit_key(direct_commit_key);

  // Mutual exclusion normalization.
  if (config->use_auto_conversion()) {
    config->set_use_direct_commit(false);
    config->set_direct_commit_key(0);
  } else if (config->use_direct_commit()) {
    config->set_use_auto_conversion(false);
    config->set_auto_conversion_key(0);
  }

  GET_COMBOBOX(shiftKeyModeSwitchComboBox, ShiftKeyModeSwitch,
               shift_key_mode_switch);

  // tab4
  GET_CHECKBOX(historySuggestCheckBox, use_history_suggest);
  GET_CHECKBOX(dictionarySuggestCheckBox, use_dictionary_suggest);
  GET_CHECKBOX(realtimeConversionCheckBox, use_realtime_conversion);

  config->set_suggestions_size(
      static_cast<uint32_t>(suggestionsSizeSpinBox->value()));

  // tab5
  GET_CHECKBOX(incognitoModeCheckBox, incognito_mode);
  GET_CHECKBOX(presentationModeCheckBox, presentation_mode);

  // tab6
  config->set_verbose_level(verboseLevelComboBox->currentIndex());
  GET_COMBOBOX(yenSignComboBox, YenSignCharacter, yen_sign_character);

  characterFormEditor->Save(config);
}

#undef SET_COMBOBOX
#undef SET_CHECKBOX
#undef GET_COMBOBOX
#undef GET_CHECKBOX

void ConfigDialog::SelectPreeditColor() {
  QPushButton *button = qobject_cast<QPushButton *>(sender());
  if (button == nullptr) {
    return;
  }

  const uint32_t current_rgb = GetColorButtonRgb(button, 0x000000);
  const QColor selected_color =
      QColorDialog::getColor(RgbHexToQColor(current_rgb), this,
                            QString::fromUtf8("未確定文字の色を選択"));

  if (!selected_color.isValid()) {
    return;
  }

  SetColorButton(button, QColorToRgbHex(selected_color));
  EnableApplyButton();
}

void ConfigDialog::clicked(QAbstractButton *button) {
  switch (configDialogButtonBox->buttonRole(button)) {
    case QDialogButtonBox::AcceptRole:
      if (Update()) {
        QWidget::close();
      }
      break;
    case QDialogButtonBox::ApplyRole:
      Update();
      break;
    case QDialogButtonBox::RejectRole:
      QWidget::close();
      break;
    default:
      break;
  }
}

void ConfigDialog::ClearUserHistory() {
  if (QMessageBox::Ok !=
      QMessageBox::question(
          this, windowTitle(),
          tr("Do you want to clear personalization data? "
             "Input history is not reset with this operation. "
             "Please open \"suggestion\" tab to remove input history data."),
          QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel)) {
    return;
  }

  client_->CheckVersionOrRestartServer();

  if (!client_->ClearUserHistory()) {
    QMessageBox::critical(this, windowTitle(),
                          tr("%1 Converter is not running. "
                             "Settings were not saved.")
                              .arg(GuiUtil::ProductName()));
  }
}

void ConfigDialog::ClearUserPrediction() {
  if (QMessageBox::Ok !=
      QMessageBox::question(
          this, windowTitle(), tr("Do you want to clear all history data?"),
          QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel)) {
    return;
  }

  client_->CheckVersionOrRestartServer();

  if (!client_->ClearUserPrediction()) {
    QMessageBox::critical(
        this, windowTitle(),
        tr("%1 Converter is not running. Settings were not saved.")
            .arg(GuiUtil::ProductName()));
  }
}

void ConfigDialog::ClearUnusedUserPrediction() {
  if (QMessageBox::Ok !=
      QMessageBox::question(
          this, windowTitle(), tr("Do you want to clear unused history data?"),
          QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel)) {
    return;
  }

  client_->CheckVersionOrRestartServer();

  if (!client_->ClearUnusedUserPrediction()) {
    QMessageBox::critical(
        this, windowTitle(),
        tr("%1 Converter is not running. Operation was not executed.")
            .arg(GuiUtil::ProductName()));
  }
}

void ConfigDialog::EditZenzFeedback() {
  ShowZenzFeedbackManagementDialog(this);
}

void ConfigDialog::EditUserDictionary() {
  client_->LaunchTool("dictionary_tool", "");
}

void ConfigDialog::EditKeymap() {
  std::string current_keymap_table = "";
  const QString keymap_name = keymapSettingComboBox->currentText();
  const std::map<QString, config::Config::SessionKeymap>::const_iterator itr =
      keymapname_sessionkeymap_map_.find(keymap_name);
  if (itr != keymapname_sessionkeymap_map_.end()) {
    // Load from predefined mapping file.
    const char *keymap_file =
        keymap::KeyMapManager::GetKeyMapFileName(itr->second);
    std::unique_ptr<std::istream> ifs(
        ConfigFileStream::LegacyOpen(keymap_file));
    CHECK(ifs.get() != nullptr);  // should never happen
    std::stringstream buffer;
    buffer << ifs->rdbuf();
    current_keymap_table = buffer.str();
  } else {
    current_keymap_table = custom_keymap_table_;
  }
  std::string output;
  if (gui::KeyMapEditorDialog::Show(this, current_keymap_table, &output)) {
    custom_keymap_table_ = output;
    // set keymapSettingComboBox to "Custom keymap"
    keymapSettingComboBox->setCurrentIndex(0);
  }
}

void ConfigDialog::EditRomanTable() {
  std::string output;
  if (gui::RomanTableEditorDialog::Show(this, custom_roman_table_, &output)) {
    custom_roman_table_ = output;
  }
}

void ConfigDialog::SelectInputModeSetting(int index) {
  // enable "EDIT" button if roman mode is selected
  editRomanTableButton->setEnabled((index == 0));
}

void ConfigDialog::SelectLiveConversionSetting(int state) {
  const bool enabled = static_cast<bool>(state);

  liveConversionDelayLabel->setEnabled(enabled);
  liveConversionDelaySpinBox->setEnabled(enabled);

  zenzLiveCorrectionCheckBox->setEnabled(enabled);
  SelectZenzLiveCorrectionSetting(
      enabled ? static_cast<int>(zenzLiveCorrectionCheckBox->isChecked()) : 0);
}

void ConfigDialog::SelectZenzLiveCorrectionSetting(int state) {
  const bool enabled =
      liveConversionCheckBox->isChecked() && static_cast<bool>(state);

  zenzLiveCorrectionDelayLabel->setEnabled(enabled);
  zenzLiveCorrectionDelaySpinBox->setEnabled(enabled);
  zenzLiveCorrectionMinKeyLengthLabel->setEnabled(enabled);
  zenzLiveCorrectionMinKeyLengthSpinBox->setEnabled(enabled);
  zenzFeedbackLearningCheckBox->setEnabled(enabled);
}

void ConfigDialog::SelectAutoConversionSetting(int state) {
  const bool enabled = static_cast<bool>(state);

  kutenCheckBox->setEnabled(enabled);
  toutenCheckBox->setEnabled(enabled);
  questionMarkCheckBox->setEnabled(enabled);
  exclamationMarkCheckBox->setEnabled(enabled);

  if (enabled && useDirectCommit->isChecked()) {
    useDirectCommit->setChecked(false);
  }
}

void ConfigDialog::SelectDirectCommitSetting(int state) {
  const bool enabled = static_cast<bool>(state);

  directCommitKutenCheckBox->setEnabled(enabled);
  directCommitToutenCheckBox->setEnabled(enabled);
  directCommitQuestionMarkCheckBox->setEnabled(enabled);
  directCommitExclamationMarkCheckBox->setEnabled(enabled);
  directCommitOpenParenthesisCheckBox->setEnabled(enabled);
  directCommitCloseParenthesisCheckBox->setEnabled(enabled);
  directCommitOpenBracketCheckBox->setEnabled(enabled);
  directCommitCloseBracketCheckBox->setEnabled(enabled);

  if (enabled && useAutoConversion->isChecked()) {
    useAutoConversion->setChecked(false);
  }
}

void ConfigDialog::SelectSuggestionSetting(int state) {
  if (historySuggestCheckBox->isChecked() ||
      dictionarySuggestCheckBox->isChecked() ||
      realtimeConversionCheckBox->isChecked()) {
    presentationModeCheckBox->setEnabled(true);
  } else {
    presentationModeCheckBox->setEnabled(false);
  }
}

void ConfigDialog::ResetToDefaults() {
  const QString message =
      tr("When you reset %1 settings, any changes "
         "you've made will be reverted to the default settings. "
         "Do you want to reset settings? "
         "The following items are not reset with this operation.\n"
         " - Personalization data\n"
         " - Input history\n"
         " - Administrator settings")
          .arg(GuiUtil::ProductName());
  if (QMessageBox::Ok ==
      QMessageBox::question(this, windowTitle(), message,
                            QMessageBox::Ok | QMessageBox::Cancel,
                            QMessageBox::Cancel)) {
    // TODO(taku): remove the dependency to config::ConfigHandler
    // nice to have GET_DEFAULT_CONFIG command
    ConvertFromProto(config::ConfigHandler::DefaultConfig());
  }
}

void ConfigDialog::LaunchAdministrationDialog() {
#ifdef _WIN32
  client_->LaunchTool("administration_dialog", "");
#endif  // _WIN32
}

void ConfigDialog::SetMozkeyAsDefaultIme() {
#ifdef _WIN32
  const std::wstring mozkey_input_tip = mozc::win32::ImeUtil::GetInputTip();
  if (mozkey_input_tip.empty()) {
    QMessageBox::critical(
        this, windowTitle(),
        tr("Failed to get %1 InputTip.").arg(GuiUtil::ProductName()));
    return;
  }

  const QMessageBox::StandardButton result = QMessageBox::question(
      this, windowTitle(),
      tr("Set %1 as the Windows default IME?\n\n"
         "The current Windows default IME override will be saved so it can "
         "be restored later.")
          .arg(GuiUtil::ProductName()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (result != QMessageBox::Yes) {
    return;
  }

  const int exit_code =
      RunPowerShellScript(BuildSetDefaultImeScript(mozkey_input_tip));

  if (exit_code == 0) {
    QMessageBox::information(
        this, windowTitle(),
        tr("%1 has been set as the Windows default IME.")
            .arg(GuiUtil::ProductName()));
  } else {
    QMessageBox::critical(
        this, windowTitle(),
        tr("Failed to set %1 as the Windows default IME.")
            .arg(GuiUtil::ProductName()));
  }
#endif  // _WIN32
}

void ConfigDialog::RestorePreviousDefaultImeSetting() {
#ifdef _WIN32
  const QMessageBox::StandardButton result = QMessageBox::question(
      this, windowTitle(),
      tr("Restore the previous Windows default IME setting?"),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (result != QMessageBox::Yes) {
    return;
  }

  const int exit_code = RunPowerShellScript(BuildRestoreDefaultImeScript());

  if (exit_code == 0) {
    QMessageBox::information(
        this, windowTitle(),
        tr("The previous Windows default IME setting has been restored."));
  } else if (exit_code == 2) {
    QMessageBox::warning(
        this, windowTitle(),
        tr("No previous Windows default IME setting has been saved."));
  } else {
    QMessageBox::critical(
        this, windowTitle(),
        tr("Failed to restore the previous Windows default IME setting."));
  }
#endif  // _WIN32
}

void ConfigDialog::EnableApplyButton() {
  configDialogButtonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
}

// Catch MouseButtonRelease event to toggle the CheckBoxes
bool ConfigDialog::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::MouseButtonRelease) {
    if (obj == incognitoModeMessage) {
      incognitoModeCheckBox->toggle();
    }
  }
  return QObject::eventFilter(obj, event);
}

}  // namespace gui
}  // namespace mozc
