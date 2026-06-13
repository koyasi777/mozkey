#include "session/zenz_prompt_builder.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace mozc {
namespace session {
namespace {

constexpr char kZenzReadingBegin[] = "\xEE\xB8\x80";       // U+EE00
constexpr char kZenzOutputBegin[] = "\xEE\xB8\x81";        // U+EE01
constexpr char kZenzContextBegin[] = "\xEE\xB8\x82";       // U+EE02
constexpr char kZenzProfileBegin[] = "\xEE\xB8\x83";       // U+EE03
constexpr char kZenzTopicBegin[] = "\xEE\xB8\x84";         // U+EE04
constexpr char kZenzStyleBegin[] = "\xEE\xB8\x85";         // U+EE05
constexpr char kZenzSettingsBegin[] = "\xEE\xB8\x86";      // U+EE06
constexpr char kZenzRightContextBegin[] = "\xEE\xB8\x87";  // U+EE07

constexpr size_t kMaxZenzConditionChars = 64;

}  // namespace

bool ZenzPromptBuilder::DecodeOneUtf8(absl::string_view input, size_t* index,
                                      char32_t* codepoint) {
  if (*index >= input.size()) {
    return false;
  }

  const unsigned char c0 = static_cast<unsigned char>(input[*index]);

  if (c0 < 0x80) {
    *codepoint = c0;
    ++(*index);
    return true;
  }

  if ((c0 & 0xE0) == 0xC0) {
    if (*index + 1 >= input.size()) {
      return false;
    }
    const unsigned char c1 = static_cast<unsigned char>(input[*index + 1]);
    if ((c1 & 0xC0) != 0x80) {
      return false;
    }
    *codepoint = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
    *index += 2;
    return true;
  }

  if ((c0 & 0xF0) == 0xE0) {
    if (*index + 2 >= input.size()) {
      return false;
    }
    const unsigned char c1 = static_cast<unsigned char>(input[*index + 1]);
    const unsigned char c2 = static_cast<unsigned char>(input[*index + 2]);
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
      return false;
    }
    *codepoint = ((c0 & 0x0F) << 12) |
                 ((c1 & 0x3F) << 6) |
                 (c2 & 0x3F);
    *index += 3;
    return true;
  }

  if ((c0 & 0xF8) == 0xF0) {
    if (*index + 3 >= input.size()) {
      return false;
    }
    const unsigned char c1 = static_cast<unsigned char>(input[*index + 1]);
    const unsigned char c2 = static_cast<unsigned char>(input[*index + 2]);
    const unsigned char c3 = static_cast<unsigned char>(input[*index + 3]);
    if ((c1 & 0xC0) != 0x80 ||
        (c2 & 0xC0) != 0x80 ||
        (c3 & 0xC0) != 0x80) {
      return false;
    }
    *codepoint = ((c0 & 0x07) << 18) |
                 ((c1 & 0x3F) << 12) |
                 ((c2 & 0x3F) << 6) |
                 (c3 & 0x3F);
    *index += 4;
    return true;
  }

  return false;
}

void ZenzPromptBuilder::AppendUtf8(char32_t codepoint, std::string* output) {
  if (codepoint <= 0x7F) {
    output->push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output->push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output->push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output->push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

bool ZenzPromptBuilder::IsZenzControlCodepoint(char32_t codepoint) {
  return 0xEE00 <= codepoint && codepoint <= 0xEE0F;
}

bool ZenzPromptBuilder::IsUnsafeControlCodepoint(char32_t codepoint) {
  return codepoint < 0x20 || codepoint == 0x7F;
}

std::string ZenzPromptBuilder::SanitizeConditionText(
    absl::string_view input, size_t max_chars) {
  std::string output;
  output.reserve(input.size());

  size_t index = 0;
  size_t chars = 0;
  while (index < input.size()) {
    if (max_chars > 0 && chars >= max_chars) {
      break;
    }

    const size_t old_index = index;
    char32_t cp = 0;
    if (!DecodeOneUtf8(input, &index, &cp)) {
      break;
    }

    if (IsZenzControlCodepoint(cp)) {
      continue;
    }

    if (IsUnsafeControlCodepoint(cp)) {
      if (cp == '\t' || cp == '\n' || cp == '\r') {
        output.push_back(' ');
        ++chars;
      }
      continue;
    }

    AppendUtf8(cp, &output);
    ++chars;

    if (index == old_index) {
      break;
    }
  }

  return output;
}

std::string ZenzPromptBuilder::HiraganaToKatakana(
    absl::string_view input) const {
  std::string output;
  output.reserve(input.size());

  size_t index = 0;
  while (index < input.size()) {
    const size_t old_index = index;
    char32_t cp = 0;
    if (!DecodeOneUtf8(input, &index, &cp)) {
      output.append(input.substr(old_index));
      break;
    }

    // Hiragana U+3041..U+3096 -> Katakana U+30A1..U+30F6
    if (0x3041 <= cp && cp <= 0x3096) {
      cp += 0x60;
    }

    AppendUtf8(cp, &output);
  }

  return output;
}

std::string ZenzPromptBuilder::Build(
    absl::string_view left_context,
    absl::string_view reading_hiragana) const {
  ZenzPromptOptions options;
  options.left_context = std::string(left_context.data(), left_context.size());
  return Build(reading_hiragana, options);
}

std::string ZenzPromptBuilder::Build(
    absl::string_view reading_hiragana,
    const ZenzPromptOptions& options) const {
  const std::string left_context =
      SanitizeConditionText(options.left_context, 0);
  const std::string right_context =
      SanitizeConditionText(options.right_context, 0);
  const std::string profile =
      SanitizeConditionText(options.profile, kMaxZenzConditionChars);
  const std::string topic =
      SanitizeConditionText(options.topic, kMaxZenzConditionChars);
  const std::string style =
      SanitizeConditionText(options.style, kMaxZenzConditionChars);
  const std::string settings =
      SanitizeConditionText(options.settings, kMaxZenzConditionChars);
  const std::string reading_katakana = HiraganaToKatakana(reading_hiragana);

  std::string prompt;
  prompt.reserve(sizeof(kZenzContextBegin) - 1 +
                 left_context.size() +
                 sizeof(kZenzRightContextBegin) - 1 +
                 right_context.size() +
                 sizeof(kZenzProfileBegin) - 1 +
                 profile.size() +
                 sizeof(kZenzTopicBegin) - 1 +
                 topic.size() +
                 sizeof(kZenzStyleBegin) - 1 +
                 style.size() +
                 sizeof(kZenzSettingsBegin) - 1 +
                 settings.size() +
                 sizeof(kZenzReadingBegin) - 1 +
                 reading_katakana.size() +
                 sizeof(kZenzOutputBegin) - 1);

  prompt.append(kZenzContextBegin);
  prompt.append(left_context);
  if (!right_context.empty()) {
    prompt.append(kZenzRightContextBegin);
    prompt.append(right_context);
  }
  if (!profile.empty()) {
    prompt.append(kZenzProfileBegin);
    prompt.append(profile);
  }
  if (!topic.empty()) {
    prompt.append(kZenzTopicBegin);
    prompt.append(topic);
  }
  if (!style.empty()) {
    prompt.append(kZenzStyleBegin);
    prompt.append(style);
  }
  if (!settings.empty()) {
    prompt.append(kZenzSettingsBegin);
    prompt.append(settings);
  }
  prompt.append(kZenzReadingBegin);
  prompt.append(reading_katakana);
  prompt.append(kZenzOutputBegin);

  return prompt;
}

}  // namespace session
}  // namespace mozc
