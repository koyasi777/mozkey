// Copyright 2026
// Licensed under the same terms as Mozc.

#include "win32/tip/tip_zenz_context_request.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "base/util.h"
#include "protocol/commands.pb.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

constexpr uint32_t kMaxZenzContextRequestLength = 128;
constexpr size_t kMaxUtf16CodeUnitsPerUnicodeCharacter = 2;

uint32_t ClampRequestLength(uint32_t value) {
  return std::min(value, kMaxZenzContextRequestLength);
}

}  // namespace

void TipZenzContextRequestState::Reset() {
  request_ = TipZenzContextRequest();
  has_result_ = false;
}

void TipZenzContextRequestState::UpdateFromOutput(
    const commands::Output& output) {
  request_.preceding_length =
      ClampRequestLength(output.zenz_preceding_text_request_length());
  request_.following_length =
      ClampRequestLength(output.zenz_following_text_request_length());
  has_result_ = true;
}

TipZenzContextRequest TipZenzContextRequestState::Take() {
  const TipZenzContextRequest request = request_;
  Reset();
  return request;
}

bool ShouldRunZenzContextRequestFallback(
    const bool has_test_key_result, const bool has_generic_surrounding_text,
    const bool in_composition, const bool used_legacy_imm32_fallback) {
  return !has_test_key_result && has_generic_surrounding_text &&
         !in_composition && !used_legacy_imm32_fallback;
}

void SetZenzContextUnavailable(commands::Context* context) {
  context->set_zenz_preceding_text("");
  context->set_zenz_following_text("");
}

TipZenzTsfNativeAcquisitionLengths GetZenzTsfNativeAcquisitionLengths(
    const TipZenzContextRequest& request) {
  TipZenzTsfNativeAcquisitionLengths lengths;
  lengths.preceding =
      static_cast<size_t>(request.preceding_length) *
      kMaxUtf16CodeUnitsPerUnicodeCharacter;
  lengths.following =
      static_cast<size_t>(request.following_length) *
      kMaxUtf16CodeUnitsPerUnicodeCharacter;
  return lengths;
}

bool HasAtLeastZenzContextCharacters(const std::string_view text,
                                     const size_t character_count) {
  return character_count == 0 || Util::CharsLen(text) >= character_count;
}

std::string TakeLeadingZenzContextCharacters(const std::string_view text,
                                              const size_t max_characters) {
  if (max_characters == 0 || text.empty()) {
    return std::string();
  }

  const size_t length = Util::CharsLen(text);
  if (length <= max_characters) {
    return std::string(text);
  }

  return std::string(Util::Utf8SubString(text, 0, max_characters));
}

std::string TakeTrailingZenzContextCharacters(const std::string_view text,
                                               const size_t max_characters) {
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

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
