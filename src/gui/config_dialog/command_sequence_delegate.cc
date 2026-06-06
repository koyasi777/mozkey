// Copyright 2010-2021, Google Inc.
// All rights reserved.

#include "gui/config_dialog/command_sequence_delegate.h"

#include <QAbstractItemModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWidget>

#include "gui/config_dialog/command_sequence_editor_dialog.h"

namespace mozc {
namespace gui {

CommandSequenceDelegate::CommandSequenceDelegate(QObject* parent)
    : QItemDelegate(parent) {}

void CommandSequenceDelegate::SetCommandList(
    const QStringList& raw_commands,
    const QHash<QString, QString>& raw_to_display) {
  raw_commands_ = raw_commands;
  raw_to_display_ = raw_to_display;
}

QWidget* CommandSequenceDelegate::createEditor(
    QWidget* parent,
    const QStyleOptionViewItem& option,
    const QModelIndex& index) const {
  Q_UNUSED(option);

  OpenCommandSequenceEditor(
      parent, const_cast<QAbstractItemModel*>(index.model()), index);
  return nullptr;
}

bool CommandSequenceDelegate::editorEvent(
    QEvent* event, QAbstractItemModel* model,
    const QStyleOptionViewItem& option, const QModelIndex& index) {
  if (event == nullptr || model == nullptr || !index.isValid()) {
    return false;
  }

  switch (event->type()) {
    case QEvent::MouseButtonRelease: {
      QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() != Qt::LeftButton) {
        return false;
      }
      return OpenCommandSequenceEditor(
          const_cast<QWidget*>(option.widget), model, index);
    }

    case QEvent::MouseButtonDblClick:
      return OpenCommandSequenceEditor(
          const_cast<QWidget*>(option.widget), model, index);

    case QEvent::KeyPress: {
      QKeyEvent* key_event = static_cast<QKeyEvent*>(event);
      switch (key_event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
        case Qt::Key_F2:
          return OpenCommandSequenceEditor(
              const_cast<QWidget*>(option.widget), model, index);
        default:
          break;
      }
      break;
    }

    default:
      break;
  }

  return QItemDelegate::editorEvent(event, model, option, index);
}

bool CommandSequenceDelegate::OpenCommandSequenceEditor(
    QWidget* parent, QAbstractItemModel* model,
    const QModelIndex& index) const {
  if (model == nullptr || !index.isValid()) {
    return false;
  }

  QString raw_sequence = index.data(Qt::UserRole).toString();
  if (raw_sequence.isEmpty()) {
    raw_sequence = index.data(Qt::EditRole).toString();
  }

  CommandSequenceEditorDialog dialog(raw_commands_, raw_to_display_, parent);
  dialog.SetCommandSequence(raw_sequence);

  if (dialog.exec() != QDialog::Accepted) {
    return true;
  }

  const QString new_raw_sequence = dialog.command_sequence();
  const QString new_display_text = dialog.display_text();

  model->setData(index, new_display_text, Qt::EditRole);
  model->setData(index, new_raw_sequence, Qt::UserRole);
  return true;
}

}  // namespace gui
}  // namespace mozc
