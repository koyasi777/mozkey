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

#include "gui/tool/mozc_tool_libmain.h"

#include <QtGui>
#include <string>
#include <vector>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "base/init_mozc.h"
#include "base/run_level.h"
#include "gui/base/debug_util.h"

#ifdef __APPLE__
#include <cstdlib>
#ifndef IGNORE_INVALID_FLAG
#include <iostream>
#endif  // IGNORE_INVALID_FLAG

#include "base/const.h"
#include "base/environ.h"
#include "base/file_util.h"
#endif  // __APPLE__

#ifdef _WIN32
#include <windows.h>

#include "gui/base/win_util.h"
#endif  // _WIN32

ABSL_FLAG(std::string, mode, "about_dialog", "mozc_tool mode");
ABSL_FLAG(std::string, windows_ime_icon_style, "default",
          "Windows IME icon style for internal TSF profile icon update");
ABSL_DECLARE_FLAG(std::string, error_type);

// Run* are defined in each qt module
int RunAboutDialog(int argc, char *argv[]);
int RunConfigDialog(int argc, char *argv[]);
int RunDictionaryTool(int argc, char *argv[]);
int RunWordRegisterDialog(int argc, char *argv[]);
int RunErrorMessageDialog(int argc, char *argv[]);

#ifdef _WIN32
// (PostInstall|RunAdministartion)Dialog are used for Windows only.
int RunPostInstallDialog(int argc, char *argv[]);
int RunAdministrationDialog(int argc, char *argv[]);
#endif  // _WIN32

#ifdef __APPLE__
int RunPrelaunchProcesses(int argc, char *argv[]);
#endif  // __APPLE__


#ifdef _WIN32
namespace {

constexpr int kTsfProfileIconIndexDefault = 0;
constexpr int kTsfProfileIconIndexSimpleBlack = 15;
constexpr int kTsfProfileIconIndexSimpleWhite = 16;
constexpr int kTsfProfileIconIndexSimpleHighContrast = 17;

constexpr wchar_t kTsfProfileSubKey[] =
    L"SOFTWARE\\Microsoft\\CTF\\TIP\\"
    L"{10A67BC8-22FA-4A59-90DC-2546652C56BF}\\"
    L"LanguageProfile\\0x00000411\\"
    L"{186F700C-71CF-43FE-A00E-AACB1D9E6D3D}";

bool EndsWithCaseInsensitive(const std::wstring& text,
                             const std::wstring& suffix) {
  if (text.size() < suffix.size()) {
    return false;
  }
  return ::CompareStringOrdinal(
             text.c_str() + text.size() - suffix.size(),
             static_cast<int>(suffix.size()), suffix.c_str(),
             static_cast<int>(suffix.size()), TRUE) == CSTR_EQUAL;
}

int GetTsfProfileIconIndexForStyleName(const std::string& style) {
  if (style == "monochrome_black") {
    return kTsfProfileIconIndexSimpleBlack;
  }
  if (style == "monochrome_white") {
    return kTsfProfileIconIndexSimpleWhite;
  }
  if (style == "monochrome_high_contrast") {
    return kTsfProfileIconIndexSimpleHighContrast;
  }
  if (style == "default") {
    return kTsfProfileIconIndexDefault;
  }
  return -1;
}

std::wstring GetCurrentModuleDirectory() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length = ::GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return std::wstring();
    }
    if (length < buffer.size() - 1) {
      std::wstring path(buffer.data(), length);
      const size_t separator = path.find_last_of(L"\\/");
      if (separator == std::wstring::npos) {
        return std::wstring();
      }
      return path.substr(0, separator);
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::wstring GetFallbackTsfProfileIconFile() {
  const std::wstring module_dir = GetCurrentModuleDirectory();
  if (!module_dir.empty()) {
    return module_dir + L"\\mozc_tip32.dll";
  }

  wchar_t program_files_x86[MAX_PATH] = {};
  const DWORD length = ::GetEnvironmentVariableW(
      L"ProgramFiles(x86)", program_files_x86, MAX_PATH);
  if (length > 0 && length < MAX_PATH) {
    return std::wstring(program_files_x86) + L"\\Mozc\\mozc_tip32.dll";
  }
  return L"C:\\Program Files (x86)\\Mozc\\mozc_tip32.dll";
}

bool ReadTsfProfileIconFile(std::wstring* icon_file) {
  if (icon_file == nullptr) {
    return false;
  }

  std::vector<wchar_t> icon_file_buffer(32768);
  DWORD icon_file_size = static_cast<DWORD>(
      icon_file_buffer.size() * sizeof(wchar_t));
  const LSTATUS status = ::RegGetValueW(
      HKEY_LOCAL_MACHINE, kTsfProfileSubKey, L"IconFile",
      RRF_RT_ANY | RRF_NOEXPAND | RRF_SUBKEY_WOW6464KEY, nullptr,
      icon_file_buffer.data(), &icon_file_size);
  if (status != ERROR_SUCCESS) {
    return false;
  }

  *icon_file = icon_file_buffer.data();
  return true;
}

std::wstring GetTsfProfileIconFileToWrite() {
  std::wstring icon_file;
  if (ReadTsfProfileIconFile(&icon_file) &&
      EndsWithCaseInsensitive(icon_file, L"\\mozc_tip32.dll")) {
    return icon_file;
  }
  return GetFallbackTsfProfileIconFile();
}

int RunTsfProfileIconStyleUpdate() {
  const int icon_index = GetTsfProfileIconIndexForStyleName(
      absl::GetFlag(FLAGS_windows_ime_icon_style));
  if (icon_index < 0) {
    LOG(ERROR) << "Unknown Windows IME icon style: "
               << absl::GetFlag(FLAGS_windows_ime_icon_style);
    return 2;
  }

  const std::wstring icon_file = GetTsfProfileIconFileToWrite();
  if (icon_file.empty()) {
    LOG(ERROR) << "Failed to resolve TSF profile icon file";
    return 3;
  }

  HKEY key = nullptr;
  const LSTATUS open_status = ::RegOpenKeyExW(
      HKEY_LOCAL_MACHINE, kTsfProfileSubKey, 0,
      KEY_SET_VALUE | KEY_WOW64_64KEY, &key);
  if (open_status != ERROR_SUCCESS || key == nullptr) {
    LOG(ERROR) << "RegOpenKeyExW failed: " << open_status;
    return 4;
  }

  const DWORD icon_index_dword = static_cast<DWORD>(icon_index);
  const LSTATUS file_status = ::RegSetValueExW(
      key, L"IconFile", 0, REG_SZ,
      reinterpret_cast<const BYTE*>(icon_file.c_str()),
      static_cast<DWORD>((icon_file.size() + 1) * sizeof(wchar_t)));
  const LSTATUS index_status = ::RegSetValueExW(
      key, L"IconIndex", 0, REG_DWORD,
      reinterpret_cast<const BYTE*>(&icon_index_dword),
      sizeof(icon_index_dword));
  ::RegCloseKey(key);

  if (file_status != ERROR_SUCCESS) {
    LOG(ERROR) << "RegSetValueExW(IconFile) failed: " << file_status;
    return 5;
  }
  if (index_status != ERROR_SUCCESS) {
    LOG(ERROR) << "RegSetValueExW(IconIndex) failed: " << index_status;
    return 6;
  }

  return 0;
}

}  // namespace
#endif  // _WIN32

