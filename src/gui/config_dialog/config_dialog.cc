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
#include <QByteArray>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStyle>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdint>
#include <istream>
#include <iterator>
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

int FindComboBoxItemByData(QComboBox *combo_box, const QString &data) {
  if (combo_box == nullptr) {
    return -1;
  }

  for (int i = 0; i < combo_box->count(); ++i) {
    if (combo_box->itemData(i).toString().compare(
            data, Qt::CaseInsensitive) == 0) {
      return i;
    }
  }

  return -1;
}

void AddComboBoxFontItemIfMissing(QComboBox *combo_box,
                                  const QString &font_name) {
  if (combo_box == nullptr || font_name.isEmpty()) {
    return;
  }

  if (FindComboBoxItemByData(combo_box, font_name) < 0) {
    combo_box->addItem(font_name, font_name);
  }
}

void SetComboBoxCurrentFontNameOrAdd(QComboBox *combo_box,
                                     const QString &font_name) {
  if (combo_box == nullptr) {
    return;
  }

  if (font_name.isEmpty()) {
    const int default_index = FindComboBoxItemByData(combo_box, QString());
    if (default_index >= 0) {
      combo_box->setCurrentIndex(default_index);
    }
    return;
  }

  int index = FindComboBoxItemByData(combo_box, font_name);
  if (index < 0) {
    combo_box->addItem(font_name, font_name);
    index = combo_box->count() - 1;
  }

  combo_box->setCurrentIndex(index);
}

bool FontFamilyExists(const QStringList &families, const QString &family) {
  return families.contains(family, Qt::CaseInsensitive);
}

