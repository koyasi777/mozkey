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
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QByteArray>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFont>
#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QFontMetrics>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStyle>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
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
#include <QScreen>

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
#include "renderer/conversion_status_indicator.h"
#include "renderer/renderer_color_theme.h"
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

QAbstractScrollArea *FindAncestorScrollArea(QWidget *widget) {
  for (QWidget *parent = widget->parentWidget(); parent != nullptr;
       parent = parent->parentWidget()) {
    if (QAbstractScrollArea *scroll_area =
            qobject_cast<QAbstractScrollArea *>(parent)) {
      return scroll_area;
    }
  }
  return nullptr;
}

void ForwardWheelEventToScrollArea(QWidget *source, QWheelEvent *event) {
  QAbstractScrollArea *scroll_area = FindAncestorScrollArea(source);
  if (scroll_area == nullptr) {
    return;
  }

  QWidget *viewport = scroll_area->viewport();
  const QPointF viewport_position =
      viewport->mapFromGlobal(event->globalPosition().toPoint());
  QWheelEvent forwarded_event(
      viewport_position, event->globalPosition(), event->pixelDelta(),
      event->angleDelta(), event->buttons(), event->modifiers(), event->phase(),
      event->inverted(), Qt::MouseEventNotSynthesized,
      event->pointingDevice());
  QCoreApplication::sendEvent(viewport, &forwarded_event);
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

  // Both scrollable tabs must preserve the native tab-page background and
  // avoid stylesheets that can affect checkbox and separator rendering.
  const auto prepare_scroll_area = [](QScrollArea* area, QWidget* contents) {
    area->setFrameShape(QFrame::NoFrame);
    area->setAutoFillBackground(false);
    area->viewport()->setAutoFillBackground(false);
    contents->setAutoFillBackground(false);
    area->setAttribute(Qt::WA_StyledBackground, false);
    area->viewport()->setAttribute(Qt::WA_StyledBackground, false);
    contents->setAttribute(Qt::WA_StyledBackground, false);
    area->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
    contents->setAttribute(Qt::WA_TranslucentBackground, true);
    area->viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
    contents->setAttribute(Qt::WA_NoSystemBackground, true);
    area->setStyleSheet(QString());
    area->viewport()->setStyleSheet(QString());
    contents->setStyleSheet(QString());
  };
  prepare_scroll_area(inputSupportScrollArea,
                      inputSupportScrollAreaWidgetContents);
  prepare_scroll_area(inputAppearanceScrollArea,
                      inputAppearanceScrollAreaWidgetContents);

  // Keep short dialog windows usable on every tab. The Advanced and
  // Appearance pages already have scroll areas in the .ui file; the other
  // pages must also scroll instead of compressing wrapped explanatory text.
  // Reparent the existing page layout so generated widget pointers, signal
  // connections, translations and tab ordering remain unchanged.
  const auto make_scrollable_tab = [&prepare_scroll_area](
                                       QWidget* tab, const char* object_name) {
    QLayout* contents_layout = tab->layout();
    CHECK(contents_layout != nullptr);
    auto* contents = new QWidget;
    contents->setLayout(contents_layout);
    contents_layout->setSizeConstraint(QLayout::SetMinAndMaxSize);

    auto* area = new QScrollArea(tab);
    area->setObjectName(QString::fromLatin1(object_name));
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->setWidget(contents);
    prepare_scroll_area(area, contents);

    auto* outer_layout = new QVBoxLayout(tab);
    outer_layout->setContentsMargins(0, 0, 0, 0);
    outer_layout->setSpacing(0);
    outer_layout->addWidget(area);
  };
  make_scrollable_tab(basicTab, "basicScrollArea");
  make_scrollable_tab(dictionaryTab, "dictionaryScrollArea");
  make_scrollable_tab(suggestionsTab, "suggestionsScrollArea");
  make_scrollable_tab(privacyTab, "privacyScrollArea");
  make_scrollable_tab(miscTab, "miscScrollArea");

  // Move the existing TSF preedit widgets without recreating them. The
  // existing signal connections, config fields, and translations are reused.
  inputSupportContentLayout->removeWidget(preeditDisplayColorGroupBox);
  preeditDisplayColorGroupBox->setParent(
      inputAppearanceScrollAreaWidgetContents);
  inputAppearanceContentLayout->insertWidget(0, preeditDisplayColorGroupBox);
  preeditDisplayColorGroupBox->show();

#if !defined(_WIN32) && !defined(__APPLE__)
  // On Linux neither the detailed renderer appearance nor TSF preedit
  // attributes are supported. Keep the tab out of the navigation entirely.
  const int appearance_index = configDialogTabWidget->indexOf(inputAppearanceTab);
  if (appearance_index >= 0) {
    configDialogTabWidget->removeTab(appearance_index);
  }
#endif  // !defined(_WIN32) && !defined(__APPLE__)

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
  const int misc_index = configDialogTabWidget->indexOf(miscTab);
  if (misc_index >= 0) {
    configDialogTabWidget->removeTab(misc_index);
  }
#endif  // __linux__
#endif  // NDEBUG

  suggestionsSizeSpinBox->setRange(1, 9);

  pendingRomanDimnessSpinBox->setRange(0, 100);
  pendingRomanDimnessSpinBox->setSingleStep(1);
  pendingRomanDimnessSpinBox->setSuffix(QString::fromUtf8(" %"));

  liveConversionDelaySpinBox->setRange(0, 1000);
  liveConversionDelaySpinBox->setSingleStep(1);
  liveConversionDelaySpinBox->setSuffix(QString::fromUtf8(" ms"));
  liveConversionDelaySpinBox->setSpecialValueText(QString::fromUtf8("即時"));

  liveConversionMinKeyLengthSpinBox->setRange(1, 20);
  liveConversionMinKeyLengthSpinBox->setSingleStep(1);
  liveConversionMinKeyLengthSpinBox->setSuffix(QString::fromUtf8(" 文字"));

  zenzLiveCorrectionDelaySpinBox->setRange(0, 5000);
  zenzLiveCorrectionDelaySpinBox->setSingleStep(1);
  zenzLiveCorrectionDelaySpinBox->setSuffix(QString::fromUtf8(" ms"));
  zenzLiveCorrectionDelaySpinBox->setSpecialValueText(QString::fromUtf8("即時"));

  zenzDeferredPresentationTimeoutSpinBox->setRange(0, 3000);
  zenzDeferredPresentationTimeoutSpinBox->setSingleStep(50);
  zenzDeferredPresentationTimeoutSpinBox->setSuffix(
      QString::fromUtf8(" ms"));

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

  zenzFeedbackMinKeyLengthSpinBox->setRange(1, 20);
  zenzFeedbackMinKeyLengthSpinBox->setSingleStep(1);
  zenzFeedbackMinKeyLengthSpinBox->setSuffix(
      QString::fromUtf8(" 文字以上"));

  zenzFeedbackAutoBlockRejectThresholdSpinBox->setRange(1, 999);
  zenzFeedbackAutoBlockRejectThresholdSpinBox->setSingleStep(1);
  zenzFeedbackAutoBlockRejectThresholdSpinBox->setSuffix(
      tr(" 回"));

  zenzFeedbackAutoBlockMinimumRejectPercentageSpinBox->setRange(1, 100);
  zenzFeedbackAutoBlockMinimumRejectPercentageSpinBox->setSingleStep(1);
  zenzFeedbackAutoBlockMinimumRejectPercentageSpinBox->setSuffix(
      tr(" % 以上"));

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

#ifdef _WIN32
  modeIndicatorThemeComboBox->clear();
  modeIndicatorThemeComboBox->addItem(
      tr("System theme"),
      static_cast<int>(
          config::Config::WINDOWS_MODE_INDICATOR_THEME_SYSTEM));
  modeIndicatorThemeComboBox->addItem(
      tr("Dark"),
      static_cast<int>(config::Config::WINDOWS_MODE_INDICATOR_THEME_DARK));
  modeIndicatorThemeComboBox->addItem(
      tr("Light"),
      static_cast<int>(config::Config::WINDOWS_MODE_INDICATOR_THEME_LIGHT));
  modeIndicatorThemeComboBox->addItem(
      tr("Custom"),
      static_cast<int>(config::Config::WINDOWS_MODE_INDICATOR_THEME_CUSTOM));
#endif  // _WIN32

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
  modeIndicatorThemeComboBox->hide();

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
  QObject::connect(dimPendingRomanInputCheckBox, SIGNAL(toggled(bool)),
                   pendingRomanDimnessLabel, SLOT(setEnabled(bool)));
  QObject::connect(dimPendingRomanInputCheckBox, SIGNAL(toggled(bool)),
                   pendingRomanDimnessSpinBox, SLOT(setEnabled(bool)));
  QObject::connect(liveConversionCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectLiveConversionSetting(int)));
  QObject::connect(zenzLiveCorrectionCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectZenzLiveCorrectionSetting(int)));
  QObject::connect(zenzDeferLiveConversionDisplayCheckBox,
                   SIGNAL(stateChanged(int)), this,
                   SLOT(SelectZenzDeferredPresentationSetting(int)));
  QObject::connect(zenzLiveCorrectionRightContextCheckBox,
                   SIGNAL(stateChanged(int)), this,
                   SLOT(SelectZenzRightContextSetting(int)));
  QObject::connect(zenzFeedbackReuseCheckBox,
                   SIGNAL(stateChanged(int)), this,
                   SLOT(SelectZenzFeedbackReuseSetting(int)));
  QObject::connect(zenzFeedbackMinKeyLengthCheckBox,
                   &QCheckBox::stateChanged, this, [this](int) {
                     SelectZenzFeedbackReuseSetting(
                         zenzFeedbackReuseCheckBox->isChecked());
                   });
  QObject::connect(zenzFeedbackAutoBlockCheckBox,
                   SIGNAL(stateChanged(int)), this,
                   SLOT(SelectZenzFeedbackAutoBlockSetting(int)));
  QObject::connect(useAutoConversion, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectAutoConversionSetting(int)));
  QObject::connect(useDirectCommit, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectDirectCommitSetting(int)));
  QObject::connect(useModeIndicator, SIGNAL(toggled(bool)),
                   modeIndicatorThemeComboBox, SLOT(setEnabled(bool)));
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
  QObject::connect(zenzPreeditTextColorButton, SIGNAL(clicked()), this,
                   SLOT(SelectPreeditColor()));
  QObject::connect(zenzPreeditBackgroundColorButton, SIGNAL(clicked()), this,
                   SLOT(SelectPreeditColor()));
  QObject::connect(zenzPreeditUnderlineColorButton, SIGNAL(clicked()), this,
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
  QObject::connect(zenzPreeditTextColorCheckBox, SIGNAL(toggled(bool)),
                   zenzPreeditTextColorButton, SLOT(setEnabled(bool)));
  QObject::connect(zenzPreeditBackgroundColorCheckBox, SIGNAL(toggled(bool)),
                   zenzPreeditBackgroundColorButton, SLOT(setEnabled(bool)));
  QObject::connect(zenzPreeditUnderlineColorCheckBox, SIGNAL(toggled(bool)),
                   zenzPreeditUnderlineColorButton, SLOT(setEnabled(bool)));

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

  // Prevent mouse-wheel and trackpad scrolling from silently changing
  // combo-box and spin-box values anywhere in this dialog.  Install the
  // filter on the application so controls created later by item delegates are
  // covered too.
  if (QCoreApplication *application = QCoreApplication::instance()) {
    application->installEventFilter(this);
  }

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

  // QScrollArea does not automatically propagate child minimumSizeHint().
  // Recalculate the two tab widths after translations and platform filtering.
  const auto synchronize_minimum_width = [](QScrollArea* area,
                                            QWidget* contents,
                                            QVBoxLayout* layout) {
    layout->invalidate();
    layout->activate();
    const int content_width = contents->minimumSizeHint().width();
    if (content_width > 0) {
      const int scrollbar_width =
          area->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, area);
      area->setMinimumWidth(content_width + scrollbar_width +
                            2 * area->frameWidth());
    }
  };
  synchronize_minimum_width(inputSupportScrollArea,
                            inputSupportScrollAreaWidgetContents,
                            inputSupportContentLayout);
#if defined(_WIN32) || defined(__APPLE__)
  synchronize_minimum_width(inputAppearanceScrollArea,
                            inputAppearanceScrollAreaWidgetContents,
                            inputAppearanceContentLayout);
