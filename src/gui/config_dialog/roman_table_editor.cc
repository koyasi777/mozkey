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

#include "gui/config_dialog/roman_table_editor.h"

#include <QEvent>
#include <QDir>
#include <QKeyEvent>
#include <QIODevice>
#include <QFile>
#include <QDialog>
#include <QCoreApplication>
#include <QHash>
#include <QVector>
#include <QBrush>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QComboBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QAbstractItemModel>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHelpEvent>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QToolTip>
#include <QtGui>
#include <istream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_split.h"
#include "base/config_file_stream.h"
#include "config/roman_table_presets.h"
#include "gui/config_dialog/roman_table_rules.h"
#include "base/util.h"
#include "base/vlog.h"
#include "gui/base/table_util.h"
#include "gui/base/util.h"
#include "gui/config_dialog/generic_table_editor.h"

namespace mozc {
namespace gui {
namespace {
enum {
  NEW_INDEX = 0,
  INSERT_ABOVE_INDEX = 1,
  INSERT_BELOW_INDEX = 2,
  REMOVE_INDEX = 3,
  IMPORT_FROM_FILE_INDEX = 4,
  EXPORT_TO_FILE_INDEX = 5,
  RESET_INDEX = 6,
  MENU_SIZE = 7
};
constexpr char kRomanTableFile[] = "system://romanji-hiragana.tsv";
constexpr char kDisplayAmbiguousResult[] = "DisplayAmbiguousResult";

bool SplitDisplayAmbiguousResultAttribute(const std::string &attributes,
                                          std::string *remaining_attributes) {
  CHECK(remaining_attributes);
  remaining_attributes->clear();

  bool found = false;
  for (absl::string_view token : absl::StrSplit(attributes, ' ', absl::SkipEmpty())) {
    if (token == kDisplayAmbiguousResult) {
      found = true;
      continue;
    }
    if (!remaining_attributes->empty()) {
      remaining_attributes->push_back(' ');
    }
    remaining_attributes->append(token.data(), token.size());
  }

  return found;
}

std::string BuildAttributes(const std::string &remaining_attributes,
                            bool display_ambiguous_result) {
  std::string attributes = remaining_attributes;
  if (display_ambiguous_result) {
    if (!attributes.empty()) {
      attributes.push_back(' ');
    }
    attributes += kDisplayAmbiguousResult;
  }
  return attributes;
}

QTableWidgetItem *CreateDisplayAmbiguousResultItem(
    bool checked, const std::string &remaining_attributes) {
  auto *item = new QTableWidgetItem();
  item->setFlags((item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled |
                  Qt::ItemIsSelectable) &
                  ~Qt::ItemIsEditable);
  item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
  item->setData(Qt::UserRole, QString::fromStdString(remaining_attributes));
  item->setToolTip(QObject::tr(
    "まだ入力途中でも、この行の出力を先に表示します。"
    "続きの入力で別の規則に一致した場合は表示が更新されます。"));
  return item;
}

std::vector<roman_table_rules::Rule> CollectEditorRules(
    QTableWidget *widget) {
  std::vector<roman_table_rules::Rule> rules;
  rules.reserve(widget->rowCount());
  for (int i = 0; i < widget->rowCount(); ++i) {
    const QTableWidgetItem *attribute_item = widget->item(i, 3);
    const bool show_partial = attribute_item != nullptr &&
                              attribute_item->checkState() == Qt::Checked;
    const std::string other_attributes = attribute_item == nullptr
        ? std::string()
        : attribute_item->data(Qt::UserRole).toString().toStdString();
    rules.push_back({
        TableUtil::SafeGetItemText(widget, i, 0).toStdString(),
        TableUtil::SafeGetItemText(widget, i, 1).toStdString(),
        TableUtil::SafeGetItemText(widget, i, 2).toStdString(),
        BuildAttributes(other_attributes, show_partial),
    });
  }
  return rules;
}
}  // namespace

RomanTableEditorDialog::RomanTableEditorDialog(QWidget *parent)
    : GenericTableEditorDialog(parent, 4),
      actions_(MENU_SIZE),
      default_table_requested_(false),
      loading_table_(false) {
  actions_[NEW_INDEX] = mutable_edit_menu()->addAction(tr("New entry"));
  actions_[INSERT_ABOVE_INDEX] =
      mutable_edit_menu()->addAction(tr("Insert entry above"));
  actions_[INSERT_BELOW_INDEX] =
      mutable_edit_menu()->addAction(tr("Insert entry below"));
  actions_[REMOVE_INDEX] =
      mutable_edit_menu()->addAction(tr("Remove selected entries"));
  mutable_edit_menu()->addSeparator();
  actions_[IMPORT_FROM_FILE_INDEX] =
      mutable_edit_menu()->addAction(tr("Import from file..."));
  actions_[EXPORT_TO_FILE_INDEX] =
      mutable_edit_menu()->addAction(tr("Export to file..."));
  mutable_edit_menu()->addSeparator();

  actions_[RESET_INDEX] =
      mutable_edit_menu()->addAction(tr("Reset to defaults"));
  // Standard is selectable at the top. Do not duplicate the same action here.
  actions_[RESET_INDEX]->setVisible(false);
  actions_[IMPORT_FROM_FILE_INDEX]->setText(tr("Import to current table..."));
  actions_[EXPORT_TO_FILE_INDEX]->setText(tr("Export current table..."));

  setWindowTitle(tr("[ProductName] Romaji table editor"));
  GuiUtil::ReplaceWidgetLabels(this);
  dialog_title_ = GuiUtil::ReplaceString(tr("[ProductName] settings"));
  CHECK(mutable_table_widget());
  CHECK_EQ(mutable_table_widget()->columnCount(), 4);
  QStringList headers;
  headers << tr("Input") << tr("Output") << tr("Next input")
          << tr("途中一致でも表示");
  mutable_table_widget()->setHorizontalHeaderLabels(headers);

  // Romaji table is an ordered list of conversion rules.  Sorting changes the
  // serialized rule order and can change actual input behavior.
  mutable_table_widget()->setSortingEnabled(false);
  mutable_table_widget()->horizontalHeader()->setSortIndicatorShown(false);

  QObject::connect(mutable_table_widget(), SIGNAL(itemChanged(QTableWidgetItem *)),
                   this, SLOT(MarkRomanTableEdited(QTableWidgetItem *)));
  QObject::connect(mutable_table_widget(), &QTableWidget::itemSelectionChanged,
                   this, &RomanTableEditorDialog::UpdateMenuStatus);

  mutable_table_widget()->horizontalHeader()->viewport()->installEventFilter(this);

  // Source rules remain in file order: do not enable sorting here.
  auto *table_widget = mutable_table_widget();
  auto *header = table_widget->horizontalHeader();
  header->setStretchLastSection(true);
  header->setSectionResizeMode(QHeaderView::Interactive);
  header->setSectionsMovable(false);
  header->setMinimumHeight(32);
  table_widget->setColumnWidth(0, 175);
  table_widget->setColumnWidth(1, 185);
  table_widget->setColumnWidth(2, 160);
  table_widget->setColumnWidth(3, 155);
  table_widget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  table_widget->verticalHeader()->setDefaultSectionSize(
      qMax(29, table_widget->verticalHeader()->defaultSectionSize()));
  table_widget->setToolTip(tr("Double-click a cell to edit its rule."));
  table_widget->horizontalHeaderItem(3)->setText(tr("Partial match"));
  table_widget->horizontalHeaderItem(3)->setToolTip(
      tr("Show a candidate before all input is completed."));
  if (auto *buttons = findChild<QDialogButtonBox *>(
          QStringLiteral("editorButtonBox"))) {
    if (auto *cancel = buttons->button(QDialogButtonBox::Cancel)) {
      cancel->setText(tr("Cancel"));
    }
  }

  auto *root = qobject_cast<QVBoxLayout *>(layout());
  CHECK(root);
  auto *heading = new QLabel(tr("Romaji table rules"), this);
  QFont heading_font = heading->font();
  heading_font.setBold(true);
  heading_font.setPointSize(qMax(heading_font.pointSize() + 1, 11));
  heading->setFont(heading_font);
  root->insertWidget(0, heading);
  auto *mode_row = new QWidget(this);
  auto *mode_layout = new QHBoxLayout(mode_row);
  mode_layout->setContentsMargins(0, 0, 0, 0);
  mode_layout->setSpacing(10);
  auto *mode_label = new QLabel(tr("Input method:"), mode_row);
  preset_selector_ = new QComboBox(mode_row);
  // The explanation lives in the same row as the selection control.
  // Allocate the available width to the explanation and wrap as needed.
  preset_selector_->setMinimumWidth(260);
  preset_selector_->setMaximumWidth(350);
  preset_selector_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  mode_label->setBuddy(preset_selector_);
  mode_layout->addWidget(mode_label);
  mode_layout->addWidget(preset_selector_, 0);
  preset_state_label_ = new QLabel(mode_row);
  preset_state_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  preset_state_label_->setWordWrap(true);
  preset_state_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  mode_layout->addWidget(preset_state_label_, 1);
  root->insertWidget(1, mode_row);
  QObject::connect(preset_selector_,
                   QOverload<int>::of(&QComboBox::currentIndexChanged),
                   this, &RomanTableEditorDialog::SelectPresetInEditor);

  auto *actions = new QWidget(this);
  auto *action_row = new QHBoxLayout(actions);
  action_row->setContentsMargins(0, 4, 0, 8);
  save_preset_button_ = new QPushButton(tr("Save as new preset..."), actions);
  overwrite_preset_button_ = new QPushButton(tr("Overwrite preset..."), actions);
  overwrite_preset_button_->setToolTip(tr("Overwrite the selected personal "
      "preset with the rules currently displayed in the editor."));
  manage_preset_button_ = new QPushButton(tr("Manage presets..."), actions);
  auto *licenses_button = new QPushButton(tr("Origins and licenses..."), actions);
  action_row->addWidget(save_preset_button_);
  action_row->addWidget(overwrite_preset_button_);
  action_row->addWidget(manage_preset_button_);
  action_row->addStretch(1);
  action_row->addWidget(licenses_button);
  root->insertWidget(2, actions);
  // Search navigates the original rows. It never sorts, filters or mutates
  // rules, so inserting below a selected match has an unambiguous position.
  auto *search_row = new QWidget(this);
  auto *search_layout = new QHBoxLayout(search_row);
  search_layout->setContentsMargins(0, 0, 0, 4);
  search_layout->setSpacing(8);
  auto *search_label = new QLabel(tr("Search:"), search_row);
  auto *search_input = new QLineEdit(search_row);
  search_input_ = search_input;
  search_input_->installEventFilter(this);
  search_input->setObjectName(QStringLiteral("romanTableSearchInput"));
  search_input->setPlaceholderText(tr("Find an input key or output..."));
  search_input->setClearButtonEnabled(true);
  search_input->setMinimumWidth(140);
  search_label->setBuddy(search_input);
  auto *search_scope = new QComboBox(search_row);
  search_scope->setObjectName(QStringLiteral("romanTableSearchScope"));
  search_scope->addItem(tr("Input / Output"));
  search_scope->addItem(tr("Input only"));
  search_scope->addItem(tr("Output only"));
  auto *search_previous = new QPushButton(tr("Previous"), search_row);
  auto *search_next = new QPushButton(tr("Next"), search_row);
  search_next_button_ = search_next;
  auto *search_status = new QLabel(search_row);
  search_status->setObjectName(QStringLiteral("romanTableSearchStatus"));
  search_status->setMinimumWidth(100);
  search_layout->addWidget(search_label);
  search_layout->addWidget(search_input, 1);
  search_layout->addWidget(search_scope);
  search_layout->addWidget(search_previous);
  search_layout->addWidget(search_next);
  search_layout->addWidget(search_status);
  root->insertWidget(3, search_row);

  // Search is a navigation aid, not a row styling or data-edit operation.
  // Outline only the current result, across all columns. Other matches keep
  // their ordinary alternating-row appearance.
  class SearchOutlineDelegate final : public QStyledItemDelegate {
   public:
    SearchOutlineDelegate(QTableWidget *table, QLineEdit *input,
                          QComboBox *scope)
        : QStyledItemDelegate(table), table_(table), input_(input),
          scope_(scope) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
      const QString needle = input_->text();
      if (needle.isEmpty() || index.row() != table_->currentRow()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
      }
      const int scope = scope_->currentIndex();
      const bool input_match = scope != 2 &&
          TableUtil::SafeGetItemText(table_, index.row(), 0).contains(
              needle, Qt::CaseInsensitive);
      const bool output_match = scope != 1 &&
          TableUtil::SafeGetItemText(table_, index.row(), 1).contains(
              needle, Qt::CaseInsensitive);
      if (!input_match && !output_match) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
      }
      // The input cell remains selected for Edit/Remove semantics, but its
      // ordinary selection fill is not painted while the search outline is
      // visible. This retains the user's preferred uncluttered appearance.
      QStyleOptionViewItem plain(option);
      plain.state &= ~QStyle::State_Selected;
      QStyledItemDelegate::paint(painter, plain, index);

      QColor accent = option.palette.color(QPalette::Highlight);
      if (option.palette.color(QPalette::Base).lightness() < 128) {
        accent = accent.lighter(200);  // Visible on dark themes too.
      }
      painter->save();
      QPen pen(accent);
      pen.setWidth(2);
      pen.setCosmetic(true);
      painter->setPen(pen);
      const QRect r = option.rect;
      // Horizontal borders from every cell join into a whole-row frame.
      painter->drawLine(r.left(), r.top() + 1, r.right(), r.top() + 1);
      painter->drawLine(r.left(), r.bottom() - 1, r.right(), r.bottom() - 1);
      if (index.column() == 0) {
        painter->drawLine(r.left() + 1, r.top() + 1,
                          r.left() + 1, r.bottom() - 1);
      }
      if (index.column() == table_->columnCount() - 1) {
        painter->drawLine(r.right() - 1, r.top() + 1,
                          r.right() - 1, r.bottom() - 1);
      }
      painter->restore();
    }

