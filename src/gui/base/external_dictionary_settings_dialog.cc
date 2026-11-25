// Copyright 2026 Mozkey contributors.
#include "gui/base/external_dictionary_settings_dialog.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mozc::gui {
namespace {
constexpr size_t kMaxSources = 32;

QString FromUtf8(const std::string& value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string ToUtf8(const QString& value) {
  return value.toUtf8().toStdString();
}
}  // namespace

bool ExternalDictionarySettingsDialog::Edit(
    QWidget* parent, bool* enabled, std::vector<std::string>* paths) {
  if (enabled == nullptr || paths == nullptr) return false;
  ExternalDictionarySettingsDialog dialog(parent, *enabled, *paths);
  if (dialog.exec() != QDialog::Accepted) return false;
  return dialog.CollectAndValidate(enabled, paths);
}

ExternalDictionarySettingsDialog::ExternalDictionarySettingsDialog(
    QWidget* parent, bool enabled, const std::vector<std::string>& paths)
    : QDialog(parent), paths_(paths) {
  setWindowTitle(QStringLiteral("外部辞書の設定"));
  setMinimumSize(650, 440);
  resize(760, 530);
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(18, 16, 18, 16);
  root->setSpacing(12);

  auto* lead = new QLabel(QStringLiteral(
      "TSV ファイルを登録すると、Mozkey の変換候補に自動で追加されます。"
      "\nファイルを更新すれば、通常は約3秒以内に再読み込みされます。"), this);
  lead->setWordWrap(true);
  root->addWidget(lead);

  enabled_checkbox_ = new QCheckBox(QStringLiteral("外部辞書を使用する"), this);
  enabled_checkbox_->setChecked(enabled);
  root->addWidget(enabled_checkbox_);

  auto* heading = new QLabel(QStringLiteral("登録済みの辞書ファイル"), this);
  QFont heading_font = heading->font();
  heading_font.setBold(true);
  heading->setFont(heading_font);
  root->addWidget(heading);

  auto* list_row = new QHBoxLayout();
  table_ = new QTableWidget(this);
  table_->setColumnCount(2);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("ファイル"), QStringLiteral("状態")});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  table_->setAlternatingRowColors(true);
  list_row->addWidget(table_, 1);

  auto* buttons = new QVBoxLayout();
  auto* add = new QPushButton(QStringLiteral("追加…"), this);
  remove_button_ = new QPushButton(QStringLiteral("削除"), this);
  up_button_ = new QPushButton(QStringLiteral("上へ"), this);
  down_button_ = new QPushButton(QStringLiteral("下へ"), this);
  open_button_ = new QPushButton(QStringLiteral("ファイルを開く"), this);
  for (auto* button : {add, remove_button_, up_button_, down_button_, open_button_}) {
    button->setMinimumWidth(112);
    buttons->addWidget(button);
  }
  buttons->addStretch(1);
  list_row->addLayout(buttons);
  root->addLayout(list_row, 1);

  auto* hint = new QLabel(QStringLiteral(
      "上から順に候補へ追加します。同じ読みと候補の重複はまとめます。"
      "\n通常の第1候補は変更しません。登録したファイル自体は変更されません。"), this);
  hint->setWordWrap(true);
  root->addWidget(hint);

  auto* example_group = new QGroupBox(QStringLiteral("使い方・記述例"), this);
  auto* example_layout = new QVBoxLayout(example_group);
  auto* explanation = new QLabel(QStringLiteral(
      "例：「せいひんえー」から製品の型番、電話番号から担当者名を呼び出せます。"
      "\n製品台帳や連絡先一覧を「読み」と「候補」のタブ区切りで登録します（以下は架空の例）。"),
      example_group);
  explanation->setWordWrap(true);
  example_layout->addWidget(explanation);
  auto* example = new QPlainTextEdit(example_group);
  example->setReadOnly(true);
  example->setPlainText(QStringLiteral(
      "せいひんえー\tABC-1234\n"
      "0312345678\t山田 太郎\n"
      "しゃないへるぷ\t社内ヘルプデスク（内線1234）"));
  example->setMaximumHeight(82);
  example_layout->addWidget(example);
  root->addWidget(example_group);

  auto* footer = new QDialogButtonBox(QDialogButtonBox::Ok |
                                       QDialogButtonBox::Cancel, this);
  footer->button(QDialogButtonBox::Ok)->setText(QStringLiteral("決定"));
  footer->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("キャンセル"));
  root->addWidget(footer);

  connect(add, &QPushButton::clicked, this, [this] { AddFiles(); });
  connect(remove_button_, &QPushButton::clicked, this, [this] { RemoveSelected(); });
  connect(up_button_, &QPushButton::clicked, this, [this] { MoveSelected(-1); });
  connect(down_button_, &QPushButton::clicked, this, [this] { MoveSelected(1); });
  connect(open_button_, &QPushButton::clicked, this, [this] { OpenSelected(); });
  connect(table_, &QTableWidget::itemSelectionChanged,
          this, [this] { UpdateSelectionActions(); });
  connect(footer, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(footer, &QDialogButtonBox::accepted, this, [this] {
    if (enabled_checkbox_->isChecked() && paths_.empty()) {
      QMessageBox::warning(this, windowTitle(),
                           QStringLiteral("使用する TSV ファイルを追加してください。"));
      return;
    }
    accept();
  });
  RefreshRows();
}