#endif  // _WIN32 || __APPLE__
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

  initial_use_custom_zenz_live_correction_text_color_ =
      config.use_custom_zenz_live_correction_text_color();
  initial_zenz_live_correction_text_color_ =
      config.zenz_live_correction_text_color();
  initial_use_custom_zenz_live_correction_background_color_ =
      config.use_custom_zenz_live_correction_background_color();
  initial_zenz_live_correction_background_color_ =
      config.zenz_live_correction_background_color();
  initial_use_custom_zenz_live_correction_underline_color_ =
      config.use_custom_zenz_live_correction_underline_color();
  initial_zenz_live_correction_underline_color_ =
      config.zenz_live_correction_underline_color();
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
  if (zenzDeferredPresentationTimeoutSpinBox->value() < 50) {
    QMessageBox::warning(
        this, windowTitle(),
        tr("Zenz correction maximum wait must be at least 50 ms."));
    zenzDeferredPresentationTimeoutSpinBox->setFocus();
    zenzDeferredPresentationTimeoutSpinBox->selectAll();
    return false;
  }

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
          config.preedit_target_underline_color() ||
      initial_use_custom_zenz_live_correction_text_color_ !=
          config.use_custom_zenz_live_correction_text_color() ||
      initial_zenz_live_correction_text_color_ !=
          config.zenz_live_correction_text_color() ||
      initial_use_custom_zenz_live_correction_background_color_ !=
          config.use_custom_zenz_live_correction_background_color() ||
      initial_zenz_live_correction_background_color_ !=
          config.zenz_live_correction_background_color() ||
      initial_use_custom_zenz_live_correction_underline_color_ !=
          config.use_custom_zenz_live_correction_underline_color() ||
      initial_zenz_live_correction_underline_color_ !=
          config.zenz_live_correction_underline_color();

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
  if (QWidget* preview = findChild<QWidget*>("modeIndicatorLivePreview")) {
    preview->repaint();
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

    initial_use_custom_zenz_live_correction_text_color_ =
        config.use_custom_zenz_live_correction_text_color();
    initial_zenz_live_correction_text_color_ =
        config.zenz_live_correction_text_color();
    initial_use_custom_zenz_live_correction_background_color_ =
        config.use_custom_zenz_live_correction_background_color();
    initial_zenz_live_correction_background_color_ =
        config.zenz_live_correction_background_color();
    initial_use_custom_zenz_live_correction_underline_color_ =
        config.use_custom_zenz_live_correction_underline_color();
    initial_zenz_live_correction_underline_color_ =
        config.zenz_live_correction_underline_color();
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

constexpr uint32_t kDefaultPendingRomanDimnessPercent = 85;
constexpr uint32_t kMinPendingRomanDimnessPercent = 0;
constexpr uint32_t kMaxPendingRomanDimnessPercent = 100;

constexpr uint32_t kDefaultLiveConversionDelayMsec = 228;
constexpr uint32_t kMaxLiveConversionDelayMsec = 1000;
constexpr uint32_t kDefaultLiveConversionMinKeyLength = 2;
constexpr uint32_t kMinLiveConversionMinKeyLength = 1;
constexpr uint32_t kMaxLiveConversionMinKeyLength = 20;
constexpr uint32_t kDefaultZenzLiveCorrectionDelayMsec = 700;
constexpr uint32_t kMaxZenzLiveCorrectionDelayMsec = 5000;
constexpr uint32_t kDefaultZenzDeferredPresentationTimeoutMsec = 250;
constexpr uint32_t kMinZenzDeferredPresentationTimeoutMsec = 50;
constexpr uint32_t kMaxZenzDeferredPresentationTimeoutMsec = 3000;
constexpr uint32_t kDefaultZenzLiveCorrectionMinKeyLength = 2;
constexpr uint32_t kMinZenzLiveCorrectionMinKeyLength = 2;
constexpr uint32_t kMaxZenzLiveCorrectionMinKeyLength = 20;
constexpr uint32_t kMaxZenzLiveCorrectionRightContextLength = 128;
constexpr uint32_t kDefaultZenzAutoBlockMinimumRejectCount = 1;
constexpr uint32_t kMinZenzAutoBlockMinimumRejectCount = 1;
constexpr uint32_t kMaxZenzAutoBlockMinimumRejectCount = 999;
constexpr uint32_t kDefaultZenzAutoBlockMinimumRejectPercentage = 50;
constexpr uint32_t kMinZenzAutoBlockMinimumRejectPercentage = 1;
constexpr uint32_t kMaxZenzAutoBlockMinimumRejectPercentage = 100;

constexpr uint32_t kDefaultInputPreeditTextColor = 0xff5000;
constexpr uint32_t kDefaultInputPreeditBackgroundColor = 0xffffcc;
constexpr uint32_t kDefaultInputPreeditUnderlineColor = 0x30dcc8;

constexpr uint32_t kDefaultTargetPreeditTextColor = 0x000000;
constexpr uint32_t kDefaultTargetPreeditBackgroundColor = 0xddeeff;
constexpr uint32_t kDefaultTargetPreeditUnderlineColor = 0xc1a5ab;

constexpr uint32_t kDefaultZenzPreeditTextColor = 0x000000;
constexpr uint32_t kDefaultZenzPreeditBackgroundColor = 0xddeeff;
constexpr uint32_t kDefaultZenzPreeditUnderlineColor = 0xc4dc6c;

const config::Config::WindowsModeIndicatorCustomStyle&
GetDefaultModeIndicatorCustomStyle() {
  static const config::Config::WindowsModeIndicatorCustomStyle style;
  return style;
}

// BalloonImage uses a direct 2-D Gaussian convolution. Keep the custom
// indicator blur range close to its 4 px default to avoid disproportionate
// redraw cost, especially on high-DPI displays.
constexpr int kMaxModeIndicatorShadowBlur = 6;

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

void ShowJapaneseLongInformation(QWidget* parent,
                                 const QString& title,
                                 const QString& text) {
  QDialog dialog(parent);
  dialog.setWindowTitle(title);
  dialog.setModal(true);

  QVBoxLayout* root_layout = new QVBoxLayout(&dialog);

  QPlainTextEdit* text_view = new QPlainTextEdit(&dialog);
  text_view->setPlainText(text);
  text_view->setReadOnly(true);
  text_view->setFrameShape(QFrame::NoFrame);
  text_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  text_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  text_view->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  root_layout->addWidget(text_view, 1);

  QDialogButtonBox* button_box =
      new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  if (QPushButton* ok_button = button_box->button(QDialogButtonBox::Ok)) {
    ok_button->setText(QString::fromUtf8("OK"));
    ok_button->setDefault(true);
  }
  QObject::connect(button_box, &QDialogButtonBox::accepted,
                   &dialog, &QDialog::accept);
  root_layout->addWidget(button_box);

  int dialog_width = 960;
  int dialog_height = 720;
  QScreen* screen = parent != nullptr ? parent->screen() : nullptr;
  if (screen != nullptr) {
    const QRect available = screen->availableGeometry();
    constexpr int kScreenMargin = 64;
    dialog_width = std::min(
        dialog_width, std::max(1, available.width() - kScreenMargin));
    dialog_height = std::min(
        dialog_height, std::max(1, available.height() - kScreenMargin));
    dialog.setMaximumSize(
        std::max(1, available.width() - kScreenMargin),
        std::max(1, available.height() - kScreenMargin));
  }

  dialog.resize(dialog_width, dialog_height);
  if (screen != nullptr) {
    const QRect available = screen->availableGeometry();
    dialog.move(
        available.left() + (available.width() - dialog.width()) / 2,
        available.top() + (available.height() - dialog.height()) / 2);
  }
  dialog.exec();
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
  dialog.setWindowTitle(QString::fromUtf8("Zenz フィードバック学習データの管理"));
  dialog.resize(760, 440);

  session::ZenzFeedbackStore store;

  session::ZenzFeedbackReusePolicy reuse_policy;
  reuse_policy.minimum_key_length_enabled =
      current_config.use_zenz_feedback_min_key_length();
  reuse_policy.minimum_key_length = std::clamp<uint32_t>(
      current_config.zenz_feedback_min_key_length(), 1, 20);
  reuse_policy.auto_block_policy.enabled =
      current_config.use_zenz_auto_block_rejected_correction();
  reuse_policy.auto_block_policy.minimum_reject_count =
      static_cast<int>(current_config.zenz_auto_block_reject_threshold());
  reuse_policy.auto_block_policy.minimum_reject_percentage =
      static_cast<int>(current_config.zenz_auto_block_minimum_reject_percentage());

  QVBoxLayout* root_layout = new QVBoxLayout(&dialog);

  QWidget* description_widget = new QWidget(&dialog);
  QHBoxLayout* description_layout = new QHBoxLayout(description_widget);
  description_layout->setContentsMargins(0, 0, 0, 0);
  description_layout->setSpacing(8);

  QLabel* description_label = new QLabel(
      QString::fromUtf8(
          "Zenz 補正の採用・却下・手動ブロックの記録を管理します。"
          "記録は次回以降の候補順位や自動ブロックの判定に使われます。"
          "Mozc の通常変換履歴とは別に保存されています。"),
      &dialog);
  description_label->setWordWrap(true);

  QPushButton* details_button =
      new QPushButton(QString::fromUtf8("詳しく..."), &dialog);
  details_button->setFixedWidth(84);
  details_button->setToolTip(QString::fromUtf8(
      "状態ラベル、フィードバック学習の記録条件、通常の変換履歴との違いを表示します"));

  description_layout->addWidget(description_label, 1);
  description_layout->addWidget(details_button, 0, Qt::AlignTop);
  root_layout->addWidget(description_widget);

  QObject::connect(details_button, &QPushButton::clicked,
                   &dialog, [&]() {
                     ShowJapaneseLongInformation(
                         &dialog, dialog.windowTitle(),
                         QString::fromUtf8(
                             "【この画面で扱うデータ】\n"
                             "この画面で扱うのは、Zenz が検証・採用ポリシーを通過した"
                             "読み全体、補正後の候補、文脈クラス、採用/却下の記録です。"
                             "表示された Zenz 補正だけでなく、自動ブロック中に内部で"
                             "生成された同じ補正も、最終確定との比較対象になります。\n\n"
                             "フィードバック学習データは full-sequence 単位です。"
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
                             "また、表示された Zenz 補正を通常変換へ戻した後、または"
                             "自動ブロック中の hidden Zenz 補正に対して、同じ読み全体を"
                             "最終的に同じ値で確定した場合も採用として記録されます。"
                             "hidden match は Zenz feedback の統計だけを更新し、通常 Mozc "
                             "履歴への追加学習は行いません。\n\n"
                             "【いつ却下として記録されるか】\n"
                             "Zenz 補正が表示された後に Space や候補移動などで"
                             "通常変換へ戻り、最終的に Zenz 補正とは異なる値で"
                             "確定された場合、その Zenz 補正は却下として保存されます。"
                             "自動ブロック中も、hidden Zenz 補正と同じ読み全体を"
                             "異なる値で確定した場合は却下として記録されます。"
                             "これは通常候補を削除する命令ではなく、次回以降の"
                             "候補順位や自動ブロック判定を調整するための"
                             "弱いマイナス信号です。\n\n"
                             "【記録されない操作】\n"
                             "対応する visible / hidden Zenz 観測がない通常変換、通常候補の"
                             "選択、通常変換の確定は、この画面のフィードバック学習スコアには"
                             "影響しません。hidden Zenz の読みより入力が伸びた場合も、"
                             "別の full-sequence として扱い、その古い観測は数えません。"
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
                             "自動ブロック中: auto-block が ON で、同じ exact 文脈クラスの"
                             "通常却下回数が最低拒否回数以上、かつ拒否割合が最低拒否割合以上"
                             "です。拒否割合は 通常却下 / (採用 + 通常却下) で計算します。"
                             "TSV に hard reject を固定保存するのではなく、現在の設定と"
                             "観測履歴から動的に判定し、hidden match が増えて割合が下がれば"
                             "自動解除されます。\n"
                             "手動ブロック中: この画面で明示的にブロックされた状態です。"
                             "解除したい場合は、該当エントリを削除して必要に応じて"
                             "再学習してください。\n"
                             "中立: 優先にもブロックにも使うだけの有効な信号がない状態です。\n\n"
                             "【短い読みの自動 feedback 制限】\n"
                             "既定では7文字未満の読みは採用・却下履歴を集計しますが、"
                             "保存済み補正の優先・即時適用・自動ブロックには使いません。"
                             "手動ブロックだけは文字数に関係なく有効です。"
                             "対象外かどうかは現在の設定から動的に決まります。\n\n"
                             "【通常の変換履歴との違い】\n"
                             "Zenz の結果を確定した場合、条件によっては通常の変換履歴にも"
                             "反映されます。また、安全に判断できる場合は、直前の通常 Mozc "
                             "ライブ変換文節列へ逆投影し、文節列全体を通常変換履歴に近い形で"
                             "反映します。このとき、Zenz が実際に直した文節だけを"
                             "強い選択履歴として扱います。\n\n"
                             "そのため、この画面でフィードバック学習データを削除しても、"
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
  table->setColumnCount(8);
  table->setHorizontalHeaderLabels(QStringList()
                                   << QString::fromUtf8("読み")
                                   << QString::fromUtf8("候補")
                                   << QString::fromUtf8("文脈クラス")
                                   << QString::fromUtf8("採用")
                                   << QString::fromUtf8("却下")
                                   << QString::fromUtf8("拒否割合")
                                   << QString::fromUtf8("状態")
                                   << QString::fromUtf8("feedback適用"));
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
    QTableWidgetItem* reason_item = table->item(row, 6);
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
        store.ListEntries(reuse_policy);

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
      SetTableItem(
          table, row, 5,
          entry.hard_rejected
              ? QString::fromUtf8("-")
              : QString::fromUtf8("%1 %")
                    .arg(entry.auto_block_reject_percentage));
      QString feedback_status = FeedbackReasonLabel(entry.reason);
      if (entry.reuse_excluded && entry.auto_blocked) {
        feedback_status = QString::fromUtf8("自動ブロック条件成立");
      }
      SetTableItem(table, row, 6, feedback_status);
      SetTableItem(
          table, row, 7,
          entry.hard_rejected
              ? QString::fromUtf8("手動ブロック有効")
              : entry.reuse_excluded
                    ? QString::fromUtf8("対象外（記録のみ）")
                    : QString::fromUtf8("有効"));
      if (entry.reuse_excluded) {
        table->item(row, 0)->setToolTip(QString::fromUtf8(
            "短い読みのため自動 feedback 適用対象外。記録・集計は継続します。"));
      }

      table->item(row, 0)->setData(Qt::UserRole, key);
      table->item(row, 1)->setData(Qt::UserRole, value);
      table->item(row, 2)->setData(Qt::UserRole, context_class);
      table->item(row, 6)->setData(Qt::UserRole, ToQString(entry.reason));

      ++visible_count;
    }

    table->resizeColumnsToContents();

    const QString auto_block_status =
        reuse_policy.auto_block_policy.enabled
            ? QString::fromUtf8(
                  " / 自動ブロック条件 最低拒否 %1 回 / %2 %")
                  .arg(reuse_policy.auto_block_policy.minimum_reject_count)
                  .arg(reuse_policy.auto_block_policy.minimum_reject_percentage)
            : QString::fromUtf8(" / 自動ブロック OFF");
    const QString min_key_status =
        reuse_policy.minimum_key_length_enabled
            ? QString::fromUtf8(" / 自動feedback %1文字以上")
                  .arg(reuse_policy.minimum_key_length)
            : QString::fromUtf8(" / 読み文字数制限 OFF");
    status_label->setText(
        QString::fromUtf8("表示 %1 件 / 全 %2 件%3%4")
            .arg(visible_count)
            .arg(static_cast<int>(entries.size()))
            .arg(auto_block_status)
            .arg(min_key_status));

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
                         QString::fromUtf8("フィードバック学習データをエクスポート"),
                         QStringLiteral("zenz_feedback.tsv"),
                         QString::fromUtf8(
                             "TSV ファイル (*.tsv);;すべてのファイル (*)"));
                     if (path.isEmpty()) {
                       return;
                     }

                     if (!store.ExportToFile(path.toUtf8().toStdString())) {
                       ShowJapaneseCritical(
                           &dialog, dialog.windowTitle(),
                           QString::fromUtf8(
                               "フィードバック学習データをエクスポートできませんでした。"));
                       return;
                     }

                     ShowJapaneseInformation(
                         &dialog, dialog.windowTitle(),
                         QString::fromUtf8(
                             "フィードバック学習データをエクスポートしました。"));
                   });

  QObject::connect(import_button, &QPushButton::clicked,
                   &dialog, [&]() {
                     const QString path = QFileDialog::getOpenFileName(
                         &dialog,
                         QString::fromUtf8("フィードバック学習データをインポート"),
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
                         QString::fromUtf8("フィードバック学習データをインポートします。"));
                     message_box.setInformativeText(
                         QString::fromUtf8(
                             "既存のフィードバック学習データに追加しますか？\n\n"
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

                     if (!store.ImportFromFile(path.toUtf8().toStdString(),
                                               import_mode)) {
                       ShowJapaneseCritical(
                           &dialog, dialog.windowTitle(),
                           QString::fromUtf8(
                               "フィードバック学習データをインポートできませんでした。\n"
                               "ファイル形式が壊れているか、未対応の行が含まれています。"));
                       return;
                     }

                     reload_table();

                     ShowJapaneseInformation(
                         &dialog, dialog.windowTitle(),
                         QString::fromUtf8(
                             "フィードバック学習データをインポートしました。"));
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
                         QString::fromUtf8("選択したフィードバック学習エントリを削除しますか？"));
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
                               "フィードバック学習エントリを削除できませんでした。"));
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
                         QString::fromUtf8("フィードバック学習データをすべて削除しますか？"));
                     message_box.setInformativeText(
                         QString::fromUtf8(
                             "この操作は元に戻せません。\n"
                             "Mozc の通常変換履歴に追加学習した内容は削除されません。"));

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
                               "フィードバック学習データを削除できませんでした。"));
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
// as index 0, the black/white monochrome icons are indices 15 and 16, and the
// static high-contrast variant that preserves the black icon silhouette is
// expected at index 17.  Validate the built DLL whenever this list changes.
// Keep these values in sync with resource-order validation
// whenever the TIP icon resource list is changed.
constexpr int kTsfProfileIconIndexSimpleBlack = 15;
constexpr int kTsfProfileIconIndexSimpleWhite = 16;
constexpr int kTsfProfileIconIndexSimpleHighContrast = 17;

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
    case config::Config::WINDOWS_IME_ICON_MONOCHROME_HIGH_CONTRAST:
      return kTsfProfileIconIndexSimpleHighContrast;
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
    case config::Config::WINDOWS_IME_ICON_MONOCHROME_HIGH_CONTRAST:
      return L"monochrome_high_contrast";
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
  combo->addItem(
      TrConfigDialog("Monochrome (Light/Dark)"),
      static_cast<int>(
          config::Config::WINDOWS_IME_ICON_MONOCHROME_HIGH_CONTRAST));
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
namespace {
// Follow the Qt application's translation where provided. Existing .qm files
// predate this dynamically created section; Japanese OS users still receive a
// native Japanese fallback without forcing Japanese on English installations.
QString StatusUiText(const char* english, const char* japanese) {
  const QString translated = QCoreApplication::translate("ConfigDialog", english);
  if (translated != QString::fromUtf8(english)) return translated;
  return QLocale::system().language() == QLocale::Japanese
             ? QString::fromUtf8(japanese) : translated;
}

// Presets own the entire style, except the enabled state.  Default font and
// automatic placement are intentional parts of each preset.
config::Config::ConversionStatusIndicatorStyle ConversionStatusPreset(int preset) {
  config::Config::ConversionStatusIndicatorStyle style;
  if (preset == 1) {
    style.set_shape(config::Config::CONVERSION_STATUS_CIRCLE);
    style.set_label(config::Config::CONVERSION_STATUS_COLOR_ONLY);
    style.set_height(14);
    style.set_opacity_percent(90);
    style.set_shadow_size(0);
    style.set_shadow_opacity_percent(0);
    style.set_shadow_distance(0);
  } else if (preset == 2) {
    style.set_width(84);
    style.set_height(27);
    style.set_font_size(13);
  }
  return style;
}

// Compare logical values, not SerializeAsString(): proto2 field presence is
// different for freshly created defaults and settings saved by the UI.
bool SameConversionStatusStyle(
    const config::Config::ConversionStatusIndicatorStyle& a,
    const config::Config::ConversionStatusIndicatorStyle& b) {
  return a.shape() == b.shape() && a.label() == b.label() &&
         a.placement() == b.placement() &&
         a.mozc_color() == b.mozc_color() &&
         a.zenz_pending_color() == b.zenz_pending_color() &&
         a.zenz_color() == b.zenz_color() &&
         a.reading_color() == b.reading_color() &&
         a.text_color() == b.text_color() &&
         a.border_color() == b.border_color() &&
         a.width() == b.width() && a.height() == b.height() &&
         a.font_size() == b.font_size() &&
         a.font_family() == b.font_family() &&
         a.corner_radius() == b.corner_radius() &&
         a.border_width() == b.border_width() &&
         a.opacity_percent() == b.opacity_percent() &&
         a.shadow_size() == b.shadow_size() &&
         a.shadow_opacity_percent() == b.shadow_opacity_percent() &&
         a.shadow_angle_degrees() == b.shadow_angle_degrees() &&
         a.shadow_distance() == b.shadow_distance() &&
         a.offset_x() == b.offset_x() && a.offset_y() == b.offset_y() &&
         a.gap() == b.gap();
}

class ConversionStatusPreviewWidget final : public QWidget {
 public:
  explicit ConversionStatusPreviewWidget(QWidget* parent) : QWidget(parent) {
    setFixedHeight(98);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAutoFillBackground(false);
  }
  void Configure(const config::Config::ConversionStatusIndicatorStyle& style) {
    style_.CopyFrom(style);
    update();
  }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    const int desired = width() < 520 ? 166 : 98;
    if (height() != desired) setFixedHeight(desired);
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor canvas = palette().color(QPalette::Base);
    QColor edge = palette().color(QPalette::Mid);
    edge.setAlpha(90);
    p.setPen(Qt::NoPen);
    p.setBrush(canvas);
    p.drawRoundedRect(QRectF(rect()), 9, 9);
    p.setPen(QPen(edge, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);

    // Four independent status samples.  At narrow widths, flow into two rows.
    // Neither the host application's preedit nor ruby affects this preview.
    const int columns = width() < 520 ? 2 : 4;
    const int rows = 4 / columns;
    const double cell_width = (width() - 20.0) / columns;
    const double cell_height = (height() - 12.0) / rows;
    const QString labels[4] = {
        StatusUiText("Reading", "入力中"), QStringLiteral("Mozc"),
        StatusUiText("Zenz processing", "Zenz処理中"),
        StatusUiText("Zenz applied", "Zenz適用済み")};
    const char* words[4] = {"入力中", "Mozc", "Zenz…", "Zenz"};
    const char* symbols[4] = {"あ", "M", "…", "Z"};
    const uint32_t colors[4] = {
        style_.reading_color(), style_.mozc_color(),
        style_.zenz_pending_color(), style_.zenz_color()};
    const auto shape = style_.shape();
    const auto mode = shape == config::Config::CONVERSION_STATUS_CIRCLE &&
                              style_.label() == config::Config::CONVERSION_STATUS_FULL_LABEL
                          ? config::Config::CONVERSION_STATUS_SYMBOL : style_.label();
    QFont label_font = font();
    if (!style_.font_family().empty()) {
      label_font.setFamily(QString::fromStdString(style_.font_family()));
    }
    label_font.setPointSize(std::clamp(static_cast<int>(style_.font_size()), 8, 24));
    label_font.setWeight(QFont::Medium);
    const QFontMetrics metrics(label_font);
    const int h = std::clamp(static_cast<int>(style_.height()), 12, 100);
    const int stroke = std::clamp(static_cast<int>(style_.border_width()), 0, 5);
    const int rgb_text = static_cast<int>(style_.text_color());
    const QColor foreground((rgb_text >> 16) & 255, (rgb_text >> 8) & 255,
                            rgb_text & 255);
    const int shadow_size = std::clamp(static_cast<int>(style_.shadow_size()), 0, 15);
    const int shadow_alpha = std::clamp(static_cast<int>(style_.shadow_opacity_percent()), 0, 100);
    const double angle = style_.shadow_angle_degrees() * 3.141592653589793 / 180.0;
    const double distance = style_.shadow_distance();
    const QPointF shadow_shift(std::cos(angle) * distance,
                               std::sin(angle) * distance);
    for (int state = 0; state < 4; ++state) {
      const int col = state % columns;
      const int row = state / columns;
      const QRectF cell(10.0 + col * cell_width,
                        6.0 + row * cell_height, cell_width, cell_height);
      p.save();
      p.setClipRect(cell.adjusted(1, 1, -1, -1));
      QFont caption_font = font();
      caption_font.setPointSizeF(std::max(8.0, caption_font.pointSizeF() - 1.0));
      p.setFont(caption_font);
      p.setPen(palette().color(QPalette::Text));
      p.drawText(QRectF(cell.left() + 3, cell.top() + 2, cell.width() - 6, 18),
                 Qt::AlignHCenter | Qt::AlignVCenter, labels[state]);
      const QString title = mode == config::Config::CONVERSION_STATUS_COLOR_ONLY
          ? QString() : QString::fromUtf8(
                mode == config::Config::CONVERSION_STATUS_SYMBOL ? symbols[state] : words[state]);
      int w = std::clamp(static_cast<int>(style_.width()), 12, 240);
      if (shape == config::Config::CONVERSION_STATUS_CIRCLE) {
        w = h;
      } else if (!title.isEmpty()) {
        w = std::max(w, metrics.horizontalAdvance(title) + 28);
      }
      // Scale only when a user-selected chip exceeds its cell.  This keeps
      // four large custom labels visible instead of clipping them.
      const double zoom = std::min({1.0,
          (cell.width() - 14) / (w + shadow_size + std::abs(shadow_shift.x()) + 2.0),
          (cell.height() - 30) / (h + shadow_size + std::abs(shadow_shift.y()) + 2.0)});
      p.translate(cell.center().x(), cell.top() + cell.height() / 2 + 10);
      p.scale(std::max(0.1, zoom), std::max(0.1, zoom));
      QRectF chip(-w / 2.0, -h / 2.0, w, h);
      const int radius = shape == config::Config::CONVERSION_STATUS_CIRCLE ||
                                 shape == config::Config::CONVERSION_STATUS_PILL
          ? h / 2 : std::clamp(static_cast<int>(style_.corner_radius()), 0, h / 2);
      if (shadow_size > 0 && shadow_alpha > 0) {
        for (int i = shadow_size; i > 0; --i) {
          QColor shadow(0, 0, 0,
              std::clamp(shadow_alpha * (shadow_size - i + 1) /
                        (shadow_size * 2), 0, 80));
          p.setPen(Qt::NoPen);
          p.setBrush(shadow);
          p.drawRoundedRect(chip.adjusted(-i / 2.0, -i / 2.0,
                                          i / 2.0, i / 2.0).translated(shadow_shift),
                            radius + i / 2.0, radius + i / 2.0);
        }
      }
      p.save();
      p.setOpacity(std::clamp(style_.opacity_percent(), 20u, 100u) / 100.0);
      const uint32_t rgb = colors[state];
      const QColor base((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
      QLinearGradient gradient(chip.topLeft(), chip.bottomLeft());
      gradient.setColorAt(0, base.lighter(113));
      gradient.setColorAt(1, base);
      p.setBrush(gradient);
      if (stroke > 0) {
        const uint32_t border = style_.border_color();
        p.setPen(QPen(QColor((border >> 16) & 255, (border >> 8) & 255,
                             border & 255), stroke));
      } else {
        p.setPen(Qt::NoPen);
      }
      p.drawRoundedRect(chip, radius, radius);
      if (!title.isEmpty()) {
        QRectF label_rect = chip;
        if (shape != config::Config::CONVERSION_STATUS_CIRCLE &&
            mode == config::Config::CONVERSION_STATUS_FULL_LABEL) {
          p.setPen(Qt::NoPen);
          QColor dot = foreground;
          dot.setAlpha(215);
          p.setBrush(dot);
          p.drawEllipse(QPointF(chip.left() + 12, chip.center().y()), 2.5, 2.5);
          label_rect.adjust(17, 0, -3, 0);
        }
        p.setPen(foreground);
        p.setFont(label_font);
        p.drawText(label_rect, Qt::AlignCenter, title);
      }
      p.restore();
      p.restore();
    }
  }

 private:
  config::Config::ConversionStatusIndicatorStyle style_;
};
#ifdef _WIN32
// Keep the standard palette constants in one place: the live preview and
// "Load light/dark colors" actions must use exactly the same values.
// These are the palettes used by renderer/win32/indicator_window.cc.
struct ModeIndicatorPalette {
  uint32_t background;
  uint32_t border;
  uint32_t shadow;
  uint32_t text;
};

constexpr ModeIndicatorPalette kModeIndicatorLightPalettes[3] = {
    {0xf5f8fc, 0x5c7491, 0x788291, 0x1c2634},
    {0xebfcf8, 0x24968c, 0x69918a, 0x124e48},
    {0xf4f4ff, 0x626cd7, 0x78789b, 0x303469},
};
constexpr ModeIndicatorPalette kModeIndicatorDarkPalettes[3] = {
    {0x1e2634, 0x6082aa, 0x000000, 0xebf2fa},
    {0x14403d, 0x46beb4, 0x000000, 0xf5ffff},
    {0x2a2c46, 0x7e87f5, 0x000000, 0xf5f5ff},
};

// Read the editor controls directly so that the preview includes unsaved
// changes. The real overlay uses BalloonImage/GDI; this is a Qt approximation
// with the same theme colors, nominal geometry and mode labels.
class ModeIndicatorPreviewWidget final : public QWidget {
 public:
  ModeIndicatorPreviewWidget(QWidget* settings, QWidget* parent)
      : QWidget(parent), settings_(settings) {
    setFixedHeight(122);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAutoFillBackground(false);
  }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    // Three labels and compact glyphs fit side by side above 310 logical px.
    // At 150% Windows scaling, a visually wide preview can be <450 logical px.
    const int desired = width() < 310 ? 224 : 122;
    if (height() != desired) setFixedHeight(desired);
  }

  void paintEvent(QPaintEvent*) override {
    const auto& defaults = GetDefaultModeIndicatorCustomStyle();
    auto spin = [this](const char* name, int fallback) {
      const auto* widget = settings_->findChild<QSpinBox*>(name);
      return widget != nullptr ? widget->value() : fallback;
    };
    auto color = [this](const char* name, uint32_t fallback) {
      return GetColorButtonRgb(settings_->findChild<QPushButton*>(name),
                               fallback);
    };
    const auto* theme_combo =
        settings_->findChild<QComboBox*>("modeIndicatorThemeComboBox");
    const int theme = theme_combo != nullptr
        ? theme_combo->currentData().toInt()
        : static_cast<int>(config::Config::WINDOWS_MODE_INDICATOR_THEME_SYSTEM);
    const bool custom = theme ==
        static_cast<int>(config::Config::WINDOWS_MODE_INDICATOR_THEME_CUSTOM);
    const bool dark = renderer::ResolveWindowsModeIndicatorTheme(
        static_cast<config::Config::WindowsModeIndicatorTheme>(theme),
        renderer::GetSystemColorTheme()) == renderer::SystemColorTheme::kDark;

    const ModeIndicatorPalette custom_colors[3] = {
        {color("modeIndicatorAsciiBackgroundColorButton", defaults.ascii_background_color()),
         color("modeIndicatorAsciiBorderColorButton", defaults.ascii_border_color()),
         color("modeIndicatorAsciiShadowColorButton", defaults.ascii_shadow_color()),
         color("modeIndicatorAsciiTextColorButton", defaults.ascii_text_color())},
        {color("modeIndicatorHiraganaBackgroundColorButton", defaults.hiragana_background_color()),
         color("modeIndicatorHiraganaBorderColorButton", defaults.hiragana_border_color()),
         color("modeIndicatorHiraganaShadowColorButton", defaults.hiragana_shadow_color()),
         color("modeIndicatorHiraganaTextColorButton", defaults.hiragana_text_color())},
        {color("modeIndicatorKatakanaBackgroundColorButton", defaults.katakana_background_color()),
         color("modeIndicatorKatakanaBorderColorButton", defaults.katakana_border_color()),
         color("modeIndicatorKatakanaShadowColorButton", defaults.katakana_shadow_color()),
         color("modeIndicatorKatakanaTextColorButton", defaults.katakana_text_color())},
    };
    const ModeIndicatorPalette* palettes = custom ? custom_colors
        : (dark ? kModeIndicatorDarkPalettes : kModeIndicatorLightPalettes);
    const int chip_width = custom
        ? spin("modeIndicatorWidthSpinBox", defaults.width()) : 36;
    const int chip_height = custom
        ? spin("modeIndicatorHeightSpinBox", defaults.height()) : 28;
    const int corner = custom
        ? spin("modeIndicatorCornerRadiusSpinBox", defaults.corner_radius()) : 8;
    const int label_size = custom
        ? spin("modeIndicatorLabelSizeSpinBox", defaults.label_size()) : 12;
    const int border = custom
        ? spin("modeIndicatorBorderThicknessSpinBox", defaults.border_thickness()) : 1;
    const int blur = custom
        ? spin("modeIndicatorShadowBlurSpinBox", defaults.shadow_blur()) : 4;
    const int opacity = custom
        ? spin("modeIndicatorShadowOpacitySpinBox", defaults.shadow_opacity_percent())
        : (dark ? 25 : 22);
    const int shadow_x = custom
        ? spin("modeIndicatorShadowOffsetXSpinBox", defaults.shadow_offset_x()) : 0;
    const int shadow_y = custom
        ? spin("modeIndicatorShadowOffsetYSpinBox", defaults.shadow_offset_y()) : 1;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Base));
    painter.drawRoundedRect(QRectF(rect()), 9, 9);
    QColor outline = palette().color(QPalette::Mid);
    outline.setAlpha(90);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(outline, 1));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);

    const bool narrow = width() < 310;
    const double cell_width = narrow ? width() - 20.0 : (width() - 20.0) / 3;
    const double cell_height = narrow ? (height() - 12.0) / 3 : height() - 12.0;
    const QString captions[3] = {
        StatusUiText("Alphanumeric", "英数"),
        StatusUiText("Hiragana", "ひらがな"),
        StatusUiText("Katakana", "カタカナ")};
    const QString glyphs[3] = {
        QStringLiteral("A"), QString::fromUtf8("あ"), QString::fromUtf8("ア")};
    QFont caption_font = font();
    caption_font.setPointSizeF(std::max(8.0, caption_font.pointSizeF() - 1.0));
    QFont glyph_font = font();
    glyph_font.setPointSize(std::clamp(label_size, 8, 32));

    for (int i = 0; i < 3; ++i) {
      const QRectF cell(10.0 + (narrow ? 0 : i) * cell_width,
                        6.0 + (narrow ? i : 0) * cell_height,
                        cell_width, cell_height);
      painter.save();
      painter.setClipRect(cell.adjusted(2, 2, -2, -2));
      painter.setFont(caption_font);
      painter.setPen(palette().color(QPalette::Text));
      painter.drawText(QRectF(cell.left(), cell.top() + 2,
                              cell.width(), 19),
                       Qt::AlignCenter, captions[i]);
      const double shadow_extent = blur * 2.0 +
          std::max(std::abs(shadow_x), std::abs(shadow_y));
      const double scale = std::clamp(std::min(
          (cell.width() - 18.0) / (chip_width + shadow_extent + 6.0),
          (cell.height() - 30.0) / (chip_height + shadow_extent + 6.0)),
          0.1, 1.0);
      painter.translate(cell.center().x(), cell.center().y() + 10);
      painter.scale(scale, scale);
      const QRectF chip(-chip_width / 2.0, -chip_height / 2.0,
                        chip_width, chip_height);
      const double radius = std::clamp(static_cast<double>(corner), 0.0,
                                       std::min(chip.width(), chip.height()) / 2.0);
      for (int spread = blur; spread > 0 && opacity > 0; --spread) {
        QColor shade = RgbHexToQColor(palettes[i].shadow);
        shade.setAlpha(std::clamp(opacity * (blur - spread + 1) * 255 /
                                  (blur * 100 * 2), 0, 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(shade);
        painter.drawRoundedRect(
            chip.adjusted(-spread, -spread, spread, spread)
                .translated(shadow_x, shadow_y),
            radius + spread, radius + spread);
      }
      painter.setBrush(RgbHexToQColor(palettes[i].background));
      painter.setPen(border > 0
                         ? QPen(RgbHexToQColor(palettes[i].border), border)
                         : QPen(Qt::NoPen));
      painter.drawRoundedRect(chip, radius, radius);
      painter.setPen(RgbHexToQColor(palettes[i].text));
      painter.setFont(glyph_font);
      painter.drawText(chip, Qt::AlignCenter | Qt::TextSingleLine, glyphs[i]);
      painter.restore();
    }
  }

 private:
  QWidget* settings_;
};
#endif  // _WIN32

