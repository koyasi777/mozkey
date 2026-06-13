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
#include <QHelpEvent>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QToolTip>
#include <QtGui>
#include <istream>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_split.h"
#include "base/config_file_stream.h"
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
  REMOVE_INDEX = 1,
  IMPORT_FROM_FILE_INDEX = 2,
  EXPORT_TO_FILE_INDEX = 3,
  RESET_INDEX = 4,
  MENU_SIZE = 5
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
}  // namespace

RomanTableEditorDialog::RomanTableEditorDialog(QWidget *parent)
    : GenericTableEditorDialog(parent, 4),
      actions_(MENU_SIZE),
      default_table_requested_(false),
      loading_table_(false) {
  actions_[NEW_INDEX] = mutable_edit_menu()->addAction(tr("New entry"));
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

  mutable_table_widget()->horizontalHeader()->viewport()->installEventFilter(this);

  resize(430, 350);

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
  const bool was_loading_table = loading_table_;
  loading_table_ = true;

  std::string line;
  mutable_table_widget()->setRowCount(0);
  mutable_table_widget()->verticalHeader()->hide();

  int row = 0;
  while (std::getline(*is, line)) {
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

    while (fields.size() < 4) {
      fields.push_back("");
    }

    std::string remaining_attributes;
    const bool display_ambiguous_result =
        SplitDisplayAmbiguousResultAttribute(fields[3], &remaining_attributes);

    QTableWidgetItem *input =
        new QTableWidgetItem(QString::fromStdString(fields[0]));
    QTableWidgetItem *output =
        new QTableWidgetItem(QString::fromStdString(fields[1]));
    QTableWidgetItem *pending =
        new QTableWidgetItem(QString::fromStdString(fields[2]));
    QTableWidgetItem *attributes =
        CreateDisplayAmbiguousResultItem(display_ambiguous_result,
                                         remaining_attributes);
    mutable_table_widget()->insertRow(row);
    mutable_table_widget()->setItem(row, 0, input);
    mutable_table_widget()->setItem(row, 1, output);
    mutable_table_widget()->setItem(row, 2, pending);
    mutable_table_widget()->setItem(row, 3, attributes);
    ++row;

    if (row >= max_entry_size()) {
      QMessageBox::warning(
          this, dialog_title_,
          tr("You can't have more than %1 entries").arg(max_entry_size()));
      break;
    }
  }

  loading_table_ = was_loading_table;

  UpdateMenuStatus();

  return true;
}

bool RomanTableEditorDialog::LoadDefaultRomanTable() {
  std::unique_ptr<std::istream> ifs(
      ConfigFileStream::LegacyOpen(kRomanTableFile));
  CHECK(ifs);  // should never happen
  CHECK(LoadFromStream(ifs.get()));
  default_table_requested_ = true;
  return true;
}

bool RomanTableEditorDialog::Update() {
  if (mutable_table_widget()->rowCount() == 0) {
    QMessageBox::warning(this, dialog_title_,
                         tr("Romaji to Kana table is empty."));
    return false;
  }

  bool contains_capital = false;
  std::string *table = mutable_table();
  table->clear();
  for (int i = 0; i < mutable_table_widget()->rowCount(); ++i) {
    const std::string &input =
        TableUtil::SafeGetItemText(mutable_table_widget(), i, 0).toStdString();
    const std::string &output =
        TableUtil::SafeGetItemText(mutable_table_widget(), i, 1).toStdString();
    const std::string &pending =
        TableUtil::SafeGetItemText(mutable_table_widget(), i, 2).toStdString();

    QTableWidgetItem *attributes_item = mutable_table_widget()->item(i, 3);
    const bool display_ambiguous_result =
        (attributes_item != nullptr &&
        attributes_item->checkState() == Qt::Checked);
    const std::string remaining_attributes =
        (attributes_item == nullptr)
            ? std::string()
            : attributes_item->data(Qt::UserRole).toString().toStdString();
    const std::string attributes =
        BuildAttributes(remaining_attributes, display_ambiguous_result);

    if (input.empty() || (output.empty() && pending.empty())) {
      continue;
    }

    *table += input;
    *table += '\t';
    *table += output;
    if (!pending.empty() || !attributes.empty()) {
      *table += '\t';
      *table += pending;
    }
    if (!attributes.empty()) {
      *table += '\t';
      *table += attributes;
    }
    *table += '\n';

    if (!contains_capital) {
      std::string lower = input;
      Util::LowerString(&lower);
      contains_capital = (lower != input);
    }
  }

  if (contains_capital) {
    // TODO(taku):
    // Want to see the current setting and suppress this
    // dialog if the shift-mode-switch is already off.
    QMessageBox::information(this, dialog_title_,
                             tr("Input fields contain capital characters. "
                                "\"Shift-mode-switch\" function is disabled "
                                "with this new mapping."));
  }

  return true;
}

void RomanTableEditorDialog::UpdateMenuStatus() {
  const bool status = (mutable_table_widget()->rowCount() > 0);
  actions_[RESET_INDEX]->setEnabled(status);
  actions_[REMOVE_INDEX]->setEnabled(status);
  UpdateOKButton(status);
}

void RomanTableEditorDialog::OnEditMenuAction(QAction *action) {
  if (action == actions_[NEW_INDEX]) {
    default_table_requested_ = false;
    AddNewItem();
    const int row = (mutable_table_widget()->currentRow() >= 0)
                        ? mutable_table_widget()->currentRow()
                        : (mutable_table_widget()->rowCount() - 1);
    if (row >= 0) {
      delete mutable_table_widget()->takeItem(row, 3);
      mutable_table_widget()->setItem(
          row, 3, CreateDisplayAmbiguousResultItem(false, std::string()));
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
      default_table_requested_ = false;
      Import();
    } else if (action == actions_[RESET_INDEX]) {
      LoadDefaultRomanTable();
    }
  } else if (action == actions_[EXPORT_TO_FILE_INDEX]) {
    Export();
  }
}


void RomanTableEditorDialog::MarkRomanTableEdited(QTableWidgetItem *item) {
  (void)item;
  if (loading_table_) {
    return;
  }
  default_table_requested_ = false;
}

bool RomanTableEditorDialog::eventFilter(QObject *obj, QEvent *event) {
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
bool RomanTableEditorDialog::Show(QWidget *parent,
                                  const std::string &current_roman_table,
                                  std::string *new_roman_table) {
  RomanTableEditorDialog window(parent);

  if (current_roman_table.empty()) {
    window.LoadDefaultRomanTable();
  } else {
    window.LoadFromString(current_roman_table);
  }

  // open modal mode
  const bool result = (QDialog::Accepted == window.exec());
  new_roman_table->clear();

  if (result && !window.default_table_requested_ &&
      window.table() != window.GetDefaultRomanTable()) {
    *new_roman_table = window.table();
  }

  return result;
}
}  // namespace gui
}  // namespace mozc
