// Copyright 2026 Mozkey contributors.
#include "gui/base/external_dictionary_settings_dialog.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <string>
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
using SourceOptions = config::ExternalDictionarySourceOptions;

void ShowHelp(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("外部辞書の使い方・活用例"));
  dialog.setMinimumSize(580, 420);
  dialog.resize(740, 530);
  auto* root = new QVBoxLayout(&dialog);
  auto* intro = new QLabel(QStringLiteral(
      "外部の TSV ファイルを変換候補として利用できます。用途に合わせて辞書を管理しましょう。"), &dialog);
  intro->setWordWrap(true);
  root->addWidget(intro);
  auto* tabs = new QTabWidget(&dialog);
  const auto add_page = [tabs](const QString& title, const QString& text,
                               const QString& example = QString()) {
    auto* page = new QWidget(tabs);
    auto* layout = new QVBoxLayout(page);
    auto* body = new QPlainTextEdit(page);
    body->setReadOnly(true);
    body->setPlainText(text);
    layout->addWidget(body, 2);
    if (!example.isEmpty()) {
      layout->addWidget(new QLabel(QStringLiteral("TSV の例（タブ区切り・コピー可能）"), page));
      auto* sample = new QPlainTextEdit(page);
      sample->setReadOnly(true);
      sample->setPlainText(example);
      sample->setLineWrapMode(QPlainTextEdit::NoWrap);
      layout->addWidget(sample, 1);
    }
    tabs->addTab(page, title);
  };
  add_page(QStringLiteral("基本・書式"), QStringLiteral(
      "1. UTF-8 の TSV ファイルを用意します。\n"
      "2. 追加… でファイルを登録し、外部辞書を使用する を有効にします。\n"
      "3. 決定し、設定画面から開いた場合は設定画面側でも保存します。\n"
      "4. 登録した読みを入力し、通常の変換操作で候補を選択します。\n\n"
      "読みと候補をタブで区切った2列構成です。品詞やコストは不要です。"
      "空行と # から始まるコメント行は無視されます。空欄、値に含まれるタブ・改行は使用できません。\n\n"
      "編集… で辞書名と一致方式を変更できます。名前が空欄ならファイル名が表示されます。"
      "名前を変更しても元のファイルは変更されません。パスをコピー でファイルの所在を確認できます。"),
      QStringLiteral("せいひんえー\tABC-1234\n0312345678\t山田 太郎"));
  add_page(QStringLiteral("一致方式"), QStringLiteral(
      "完全一致（初期設定）\n"
      "通常変換の対象全体が登録された読みと一致すると候補を追加します。\n\n"
      "完全一致＋前方一致\n"
      "下の例で「せいひん」と入力して変換すると、ABC-1234 と XYZ-5678 が追加されます。"
      "完全一致も引き続き利用できます。前方一致を許可したファイルだけが対象です。\n\n"
      "前方一致は3文字（Unicode コードポイント）以上、合計最大10候補です。"
      "1回の検索で最大256件の候補参照を調べるため、10件未満で打ち切る場合もあります。"
      "完全一致候補を優先し、通常の第1候補は維持します。\n\n"
      "文中・中間・曖昧一致や予測・サジェストではありません。候補を選択すると、入力済みの読みだけを消費して確定します。"),
      QStringLiteral("せいひんえー\tABC-1234\nせいひんびー\tXYZ-5678"));
  add_page(QStringLiteral("一覧・別名"), QStringLiteral(
      "同じ読みから複数候補を呼び出せます。異なる読みから同じ候補を呼び出すこともできます。\n\n"
      "下の例では「せいひんいちらん」から3製品を選択でき、「せいひんえー」から個別に呼び出せます。"
      "「ざいこあり」のような分類名も自由に割り当てられます。完全一致のままで利用できます。\n\n"
      "一覧が大きい場合は、分類ごとに読みを割り当ててください。同じ読みの完全一致候補は最大64件です。"),
      QStringLiteral("せいひんいちらん\tABC-1234\nせいひんいちらん\tXYZ-5678\n"
                     "せいひんいちらん\tDEF-9012\nせいひんえー\tABC-1234\nざいこあり\tABC-1234\nざいこあり\tDEF-9012"));
  add_page(QStringLiteral("定型文・コマンド"), QStringLiteral(
      "よく使う定型文、Git コマンド、SQL、単一行のコードスニペットを一覧として登録できます。"
      "例えば「ぎっと」や「めーる」を変換すると、登録した候補から選択できます。\n\n"
      "文字列を入力する機能です。登録したコマンドを自動実行するものではありません。"
      "業務用と個人用の辞書を分けて管理することもできます。"),
      QStringLiteral("ぎっと\tgit status\nぎっと\tgit diff\nぎっと\tgit log --oneline\n"
                     "めーる\tいつもお世話になっております。\nめーる\tご確認いただけますと幸いです。"));
  add_page(QStringLiteral("連携・制限"), QStringLiteral(
      "外部プログラムや業務システムでローカル TSV を生成・更新できます。"
      "Git での管理も可能です。Mozkey が外部システムに直接接続する機能ではありません。\n\n"
      "ファイルの変更は通常約3秒以内に検出されます。読み込みに失敗した場合は最後に正常に読み込んだ内容が保持されます。"
      "一覧の状態はファイルの存在・読み取り可否であり、TSV の解析結果を保証するものではありません。\n\n"
      "上限：32ファイル、1ファイル16 MiB、1ファイルおよび統合辞書それぞれ20万エントリ、"
      "同じ読みにつき64候補、読み256 bytes、候補1024 bytes（UTF-8）、最大16文節。\n\n"
      "外部辞書はユーザー辞書とは独立しています。通常の文章変換に参加させたい語彙はユーザー辞書を使用してください。"
      "外部辞書で新たに追加された候補は学習対象外です。シークレットモード・パスワード入力欄・"
      "プライベート入力では追加されません。ただし入力先アプリや OS による保存は防ぎません。"));
  root->addWidget(tabs, 1);
  auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  close->button(QDialogButtonBox::Close)->setText(QStringLiteral("閉じる"));
  QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  root->addWidget(close);
  dialog.exec();
}
}  // namespace