// A settings-only approximation of the renderer output.  This never creates
// a renderer window or writes to ConfigHandler: it reads the unsaved Qt widgets.
class RendererWindowAppearancePreview final : public QWidget {
 public:
  enum class Kind { kCandidate, kSuggestion, kRuby };

  RendererWindowAppearancePreview(QWidget* settings, Kind kind, QWidget* parent)
      : QWidget(parent), settings_(settings), kind_(kind) {
    setFixedHeight(170);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void Refresh() {
    if (isVisible()) {
      repaint();
    } else {
      update();
    }
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor base = palette().color(QPalette::Base);
    QColor outline = palette().color(QPalette::Mid);
    outline.setAlpha(95);
    painter.setPen(Qt::NoPen);
    painter.setBrush(base);
    painter.drawRoundedRect(QRectF(rect()), 8, 8);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(outline, 1));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                            8, 8);

    auto get_value = [this](const QString& name, int fallback) {
      const QSpinBox* control = settings_->findChild<QSpinBox*>(name);
      return control ? control->value() : fallback;
    };
    auto get_theme = [this](const char* name) {
      return static_cast<config::Config::RendererWindowColorTheme>(
          GetComboCurrentData(FindComboBox(settings_, name),
                              config::Config::RENDERER_WINDOW_COLOR_AUTO));
    };

    const auto system_theme = renderer::GetSystemColorTheme();
    const auto candidate_theme = renderer::ResolveRendererWindowColorTheme(
        get_theme("candidateWindowColorThemeComboBox"), system_theme);
    const auto target_theme = kind_ == Kind::kCandidate
        ? candidate_theme
        : renderer::ResolveDependentRendererWindowColorTheme(
              get_theme(kind_ == Kind::kSuggestion
                            ? "suggestWindowColorThemeComboBox"
                            : "rubyWindowColorThemeComboBox"),
              candidate_theme, system_theme);
    const bool dark = target_theme ==
        config::Config::RENDERER_WINDOW_COLOR_DARK;
    const QString candidate_prefix = QStringLiteral("candidateWindow");
    const auto candidate_palette =
        candidate_theme == config::Config::RENDERER_WINDOW_COLOR_CUSTOM
            ? GetCandidatePaletteButtons(settings_, candidate_prefix,
                                         kLightCandidatePalette)
            : (candidate_theme == config::Config::RENDERER_WINDOW_COLOR_DARK
                   ? kDarkCandidatePalette : kLightCandidatePalette);

    const bool follows_candidate = kind_ != Kind::kCandidate &&
        get_theme(kind_ == Kind::kSuggestion
                      ? "suggestWindowColorThemeComboBox"
                      : "rubyWindowColorThemeComboBox") ==
            config::Config::RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE;
    CandidateWindowPaletteDefaults candidate_like =
        dark ? kDarkCandidatePalette : kLightCandidatePalette;
    if (kind_ == Kind::kCandidate || follows_candidate) {
      candidate_like = candidate_palette;
    } else if (target_theme == config::Config::RENDERER_WINDOW_COLOR_CUSTOM &&
               kind_ == Kind::kSuggestion) {
      candidate_like = GetCandidatePaletteButtons(
          settings_, QStringLiteral("suggestWindow"), kLightCandidatePalette);
    }
    RubyWindowPaletteDefaults ruby =
        dark ? kDarkRubyPalette : kLightRubyPalette;
    if (follows_candidate) {
      ruby = {candidate_palette.background_color,
              candidate_palette.text_color, candidate_palette.border_color};
    } else if (kind_ == Kind::kRuby &&
               target_theme == config::Config::RENDERER_WINDOW_COLOR_CUSTOM) {
      ruby = GetRubyPaletteButtons(settings_, QStringLiteral("rubyWindow"),
                                   kLightRubyPalette);
    }

    const QString prefix = kind_ == Kind::kCandidate
        ? QStringLiteral("candidateWindow")
        : (kind_ == Kind::kSuggestion
               ? QStringLiteral("suggestWindow")
               : QStringLiteral("rubyWindow"));
    const int size_percent = get_value(prefix + "SizePercentSpinBox", 100);
    const int corner = get_value(prefix + "CornerRadiusSpinBox",
                                 kind_ == Kind::kRuby ? 9 : 6);
    const int opacity = get_value(prefix + "OpacityPercentSpinBox",
                                  kind_ == Kind::kRuby ? 90 : 100);
    const int spread = get_value(prefix + "ShadowSizeSpinBox", 5);
    const int shadow_opacity = get_value(
        prefix + "ShadowOpacityPercentSpinBox", kind_ == Kind::kRuby ? 8 : 10);
    const int distance = get_value(prefix + "ShadowDistanceSpinBox",
                                   kind_ == Kind::kRuby ? 3 : 6);
    const int angle = get_value(prefix + "ShadowAngleDegreesSpinBox", 45);
    int font_weight = 400;
    if (const auto* combo = FindComboBox(
            settings_, (prefix + "FontWeightComboBox").toLatin1().constData())) {
      font_weight = GetComboCurrentData(combo, 400);
    }

