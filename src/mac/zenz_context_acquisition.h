// Copyright 2026
// Licensed under the same terms as Mozc.

#ifndef MOZC_MAC_ZENZ_CONTEXT_ACQUISITION_H_
#define MOZC_MAC_ZENZ_CONTEXT_ACQUISITION_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mozc::mac {

// A range expressed in the native UTF-16 code-unit coordinate system used by
// NSString and IMKTextInput.
struct ZenzContextNativeRange {
  size_t location = 0;
  size_t length = 0;
};

struct ZenzContextNativeRanges {
  // Presence means the corresponding direction was requested. A present
  // zero-length range is valid at a document boundary.
  std::optional<ZenzContextNativeRange> preceding;
  std::optional<ZenzContextNativeRange> following;
};

// Converts semantic Unicode-character budgets from the server into bounded
// UTF-16 native ranges around the current selection.
//
// The selected text itself is intentionally excluded. The preceding range ends
// at selection_location, and the following range starts after selection_length.
// Invalid selection coordinates return std::nullopt.
std::optional<ZenzContextNativeRanges> GetZenzContextNativeRanges(
    size_t document_length, size_t selection_location, size_t selection_length,
    uint32_t preceding_length, uint32_t following_length);

// Trims UTF-8 text by Unicode character count without splitting a character.
std::string TakeLeadingZenzContextCharacters(std::string_view text,
                                             size_t max_characters);
std::string TakeTrailingZenzContextCharacters(std::string_view text,
                                              size_t max_characters);

}  // namespace mozc::mac

#endif  // MOZC_MAC_ZENZ_CONTEXT_ACQUISITION_H_