bool ExternalDictionarySettingsDialog::Edit(
    QWidget* parent, bool* enabled, std::vector<config::ExternalDictionarySourceOptions>* sources) {
  if (enabled == nullptr || sources == nullptr) return false;
  ExternalDictionarySettingsDialog dialog(parent, *enabled, *sources);
  if (dialog.exec() != QDialog::Accepted) return false;
  return dialog.CollectAndValidate(enabled, sources);
}

ExternalDictionarySettingsDialog::ExternalDictionarySettingsDialog(
    QWidget* parent, bool enabled, const std::vector<config::ExternalDictionarySourceOptions>& sources)
    : QDialog(parent), sources_(sources) {
  setWindowTitle(QStringLiteral("外部辞書の設定"));
  setMinimumSize(690, 360);
  resize(860, 480);
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
  table_->setColumnCount(3);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("辞書名"), QStringLiteral("一致方式"),
       QStringLiteral("状態")});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  table_->setAlternatingRowColors(true);
  list_row->addWidget(table_, 1);

  auto* buttons = new QVBoxLayout();
  auto* add = new QPushButton(QStringLiteral("追加…"), this);
  edit_button_ = new QPushButton(QStringLiteral("編集…"), this);
  remove_button_ = new QPushButton(QStringLiteral("削除"), this);
  up_button_ = new QPushButton(QStringLiteral("上へ"), this);
  down_button_ = new QPushButton(QStringLiteral("下へ"), this);
  copy_path_button_ = new QPushButton(QStringLiteral("パスをコピー"), this);
  copy_path_button_->setToolTip(QStringLiteral(
      "選択した辞書ファイルのパスをクリップボードにコピーします。"));
  for (auto* button : {add, edit_button_, remove_button_, up_button_, down_button_,
                       copy_path_button_}) {
    button->setAutoDefault(false);
    button->setMinimumWidth(112);
    buttons->addWidget(button);
  }
  buttons->addStretch(1);
  list_row->addLayout(buttons);
  root->addLayout(list_row, 1);

  auto* path_row = new QHBoxLayout();
  path_row->addWidget(new QLabel(QStringLiteral("ファイルパス"), this));
  selected_path_ = new QLineEdit(this);
  selected_path_->setReadOnly(true);
  selected_path_->setPlaceholderText(QStringLiteral("辞書を選択してください"));
  path_row->addWidget(selected_path_, 1);
  root->addLayout(path_row);

  auto* hint = new QLabel(QStringLiteral(
      "完全一致は上から順に追加し、前方一致は読み順に追加します。重複はまとめます。"
      "\n通常の第1候補は変更しません。登録したファイル自体は変更されません。"), this);
  hint->setWordWrap(true);
  root->addWidget(hint);

  auto* footer = new QDialogButtonBox(QDialogButtonBox::Ok |
                                       QDialogButtonBox::Cancel, this);
  footer->button(QDialogButtonBox::Ok)->setText(QStringLiteral("決定"));
  footer->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("キャンセル"));
  auto* footer_row = new QHBoxLayout();
  auto* help = new QPushButton(QStringLiteral("使い方・活用例…"), this);
  help->setAutoDefault(false);
  footer_row->addWidget(help);
  footer_row->addStretch(1);
  footer_row->addWidget(footer);
  root->addLayout(footer_row);
  connect(help, &QPushButton::clicked, this, [this] { ShowHelp(this); });

  connect(add, &QPushButton::clicked, this, [this] { AddFiles(); });
  connect(edit_button_, &QPushButton::clicked, this, [this] { EditSelected(); });
  connect(table_, &QTableWidget::cellDoubleClicked, this,
          [this](int, int) { EditSelected(); });
  connect(remove_button_, &QPushButton::clicked, this, [this] { RemoveSelected(); });
  connect(up_button_, &QPushButton::clicked, this, [this] { MoveSelected(-1); });
  connect(down_button_, &QPushButton::clicked, this, [this] { MoveSelected(1); });
  connect(copy_path_button_, &QPushButton::clicked, this,
          [this] { CopySelectedPath(); });
  connect(table_, &QTableWidget::itemSelectionChanged,
          this, [this] { UpdateSelectionActions(); });
  connect(footer, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(footer, &QDialogButtonBox::accepted, this, [this] {
    if (enabled_checkbox_->isChecked() && sources_.empty()) {
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
  const QSignalBlocker blocker(table_);
  table_->setRowCount(static_cast<int>(sources_.size()));
  for (size_t i = 0; i < sources_.size(); ++i) {
    const auto& source = sources_[i];
    const QString path = FromUtf8(source.path());
    const QString name = source.display_name().empty()
                             ? QFileInfo(path).fileName()
                             : FromUtf8(source.display_name());
    auto* item = new QTableWidgetItem(name.isEmpty() ? path : name);
    item->setToolTip(path);
    table_->setItem(static_cast<int>(i), 0, item);
    table_->setItem(static_cast<int>(i), 1, new QTableWidgetItem(
        source.match_mode() == SourceOptions::EXACT_AND_PREFIX
            ? QStringLiteral("完全一致＋前方一致") : QStringLiteral("完全一致")));
    const QFileInfo info(path);
    const QString state = info.isFile() && info.isReadable()
        ? QStringLiteral("ファイルあり") : QStringLiteral("未検出・読込不可");
    table_->setItem(static_cast<int>(i), 2, new QTableWidgetItem(state));
  }
  if (!sources_.empty()) {
    table_->selectRow(std::max(0, std::min(previous,
                              static_cast<int>(sources_.size()) - 1)));
  }
  UpdateSelectionActions();
}

void ExternalDictionarySettingsDialog::UpdateSelectionActions() {
  const int row = table_->currentRow();
  const bool selected = row >= 0 && row < static_cast<int>(sources_.size());
  edit_button_->setEnabled(selected);
  selected_path_->setText(selected ? FromUtf8(sources_[row].path()) : QString());
  selected_path_->setCursorPosition(0);
  remove_button_->setEnabled(selected);
  copy_path_button_->setEnabled(selected);
  up_button_->setEnabled(selected && row > 0);
  down_button_->setEnabled(selected && row + 1 < static_cast<int>(sources_.size()));
}

void ExternalDictionarySettingsDialog::AddFiles() {
  const QStringList files = QFileDialog::getOpenFileNames(
      this, QStringLiteral("外部 TSV 辞書を追加"), QString(),
      QStringLiteral("TSV ファイル (*.tsv);;すべてのファイル (*)"));
  for (const QString& file : files) {
    const std::string path = ToUtf8(QFileInfo(file).absoluteFilePath());
    if (std::any_of(sources_.begin(), sources_.end(), [&path](const auto& source) {
          return source.path() == path;
        })) continue;
    if (sources_.size() >= kMaxSources) {
      QMessageBox::warning(this, windowTitle(),
                           QStringLiteral("登録できるファイルは最大32個です。"));
      break;
    }
    sources_.emplace_back().set_path(path);
  }
  RefreshRows();
}

void ExternalDictionarySettingsDialog::EditSelected() {
  const int row = table_->currentRow();
  if (row < 0 || row >= static_cast<int>(sources_.size())) return;
  auto& source = sources_[row];
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("辞書の設定"));
  dialog.setMinimumWidth(460);
  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(QStringLiteral("辞書名（任意）"), &dialog));
  auto* name = new QLineEdit(FromUtf8(source.display_name()), &dialog);
  name->setPlaceholderText(QFileInfo(FromUtf8(source.path())).fileName());
  layout->addWidget(name);
  layout->addWidget(new QLabel(QStringLiteral("一致方式"), &dialog));
  auto* mode = new QComboBox(&dialog);
  mode->addItem(QStringLiteral("完全一致"));
  mode->addItem(QStringLiteral("完全一致＋前方一致"));
  mode->setCurrentIndex(source.match_mode() == SourceOptions::EXACT_AND_PREFIX ? 1 : 0);
  layout->addWidget(mode);
  auto* hint = new QLabel(QStringLiteral(
      "完全一致では登録された読み全体と一致した場合だけ候補が出ます。"
      "\n前方一致では3文字以上の入力から、登録語の先頭が一致する候補も追加します。"), &dialog);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                       QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("決定"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("キャンセル"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  source.set_display_name(ToUtf8(name->text().trimmed()));
  source.set_match_mode(mode->currentIndex() == 1
                            ? SourceOptions::EXACT_AND_PREFIX : SourceOptions::EXACT);
  RefreshRows();
}

void ExternalDictionarySettingsDialog::RemoveSelected() {
  const int row = table_->currentRow();
  if (row < 0 || row >= static_cast<int>(sources_.size())) return;
  sources_.erase(sources_.begin() + row);
  RefreshRows();
}

void ExternalDictionarySettingsDialog::MoveSelected(int direction) {
  const int row = table_->currentRow();
  const int target = row + direction;
  if (row < 0 || target < 0 || target >= static_cast<int>(sources_.size())) return;
  std::swap(sources_[row], sources_[target]);
  RefreshRows();
  table_->selectRow(target);
}

void ExternalDictionarySettingsDialog::CopySelectedPath() {
  const int row = table_->currentRow();
  if (row < 0 || row >= static_cast<int>(sources_.size())) return;
  QGuiApplication::clipboard()->setText(FromUtf8(sources_[row].path()));
}

bool ExternalDictionarySettingsDialog::CollectAndValidate(
    bool* enabled, std::vector<config::ExternalDictionarySourceOptions>* sources) {
  if (enabled == nullptr || sources == nullptr) return false;
  *enabled = enabled_checkbox_->isChecked();
  *sources = sources_;
  return true;
}
}  // namespace mozc::gui