#ifdef __APPLE__
namespace {

void SetFlagsFromEnv() {
  const std::string mode = mozc::Environ::GetEnv("FLAGS_mode");
  if (!mode.empty()) {
    absl::SetFlag(&FLAGS_mode, mode);
  }

  const std::string error_type = mozc::Environ::GetEnv("FLAGS_error_type");
  if (!error_type.empty()) {
    absl::SetFlag(&FLAGS_error_type, error_type);
  }
}

}  // namespace
#endif  // __APPLE__

int RunMozcTool(int argc, char *argv[]) {
#ifdef __APPLE__
  // OSX's app won't accept command line flags.  Here we preset flags from
  // environment variables.
  SetFlagsFromEnv();
#endif  // __APPLE__
  mozc::InitMozc(argv[0], &argc, &argv);

#ifdef __APPLE__
  // In Mac, we shares the same binary but changes the application
  // name.
  std::string binary_name = mozc::FileUtil::Basename(argv[0]);
  if (binary_name == "AboutDialog") {
    absl::SetFlag(&FLAGS_mode, "about_dialog");
  } else if (binary_name == "ConfigDialog") {
    absl::SetFlag(&FLAGS_mode, "config_dialog");
  } else if (binary_name == "DictionaryTool") {
    absl::SetFlag(&FLAGS_mode, "dictionary_tool");
  } else if (binary_name == "ErrorMessageDialog") {
    absl::SetFlag(&FLAGS_mode, "error_message_dialog");
  } else if (binary_name == "WordRegisterDialog") {
    absl::SetFlag(&FLAGS_mode, "word_register_dialog");
  } else if (binary_name == kProductPrefix "Prelauncher") {
    // The binary name of prelauncher is user visible in
    // "System Preferences" -> "Accounts" -> "Login items".
    // So we set kProductPrefix to the binary name.
    absl::SetFlag(&FLAGS_mode, "prelauncher");
  }
#endif  // __APPLE__

#ifdef _WIN32
  if (absl::GetFlag(FLAGS_mode) == "tsf_profile_icon_style_update") {
    return RunTsfProfileIconStyleUpdate();
  }
#endif  // _WIN32

  if (absl::GetFlag(FLAGS_mode) != "administration_dialog" &&
      !mozc::RunLevel::IsValidClientRunLevel()) {
    return -1;
  }

  // install Qt debug handler
  qInstallMessageHandler(mozc::gui::DebugUtil::MessageHandler);

#ifdef _WIN32
  // Update JumpList if available.
  mozc::gui::WinUtil::KeepJumpListUpToDate();
#endif  // _WIN32

  if (absl::GetFlag(FLAGS_mode) == "config_dialog") {
    return RunConfigDialog(argc, argv);
  } else if (absl::GetFlag(FLAGS_mode) == "dictionary_tool") {
    return RunDictionaryTool(argc, argv);
  } else if (absl::GetFlag(FLAGS_mode) == "word_register_dialog") {
    return RunWordRegisterDialog(argc, argv);
  } else if (absl::GetFlag(FLAGS_mode) == "error_message_dialog") {
    return RunErrorMessageDialog(argc, argv);
  } else if (absl::GetFlag(FLAGS_mode) == "about_dialog") {
    return RunAboutDialog(argc, argv);
#ifdef _WIN32
  } else if (absl::GetFlag(FLAGS_mode) == "post_install_dialog") {
    // post_install_dialog is used on Windows only.
    return RunPostInstallDialog(argc, argv);
  } else if (absl::GetFlag(FLAGS_mode) == "administration_dialog") {
    // administration_dialog is used on Windows only.
    return RunAdministrationDialog(argc, argv);
#endif  // _WIN32
#ifdef __APPLE__
  } else if (absl::GetFlag(FLAGS_mode) == "prelauncher") {
    // Prelauncher is used on Mac only.
    return RunPrelaunchProcesses(argc, argv);
#endif  // __APPLE__
  } else {
    LOG(ERROR) << "Unknown mode: " << absl::GetFlag(FLAGS_mode);
    return -1;
  }

  return 0;
}