    QFont text_font = font();
    const char* font_picker = kind_ == Kind::kCandidate
        ? "candidateRubyFontComboBox"
        : (kind_ == Kind::kSuggestion
               ? "suggestWindowFontNameComboBox"
               : "rubyWindowFontNameComboBox");
    if (const QComboBox* font_combo = FindComboBox(settings_, font_picker)) {
      const QString family = font_combo->currentData().toString();
      if (!family.isEmpty()) {
        text_font.setFamily(family);
      }
    }
    text_font.setWeight(static_cast<QFont::Weight>(
        std::clamp(font_weight, 100, 900)));
    const double size_factor = size_percent / 100.0;
    text_font.setPointSizeF(kind_ == Kind::kRuby
        ? std::clamp(13.0 * size_factor, 10.4, 26.0)
        : std::clamp(10.0 * size_factor, 8.0, 18.0));

    // The real Windows ruby window sizes itself to the measured reading text
    // plus horizontal/vertical padding, rather than using a fixed 225x60 box.
    // A short reading makes the representative live-conversion overlay clear.
    const QString ruby_reading = QStringLiteral("きょうは");
    const QString ruby_composition = QStringLiteral("今日は");
    const QFontMetricsF ruby_metrics(text_font);
    // Native ruby spacing uses 144 DPI as its design baseline. Qt measures
    // here in logical pixels, so normalize the spacing for the preview.
    const double ruby_dpi_factor =
        std::clamp(logicalDpiX() / 144.0, 0.5, 2.0);
    const double ruby_pad_x =
        get_value("rubyWindowHorizontalPaddingSpinBox", 14) *
        size_factor * ruby_dpi_factor;
    const double ruby_pad_y =
        get_value("rubyWindowVerticalPaddingSpinBox", 6) *
        size_factor * ruby_dpi_factor;
    const double target_width = kind_ == Kind::kRuby
        ? ruby_metrics.horizontalAdvance(ruby_reading) + 2 * ruby_pad_x
        : 275.0 * size_factor;
    const double target_height = kind_ == Kind::kRuby
        ? ruby_metrics.height() + 2 * ruby_pad_y
        : 105.0 * size_factor;
    const double available_width = std::max(80, width() - 58);
    const double scale = std::min({1.20, available_width / target_width,
                                   (kind_ == Kind::kRuby ? 100.0 : 130.0) /
                                       target_height});
    painter.save();
    painter.translate(width() / 2.0, height() / 2.0 + 6);
    painter.scale(scale, scale);
    const QRectF window(-target_width / 2, -target_height / 2,
                        target_width, target_height);
    const double radius = std::clamp<double>(corner, 0.0, 24.0);
    const double radians = angle * 3.141592653589793 / 180.0;
    const double dx = std::cos(radians) * distance;
    const double dy = std::sin(radians) * distance;
    // A lightweight approximate shadow, kept separate from production GDI.
    for (int i = std::min(spread, 16); i >= 1; --i) {
      QColor shade = Qt::black;
      shade.setAlpha(std::clamp(shadow_opacity * 255 /
                                    (100 * std::max(1, std::min(spread, 16))),
                                0, 255));
      painter.setPen(Qt::NoPen);
      painter.setBrush(shade);
      painter.drawRoundedRect(window.adjusted(-i, -i, i, i).translated(dx, dy),
                              radius + i, radius + i);
    }
    painter.setOpacity(opacity / 100.0);
    const QColor background = RgbHexToQColor(
        kind_ == Kind::kRuby ? ruby.background_color
                             : candidate_like.background_color);
    const QColor border = RgbHexToQColor(
        kind_ == Kind::kRuby ? ruby.border_color
                             : candidate_like.border_color);
    const QColor foreground = RgbHexToQColor(
        kind_ == Kind::kRuby ? ruby.text_color
                             : candidate_like.text_color);
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(window, radius, radius);

    QPainterPath clip;
    clip.addRoundedRect(window.adjusted(1, 1, -1, -1), radius, radius);
    painter.setClipPath(clip);
    painter.setFont(text_font);
    if (kind_ == Kind::kRuby) {
      const QRectF ruby_text = window.adjusted(
          ruby_pad_x, ruby_pad_y, -ruby_pad_x, -ruby_pad_y);
      painter.setPen(foreground);
      painter.drawText(ruby_text, Qt::AlignCenter | Qt::TextSingleLine,
                       ruby_reading);
      painter.setClipping(false);
      painter.setOpacity(1.0);
      painter.setPen(palette().color(QPalette::Text));
      // The reading overlay floats above the composition text. The gap is
      // the configured distance from the real composition, not inner padding.
      const double gap = get_value("rubyWindowCompositionGapSpinBox", 4) *
                         size_factor * ruby_dpi_factor;
      QFont composition_font = font();
      composition_font.setPointSizeF(12.0);
      painter.setFont(composition_font);
      const QFontMetricsF composition_metrics(composition_font);
      painter.drawText(QRectF(window.center().x() -
                                  composition_metrics.horizontalAdvance(
                                      ruby_composition) / 2.0,
                              window.bottom() + gap,
                              composition_metrics.horizontalAdvance(
                                  ruby_composition),
                              composition_metrics.height()),
                       Qt::AlignCenter | Qt::TextSingleLine,
                       ruby_composition);
    } else {
      const QString candidates[] = {
          QStringLiteral("今日はいい天気です"),
          QStringLiteral("今日もいい天気です"),
          QStringLiteral("今日は良い天気")};
      const double row_height = std::max(19.0, (window.height() - 25) / 3.0);
      for (int i = 0; i < 3; ++i) {
        const QRectF row(window.left() + 5,
                         window.top() + 2 + row_height * i,
                         window.width() - 10, row_height);
        if (i == 0) {
          painter.setPen(QPen(RgbHexToQColor(
              candidate_like.selected_border_color), 1));
          painter.setBrush(RgbHexToQColor(
              candidate_like.selected_background_color));
          painter.drawRoundedRect(row, 3, 3);
        }
        const QRectF shortcut(row.left() + 2, row.top() + 3, 24, row.height() - 6);
        painter.fillRect(shortcut, RgbHexToQColor(
            candidate_like.shortcut_background_color));
        painter.setFont(text_font);
        painter.setPen(RgbHexToQColor(candidate_like.shortcut_text_color));
        painter.drawText(shortcut, Qt::AlignCenter,
                         QString::number(i + 1));
        painter.setPen(foreground);
        painter.drawText(row.adjusted(32, 0, -4, 0),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                         candidates[i]);
      }
      const QRectF footer(window.left() + 1, window.bottom() - 27,
                          window.width() - 2, 26);
      painter.fillRect(footer, RgbHexToQColor(
          candidate_like.footer_background_color));
      painter.setPen(QPen(RgbHexToQColor(
          candidate_like.footer_border_color), 1));
      painter.drawLine(footer.topLeft(), footer.topRight());
      painter.setPen(RgbHexToQColor(candidate_like.footer_text_color));
      QFont footer_font = text_font;
      footer_font.setPointSizeF(std::max(8.0, text_font.pointSizeF() - 1.0));
      painter.setFont(footer_font);
      painter.drawText(footer.adjusted(9, 0, -8, 0),
                       Qt::AlignVCenter | Qt::AlignRight,
                       kind_ == Kind::kCandidate
                           ? QStringLiteral("1 / 3")
                           : QStringLiteral("予測候補"));
      const QRectF track(window.right() - 8, window.top() + 4,
                         4, window.height() - 36);
      painter.fillRect(track, RgbHexToQColor(
          candidate_like.scrollbar_background_color));
      painter.fillRect(QRectF(track.left(), track.top(), 4, track.height() / 2),
                       RgbHexToQColor(candidate_like.scrollbar_indicator_color));
    }
    painter.restore();
    painter.setPen(palette().color(QPalette::Text));
    QFont label_font = font();
    label_font.setPointSizeF(std::max(8.0, label_font.pointSizeF() - 1));
    painter.setFont(label_font);
    painter.drawText(QRectF(12, 6, width() - 24, 19),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     StatusUiText("Preview (illustrative)", "プレビュー（表示例）"));
  }

 private:
  QWidget* settings_;
  Kind kind_;
};
}  // namespace

void ConfigDialog::UpdateConversionStatusPreview() {
  if (!conversion_status_preview_) return;
  config::Config config;
  ConvertRendererAppearanceToProto(&config);
  const auto& style = config.conversion_status_indicator_style();
  auto* preview = static_cast<ConversionStatusPreviewWidget*>(conversion_status_preview_);
  preview->Configure(style);
  if (QComboBox* preset = FindComboBox(this, "conversionStatusPresetComboBox")) {
    int matching = -1;  // Custom, including custom font and placement.
    for (int i = 0; i < 3; ++i) {
      if (SameConversionStatusStyle(style, ConversionStatusPreset(i))) {
        matching = i;
        break;
      }
    }
    const QSignalBlocker block(preset);
    SetComboCurrentData(preset, matching);
    if (QPushButton* apply =
            findChild<QPushButton*>("conversionStatusApplyPresetButton")) {
      apply->setEnabled(matching >= 0);
    }
  }
}

void ConfigDialog::InitializeRendererAppearanceControls() {
#if !defined(_WIN32) && !defined(__APPLE__)
  return;
#endif  // !defined(_WIN32) && !defined(__APPLE__)

  useDarkModeCandidateWindow->hide();
  candidateRubyFontLabel->hide();
  candidateRubyFontComboBox->hide();

  // A self-contained editor: it never depends on the ruby-visibility checkbox.
  auto* status_group = new QFrame(inputAppearanceScrollAreaWidgetContents);
  status_group->setObjectName(QStringLiteral("conversionStatusAppearanceGroupBox"));
  status_group->setFrameShape(QFrame::NoFrame);
  status_group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* status_root = new QVBoxLayout(status_group);
  status_root->setContentsMargins(0, 18, 0, 9);
  status_root->setSpacing(9);

  // Match the existing Input mode indicator section, including its header
  // baseline and horizontal separator instead of a one-off bold heading.
  auto* header_widget = new QWidget(status_group);
  auto* header = new QHBoxLayout(header_widget);
  header->setContentsMargins(9, 9, 9, 9);
  header->setSpacing(0);
  auto* status_heading = new QLabel(
      StatusUiText("Conversion status indicator", "変換状態インジケータ"),
      header_widget);
  status_heading->setObjectName(QStringLiteral("conversionStatusHeaderLabel"));
  header->addWidget(status_heading, 0, Qt::AlignVCenter);
  header->addSpacing(8);
  auto* header_line = new QFrame(header_widget);
  header_line->setObjectName(QStringLiteral("conversionStatusHeaderLine"));
  header_line->setFrameShape(QFrame::HLine);
  header_line->setFrameShadow(QFrame::Sunken);
  header->addWidget(header_line, 1, Qt::AlignVCenter);
  status_root->addWidget(header_widget);

  auto* enabled = new QCheckBox(
      StatusUiText("Show near composition", "未確定文字の近くに表示する"),
      status_group);
  enabled->setObjectName(QStringLiteral("conversionStatusEnabledCheckBox"));
  auto* enabled_row = new QWidget(status_group);
  auto* enabled_layout = new QHBoxLayout(enabled_row);
  enabled_layout->setContentsMargins(24, 0, 12, 0);
  enabled_layout->addWidget(enabled);
  status_root->addWidget(enabled_row);

  // The preview is deliberately above the controls and never hidden when the
  // production overlay is disabled: users can design the indicator offline.
  auto* preview_section = new QFrame(status_group);
  auto* preview_layout = new QVBoxLayout(preview_section);
  preview_layout->setContentsMargins(16, 0, 16, 0);
  preview_layout->setSpacing(6);
  auto* preview_title = new QLabel(
      StatusUiText("Live preview", "プレビュー"), preview_section);
  preview_title->setContentsMargins(8, 0, 0, 0);
  preview_layout->addWidget(preview_title);
  auto* preview_widget = new ConversionStatusPreviewWidget(preview_section);
  preview_widget->setObjectName(QStringLiteral("conversionStatusLivePreview"));
  conversion_status_preview_ = preview_widget;
  preview_layout->addWidget(preview_widget);
  status_root->addWidget(preview_section);

  QComboBox* preset_combo = nullptr;
  QPushButton* preset_button = nullptr;
  auto make_section = [&](const QString& title, bool is_basic = false) -> QGridLayout* {
    auto* section = new QFrame(status_group);
    auto* layout = new QVBoxLayout(section);
    // Match the small gaps above the preedit color subsections.
    // Basic follows the preview directly; subsequent subsections get 8 px
    // more breathing room without enlarging their internal controls.
    layout->setContentsMargins(0, is_basic ? 2 : 10, 0, 0);
    layout->setSpacing(5);
    auto* section_header = new QHBoxLayout;
    section_header->setContentsMargins(24, 0, 16, 0);
    section_header->setSpacing(9);
    auto* name = new QLabel(title, section);
    section_header->addWidget(name);
    if (is_basic) {
      preset_combo = new QComboBox(section);
      preset_combo->setObjectName(QStringLiteral("conversionStatusPresetComboBox"));
      preset_combo->addItem(StatusUiText("Custom", "カスタム"), -1);
      preset_combo->addItem(StatusUiText("Standard", "標準"), 0);
      preset_combo->addItem(StatusUiText("Minimal", "ミニマル"), 1);
      preset_combo->addItem(StatusUiText("Detailed", "詳細"), 2);
      preset_combo->setMinimumWidth(114);
      preset_combo->setToolTip(StatusUiText("Select a preset, then apply it.",
                                            "プリセットを選び、適用ボタンを押してください。"));
      preset_button = new QPushButton(
          StatusUiText("Apply", "適用"), section);
      preset_button->setObjectName(QStringLiteral("conversionStatusApplyPresetButton"));
      preset_button->setEnabled(false);
      preset_button->setToolTip(StatusUiText("Apply only the indicator style preset.",
                                              "変換状態インジケータの外観だけを変更します。"));
      section_header->addWidget(
          new QLabel(StatusUiText("Preset", "プリセット"), section));
      section_header->addWidget(preset_combo);
      section_header->addWidget(preset_button);
      QObject::connect(preset_combo,
                       QOverload<int>::of(&QComboBox::currentIndexChanged),
                       preset_button, [preset_combo, preset_button](int) {
                         preset_button->setEnabled(preset_combo->currentData().toInt() >= 0);
                       });
    }
    section_header->addStretch();
    layout->addLayout(section_header);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(24, 3, 16, 3);
    grid->setHorizontalSpacing(9);
    grid->setVerticalSpacing(7);
    layout->addLayout(grid);
    status_root->addWidget(section);
    return grid;
  };
  auto add_combo = [&](QGridLayout* grid, int row, int column,
                       const QString& label, const char* id) {
    auto* widget = new QComboBox(status_group);
    widget->setObjectName(QString::fromLatin1(id));
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    grid->addWidget(new QLabel(label, status_group), row, column * 2);
    grid->addWidget(widget, row, column * 2 + 1);
    QObject::connect(widget, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, [this](int) {
                       UpdateConversionStatusPreview();
                       EnableApplyButton();
                     });
    return widget;
  };
  auto add_spin = [&](QGridLayout* grid, int row, int column,
                      const QString& label, const char* id,
                      int min, int max, int def, const QString& suffix) {
    auto* spin = new QSpinBox(status_group);
    spin->setObjectName(QString::fromLatin1(id));
    spin->setRange(min, max);
    spin->setValue(def);
    spin->setSuffix(suffix);
    spin->setMinimumWidth(88);
    spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    grid->addWidget(new QLabel(label, status_group), row, column * 2);
    grid->addWidget(spin, row, column * 2 + 1);
    QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                     this, [this](int) {
                       UpdateConversionStatusPreview();
                       EnableApplyButton();
                     });
    return spin;
  };

  auto* basic = make_section(StatusUiText("Basic", "基本"), true);
  auto* shape = add_combo(basic, 0, 0, StatusUiText("Shape", "形状"),
                          "conversionStatusShapeComboBox");
  shape->addItem(StatusUiText("Capsule", "カプセル"),
                 static_cast<int>(config::Config::CONVERSION_STATUS_PILL));
  shape->addItem(StatusUiText("Circle", "円形"),
                 static_cast<int>(config::Config::CONVERSION_STATUS_CIRCLE));
  shape->addItem(StatusUiText("Rounded rectangle", "角丸四角形"),
                 static_cast<int>(config::Config::CONVERSION_STATUS_ROUNDED_RECT));
  auto* label = add_combo(basic, 0, 1, StatusUiText("Label", "ラベル"),
                          "conversionStatusLabelComboBox");
  label->addItem(StatusUiText("Name", "名称を表示"),
                 static_cast<int>(config::Config::CONVERSION_STATUS_FULL_LABEL));
  label->addItem(StatusUiText("Symbol", "記号のみ"),
                 static_cast<int>(config::Config::CONVERSION_STATUS_SYMBOL));
  label->addItem(StatusUiText("Color only", "色のみ"),
                 static_cast<int>(config::Config::CONVERSION_STATUS_COLOR_ONLY));
  auto* placement = add_combo(basic, 1, 0,
                              StatusUiText("Position", "表示位置"),
                              "conversionStatusPlacementComboBox");
  placement->addItem(StatusUiText("Automatic", "自動（重なりを回避）"),
                     static_cast<int>(config::Config::CONVERSION_STATUS_AUTO));
  placement->addItem(StatusUiText("Beside ruby", "ルビの横"),
                     static_cast<int>(config::Config::CONVERSION_STATUS_RUBY_SIDE));
  placement->addItem(StatusUiText("Above ruby", "ルビの上"),
                     static_cast<int>(config::Config::CONVERSION_STATUS_RUBY_ABOVE));
  placement->addItem(StatusUiText("Above text", "未確定文字の上"),
                     static_cast<int>(config::Config::CONVERSION_STATUS_PREEDIT_ABOVE));
  placement->addItem(StatusUiText("Below text", "未確定文字の下"),
                     static_cast<int>(config::Config::CONVERSION_STATUS_PREEDIT_BELOW));
  placement->setToolTip(StatusUiText(
      "Fixed positions stay on the selected side. If no non-overlapping "
      "position exists, the indicator is hidden.",
      "位置を指定した場合、別の方向には移動しません。空きがなければ非表示になります。"));
  auto* status_font = add_combo(basic, 1, 1,
                                StatusUiText("Font", "フォント"),
                                "conversionStatusFontComboBox");
  status_font->clear();
  status_font->addItem(StatusUiText("System default", "既定（OS標準）"), QString());
  QFontDatabase font_database;
  for (const QString& family : font_database.families()) {
    if (!family.startsWith(QLatin1Char('@'))) {
      status_font->addItem(family, family);
    }
  }
  status_font->setToolTip(StatusUiText("Default uses the system font.",
                                      "既定ではOSの標準フォントを使用します。"));
  basic->setColumnStretch(1, 1);
  basic->setColumnStretch(3, 1);

  auto* colors = make_section(StatusUiText("Status colors", "状態別の色"));
  const struct { const char* english; const char* japanese;
                 const char* id; uint32_t rgb; } color_items[] = {
      {"Mozc", "Mozc", "conversionStatusMozcColorButton", 0x586b85},
      {"Zenz processing", "Zenz処理中", "conversionStatusPendingColorButton", 0x847438},
      {"Zenz applied", "Zenz適用済み", "conversionStatusZenzColorButton", 0x338d85},
      {"Reading", "入力中", "conversionStatusReadingColorButton", 0x747d8c},
      {"Text", "文字", "conversionStatusTextColorButton", 0xffffff},
      {"Border", "枠線", "conversionStatusBorderColorButton", 0x354357},
  };
  for (int i = 0; i < 6; ++i) {
    const int row = i / 2;
    const int col = (i % 2) * 2;
    auto* button = new QPushButton(status_group);
    button->setObjectName(QString::fromLatin1(color_items[i].id));
    button->setFixedWidth(96);
    button->setMinimumHeight(26);
    SetColorButton(button, color_items[i].rgb);
    colors->addWidget(new QLabel(StatusUiText(color_items[i].english,
                                              color_items[i].japanese),
                                 status_group), row, col);
    colors->addWidget(button, row, col + 1, Qt::AlignLeft);
    QObject::connect(button, SIGNAL(clicked()), this,
                     SLOT(SelectRendererAppearanceColor()));
  }
  colors->setColumnStretch(1, 1);
  colors->setColumnStretch(3, 1);

  auto* geometry = make_section(StatusUiText("Size and appearance", "サイズと外観"));
  add_spin(geometry, 0, 0, StatusUiText("Minimum width", "最小幅"),
           "conversionStatusWidthSpinBox", 12, 240, 58, QStringLiteral(" px"));
  add_spin(geometry, 0, 1, StatusUiText("Height", "高さ"),
           "conversionStatusHeightSpinBox", 12, 100, 24, QStringLiteral(" px"));
  add_spin(geometry, 1, 0, StatusUiText("Font size", "文字サイズ"),
           "conversionStatusFontSizeSpinBox", 8, 24, 12, QStringLiteral(" pt"));
  add_spin(geometry, 1, 1, StatusUiText("Opacity", "透明度"),
           "conversionStatusOpacitySpinBox", 20, 100, 93, QStringLiteral(" %"));
  add_spin(geometry, 2, 0, StatusUiText("Corner radius", "角丸"),
           "conversionStatusCornerSpinBox", 0, 50, 7, QStringLiteral(" px"));
  add_spin(geometry, 2, 1, StatusUiText("Border width", "枠線の太さ"),
           "conversionStatusBorderWidthSpinBox", 0, 5, 0, QStringLiteral(" px"));
  geometry->setColumnStretch(1, 1);
  geometry->setColumnStretch(3, 1);

  auto* detail = make_section(StatusUiText("Shadow and placement", "影と位置の微調整"));
  add_spin(detail, 0, 0, StatusUiText("Shadow blur", "影のぼかし"),
           "conversionStatusShadowSizeSpinBox", 0, 24, 3, QStringLiteral(" px"));
  add_spin(detail, 0, 1, StatusUiText("Shadow opacity", "影の濃さ"),
           "conversionStatusShadowOpacitySpinBox", 0, 100, 12, QStringLiteral(" %"));
  add_spin(detail, 1, 0, StatusUiText("Shadow angle", "影の方向"),
           "conversionStatusShadowAngleSpinBox", 0, 359, 45, QStringLiteral(" °"));
  add_spin(detail, 1, 1, StatusUiText("Shadow distance", "影の距離"),
           "conversionStatusShadowDistanceSpinBox", 0, 24, 2, QStringLiteral(" px"));
  add_spin(detail, 2, 0, StatusUiText("Horizontal offset", "水平方向の補正"),
           "conversionStatusOffsetXSpinBox", -100, 100, 0, QStringLiteral(" px"));
  add_spin(detail, 2, 1, StatusUiText("Vertical offset", "垂直方向の補正"),
           "conversionStatusOffsetYSpinBox", -100, 100, 0, QStringLiteral(" px"));
  add_spin(detail, 3, 0, StatusUiText("Spacing", "文字との間隔"),
           "conversionStatusGapSpinBox", 0, 60, 5, QStringLiteral(" px"));
  detail->setColumnStretch(1, 1);
  detail->setColumnStretch(3, 1);

  inputAppearanceContentLayout->insertWidget(1, status_group);

  QObject::connect(enabled, &QCheckBox::toggled, this, [this](bool) {
    UpdateConversionStatusPreview();
    EnableApplyButton();
  });
  QObject::connect(preset_button, &QPushButton::clicked, this,
                   [this, preset_combo]() {
    const int preset = preset_combo->currentData().toInt();
    if (preset < 0 || preset > 2) return;
    const auto style = ConversionStatusPreset(preset);
    auto set_spin = [this](const char* id, int value) {
      if (QSpinBox* spin = FindSpinBox(this, id)) spin->setValue(value);
    };
    auto set_color = [this](const char* id, uint32_t rgb) {
      SetColorButton(findChild<QPushButton*>(id), rgb);
    };
    SetComboCurrentData(FindComboBox(this, "conversionStatusShapeComboBox"),
                        static_cast<int>(style.shape()));
    SetComboCurrentData(FindComboBox(this, "conversionStatusLabelComboBox"),
                        static_cast<int>(style.label()));
    SetComboCurrentData(FindComboBox(this, "conversionStatusPlacementComboBox"),
                        static_cast<int>(style.placement()));
    SetComboBoxCurrentFontNameOrAdd(
        FindComboBox(this, "conversionStatusFontComboBox"),
        QString::fromStdString(style.font_family()));
    set_spin("conversionStatusWidthSpinBox", style.width());
    set_spin("conversionStatusHeightSpinBox", style.height());
    set_spin("conversionStatusFontSizeSpinBox", style.font_size());
    set_spin("conversionStatusCornerSpinBox", style.corner_radius());
    set_spin("conversionStatusBorderWidthSpinBox", style.border_width());
    set_spin("conversionStatusOpacitySpinBox", style.opacity_percent());
    set_spin("conversionStatusShadowSizeSpinBox", style.shadow_size());
    set_spin("conversionStatusShadowOpacitySpinBox", style.shadow_opacity_percent());
    set_spin("conversionStatusShadowAngleSpinBox", style.shadow_angle_degrees());
    set_spin("conversionStatusShadowDistanceSpinBox", style.shadow_distance());
    set_spin("conversionStatusOffsetXSpinBox", style.offset_x());
    set_spin("conversionStatusOffsetYSpinBox", style.offset_y());
    set_spin("conversionStatusGapSpinBox", style.gap());
    set_color("conversionStatusMozcColorButton", style.mozc_color());
    set_color("conversionStatusPendingColorButton", style.zenz_pending_color());
    set_color("conversionStatusZenzColorButton", style.zenz_color());
    set_color("conversionStatusReadingColorButton", style.reading_color());
    set_color("conversionStatusTextColorButton", style.text_color());
    set_color("conversionStatusBorderColorButton", style.border_color());
    UpdateConversionStatusPreview();
    EnableApplyButton();
  });
  UpdateConversionStatusPreview();


