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

#include "session/key_info_util.h"

#include <algorithm>
#include <istream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/config_file_stream.h"
#include "base/util.h"
#include "composer/key_event_util.h"
#include "composer/key_parser.h"
#include "config/config_handler.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "session/keymap.h"

namespace mozc {
namespace {

using ::mozc::config::Config;

enum class ExtractKeyType {
  kDirectModeKey,
  kDirectModeImeOffKey,
  kActiveModeImeOnKey,
  kActiveModeImeOffKey,
  kDirectModeImeOnKey,
};

bool IsDirectModeStatus(absl::string_view status) {
  return status == "Direct" || status == "DirectInput";
}

// The keymap "command" field may be a command sequence such as "Commit|IMEOff".
// The server splits these on '|' and runs each command, so mirror that here
// when checking whether a binding contains a particular command.
bool CommandSequenceContains(absl::string_view command,
                             absl::string_view target) {
  for (absl::string_view part : absl::StrSplit(command, '|')) {
    if (part == target) {
      return true;
    }
  }
  return false;
}

bool ShouldExtractKey(absl::string_view status, absl::string_view command,
                      ExtractKeyType type) {
  switch (type) {
    case ExtractKeyType::kDirectModeKey:
      return IsDirectModeStatus(status);

    case ExtractKeyType::kDirectModeImeOffKey:
      return IsDirectModeStatus(status) &&
             CommandSequenceContains(command, "IMEOff");

    case ExtractKeyType::kActiveModeImeOnKey:
      return !IsDirectModeStatus(status) &&
             CommandSequenceContains(command, "IMEOn");

    case ExtractKeyType::kActiveModeImeOffKey:
      return !IsDirectModeStatus(status) &&
             CommandSequenceContains(command, "IMEOff");

    case ExtractKeyType::kDirectModeImeOnKey:
      return IsDirectModeStatus(status) &&
             CommandSequenceContains(command, "IMEOn");
  }
  return false;
}

void AddKeyInformation(const commands::KeyEvent& key_event,
                       std::vector<KeyInformation>* result) {
  KeyInformation info;
  if (KeyEventUtil::GetKeyInformation(key_event, &info)) {
    result->push_back(info);
  }
}

std::vector<KeyInformation> ExtractSortedKeysFromStream(
    std::istream* ifs, ExtractKeyType type) {
  std::vector<KeyInformation> result;

  std::string line;
  std::getline(*ifs, line);  // Skip the first line.
  while (!ifs->eof()) {
    std::getline(*ifs, line);
    Util::ChopReturns(&line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    std::vector<std::string> rules =
        absl::StrSplit(line, '\t', absl::SkipEmpty());
    if (rules.size() != 3) {
      LOG(ERROR) << "Invalid format: " << line;
      continue;
    }

    const absl::string_view status = rules[0];
    const absl::string_view key = rules[1];
    const absl::string_view command = rules[2];

    if (!ShouldExtractKey(status, command, type)) {
      continue;
    }

    commands::KeyEvent key_event;
    if (KeyParser::ParseKey(key, &key_event)) {
      AddKeyInformation(key_event, &result);
    }
  }

  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::vector<KeyInformation> ExtractSortedKeysFromFile(
    const std::string& filename, ExtractKeyType type) {
  std::unique_ptr<std::istream> ifs(ConfigFileStream::LegacyOpen(filename));
  if (ifs == nullptr) {
    DLOG(FATAL) << "could not open file: " << filename;
    return std::vector<KeyInformation>();
  }
  return ExtractSortedKeysFromStream(ifs.get(), type);
}

std::vector<KeyInformation> ExtractSortedKeysFromConfig(
    const config::Config& config, ExtractKeyType type) {
  const config::Config::SessionKeymap& keymap = config.session_keymap();
  if (keymap == Config::CUSTOM) {
    const std::string& custom_keymap_table = config.custom_keymap_table();
    if (custom_keymap_table.empty()) {
      LOG(WARNING) << "custom_keymap_table is empty. use default setting";
      const char* default_keymapfile = keymap::KeyMapManager::GetKeyMapFileName(
          config::ConfigHandler::GetDefaultKeyMap());
      return ExtractSortedKeysFromFile(default_keymapfile, type);
    }
    std::istringstream ifs(custom_keymap_table);
    return ExtractSortedKeysFromStream(&ifs, type);
  }

  const char* keymap_file = keymap::KeyMapManager::GetKeyMapFileName(keymap);
  return ExtractSortedKeysFromFile(keymap_file, type);
}

}  // namespace

std::vector<KeyInformation> KeyInfoUtil::ExtractSortedDirectModeKeys(
    const config::Config& config) {
  return ExtractSortedKeysFromConfig(config, ExtractKeyType::kDirectModeKey);
}

std::vector<KeyInformation> KeyInfoUtil::ExtractSortedDirectModeImeOffKeys(
    const config::Config& config) {
  return ExtractSortedKeysFromConfig(config,
                                     ExtractKeyType::kDirectModeImeOffKey);
}

std::vector<KeyInformation> KeyInfoUtil::ExtractSortedActiveModeImeOnKeys(
    const config::Config& config) {
  return ExtractSortedKeysFromConfig(config,
                                     ExtractKeyType::kActiveModeImeOnKey);
}

std::vector<KeyInformation> KeyInfoUtil::ExtractSortedActiveModeImeOffKeys(
    const config::Config& config) {
  return ExtractSortedKeysFromConfig(config,
                                     ExtractKeyType::kActiveModeImeOffKey);
}

std::vector<KeyInformation> KeyInfoUtil::ExtractSortedDirectModeImeOnKeys(
    const config::Config& config) {
  return ExtractSortedKeysFromConfig(config,
                                     ExtractKeyType::kDirectModeImeOnKey);
}

bool KeyInfoUtil::ContainsKey(absl::Span<const KeyInformation> sorted_keys,
                              const commands::KeyEvent& key_event) {
  KeyInformation key_info;
  if (KeyEventUtil::GetKeyInformation(key_event, &key_info) &&
      std::binary_search(sorted_keys.begin(), sorted_keys.end(), key_info)) {
    return true;
  }

  commands::KeyEvent normalized_key_event;
  KeyEventUtil::NormalizeModifiers(key_event, &normalized_key_event);
  if (!KeyEventUtil::GetKeyInformation(normalized_key_event, &key_info)) {
    return false;
  }

  return std::binary_search(sorted_keys.begin(), sorted_keys.end(), key_info);
}

bool KeyInfoUtil::ContainsKeyInformation(
    absl::Span<const KeyInformation> sorted_keys,
    KeyInformation key_info) {
  return std::binary_search(sorted_keys.begin(), sorted_keys.end(), key_info);
}

}  // namespace mozc
