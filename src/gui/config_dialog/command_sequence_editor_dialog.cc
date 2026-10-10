// Copyright 2010-2021, Google Inc.
// All rights reserved.

#include "gui/config_dialog/command_sequence_editor_dialog.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"

namespace mozc {
namespace gui {
namespace {

constexpr int kRawCommandRole = Qt::UserRole;

QStringList SplitCommandSequence(const QString& raw_sequence) {
  QStringList result;
  const std::string raw = raw_sequence.toStdString();

  for (absl::string_view token :
       absl::StrSplit(raw, '|', absl::SkipEmpty())) {
    token = absl::StripAsciiWhitespace(token);
    if (!token.empty()) {
      result << QString::fromStdString(std::string(token));
    }
  }

  return result;
}

}  // namespace

CommandSequenceEditorDialog::CommandSequenceEditorDialog(
    const QStringList& raw_commands,
    const QHash<QString, QString>& raw_to_display,
    QWidget* parent)
    : QDialog(parent),
      raw_commands_(raw_commands),
      raw_to_display_(raw_to_display) {
  setWindowTitle(tr("Edit command sequence"));

  filter_edit_ = new QLineEdit(this);
  filter_edit_->setPlaceholderText(tr("Filter commands"));

  available_list_ = new QListWidget(this);
  sequence_list_ = new QListWidget(this);

  QPushButton* add_button = new QPushButton(tr("Add >"), this);
  QPushButton* remove_button = new QPushButton(tr("< Remove"), this);
  QPushButton* up_button = new QPushButton(tr("Up"), this);
  QPushButton* down_button = new QPushButton(tr("Down"), this);
  QPushButton* clear_button = new QPushButton(tr("Clear"), this);

  QVBoxLayout* available_layout = new QVBoxLayout;
  available_layout->addWidget(new QLabel(tr("Available commands"), this));
  available_layout->addWidget(filter_edit_);
  available_layout->addWidget(available_list_);

  QVBoxLayout* button_layout = new QVBoxLayout;
  button_layout->addStretch();
  button_layout->addWidget(add_button);
  button_layout->addWidget(remove_button);
  button_layout->addSpacing(12);
  button_layout->addWidget(up_button);
  button_layout->addWidget(down_button);
  button_layout->addSpacing(12);
  button_layout->addWidget(clear_button);
  button_layout->addStretch();

  QVBoxLayout* sequence_layout = new QVBoxLayout;
  sequence_layout->addWidget(new QLabel(tr("Command sequence"), this));
  sequence_layout->addWidget(sequence_list_);

  QHBoxLayout* main_layout = new QHBoxLayout;
  main_layout->addLayout(available_layout, 1);
  main_layout->addLayout(button_layout);
  main_layout->addLayout(sequence_layout, 1);

  QDialogButtonBox* button_box =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                           this);

  QVBoxLayout* root_layout = new QVBoxLayout(this);
  root_layout->addLayout(main_layout);
  root_layout->addWidget(button_box);
  setLayout(root_layout);

  connect(filter_edit_, &QLineEdit::textChanged,
          this, [this]() { UpdateAvailableFilter(); });
  connect(add_button, &QPushButton::clicked,
          this, [this]() { AddSelectedCommand(); });
  connect(remove_button, &QPushButton::clicked,
          this, [this]() { RemoveSelectedCommand(); });
  connect(up_button, &QPushButton::clicked,
          this, [this]() { MoveSelectedCommandUp(); });
  connect(down_button, &QPushButton::clicked,
          this, [this]() { MoveSelectedCommandDown(); });
  connect(clear_button, &QPushButton::clicked,
          this, [this]() { ClearSequence(); });
  connect(available_list_, &QListWidget::itemDoubleClicked,
          this, [this](QListWidgetItem*) { AddSelectedCommand(); });
  connect(sequence_list_, &QListWidget::itemDoubleClicked,
          this, [this](QListWidgetItem* item) {
            if (item == nullptr) return;
            const QString raw = item->data(kRawCommandRole).toString();
            if (!raw.startsWith(QStringLiteral("Delay("))) {
              RemoveSelectedCommand();  // Preserve existing double-click UX.
              return;
            }
            bool valid = false;
            const int old_ms = raw.mid(6, raw.size() - 7).toInt(&valid);
            if (!valid) return;
            bool ok = false;
            const int ms = QInputDialog::getInt(
                this,
                QCoreApplication::translate("mozc::gui::KeyMapEditorDialog", "Delay"),
                QCoreApplication::translate("mozc::gui::KeyMapEditorDialog", "Delay (ms)"),
                old_ms, 0, 5000, 1, &ok);
            if (ok) {
              const QString updated = QStringLiteral("Delay(%1)").arg(ms);
              item->setData(kRawCommandRole, updated);
              item->setText(DisplayNameForRawCommand(updated));
            }
          });
  connect(button_box, &QDialogButtonBox::accepted, this, [this]() {
    for (int i = 0; i < sequence_list_->count(); ++i) {
      const QString raw =
          sequence_list_->item(i)->data(kRawCommandRole).toString();
      if (raw == QStringLiteral("RevertZenzToMozc") &&
          sequence_list_->count() != 1) {
        QMessageBox::warning(
            this,
            QCoreApplication::translate("mozc::gui::KeyMapEditorDialog",
                                        "Invalid command sequence"),
            QCoreApplication::translate(
                "mozc::gui::KeyMapEditorDialog",
                "Return to Mozc conversion must be used alone."));
        return;
      }
      if ((i == 0 && raw.startsWith(QStringLiteral("Delay("))) ||
          (i + 1 < sequence_list_->count() &&
           raw == QStringLiteral("ForceZenzLiveCorrection"))) {
        QMessageBox::warning(
            this,
            QCoreApplication::translate("mozc::gui::KeyMapEditorDialog",
                                        "Invalid command sequence"),
            QCoreApplication::translate(
                "mozc::gui::KeyMapEditorDialog",
                "Delay cannot be first; manual Zenz correction must be last."));
        return;
      }
    }
    accept();
  });
  connect(button_box, &QDialogButtonBox::rejected,
          this, &QDialog::reject);

