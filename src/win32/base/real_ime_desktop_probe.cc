// Copyright 2026, Mozc contributors.
// All rights reserved.

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

#include "win32/base/imm_util.h"

int wmain() {
#if !defined(_M_ARM64)
#error real_ime_desktop_probe must be built for ARM64.
#endif

  const std::wstring input_tip = mozc::win32::ImeUtil::GetInputTip();
  std::wprintf(L"process_architecture=ARM64\n");
  std::wprintf(L"input_tip=%ls\n", input_tip.c_str());

  if (input_tip.empty()) {
    std::wprintf(L"PROFILE_ACTIVATION=FAIL\n");
    std::wprintf(L"reason=empty input TIP identifier\n");
    return 10;
  }

  const bool activated = mozc::win32::ImeUtil::SetDefault();
  std::wprintf(L"set_default=%ls\n", activated ? L"true" : L"false");

  if (!activated) {
    std::wprintf(L"PROFILE_ACTIVATION=FAIL\n");
    return 20;
  }

  std::wprintf(L"PROFILE_ACTIVATION=PASS\n");
  return 0;
}