   private:
    QTableWidget *table_;
    QLineEdit *input_;
    QComboBox *scope_;
  };
  table_widget->setItemDelegate(
      new SearchOutlineDelegate(table_widget, search_input, search_scope));

  // Only input and output columns are inspected. The user can see every
  // untouched row and its original neighbours at all times.
  const auto find_search_matches =
      [table_widget, search_input, search_scope]() -> QVector<int> {
    QVector<int> matched;
    const QString needle = search_input->text();
    if (needle.isEmpty()) return matched;
    const int scope = search_scope->currentIndex();
    for (int row = 0; row < table_widget->rowCount(); ++row) {
      const bool input_matches = scope != 2 &&
          TableUtil::SafeGetItemText(table_widget, row, 0).contains(
              needle, Qt::CaseInsensitive);
      const bool output_matches = scope != 1 &&
          TableUtil::SafeGetItemText(table_widget, row, 1).contains(
              needle, Qt::CaseInsensitive);
      if (input_matches || output_matches) matched.push_back(row);
    }
    return matched;
  };
  const auto select_search_row = [table_widget](int row) {
    // The input cell must be genuinely selected for Remove selected entries.
    // SearchOutlineDelegate hides only its selection fill, not its semantics.
    table_widget->setCurrentCell(row, 0,
                                 QItemSelectionModel::ClearAndSelect);
    if (QTableWidgetItem *item = table_widget->item(row, 0)) {
      table_widget->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }
  };
  const auto update_search_status =
      [this, search_input, search_status, search_previous, search_next,
       table_widget, find_search_matches]() {
    const QVector<int> matches = find_search_matches();
    const bool have_matches = !matches.isEmpty();
    search_previous->setEnabled(have_matches);
    search_next->setEnabled(have_matches);
    if (search_input->text().isEmpty()) {
      search_status->setText(tr("Search does not reorder rules"));
    } else if (!have_matches) {
      search_status->setText(tr("No matches"));
    } else {
      const int index = matches.indexOf(table_widget->currentRow());
      search_status->setText(index < 0
          ? tr("%1 matches").arg(matches.size())
          : tr("Match %1 of %2").arg(index + 1).arg(matches.size()));
    }
  };
  const auto select_first_search_match =
      [find_search_matches, select_search_row, update_search_status]() {
    const QVector<int> matches = find_search_matches();
    if (!matches.isEmpty()) select_search_row(matches.front());
    update_search_status();
  };
  const auto move_search_match =
      [table_widget, find_search_matches, select_search_row,
       update_search_status](int delta) {
    const QVector<int> matches = find_search_matches();
    if (matches.isEmpty()) {
      update_search_status();
      return;
    }
    const int selected_index = matches.indexOf(table_widget->currentRow());
    int target_index = delta > 0 ? 0 : matches.size() - 1;
    if (selected_index >= 0) {
      target_index = (selected_index + delta + matches.size()) % matches.size();
    }
    select_search_row(matches.at(target_index));
    update_search_status();
  };
  QObject::connect(search_input, &QLineEdit::textChanged, this,
                   [select_first_search_match, table_widget](const QString &) {
                     select_first_search_match();
                     table_widget->viewport()->update();
                   });
  QObject::connect(search_scope,
                   QOverload<int>::of(&QComboBox::currentIndexChanged),
                   this, [select_first_search_match, table_widget](int) {
                     select_first_search_match();
                     table_widget->viewport()->update();
                   });
  QObject::connect(search_previous, &QPushButton::clicked, this,
                   [move_search_match]() { move_search_match(-1); });
  QObject::connect(search_next, &QPushButton::clicked, this,
                   [move_search_match]() { move_search_match(1); });
  QObject::connect(table_widget, &QTableWidget::currentCellChanged, this,
                   [update_search_status, table_widget](int, int, int, int) {
                     update_search_status();
                     table_widget->viewport()->update();
                   });
  QObject::connect(table_widget, &QTableWidget::itemChanged, this,
                   [update_search_status](QTableWidgetItem *) {
                     update_search_status();
                   });
  // Inserting/removing a row returns to normal editing state. The existing
  // insertion code retains responsibility for selecting the new row.
  QObject::connect(table_widget->model(), &QAbstractItemModel::rowsInserted,
                   this, [search_input](const QModelIndex &, int, int) {
                     search_input->clear();
                   });
  QObject::connect(table_widget->model(), &QAbstractItemModel::rowsRemoved,
                   this, [search_input](const QModelIndex &, int, int) {
                     search_input->clear();
                   });
  update_search_status();

