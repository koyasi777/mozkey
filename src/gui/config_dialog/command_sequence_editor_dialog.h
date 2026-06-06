// Copyright 2010-2021, Google Inc.
// All rights reserved.

#ifndef MOZC_GUI_CONFIG_DIALOG_COMMAND_SEQUENCE_EDITOR_DIALOG_H_
#define MOZC_GUI_CONFIG_DIALOG_COMMAND_SEQUENCE_EDITOR_DIALOG_H_

#include <QDialog>
#include <QHash>
#include <QLineEdit>
#include <QListWidget>
#include <QString>
#include <QStringList>

namespace mozc {
namespace gui {

class CommandSequenceEditorDialog : public QDialog {
  Q_OBJECT

 public:
  CommandSequenceEditorDialog(const QStringList& raw_commands,
                              const QHash<QString, QString>& raw_to_display,
                              QWidget* parent = nullptr);

  void SetCommandSequence(const QString& raw_sequence);

  QString command_sequence() const;
  QString display_text() const;

 private:
  void PopulateAvailableCommands();
  void AddSelectedCommand();
  void RemoveSelectedCommand();
  void MoveSelectedCommandUp();
  void MoveSelectedCommandDown();
  void ClearSequence();
  void UpdateAvailableFilter();

  QString DisplayNameForRawCommand(const QString& raw_command) const;
  void AddSequenceCommand(const QString& raw_command);

  QStringList raw_commands_;
  QHash<QString, QString> raw_to_display_;

  QLineEdit* filter_edit_ = nullptr;
  QListWidget* available_list_ = nullptr;
  QListWidget* sequence_list_ = nullptr;
};

}  // namespace gui
}  // namespace mozc

#endif  // MOZC_GUI_CONFIG_DIALOG_COMMAND_SEQUENCE_EDITOR_DIALOG_H_
