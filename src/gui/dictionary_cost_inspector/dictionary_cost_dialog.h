// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZC_GUI_DICTIONARY_COST_INSPECTOR_DICTIONARY_COST_DIALOG_H_
#define MOZC_GUI_DICTIONARY_COST_INSPECTOR_DICTIONARY_COST_DIALOG_H_

#include <QDialog>
#include <QPointer>
#include <cstdint>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QCloseEvent;

namespace mozc::gui::cost_inspector {

class LookupThread;

// Read-only inspector for the installed Mozkey server.
class DictionaryCostDialog final : public QDialog {
 public:
  // Product entry point: connects only when the user explicitly searches.
  explicit DictionaryCostDialog(QWidget* parent = nullptr);
  ~DictionaryCostDialog() override;

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void Search();
  void OnFinished(LookupThread* thread, uint64_t generation);
  void FillTable(const LookupThread& thread);
  void SetBusy(bool value);

  QLineEdit* reading_ = nullptr;
  QSpinBox* limit_ = nullptr;
  QPushButton* search_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* notice_ = nullptr;
  QTableWidget* table_ = nullptr;
  QPointer<LookupThread> active_;
  uint64_t generation_ = 0;
};

}  // namespace mozc::gui::cost_inspector
#endif  // MOZC_GUI_DICTIONARY_COST_INSPECTOR_DICTIONARY_COST_DIALOG_H_