  QObject::connect(save_preset_button_, &QPushButton::clicked,
                   this, &RomanTableEditorDialog::SaveCurrentAsPreset);
  QObject::connect(overwrite_preset_button_, &QPushButton::clicked,
                   this, &RomanTableEditorDialog::OverwriteCurrentPersonalPreset);
  QObject::connect(manage_preset_button_, &QPushButton::clicked,
                   this, &RomanTableEditorDialog::ManagePersonalPresets);
  QObject::connect(licenses_button, &QPushButton::clicked, this,
                   [this]() { ShowPresetLicenses(this); });
  const auto on_structure_changed = [this]() {
    if (loading_table_) return;
    default_table_requested_ = false;
    if (preset_id_.rfind("user:", 0) != 0) {
      preset_id_.clear();
      preset_snapshot_.clear();
    }
    RefreshPresetState();
  };
  QObject::connect(table_widget->model(), &QAbstractItemModel::rowsRemoved,
                   this, on_structure_changed);
  QObject::connect(table_widget->model(), &QAbstractItemModel::rowsInserted,
                   this, on_structure_changed);
  RefreshPresetState();
  setMinimumSize(700, 450);
  resize(860, 600);
  UpdateMenuStatus();
}

std::string RomanTableEditorDialog::GetDefaultRomanTable() {
  std::unique_ptr<std::istream> ifs(
      ConfigFileStream::LegacyOpen(kRomanTableFile));
  CHECK(ifs.get() != nullptr);  // should never happen
  std::string line, result;
  while (std::getline(*ifs, line)) {
    if (line.empty()) {
      continue;
    }
    Util::ChopReturns(&line);
    std::vector<std::string> fields =
        absl::StrSplit(line, '\t', absl::AllowEmpty());
    if (fields.size() < 2) {
      MOZC_VLOG(3) << "field size < 2";
      continue;
    }
    result += fields[0];
    result += '\t';
    result += fields[1];
    if (fields.size() >= 3) {
      result += '\t';
      result += fields[2];
    }
    if (fields.size() >= 4) {
      result += '\t';
      result += fields[3];
    }
    result += '\n';
  }
  return result;
}

bool RomanTableEditorDialog::LoadFromStream(std::istream *is) {
  CHECK(is);
  // Validate all rows *before* modifying the QTableWidget. A malformed or
  // overlong import leaves the previously edited table completely intact.
  const roman_table_rules::ParseResult parsed =
      roman_table_rules::Parse(*is, max_entry_size());
  if (!parsed.ok()) {
    QString message;
    switch (parsed.error) {
      case roman_table_rules::ParseError::kInvalidRule:
        message = tr("Invalid roman table rule on line %1. The table was not changed.")
                      .arg(static_cast<qulonglong>(parsed.error_line));
        break;
      case roman_table_rules::ParseError::kTooManyRules:
        message = tr("You can't have more than %1 entries")
                      .arg(max_entry_size());
        break;
      case roman_table_rules::ParseError::kNoRules:
        message = tr("Roman table has no valid rules. The table was not changed.");
        break;
      case roman_table_rules::ParseError::kIoError:
        message = tr("Could not read roman table file.");
        break;
      case roman_table_rules::ParseError::kNone:
        break;
    }
    QMessageBox::warning(this, dialog_title_, message);
    return false;
  }

  const bool was_loading_table = loading_table_;
  loading_table_ = true;
  QTableWidget *widget = mutable_table_widget();
  widget->setRowCount(0);
  widget->verticalHeader()->hide();
  for (int row = 0; row < static_cast<int>(parsed.rules.size()); ++row) {
    const roman_table_rules::Rule &rule = parsed.rules[row];
    std::string remaining_attributes;
    const bool display_ambiguous_result =
        SplitDisplayAmbiguousResultAttribute(rule.attributes,
                                             &remaining_attributes);
    widget->insertRow(row);
    widget->setItem(row, 0,
        new QTableWidgetItem(QString::fromStdString(rule.input)));
    widget->setItem(row, 1,
        new QTableWidgetItem(QString::fromStdString(rule.output)));
    widget->setItem(row, 2,
        new QTableWidgetItem(QString::fromStdString(rule.pending)));
    widget->setItem(row, 3, CreateDisplayAmbiguousResultItem(
        display_ambiguous_result, remaining_attributes));
  }
  loading_table_ = was_loading_table;
  if (importing_table_) import_loaded_ = true;
  UpdateMenuStatus();
  return true;
}