  PopulateAvailableCommands();
  resize(640, 420);
}

void CommandSequenceEditorDialog::SetCommandSequence(
    const QString& raw_sequence) {
  sequence_list_->clear();

  for (const QString& raw_command : SplitCommandSequence(raw_sequence)) {
    AddSequenceCommand(raw_command);
  }
}

QString CommandSequenceEditorDialog::command_sequence() const {
  QStringList raw_commands;
  for (int i = 0; i < sequence_list_->count(); ++i) {
    const QListWidgetItem* item = sequence_list_->item(i);
    raw_commands << item->data(kRawCommandRole).toString();
  }
  return raw_commands.join("|");
}

QString CommandSequenceEditorDialog::display_text() const {
  QStringList display_commands;
  for (int i = 0; i < sequence_list_->count(); ++i) {
    display_commands << sequence_list_->item(i)->text();
  }
  return display_commands.join(QString::fromUtf8(" → "));
}

void CommandSequenceEditorDialog::PopulateAvailableCommands() {
  available_list_->clear();

  for (const QString& raw_command : raw_commands_) {
    QListWidgetItem* item =
        new QListWidgetItem(DisplayNameForRawCommand(raw_command),
                            available_list_);
    item->setData(kRawCommandRole, raw_command);
  }

  UpdateAvailableFilter();
}

void CommandSequenceEditorDialog::AddSelectedCommand() {
  QListWidgetItem* item = available_list_->currentItem();
  if (item == nullptr) {
    return;
  }

  QString raw = item->data(kRawCommandRole).toString();
  if (raw == QStringLiteral("Delay")) {
    if (sequence_list_->count() == 0) {
      // The first command determines the keymap's initial input state.
      // Delay is deliberately only a continuation step.
      return;
    }
    bool ok = false;
    const int ms = QInputDialog::getInt(
        this, QCoreApplication::translate("mozc::gui::KeyMapEditorDialog", "Delay"),
        QCoreApplication::translate("mozc::gui::KeyMapEditorDialog", "Delay (ms)"),
        700, 0, 5000, 1, &ok);
    if (!ok) return;
    raw = QStringLiteral("Delay(%1)").arg(ms);
  }
  AddSequenceCommand(raw);
}

void CommandSequenceEditorDialog::RemoveSelectedCommand() {
  const int row = sequence_list_->currentRow();
  if (row < 0) {
    return;
  }

  delete sequence_list_->takeItem(row);

  if (sequence_list_->count() > 0) {
    sequence_list_->setCurrentRow(std::min(row, sequence_list_->count() - 1));
  }
}

void CommandSequenceEditorDialog::MoveSelectedCommandUp() {
  const int row = sequence_list_->currentRow();
  if (row <= 0) {
    return;
  }

  QListWidgetItem* item = sequence_list_->takeItem(row);
  sequence_list_->insertItem(row - 1, item);
  sequence_list_->setCurrentRow(row - 1);
}

void CommandSequenceEditorDialog::MoveSelectedCommandDown() {
  const int row = sequence_list_->currentRow();
  if (row < 0 || row + 1 >= sequence_list_->count()) {
    return;
  }

  QListWidgetItem* item = sequence_list_->takeItem(row);
  sequence_list_->insertItem(row + 1, item);
  sequence_list_->setCurrentRow(row + 1);
}

void CommandSequenceEditorDialog::ClearSequence() {
  sequence_list_->clear();
}

void CommandSequenceEditorDialog::UpdateAvailableFilter() {
  const QString filter = filter_edit_->text();

  for (int i = 0; i < available_list_->count(); ++i) {
    QListWidgetItem* item = available_list_->item(i);
    const QString raw = item->data(kRawCommandRole).toString();
    const QString display = item->text();

    const bool matched =
        filter.isEmpty() ||
        raw.contains(filter, Qt::CaseInsensitive) ||
        display.contains(filter, Qt::CaseInsensitive);

    item->setHidden(!matched);
  }
}

QString CommandSequenceEditorDialog::DisplayNameForRawCommand(
    const QString& raw_command) const {
  const auto it = raw_to_display_.find(raw_command);
  if (it != raw_to_display_.end()) {
    return it.value();
  }
  if (raw_command == QStringLiteral("Delay")) {
    return QCoreApplication::translate("mozc::gui::KeyMapEditorDialog", "Delay");
  }
  if (raw_command.startsWith(QStringLiteral("Delay(")) &&
      raw_command.endsWith(QLatin1Char(')'))) {
    return QCoreApplication::translate(
        "mozc::gui::KeyMapEditorDialog", "Delay %1 ms")
        .arg(raw_command.mid(6, raw_command.size() - 7));
  }
  return raw_command;
}

void CommandSequenceEditorDialog::AddSequenceCommand(
    const QString& raw_command) {
  QListWidgetItem* item =
      new QListWidgetItem(DisplayNameForRawCommand(raw_command),
                          sequence_list_);
  item->setData(kRawCommandRole, raw_command);
}

}  // namespace gui
}  // namespace mozc