#ifdef _WIN32
  // Keep the input-mode indicator controls together with renderer appearance.
  // The checkbox and theme combo are defined in the .ui file for config
  // serialization compatibility, then reparented here.
  QWidget* mode_indicator_group =
      new QWidget(inputAppearanceScrollAreaWidgetContents);
  mode_indicator_group->setObjectName(
      QStringLiteral("modeIndicatorAppearanceGroupBox"));
  mode_indicator_group->setSizePolicy(QSizePolicy::Preferred,
                                      QSizePolicy::Fixed);

  QVBoxLayout* mode_indicator_root_layout =
      new QVBoxLayout(mode_indicator_group);
  mode_indicator_root_layout->setContentsMargins(0, 0, 0, 0);
  mode_indicator_root_layout->setSpacing(0);

  QWidget* mode_indicator_section = new QWidget(mode_indicator_group);
  QHBoxLayout* mode_indicator_section_layout =
      new QHBoxLayout(mode_indicator_section);
  mode_indicator_section_layout->setContentsMargins(9, 9, 9, 9);
  QLabel* mode_indicator_section_label =
      new QLabel(tr("Input mode indicator"), mode_indicator_section);
  QPushButton* mode_indicator_reset_button =
      new QPushButton(tr("Reset"), mode_indicator_section);
  mode_indicator_reset_button->setObjectName(
      QStringLiteral("modeIndicatorAppearanceResetButton"));
  mode_indicator_reset_button->setFixedWidth(64);
  mode_indicator_reset_button->setMinimumHeight(24);
  QFrame* mode_indicator_section_line = new QFrame(mode_indicator_section);
  mode_indicator_section_line->setFrameShape(QFrame::HLine);
  mode_indicator_section_line->setFrameShadow(QFrame::Sunken);
  mode_indicator_section_label->setSizePolicy(QSizePolicy::Maximum,
                                               QSizePolicy::Preferred);
  mode_indicator_reset_button->setSizePolicy(QSizePolicy::Fixed,
                                              QSizePolicy::Fixed);
  mode_indicator_section_line->setSizePolicy(QSizePolicy::Expanding,
                                              QSizePolicy::Fixed);
  mode_indicator_section_layout->setSpacing(0);
  mode_indicator_section_layout->addWidget(mode_indicator_section_label, 0,
                                           Qt::AlignVCenter);
  mode_indicator_section_layout->addSpacing(8);
  mode_indicator_section_layout->addWidget(mode_indicator_reset_button, 0,
                                           Qt::AlignVCenter);
  mode_indicator_section_layout->addSpacing(8);
  mode_indicator_section_layout->addWidget(mode_indicator_section_line, 1,
                                           Qt::AlignVCenter);
  mode_indicator_root_layout->addWidget(mode_indicator_section);

  QWidget* mode_indicator_body = new QWidget(mode_indicator_group);
  mode_indicator_body->setSizePolicy(QSizePolicy::Preferred,
                                     QSizePolicy::Fixed);
  QGridLayout* mode_indicator_layout = new QGridLayout(mode_indicator_body);
  mode_indicator_layout->setContentsMargins(24, 9, 24, 9);
  mode_indicator_layout->setHorizontalSpacing(8);
  mode_indicator_layout->setVerticalSpacing(6);

  useModeIndicator->setParent(mode_indicator_body);
  useModeIndicator->show();
  mode_indicator_layout->addWidget(useModeIndicator, 0, 0, 1, 2);

  QLabel* mode_indicator_theme_label =
      new QLabel(tr("Theme"), mode_indicator_body);
  mode_indicator_theme_label->setMinimumHeight(24);
  modeIndicatorThemeComboBox->setParent(mode_indicator_body);
  modeIndicatorThemeComboBox->show();
  modeIndicatorThemeComboBox->setMinimumHeight(24);
  modeIndicatorThemeComboBox->setSizePolicy(QSizePolicy::Fixed,
                                             QSizePolicy::Fixed);
  mode_indicator_layout->addWidget(mode_indicator_theme_label, 1, 0);
  mode_indicator_layout->addWidget(modeIndicatorThemeComboBox, 1, 1, 1, 1,
                                   Qt::AlignRight);
  mode_indicator_layout->setColumnStretch(1, 1);

  // Keep the preview outside the custom-only controls so that Standard,
  // Dark, Light and disabled indicators can all be inspected without Apply.
  auto* mode_preview_section = new QWidget(mode_indicator_body);
  auto* mode_preview_layout = new QVBoxLayout(mode_preview_section);
  mode_preview_layout->setContentsMargins(8, 4, 8, 6);
  mode_preview_layout->setSpacing(5);
  auto* mode_preview_title = new QLabel(
      StatusUiText("Live preview", "プレビュー"), mode_preview_section);
  mode_preview_layout->addWidget(mode_preview_title);
  auto* mode_preview = new ModeIndicatorPreviewWidget(this, mode_preview_section);
  mode_preview->setObjectName(QStringLiteral("modeIndicatorLivePreview"));
  mode_preview_layout->addWidget(mode_preview);
  mode_indicator_layout->addWidget(mode_preview_section, 2, 0, 1, 2);

  // A combo box popup can defer a scheduled update() until the next input
  // event. Paint synchronously on selection so theme changes are immediate.
  QObject::connect(modeIndicatorThemeComboBox,
                   QOverload<int>::of(&QComboBox::currentIndexChanged),
                   mode_preview,
                   [mode_preview](int) { mode_preview->repaint(); });

  QGroupBox* mode_indicator_custom_box =
      new QGroupBox(tr("Custom design"), mode_indicator_body);
  mode_indicator_custom_box->setObjectName(
      QStringLiteral("modeIndicatorCustomGroupBox"));
  mode_indicator_custom_box->setSizePolicy(QSizePolicy::Preferred,
                                           QSizePolicy::Fixed);
  QVBoxLayout* mode_indicator_custom_layout =
      new QVBoxLayout(mode_indicator_custom_box);
  mode_indicator_custom_layout->setContentsMargins(8, 8, 8, 8);
  mode_indicator_custom_layout->setSpacing(8);

  // Load only the four colors for each displayed input-mode family. Geometry,
  // shadow strength and the selected CUSTOM theme are intentionally preserved.
  auto* palette_actions = new QHBoxLayout();
  palette_actions->setContentsMargins(0, 0, 0, 0);
  palette_actions->setSpacing(8);
  auto* load_light = new QPushButton(
      StatusUiText("Load light colors", "ライト配色を読み込む"),
      mode_indicator_custom_box);
  auto* load_dark = new QPushButton(
      StatusUiText("Load dark colors", "ダーク配色を読み込む"),
      mode_indicator_custom_box);
  load_light->setObjectName(QStringLiteral("modeIndicatorLoadLightColorsButton"));
  load_dark->setObjectName(QStringLiteral("modeIndicatorLoadDarkColorsButton"));
  const QString palette_tooltip = StatusUiText(
      "Replace the colors for all three modes, keeping size and shadow settings.",
      "英数・ひらがな・カタカナの配色のみ変更します。サイズや影の設定は維持します。");
  load_light->setToolTip(palette_tooltip);
  load_dark->setToolTip(palette_tooltip);
  palette_actions->addWidget(load_light);
  palette_actions->addWidget(load_dark);
  palette_actions->addStretch(1);
  mode_indicator_custom_layout->addLayout(palette_actions);

  auto load_mode_palette = [this, mode_preview](
      const ModeIndicatorPalette (&colors)[3]) {
    const char* const controls[3][4] = {
        {"modeIndicatorAsciiBackgroundColorButton",
         "modeIndicatorAsciiBorderColorButton",
         "modeIndicatorAsciiShadowColorButton",
         "modeIndicatorAsciiTextColorButton"},
        {"modeIndicatorHiraganaBackgroundColorButton",
         "modeIndicatorHiraganaBorderColorButton",
         "modeIndicatorHiraganaShadowColorButton",
         "modeIndicatorHiraganaTextColorButton"},
        {"modeIndicatorKatakanaBackgroundColorButton",
         "modeIndicatorKatakanaBorderColorButton",
         "modeIndicatorKatakanaShadowColorButton",
         "modeIndicatorKatakanaTextColorButton"},
    };
    for (int i = 0; i < 3; ++i) {
      const uint32_t values[4] = {
          colors[i].background, colors[i].border,
          colors[i].shadow, colors[i].text};
      for (int j = 0; j < 4; ++j) {
        SetColorButton(FindButton(this, QString::fromLatin1(controls[i][j])),
                       values[j]);
      }
    }
    mode_preview->repaint();
    EnableApplyButton();
  };
  QObject::connect(load_light, &QPushButton::clicked, this,
                   [load_mode_palette]() {
                     load_mode_palette(kModeIndicatorLightPalettes);
                   });
  QObject::connect(load_dark, &QPushButton::clicked, this,
                   [load_mode_palette]() {
                     load_mode_palette(kModeIndicatorDarkPalettes);
                   });

  QGridLayout* mode_indicator_color_layout = new QGridLayout();
  mode_indicator_color_layout->setHorizontalSpacing(8);
  mode_indicator_color_layout->setVerticalSpacing(6);
  mode_indicator_color_layout->addWidget(
      new QLabel(tr("Target"), mode_indicator_custom_box), 0, 0);
  mode_indicator_color_layout->addWidget(
      new QLabel(tr("Background"), mode_indicator_custom_box), 0, 1);
  mode_indicator_color_layout->addWidget(
      new QLabel(tr("Border"), mode_indicator_custom_box), 0, 2);
  mode_indicator_color_layout->addWidget(
      new QLabel(tr("Text"), mode_indicator_custom_box), 0, 3);
  mode_indicator_color_layout->addWidget(
      new QLabel(tr("Shadow"), mode_indicator_custom_box), 0, 4);

  auto add_mode_indicator_color_button =
      [&](int row, int column, const char* object_name,
          uint32_t default_color) {
        QPushButton* button = new QPushButton(mode_indicator_custom_box);
        button->setObjectName(QString::fromLatin1(object_name));
        button->setMinimumSize(78, 26);
        SetColorButton(button, default_color);
        mode_indicator_color_layout->addWidget(button, row, column);
        QObject::connect(button, SIGNAL(clicked()), this,
                         SLOT(SelectRendererAppearanceColor()));
      };

  mode_indicator_color_layout->addWidget(
      new QLabel(tr("Alphanumeric"), mode_indicator_custom_box), 1, 0);
  add_mode_indicator_color_button(
      1, 1, "modeIndicatorAsciiBackgroundColorButton",
      GetDefaultModeIndicatorCustomStyle().ascii_background_color());
  add_mode_indicator_color_button(
      1, 2, "modeIndicatorAsciiBorderColorButton",
      GetDefaultModeIndicatorCustomStyle().ascii_border_color());
  add_mode_indicator_color_button(
      1, 3, "modeIndicatorAsciiTextColorButton",
      GetDefaultModeIndicatorCustomStyle().ascii_text_color());
  add_mode_indicator_color_button(
      1, 4, "modeIndicatorAsciiShadowColorButton",
      GetDefaultModeIndicatorCustomStyle().ascii_shadow_color());

  mode_indicator_color_layout->addWidget(
      new QLabel(tr("Hiragana"), mode_indicator_custom_box), 2, 0);
  add_mode_indicator_color_button(
      2, 1, "modeIndicatorHiraganaBackgroundColorButton",
      GetDefaultModeIndicatorCustomStyle().hiragana_background_color());
  add_mode_indicator_color_button(
      2, 2, "modeIndicatorHiraganaBorderColorButton",
      GetDefaultModeIndicatorCustomStyle().hiragana_border_color());
  add_mode_indicator_color_button(
      2, 3, "modeIndicatorHiraganaTextColorButton",
      GetDefaultModeIndicatorCustomStyle().hiragana_text_color());
  add_mode_indicator_color_button(
      2, 4, "modeIndicatorHiraganaShadowColorButton",
      GetDefaultModeIndicatorCustomStyle().hiragana_shadow_color());

  mode_indicator_color_layout->addWidget(
      new QLabel(tr("Katakana"), mode_indicator_custom_box), 3, 0);
  add_mode_indicator_color_button(
      3, 1, "modeIndicatorKatakanaBackgroundColorButton",
      GetDefaultModeIndicatorCustomStyle().katakana_background_color());
  add_mode_indicator_color_button(
      3, 2, "modeIndicatorKatakanaBorderColorButton",
      GetDefaultModeIndicatorCustomStyle().katakana_border_color());
  add_mode_indicator_color_button(
      3, 3, "modeIndicatorKatakanaTextColorButton",
      GetDefaultModeIndicatorCustomStyle().katakana_text_color());
  add_mode_indicator_color_button(
      3, 4, "modeIndicatorKatakanaShadowColorButton",
      GetDefaultModeIndicatorCustomStyle().katakana_shadow_color());
  mode_indicator_custom_layout->addLayout(mode_indicator_color_layout);

  QGridLayout* mode_indicator_shape_layout = new QGridLayout();
  mode_indicator_shape_layout->setHorizontalSpacing(8);
  mode_indicator_shape_layout->setVerticalSpacing(6);

  auto add_mode_indicator_spin =
      [&](int row, int pair, const QString& label_text,
          const char* object_name, int minimum, int maximum, int value,
          const QString& suffix) {
        const int label_column = pair * 2;
        QLabel* label = new QLabel(label_text, mode_indicator_custom_box);
        QSpinBox* spin = new QSpinBox(mode_indicator_custom_box);
        spin->setObjectName(QString::fromLatin1(object_name));
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        spin->setSuffix(suffix);
        spin->setMinimumWidth(84);
        spin->setMinimumHeight(24);
        mode_indicator_shape_layout->addWidget(label, row, label_column);
        mode_indicator_shape_layout->addWidget(spin, row, label_column + 1);
      };

  add_mode_indicator_spin(
      0, 0, tr("Width"), "modeIndicatorWidthSpinBox", 20, 96,
      static_cast<int>(GetDefaultModeIndicatorCustomStyle().width()),
      QStringLiteral(" px"));
  add_mode_indicator_spin(
      0, 1, tr("Height"), "modeIndicatorHeightSpinBox", 20, 96,
      static_cast<int>(GetDefaultModeIndicatorCustomStyle().height()),
      QStringLiteral(" px"));
  add_mode_indicator_spin(
      1, 0, tr("Corner radius"), "modeIndicatorCornerRadiusSpinBox", 0, 32,
      static_cast<int>(GetDefaultModeIndicatorCustomStyle().corner_radius()),
      QStringLiteral(" px"));
  add_mode_indicator_spin(
      1, 1, tr("Text size"), "modeIndicatorLabelSizeSpinBox", 8, 32,
      static_cast<int>(GetDefaultModeIndicatorCustomStyle().label_size()),
      QStringLiteral(" pt"));
  add_mode_indicator_spin(
      2, 0, tr("Border thickness"), "modeIndicatorBorderThicknessSpinBox", 0,
      6,
      static_cast<int>(
          GetDefaultModeIndicatorCustomStyle().border_thickness()),
      QStringLiteral(" px"));
  add_mode_indicator_spin(
      2, 1, tr("Shadow blur"), "modeIndicatorShadowBlurSpinBox", 0,
      kMaxModeIndicatorShadowBlur,
      static_cast<int>(GetDefaultModeIndicatorCustomStyle().shadow_blur()),
      QStringLiteral(" px"));
  add_mode_indicator_spin(
      3, 0, tr("Shadow opacity"), "modeIndicatorShadowOpacitySpinBox", 0, 100,
      static_cast<int>(
          GetDefaultModeIndicatorCustomStyle().shadow_opacity_percent()),
      QStringLiteral(" %"));
  add_mode_indicator_spin(
      3, 1, tr("Shadow X offset"), "modeIndicatorShadowOffsetXSpinBox", -24,
      24, GetDefaultModeIndicatorCustomStyle().shadow_offset_x(),
      QStringLiteral(" px"));
  add_mode_indicator_spin(
      4, 0, tr("Shadow Y offset"), "modeIndicatorShadowOffsetYSpinBox", -24,
      24, GetDefaultModeIndicatorCustomStyle().shadow_offset_y(),
      QStringLiteral(" px"));
  mode_indicator_shape_layout->setColumnStretch(1, 1);
  mode_indicator_shape_layout->setColumnStretch(3, 1);
  mode_indicator_custom_layout->addLayout(mode_indicator_shape_layout);

  // Spin changes update the preview immediately; no configuration is saved.
  for (QSpinBox* spin : mode_indicator_custom_box->findChildren<QSpinBox*>()) {
    QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                     mode_preview, [mode_preview](int) { mode_preview->repaint(); });
  }

  mode_indicator_layout->addWidget(mode_indicator_custom_box, 3, 0, 1, 2);
  mode_indicator_root_layout->addWidget(mode_indicator_body);

  QObject::connect(
      mode_indicator_reset_button, &QPushButton::clicked, this, [this]() {
        const config::Config::WindowsModeIndicatorCustomStyle& defaults =
            GetDefaultModeIndicatorCustomStyle();

        auto reset_color = [this](const char* object_name, uint32_t rgb) {
          SetColorButton(
              FindButton(this, QString::fromLatin1(object_name)), rgb);
        };
        reset_color("modeIndicatorAsciiBackgroundColorButton",
                    defaults.ascii_background_color());
        reset_color("modeIndicatorAsciiBorderColorButton",
                    defaults.ascii_border_color());
        reset_color("modeIndicatorAsciiTextColorButton",
                    defaults.ascii_text_color());
        reset_color("modeIndicatorAsciiShadowColorButton",
                    defaults.ascii_shadow_color());
        reset_color("modeIndicatorHiraganaBackgroundColorButton",
                    defaults.hiragana_background_color());
        reset_color("modeIndicatorHiraganaBorderColorButton",
                    defaults.hiragana_border_color());
        reset_color("modeIndicatorHiraganaTextColorButton",
                    defaults.hiragana_text_color());
        reset_color("modeIndicatorHiraganaShadowColorButton",
                    defaults.hiragana_shadow_color());
        reset_color("modeIndicatorKatakanaBackgroundColorButton",
                    defaults.katakana_background_color());
        reset_color("modeIndicatorKatakanaBorderColorButton",
                    defaults.katakana_border_color());
        reset_color("modeIndicatorKatakanaTextColorButton",
                    defaults.katakana_text_color());
        reset_color("modeIndicatorKatakanaShadowColorButton",
                    defaults.katakana_shadow_color());

        auto reset_spin = [this](const char* object_name, int value) {
          if (QSpinBox* spin = FindSpinBox(this, object_name)) {
            spin->setValue(value);
          }
        };
        reset_spin("modeIndicatorWidthSpinBox",
                   static_cast<int>(defaults.width()));
        reset_spin("modeIndicatorHeightSpinBox",
                   static_cast<int>(defaults.height()));
        reset_spin("modeIndicatorCornerRadiusSpinBox",
                   static_cast<int>(defaults.corner_radius()));
        reset_spin("modeIndicatorLabelSizeSpinBox",
                   static_cast<int>(defaults.label_size()));
        reset_spin("modeIndicatorBorderThicknessSpinBox",
                   static_cast<int>(defaults.border_thickness()));
        reset_spin("modeIndicatorShadowBlurSpinBox",
                   static_cast<int>(defaults.shadow_blur()));
        reset_spin("modeIndicatorShadowOpacitySpinBox",
                   static_cast<int>(defaults.shadow_opacity_percent()));
        reset_spin("modeIndicatorShadowOffsetXSpinBox",
                   defaults.shadow_offset_x());
        reset_spin("modeIndicatorShadowOffsetYSpinBox",
                   defaults.shadow_offset_y());

        SetComboCurrentData(
            modeIndicatorThemeComboBox,
            static_cast<int>(
                config::Config::WINDOWS_MODE_INDICATOR_THEME_SYSTEM));
        UpdateRendererAppearanceControls();
        EnableApplyButton();
      });

  QObject::connect(modeIndicatorThemeComboBox,
                   SIGNAL(currentIndexChanged(int)), this,
                   SLOT(UpdateRendererAppearanceControls()));
  QObject::connect(useModeIndicator, SIGNAL(toggled(bool)), this,
                   SLOT(UpdateRendererAppearanceControls()));