void ExternalDictionarySettingsDialog::RefreshRows() {
  const int previous = table_->currentRow();
  table_->setRowCount(static_cast<int>(paths_.size()));
  for (size_t i = 0; i < paths_.size(); ++i) {
    const QString path = FromUtf8(paths_[i]);
    auto* item = new QTableWidgetItem(path);
    item->setToolTip(path);
    table_->setItem(static_cast<int>(i), 0, item);
    const QFileInfo info(path);
    const QString state = info.isFile() && info.isReadable()
        ? QStringLiteral("ファイルあり") : QStringLiteral("未検出・読込不可");
    table_->setItem(static_cast<int>(i), 1, new QTableWidgetItem(state));
  }
  if (!paths_.empty()) {
    table_->selectRow(std::max(0, std::min(previous,
                              static_cast<int>(paths_.size()) - 1)));
  }
  UpdateSelectionActions();
}

void ExternalDictionarySettingsDialog::UpdateSelectionActions() {
  const int row = table_->currentRow();
  const bool selected = row >= 0 && row < static_cast<int>(paths_.size());
  remove_button_->setEnabled(selected);
  open_button_->setEnabled(selected);
  up_button_->setEnabled(selected && row > 0);
  down_button_->setEnabled(selected && row + 1 < static_cast<int>(paths_.size()));
}

void ExternalDictionarySettingsDialog::AddFiles() {
  const QStringList files = QFileDialog::getOpenFileNames(
      this, QStringLiteral("外部 TSV 辞書を追加"), QString(),
      QStringLiteral("TSV ファイル (*.tsv);;すべてのファイル (*)"));
  for (const QString& file : files) {
    const std::string path = ToUtf8(QFileInfo(file).absoluteFilePath());
    if (std::find(paths_.begin(), paths_.end(), path) != paths_.end()) continue;
    if (paths_.size() >= kMaxSources) {
      QMessageBox::warning(this, windowTitle(),
                           QStringLiteral("登録できるファイルは最大32個です。"));
      break;
    }
    paths_.push_back(path);
  }
  RefreshRows();
}

void ExternalDictionarySettingsDialog::RemoveSelected() {
  const int row = table_->currentRow();
  if (row < 0 || row >= static_cast<int>(paths_.size())) return;
  paths_.erase(paths_.begin() + row);
  RefreshRows();
}

void ExternalDictionarySettingsDialog::MoveSelected(int direction) {
  const int row = table_->currentRow();
  const int target = row + direction;
  if (row < 0 || target < 0 || target >= static_cast<int>(paths_.size())) return;
  std::swap(paths_[row], paths_[target]);
  RefreshRows();
  table_->selectRow(target);
}

void ExternalDictionarySettingsDialog::OpenSelected() {
  const int row = table_->currentRow();
  if (row < 0 || row >= static_cast<int>(paths_.size())) return;
  const QString path = FromUtf8(paths_[row]);
  if (!QFileInfo(path).isFile()) {
    QMessageBox::warning(this, windowTitle(), QStringLiteral("ファイルが見つかりません。"));
    return;
  }
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
    QMessageBox::warning(this, windowTitle(), QStringLiteral("ファイルを開けませんでした。"));
  }
}

bool ExternalDictionarySettingsDialog::CollectAndValidate(
    bool* enabled, std::vector<std::string>* paths) {
  if (enabled == nullptr || paths == nullptr) return false;
  *enabled = enabled_checkbox_->isChecked();
  *paths = paths_;
  return true;
}
}  // namespace mozc::gui