void InitializeCandidateRubyFontComboBox(QComboBox *combo_box) {
  if (combo_box == nullptr) {
    return;
  }

  combo_box->clear();

  combo_box->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  combo_box->setMinimumContentsLength(18);
  combo_box->setMinimumWidth(180);
  combo_box->setMaximumWidth(280);

  // Empty data means the platform/default font.
  combo_box->addItem(
      QCoreApplication::translate("ConfigDialog", "Default"),
      QString());

  QFontDatabase font_database;
  const QStringList all_families = font_database.families();
  const QStringList japanese_families =
      font_database.families(QFontDatabase::Japanese);

  QStringList font_families;

  for (const QString &family : japanese_families) {
    // Skip vertical font aliases such as "@Yu Gothic".
    if (family.startsWith(QLatin1Char('@'))) {
      continue;
    }
    if (!FontFamilyExists(font_families, family)) {
      font_families.append(family);
    }
  }

  // Some Japanese UI fonts may not be returned by the Japanese writing-system
  // query on every environment.  Add known useful families if they exist, then
  // sort the final list so users can find fonts by name.
  const QStringList supplemental_families = {
      QString::fromUtf8("BIZ UDPGothic"),
      QString::fromUtf8("BIZ UDGothic"),
      QString::fromUtf8("Meiryo"),
      QString::fromUtf8("Meiryo UI"),
      QString::fromUtf8("MS Gothic"),
      QString::fromUtf8("MS PGothic"),
      QString::fromUtf8("Noto Sans CJK JP"),
      QString::fromUtf8("Noto Sans JP"),
      QString::fromUtf8("Yu Gothic"),
      QString::fromUtf8("Yu Gothic UI"),
  };

  for (const QString &family : supplemental_families) {
    if (FontFamilyExists(all_families, family) &&
        !FontFamilyExists(font_families, family)) {
      font_families.append(family);
    }
  }

  font_families.sort(Qt::CaseInsensitive);

  for (const QString &family : font_families) {
    AddComboBoxFontItemIfMissing(combo_box, family);
  }

  combo_box->setCurrentIndex(0);
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
      initial_ime_hot_key_disabled_(false),
      initial_startup_enabled_(false),
      suppress_apply_button_update_(true),
      initial_preedit_method_(0),
      initial_use_keyboard_to_change_preedit_method_(false),
      initial_use_mode_indicator_(true),
      initial_windows_ime_icon_style_(
          static_cast<int>(config::Config::WINDOWS_IME_ICON_DEFAULT)) {
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

  Qt::WindowFlags flags = windowFlags();
  flags &= ~Qt::WindowContextHelpButtonHint;
  setWindowFlags(flags);
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

  liveConversionMinKeyLengthSpinBox->setRange(1, 20);
  liveConversionMinKeyLengthSpinBox->setSingleStep(1);
  liveConversionMinKeyLengthSpinBox->setSuffix(QString::fromUtf8(" 文字"));

  zenzLiveCorrectionDelaySpinBox->setRange(0, 5000);
  zenzLiveCorrectionDelaySpinBox->setSingleStep(100);
  zenzLiveCorrectionDelaySpinBox->setSuffix(QString::fromUtf8(" ms"));
  zenzLiveCorrectionDelaySpinBox->setSpecialValueText(QString::fromUtf8("即時"));

  zenzLiveCorrectionMinKeyLengthSpinBox->setRange(2, 20);
  zenzLiveCorrectionMinKeyLengthSpinBox->setSingleStep(1);
  zenzLiveCorrectionMinKeyLengthSpinBox->setSuffix(QString::fromUtf8(" 文字"));

  zenzLiveCorrectionProfileLineEdit->setMaxLength(128);
  zenzLiveCorrectionTopicLineEdit->setMaxLength(128);
  zenzLiveCorrectionStyleLineEdit->setMaxLength(128);
  zenzLiveCorrectionSettingsLineEdit->setMaxLength(128);

  zenzLiveCorrectionRightContextLengthSpinBox->setRange(0, 128);
  zenzLiveCorrectionRightContextLengthSpinBox->setSingleStep(1);
  zenzLiveCorrectionRightContextLengthSpinBox->setSuffix(
      QString::fromUtf8(" 文字"));
  zenzLiveCorrectionRightContextLengthSpinBox->setSpecialValueText(
      QString::fromUtf8("使わない"));

  zenzFeedbackAutoBlockRejectThresholdSpinBox->setRange(1, 999);
  zenzFeedbackAutoBlockRejectThresholdSpinBox->setSingleStep(1);
  zenzFeedbackAutoBlockRejectThresholdSpinBox->setSuffix(
      QString::fromUtf8(" 回"));

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

  InitializeWindowsImeIconStyleControls();
  InitializeRendererAppearanceControls();

#ifndef _WIN32
  // Mode indicator is available only on Windows.
  useModeIndicator->hide();

  // Preedit display color customization is available only on Windows TSF.
  preeditDisplayColorGroupBox->hide();
#endif  // !_WIN32

#if !defined(_WIN32) && !defined(__APPLE__)
  // Detailed renderer appearance controls are supported by the Windows and
  // macOS desktop renderers.
  useDarkModeCandidateWindow->hide();
  candidateRubyFontLabel->hide();
  candidateRubyFontComboBox->hide();
  showLiveConversionRubyWindow->hide();
#endif  // !defined(_WIN32) && !defined(__APPLE__)

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
  QObject::connect(zenzLiveCorrectionRightContextCheckBox,
                   SIGNAL(stateChanged(int)), this,
                   SLOT(SelectZenzRightContextSetting(int)));
  QObject::connect(zenzFeedbackLearningCheckBox,
                   SIGNAL(stateChanged(int)), this,
                   SLOT(SelectZenzFeedbackLearningSetting(int)));
  QObject::connect(zenzFeedbackAutoBlockCheckBox,
                   SIGNAL(stateChanged(int)), this,
                   SLOT(SelectZenzFeedbackLearningSetting(int)));
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

  InitializeCandidateRubyFontComboBox(candidateRubyFontComboBox);

  // Event handlers to update 'Apply' button state.
  Connect(findChildren<QCheckBox *>(), SIGNAL(stateChanged(int)), this,
          SLOT(EnableApplyButton()));
  Connect(findChildren<QComboBox *>(), SIGNAL(currentIndexChanged(int)), this,
          SLOT(EnableApplyButton()));
  Connect(findChildren<QSpinBox *>(), SIGNAL(valueChanged(int)), this,
          SLOT(EnableApplyButton()));
  Connect(findChildren<QLineEdit *>(), SIGNAL(textEdited(QString)), this,
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

  // QScrollArea breaks the minimum-width chain from its scroll widget to the
  // surrounding dialog.  Synchronize the Advanced tab's minimum width only
  // after translated labels, config-backed table contents, and all
  // platform-specific visibility have reached their final initial state.
  // Keep an as-needed horizontal scrollbar as a safety valve for unusual font,
  // translation, or accessibility configurations.
  inputSupportContentLayout->invalidate();
  inputSupportContentLayout->activate();
  const int input_support_minimum_width =
      inputSupportScrollAreaWidgetContents->minimumSizeHint().width();
  if (input_support_minimum_width > 0) {
    const int scrollbar_extent = inputSupportScrollArea->style()->pixelMetric(
        QStyle::PM_ScrollBarExtent, nullptr, inputSupportScrollArea);
    inputSupportScrollArea->setMinimumWidth(
        input_support_minimum_width + scrollbar_extent +
        2 * inputSupportScrollArea->frameWidth());
  }

  RecordCurrentStateAsApplied();
  suppress_apply_button_update_ = false;
  EnableApplyButton();
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

  const bool was_suppressed = suppress_apply_button_update_;
  suppress_apply_button_update_ = true;
  ConvertFromProto(config);
  UpdateDependentControls();
  suppress_apply_button_update_ = was_suppressed;
  initial_preedit_method_ = static_cast<int>(config.preedit_method());
  initial_use_keyboard_to_change_preedit_method_ =
      config.use_keyboard_to_change_preedit_method();
  initial_use_mode_indicator_ = config.use_mode_indicator();
  initial_windows_ime_icon_style_ =
      static_cast<int>(config.windows_ime_icon_style());

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

#ifdef _WIN32
namespace {
// Forward declarations for Windows TSF profile icon update.  Definitions are
// placed near other local helpers below.
QString TrConfigDialog(const char* source);
bool IsTsfProfileIconCurrent(config::Config::WindowsImeIconStyle style);
bool ApplyTsfProfileIconStyle(config::Config::WindowsImeIconStyle style,
                              bool* refresh_succeeded);
bool RefreshTsfProfileIcon();
}  // namespace
#endif  // _WIN32

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

#ifdef _WIN32
  const bool windows_ime_icon_style_changed =
      (initial_windows_ime_icon_style_ !=
       static_cast<int>(config.windows_ime_icon_style()));
  const bool windows_ime_icon_registry_mismatch =
      !IsTsfProfileIconCurrent(config.windows_ime_icon_style());
#endif  // _WIN32

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

#ifdef _WIN32
  if (windows_ime_icon_style_changed || windows_ime_icon_registry_mismatch) {
    const bool update_succeeded = ApplyTsfProfileIconStyle(
        config.windows_ime_icon_style(), nullptr);

    QMessageBox::information(
        this, windowTitle(),
        update_succeeded
            ? TrConfigDialog(
                  "IME icon setting has been applied to Windows. "
                  "The taskbar and IME list icons may not update immediately. "
                  "If they do not update, restart Windows.")
            : TrConfigDialog("IME icon setting has been saved, but it could not be "
                             "applied to Windows. If you canceled administrator approval, "
                             "click Apply again."));
    initial_windows_ime_icon_style_ =
        static_cast<int>(config.windows_ime_icon_style());
  }
#endif  // _WIN32

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
    return false;
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

  base_config_ = config;
  RecordCurrentStateAsApplied();
  EnableApplyButton();

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
constexpr uint32_t kDefaultLiveConversionMinKeyLength = 2;
constexpr uint32_t kMinLiveConversionMinKeyLength = 1;
constexpr uint32_t kMaxLiveConversionMinKeyLength = 20;
constexpr uint32_t kDefaultZenzLiveCorrectionDelayMsec = 1000;
constexpr uint32_t kMaxZenzLiveCorrectionDelayMsec = 5000;
constexpr uint32_t kDefaultZenzLiveCorrectionMinKeyLength = 2;
constexpr uint32_t kMinZenzLiveCorrectionMinKeyLength = 2;
constexpr uint32_t kMaxZenzLiveCorrectionMinKeyLength = 20;
constexpr uint32_t kMaxZenzLiveCorrectionRightContextLength = 128;
constexpr uint32_t kDefaultZenzAutoBlockRejectThreshold = 3;
constexpr uint32_t kMinZenzAutoBlockRejectThreshold = 1;
constexpr uint32_t kMaxZenzAutoBlockRejectThreshold = 999;

constexpr uint32_t kDefaultInputPreeditTextColor = 0xff5000;
constexpr uint32_t kDefaultInputPreeditBackgroundColor = 0xffffcc;
constexpr uint32_t kDefaultInputPreeditUnderlineColor = 0xff0000;

constexpr uint32_t kDefaultTargetPreeditTextColor = 0x000000;
constexpr uint32_t kDefaultTargetPreeditBackgroundColor = 0xddeeff;
constexpr uint32_t kDefaultTargetPreeditUnderlineColor = 0x0066ff;

constexpr int kDefaultRendererFontWeight = 400;
constexpr int kMinRendererFontWeight = 100;
constexpr int kMaxRendererFontWeight = 900;
constexpr int kRendererFontWeightStep = 100;

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

struct CandidateWindowPaletteDefaults {
  uint32_t background_color;
  uint32_t text_color;
  uint32_t selected_background_color;
  uint32_t selected_border_color;
  uint32_t border_color;
  uint32_t shortcut_text_color;
  uint32_t shortcut_background_color;
  uint32_t description_text_color;
  uint32_t footer_text_color;
  uint32_t footer_background_color;
  uint32_t footer_border_color;
  uint32_t scrollbar_background_color;
  uint32_t scrollbar_indicator_color;
};

struct RubyWindowPaletteDefaults {
  uint32_t background_color;
  uint32_t text_color;
  uint32_t border_color;
};

constexpr CandidateWindowPaletteDefaults kLightCandidatePalette = {
    0xffffff, 0x000000, 0xd1eaff, 0x7facdd, 0x969696,
    0x777777, 0xf3f4ff, 0x888888, 0x4c4c4c, 0xffffff,
    0x606060, 0xe0e0e0, 0x7590b8};

constexpr CandidateWindowPaletteDefaults kDarkCandidatePalette = {
    0x181b20, 0xe6edf3, 0x242b34, 0x3f4b59, 0x323840,
    0x96a0aa, 0x181b20, 0x8b949e, 0xb7c0c9, 0x161a1f,
    0x2a3037, 0x1d2228, 0x4b5766};

constexpr RubyWindowPaletteDefaults kLightRubyPalette = {
    0xffffff, 0x000000, 0x969696};
constexpr RubyWindowPaletteDefaults kDarkRubyPalette = {
    0x181b20, 0xe6edf3, 0x323840};

constexpr const char* kCandidatePaletteButtonNames[] = {
    "BackgroundColorButton", "TextColorButton", "SelectedBackgroundColorButton",
    "SelectedBorderColorButton", "BorderColorButton", "ShortcutTextColorButton",
    "ShortcutBackgroundColorButton", "DescriptionTextColorButton",
    "FooterTextColorButton", "FooterBackgroundColorButton",
    "FooterBorderColorButton", "ScrollbarBackgroundColorButton",
    "ScrollbarIndicatorColorButton"};

constexpr const char* kRubyPaletteButtonNames[] = {
    "BackgroundColorButton", "TextColorButton", "BorderColorButton"};

QComboBox* FindComboBox(const QObject* parent, const char* name) {
  return parent->findChild<QComboBox*>(QString::fromLatin1(name));
}

QSpinBox* FindSpinBox(const QObject* parent, const char* name) {
  return parent->findChild<QSpinBox*>(QString::fromLatin1(name));
}
QString TrConfigDialog(const char* source) {
  return QCoreApplication::translate("ConfigDialog", source);
}

int NormalizeRendererFontWeight(uint32_t weight) {
  const uint32_t clamped_weight =
      std::clamp(weight, static_cast<uint32_t>(kMinRendererFontWeight),
                 static_cast<uint32_t>(kMaxRendererFontWeight));
  const int clamped = static_cast<int>(clamped_weight);
  return ((clamped + kRendererFontWeightStep / 2) /
          kRendererFontWeightStep) *
         kRendererFontWeightStep;
}

void InitializeRendererFontWeightComboBox(QComboBox* combo) {
  if (combo == nullptr) {
    return;
  }
  combo->addItem(TrConfigDialog("Thin (100)"), 100);
  combo->addItem(TrConfigDialog("Extra Light (200)"), 200);
  combo->addItem(TrConfigDialog("Light (300)"), 300);
  combo->addItem(TrConfigDialog("Regular (400)"), 400);
  combo->addItem(TrConfigDialog("Medium (500)"), 500);
  combo->addItem(TrConfigDialog("SemiBold (600)"), 600);
  combo->addItem(TrConfigDialog("Bold (700)"), 700);
  combo->addItem(TrConfigDialog("Extra Bold (800)"), 800);
  combo->addItem(TrConfigDialog("Black (900)"), 900);
  const int default_index = combo->findData(kDefaultRendererFontWeight);
  combo->setCurrentIndex(default_index >= 0 ? default_index : 0);
}

QPushButton* FindButton(const QObject* parent, const QString& name) {
  return parent->findChild<QPushButton*>(name);
}

void SetComboCurrentData(QComboBox* combo, int value) {
  if (combo == nullptr) {
    return;
  }
  const int index = combo->findData(value);
  combo->setCurrentIndex(index >= 0 ? index : 0);
}

int GetComboCurrentData(const QComboBox* combo, int default_value) {
  if (combo == nullptr) {
    return default_value;
  }
  const QVariant data = combo->currentData();
  return data.isValid() ? data.toInt() : default_value;
}

void SetCandidatePaletteButtons(QObject* parent, const QString& prefix,
                                const CandidateWindowPaletteDefaults& palette) {
  const uint32_t values[] = {
      palette.background_color,
      palette.text_color,
      palette.selected_background_color,
      palette.selected_border_color,
      palette.border_color,
      palette.shortcut_text_color,
      palette.shortcut_background_color,
      palette.description_text_color,
      palette.footer_text_color,
      palette.footer_background_color,
      palette.footer_border_color,
      palette.scrollbar_background_color,
      palette.scrollbar_indicator_color,
  };
  for (size_t i = 0; i < std::size(kCandidatePaletteButtonNames); ++i) {
    SetColorButton(FindButton(parent, prefix + kCandidatePaletteButtonNames[i]),
                   values[i]);
  }
}

CandidateWindowPaletteDefaults GetCandidatePaletteButtons(
    const QObject* parent, const QString& prefix,
    const CandidateWindowPaletteDefaults& defaults) {
  CandidateWindowPaletteDefaults palette = defaults;
  uint32_t* values[] = {
      &palette.background_color,
      &palette.text_color,
      &palette.selected_background_color,
      &palette.selected_border_color,
      &palette.border_color,
      &palette.shortcut_text_color,
      &palette.shortcut_background_color,
      &palette.description_text_color,
      &palette.footer_text_color,
      &palette.footer_background_color,
      &palette.footer_border_color,
      &palette.scrollbar_background_color,
      &palette.scrollbar_indicator_color,
  };
  for (size_t i = 0; i < std::size(kCandidatePaletteButtonNames); ++i) {
    *values[i] = GetColorButtonRgb(
        FindButton(parent, prefix + kCandidatePaletteButtonNames[i]),
        *values[i]);
  }
  return palette;
}

void SetRubyPaletteButtons(QObject* parent, const QString& prefix,
                           const RubyWindowPaletteDefaults& palette) {
  const uint32_t values[] = {palette.background_color, palette.text_color,
                             palette.border_color};
  for (size_t i = 0; i < std::size(kRubyPaletteButtonNames); ++i) {
    SetColorButton(FindButton(parent, prefix + kRubyPaletteButtonNames[i]),
                   values[i]);
  }
}

RubyWindowPaletteDefaults GetRubyPaletteButtons(
    const QObject* parent, const QString& prefix,
    const RubyWindowPaletteDefaults& defaults) {
  RubyWindowPaletteDefaults palette = defaults;
  uint32_t* values[] = {&palette.background_color, &palette.text_color,
                        &palette.border_color};
  for (size_t i = 0; i < std::size(kRubyPaletteButtonNames); ++i) {
    *values[i] = GetColorButtonRgb(
        FindButton(parent, prefix + kRubyPaletteButtonNames[i]), *values[i]);
  }
  return palette;
}

void SetCandidatePaletteButtonsFromProto(
    QObject* parent, const QString& prefix,
    const config::Config::CandidateWindowColorPalette& proto) {
  SetCandidatePaletteButtons(
      parent, prefix,
      {proto.background_color(), proto.text_color(),
       proto.selected_background_color(), proto.selected_border_color(),
       proto.border_color(), proto.shortcut_text_color(),
       proto.shortcut_background_color(), proto.description_text_color(),
       proto.footer_text_color(), proto.footer_background_color(),
       proto.footer_border_color(), proto.scrollbar_background_color(),
       proto.scrollbar_indicator_color()});
}

void SetRubyPaletteButtonsFromProto(
    QObject* parent, const QString& prefix,
    const config::Config::RubyWindowColorPalette& proto) {
  SetRubyPaletteButtons(parent, prefix,
                        {proto.background_color(), proto.text_color(),
                         proto.border_color()});
}

void SaveCandidatePaletteToProto(
    const QObject* parent, const QString& prefix,
    config::Config::CandidateWindowColorPalette* proto) {
  const CandidateWindowPaletteDefaults palette =
      GetCandidatePaletteButtons(parent, prefix, kLightCandidatePalette);
  proto->set_background_color(palette.background_color);
  proto->set_text_color(palette.text_color);
  proto->set_selected_background_color(palette.selected_background_color);
  proto->set_selected_border_color(palette.selected_border_color);
  proto->set_border_color(palette.border_color);
  proto->set_shortcut_text_color(palette.shortcut_text_color);
  proto->set_shortcut_background_color(palette.shortcut_background_color);
  proto->set_description_text_color(palette.description_text_color);
  proto->set_footer_text_color(palette.footer_text_color);
  proto->set_footer_background_color(palette.footer_background_color);
  proto->set_footer_border_color(palette.footer_border_color);
  proto->set_scrollbar_background_color(palette.scrollbar_background_color);
  proto->set_scrollbar_indicator_color(palette.scrollbar_indicator_color);
}

void SaveRubyPaletteToProto(const QObject* parent, const QString& prefix,
                            config::Config::RubyWindowColorPalette* proto) {
  const RubyWindowPaletteDefaults palette =
      GetRubyPaletteButtons(parent, prefix, kLightRubyPalette);
  proto->set_background_color(palette.background_color);
  proto->set_text_color(palette.text_color);
  proto->set_border_color(palette.border_color);
}

QString ToQString(absl::string_view s) {
  return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

QString FeedbackReasonLabel(absl::string_view reason) {
  if (reason == "feedback_preferred") {
    return QString::fromUtf8("優先スコアあり");
  }
  if (reason == "feedback_hard_rejected") {
    return QString::fromUtf8("手動ブロック中");
  }
  if (reason == "feedback_auto_blocked") {
    return QString::fromUtf8("自動ブロック中");
  }
  if (reason == "feedback_reject_count_dominant") {
    return QString::fromUtf8("却下数優勢");
  }
  if (reason == "feedback_rejected" || reason == "feedback_downgraded") {
    return QString::fromUtf8("却下スコアあり");
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

void ShowZenzFeedbackManagementDialog(QWidget* parent,
                                      const config::Config& current_config) {
  QDialog dialog(parent);
  dialog.setWindowTitle(QString::fromUtf8("Zenz 学習データの管理"));
  dialog.resize(760, 440);

  session::ZenzFeedbackStore store;

  session::ZenzFeedbackAutoBlockPolicy auto_block_policy;
  auto_block_policy.enabled =
      current_config.use_zenz_auto_block_rejected_correction();
  auto_block_policy.reject_threshold =
      static_cast<int>(current_config.zenz_auto_block_reject_threshold());

  QVBoxLayout* root_layout = new QVBoxLayout(&dialog);

  QWidget* description_widget = new QWidget(&dialog);
  QHBoxLayout* description_layout = new QHBoxLayout(description_widget);
  description_layout->setContentsMargins(0, 0, 0, 0);
  description_layout->setSpacing(8);

  QLabel* description_label = new QLabel(
      QString::fromUtf8(
          "Zenz 補正の学習データを管理します。"
          "ここでは、補正結果として保存された読みと候補の記録を"
          "安全に確認・削除できます。"),
      &dialog);
  description_label->setWordWrap(true);

  QPushButton* details_button =
      new QPushButton(QString::fromUtf8("詳しく..."), &dialog);
  details_button->setFixedWidth(84);
  details_button->setToolTip(QString::fromUtf8(
      "状態ラベル、Zenz 学習の記録条件、通常の変換履歴との違いを表示します"));

  description_layout->addWidget(description_label, 1);
  description_layout->addWidget(details_button, 0, Qt::AlignTop);
  root_layout->addWidget(description_widget);

  QObject::connect(details_button, &QPushButton::clicked,
                   &dialog, [&]() {
                     ShowJapaneseInformation(
                         &dialog, dialog.windowTitle(),
                         QString::fromUtf8(
                             "【この画面で扱うデータ】\n"
                             "この画面で扱うのは、Zenz 補正が実際に表示・反映されたときの"
                             "読み全体、補正後の候補、文脈クラス、採用/却下の記録です。"
                             "通常変換だけを操作した履歴は、この Zenz 学習データの"
                             "採用数・却下数・スコアには入りません。\n\n"
                             "Zenz 学習データは full-sequence 単位です。"
                             "単語や文節ごとの学習ではなく、同じ読み全体、同じ文脈クラス、"
                             "同じ補正結果の組み合わせごとに集計します。"
                             "左文脈そのものは保存せず、empty / japanese_only / "
                             "mixed_japanese_ascii / sensitive_like などの"
                             "非可逆な文脈クラスだけを保存します。\n\n"
                             "【いつ採用として記録されるか】\n"
                             "Zenz 補正が表示され、その補正結果を Enter や"
                             "句読点・記号の単打確定などでそのまま確定した場合、"
                             "採用候補として一時保留されます。"
                             "その後、次の実テキスト入力までに Backspace / Escape / "
                             "Revert / Undo などで取り消されなかった場合だけ、"
                             "採用として保存されます。"
                             "Zenz 補正が表示されただけでは保存されません。\n\n"
                             "【いつ却下として記録されるか】\n"
                             "Zenz 補正が表示された後に Space や候補移動などで"
                             "通常変換へ戻り、最終的に Zenz 補正とは異なる値で"
                             "確定された場合、その Zenz 補正は却下として保存されます。"
                             "これは通常候補を削除する命令ではなく、次回以降の"
                             "候補順位や保存済み Zenz feedback による即時補正を調整するための"
                             "弱いマイナス信号です。\n\n"
                             "【記録されない操作】\n"
                             "Zenz 補正が走っていないときの通常変換、通常候補の選択、"
                             "通常変換の確定は、この画面の Zenz 学習スコアには影響しません。"
                             "また、Zenz が通常 Mozc と同じ値を返した場合、"
                             "出力検証や採用ポリシーで不採用になった場合、"
                             "password / privacy gate で止められた場合も、"
                             "表示済み Zenz 補正としては扱われません。\n\n"
                             "【状態ラベルの意味】\n"
                             "優先スコアあり: 採用・却下を重み付きで見た結果、"
                             "この Zenz 補正を優先候補や保存済み feedback による即時補正として"
                             "再利用できる状態です。\n"
                             "却下スコアあり: 却下履歴により順位を下げる信号があります。"
                             "ただし、手動ブロックや自動ブロックではありません。\n"
                             "却下数優勢: 同じ読み全体・同じ文脈クラス・同じ補正結果で、"
                             "通常却下回数が採用回数を上回っています。"
                             "この場合、auto-block が OFF でも、Zenz feedback による"
                             "優先候補や保存済み feedback による即時補正としては使いません。"
                             "ただし hard block ではないため、Zenz が新しく同じ補正を"
                             "返すこと自体や、通常 Mozc 候補を消すことはありません。\n"
                             "自動ブロック中: auto-block が ON で、通常却下回数が"
                             "設定したしきい値に達しています。TSV に hard reject を"
                             "固定保存するのではなく、現在の ON/OFF としきい値から"
                             "動的に判定します。\n"
                             "手動ブロック中: この画面で明示的にブロックされた状態です。"
                             "解除したい場合は、該当エントリを削除して必要に応じて"
                             "再学習してください。\n"
                             "中立: 優先にもブロックにも使うだけの有効な信号がない状態です。\n\n"
                             "【通常の変換履歴との違い】\n"
                             "Zenz の結果を確定した場合、条件によっては通常の変換履歴にも"
                             "反映されます。また、安全に判断できる場合は、直前の通常 Mozc "
                             "ライブ変換文節列へ逆投影し、文節列全体を通常変換履歴に近い形で"
                             "反映します。このとき、Zenz が実際に直した文節だけを"
                             "強い選択履歴として扱います。\n\n"
                             "そのため、この画面で Zenz 学習データを削除しても、"
                             "通常の変換履歴にすでに反映された内容は削除されません。"
                             "通常の変換履歴を消したい場合は、設定画面の辞書タブ内にある"
                             "学習履歴のクリアを使用してください。"));
                   });

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
                                   << QString::fromUtf8("状態"));
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
  QPushButton* block_button =
      new QPushButton(QString::fromUtf8("この補正をブロック"), &dialog);
  block_button->setToolTip(QString::fromUtf8(
      "選択した Zenz 補正を今後の補正や候補順位に使われにくくします"));
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
  button_layout->addWidget(block_button);
  button_layout->addWidget(delete_button);
  button_layout->addWidget(clear_button);
  button_layout->addStretch();
  button_layout->addWidget(close_buttons);
  root_layout->addLayout(button_layout);

  auto selected_row_is_hard_rejected = [&]() -> bool {
    const int row = table->currentRow();
    if (row < 0) {
      return false;
    }
    QTableWidgetItem* reason_item = table->item(row, 5);
    if (reason_item == nullptr) {
      return false;
    }
    return reason_item->data(Qt::UserRole).toString() ==
           QStringLiteral("feedback_hard_rejected");
  };

  auto update_selection_buttons = [&]() {
    const bool has_selected_row = table->currentRow() >= 0;
    delete_button->setEnabled(has_selected_row);

    if (!has_selected_row) {
      block_button->setText(QString::fromUtf8("この補正をブロック"));
      block_button->setEnabled(false);
      block_button->setToolTip(QString::fromUtf8(
          "選択した Zenz 補正を今後の補正や候補順位に使われにくくします"));
      return;
    }

    if (selected_row_is_hard_rejected()) {
      block_button->setText(QString::fromUtf8("ブロック済み"));
      block_button->setEnabled(false);
      block_button->setToolTip(QString::fromUtf8(
          "この Zenz 補正はすでにブロックされています。"
          "解除する場合は「選択項目を削除」を使用します"));
      return;
    }

    block_button->setText(QString::fromUtf8("この補正をブロック"));
    block_button->setEnabled(true);
    block_button->setToolTip(QString::fromUtf8(
        "選択した Zenz 補正を今後の補正や候補順位に使われにくくします"));
  };

  auto reload_table = [&]() {
    const QString filter = search_edit->text();
    const std::vector<session::ZenzFeedbackEntry> entries =
        store.ListEntries(auto_block_policy);

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
      table->item(row, 5)->setData(Qt::UserRole, ToQString(entry.reason));

      ++visible_count;
    }

    table->resizeColumnsToContents();

    const QString auto_block_status =
        auto_block_policy.enabled
            ? QString::fromUtf8(" / 自動ブロックしきい値 %1 回")
                  .arg(auto_block_policy.reject_threshold)
            : QString::fromUtf8(" / 自動ブロック OFF");
    status_label->setText(
        QString::fromUtf8("表示 %1 件 / 全 %2 件%3")
            .arg(visible_count)
            .arg(static_cast<int>(entries.size()))
            .arg(auto_block_status));

    export_button->setEnabled(!entries.empty());
    clear_button->setEnabled(!entries.empty());
    update_selection_buttons();
  };

  QObject::connect(table, &QTableWidget::itemSelectionChanged,
                   &dialog, [&]() {
                     update_selection_buttons();
                   });

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

  QObject::connect(block_button, &QPushButton::clicked,
                   &dialog, [&]() {
                     const int row = table->currentRow();
                     if (row < 0 || selected_row_is_hard_rejected()) {
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
                         QString::fromUtf8("選択した Zenz 補正をブロックしますか？"));
                     message_box.setInformativeText(
                         QString::fromUtf8(
                             "同じ読みと候補の組み合わせは、今後の Zenz 補正や"
                             "候補順位に使われにくくなります。\n\n"
                             "解除したい場合は、この学習エントリを削除してから"
                             "必要に応じて再学習してください。\n\n"
                             "読み: %1\n候補: %2\n文脈クラス: %3")
                             .arg(key, value, context_class));

                     QPushButton* block_confirm_button =
                         message_box.addButton(QString::fromUtf8("ブロック"),
                                               QMessageBox::DestructiveRole);
                     QPushButton* cancel_button =
                         message_box.addButton(QString::fromUtf8("キャンセル"),
                                               QMessageBox::RejectRole);

                     message_box.setDefaultButton(cancel_button);
                     message_box.exec();

                     if (message_box.clickedButton() != block_confirm_button) {
                       return;
                     }

                     store.RecordRejected(
                         key.toUtf8().constData(),
                         context_class.toUtf8().constData(),
                         value.toUtf8().constData(),
                         "hard_reject");

                     reload_table();

                     ShowJapaneseInformation(
                         &dialog, dialog.windowTitle(),
                         QString::fromUtf8(
                             "選択した Zenz 補正をブロックしました。"));
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
#ifdef _WIN32

constexpr int kTsfProfileIconIndexDefault = 0;

// These values are TSF language profile icon indices in mozc_tip*.dll, not
// resource IDs.  With the current tip_resource.rc order, IDI_IMM32 is exposed
// as index 0, and the simple monochrome icons added immediately after it are
// exposed as indices 15 and 16 on the generated mozc_tip32.dll in the current
// Windows package.  Keep these values in sync with resource-order validation
// whenever the TIP icon resource list is changed.
constexpr int kTsfProfileIconIndexSimpleBlack = 15;
constexpr int kTsfProfileIconIndexSimpleWhite = 16;

constexpr wchar_t kTsfProfileSubKey[] =
    L"SOFTWARE\\Microsoft\\CTF\\TIP\\"
    L"{10A67BC8-22FA-4A59-90DC-2546652C56BF}\\"
    L"LanguageProfile\\0x00000411\\"
    L"{186F700C-71CF-43FE-A00E-AACB1D9E6D3D}";

bool EndsWithCaseInsensitive(const std::wstring& text,
                             const std::wstring& suffix) {
  if (text.size() < suffix.size()) {
    return false;
  }
  return ::CompareStringOrdinal(
             text.c_str() + text.size() - suffix.size(),
             static_cast<int>(suffix.size()), suffix.c_str(),
             static_cast<int>(suffix.size()), TRUE) == CSTR_EQUAL;
}

int GetTsfProfileIconIndexForImeIconStyle(
    config::Config::WindowsImeIconStyle style) {
  switch (style) {
    case config::Config::WINDOWS_IME_ICON_MONOCHROME_BLACK:
      return kTsfProfileIconIndexSimpleBlack;
    case config::Config::WINDOWS_IME_ICON_MONOCHROME_WHITE:
      return kTsfProfileIconIndexSimpleWhite;
    case config::Config::WINDOWS_IME_ICON_DEFAULT:
    default:
      return kTsfProfileIconIndexDefault;
  }
}

bool ReadTsfProfileIconState(std::wstring* icon_file,
                             DWORD* icon_index) {
  using RegGetValueWFunction = LSTATUS(WINAPI *)(HKEY, LPCWSTR, LPCWSTR,
                                                 DWORD, LPDWORD, PVOID,
                                                 LPDWORD);

  HMODULE advapi32 = ::LoadLibraryW(L"Advapi32.dll");
  if (advapi32 == nullptr) {
    return false;
  }

  const auto reg_get_value = reinterpret_cast<RegGetValueWFunction>(
      ::GetProcAddress(advapi32, "RegGetValueW"));
  if (reg_get_value == nullptr) {
    ::FreeLibrary(advapi32);
    return false;
  }

  DWORD current_icon_index = 0;
  DWORD icon_index_size = sizeof(current_icon_index);
  const LSTATUS icon_index_status = reg_get_value(
      HKEY_LOCAL_MACHINE, kTsfProfileSubKey, L"IconIndex",
      RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr,
      &current_icon_index, &icon_index_size);

  std::vector<wchar_t> icon_file_buffer(32768);
  DWORD icon_file_size = static_cast<DWORD>(
      icon_file_buffer.size() * sizeof(wchar_t));
  const LSTATUS icon_file_status = reg_get_value(
      HKEY_LOCAL_MACHINE, kTsfProfileSubKey, L"IconFile",
      RRF_RT_ANY | RRF_NOEXPAND | RRF_SUBKEY_WOW6464KEY, nullptr,
      icon_file_buffer.data(), &icon_file_size);

  ::FreeLibrary(advapi32);

  if (icon_index_status != ERROR_SUCCESS ||
      icon_file_status != ERROR_SUCCESS) {
    return false;
  }

  *icon_index = current_icon_index;
  *icon_file = icon_file_buffer.data();
  return true;
}

bool IsTsfProfileIconCurrent(config::Config::WindowsImeIconStyle style) {
  std::wstring icon_file;
  DWORD icon_index = 0;
  if (!ReadTsfProfileIconState(&icon_file, &icon_index)) {
    return false;
  }

  return icon_index ==
             static_cast<DWORD>(GetTsfProfileIconIndexForImeIconStyle(style)) &&
         EndsWithCaseInsensitive(icon_file, L"\\mozc_tip32.dll");
}

const wchar_t* GetTsfProfileIconStyleFlagValue(
    config::Config::WindowsImeIconStyle style) {
  switch (style) {
    case config::Config::WINDOWS_IME_ICON_MONOCHROME_BLACK:
      return L"monochrome_black";
    case config::Config::WINDOWS_IME_ICON_MONOCHROME_WHITE:
      return L"monochrome_white";
    case config::Config::WINDOWS_IME_ICON_DEFAULT:
    default:
      return L"default";
  }
}

std::wstring GetCurrentExecutablePath() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length = ::GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return std::wstring();
    }
    if (length < buffer.size() - 1) {
      return std::wstring(buffer.data(), length);
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::wstring GetMozcToolExecutablePath() {
  const std::wstring current_executable = GetCurrentExecutablePath();
  if (current_executable.empty()) {
    return std::wstring();
  }

  constexpr wchar_t kMozcToolFileName[] = L"mozc_tool.exe";
  const size_t separator = current_executable.find_last_of(L"\\/");
  const std::wstring current_file_name =
      separator == std::wstring::npos
          ? current_executable
          : current_executable.substr(separator + 1);

  if (::CompareStringOrdinal(
          current_file_name.c_str(),
          static_cast<int>(current_file_name.size()),
          kMozcToolFileName,
          static_cast<int>(std::char_traits<wchar_t>::length(kMozcToolFileName)),
          TRUE) == CSTR_EQUAL) {
    return current_executable;
  }

  if (separator == std::wstring::npos) {
    LOG(ERROR) << "Cannot resolve mozc_tool.exe path from current executable";
    return std::wstring();
  }

  const std::wstring current_dir = current_executable.substr(0, separator);
  const std::wstring mozc_tool = current_dir + L"\\mozc_tool.exe";
  const DWORD attributes = ::GetFileAttributesW(mozc_tool.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    return mozc_tool;
  }

  LOG(ERROR) << "mozc_tool.exe is not found next to current executable";
  return std::wstring();
}

bool LaunchElevatedTsfProfileIconUpdate(
    config::Config::WindowsImeIconStyle style) {
  const std::wstring executable_path = GetMozcToolExecutablePath();
  if (executable_path.empty()) {
    return false;
  }

  const std::wstring parameters =
      L"--mode=tsf_profile_icon_style_update --windows_ime_icon_style=" +
      std::wstring(GetTsfProfileIconStyleFlagValue(style));

  SHELLEXECUTEINFOW execute_info = {};
  execute_info.cbSize = sizeof(execute_info);
  execute_info.fMask = SEE_MASK_NOCLOSEPROCESS;
  execute_info.lpVerb = L"runas";
  execute_info.lpFile = executable_path.c_str();
  execute_info.lpParameters = parameters.c_str();
  execute_info.nShow = SW_SHOWNORMAL;

  if (!::ShellExecuteExW(&execute_info) ||
      execute_info.hProcess == nullptr) {
    return false;
  }

  ::WaitForSingleObject(execute_info.hProcess, INFINITE);

  DWORD exit_code = 1;
  const bool got_exit_code =
      ::GetExitCodeProcess(execute_info.hProcess, &exit_code);
  ::CloseHandle(execute_info.hProcess);

  return got_exit_code && exit_code == 0;
}

bool ApplyTsfProfileIconStyle(config::Config::WindowsImeIconStyle style,
                              bool* refresh_succeeded) {
  if (refresh_succeeded != nullptr) {
    *refresh_succeeded = false;
  }

  if (!IsTsfProfileIconCurrent(style) &&
      !LaunchElevatedTsfProfileIconUpdate(style)) {
    return false;
  }

  const bool refreshed = RefreshTsfProfileIcon();
  if (refresh_succeeded != nullptr) {
    *refresh_succeeded = refreshed;
  }
  return true;
}

bool ActivateMozkeyTsfProfile(ITfInputProcessorProfileMgr* profile_mgr,
                             const CLSID& mozc_tip_guid,
                             const GUID& mozc_profile_guid,
                             DWORD sleep_msec) {
  if (profile_mgr == nullptr) {
    return false;
  }

  const HRESULT hr = profile_mgr->ActivateProfile(
      TF_PROFILETYPE_INPUTPROCESSOR, 0x0411, mozc_tip_guid,
      mozc_profile_guid, nullptr, TF_IPPMF_FORSESSION);
  ::Sleep(sleep_msec);
  return SUCCEEDED(hr);
}

bool RefreshTsfProfileIconViaInstalledInputProcessorProfile(
    ITfInputProcessorProfileMgr* profile_mgr,
    const CLSID& mozc_tip_guid,
    const GUID& mozc_profile_guid) {
  if (profile_mgr == nullptr) {
    return false;
  }

  IEnumTfInputProcessorProfiles* profiles = nullptr;
  const HRESULT enum_hr = profile_mgr->EnumProfiles(0x0411, &profiles);
  if (FAILED(enum_hr) || profiles == nullptr) {
    return false;
  }

  bool result = false;
  for (;;) {
    TF_INPUTPROCESSORPROFILE profile = {};
    ULONG fetched = 0;
    const HRESULT next_hr = profiles->Next(1, &profile, &fetched);
    if (next_hr != S_OK || fetched != 1) {
      break;
    }

    if (profile.dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR ||
        profile.langid != 0x0411) {
      continue;
    }

    if (::IsEqualGUID(profile.clsid, mozc_tip_guid) &&
        ::IsEqualGUID(profile.guidProfile, mozc_profile_guid)) {
      continue;
    }

    const HRESULT other_hr = profile_mgr->ActivateProfile(
        TF_PROFILETYPE_INPUTPROCESSOR, profile.langid, profile.clsid,
        profile.guidProfile, nullptr, TF_IPPMF_FORSESSION);
    if (FAILED(other_hr)) {
      continue;
    }

    ::Sleep(700);
    if (ActivateMozkeyTsfProfile(profile_mgr, mozc_tip_guid,
                                 mozc_profile_guid, 500)) {
      result = true;
      break;
    }
  }

  profiles->Release();
  return result;
}

bool RefreshTsfProfileIconViaTemporaryKeyboardLayout(
    ITfInputProcessorProfileMgr* profile_mgr,
    const CLSID& mozc_tip_guid,
    const GUID& mozc_profile_guid) {
  if (profile_mgr == nullptr) {
    return false;
  }

  constexpr DWORD kLoadKeyboardFlags =
      KLF_SUBSTITUTE_OK | KLF_NOTELLSHELL;
  const HKL hkl = ::LoadKeyboardLayoutW(L"00000409", kLoadKeyboardFlags);

  CLSID null_clsid = CLSID_NULL;
  GUID null_guid = GUID_NULL;

  HRESULT keyboard_hr = E_FAIL;
  if (hkl != nullptr) {
    keyboard_hr = profile_mgr->ActivateProfile(
        TF_PROFILETYPE_KEYBOARDLAYOUT, 0x0409, null_clsid, null_guid,
        hkl, TF_IPPMF_FORSESSION);
  }

  ::Sleep(500);
  const bool mozc_activated = ActivateMozkeyTsfProfile(
      profile_mgr, mozc_tip_guid, mozc_profile_guid, 300);

  bool unloaded = true;
  if (hkl != nullptr) {
    unloaded = ::UnloadKeyboardLayout(hkl) != FALSE;
  }

  return SUCCEEDED(keyboard_hr) && mozc_activated && unloaded;
}

bool RefreshTsfProfileIcon() {
  HRESULT coinit_result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool should_uninitialize =
      (coinit_result == S_OK || coinit_result == S_FALSE);

  bool result = false;
  if (SUCCEEDED(coinit_result) || coinit_result == RPC_E_CHANGED_MODE) {
    ITfInputProcessorProfileMgr* profile_mgr = nullptr;
    const HRESULT create_hr = ::CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(&profile_mgr));

    if (SUCCEEDED(create_hr) && profile_mgr != nullptr) {
      CLSID mozc_tip_guid = {};
      GUID mozc_profile_guid = {};
      const HRESULT clsid_hr = ::CLSIDFromString(
          L"{10A67BC8-22FA-4A59-90DC-2546652C56BF}", &mozc_tip_guid);
      const HRESULT profile_guid_hr = ::CLSIDFromString(
          L"{186F700C-71CF-43FE-A00E-AACB1D9E6D3D}", &mozc_profile_guid);

      if (SUCCEEDED(clsid_hr) && SUCCEEDED(profile_guid_hr)) {
        result = RefreshTsfProfileIconViaInstalledInputProcessorProfile(
                     profile_mgr, mozc_tip_guid, mozc_profile_guid) ||
                 RefreshTsfProfileIconViaTemporaryKeyboardLayout(
                     profile_mgr, mozc_tip_guid, mozc_profile_guid);
      }

      profile_mgr->Release();
    }
  }

  if (should_uninitialize) {
    ::CoUninitialize();
  }

  return result;
}

#endif  // _WIN32
}  // namespace

void ConfigDialog::InitializeWindowsImeIconStyleControls() {
#ifndef _WIN32
  return;
#else   // _WIN32
  QWidget* group = new QWidget(widgetMisc);
  group->setObjectName(QStringLiteral("windowsImeIconStyleGroup"));
  group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  QVBoxLayout* group_layout = new QVBoxLayout(group);
  group_layout->setSpacing(0);
  group_layout->setContentsMargins(0, 0, 0, 0);

  QWidget* header = new QWidget(group);
  header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  QHBoxLayout* header_layout = new QHBoxLayout(header);
  header_layout->setSpacing(6);
  header_layout->setContentsMargins(9, 9, 9, 9);

  QLabel* title = new QLabel(TrConfigDialog("IME icon"), header);
  title->setObjectName(QStringLiteral("windowsImeIconStyleTitle"));
  header_layout->addWidget(title);

  QFrame* line = new QFrame(header);
  line->setObjectName(QStringLiteral("windowsImeIconStyleLine"));
  line->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  header_layout->addWidget(line);

  group_layout->addWidget(header);

  QWidget* body = new QWidget(group);
  body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  QGridLayout* body_layout = new QGridLayout(body);
  body_layout->setContentsMargins(24, 9, 24, 9);

  QLabel* label = new QLabel(TrConfigDialog("IME icon style"), body);
  label->setObjectName(QStringLiteral("windowsImeIconStyleLabel"));
  label->setMinimumHeight(24);

  QComboBox* combo = new QComboBox(body);
  combo->setObjectName(QStringLiteral("windowsImeIconStyleComboBox"));
  combo->setMinimumHeight(24);
  combo->setMinimumWidth(180);
  combo->setToolTip(TrConfigDialog(
      "Changes the IME icon shown next to the input mode indicator."));
  combo->addItem(TrConfigDialog("Default"),
                 static_cast<int>(
                     config::Config::WINDOWS_IME_ICON_DEFAULT));
  combo->addItem(TrConfigDialog("Monochrome (Black)"),
                 static_cast<int>(
                     config::Config::WINDOWS_IME_ICON_MONOCHROME_BLACK));
  combo->addItem(TrConfigDialog("Monochrome (White)"),
                 static_cast<int>(
                     config::Config::WINDOWS_IME_ICON_MONOCHROME_WHITE));

  body_layout->addWidget(label, 0, 0);
  body_layout->setColumnStretch(1, 1);
  body_layout->addWidget(combo, 0, 2);

  group_layout->addWidget(body);

  QLabel* note = new QLabel(
      TrConfigDialog("* Administrator approval may be required when applying "
                     "this setting. The taskbar and IME list icons may not "
                     "update immediately. If they do not update, restart "
                     "Windows."),
      group);
  note->setObjectName(QStringLiteral("windowsImeIconStyleNote"));
  note->setWordWrap(true);
  note->setContentsMargins(24, 0, 24, 9);
  group_layout->addWidget(note);

  const int insert_index = verticalLayout->indexOf(miscAdministrationWidget);
  verticalLayout->insertWidget(
      insert_index >= 0 ? insert_index : verticalLayout->count(), group);
#endif  // _WIN32
}

// TODO(taku)
// Actually ConvertFromProto and ConvertToProto are almost the same.
// The difference only SET_ and GET_. We would like to unify the twos.
void ConfigDialog::InitializeRendererAppearanceControls() {
#if !defined(_WIN32) && !defined(__APPLE__)
  return;
#endif  // !defined(_WIN32) && !defined(__APPLE__)

  useDarkModeCandidateWindow->hide();
  candidateRubyFontLabel->hide();
  candidateRubyFontComboBox->hide();

  QWidget* group = new QWidget(inputSupportScrollAreaWidgetContents);
  group->setObjectName(QStringLiteral("rendererAppearanceGroupBox"));
  group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  QVBoxLayout* root_layout = new QVBoxLayout(group);
  // Keep the renderer section in the same layout system as the static
  // Advanced-tab groups.  Use layout margins instead of a dialog-wide
  // stylesheet so native checkbox and frame rendering stays untouched.
  root_layout->setContentsMargins(0, 0, 0, 0);
  root_layout->setSpacing(0);

  QWidget* section = new QWidget(group);
  QHBoxLayout* section_layout = new QHBoxLayout(section);
  section_layout->setContentsMargins(9, 9, 9, 9);
  QLabel* section_label =
      new QLabel(tr("Candidate, suggestion, and ruby window appearance"),
                 section);
  QPushButton* reset_button = new QPushButton(tr("Reset"), section);
  reset_button->setObjectName(QStringLiteral("rendererAppearanceResetButton"));
  reset_button->setFixedWidth(64);
  reset_button->setMinimumHeight(24);
  reset_button->setToolTip(tr(
      "Reset candidate, suggestion, and ruby window appearance to defaults"));
  QFrame* section_line = new QFrame(section);
  section_line->setFrameShape(QFrame::HLine);
  section_line->setFrameShadow(QFrame::Sunken);
  section_layout->addWidget(section_label);
  section_layout->addWidget(reset_button);
  section_layout->addWidget(section_line);
  QObject::connect(reset_button, SIGNAL(clicked()), this,
                   SLOT(ResetRendererAppearanceControls()));
  root_layout->addWidget(section);

  QWidget* body = new QWidget(group);
  body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  QVBoxLayout* body_layout = new QVBoxLayout(body);
  body_layout->setContentsMargins(24, 9, 24, 9);
  body_layout->setSpacing(6);
  root_layout->addWidget(body);

  QGridLayout* font_layout = new QGridLayout();
  font_layout->setContentsMargins(0, 0, 0, 0);
  font_layout->setHorizontalSpacing(8);
  font_layout->setVerticalSpacing(0);
  QLabel* font_label =
      new QLabel(tr("Candidate, suggestion, and ruby font"), group);
  font_label->setMinimumHeight(24);
  font_label->setMinimumWidth(220);
  candidateRubyFontComboBox->setParent(group);
  candidateRubyFontComboBox->show();
  candidateRubyFontComboBox->setMinimumHeight(24);
  font_layout->addWidget(font_label, 0, 0);
  font_layout->addWidget(candidateRubyFontComboBox, 0, 1);
  font_layout->setColumnStretch(1, 1);
  body_layout->addLayout(font_layout);

  QGroupBox* font_weight_box = new QGroupBox(tr("Font weight"), group);
  font_weight_box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  QGridLayout* font_weight_layout = new QGridLayout(font_weight_box);
  font_weight_layout->setContentsMargins(8, 8, 8, 8);
  font_weight_layout->setHorizontalSpacing(8);
  font_weight_layout->setVerticalSpacing(4);

  auto add_font_weight_row = [&](int row, const QString& label_text,
                                 const char* object_name) {
    QLabel* label = new QLabel(label_text, font_weight_box);
    label->setMinimumHeight(24);
    font_weight_layout->addWidget(label, row, 0);

    QComboBox* combo = new QComboBox(font_weight_box);
    combo->setObjectName(QString::fromLatin1(object_name));
    combo->setMinimumHeight(24);
    combo->setMinimumWidth(160);
    InitializeRendererFontWeightComboBox(combo);
    font_weight_layout->addWidget(combo, row, 1);
    font_weight_layout->setColumnStretch(0, 1);
    QObject::connect(combo, SIGNAL(currentIndexChanged(int)), this,
                     SLOT(EnableApplyButton()));
  };

  add_font_weight_row(0, tr("Candidate window"),
                      "candidateWindowFontWeightComboBox");
  add_font_weight_row(1, tr("Suggestion window"),
                      "suggestWindowFontWeightComboBox");
  add_font_weight_row(2, tr("Ruby window"),
                      "rubyWindowFontWeightComboBox");
  body_layout->addWidget(font_weight_box);

  QWidget* color_grid_widget = new QWidget(group);
  color_grid_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  QGridLayout* grid = new QGridLayout(color_grid_widget);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(2);
  body_layout->addWidget(color_grid_widget);

  auto add_color_theme_combo = [](QComboBox* combo, bool allow_follow) {
    if (allow_follow) {
      combo->addItem(
          tr("Follow candidate window"),
          static_cast<int>(
              config::Config::RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE));
    }
    combo->addItem(
        tr("Default (Light)"),
        static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_LIGHT));
    combo->addItem(
        tr("Dark"),
        static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_DARK));
    combo->addItem(
        tr("Custom"),
        static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_CUSTOM));
  };

  auto add_row = [&](int row, const QString& title, const char* color_name,
                     bool color_follow, const char* size_name,
                     const char* corner_name, const char* opacity_name,
                     int default_corner_radius, int default_opacity) {
    QLabel* title_label = new QLabel(title, group);
    title_label->setMinimumHeight(24);
    grid->addWidget(title_label, row, 0);

    QComboBox* color_combo = new QComboBox(group);
    color_combo->setObjectName(QString::fromLatin1(color_name));
    add_color_theme_combo(color_combo, color_follow);
    color_combo->setMinimumHeight(24);
    grid->addWidget(color_combo, row, 1);

    QSpinBox* size_spin = new QSpinBox(group);
    size_spin->setObjectName(QString::fromLatin1(size_name));
    size_spin->setRange(80, 200);
    size_spin->setSingleStep(5);
    size_spin->setSuffix(QStringLiteral(" %"));
    size_spin->setValue(100);
    size_spin->setMinimumHeight(24);
    grid->addWidget(size_spin, row, 2);

    QSpinBox* corner_spin = new QSpinBox(group);
    corner_spin->setObjectName(QString::fromLatin1(corner_name));
    corner_spin->setRange(0, 24);
    corner_spin->setSuffix(QStringLiteral(" px"));
    corner_spin->setValue(default_corner_radius);
    corner_spin->setMinimumHeight(24);
    grid->addWidget(corner_spin, row, 3);

    QSpinBox* opacity_spin = new QSpinBox(group);
    opacity_spin->setObjectName(QString::fromLatin1(opacity_name));
    opacity_spin->setRange(20, 100);
    opacity_spin->setSuffix(QStringLiteral(" %"));
    opacity_spin->setValue(default_opacity);
    opacity_spin->setMinimumHeight(24);
    grid->addWidget(opacity_spin, row, 4);

    QObject::connect(color_combo, SIGNAL(currentIndexChanged(int)), this,
                     SLOT(UpdateRendererAppearanceControls()));
    QObject::connect(color_combo, SIGNAL(currentIndexChanged(int)), this,
                     SLOT(EnableApplyButton()));
    QObject::connect(size_spin, SIGNAL(valueChanged(int)), this,
                     SLOT(EnableApplyButton()));
    QObject::connect(corner_spin, SIGNAL(valueChanged(int)), this,
                     SLOT(EnableApplyButton()));
    QObject::connect(opacity_spin, SIGNAL(valueChanged(int)), this,
                     SLOT(EnableApplyButton()));
  };

  grid->addWidget(new QLabel(tr("Target"), group), 0, 0);
  grid->addWidget(new QLabel(tr("Color"), group), 0, 1);
  grid->addWidget(new QLabel(tr("Size"), group), 0, 2);
  grid->addWidget(new QLabel(tr("Corner radius"), group), 0, 3);
  grid->addWidget(new QLabel(tr("Opacity"), group), 0, 4);

  add_row(1, tr("Candidate window"), "candidateWindowColorThemeComboBox",
          false, "candidateWindowSizePercentSpinBox",
          "candidateWindowCornerRadiusSpinBox",
          "candidateWindowOpacityPercentSpinBox", 6, 100);
  add_row(2, tr("Suggestion window"), "suggestWindowColorThemeComboBox", true,
          "suggestWindowSizePercentSpinBox", "suggestWindowCornerRadiusSpinBox",
          "suggestWindowOpacityPercentSpinBox", 6, 100);
  add_row(3, tr("Ruby window"), "rubyWindowColorThemeComboBox", true,
          "rubyWindowSizePercentSpinBox", "rubyWindowCornerRadiusSpinBox",
          "rubyWindowOpacityPercentSpinBox", 9, 90);
  for (int row = 0; row <= 3; ++row) {
    grid->setRowMinimumHeight(row, 24);
    grid->setRowStretch(row, 0);
  }

  body_layout->addSpacing(12);

  QGroupBox* ruby_spacing_box =
      new QGroupBox(tr("Ruby window spacing and gap"), group);
  ruby_spacing_box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  QGridLayout* ruby_spacing_layout = new QGridLayout(ruby_spacing_box);
  ruby_spacing_layout->setContentsMargins(8, 8, 8, 8);
  ruby_spacing_layout->setHorizontalSpacing(8);
  ruby_spacing_layout->setVerticalSpacing(6);

  auto add_ruby_spacing_spin = [&](int row, const QString& label_text,
                                   const QString& tooltip,
                                   const char* object_name, int maximum,
                                   int default_value) {
    QLabel* label = new QLabel(label_text, ruby_spacing_box);
    label->setToolTip(tooltip);
    ruby_spacing_layout->addWidget(label, row, 0);

    QSpinBox* spin = new QSpinBox(ruby_spacing_box);
    spin->setObjectName(QString::fromLatin1(object_name));
    spin->setRange(0, maximum);
    spin->setValue(default_value);
    spin->setMinimumHeight(24);
    spin->setMinimumWidth(72);
    spin->setToolTip(tooltip);
    ruby_spacing_layout->addWidget(spin, row, 1);
    QObject::connect(spin, SIGNAL(valueChanged(int)), this,
                     SLOT(EnableApplyButton()));
  };

  add_ruby_spacing_spin(
      0, tr("Horizontal padding"),
      tr("Space between the ruby text and the left and right window edges."),
      "rubyWindowHorizontalPaddingSpinBox", 40, 14);
  add_ruby_spacing_spin(
      1, tr("Vertical padding"),
      tr("Space between the ruby text and the top and bottom window edges."),
      "rubyWindowVerticalPaddingSpinBox", 24, 6);
  add_ruby_spacing_spin(
      2, tr("Distance from input text"),
      tr("Distance between the ruby window and the text being composed."),
      "rubyWindowCompositionGapSpinBox", 32, 4);
  ruby_spacing_layout->setColumnStretch(0, 1);
  body_layout->addWidget(ruby_spacing_box);

  body_layout->addSpacing(12);

  QWidget* shadow_grid_widget = new QWidget(group);
  shadow_grid_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  QGridLayout* shadow_grid = new QGridLayout(shadow_grid_widget);
  shadow_grid->setContentsMargins(0, 0, 0, 0);
  shadow_grid->setHorizontalSpacing(8);
  shadow_grid->setVerticalSpacing(6);
  body_layout->addWidget(shadow_grid_widget);

  auto add_shadow_spin = [&](int row, int column, const char* name, int min,
                             int max, const QString& suffix, int value) {
    QSpinBox* spin = new QSpinBox(group);
    spin->setObjectName(QString::fromLatin1(name));
    spin->setRange(min, max);
    spin->setSuffix(suffix);
    spin->setValue(value);
    spin->setMinimumHeight(24);
    spin->setMinimumWidth(58);
    shadow_grid->addWidget(spin, row, column);
    QObject::connect(spin, SIGNAL(valueChanged(int)), this,
                     SLOT(UpdateRendererAppearanceControls()));
    QObject::connect(spin, SIGNAL(valueChanged(int)), this,
                     SLOT(EnableApplyButton()));
    return spin;
  };

  auto add_direction_button = [&](QGridLayout* layout, int row, int column,
                                  const QString& text, const char* angle_name,
                                  const char* distance_name, int angle,
                                  int default_distance, bool center) {
    QPushButton* button = new QPushButton(text, group);
    button->setFixedSize(24, 24);
    button->setProperty("angleSpinBox", QString::fromLatin1(angle_name));
    button->setProperty("distanceSpinBox", QString::fromLatin1(distance_name));
    button->setProperty("angle", angle);
    button->setProperty("defaultDistance", default_distance);
    button->setProperty("center", center);
    button->setToolTip(center ? tr("Even shadow on all sides")
                              : tr("Set shadow direction"));
    layout->addWidget(button, row, column);
    QObject::connect(button, SIGNAL(clicked()), this,
                     SLOT(SelectRendererShadowDirectionPreset()));
  };

  auto add_direction_pad = [&](int row, int column, const char* angle_name,
                               const char* distance_name,
                               int default_distance) {
    QWidget* pad = new QWidget(group);
    QGridLayout* pad_layout = new QGridLayout(pad);
    pad_layout->setContentsMargins(0, 0, 0, 0);
    pad_layout->setHorizontalSpacing(1);
    pad_layout->setVerticalSpacing(1);
    add_direction_button(pad_layout, 0, 0, QStringLiteral("↖"), angle_name,
                         distance_name, 225, default_distance, false);
    add_direction_button(pad_layout, 0, 1, QStringLiteral("↑"), angle_name,
                         distance_name, 270, default_distance, false);
    add_direction_button(pad_layout, 0, 2, QStringLiteral("↗"), angle_name,
                         distance_name, 315, default_distance, false);
    add_direction_button(pad_layout, 1, 0, QStringLiteral("←"), angle_name,
                         distance_name, 180, default_distance, false);
    add_direction_button(pad_layout, 1, 1, QStringLiteral("●"), angle_name,
                         distance_name, 0, default_distance, true);
    add_direction_button(pad_layout, 1, 2, QStringLiteral("→"), angle_name,
                         distance_name, 0, default_distance, false);
    add_direction_button(pad_layout, 2, 0, QStringLiteral("↙"), angle_name,
                         distance_name, 135, default_distance, false);
    add_direction_button(pad_layout, 2, 1, QStringLiteral("↓"), angle_name,
                         distance_name, 90, default_distance, false);
    add_direction_button(pad_layout, 2, 2, QStringLiteral("↘"), angle_name,
                         distance_name, 45, default_distance, false);
    shadow_grid->addWidget(pad, row, column);
  };

  auto add_shadow_row = [&](int row, const QString& title, const char* size_name,
                            const char* opacity_name, const char* distance_name,
                            const char* angle_name, int default_size,
                            int default_opacity, int default_distance,
                            int default_angle) {
    shadow_grid->addWidget(new QLabel(title, group), row, 0);
    add_shadow_spin(row, 1, size_name, 0, 96, QStringLiteral(" px"),
                    default_size);
    add_shadow_spin(row, 2, opacity_name, 0, 100, QStringLiteral(" %"),
                    default_opacity);
    add_direction_pad(row, 3, angle_name, distance_name, default_distance);
    add_shadow_spin(row, 4, angle_name, 0, 359, QStringLiteral("°"),
                    default_angle);
    add_shadow_spin(row, 5, distance_name, 0, 96, QStringLiteral(" px"),
                    default_distance);
  };

  shadow_grid->addWidget(new QLabel(tr("Target"), group), 0, 0);
  shadow_grid->addWidget(new QLabel(tr("Shadow spread"), group), 0, 1);
  shadow_grid->addWidget(new QLabel(tr("Shadow opacity"), group), 0, 2);
  shadow_grid->addWidget(new QLabel(tr("Shadow direction"), group), 0, 3);
  shadow_grid->addWidget(new QLabel(tr("Shadow angle"), group), 0, 4);
  shadow_grid->addWidget(new QLabel(tr("Shadow distance"), group), 0, 5);
  add_shadow_row(1, tr("Candidate window"),
                 "candidateWindowShadowSizeSpinBox",
                 "candidateWindowShadowOpacityPercentSpinBox",
                 "candidateWindowShadowDistanceSpinBox",
                 "candidateWindowShadowAngleDegreesSpinBox", 5, 10, 6, 45);
  add_shadow_row(2, tr("Suggestion window"),
                 "suggestWindowShadowSizeSpinBox",
                 "suggestWindowShadowOpacityPercentSpinBox",
                 "suggestWindowShadowDistanceSpinBox",
                 "suggestWindowShadowAngleDegreesSpinBox", 5, 10, 6, 45);
  add_shadow_row(3, tr("Ruby window"),
                 "rubyWindowShadowSizeSpinBox",
                 "rubyWindowShadowOpacityPercentSpinBox",
                 "rubyWindowShadowDistanceSpinBox",
                 "rubyWindowShadowAngleDegreesSpinBox", 5, 8, 3, 45);

  auto add_palette_button = [&](QGridLayout* layout, int row, int col,
                                const QString& prefix, const char* suffix,
                                const QString& label) {
    QLabel* text = new QLabel(label, group);
    text->setMinimumHeight(26);
    text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QPushButton* button = new QPushButton(group);
    button->setObjectName(prefix + suffix);
    button->setMinimumSize(84, 26);
    button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    layout->addWidget(text, row, col * 2);
    layout->addWidget(button, row, col * 2 + 1);
    QObject::connect(button, SIGNAL(clicked()), this,
                     SLOT(SelectRendererAppearanceColor()));
    return button;
  };

  auto add_load_button = [&](QHBoxLayout* layout, const QString& text,
                             const QString& target, const char* slot) {
    QPushButton* button = new QPushButton(text, group);
    button->setProperty("target", target);
    button->setMinimumHeight(26);
    layout->addWidget(button);
    QObject::connect(button, SIGNAL(clicked()), this, slot);
  };

  auto add_candidate_palette_group = [&](const QString& title,
                                         const QString& prefix,
                                         bool allow_candidate_load) {
    QGroupBox* box = new QGroupBox(title, group);
    box->setObjectName(prefix + QStringLiteral("PaletteGroupBox"));
    box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QVBoxLayout* box_layout = new QVBoxLayout(box);
    box_layout->setContentsMargins(8, 8, 8, 8);
    box_layout->setSpacing(8);
    QGridLayout* palette_grid = new QGridLayout();
    palette_grid->setHorizontalSpacing(8);
    palette_grid->setVerticalSpacing(8);
    box_layout->addLayout(palette_grid);
    const QString labels[] = {
        tr("Background"),
        tr("Text"),
        tr("Selected background"),
        tr("Selected border"),
        tr("Border"),
        tr("Shortcut text"),
        tr("Shortcut background"),
        tr("Description"),
        tr("Footer text"),
        tr("Footer background"),
        tr("Footer border"),
        tr("Scrollbar background"),
        tr("Scrollbar thumb")};
    for (size_t i = 0; i < std::size(kCandidatePaletteButtonNames); ++i) {
      add_palette_button(palette_grid, static_cast<int>(i / 2),
                         static_cast<int>(i % 2), prefix,
                         kCandidatePaletteButtonNames[i], labels[i]);
    }
    QHBoxLayout* load_layout = new QHBoxLayout();
    load_layout->setSpacing(6);
    add_load_button(load_layout, tr("Load light colors"), prefix,
                    SLOT(LoadRendererLightAppearance()));
    add_load_button(load_layout, tr("Load dark colors"), prefix,
                    SLOT(LoadRendererDarkAppearance()));
    if (allow_candidate_load) {
      add_load_button(load_layout, tr("Load candidate colors"), prefix,
                      SLOT(LoadRendererCandidateAppearance()));
    }
    load_layout->addStretch();
    box_layout->addLayout(load_layout);
    body_layout->addWidget(box);
  };

  auto add_ruby_palette_group = [&]() {
    const QString prefix = QStringLiteral("rubyWindow");
    QGroupBox* box = new QGroupBox(tr("Ruby window custom colors"), group);
    box->setObjectName(prefix + QStringLiteral("PaletteGroupBox"));
    box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QVBoxLayout* box_layout = new QVBoxLayout(box);
    box_layout->setContentsMargins(8, 8, 8, 8);
    box_layout->setSpacing(8);
    QGridLayout* palette_grid = new QGridLayout();
    palette_grid->setHorizontalSpacing(8);
    palette_grid->setVerticalSpacing(8);
    box_layout->addLayout(palette_grid);
    const QString labels[] = {tr("Background"), tr("Text"), tr("Border")};
    for (size_t i = 0; i < std::size(kRubyPaletteButtonNames); ++i) {
      add_palette_button(palette_grid, 0, static_cast<int>(i), prefix,
                         kRubyPaletteButtonNames[i], labels[i]);
    }
    QHBoxLayout* load_layout = new QHBoxLayout();
    load_layout->setSpacing(6);
    add_load_button(load_layout, tr("Load light colors"), prefix,
                    SLOT(LoadRendererLightAppearance()));
    add_load_button(load_layout, tr("Load dark colors"), prefix,
                    SLOT(LoadRendererDarkAppearance()));
    add_load_button(load_layout, tr("Load candidate colors"), prefix,
                    SLOT(LoadRendererCandidateAppearance()));
    load_layout->addStretch();
    box_layout->addLayout(load_layout);
    body_layout->addWidget(box);
  };

  add_candidate_palette_group(tr("Candidate window custom colors"),
                              QStringLiteral("candidateWindow"), false);
  add_candidate_palette_group(tr("Suggestion window custom colors"),
                              QStringLiteral("suggestWindow"), true);
  add_ruby_palette_group();

  const int insert_index =
      inputSupportContentLayout->indexOf(preeditDisplayColorGroupBox);
  inputSupportContentLayout->insertWidget(
      insert_index >= 0 ? insert_index : inputSupportContentLayout->count(),
      group);
}

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
  SET_CHECKBOX(showCandidateWindowOnInitialConversionCheckBox,
               show_candidate_window_on_initial_conversion);

  const uint32_t live_conversion_delay_msec =
      config.has_live_conversion_delay_msec()
          ? config.live_conversion_delay_msec()
          : kDefaultLiveConversionDelayMsec;
  liveConversionDelaySpinBox->setValue(
      static_cast<int>(
          std::clamp(live_conversion_delay_msec,
                     0u,
                     kMaxLiveConversionDelayMsec)));

  const uint32_t live_conversion_min_key_length =
      config.has_live_conversion_min_key_length()
          ? config.live_conversion_min_key_length()
          : kDefaultLiveConversionMinKeyLength;
  liveConversionMinKeyLengthSpinBox->setValue(
      static_cast<int>(
          std::clamp(live_conversion_min_key_length,
                     kMinLiveConversionMinKeyLength,
                     kMaxLiveConversionMinKeyLength)));

  SET_CHECKBOX(showLiveConversionRubyWindow,
               show_live_conversion_ruby_window);

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

  zenzLiveCorrectionProfileLineEdit->setText(
      ToQString(config.zenz_live_correction_profile()));
  zenzLiveCorrectionTopicLineEdit->setText(
      ToQString(config.zenz_live_correction_topic()));
  zenzLiveCorrectionStyleLineEdit->setText(
      ToQString(config.zenz_live_correction_style()));
  zenzLiveCorrectionSettingsLineEdit->setText(
      ToQString(config.zenz_live_correction_settings()));

  SET_CHECKBOX(zenzLiveCorrectionRightContextCheckBox,
               use_zenz_live_correction_right_context);
  const uint32_t zenz_live_correction_right_context_length =
      config.zenz_live_correction_right_context_length();
  zenzLiveCorrectionRightContextLengthSpinBox->setValue(
      static_cast<int>(
          std::clamp(zenz_live_correction_right_context_length,
                     0u,
                     kMaxZenzLiveCorrectionRightContextLength)));

  SelectZenzLiveCorrectionSetting(
      static_cast<int>(zenzLiveCorrectionCheckBox->isChecked()));

  SET_CHECKBOX(zenzFeedbackLearningCheckBox, use_zenz_feedback_learning);

  SET_CHECKBOX(zenzFeedbackAutoBlockCheckBox,
               use_zenz_auto_block_rejected_correction);
  const uint32_t zenz_auto_block_reject_threshold =
      config.has_zenz_auto_block_reject_threshold()
          ? config.zenz_auto_block_reject_threshold()
          : kDefaultZenzAutoBlockRejectThreshold;
  zenzFeedbackAutoBlockRejectThresholdSpinBox->setValue(
      static_cast<int>(
          std::clamp(zenz_auto_block_reject_threshold,
                     kMinZenzAutoBlockRejectThreshold,
                     kMaxZenzAutoBlockRejectThreshold)));
  SelectZenzFeedbackLearningSetting(
      static_cast<int>(zenzFeedbackLearningCheckBox->isChecked()));

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
  directCommitMiddleDotCheckBox->setChecked(
      config.direct_commit_key() &
      config::Config::DIRECT_COMMIT_MIDDLE_DOT);

  SET_COMBOBOX(shiftKeyModeSwitchComboBox, ShiftKeyModeSwitch,
               shift_key_mode_switch);

  SET_CHECKBOX(useJapaneseLayout, use_japanese_layout);

  SET_CHECKBOX(useModeIndicator, use_mode_indicator);
  SetComboCurrentData(
      FindComboBox(this, "windowsImeIconStyleComboBox"),
      static_cast<int>(config.windows_ime_icon_style()));

  ConvertRendererAppearanceFromProto(config);

  SetComboBoxCurrentFontNameOrAdd(
      candidateRubyFontComboBox,
      QString::fromUtf8(config.candidate_ruby_font_name().c_str()));

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
  GET_CHECKBOX(showCandidateWindowOnInitialConversionCheckBox,
               show_candidate_window_on_initial_conversion);
  config->set_live_conversion_delay_msec(
      static_cast<uint32_t>(liveConversionDelaySpinBox->value()));
  config->set_live_conversion_min_key_length(
      static_cast<uint32_t>(liveConversionMinKeyLengthSpinBox->value()));
  GET_CHECKBOX(showLiveConversionRubyWindow,
               show_live_conversion_ruby_window);

  GET_CHECKBOX(zenzLiveCorrectionCheckBox, use_zenz_live_correction);
  config->set_zenz_live_correction_delay_msec(
      static_cast<uint32_t>(zenzLiveCorrectionDelaySpinBox->value()));
  config->set_zenz_live_correction_min_key_length(
      static_cast<uint32_t>(
          zenzLiveCorrectionMinKeyLengthSpinBox->value()));
  config->set_zenz_live_correction_profile(
      zenzLiveCorrectionProfileLineEdit->text().toUtf8().constData());
  config->set_zenz_live_correction_topic(
      zenzLiveCorrectionTopicLineEdit->text().toUtf8().constData());
  config->set_zenz_live_correction_style(
      zenzLiveCorrectionStyleLineEdit->text().toUtf8().constData());
  config->set_zenz_live_correction_settings(
      zenzLiveCorrectionSettingsLineEdit->text().toUtf8().constData());
  GET_CHECKBOX(zenzLiveCorrectionRightContextCheckBox,
               use_zenz_live_correction_right_context);
  config->set_zenz_live_correction_right_context_length(
      static_cast<uint32_t>(
          zenzLiveCorrectionRightContextLengthSpinBox->value()));

  GET_CHECKBOX(zenzFeedbackLearningCheckBox, use_zenz_feedback_learning);
  GET_CHECKBOX(zenzFeedbackAutoBlockCheckBox,
               use_zenz_auto_block_rejected_correction);
  config->set_zenz_auto_block_reject_threshold(
      static_cast<uint32_t>(
          zenzFeedbackAutoBlockRejectThresholdSpinBox->value()));

  GET_CHECKBOX(useAutoConversion, use_auto_conversion);
  GET_CHECKBOX(useDirectCommit, use_direct_commit);
  GET_CHECKBOX(useJapaneseLayout, use_japanese_layout);

  GET_CHECKBOX(useModeIndicator, use_mode_indicator);
  if (const QComboBox* combo =
          FindComboBox(this, "windowsImeIconStyleComboBox")) {
    config->set_windows_ime_icon_style(
        static_cast<config::Config::WindowsImeIconStyle>(
            GetComboCurrentData(
                combo,
                static_cast<int>(
                    config::Config::WINDOWS_IME_ICON_DEFAULT))));
  }

  ConvertRendererAppearanceToProto(config);

  const QString font_name =
      candidateRubyFontComboBox->currentData().toString().trimmed();
  if (!font_name.isEmpty()) {
    config->set_candidate_ruby_font_name(font_name.toUtf8().constData());
  } else {
    config->clear_candidate_ruby_font_name();
  }

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
  if (directCommitMiddleDotCheckBox->isChecked()) {
    direct_commit_key |= config::Config::DIRECT_COMMIT_MIDDLE_DOT;
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


void ConfigDialog::ConvertRendererAppearanceFromProto(
    const config::Config &config) {
#if !defined(_WIN32) && !defined(__APPLE__)
  if (useDarkModeCandidateWindow != nullptr) {
    useDarkModeCandidateWindow->setChecked(
        config.use_dark_mode_candidate_window());
  }
  return;
#endif  // !defined(_WIN32) && !defined(__APPLE__)

  const int candidate_color_theme =
      config.has_candidate_window_color_theme()
          ? static_cast<int>(config.candidate_window_color_theme())
          : static_cast<int>(config.use_dark_mode_candidate_window()
                             ? config::Config::RENDERER_WINDOW_COLOR_DARK
                             : config::Config::RENDERER_WINDOW_COLOR_LIGHT);
  SetComboCurrentData(FindComboBox(this, "candidateWindowColorThemeComboBox"),
                      candidate_color_theme);
  SetComboCurrentData(FindComboBox(this, "suggestWindowColorThemeComboBox"),
                      static_cast<int>(config.suggest_window_color_theme()));
  SetComboCurrentData(FindComboBox(this, "rubyWindowColorThemeComboBox"),
                      static_cast<int>(config.ruby_window_color_theme()));

  SetCandidatePaletteButtonsFromProto(
      this, QStringLiteral("candidateWindow"),
      config.candidate_window_custom_color_palette());
  SetCandidatePaletteButtonsFromProto(
      this, QStringLiteral("suggestWindow"),
      config.suggest_window_custom_color_palette());
  SetRubyPaletteButtonsFromProto(this, QStringLiteral("rubyWindow"),
                                 config.ruby_window_custom_color_palette());

  if (QSpinBox* spin = FindSpinBox(this, "candidateWindowSizePercentSpinBox")) {
    spin->setValue(static_cast<int>(
        config.has_candidate_window_size_percent()
            ? config.candidate_window_size_percent()
            : 100));
  }
  if (QSpinBox* spin = FindSpinBox(this, "suggestWindowSizePercentSpinBox")) {
    spin->setValue(static_cast<int>(
        config.has_suggest_window_size_percent()
            ? config.suggest_window_size_percent()
            : 100));
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowSizePercentSpinBox")) {
    spin->setValue(static_cast<int>(
        config.has_ruby_window_size_percent()
            ? config.ruby_window_size_percent()
            : 100));
  }

  SetComboCurrentData(
      FindComboBox(this, "candidateWindowFontWeightComboBox"),
      NormalizeRendererFontWeight(config.candidate_window_font_weight()));
  SetComboCurrentData(
      FindComboBox(this, "suggestWindowFontWeightComboBox"),
      NormalizeRendererFontWeight(config.suggest_window_font_weight()));
  SetComboCurrentData(
      FindComboBox(this, "rubyWindowFontWeightComboBox"),
      NormalizeRendererFontWeight(config.ruby_window_font_weight()));

  if (QSpinBox* spin = FindSpinBox(this, "candidateWindowCornerRadiusSpinBox")) {
    spin->setValue(static_cast<int>(config.candidate_window_custom_corner_radius()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "suggestWindowCornerRadiusSpinBox")) {
    spin->setValue(static_cast<int>(config.suggest_window_custom_corner_radius()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowCornerRadiusSpinBox")) {
    spin->setValue(static_cast<int>(config.ruby_window_custom_corner_radius()));
  }

  if (QSpinBox* spin = FindSpinBox(this, "candidateWindowOpacityPercentSpinBox")) {
    spin->setValue(static_cast<int>(config.candidate_window_opacity_percent()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "suggestWindowOpacityPercentSpinBox")) {
    spin->setValue(static_cast<int>(config.suggest_window_opacity_percent()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowOpacityPercentSpinBox")) {
    spin->setValue(static_cast<int>(config.ruby_window_opacity_percent()));
  }
  if (QSpinBox* spin =
          FindSpinBox(this, "rubyWindowHorizontalPaddingSpinBox")) {
    spin->setValue(static_cast<int>(config.ruby_window_horizontal_padding()));
  }
  if (QSpinBox* spin =
          FindSpinBox(this, "rubyWindowVerticalPaddingSpinBox")) {
    spin->setValue(static_cast<int>(config.ruby_window_vertical_padding()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowCompositionGapSpinBox")) {
    spin->setValue(static_cast<int>(config.ruby_window_composition_gap()));
  }

  if (QSpinBox* spin = FindSpinBox(this, "candidateWindowShadowSizeSpinBox")) {
    spin->setValue(static_cast<int>(config.candidate_window_shadow_size()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "candidateWindowShadowOpacityPercentSpinBox")) {
    spin->setValue(static_cast<int>(config.candidate_window_shadow_opacity_percent()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "candidateWindowShadowAngleDegreesSpinBox")) {
    spin->setValue(static_cast<int>(config.candidate_window_shadow_angle_degrees() % 360));
  }
  if (QSpinBox* spin = FindSpinBox(this, "candidateWindowShadowDistanceSpinBox")) {
    spin->setValue(static_cast<int>(config.candidate_window_shadow_distance()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "suggestWindowShadowSizeSpinBox")) {
    spin->setValue(static_cast<int>(config.suggest_window_shadow_size()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "suggestWindowShadowOpacityPercentSpinBox")) {
    spin->setValue(static_cast<int>(config.suggest_window_shadow_opacity_percent()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "suggestWindowShadowAngleDegreesSpinBox")) {
    spin->setValue(static_cast<int>(config.suggest_window_shadow_angle_degrees() % 360));
  }
  if (QSpinBox* spin = FindSpinBox(this, "suggestWindowShadowDistanceSpinBox")) {
    spin->setValue(static_cast<int>(config.suggest_window_shadow_distance()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowShadowSizeSpinBox")) {
    spin->setValue(static_cast<int>(config.ruby_window_shadow_size()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowShadowOpacityPercentSpinBox")) {
    spin->setValue(static_cast<int>(config.ruby_window_shadow_opacity_percent()));
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowShadowAngleDegreesSpinBox")) {
    spin->setValue(static_cast<int>(config.ruby_window_shadow_angle_degrees() % 360));
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowShadowDistanceSpinBox")) {
    spin->setValue(static_cast<int>(config.ruby_window_shadow_distance()));
  }

  if (useDarkModeCandidateWindow != nullptr) {
    useDarkModeCandidateWindow->setChecked(
        candidate_color_theme ==
        static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_DARK));
  }
  UpdateRendererAppearanceControls();
}

void ConfigDialog::ConvertRendererAppearanceToProto(
    config::Config *config) const {
#if !defined(_WIN32) && !defined(__APPLE__)
  if (useDarkModeCandidateWindow != nullptr) {
    config->set_use_dark_mode_candidate_window(
        useDarkModeCandidateWindow->isChecked());
  }
  return;
#endif  // !defined(_WIN32) && !defined(__APPLE__)

  const int candidate_color_theme = GetComboCurrentData(
      FindComboBox(this, "candidateWindowColorThemeComboBox"),
      static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_LIGHT));
  config->set_candidate_window_color_theme(
      static_cast<config::Config::RendererWindowColorTheme>(
          candidate_color_theme));
  config->set_use_dark_mode_candidate_window(
      candidate_color_theme ==
      static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_DARK));

  config->set_suggest_window_color_theme(
      static_cast<config::Config::RendererWindowColorTheme>(
          GetComboCurrentData(
              FindComboBox(this, "suggestWindowColorThemeComboBox"),
              static_cast<int>(
                  config::Config::RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE))));
  config->set_ruby_window_color_theme(
      static_cast<config::Config::RendererWindowColorTheme>(
          GetComboCurrentData(
              FindComboBox(this, "rubyWindowColorThemeComboBox"),
              static_cast<int>(
                  config::Config::RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE))));

  SaveCandidatePaletteToProto(this, QStringLiteral("candidateWindow"),
                              config->mutable_candidate_window_custom_color_palette());
  SaveCandidatePaletteToProto(this, QStringLiteral("suggestWindow"),
                              config->mutable_suggest_window_custom_color_palette());
  SaveRubyPaletteToProto(this, QStringLiteral("rubyWindow"),
                         config->mutable_ruby_window_custom_color_palette());

  if (const QSpinBox* spin = FindSpinBox(this, "candidateWindowSizePercentSpinBox")) {
    config->set_candidate_window_size_percent(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "suggestWindowSizePercentSpinBox")) {
    config->set_suggest_window_size_percent(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "rubyWindowSizePercentSpinBox")) {
    config->set_ruby_window_size_percent(
        static_cast<uint32_t>(spin->value()));
  }

  config->set_candidate_window_font_weight(static_cast<uint32_t>(
      GetComboCurrentData(FindComboBox(this,
                                       "candidateWindowFontWeightComboBox"),
                          kDefaultRendererFontWeight)));
  config->set_suggest_window_font_weight(static_cast<uint32_t>(
      GetComboCurrentData(FindComboBox(this,
                                       "suggestWindowFontWeightComboBox"),
                          kDefaultRendererFontWeight)));
  config->set_ruby_window_font_weight(static_cast<uint32_t>(
      GetComboCurrentData(FindComboBox(this, "rubyWindowFontWeightComboBox"),
                          kDefaultRendererFontWeight)));

  if (const QSpinBox* spin = FindSpinBox(this, "candidateWindowCornerRadiusSpinBox")) {
    config->set_candidate_window_custom_corner_radius(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "suggestWindowCornerRadiusSpinBox")) {
    config->set_suggest_window_custom_corner_radius(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "rubyWindowCornerRadiusSpinBox")) {
    config->set_ruby_window_custom_corner_radius(
        static_cast<uint32_t>(spin->value()));
  }

  if (const QSpinBox* spin = FindSpinBox(this, "candidateWindowOpacityPercentSpinBox")) {
    config->set_candidate_window_opacity_percent(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "suggestWindowOpacityPercentSpinBox")) {
    config->set_suggest_window_opacity_percent(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "rubyWindowOpacityPercentSpinBox")) {
    config->set_ruby_window_opacity_percent(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin =
          FindSpinBox(this, "rubyWindowHorizontalPaddingSpinBox")) {
    config->set_ruby_window_horizontal_padding(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin =
          FindSpinBox(this, "rubyWindowVerticalPaddingSpinBox")) {
    config->set_ruby_window_vertical_padding(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin =
          FindSpinBox(this, "rubyWindowCompositionGapSpinBox")) {
    config->set_ruby_window_composition_gap(
        static_cast<uint32_t>(spin->value()));
  }

  if (const QSpinBox* spin = FindSpinBox(this, "candidateWindowShadowSizeSpinBox")) {
    config->set_candidate_window_shadow_size(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "candidateWindowShadowOpacityPercentSpinBox")) {
    config->set_candidate_window_shadow_opacity_percent(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "candidateWindowShadowAngleDegreesSpinBox")) {
    config->set_candidate_window_shadow_angle_degrees(
        static_cast<uint32_t>(spin->value()) % 360u);
  }
  if (const QSpinBox* spin = FindSpinBox(this, "candidateWindowShadowDistanceSpinBox")) {
    config->set_candidate_window_shadow_distance(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "suggestWindowShadowSizeSpinBox")) {
    config->set_suggest_window_shadow_size(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "suggestWindowShadowOpacityPercentSpinBox")) {
    config->set_suggest_window_shadow_opacity_percent(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "suggestWindowShadowAngleDegreesSpinBox")) {
    config->set_suggest_window_shadow_angle_degrees(
        static_cast<uint32_t>(spin->value()) % 360u);
  }
  if (const QSpinBox* spin = FindSpinBox(this, "suggestWindowShadowDistanceSpinBox")) {
    config->set_suggest_window_shadow_distance(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "rubyWindowShadowSizeSpinBox")) {
    config->set_ruby_window_shadow_size(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "rubyWindowShadowOpacityPercentSpinBox")) {
    config->set_ruby_window_shadow_opacity_percent(
        static_cast<uint32_t>(spin->value()));
  }
  if (const QSpinBox* spin = FindSpinBox(this, "rubyWindowShadowAngleDegreesSpinBox")) {
    config->set_ruby_window_shadow_angle_degrees(
        static_cast<uint32_t>(spin->value()) % 360u);
  }
  if (const QSpinBox* spin = FindSpinBox(this, "rubyWindowShadowDistanceSpinBox")) {
    config->set_ruby_window_shadow_distance(
        static_cast<uint32_t>(spin->value()));
  }
}

void ConfigDialog::ResetRendererAppearanceControls() {
  SetComboCurrentData(FindComboBox(this, "candidateWindowColorThemeComboBox"),
                      static_cast<int>(
                          config::Config::RENDERER_WINDOW_COLOR_LIGHT));
  SetComboCurrentData(FindComboBox(this, "suggestWindowColorThemeComboBox"),
                      static_cast<int>(config::Config::
                                           RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE));
  SetComboCurrentData(FindComboBox(this, "rubyWindowColorThemeComboBox"),
                      static_cast<int>(config::Config::
                                           RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE));

  SetCandidatePaletteButtons(this, QStringLiteral("candidateWindow"),
                             kLightCandidatePalette);
  SetCandidatePaletteButtons(this, QStringLiteral("suggestWindow"),
                             kLightCandidatePalette);
  SetRubyPaletteButtons(this, QStringLiteral("rubyWindow"), kLightRubyPalette);

  if (QSpinBox* spin = FindSpinBox(this, "candidateWindowSizePercentSpinBox")) {
    spin->setValue(100);
  }
  if (QSpinBox* spin = FindSpinBox(this, "suggestWindowSizePercentSpinBox")) {
    spin->setValue(100);
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowSizePercentSpinBox")) {
    spin->setValue(100);
  }

  SetComboCurrentData(FindComboBox(this, "candidateWindowFontWeightComboBox"),
                      kDefaultRendererFontWeight);
  SetComboCurrentData(FindComboBox(this, "suggestWindowFontWeightComboBox"),
                      kDefaultRendererFontWeight);
  SetComboCurrentData(FindComboBox(this, "rubyWindowFontWeightComboBox"),
                      kDefaultRendererFontWeight);

  if (QSpinBox* spin = FindSpinBox(this, "candidateWindowCornerRadiusSpinBox")) {
    spin->setValue(6);
  }
  if (QSpinBox* spin = FindSpinBox(this, "suggestWindowCornerRadiusSpinBox")) {
    spin->setValue(6);
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowCornerRadiusSpinBox")) {
    spin->setValue(9);
  }

  if (QSpinBox* spin = FindSpinBox(this, "candidateWindowOpacityPercentSpinBox")) {
    spin->setValue(100);
  }
  if (QSpinBox* spin = FindSpinBox(this, "suggestWindowOpacityPercentSpinBox")) {
    spin->setValue(100);
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowOpacityPercentSpinBox")) {
    spin->setValue(90);
  }
  if (QSpinBox* spin =
          FindSpinBox(this, "rubyWindowHorizontalPaddingSpinBox")) {
    spin->setValue(14);
  }
  if (QSpinBox* spin =
          FindSpinBox(this, "rubyWindowVerticalPaddingSpinBox")) {
    spin->setValue(6);
  }
  if (QSpinBox* spin = FindSpinBox(this, "rubyWindowCompositionGapSpinBox")) {
    spin->setValue(4);
  }

  struct ShadowDefault {
    const char* size_name;
    const char* opacity_name;
    const char* angle_name;
    const char* distance_name;
    int size;
    int opacity;
    int angle;
    int distance;
  };
  const ShadowDefault shadow_defaults[] = {
      {"candidateWindowShadowSizeSpinBox",
       "candidateWindowShadowOpacityPercentSpinBox",
       "candidateWindowShadowAngleDegreesSpinBox",
       "candidateWindowShadowDistanceSpinBox", 5, 10, 45, 6},
      {"suggestWindowShadowSizeSpinBox",
       "suggestWindowShadowOpacityPercentSpinBox",
       "suggestWindowShadowAngleDegreesSpinBox",
       "suggestWindowShadowDistanceSpinBox", 5, 10, 45, 6},
      {"rubyWindowShadowSizeSpinBox",
       "rubyWindowShadowOpacityPercentSpinBox",
       "rubyWindowShadowAngleDegreesSpinBox",
       "rubyWindowShadowDistanceSpinBox", 5, 8, 45, 3},
  };
  for (const ShadowDefault& shadow_default : shadow_defaults) {
    if (QSpinBox* spin = FindSpinBox(this, shadow_default.size_name)) {
      spin->setValue(shadow_default.size);
    }
    if (QSpinBox* spin = FindSpinBox(this, shadow_default.opacity_name)) {
      spin->setValue(shadow_default.opacity);
    }
    if (QSpinBox* spin = FindSpinBox(this, shadow_default.angle_name)) {
      spin->setValue(shadow_default.angle);
    }
    if (QSpinBox* spin = FindSpinBox(this, shadow_default.distance_name)) {
      spin->setValue(shadow_default.distance);
    }
  }

  SetComboBoxCurrentFontNameOrAdd(candidateRubyFontComboBox, QString());
  if (useDarkModeCandidateWindow != nullptr) {
    useDarkModeCandidateWindow->setChecked(false);
  }

  UpdateRendererAppearanceControls();
  EnableApplyButton();
}

void ConfigDialog::SelectRendererShadowDirectionPreset() {
  QPushButton* button = qobject_cast<QPushButton*>(sender());
  if (button == nullptr) {
    return;
  }

  const QByteArray angle_spin_name =
      button->property("angleSpinBox").toString().toLatin1();
  const QByteArray distance_spin_name =
      button->property("distanceSpinBox").toString().toLatin1();
  QSpinBox* angle_spin = FindSpinBox(this, angle_spin_name.constData());
  QSpinBox* distance_spin =
      FindSpinBox(this, distance_spin_name.constData());
  if (angle_spin == nullptr || distance_spin == nullptr) {
    return;
  }

  const bool center = button->property("center").toBool();
  if (center) {
    distance_spin->setValue(0);
  } else {
    angle_spin->setValue(button->property("angle").toInt());
    if (distance_spin->value() == 0) {
      distance_spin->setValue(button->property("defaultDistance").toInt());
    }
  }
  UpdateRendererAppearanceControls();
  EnableApplyButton();
}

void ConfigDialog::SelectRendererAppearanceColor() {
  QPushButton *button = qobject_cast<QPushButton *>(sender());
  if (button == nullptr) {
    return;
  }
  const uint32_t current_rgb = GetColorButtonRgb(button, 0x000000);
  const QColor selected_color = QColorDialog::getColor(
      RgbHexToQColor(current_rgb), this, tr("Choose window color"));
  if (!selected_color.isValid()) {
    return;
  }
  SetColorButton(button, QColorToRgbHex(selected_color));
  EnableApplyButton();
}

void ConfigDialog::LoadRendererLightAppearance() {
  QPushButton* button = qobject_cast<QPushButton*>(sender());
  if (button == nullptr) {
    return;
  }
  const QString target = button->property("target").toString();
  if (target == QStringLiteral("rubyWindow")) {
    SetRubyPaletteButtons(this, target, kLightRubyPalette);
  } else {
    SetCandidatePaletteButtons(this, target, kLightCandidatePalette);
  }
  EnableApplyButton();
}

void ConfigDialog::LoadRendererDarkAppearance() {
  QPushButton* button = qobject_cast<QPushButton*>(sender());
  if (button == nullptr) {
    return;
  }
  const QString target = button->property("target").toString();
  if (target == QStringLiteral("rubyWindow")) {
    SetRubyPaletteButtons(this, target, kDarkRubyPalette);
  } else {
    SetCandidatePaletteButtons(this, target, kDarkCandidatePalette);
  }
  EnableApplyButton();
}

void ConfigDialog::LoadRendererCandidateAppearance() {
  QPushButton* button = qobject_cast<QPushButton*>(sender());
  if (button == nullptr) {
    return;
  }
  const QString target = button->property("target").toString();
  const CandidateWindowPaletteDefaults candidate_palette =
      GetCandidatePaletteButtons(this, QStringLiteral("candidateWindow"),
                                 kLightCandidatePalette);
  if (target == QStringLiteral("rubyWindow")) {
    SetRubyPaletteButtons(
        this, target,
        {candidate_palette.background_color, candidate_palette.text_color,
         candidate_palette.border_color});
  } else {
    SetCandidatePaletteButtons(this, target, candidate_palette);
  }
  EnableApplyButton();
}

void ConfigDialog::UpdateRendererAppearanceControls() {
  auto enable_candidate_palette = [&](const QString& prefix, bool enabled) {
    if (QWidget* box = findChild<QWidget*>(prefix + QStringLiteral("PaletteGroupBox"))) {
      box->setEnabled(enabled);
    }
  };
  const int custom_color =
      static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_CUSTOM);
  enable_candidate_palette(
      QStringLiteral("candidateWindow"),
      GetComboCurrentData(FindComboBox(this, "candidateWindowColorThemeComboBox"),
                          custom_color) == custom_color);
  enable_candidate_palette(
      QStringLiteral("suggestWindow"),
      GetComboCurrentData(FindComboBox(this, "suggestWindowColorThemeComboBox"),
                          custom_color) == custom_color);
  enable_candidate_palette(
      QStringLiteral("rubyWindow"),
      GetComboCurrentData(FindComboBox(this, "rubyWindowColorThemeComboBox"),
                          custom_color) == custom_color);

  auto update_shadow_angle_enabled = [&](const char* distance_name,
                                         const char* angle_name) {
    QSpinBox* distance_spin = FindSpinBox(this, distance_name);
    QSpinBox* angle_spin = FindSpinBox(this, angle_name);
    if (distance_spin == nullptr || angle_spin == nullptr) {
      return;
    }
    angle_spin->setEnabled(distance_spin->value() > 0);
  };
  update_shadow_angle_enabled("candidateWindowShadowDistanceSpinBox",
                              "candidateWindowShadowAngleDegreesSpinBox");
  update_shadow_angle_enabled("suggestWindowShadowDistanceSpinBox",
                              "suggestWindowShadowAngleDegreesSpinBox");
  update_shadow_angle_enabled("rubyWindowShadowDistanceSpinBox",
                              "rubyWindowShadowAngleDegreesSpinBox");
}

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
  ShowZenzFeedbackManagementDialog(this, base_config_);
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
    EnableApplyButton();
  }
}

void ConfigDialog::EditRomanTable() {
  std::string output;
  if (gui::RomanTableEditorDialog::Show(this, custom_roman_table_, &output)) {
    custom_roman_table_ = output;
    EnableApplyButton();
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
  liveConversionMinKeyLengthLabel->setEnabled(enabled);
  liveConversionMinKeyLengthSpinBox->setEnabled(enabled);
  showLiveConversionRubyWindow->setEnabled(enabled);
  showCandidateWindowOnInitialConversionCheckBox->setEnabled(!enabled);

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
  zenzLiveCorrectionProfileLabel->setEnabled(enabled);
  zenzLiveCorrectionProfileLineEdit->setEnabled(enabled);
  zenzLiveCorrectionTopicLabel->setEnabled(enabled);
  zenzLiveCorrectionTopicLineEdit->setEnabled(enabled);
  zenzLiveCorrectionStyleLabel->setEnabled(enabled);
  zenzLiveCorrectionStyleLineEdit->setEnabled(enabled);
  zenzLiveCorrectionSettingsLabel->setEnabled(enabled);
  zenzLiveCorrectionSettingsLineEdit->setEnabled(enabled);
  zenzLiveCorrectionRightContextCheckBox->setEnabled(enabled);
  SelectZenzRightContextSetting(
      enabled ? static_cast<int>(
                    zenzLiveCorrectionRightContextCheckBox->isChecked())
              : 0);
  zenzFeedbackLearningCheckBox->setEnabled(enabled);
  SelectZenzFeedbackLearningSetting(
      enabled ? static_cast<int>(zenzFeedbackLearningCheckBox->isChecked()) : 0);
}

void ConfigDialog::SelectZenzFeedbackLearningSetting(int state) {
  const bool enabled =
      liveConversionCheckBox->isChecked() &&
      zenzLiveCorrectionCheckBox->isChecked() &&
      static_cast<bool>(state);

  zenzFeedbackAutoBlockCheckBox->setEnabled(enabled);

  const bool auto_block_enabled =
      enabled && zenzFeedbackAutoBlockCheckBox->isChecked();
  zenzFeedbackAutoBlockRejectThresholdLabel->setEnabled(auto_block_enabled);
  zenzFeedbackAutoBlockRejectThresholdSpinBox->setEnabled(auto_block_enabled);
}

void ConfigDialog::SelectZenzRightContextSetting(int state) {
  const bool enabled = liveConversionCheckBox->isChecked() &&
                       zenzLiveCorrectionCheckBox->isChecked() &&
                       static_cast<bool>(state);

  zenzLiveCorrectionRightContextLengthLabel->setEnabled(enabled);
  zenzLiveCorrectionRightContextLengthSpinBox->setEnabled(enabled);
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
  directCommitMiddleDotCheckBox->setEnabled(enabled);

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
    const bool was_suppressed = suppress_apply_button_update_;
    suppress_apply_button_update_ = true;
    ConvertFromProto(config::ConfigHandler::GetProductDefaultConfig());
    UpdateDependentControls();
    suppress_apply_button_update_ = was_suppressed;
    EnableApplyButton();
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

void ConfigDialog::UpdateDependentControls() {
  SelectInputModeSetting(inputModeComboBox->currentIndex());
  SelectLiveConversionSetting(
      static_cast<int>(liveConversionCheckBox->isChecked()));
  SelectAutoConversionSetting(static_cast<int>(useAutoConversion->isChecked()));
  SelectDirectCommitSetting(static_cast<int>(useDirectCommit->isChecked()));
  SelectSuggestionSetting(
      static_cast<int>(historySuggestCheckBox->isChecked() ||
                       dictionarySuggestCheckBox->isChecked() ||
                       realtimeConversionCheckBox->isChecked()));
}

void ConfigDialog::RecordCurrentStateAsApplied() {
  ConvertToProto(&last_applied_config_);

#ifdef _WIN32
  initial_ime_hot_key_disabled_ = IMEHotKeyDisabledCheckBox->isChecked();
#endif  // _WIN32

#ifdef __APPLE__
  initial_startup_enabled_ = startupCheckBox->isChecked();
#endif  // __APPLE__
}

bool ConfigDialog::IsModified() const {
  config::Config current_config;
  ConvertToProto(&current_config);

  if (current_config.SerializeAsString() !=
      last_applied_config_.SerializeAsString()) {
    return true;
  }

#ifdef _WIN32
  if (!IsTsfProfileIconCurrent(current_config.windows_ime_icon_style())) {
    return true;
  }

  if (IMEHotKeyDisabledCheckBox->isChecked() !=
      initial_ime_hot_key_disabled_) {
    return true;
  }
#endif  // _WIN32

#ifdef __APPLE__
  if (startupCheckBox->isChecked() != initial_startup_enabled_) {
    return true;
  }
#endif  // __APPLE__

  return false;
}

void ConfigDialog::EnableApplyButton() {
  if (suppress_apply_button_update_) {
    return;
  }

  configDialogButtonBox->button(QDialogButtonBox::Apply)
      ->setEnabled(IsModified());
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
