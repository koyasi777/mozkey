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
#include <cstddef>
#include <string>
#include <vector>

#include "composer/key_event_util.h"
#include "composer/key_parser.h"
#include "config/config_handler.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "testing/gunit.h"

namespace mozc {
namespace {
using ::mozc::commands::KeyEvent;
using ::mozc::config::Config;
using ::mozc::config::ConfigHandler;

void PushKey(const std::string& key_string, std::vector<KeyInformation>* keys) {
  KeyEvent key;
  if (!KeyParser::ParseKey(key_string, &key)) {
    return;
  }
  KeyInformation key_info;
  if (!KeyEventUtil::GetKeyInformation(key, &key_info)) {
    return;
  }
  keys->push_back(key_info);
}

TEST(KeyInfoUtilTest, ExtractSortedDirectModeKeys) {
  Config config;
  ConfigHandler::GetDefaultConfig(&config);

  constexpr char kCustomKeymapTable[] =
      "status\tkey\tcommand\n"
      "DirectInput\tHenkan\tIMEOn\n"
      "DirectInput\tCtrl j\tIMEOn\n"
      "DirectInput\tCtrl k\tIMEOff\n"
      "DirectInput\tCtrl l\tLaunchWordRegisterDialog\n"
      "Precomposition\tCtrl m\tIMEOn\n";
  config.set_session_keymap(Config::CUSTOM);
  config.set_custom_keymap_table(kCustomKeymapTable);

  const auto& actual = KeyInfoUtil::ExtractSortedDirectModeKeys(config);

  std::vector<KeyInformation> expected;
  PushKey("HENKAN", &expected);
  PushKey("Ctrl j", &expected);
  PushKey("Ctrl k", &expected);
  PushKey("Ctrl l", &expected);
  std::sort(expected.begin(), expected.end());

  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(actual[i], expected[i]);
  }
}

TEST(KeyInfoUtilTest, ExtractActiveModeImeOffAndDirectModeImeOnKeys) {
  Config config;
  ConfigHandler::GetDefaultConfig(&config);

  // Mirrors the documented left/right Alt IME toggle configuration. The active
  // modes use the "Commit|IMEOff" command *sequence*, which must still be
  // recognized as an IMEOff binding.
  constexpr char kCustomKeymapTable[] =
      "status\tkey\tcommand\n"
      "DirectInput\tRightAlt\tIMEOn\n"
      "Precomposition\tLeftAlt\tIMEOff\n"
      "Composition\tLeftAlt\tCommit|IMEOff\n"
      "Conversion\tLeftAlt\tCommit|IMEOff\n";
  config.set_session_keymap(Config::CUSTOM);
  config.set_custom_keymap_table(kCustomKeymapTable);

  {
    // LeftAlt turns the IME off in the active modes, including via the
    // "Commit|IMEOff" sequence.
    const auto& actual = KeyInfoUtil::ExtractSortedActiveModeImeOffKeys(config);
    std::vector<KeyInformation> expected;
    PushKey("LeftAlt", &expected);
    ASSERT_EQ(actual.size(), expected.size());
    EXPECT_EQ(actual[0], expected[0]);

    KeyEvent left_alt;
    KeyParser::ParseKey("LeftAlt", &left_alt);
    EXPECT_TRUE(KeyInfoUtil::ContainsKey(actual, left_alt));
    KeyEvent right_alt;
    KeyParser::ParseKey("RightAlt", &right_alt);
    EXPECT_FALSE(KeyInfoUtil::ContainsKey(actual, right_alt));
  }

  {
    // RightAlt turns the IME on from direct mode.
    const auto& actual = KeyInfoUtil::ExtractSortedDirectModeImeOnKeys(config);
    std::vector<KeyInformation> expected;
    PushKey("RightAlt", &expected);
    ASSERT_EQ(actual.size(), expected.size());
    EXPECT_EQ(actual[0], expected[0]);
  }
}

TEST(KeyInfoUtilTest, ContainsKey) {
  std::vector<KeyInformation> direct_mode_keys;
  PushKey("HENKAN", &direct_mode_keys);
  PushKey("Ctrl j", &direct_mode_keys);
  PushKey("Ctrl k", &direct_mode_keys);
  PushKey("Ctrl l", &direct_mode_keys);
  std::sort(direct_mode_keys.begin(), direct_mode_keys.end());

  {
    KeyEvent key;
    KeyParser::ParseKey("HENKAN", &key);
    EXPECT_TRUE(KeyInfoUtil::ContainsKey(direct_mode_keys, key));
  }
  {
    KeyEvent key;
    KeyParser::ParseKey("EISU", &key);
    EXPECT_FALSE(KeyInfoUtil::ContainsKey(direct_mode_keys, key));
  }
  {
    KeyEvent key;
    KeyParser::ParseKey("ctrl j", &key);
    EXPECT_TRUE(KeyInfoUtil::ContainsKey(direct_mode_keys, key));
  }
  {
    KeyEvent key;
    KeyParser::ParseKey("ctrl k", &key);
    EXPECT_TRUE(KeyInfoUtil::ContainsKey(direct_mode_keys, key));
  }
  {
    KeyEvent key;
    KeyParser::ParseKey("ctrl l", &key);
    EXPECT_TRUE(KeyInfoUtil::ContainsKey(direct_mode_keys, key));
  }
  {
    KeyEvent key;
    KeyParser::ParseKey("ctrl m", &key);
    EXPECT_FALSE(KeyInfoUtil::ContainsKey(direct_mode_keys, key));
  }
  {
    KeyEvent key;
    KeyParser::ParseKey("leftctrl j", &key);
    EXPECT_TRUE(KeyInfoUtil::ContainsKey(direct_mode_keys, key));
  }
  {
    KeyEvent key;
    KeyParser::ParseKey("rightctrl k", &key);
    EXPECT_TRUE(KeyInfoUtil::ContainsKey(direct_mode_keys, key));
  }
  {
    KeyEvent key;
    KeyParser::ParseKey("rightctrl m", &key);
    EXPECT_FALSE(KeyInfoUtil::ContainsKey(direct_mode_keys, key));
  }
}

}  // namespace
}  // namespace mozc