bool RomanTableEditorDialog::LoadDefaultRomanTable() {
  std::unique_ptr<std::istream> ifs(
      ConfigFileStream::LegacyOpen(kRomanTableFile));
  CHECK(ifs);  // should never happen
  CHECK(LoadFromStream(ifs.get()));
  default_table_requested_ = true;
  preset_id_.clear();
  preset_snapshot_.clear();
  RefreshPresetState();
  return true;
}

bool RomanTableEditorDialog::Update() {
  QTableWidget *widget = mutable_table_widget();
  const std::vector<roman_table_rules::Rule> rules = CollectEditorRules(widget);
  if (rules.empty()) {
    QMessageBox::warning(this, dialog_title_,
                         tr("Romaji to Kana table is empty."));
    return false;
  }
  // Never silently discard unfinished rows or overwrite the last good
  // serialized snapshot with an empty or partial table.
  for (int row = 0; row < static_cast<int>(rules.size()); ++row) {
    if (!roman_table_rules::IsValidRule(rules[row])) {
      widget->setCurrentCell(row, 0);
      if (QTableWidgetItem *item = widget->item(row, 0)) {
        widget->scrollToItem(item);
      }
      QMessageBox::warning(this, dialog_title_,
          tr("Rule on row %1 must have an input and either an output or next input.")
              .arg(row + 1));
      return false;
    }
  }
  bool contains_capital = false;
  for (const roman_table_rules::Rule &rule : rules) {
    std::string lower = rule.input;
    Util::LowerString(&lower);
    if (lower != rule.input) {
      contains_capital = true;
      break;
    }
  }
  *mutable_table() = roman_table_rules::Serialize(rules);
  if (contains_capital) {
    QMessageBox::information(this, dialog_title_,
                             tr("Input fields contain capital characters. "
                                "\"Shift-mode-switch\" function is disabled "
                                "with this new mapping."));
  }
  return true;
}

void RomanTableEditorDialog::UpdateMenuStatus() {
  QTableWidget *table = mutable_table_widget();
  const bool has_rows = table->rowCount() > 0;
  const bool can_insert =
      static_cast<size_t>(table->rowCount()) < max_entry_size();
  const bool has_current = table->currentItem() != nullptr;
  actions_[NEW_INDEX]->setEnabled(can_insert);
  actions_[INSERT_ABOVE_INDEX]->setEnabled(has_current && can_insert);
  actions_[INSERT_BELOW_INDEX]->setEnabled(has_current && can_insert);
  actions_[RESET_INDEX]->setEnabled(has_rows);
  actions_[REMOVE_INDEX]->setEnabled(!table->selectedItems().isEmpty());
  UpdateOKButton(has_rows);
}

bool RomanTableEditorDialog::InsertRomanTableEntry(int row) {
  QTableWidget *table = mutable_table_widget();
  if (static_cast<size_t>(table->rowCount()) >= max_entry_size()) {
    QMessageBox::warning(
        this, dialog_title_,
        tr("You can't have more than %1 entries").arg(max_entry_size()));
    return false;
  }
  if (row < 0 || row > table->rowCount()) {
    return false;
  }

  // Keep all existing rows in their original order.  The fourth column is a
  // checkable item with hidden rule attributes, not an ordinary text cell.
  InsertEmptyItem(row);
  delete table->takeItem(row, 3);
  table->setItem(row, 3,
                 CreateDisplayAmbiguousResultItem(false, std::string()));
  default_table_requested_ = false;
  return true;
}

void RomanTableEditorDialog::OnContextMenuRequested(const QPoint &pos) {
  QTableWidget *table = mutable_table_widget();
  QTableWidgetItem *item = table->itemAt(pos);
  if (item == nullptr) {
    return;
  }
  const int row = item->row();
  // Use the row that was right-clicked, not a previously selected row.
  // Preserve multi-selection when clicking one of its selected cells.
  if (item->isSelected()) {
    table->setCurrentItem(item, QItemSelectionModel::NoUpdate);
  } else {
    table->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
  }

  QMenu menu(this);
  QAction *edit_action = nullptr;
  if (table->selectedItems().size() == 1) {
    edit_action =
        menu.addAction(GenericTableEditorDialog::tr("Edit entry"));
  }
  QAction *new_action = menu.addAction(tr("New entry"));
  QAction *above_action = menu.addAction(tr("Insert entry above"));
  QAction *below_action = menu.addAction(tr("Insert entry below"));
  QAction *remove_action =
      menu.addAction(GenericTableEditorDialog::tr("Remove entry"));
  const bool can_insert =
      static_cast<size_t>(table->rowCount()) < max_entry_size();
  new_action->setEnabled(can_insert);
  above_action->setEnabled(can_insert);
  below_action->setEnabled(can_insert);

  QAction *selected_action = menu.exec(table->viewport()->mapToGlobal(pos));
  if (edit_action != nullptr && selected_action == edit_action) {
    table->editItem(item);
  } else if (selected_action == new_action) {
    InsertRomanTableEntry(table->rowCount());
  } else if (selected_action == above_action) {
    InsertRomanTableEntry(row);
  } else if (selected_action == below_action) {
    InsertRomanTableEntry(row + 1);
  } else if (selected_action == remove_action) {
    default_table_requested_ = false;
    DeleteSelectedItems();
  }
}

void RomanTableEditorDialog::OnEditMenuAction(QAction *action) {
  if (action == actions_[NEW_INDEX]) {
    InsertRomanTableEntry(mutable_table_widget()->rowCount());
  } else if (action == actions_[INSERT_ABOVE_INDEX] ||
             action == actions_[INSERT_BELOW_INDEX]) {
    const int row = mutable_table_widget()->currentRow();
    if (row >= 0) {
      InsertRomanTableEntry(
          row + (action == actions_[INSERT_BELOW_INDEX] ? 1 : 0));
    }
  } else if (action == actions_[REMOVE_INDEX]) {
    default_table_requested_ = false;
    DeleteSelectedItems();
  } else if (action == actions_[IMPORT_FROM_FILE_INDEX] ||
             action == actions_[RESET_INDEX]) {  // import or reset
    if (mutable_table_widget()->rowCount() > 0 &&
        QMessageBox::Ok !=
            QMessageBox::question(
                this, dialog_title_,
                tr("Do you want to overwrite the current roman table?"),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel)) {
      return;
    }

    if (action == actions_[IMPORT_FROM_FILE_INDEX]) {
      importing_table_ = true;
      import_loaded_ = false;
      ImportCurrentRomanTable();
      importing_table_ = false;
      if (import_loaded_) {
        default_table_requested_ = false;
        preset_id_.clear();
        RefreshPresetState();
      }
    } else if (action == actions_[RESET_INDEX]) {
      LoadDefaultRomanTable();
    }
  } else if (action == actions_[EXPORT_TO_FILE_INDEX]) {
    Export();
  }
}


