// Copyright 2026 Mozkey contributors.
// SPDX-License-Identifier: BSD-3-Clause
// Existing mozc_tool.exe entry point for the read-only dictionary cost viewer.
// Does not construct client::Client, start servers, or persist dictionary data.
#include <QDialog>
#include <string>

#include "base/system_util.h"
#include "gui/base/singleton_window_helper.h"
#include "gui/base/util.h"
#include "gui/dictionary_cost_inspector/dictionary_cost_dialog.h"

int RunDictionaryCostInspector(int argc, char* argv[]) {
  auto app = mozc::gui::GuiUtil::InitQt(
      argc, argv, /*suppress_live_conversion=*/true);
  std::string singleton = "dictionary_cost_inspector.";
  singleton += mozc::SystemUtil::GetDesktopNameAsString();
  mozc::gui::SingletonWindowHelper helper(singleton);
  if (helper.FindPreviousWindow()) {
    helper.ActivatePreviousWindow();
    return -1;
  }

  mozc::gui::cost_inspector::DictionaryCostDialog window;
  window.show();
  window.raise();
  return app->exec();
}