#endif  // _WIN32

  QWidget* group = new QWidget(inputAppearanceScrollAreaWidgetContents);
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
  section_label->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
  reset_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  section_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  section_layout->setSpacing(0);
  section_layout->addWidget(section_label, 0, Qt::AlignVCenter);
  section_layout->addSpacing(8);
  section_layout->addWidget(reset_button, 0, Qt::AlignVCenter);
  section_layout->addSpacing(8);
  section_layout->addWidget(section_line, 1, Qt::AlignVCenter);
  QObject::connect(reset_button, SIGNAL(clicked()), this,
                   SLOT(ResetRendererAppearanceControls()));
  root_layout->addWidget(section);

  QWidget* body = new QWidget(group);
  body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  QVBoxLayout* body_layout = new QVBoxLayout(body);
  body_layout->setContentsMargins(24, 9, 24, 9);
  body_layout->setSpacing(6);
  root_layout->addWidget(body);

  // The pre-existing font picker is reused in the candidate tab; the other
  // two editors get independent pickers. Keep the original widget identity.
  candidateRubyFontComboBox->setParent(group);
  candidateRubyFontComboBox->setMinimumHeight(24);

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
        tr("Auto (follow system theme)"),
        static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_AUTO));
    combo->addItem(
        tr("Light"),
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
    size_spin->setSingleStep(1);
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
    // Keep two color entries per row without imposing the full label text
    // width as the minimum width of the entire nested editor. In compact
    // layouts a long label wraps rather than clipping its color swatch.
    text->setWordWrap(true);
    text->setMinimumWidth(0);
    text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QPushButton* button = new QPushButton(group);
    button->setObjectName(prefix + suffix);
    // A compact, predictable color chip leaves room for both label columns.
    // Seven-character #RRGGBB labels still remain visible.
    button->setFixedSize(76, 26);
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
    // Two color entries per row as in the original appearance editor.
    // Flexible, wrapping label columns protect the compact editor width.
    palette_grid->setHorizontalSpacing(6);
    for (size_t i = 0; i < std::size(kCandidatePaletteButtonNames); ++i) {
      add_palette_button(palette_grid, static_cast<int>(i / 2),
                         static_cast<int>(i % 2), prefix,
                         kCandidatePaletteButtonNames[i], labels[i]);
    }
    palette_grid->setColumnStretch(0, 1);
    palette_grid->setColumnStretch(2, 1);
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
    palette_grid->setHorizontalSpacing(5);
    // Ruby has only three colors; preserve the compact original one-row grid.
    for (size_t i = 0; i < std::size(kRubyPaletteButtonNames); ++i) {
      add_palette_button(palette_grid, 0, static_cast<int>(i), prefix,
                         kRubyPaletteButtonNames[i], labels[i]);
    }
    for (int column = 0; column < 6; column += 2) {
      palette_grid->setColumnStretch(column, 1);
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

  // Reorganize the editor by window rather than by property type.  Keep the
  // original objectNames so Convert*Proto and reset handlers stay unchanged.
  auto* window_tabs = new QTabWidget(body);
  window_tabs->setObjectName(QStringLiteral("rendererWindowAppearanceTabs"));
  window_tabs->setDocumentMode(true);
  const char* prefixes[] = {"candidateWindow", "suggestWindow", "rubyWindow"};
  const QString titles[] = {
      StatusUiText("Candidate window", "候補ウィンドウ"),
      StatusUiText("Suggestion window", "サジェストウィンドウ"),
      StatusUiText("Ruby window", "ルビウィンドウ")};
  RendererWindowAppearancePreview* window_previews[3] = {};
  const auto move_from_grid = [](QGridLayout* from, int row, int column,
                                 QWidget* new_parent, QGridLayout* to,
                                 int new_row, int new_column) {
    QLayoutItem* entry = from != nullptr ? from->itemAtPosition(row, column)
                                         : nullptr;
    QWidget* widget = entry != nullptr ? entry->widget() : nullptr;
    CHECK(widget != nullptr) << "Missing renderer editor control";
    from->removeWidget(widget);
    widget->setParent(new_parent);
    to->addWidget(widget, new_row, new_column);
    widget->show();
  };

  for (int index = 0; index < 3; ++index) {
    const int source_row = index + 1;
    const QString prefix = QString::fromLatin1(prefixes[index]);
    auto* page = new QWidget(window_tabs);
    page->setObjectName(prefix + QStringLiteral("AppearanceTab"));
    auto* page_layout = new QVBoxLayout(page);
    // Match the tab page width: do not inset the entire preview and editor
    // another eight pixels inside the already-margined outer appearance tab.
    page_layout->setContentsMargins(0, 10, 0, 8);
    page_layout->setSpacing(9);

    auto* preview = new RendererWindowAppearancePreview(
        this, static_cast<RendererWindowAppearancePreview::Kind>(index), page);
    preview->setObjectName(prefix + QStringLiteral("AppearancePreview"));
    window_previews[index] = preview;
    page_layout->addWidget(preview);

    // Only the controls scroll; the preview stays visible at the top of its
    // tab while editing the lower color and shadow settings.
    auto* editor_scroll = new QScrollArea(page);
    editor_scroll->setObjectName(prefix + QStringLiteral("AppearanceScroll"));
    editor_scroll->setWidgetResizable(true);
    // Keep the editor vertical-only; color labels are permitted to wrap,
    // avoiding a horizontal scrollbar at ordinary dialog widths.
    editor_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor_scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    editor_scroll->setFrameShape(QFrame::NoFrame);
    editor_scroll->setFixedHeight(425);
    auto* editor = new QWidget(editor_scroll);
    auto* editor_layout = new QVBoxLayout(editor);
    // Restore visual breathing room inside the scrolling editor while
    // preserving the full-width preview and tab page from v2.4.
    editor_layout->setContentsMargins(10, 7, 10, 12);
    editor_layout->setSpacing(12);

    auto* appearance_box = new QGroupBox(
        StatusUiText("Appearance", "外観"), editor);
    auto* appearance = new QGridLayout(appearance_box);
    appearance->setHorizontalSpacing(10);
    appearance->setVerticalSpacing(7);
    auto add_field = [&](int row, const QString& label, QGridLayout* source,
                         int source_column) {
      appearance->addWidget(new QLabel(label, appearance_box), row, 0);
      move_from_grid(source, source_row, source_column, appearance_box,
                     appearance, row, 1);
    };
    add_field(0, StatusUiText("Color theme", "配色テーマ"), grid, 1);
    add_field(1, StatusUiText("Size", "表示サイズ"), grid, 2);
    add_field(2, StatusUiText("Corner radius", "角丸"), grid, 3);
    add_field(3, StatusUiText("Opacity", "透明度"), grid, 4);
    // Each window has its own font family. The candidate picker originates
    // in the existing .ui; the other two reuse the same font list builder.
    QComboBox* font_combo = nullptr;
    if (index == 0) {
      font_combo = candidateRubyFontComboBox;
      font_combo->setParent(appearance_box);
    } else {
      font_combo = new QComboBox(appearance_box);
      font_combo->setObjectName(QString::fromLatin1(
          index == 1 ? "suggestWindowFontNameComboBox"
                     : "rubyWindowFontNameComboBox"));
      InitializeCandidateRubyFontComboBox(font_combo);
      QObject::connect(font_combo,
                       QOverload<int>::of(&QComboBox::currentIndexChanged),
                       this, [this](int) { EnableApplyButton(); });
    }
    appearance->addWidget(
        new QLabel(StatusUiText("Font family", "フォント"), appearance_box),
        4, 0);
    appearance->addWidget(font_combo, 4, 1);
    font_combo->show();
    appearance->addWidget(
        new QLabel(StatusUiText("Font weight", "フォントの太さ"),
                   appearance_box), 5, 0);
    move_from_grid(font_weight_layout, index, 1, appearance_box,
                   appearance, 5, 1);
    appearance->setColumnStretch(1, 1);
    editor_layout->addWidget(appearance_box);

    auto* shadow_box = new QGroupBox(StatusUiText("Shadow", "影"), editor);
    auto* shadow_layout = new QGridLayout(shadow_box);
    shadow_layout->setHorizontalSpacing(10);
    shadow_layout->setVerticalSpacing(6);
    // Compact two-pair layout: spread + opacity, then angle + distance.
    // Direction remains a separate 3x3 pad so it has enough room to operate.
    const auto add_shadow_field = [&](int row, int column,
                                      const QString& label,
                                      int original_column) {
      shadow_layout->addWidget(new QLabel(label, shadow_box), row, column);
      move_from_grid(shadow_grid, source_row, original_column,
                     shadow_box, shadow_layout, row, column + 1);
    };
    add_shadow_field(0, 0, StatusUiText("Spread", "広がり"), 1);
    add_shadow_field(0, 2, StatusUiText("Opacity", "濃さ"), 2);
    add_shadow_field(1, 0, StatusUiText("Angle", "角度"), 4);
    add_shadow_field(1, 2, StatusUiText("Distance", "距離"), 5);
    shadow_layout->addWidget(
        new QLabel(StatusUiText("Direction", "方向"), shadow_box), 2, 0);
    // Move the existing 3x3 direction pad intact.  Each button is 24x24
    // logical pixels with 1 px gaps; constrain its overall width so QGridLayout
    // cannot spread the button columns across the available editor width.
    QLayoutItem* pad_item = shadow_grid->itemAtPosition(source_row, 3);
    QWidget* direction_pad = pad_item != nullptr ? pad_item->widget() : nullptr;
    CHECK(direction_pad != nullptr) << "Missing direction pad";
    shadow_grid->removeWidget(direction_pad);
    direction_pad->setParent(shadow_box);
    direction_pad->setFixedSize(3 * 24 + 2, 3 * 24 + 2);
    shadow_layout->addWidget(direction_pad, 2, 1, 1, 3, Qt::AlignCenter);
    direction_pad->show();
    shadow_layout->setColumnStretch(1, 1);
    shadow_layout->setColumnStretch(3, 1);
    editor_layout->addWidget(shadow_box);

    if (index == 2) {
      body_layout->removeWidget(ruby_spacing_box);
      ruby_spacing_box->setParent(editor);
      editor_layout->addWidget(ruby_spacing_box);
      ruby_spacing_box->show();
    }

    auto* palette_group = findChild<QGroupBox*>(
        prefix + QStringLiteral("PaletteGroupBox"));
    CHECK(palette_group != nullptr) << "Missing renderer palette group";
    body_layout->removeWidget(palette_group);
    palette_group->setParent(editor);
    editor_layout->addWidget(palette_group);
    palette_group->show();
    editor_layout->addStretch();
    editor_scroll->setWidget(editor);
    page_layout->addWidget(editor_scroll);
    window_tabs->addTab(page, titles[index]);
  }
  // The old table headers and row labels are deleted here; the moved editors
  // have new QWidget parents and remain accessible by their original names.
  body_layout->removeWidget(font_weight_box);
  delete font_weight_box;
  body_layout->removeWidget(color_grid_widget);
  delete color_grid_widget;
  body_layout->removeWidget(shadow_grid_widget);
  delete shadow_grid_widget;
  for (int i = body_layout->count() - 1; i >= 0; --i) {
    if (body_layout->itemAt(i)->spacerItem() != nullptr) {
      delete body_layout->takeAt(i);
    }
  }
  body_layout->addWidget(window_tabs);

  // The same refresh path handles direct edits and programmatic preset loads.
  // Slot connections already installed during control creation execute before
  // these connections, so preview colors reflect the final button value.
  const auto refresh_previews = [window_previews]() {
    for (auto* preview : window_previews) preview->Refresh();
  };
  for (auto* combo : group->findChildren<QComboBox*>()) {
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     group, [refresh_previews](int) { refresh_previews(); });
  }
  for (auto* spin : group->findChildren<QSpinBox*>()) {
    QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                     group, [refresh_previews](int) { refresh_previews(); });
  }
  for (auto* button : group->findChildren<QPushButton*>()) {
    QObject::connect(button, &QPushButton::clicked, group,
                     [refresh_previews]() { refresh_previews(); });
  }

  // Place candidate/suggestion/ruby first on the Appearance tab.
  // On Windows, keep the input-mode indicator next and the TSF preedit color
  // customization after both sections.
  const int preedit_index =
      inputAppearanceContentLayout->indexOf(preeditDisplayColorGroupBox);
  const int renderer_insert_index =
      preedit_index >= 0 ? preedit_index : inputAppearanceContentLayout->count();
  inputAppearanceContentLayout->insertWidget(renderer_insert_index, group);