void RomanTableEditorDialog::ImportCurrentRomanTable() {
  const QString filename = QFileDialog::getOpenFileName(
      this, tr("Import to current table..."), QDir::homePath());
  if (filename.isEmpty()) return;
  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, dialog_title_,
                         tr("Could not read roman table file."));
    return;
  }
  if (file.size() >= 100 * 1024) {
    QMessageBox::warning(this, dialog_title_,
                         tr("Import file is too large."));
    return;
  }
  const QByteArray bytes = file.readAll();
  if (file.error() != QFile::NoError) {
    QMessageBox::warning(this, dialog_title_,
                         tr("Could not read roman table file."));
    return;
  }
  if (bytes.contains('\0') || QString::fromUtf8(bytes).toUtf8() != bytes) {
    QMessageBox::warning(this, dialog_title_,
                         tr("Invalid roman table file encoding."));
    return;
  }
  std::istringstream stream(std::string(bytes.constData(), bytes.size()));
  LoadFromStream(&stream);  // Detailed parse error is displayed once.
}

void RomanTableEditorDialog::MarkRomanTableEdited(QTableWidgetItem *item) {
  (void)item;
  if (loading_table_) {
    return;
  }
  default_table_requested_ = false;
  if (preset_id_.rfind("user:", 0) != 0) {
    preset_id_.clear();
    preset_snapshot_.clear();
  }
  RefreshPresetState();
}

bool RomanTableEditorDialog::eventFilter(QObject *obj, QEvent *event) {
  if (obj == search_input_ && event->type() == QEvent::KeyPress) {
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      // Prevent the dialog's default OK button from accepting the window.
      // Enter is an explicit search-next action only while this field is
      // focused. A disabled Next button means a harmless no-op.
      if (search_next_button_ != nullptr && search_next_button_->isEnabled()) {
        search_next_button_->click();
      }
      return true;
    }
  }
  if (obj == mutable_table_widget()->horizontalHeader()->viewport() &&
      event->type() == QEvent::ToolTip) {
    auto *help_event = static_cast<QHelpEvent *>(event);
    QHeaderView *header = mutable_table_widget()->horizontalHeader();
    const int logical_index = header->logicalIndexAt(help_event->pos());

    if (logical_index == 3) {
      QToolTip::showText(
          help_event->globalPos(),
          tr("長い規則の途中一致でも、この行の出力を未変換表示に出します。"
             "続きの入力で長い規則に一致した場合は表示が更新されます。"),
          header);
      return true;
    }
  }

  return GenericTableEditorDialog::eventFilter(obj, event);
}

// static
void RomanTableEditorDialog::ShowPresetLicenses(QWidget *parent) {

    struct Notice {
      const char *name;
      const char *resource;
      const char *url;
    };
    constexpr Notice kNotices[] = {
        {"Simplified-AZIK", ":/roman_table_presets/simplified_azik.LICENSE",
         "https://github.com/yuru7/Simplified-AZIK"},
        {"新月配列", ":/roman_table_presets/shingetsu.LICENSE",
         "https://github.com/nagamine-git/shingetsu-layout"},
        {"せおと配列 / せおと配列 Plus", ":/roman_table_presets/seoto.LICENSE",
         "https://github.com/koyasi777/seoto-romaji-table"},
        {"せおと配列 / せおと配列 Plus (Mozc-derived rules)",
         ":/roman_table_presets/seoto.BSD-LICENSE",
         "https://koyasi777.com/seoto/"},
    };
    QString details;
    for (const auto &notice : kNotices) {
      QFile file(QString::fromLatin1(notice.resource));
      if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(parent, QCoreApplication::translate("mozc::gui::RomanTableEditorDialog", "Preset origins and licenses"),
                             QCoreApplication::translate("mozc::gui::RomanTableEditorDialog", "Could not open preset license."));
        return;
      }
      if (!details.isEmpty()) details += QStringLiteral("\n\n");
      details += QString::fromUtf8(notice.name);
      details += QStringLiteral("\n");
      details += QString::fromLatin1(notice.url);
      details += QStringLiteral("\n\n");
      const QByteArray license_bytes = file.readAll();
      details += QString::fromUtf8(license_bytes.constData(),
                                   license_bytes.size());
    }

    // QMessageBox::setDetailedText() uses Qt's built-in untranslated
    // 'Show Details...' / 'Hide Details...' buttons on some Windows setups.
    // Use localizable actions instead of depending on qt_ja.qm deployment.
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("mozc::gui::RomanTableEditorDialog", "Preset origins and licenses"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *summary = new QLabel(
        QCoreApplication::translate("mozc::gui::RomanTableEditorDialog", "These presets include their original notices. Changes to a "
           "selected preset follow Mozkey updates unless you edit the table."),
        &dialog);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *full_text = new QPlainTextEdit(&dialog);
    full_text->setReadOnly(true);
    full_text->setPlainText(details);
    full_text->hide();
    layout->addWidget(full_text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QCoreApplication::translate("mozc::gui::RomanTableEditorDialog", "Close"));
    auto *toggle = buttons->addButton(QCoreApplication::translate("mozc::gui::RomanTableEditorDialog", "Show details"),
                                     QDialogButtonBox::ActionRole);
    QObject::connect(toggle, &QPushButton::clicked, &dialog,
                     [&dialog, full_text, toggle]() {
                       const bool visible = !full_text->isVisible();
                       full_text->setVisible(visible);
                       toggle->setText(visible ? QCoreApplication::translate("mozc::gui::RomanTableEditorDialog", "Hide details")
                                               : QCoreApplication::translate("mozc::gui::RomanTableEditorDialog", "Show details"));
                       if (visible) {
                         dialog.resize(700, 480);
                       } else {
                         dialog.layout()->invalidate();
                         dialog.layout()->activate();
                         dialog.resize(530, 160);
                       }
                     });
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(530, 160);
    dialog.exec();
}

namespace {
constexpr int kPersonalPresetLimit = 16;
constexpr int kPersonalPresetMaxBytes = 64 * 1024;
constexpr int kPersonalPresetTotalBytes = 256 * 1024;

bool CheckPersonalTable(const QByteArray &bytes, QString *message,
                        bool *duplicates) {
  *duplicates = false;
  if (bytes.isEmpty() || bytes.size() > kPersonalPresetMaxBytes ||
      bytes.contains('\0') || QString::fromUtf8(bytes).toUtf8() != bytes) {
    *message = QCoreApplication::translate(
        "mozc::gui::RomanTableEditorDialog", "Invalid preset table data.");
    return false;
  }
  std::istringstream stream(std::string(bytes.constData(), bytes.size()));
  const roman_table_rules::ParseResult parsed =
      roman_table_rules::Parse(stream, 4000);
  if (!parsed.ok()) {
    const char *translation = "Invalid preset table data.";
    if (parsed.error == roman_table_rules::ParseError::kInvalidRule) {
      *message = QCoreApplication::translate(
          "mozc::gui::RomanTableEditorDialog", "Invalid preset rule on line %1.")
          .arg(static_cast<qulonglong>(parsed.error_line));
      return false;
    }
    if (parsed.error == roman_table_rules::ParseError::kTooManyRules) {
      translation = "Too many preset rules.";
    } else if (parsed.error == roman_table_rules::ParseError::kNoRules) {
      translation = "Preset table has no rules.";
    }
    *message = QCoreApplication::translate(
        "mozc::gui::RomanTableEditorDialog", translation);
    return false;
  }
  *duplicates = roman_table_rules::HasConflictingInputKeys(parsed.rules);
  return true;
}
}  // namespace

