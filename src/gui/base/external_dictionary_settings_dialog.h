// Copyright 2026 Mozkey contributors.
#ifndef MOZC_GUI_BASE_EXTERNAL_DICTIONARY_SETTINGS_DIALOG_H_
#define MOZC_GUI_BASE_EXTERNAL_DICTIONARY_SETTINGS_DIALOG_H_

#include <QDialog>
#include <vector>

#include "config/external_dictionary_config.h"

class QCheckBox;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace mozc::gui {

// Edits values in memory. The calling settings tool owns persistence and
// server notification; Cancel never mutates any config or TSV file.
class ExternalDictionarySettingsDialog final : public QDialog {
 public:
  static bool Edit(QWidget* parent, bool* enabled,
                   std::vector<config::ExternalDictionarySourceOptions>* sources);

 private:
  ExternalDictionarySettingsDialog(QWidget* parent, bool enabled,
                                   const std::vector<config::ExternalDictionarySourceOptions>& sources);
  void RefreshRows();
  void UpdateSelectionActions();
  void AddFiles();
  void EditSelected();
  void RemoveSelected();
  void MoveSelected(int direction);
  void CopySelectedPath();
  bool CollectAndValidate(bool* enabled, std::vector<config::ExternalDictionarySourceOptions>* sources);

  QCheckBox* enabled_checkbox_ = nullptr;
  QTableWidget* table_ = nullptr;
  QLineEdit* selected_path_ = nullptr;
  QPushButton* edit_button_ = nullptr;
  QPushButton* remove_button_ = nullptr;
  QPushButton* up_button_ = nullptr;
  QPushButton* down_button_ = nullptr;
  QPushButton* copy_path_button_ = nullptr;
  std::vector<config::ExternalDictionarySourceOptions> sources_;
};

}  // namespace mozc::gui
#endif  // MOZC_GUI_BASE_EXTERNAL_DICTIONARY_SETTINGS_DIALOG_H_