#ifdef _WIN32
  const int mode_indicator_insert_index =
      inputAppearanceContentLayout->indexOf(group) + 1;
  inputAppearanceContentLayout->insertWidget(mode_indicator_insert_index,
                                          mode_indicator_group);
#endif  // _WIN32
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
  SET_CHECKBOX(zenzStrictReadingPreservationCheckBox,
               use_zenz_strict_reading_preservation);
  SET_CHECKBOX(zenzDeferLiveConversionDisplayCheckBox,
               defer_live_conversion_display_until_zenz_result);

  const uint32_t zenz_live_correction_delay_msec =
      config.has_zenz_live_correction_delay_msec()
          ? config.zenz_live_correction_delay_msec()
          : kDefaultZenzLiveCorrectionDelayMsec;
  zenzLiveCorrectionDelaySpinBox->setValue(
      static_cast<int>(
          std::clamp(zenz_live_correction_delay_msec,
                     0u,
                     kMaxZenzLiveCorrectionDelayMsec)));

  const uint32_t zenz_deferred_presentation_timeout_msec =
      config.has_zenz_deferred_presentation_timeout_msec()
          ? config.zenz_deferred_presentation_timeout_msec()
          : kDefaultZenzDeferredPresentationTimeoutMsec;
  zenzDeferredPresentationTimeoutSpinBox->setValue(
      static_cast<int>(
          std::clamp(zenz_deferred_presentation_timeout_msec,
                     kMinZenzDeferredPresentationTimeoutMsec,
                     kMaxZenzDeferredPresentationTimeoutMsec)));

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

  SET_CHECKBOX(zenzFeedbackRecordingCheckBox, use_zenz_feedback_recording);
  SET_CHECKBOX(zenzFeedbackReuseCheckBox, use_zenz_feedback_reuse);
  SET_CHECKBOX(zenzMozcHistoryLearningCheckBox,
               use_zenz_mozc_history_learning);
  SET_CHECKBOX(zenzFeedbackMinKeyLengthCheckBox,
               use_zenz_feedback_min_key_length);
  zenzFeedbackMinKeyLengthSpinBox->setValue(static_cast<int>(
      std::clamp<uint32_t>(config.zenz_feedback_min_key_length(), 1, 20)));

  SET_CHECKBOX(zenzFeedbackAutoBlockCheckBox,
               use_zenz_auto_block_rejected_correction);
  const uint32_t zenz_auto_block_minimum_reject_count =
      config.has_zenz_auto_block_reject_threshold()
          ? config.zenz_auto_block_reject_threshold()
          : kDefaultZenzAutoBlockMinimumRejectCount;
  zenzFeedbackAutoBlockRejectThresholdSpinBox->setValue(
      static_cast<int>(
          std::clamp(zenz_auto_block_minimum_reject_count,
                     kMinZenzAutoBlockMinimumRejectCount,
                     kMaxZenzAutoBlockMinimumRejectCount)));
  const uint32_t zenz_auto_block_minimum_reject_percentage =
      config.has_zenz_auto_block_minimum_reject_percentage()
          ? config.zenz_auto_block_minimum_reject_percentage()
          : kDefaultZenzAutoBlockMinimumRejectPercentage;
  zenzFeedbackAutoBlockMinimumRejectPercentageSpinBox->setValue(
      static_cast<int>(
          std::clamp(zenz_auto_block_minimum_reject_percentage,
                     kMinZenzAutoBlockMinimumRejectPercentage,
                     kMaxZenzAutoBlockMinimumRejectPercentage)));
  SelectZenzFeedbackReuseSetting(
      static_cast<int>(zenzFeedbackReuseCheckBox->isChecked()));

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
#ifdef _WIN32
  SetComboCurrentData(
      modeIndicatorThemeComboBox,
      static_cast<int>(config.windows_mode_indicator_theme()));

  const auto& mode_indicator_style =
      config.windows_mode_indicator_custom_style();
  struct ModeIndicatorColorSetting {
    const char* name;
    uint32_t value;
  };
  const ModeIndicatorColorSetting mode_indicator_colors[] = {
      {"modeIndicatorAsciiBackgroundColorButton",
       mode_indicator_style.ascii_background_color()},
      {"modeIndicatorAsciiBorderColorButton",
       mode_indicator_style.ascii_border_color()},
      {"modeIndicatorAsciiTextColorButton",
       mode_indicator_style.ascii_text_color()},
      {"modeIndicatorAsciiShadowColorButton",
       mode_indicator_style.ascii_shadow_color()},
      {"modeIndicatorHiraganaBackgroundColorButton",
       mode_indicator_style.hiragana_background_color()},
      {"modeIndicatorHiraganaBorderColorButton",
       mode_indicator_style.hiragana_border_color()},
      {"modeIndicatorHiraganaTextColorButton",
       mode_indicator_style.hiragana_text_color()},
      {"modeIndicatorHiraganaShadowColorButton",
       mode_indicator_style.hiragana_shadow_color()},
      {"modeIndicatorKatakanaBackgroundColorButton",
       mode_indicator_style.katakana_background_color()},
      {"modeIndicatorKatakanaBorderColorButton",
       mode_indicator_style.katakana_border_color()},
      {"modeIndicatorKatakanaTextColorButton",
       mode_indicator_style.katakana_text_color()},
      {"modeIndicatorKatakanaShadowColorButton",
       mode_indicator_style.katakana_shadow_color()},
  };
  for (const auto& color : mode_indicator_colors) {
    SetColorButton(
        FindButton(this, QString::fromLatin1(color.name)), color.value);
  }

  struct ModeIndicatorValueSetting {
    const char* name;
    int value;
  };
  const ModeIndicatorValueSetting mode_indicator_values[] = {
      {"modeIndicatorWidthSpinBox",
       static_cast<int>(mode_indicator_style.width())},
      {"modeIndicatorHeightSpinBox",
       static_cast<int>(mode_indicator_style.height())},
      {"modeIndicatorCornerRadiusSpinBox",
       static_cast<int>(mode_indicator_style.corner_radius())},
      {"modeIndicatorLabelSizeSpinBox",
       static_cast<int>(mode_indicator_style.label_size())},
      {"modeIndicatorBorderThicknessSpinBox",
       static_cast<int>(mode_indicator_style.border_thickness())},
      {"modeIndicatorShadowBlurSpinBox",
       static_cast<int>(mode_indicator_style.shadow_blur())},
      {"modeIndicatorShadowOpacitySpinBox",
       static_cast<int>(mode_indicator_style.shadow_opacity_percent())},
      {"modeIndicatorShadowOffsetXSpinBox",
       mode_indicator_style.shadow_offset_x()},
      {"modeIndicatorShadowOffsetYSpinBox",
       mode_indicator_style.shadow_offset_y()},
  };
  for (const auto& value : mode_indicator_values) {
    if (QSpinBox* spin = FindSpinBox(this, value.name)) {
      spin->setValue(value.value);
    }
  }
#endif  // _WIN32
  SetComboCurrentData(
      FindComboBox(this, "windowsImeIconStyleComboBox"),
      static_cast<int>(config.windows_ime_icon_style()));

  ConvertRendererAppearanceFromProto(config);

  // New window-specific fonts inherit the original shared font if absent.
  // An explicitly saved empty value selects the system font independently.
  const std::string& legacy_font_name = config.candidate_ruby_font_name();
  SetComboBoxCurrentFontNameOrAdd(
      candidateRubyFontComboBox,
      QString::fromUtf8((config.has_candidate_window_font_name()
                             ? config.candidate_window_font_name()
                             : legacy_font_name).c_str()));
  if (QComboBox* combo = FindComboBox(this, "suggestWindowFontNameComboBox")) {
    SetComboBoxCurrentFontNameOrAdd(
        combo, QString::fromUtf8((config.has_suggest_window_font_name()
                                     ? config.suggest_window_font_name()
                                     : legacy_font_name).c_str()));
  }
  if (QComboBox* combo = FindComboBox(this, "rubyWindowFontNameComboBox")) {
    SetComboBoxCurrentFontNameOrAdd(
        combo, QString::fromUtf8((config.has_ruby_window_font_name()
                                     ? config.ruby_window_font_name()
                                     : legacy_font_name).c_str()));
  }

  SET_CHECKBOX(dimPendingRomanInputCheckBox, dim_pending_roman_input);
  const uint32_t pending_roman_dimness_percent =
      config.has_pending_roman_dimness_percent()
          ? config.pending_roman_dimness_percent()
          : kDefaultPendingRomanDimnessPercent;
  pendingRomanDimnessSpinBox->setValue(
      static_cast<int>(std::clamp(
          pending_roman_dimness_percent,
          kMinPendingRomanDimnessPercent,
          kMaxPendingRomanDimnessPercent)));

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

  SET_CHECKBOX(zenzPreeditTextColorCheckBox,
                use_custom_zenz_live_correction_text_color);
  SetColorButton(zenzPreeditTextColorButton,
                 config.zenz_live_correction_text_color());
  zenzPreeditTextColorButton->setEnabled(
      config.use_custom_zenz_live_correction_text_color());

  SET_CHECKBOX(zenzPreeditBackgroundColorCheckBox,
                use_custom_zenz_live_correction_background_color);
  SetColorButton(zenzPreeditBackgroundColorButton,
                 config.zenz_live_correction_background_color());
  zenzPreeditBackgroundColorButton->setEnabled(
      config.use_custom_zenz_live_correction_background_color());

  SET_CHECKBOX(zenzPreeditUnderlineColorCheckBox,
                use_custom_zenz_live_correction_underline_color);
  SetColorButton(zenzPreeditUnderlineColorButton,
                 config.zenz_live_correction_underline_color());
  zenzPreeditUnderlineColorButton->setEnabled(
      config.use_custom_zenz_live_correction_underline_color());

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
  GET_CHECKBOX(zenzStrictReadingPreservationCheckBox,
               use_zenz_strict_reading_preservation);
  GET_CHECKBOX(zenzDeferLiveConversionDisplayCheckBox,
               defer_live_conversion_display_until_zenz_result);
  config->set_zenz_live_correction_delay_msec(
      static_cast<uint32_t>(zenzLiveCorrectionDelaySpinBox->value()));
  config->set_zenz_deferred_presentation_timeout_msec(
      static_cast<uint32_t>(
          zenzDeferredPresentationTimeoutSpinBox->value()));
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

  GET_CHECKBOX(zenzFeedbackRecordingCheckBox, use_zenz_feedback_recording);
  GET_CHECKBOX(zenzFeedbackReuseCheckBox, use_zenz_feedback_reuse);
  GET_CHECKBOX(zenzMozcHistoryLearningCheckBox,
               use_zenz_mozc_history_learning);
  GET_CHECKBOX(zenzFeedbackMinKeyLengthCheckBox,
               use_zenz_feedback_min_key_length);
  config->set_zenz_feedback_min_key_length(
      static_cast<uint32_t>(zenzFeedbackMinKeyLengthSpinBox->value()));
  GET_CHECKBOX(zenzFeedbackAutoBlockCheckBox,
               use_zenz_auto_block_rejected_correction);
  config->set_zenz_auto_block_reject_threshold(
      static_cast<uint32_t>(
          zenzFeedbackAutoBlockRejectThresholdSpinBox->value()));
  config->set_zenz_auto_block_minimum_reject_percentage(
      static_cast<uint32_t>(
          zenzFeedbackAutoBlockMinimumRejectPercentageSpinBox->value()));

  GET_CHECKBOX(useAutoConversion, use_auto_conversion);
  GET_CHECKBOX(useDirectCommit, use_direct_commit);
  GET_CHECKBOX(useJapaneseLayout, use_japanese_layout);

  GET_CHECKBOX(useModeIndicator, use_mode_indicator);