void RomanTableEditorDialog::SelectPresetInEditor(int index) {
  if (preset_selector_ == nullptr || index < 0) return;
  const QString selected = preset_selector_->itemData(index).toString();
  const QString current = default_table_requested_
      ? QStringLiteral("default")
      : (preset_id_.empty() ? QStringLiteral("custom")
                            : QString::fromStdString(preset_id_));
  if (selected == current) return;

  if (selected == QStringLiteral("custom")) {
    // Detach from the built-in/personal snapshot, retaining all visible rules.
    if (!Update()) {
      RefreshPresetState();
      return;
    }
    default_table_requested_ = false;
    preset_id_.clear();
    preset_snapshot_.clear();
    RefreshPresetState();
    return;
  }

  // Only replacing the rules requires confirmation. Switching to Custom above
  // never drops a single rule.
  if (mutable_table_widget()->rowCount() > 0 &&
      QMessageBox::Ok != QMessageBox::question(
          this, dialog_title_,
          IsUserPresetDirty()
              ? tr("Unsaved edits to the personal preset will be discarded. "
                   "Switch input method?")
              : tr("Replace the current romaji rules?"),
          QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel)) {
    RefreshPresetState();
    return;
  }
  if (selected == QStringLiteral("default")) {
    LoadDefaultRomanTable();
    RefreshPresetState();
    return;
  }

  const std::string id = selected.toStdString();
  const absl::string_view preset =
      config::ResolveRomanTablePreset(personal_presets_, id);
  if (preset.empty()) {
    QMessageBox::warning(this, dialog_title_, tr("Preset is unavailable."));
    RefreshPresetState();
    return;
  }
  const std::string rules(preset.data(), preset.size());
  if (!LoadFromString(rules) || !Update()) {
    QMessageBox::warning(this, dialog_title_, tr("Could not load preset."));
    RefreshPresetState();
    return;
  }
  preset_id_ = id;
  preset_snapshot_ = table();
  default_table_requested_ = false;
  RefreshPresetState();
}

bool RomanTableEditorDialog::IsUserPresetDirty() {
  if (default_table_requested_ || preset_id_.rfind("user:", 0) != 0) {
    return false;
  }
  const std::vector<roman_table_rules::Rule> rules =
      CollectEditorRules(mutable_table_widget());
  // A newly inserted incomplete row must still mark the preset as modified;
  // it cannot be serialized or persisted until the user completes it.
  for (const auto &rule : rules) {
    if (!roman_table_rules::IsValidRule(rule)) return true;
  }
  return roman_table_rules::Serialize(rules) != preset_snapshot_;
}

