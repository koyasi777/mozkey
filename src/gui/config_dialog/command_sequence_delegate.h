// Copyright 2010-2021, Google Inc.
// All rights reserved.

#ifndef MOZC_GUI_CONFIG_DIALOG_COMMAND_SEQUENCE_DELEGATE_H_
#define MOZC_GUI_CONFIG_DIALOG_COMMAND_SEQUENCE_DELEGATE_H_

#include <QAbstractItemModel>
#include <QEvent>
#include <QHash>
#include <QItemDelegate>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStyleOptionViewItem>
#include <QWidget>

namespace mozc {
namespace gui {

class CommandSequenceDelegate : public QItemDelegate {
 public:
  explicit CommandSequenceDelegate(QObject* parent = nullptr);

  void SetCommandList(const QStringList& raw_commands,
                      const QHash<QString, QString>& raw_to_display);

  QWidget* createEditor(QWidget* parent,
                        const QStyleOptionViewItem& option,
                        const QModelIndex& index) const override;

  bool editorEvent(QEvent* event, QAbstractItemModel* model,
                   const QStyleOptionViewItem& option,
                   const QModelIndex& index) override;

 private:
  bool OpenCommandSequenceEditor(QWidget* parent, QAbstractItemModel* model,
                                 const QModelIndex& index) const;

  QStringList raw_commands_;
  QHash<QString, QString> raw_to_display_;
};

}  // namespace gui
}  // namespace mozc

#endif  // MOZC_GUI_CONFIG_DIALOG_COMMAND_SEQUENCE_DELEGATE_H_