#ifdef _WIN32
  config->set_windows_mode_indicator_theme(
      static_cast<config::Config::WindowsModeIndicatorTheme>(
          GetComboCurrentData(
              modeIndicatorThemeComboBox,
              static_cast<int>(
                  config::Config::WINDOWS_MODE_INDICATOR_THEME_SYSTEM))));

  auto* mode_indicator_style =
      config->mutable_windows_mode_indicator_custom_style();
  mode_indicator_style->set_ascii_background_color(
      GetColorButtonRgb(
          FindButton(this,
                     QStringLiteral("modeIndicatorAsciiBackgroundColorButton")),
          GetDefaultModeIndicatorCustomStyle().ascii_background_color()));
  mode_indicator_style->set_ascii_border_color(
      GetColorButtonRgb(
          FindButton(this,
                     QStringLiteral("modeIndicatorAsciiBorderColorButton")),
          GetDefaultModeIndicatorCustomStyle().ascii_border_color()));
  mode_indicator_style->set_ascii_text_color(
      GetColorButtonRgb(
          FindButton(this,
                     QStringLiteral("modeIndicatorAsciiTextColorButton")),
          GetDefaultModeIndicatorCustomStyle().ascii_text_color()));
  mode_indicator_style->set_ascii_shadow_color(
      GetColorButtonRgb(
          FindButton(this,
                     QStringLiteral("modeIndicatorAsciiShadowColorButton")),
          GetDefaultModeIndicatorCustomStyle().ascii_shadow_color()));

  mode_indicator_style->set_hiragana_background_color(
      GetColorButtonRgb(
          FindButton(
              this,
              QStringLiteral("modeIndicatorHiraganaBackgroundColorButton")),
          GetDefaultModeIndicatorCustomStyle().hiragana_background_color()));
  mode_indicator_style->set_hiragana_border_color(
      GetColorButtonRgb(
          FindButton(this,
                     QStringLiteral("modeIndicatorHiraganaBorderColorButton")),
          GetDefaultModeIndicatorCustomStyle().hiragana_border_color()));
  mode_indicator_style->set_hiragana_text_color(
      GetColorButtonRgb(
          FindButton(this,
                     QStringLiteral("modeIndicatorHiraganaTextColorButton")),
          GetDefaultModeIndicatorCustomStyle().hiragana_text_color()));
  mode_indicator_style->set_hiragana_shadow_color(
      GetColorButtonRgb(
          FindButton(this,
                     QStringLiteral("modeIndicatorHiraganaShadowColorButton")),
          GetDefaultModeIndicatorCustomStyle().hiragana_shadow_color()));

  mode_indicator_style->set_katakana_background_color(
      GetColorButtonRgb(
          FindButton(
              this,
              QStringLiteral("modeIndicatorKatakanaBackgroundColorButton")),
          GetDefaultModeIndicatorCustomStyle().katakana_background_color()));
  mode_indicator_style->set_katakana_border_color(
      GetColorButtonRgb(
          FindButton(this,
                     QStringLiteral("modeIndicatorKatakanaBorderColorButton")),
          GetDefaultModeIndicatorCustomStyle().katakana_border_color()));
  mode_indicator_style->set_katakana_text_color(
      GetColorButtonRgb(
          FindButton(this,
                     QStringLiteral("modeIndicatorKatakanaTextColorButton")),
          GetDefaultModeIndicatorCustomStyle().katakana_text_color()));
  mode_indicator_style->set_katakana_shadow_color(
      GetColorButtonRgb(
          FindButton(this,
                     QStringLiteral("modeIndicatorKatakanaShadowColorButton")),
          GetDefaultModeIndicatorCustomStyle().katakana_shadow_color()));

  const auto spin_value = [&](const char* name, int default_value) {
    const QSpinBox* spin = FindSpinBox(this, name);
    return spin != nullptr ? spin->value() : default_value;
  };
  const auto& default_mode_indicator_style =
      GetDefaultModeIndicatorCustomStyle();
  mode_indicator_style->set_width(static_cast<uint32_t>(spin_value(
      "modeIndicatorWidthSpinBox",
      static_cast<int>(default_mode_indicator_style.width()))));
  mode_indicator_style->set_height(static_cast<uint32_t>(spin_value(
      "modeIndicatorHeightSpinBox",
      static_cast<int>(default_mode_indicator_style.height()))));
  mode_indicator_style->set_corner_radius(static_cast<uint32_t>(spin_value(
      "modeIndicatorCornerRadiusSpinBox",
      static_cast<int>(default_mode_indicator_style.corner_radius()))));
  mode_indicator_style->set_label_size(static_cast<uint32_t>(spin_value(
      "modeIndicatorLabelSizeSpinBox",
      static_cast<int>(default_mode_indicator_style.label_size()))));
  mode_indicator_style->set_border_thickness(static_cast<uint32_t>(spin_value(
      "modeIndicatorBorderThicknessSpinBox",
      static_cast<int>(default_mode_indicator_style.border_thickness()))));
  mode_indicator_style->set_shadow_blur(static_cast<uint32_t>(spin_value(
      "modeIndicatorShadowBlurSpinBox",
      static_cast<int>(default_mode_indicator_style.shadow_blur()))));
  mode_indicator_style->set_shadow_opacity_percent(
      static_cast<uint32_t>(spin_value(
          "modeIndicatorShadowOpacitySpinBox",
          static_cast<int>(
              default_mode_indicator_style.shadow_opacity_percent()))));
  mode_indicator_style->set_shadow_offset_x(
      spin_value("modeIndicatorShadowOffsetXSpinBox",
                 default_mode_indicator_style.shadow_offset_x()));
  mode_indicator_style->set_shadow_offset_y(
      spin_value("modeIndicatorShadowOffsetYSpinBox",
                 default_mode_indicator_style.shadow_offset_y()));
#endif  // _WIN32
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

  const auto selected_font = [](const QComboBox* combo) -> std::string {
    return combo == nullptr ? std::string()
                            : combo->currentData().toString().trimmed()
                                  .toUtf8().toStdString();
  };
  const std::string candidate_font =
      selected_font(candidateRubyFontComboBox);
  const std::string suggest_font = selected_font(
      FindComboBox(this, "suggestWindowFontNameComboBox"));
  const std::string ruby_font = selected_font(
      FindComboBox(this, "rubyWindowFontNameComboBox"));
  // Retain the historical shared field for older Mozkey versions, with the
  // candidate font as its compatibility value.
  if (!candidate_font.empty()) {
    config->set_candidate_ruby_font_name(candidate_font);
  } else {
    config->clear_candidate_ruby_font_name();
  }
  config->set_candidate_window_font_name(candidate_font);
  config->set_suggest_window_font_name(suggest_font);
  config->set_ruby_window_font_name(ruby_font);

  GET_CHECKBOX(dimPendingRomanInputCheckBox, dim_pending_roman_input);
  config->set_pending_roman_dimness_percent(
      static_cast<uint32_t>(pendingRomanDimnessSpinBox->value()));

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

  GET_CHECKBOX(zenzPreeditTextColorCheckBox,
                use_custom_zenz_live_correction_text_color);
  config->set_zenz_live_correction_text_color(
      GetColorButtonRgb(zenzPreeditTextColorButton,
                        kDefaultZenzPreeditTextColor));

  GET_CHECKBOX(zenzPreeditBackgroundColorCheckBox,
                use_custom_zenz_live_correction_background_color);
  config->set_zenz_live_correction_background_color(
      GetColorButtonRgb(zenzPreeditBackgroundColorButton,
                        kDefaultZenzPreeditBackgroundColor));

  GET_CHECKBOX(zenzPreeditUnderlineColorCheckBox,
                use_custom_zenz_live_correction_underline_color);
  config->set_zenz_live_correction_underline_color(
      GetColorButtonRgb(zenzPreeditUnderlineColorButton,
                        kDefaultZenzPreeditUnderlineColor));

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

  if (QCheckBox* check = findChild<QCheckBox*>("conversionStatusEnabledCheckBox"))
    check->setChecked(config.show_conversion_status_indicator());
  const auto& status = config.conversion_status_indicator_style();
  SetComboCurrentData(FindComboBox(this, "conversionStatusShapeComboBox"),
                      static_cast<int>(status.shape()));
  SetComboCurrentData(FindComboBox(this, "conversionStatusLabelComboBox"),
                      static_cast<int>(status.label()));
  SetComboCurrentData(FindComboBox(this, "conversionStatusPlacementComboBox"),
                      static_cast<int>(status.placement()));
  SetComboBoxCurrentFontNameOrAdd(
      FindComboBox(this, "conversionStatusFontComboBox"),
      QString::fromStdString(status.font_family()));
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusMozcColorButton"))
    SetColorButton(button, status.mozc_color());
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusPendingColorButton"))
    SetColorButton(button, status.zenz_pending_color());
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusZenzColorButton"))
    SetColorButton(button, status.zenz_color());
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusReadingColorButton"))
    SetColorButton(button, status.reading_color());
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusTextColorButton"))
    SetColorButton(button, status.text_color());
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusBorderColorButton"))
    SetColorButton(button, status.border_color());
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusWidthSpinBox"))
    spin->setValue(static_cast<int>(status.width()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusHeightSpinBox"))
    spin->setValue(static_cast<int>(status.height()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusFontSizeSpinBox"))
    spin->setValue(static_cast<int>(status.font_size()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusCornerSpinBox"))
    spin->setValue(static_cast<int>(status.corner_radius()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusBorderWidthSpinBox"))
    spin->setValue(static_cast<int>(status.border_width()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusOpacitySpinBox"))
    spin->setValue(static_cast<int>(status.opacity_percent()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusShadowSizeSpinBox"))
    spin->setValue(static_cast<int>(status.shadow_size()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusShadowOpacitySpinBox"))
    spin->setValue(static_cast<int>(status.shadow_opacity_percent()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusShadowAngleSpinBox"))
    spin->setValue(static_cast<int>(status.shadow_angle_degrees()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusShadowDistanceSpinBox"))
    spin->setValue(static_cast<int>(status.shadow_distance()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusOffsetXSpinBox"))
    spin->setValue(static_cast<int>(status.offset_x()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusOffsetYSpinBox"))
    spin->setValue(static_cast<int>(status.offset_y()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusGapSpinBox"))
    spin->setValue(static_cast<int>(status.gap()));
  UpdateConversionStatusPreview();

  const int candidate_color_theme =
      config.has_candidate_window_color_theme()
          ? static_cast<int>(config.candidate_window_color_theme())
      : config.has_use_dark_mode_candidate_window()
          ? static_cast<int>(config.use_dark_mode_candidate_window()
                                 ? config::Config::RENDERER_WINDOW_COLOR_DARK
                                 : config::Config::RENDERER_WINDOW_COLOR_LIGHT)
          : static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_AUTO);
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

  if (const QCheckBox* check = findChild<QCheckBox*>("conversionStatusEnabledCheckBox"))
    config->set_show_conversion_status_indicator(check->isChecked());
  auto* status = config->mutable_conversion_status_indicator_style();
  status->set_shape(static_cast<config::Config::ConversionStatusIndicatorShape>(
      GetComboCurrentData(FindComboBox(this, "conversionStatusShapeComboBox"), 0)));
  status->set_label(static_cast<config::Config::ConversionStatusIndicatorLabel>(
      GetComboCurrentData(FindComboBox(this, "conversionStatusLabelComboBox"), 0)));
  status->set_placement(static_cast<config::Config::ConversionStatusIndicatorPlacement>(
      GetComboCurrentData(FindComboBox(this, "conversionStatusPlacementComboBox"), 0)));
  if (const QComboBox* font = FindComboBox(this, "conversionStatusFontComboBox")) {
    status->set_font_family(font->currentData().toString().toStdString());
  }
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusMozcColorButton"))
    status->set_mozc_color(GetColorButtonRgb(button, status->mozc_color()));
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusPendingColorButton"))
    status->set_zenz_pending_color(GetColorButtonRgb(button, status->zenz_pending_color()));
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusZenzColorButton"))
    status->set_zenz_color(GetColorButtonRgb(button, status->zenz_color()));
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusReadingColorButton"))
    status->set_reading_color(GetColorButtonRgb(button, status->reading_color()));
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusTextColorButton"))
    status->set_text_color(GetColorButtonRgb(button, status->text_color()));
  if (QPushButton* button = findChild<QPushButton*>("conversionStatusBorderColorButton"))
    status->set_border_color(GetColorButtonRgb(button, status->border_color()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusWidthSpinBox"))
    status->set_width(static_cast<uint32_t>(spin->value()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusHeightSpinBox"))
    status->set_height(static_cast<uint32_t>(spin->value()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusFontSizeSpinBox"))
    status->set_font_size(static_cast<uint32_t>(spin->value()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusCornerSpinBox"))
    status->set_corner_radius(static_cast<uint32_t>(spin->value()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusBorderWidthSpinBox"))
    status->set_border_width(static_cast<uint32_t>(spin->value()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusOpacitySpinBox"))
    status->set_opacity_percent(static_cast<uint32_t>(spin->value()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusShadowSizeSpinBox"))
    status->set_shadow_size(static_cast<uint32_t>(spin->value()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusShadowOpacitySpinBox"))
    status->set_shadow_opacity_percent(static_cast<uint32_t>(spin->value()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusShadowAngleSpinBox"))
    status->set_shadow_angle_degrees(static_cast<uint32_t>(spin->value()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusShadowDistanceSpinBox"))
    status->set_shadow_distance(static_cast<uint32_t>(spin->value()));
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusOffsetXSpinBox"))
    status->set_offset_x(spin->value());
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusOffsetYSpinBox"))
    status->set_offset_y(spin->value());
  if (QSpinBox* spin = FindSpinBox(this, "conversionStatusGapSpinBox"))
    status->set_gap(static_cast<uint32_t>(spin->value()));

  const int candidate_color_theme = GetComboCurrentData(
      FindComboBox(this, "candidateWindowColorThemeComboBox"),
      static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_AUTO));
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
              static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_AUTO))));
  config->set_ruby_window_color_theme(
      static_cast<config::Config::RendererWindowColorTheme>(
          GetComboCurrentData(
              FindComboBox(this, "rubyWindowColorThemeComboBox"),
              static_cast<int>(config::Config::RENDERER_WINDOW_COLOR_AUTO))));

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
                          config::Config::RENDERER_WINDOW_COLOR_AUTO));
  SetComboCurrentData(FindComboBox(this, "suggestWindowColorThemeComboBox"),
                      static_cast<int>(
                          config::Config::RENDERER_WINDOW_COLOR_AUTO));
  SetComboCurrentData(FindComboBox(this, "rubyWindowColorThemeComboBox"),
                      static_cast<int>(
                          config::Config::RENDERER_WINDOW_COLOR_AUTO));

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
  SetComboBoxCurrentFontNameOrAdd(
      FindComboBox(this, "suggestWindowFontNameComboBox"), QString());
  SetComboBoxCurrentFontNameOrAdd(
      FindComboBox(this, "rubyWindowFontNameComboBox"), QString());
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
  UpdateConversionStatusPreview();
  if (QWidget* preview = findChild<QWidget*>("modeIndicatorLivePreview")) {
    preview->repaint();
  }
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
#ifdef _WIN32
  if (QWidget* custom_box =
          findChild<QWidget*>(QStringLiteral("modeIndicatorCustomGroupBox"))) {
    const int custom_theme =
        static_cast<int>(
            config::Config::WINDOWS_MODE_INDICATOR_THEME_CUSTOM);
    custom_box->setEnabled(
        useModeIndicator->isChecked() &&
        GetComboCurrentData(modeIndicatorThemeComboBox, custom_theme) ==
            custom_theme);
  }
#endif  // _WIN32

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
  // QWidget has Q_OBJECT; the settings-only preview subclass does not.
  // Look up previews by their stable object names and schedule a repaint.
  constexpr const char* kPreviewNames[] = {
      "candidateWindowAppearancePreview",
      "suggestWindowAppearancePreview",
      "rubyWindowAppearancePreview"};
  for (const char* name : kPreviewNames) {
    if (QWidget* preview = findChild<QWidget*>(QString::fromLatin1(name))) {
      preview->update();
    }
  }
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
          tr("This operation clears Mozc conversion learning, including "
             "learning reflected from accepted Zenz results. Input history "
             "data and Zenz learning data itself are not cleared. To remove "
             "input history data, use \"Suggestion\" -> \"Clear all history\". "
             "To manage Zenz learning data, use "
             "\"Advanced\" -> \"Zenz Learning\". Do you want to continue?"),
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
  // Enable Roman-table-specific controls only for Roman input.
  const bool roman_input = (index == 0);
  editRomanTableButton->setEnabled(roman_input);
  dimPendingRomanInputCheckBox->setEnabled(roman_input);

  const bool dimness_enabled =
      roman_input && dimPendingRomanInputCheckBox->isChecked();
  pendingRomanDimnessLabel->setEnabled(dimness_enabled);
  pendingRomanDimnessSpinBox->setEnabled(dimness_enabled);
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
  zenzDeferLiveConversionDisplayCheckBox->setEnabled(enabled);
  SelectZenzDeferredPresentationSetting(
      enabled ? static_cast<int>(
                    zenzDeferLiveConversionDisplayCheckBox->isChecked())
              : 0);
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
  // Recording, automatic reuse, and external Mozc history learning are
  // independent preferences. Keep their controls available even when live
  // correction is disabled: the feedback candidate rewriter also operates
  // on normal conversion, and turning live correction back on must not
  // silently change any of the three persisted choices.
}

void ConfigDialog::SelectZenzDeferredPresentationSetting(int state) {
  const bool enabled =
      liveConversionCheckBox->isChecked() &&
      zenzLiveCorrectionCheckBox->isChecked() &&
      static_cast<bool>(state);

  zenzDeferredPresentationTimeoutLabel->setEnabled(enabled);
  zenzDeferredPresentationTimeoutSpinBox->setEnabled(enabled);
}

void ConfigDialog::SelectZenzFeedbackReuseSetting(int state) {
  const bool enabled = static_cast<bool>(state);

  zenzFeedbackAutoBlockCheckBox->setEnabled(enabled);
  zenzFeedbackMinKeyLengthCheckBox->setEnabled(enabled);
  const bool min_key_enabled =
      enabled && zenzFeedbackMinKeyLengthCheckBox->isChecked();
  zenzFeedbackMinKeyLengthLabel->setEnabled(min_key_enabled);
  zenzFeedbackMinKeyLengthSpinBox->setEnabled(min_key_enabled);

  SelectZenzFeedbackAutoBlockSetting(
      enabled ? static_cast<int>(
                    zenzFeedbackAutoBlockCheckBox->isChecked())
              : 0);
}

void ConfigDialog::SelectZenzFeedbackAutoBlockSetting(int state) {
  const bool enabled =
      zenzFeedbackReuseCheckBox->isChecked() && static_cast<bool>(state);

  zenzFeedbackAutoBlockRejectThresholdLabel->setEnabled(enabled);
  zenzFeedbackAutoBlockRejectThresholdSpinBox->setEnabled(enabled);
  zenzFeedbackAutoBlockMinimumRejectPercentageLabel->setEnabled(enabled);
  zenzFeedbackAutoBlockMinimumRejectPercentageSpinBox->setEnabled(enabled);
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
#ifdef _WIN32
  modeIndicatorThemeComboBox->setEnabled(useModeIndicator->isChecked());
#endif  // _WIN32
  UpdateRendererAppearanceControls();
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
  if (event->type() == QEvent::Wheel) {
    QWidget *widget = qobject_cast<QWidget *>(obj);
    if (widget != nullptr && isAncestorOf(widget) &&
        (qobject_cast<QComboBox *>(widget) != nullptr ||
         qobject_cast<QAbstractSpinBox *>(widget) != nullptr)) {
      // QComboBox and QAbstractSpinBox normally consume wheel gestures to
      // change their value.  In a settings dialog this is easy to trigger
      // accidentally while scrolling.  Keep the setting unchanged and, when
      // the control lives in a scroll area, forward the same gesture to that
      // scroll area instead.
      ForwardWheelEventToScrollArea(widget,
                                    static_cast<QWheelEvent *>(event));
      return true;
    }
  }

  if (event->type() == QEvent::MouseButtonRelease) {
    if (obj == incognitoModeMessage) {
      incognitoModeCheckBox->toggle();
    }
  }
  return QObject::eventFilter(obj, event);
}

}  // namespace gui
}  // namespace mozc