void RomanTableEditorDialog::OverwriteCurrentPersonalPreset() {
  if (!IsUserPresetDirty()) return;
  const std::string id = preset_id_.substr(5);
  int index = -1;
  for (int i = 0; i < personal_presets_.user_roman_table_presets_size(); ++i) {
    if (personal_presets_.user_roman_table_presets(i).id() == id) {
      index = i;
      break;
    }
  }
  if (index < 0 || !Update()) {
    RefreshPresetState();
    return;
  }
  const QByteArray bytes(table().data(), static_cast<int>(table().size()));
  QString error;
  bool duplicates = false;
  if (!CheckPersonalTable(bytes, &error, &duplicates)) {
    QMessageBox::warning(this, dialog_title_, error);
    return;
  }
  if (duplicates && QMessageBox::Yes != QMessageBox::question(
          this, dialog_title_, tr("Different rules share an input key. Save anyway?"),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) return;
  int total_bytes = bytes.size();
  for (int i = 0; i < personal_presets_.user_roman_table_presets_size(); ++i) {
    if (i != index) {
      total_bytes += static_cast<int>(
          personal_presets_.user_roman_table_presets(i).table().size());
    }
  }
  if (total_bytes > kPersonalPresetTotalBytes) {
    QMessageBox::warning(this, dialog_title_, tr("Preset storage limit reached."));
    return;
  }
  const QString name = QString::fromStdString(
      personal_presets_.user_roman_table_presets(index).name());
  if (QMessageBox::Yes != QMessageBox::question(
          this, dialog_title_,
          tr("Overwrite the personal preset '%1' with these rules?").arg(name),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) return;
  personal_presets_.mutable_user_roman_table_presets(index)->set_table(
      bytes.constData(), bytes.size());
  preset_snapshot_ = table();
  default_table_requested_ = false;
  RefreshPresetState();
}

void RomanTableEditorDialog::RefreshPresetState() {
  if (preset_selector_ != nullptr) {
    QSignalBlocker blocker(preset_selector_);
    preset_selector_->clear();
    preset_selector_->addItem(tr("Standard"), QStringLiteral("default"));
    preset_selector_->addItem(QStringLiteral("Simplified-AZIK"),
                              QStringLiteral("simplified_azik"));
    preset_selector_->addItem(QString::fromUtf8("新月配列"),
                              QStringLiteral("shingetsu"));
    preset_selector_->addItem(QString::fromUtf8("せおと配列"),
                              QStringLiteral("seoto"));
    preset_selector_->addItem(QString::fromUtf8("せおと配列 Plus"),
                              QStringLiteral("seoto_plus"));
    preset_selector_->insertSeparator(preset_selector_->count());
    preset_selector_->addItem(tr("Custom"), QStringLiteral("custom"));
    if (personal_presets_.user_roman_table_presets_size() > 0) {
      preset_selector_->insertSeparator(preset_selector_->count());
    }
    for (const auto &entry : personal_presets_.user_roman_table_presets()) {
      QString label = QString::fromStdString(entry.name());
      if (preset_id_ == "user:" + entry.id() && IsUserPresetDirty()) {
        label += tr(" (changes not saved)");
      }
      preset_selector_->addItem(label,
          QString::fromStdString("user:" + entry.id()));
    }
    const QString id = default_table_requested_
        ? QStringLiteral("default")
        : (preset_id_.empty() ? QStringLiteral("custom")
                              : QString::fromStdString(preset_id_));
    const int selected_index = preset_selector_->findData(id);
    preset_selector_->setCurrentIndex(selected_index >= 0 ? selected_index :
        preset_selector_->findData(QStringLiteral("custom")));
  }
  QString title;
  QString detail;
  if (default_table_requested_) {
    title = tr("Standard");
    detail = tr("Uses the Mozkey standard rules.");
  } else if (preset_id_.empty()) {
    title = tr("Custom");
    detail = tr("Personal edits are kept; Mozkey updates do not replace them.");
  } else if (preset_id_.rfind("user:", 0) == 0) {
    title = tr("Personal preset");
    const std::string local_id = preset_id_.substr(5);
    for (const auto &entry : personal_presets_.user_roman_table_presets()) {
      if (entry.id() == local_id) {
        title = QString::fromStdString(entry.name());
        break;
      }
    }
    detail = IsUserPresetDirty()
        ? tr("Changes not saved to this preset. Save it here, or OK will use "
             "the edited rules as Custom without changing the saved preset.")
        : tr("Saved by you; unchanged by Mozkey updates.");
  } else {
    if (preset_id_ == "simplified_azik") {
      title = QStringLiteral("Simplified-AZIK");
    } else if (preset_id_ == "shingetsu") {
      title = QString::fromUtf8("新月配列");
    } else if (preset_id_ == "seoto") {
      title = QString::fromUtf8("せおと配列");
    } else if (preset_id_ == "seoto_plus") {
      title = QString::fromUtf8("せおと配列 Plus");
    } else {
      title = QString::fromStdString(preset_id_);
    }
    detail = tr("Follows bundled updates until you customize the rules.");
  }
  if (preset_state_label_) {
    // The selector already displays the name; the second line explains
    // only what updates automatically, without repeating the current value.
    preset_state_label_->setText(detail);
    if (preset_selector_) preset_selector_->setToolTip(title);
  }
  if (save_preset_button_) {
    save_preset_button_->setEnabled(
        mutable_table_widget()->rowCount() > 0 &&
        personal_presets_.user_roman_table_presets_size() < kPersonalPresetLimit);
  }
  if (overwrite_preset_button_) {
    overwrite_preset_button_->setEnabled(IsUserPresetDirty());
  }
  if (manage_preset_button_) {
    manage_preset_button_->setEnabled(true);
  }
}

bool RomanTableEditorDialog::CreatePersonalPreset(
    const QString &name, const QByteArray &tsv) {
  QString error;
  bool duplicates = false;
  if (!CheckPersonalTable(tsv, &error, &duplicates)) {
    QMessageBox::warning(this, dialog_title_, error);
    return false;
  }
  if (duplicates && QMessageBox::Yes != QMessageBox::question(
          this, dialog_title_, tr("Different rules share an input key. Save anyway?"),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) return false;
  const QString clean = name.trimmed();
  if (clean.isEmpty() || clean.size() > 64) {
    QMessageBox::warning(this, dialog_title_,
                         tr("Preset name must have 1 to 64 characters."));
    return false;
  }
  if (personal_presets_.user_roman_table_presets_size() >= kPersonalPresetLimit) {
    QMessageBox::warning(this, dialog_title_, tr("Preset limit reached."));
    return false;
  }
  int size = tsv.size();
  for (const auto &preset : personal_presets_.user_roman_table_presets()) {
    size += static_cast<int>(preset.table().size());
    if (QString::fromStdString(preset.name()).compare(
            clean, Qt::CaseInsensitive) == 0) {
      QMessageBox::warning(this, dialog_title_, tr("Preset name already exists."));
      return false;
    }
  }
  if (size > kPersonalPresetTotalBytes) {
    QMessageBox::warning(this, dialog_title_, tr("Preset storage limit reached."));
    return false;
  }
  const std::string id =
      QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  auto *entry = personal_presets_.add_user_roman_table_presets();
  entry->set_id(id);
  entry->set_name(clean.toStdString());
  entry->set_table(tsv.constData(), tsv.size());
  // The caller has already validated the table and the editor always stores
  // an exact text snapshot, never a truncated or inferred table.
  if (!LoadFromString(entry->table()) || !Update()) {
    personal_presets_.mutable_user_roman_table_presets()->RemoveLast();
    QMessageBox::warning(this, dialog_title_, tr("Could not load preset."));
    return false;
  }
  preset_id_ = "user:" + id;
  preset_snapshot_ = table();
  default_table_requested_ = false;
  RefreshPresetState();
  return true;
}

void RomanTableEditorDialog::SaveCurrentAsPreset() {
  if (!Update()) return;
  const QByteArray bytes(table().data(), table().size());
  bool accepted = false;
  const QString name = QInputDialog::getText(
      this, tr("Save as preset"), tr("Preset name:"),
      QLineEdit::Normal, QString(), &accepted);
  if (!accepted) return;
  CreatePersonalPreset(name, bytes);
}

void RomanTableEditorDialog::ManagePersonalPresets() {
  // This is a second transaction nested inside the Roman table editor's
  // transaction. No personal-preset changes are committed on Cancel.
  config::Config staged = personal_presets_;
  QDialog manager(this);
  manager.setWindowTitle(tr("Manage personal presets"));
  manager.setMinimumSize(600, 390);
  manager.resize(740, 480);

  auto *root = new QVBoxLayout(&manager);
  auto *help = new QLabel(
      tr("Select a saved preset to rename, export or delete it. "
         "Built-in presets cannot be changed here."), &manager);
  help->setWordWrap(true);
  root->addWidget(help);

  auto *content = new QHBoxLayout();
  auto *items = new QListWidget(&manager);
  items->setSelectionMode(QAbstractItemView::SingleSelection);
  items->setAlternatingRowColors(true);
  content->addWidget(items, 1);

  auto *actions = new QVBoxLayout();
  auto *add = new QPushButton(tr("Add preset from file..."), &manager);
  auto *rename = new QPushButton(tr("Rename..."), &manager);
  auto *export_file = new QPushButton(tr("Export preset..."), &manager);
  auto *remove = new QPushButton(tr("Delete..."), &manager);
  for (QPushButton *button : {add, rename, export_file, remove}) {
    button->setMinimumWidth(220);
    actions->addWidget(button);
  }
  actions->addStretch();
  content->addLayout(actions);
  root->addLayout(content, 1);

  auto *detail = new QLabel(&manager);
  detail->setWordWrap(true);
  root->addWidget(detail);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &manager);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Save changes"));
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
  root->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted,
                   &manager, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected,
                   &manager, &QDialog::reject);

  const auto selected_index = [&]() -> int {
    const QListWidgetItem *item = items->currentItem();
    if (!item) return -1;
    const std::string id = item->data(Qt::UserRole).toString().toStdString();
    for (int i = 0; i < staged.user_roman_table_presets_size(); ++i) {
      if (staged.user_roman_table_presets(i).id() == id) return i;
    }
    return -1;
  };
  const auto update_selection = [&]() {
    const int index = selected_index();
    const bool has_selection = index >= 0;
    rename->setEnabled(has_selection);
    export_file->setEnabled(has_selection);
    remove->setEnabled(has_selection);
    add->setEnabled(staged.user_roman_table_presets_size() < kPersonalPresetLimit);
    if (!has_selection) {
      detail->setText(tr("Saved presets: %1 / %2")
          .arg(staged.user_roman_table_presets_size()).arg(kPersonalPresetLimit));
    } else {
      const auto &entry = staged.user_roman_table_presets(index);
      int count = 0;
      const QByteArray table_bytes(entry.table().data(),
                                   static_cast<int>(entry.table().size()));
      for (const QByteArray &line : table_bytes.split('\n')) {
        if (!line.isEmpty() && !line.startsWith('#')) ++count;
      }
      detail->setText(tr("%1 — %2 rules").arg(
          QString::fromStdString(entry.name())).arg(count));
    }
  };
  const auto repopulate = [&](const QString &preferred_id) {
    QSignalBlocker blocker(items);
    items->clear();
    int target = -1;
    for (const auto &entry : staged.user_roman_table_presets()) {
      QString label = QString::fromStdString(entry.name());
      if (preset_id_ == "user:" + entry.id()) label += tr(" (selected)");
      auto *item = new QListWidgetItem(label, items);
      const QString id = QString::fromStdString(entry.id());
      item->setData(Qt::UserRole, id);
      if (id == preferred_id) target = items->count() - 1;
    }
    if (target < 0 && items->count() > 0) target = 0;
    if (target >= 0) items->setCurrentRow(target);
    update_selection();
  };
  QObject::connect(items, &QListWidget::itemSelectionChanged,
                   &manager, update_selection);
  repopulate(QString());

  QObject::connect(add, &QPushButton::clicked, &manager, [&]() {
    const QString filename = QFileDialog::getOpenFileName(
        &manager, tr("Add preset from file"), QDir::homePath(),
        tr("Romaji tables (*.txt *.tsv);;All files (*)"));
    if (filename.isEmpty()) return;
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly) ||
        file.size() > kPersonalPresetMaxBytes) {
      QMessageBox::warning(&manager, manager.windowTitle(),
                           tr("Cannot read preset file."));
      return;
    }
    const QByteArray bytes = file.readAll();
    QString error;
    bool duplicates = false;
    if (!CheckPersonalTable(bytes, &error, &duplicates)) {
      QMessageBox::warning(&manager, manager.windowTitle(), error);
      return;
    }
    if (duplicates && QMessageBox::Yes != QMessageBox::question(
            &manager, manager.windowTitle(),
            tr("Different rules share an input key. Save anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(
        &manager, tr("Add preset from file"), tr("Preset name:"),
        QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted) return;
    if (name.isEmpty() || name.size() > 64) {
      QMessageBox::warning(&manager, manager.windowTitle(),
                           tr("Preset name must have 1 to 64 characters."));
      return;
    }
    int used = bytes.size();
    for (const auto &entry : staged.user_roman_table_presets()) {
      if (QString::fromStdString(entry.name()).compare(
              name, Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(&manager, manager.windowTitle(),
                             tr("Preset name already exists."));
        return;
      }
      used += static_cast<int>(entry.table().size());
    }
    if (staged.user_roman_table_presets_size() >= kPersonalPresetLimit ||
        used > kPersonalPresetTotalBytes) {
      QMessageBox::warning(&manager, manager.windowTitle(),
                           tr("Preset storage limit reached."));
      return;
    }
    const std::string id =
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    auto *entry = staged.add_user_roman_table_presets();
    entry->set_id(id);
    entry->set_name(name.toStdString());
    entry->set_table(bytes.constData(), bytes.size());
    repopulate(QString::fromStdString(id));
  });

  QObject::connect(rename, &QPushButton::clicked, &manager, [&]() {
    const int index = selected_index();
    if (index < 0) return;
    const auto &entry = staged.user_roman_table_presets(index);
    const QString old_name = QString::fromStdString(entry.name());
    const QString id = QString::fromStdString(entry.id());
    bool accepted = false;
    const QString name = QInputDialog::getText(
        &manager, tr("Rename preset"), tr("New name:"),
        QLineEdit::Normal, old_name, &accepted).trimmed();
    if (!accepted) return;
    if (name.isEmpty() || name.size() > 64) {
      QMessageBox::warning(&manager, manager.windowTitle(),
                           tr("Preset name must have 1 to 64 characters."));
      return;
    }
    for (int i = 0; i < staged.user_roman_table_presets_size(); ++i) {
      if (i == index) continue;
      if (QString::fromStdString(
              staged.user_roman_table_presets(i).name()).compare(
                  name, Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(&manager, manager.windowTitle(),
                             tr("Preset name already exists."));
        return;
      }
    }
    staged.mutable_user_roman_table_presets(index)->set_name(name.toStdString());
    repopulate(id);
  });

  QObject::connect(export_file, &QPushButton::clicked, &manager, [&]() {
    const int index = selected_index();
    if (index < 0) return;
    const auto &entry = staged.user_roman_table_presets(index);
    const QString filename = QFileDialog::getSaveFileName(
        &manager, tr("Export preset"),
        QString::fromStdString(entry.name()) + QStringLiteral(".txt"),
        tr("Romaji tables (*.txt *.tsv);;All files (*)"));
    if (filename.isEmpty()) return;
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(entry.table().data(), entry.table().size()) !=
            static_cast<qint64>(entry.table().size()) || !file.commit()) {
      QMessageBox::warning(&manager, manager.windowTitle(),
                           tr("Could not export preset."));
    }
  });

  QObject::connect(remove, &QPushButton::clicked, &manager, [&]() {
    const int index = selected_index();
    if (index < 0) return;
    const auto &entry = staged.user_roman_table_presets(index);
    const QString name = QString::fromStdString(entry.name());
    if (QMessageBox::Yes != QMessageBox::question(
        &manager, manager.windowTitle(),
        tr("Delete the saved preset '%1'? The current editor rules will "
           "not be deleted.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No)) return;
    staged.mutable_user_roman_table_presets()->DeleteSubrange(index, 1);
    repopulate(QString());
  });

  if (manager.exec() != QDialog::Accepted) return;
  // A deleted active preset falls back to Custom. Its visible rules remain.
  if (preset_id_.rfind("user:", 0) == 0) {
    bool found = false;
    for (const auto &entry : staged.user_roman_table_presets()) {
      if ("user:" + entry.id() == preset_id_) {
        found = true;
        break;
      }
    }
    if (!found) {
      preset_id_.clear();
      preset_snapshot_.clear();
      default_table_requested_ = false;
    }
  }
  personal_presets_.mutable_user_roman_table_presets()->CopyFrom(
      staged.user_roman_table_presets());
  RefreshPresetState();
}

// static
bool RomanTableEditorDialog::Show(QWidget *parent,
                                  const std::string &current_roman_table,
                                  const std::string &current_preset_id,
                                  config::Config *preset_library,
                                  std::string *new_roman_table,
                                  std::string *new_preset_id) {
  CHECK(preset_library);
  RomanTableEditorDialog window(parent);
  window.personal_presets_ = *preset_library;

  if (current_roman_table.empty()) {
    window.LoadDefaultRomanTable();
  } else {
    window.LoadFromString(current_roman_table);
    // The config loader already replaced the snapshot with the currently
    // bundled preset. Retain its ID if no table content is changed.
    if (!current_preset_id.empty() && window.Update()) {
      window.preset_id_ = current_preset_id;
      window.preset_snapshot_ = window.table();
    }
  }

  window.RefreshPresetState();
  const bool result = (QDialog::Accepted == window.exec());
  if (result) {
    preset_library->mutable_user_roman_table_presets()->CopyFrom(
        window.personal_presets_.user_roman_table_presets());
  }
  new_roman_table->clear();
  new_preset_id->clear();

  if (result && !window.default_table_requested_ &&
      (!window.preset_id_.empty() ||
       window.table() != window.GetDefaultRomanTable())) {
    *new_roman_table = window.table();
    // Import, deletion, resetting, checkbox edits and manual row changes
    // all become custom whenever their resulting table differs.
    if (!window.preset_id_.empty() &&
        window.table() == window.preset_snapshot_) {
      *new_preset_id = window.preset_id_;
    }
  }
  return result;
}
}  // namespace gui
}  // namespace mozc
