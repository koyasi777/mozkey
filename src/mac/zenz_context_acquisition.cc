// Copyright 2026
// Licensed under the same terms as Mozc.

#include "mac/zenz_context_acquisition.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "base/util.h"

namespace mozc::mac {
namespace {

constexpr uint32_t kMaxZenzContextRequestLength = 128;
constexpr size_t kMaxUtf16CodeUnitsPerUnicodeCharacter = 2;
constexpr size_t kNativeAcquisitionPadding = 1;

size_t GetNativeUtf16Budget(uint32_t requested_characters) {
  const uint32_t clamped =
      std::min(requested_characters, kMaxZenzContextRequestLength);
  return static_cast<size_t>(clamped) *
             kMaxUtf16CodeUnitsPerUnicodeCharacter +
         kNativeAcquisitionPadding;
}

}  // namespace

std::optional<ZenzContextNativeRanges> GetZenzContextNativeRanges(
    size_t document_length, size_t selection_location, size_t selection_length,
    uint32_t preceding_length, uint32_t following_length) {
  if (selection_location > document_length ||
      selection_length > document_length - selection_location) {
    return std::nullopt;
  }

  ZenzContextNativeRanges ranges;

  if (preceding_length > 0) {
    const size_t native_budget = GetNativeUtf16Budget(preceding_length);
    const size_t native_length = std::min(selection_location, native_budget);
    ranges.preceding = ZenzContextNativeRange{
        .location = selection_location - native_length,
        .length = native_length,
    };
  }

  if (following_length > 0) {
    const size_t following_start = selection_location + selection_length;
    const size_t native_budget = GetNativeUtf16Budget(following_length);
    const size_t native_length =
        std::min(document_length - following_start, native_budget);
    ranges.following = ZenzContextNativeRange{
        .location = following_start,
        .length = native_length,
    };
  }

  return ranges;
}

std::string TakeLeadingZenzContextCharacters(
    const std::string_view text, const size_t max_characters) {
  if (max_characters == 0 || text.empty()) {
    return std::string();
  }

  const size_t length = Util::CharsLen(text);
  if (length <= max_characters) {
    return std::string(text);
  }

  return std::string(Util::Utf8SubString(text, 0, max_characters));
}

std::string TakeTrailingZenzContextCharacters(
    const std::string_view text, const size_t max_characters) {
  if (max_characters == 0 || text.empty()) {
    return std::string();
  }

  const size_t length = Util::CharsLen(text);
  if (length <= max_characters) {
    return std::string(text);
  }

  return std::string(
      Util::Utf8SubString(text, length - max_characters, max_characters));
}

}  // namespace mozc::mac
