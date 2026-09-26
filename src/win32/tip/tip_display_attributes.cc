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

#include "win32/tip/tip_display_attributes.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string_view>

#include "absl/base/nullability.h"
#include "base/win32/com.h"
#include "config/config_handler.h"
#include "protocol/config.pb.h"

namespace mozc {
namespace win32 {
namespace tsf {

namespace {

constexpr std::wstring_view kInputDescription =
    L"TextService Display Attribute Input";
constexpr TF_DISPLAYATTRIBUTE kInputAttribute = {
    {TF_CT_NONE, {}},  // text color
    {TF_CT_NONE, {}},  // background color
    TF_LS_DOT,         // underline style
    FALSE,             // underline boldness
    {TF_CT_NONE, {}},  // underline color
    TF_ATTR_INPUT      // attribute info
};

constexpr std::wstring_view kConvertedDescription =
    L"TextService Display Attribute Converted";
constexpr TF_DISPLAYATTRIBUTE kConvertedAttribute = {
    {TF_CT_NONE, {}},         // text color
    {TF_CT_NONE, {}},         // background color
    TF_LS_SOLID,              // underline style
    TRUE,                     // underline boldness
    {TF_CT_NONE, {}},         // underline color
    TF_ATTR_TARGET_CONVERTED  // attribute info
};

constexpr std::wstring_view kPendingRomanDescription =
    L"TextService Display Attribute Pending Roman";

COLORREF RgbHexToColorRef(const uint32_t rgb) {
  const BYTE r = static_cast<BYTE>((rgb >> 16) & 0xff);
  const BYTE g = static_cast<BYTE>((rgb >> 8) & 0xff);
  const BYTE b = static_cast<BYTE>(rgb & 0xff);
  return RGB(r, g, b);
}

TF_DA_COLOR NoColor() {
  TF_DA_COLOR color = {};
  color.type = TF_CT_NONE;
  return color;
}

TF_DA_COLOR CustomColor(const uint32_t rgb) {
  TF_DA_COLOR color = {};
  color.type = TF_CT_COLORREF;
  color.cr = RgbHexToColorRef(rgb);
  return color;
}

TF_DA_COLOR SystemColor(const int index) {
  TF_DA_COLOR color = {};
  color.type = TF_CT_SYSCOLOR;
  color.nIndex = index;
  return color;
}

enum class PendingRomanDisplayMode {
  kCompatibilityFallback,
  kSystemGrayText,
  kSampledColors,
};

struct PendingRomanDisplayState {
  PendingRomanDisplayMode mode =
      PendingRomanDisplayMode::kCompatibilityFallback;
  COLORREF text_color = RGB(0, 0, 0);
  COLORREF background_color = RGB(0, 0, 0);
  bool use_explicit_background = true;
};

struct GeckoDisplayCompatibilityState {
  bool has_background = false;
  bool use_explicit_background = false;
  COLORREF background_color = RGB(0, 0, 0);
};

// ITfDisplayAttributeInfo::GetAttributeInfo() is queried in the host process.
// Keep presentation choices thread-local so independent TSF threads do not
// leak one editor's sampled background into another.
thread_local PendingRomanDisplayState g_pending_roman_display_state;
thread_local GeckoDisplayCompatibilityState
    g_gecko_display_compatibility_state;

TF_DA_COLOR ColorRefColor(const COLORREF color_ref) {
  TF_DA_COLOR color = {};
  color.type = TF_CT_COLORREF;
  color.cr = color_ref;
  return color;
}

TF_DISPLAYATTRIBUTE CreatePendingRomanAttribute() {
  TF_DISPLAYATTRIBUTE attr = kInputAttribute;

  switch (g_pending_roman_display_state.mode) {
    case PendingRomanDisplayMode::kSystemGrayText:
      attr.crText = SystemColor(COLOR_GRAYTEXT);
      attr.crBk = NoColor();
      attr.lsStyle = TF_LS_DOT;
      attr.fBoldLine = FALSE;
      attr.crLine = SystemColor(COLOR_GRAYTEXT);
      break;

    case PendingRomanDisplayMode::kSampledColors:
      attr.crText =
          ColorRefColor(g_pending_roman_display_state.text_color);
      attr.crBk =
          g_pending_roman_display_state.use_explicit_background
              ? ColorRefColor(
                    g_pending_roman_display_state.background_color)
              : NoColor();
      attr.lsStyle = TF_LS_DOT;
      attr.fBoldLine = FALSE;
      attr.crLine = SystemColor(COLOR_GRAYTEXT);
      break;

    case PendingRomanDisplayMode::kCompatibilityFallback:
      // Color-free fallback: Gecko keeps the page background untouched.
      attr.crText = NoColor();
      attr.crBk = NoColor();
      attr.lsStyle = TF_LS_DOT;
      attr.fBoldLine = FALSE;
      attr.crLine = SystemColor(COLOR_GRAYTEXT);
      break;
  }

  attr.bAttr = TF_ATTR_INPUT;
  return attr;
}

std::shared_ptr<const config::Config> ReloadAndGetConfig() {
  // The config dialog runs in another process.  Reload here so that
  // GetAttributeInfo() can pick up newly saved values when TSF asks for
  // updated display attributes.
  config::ConfigHandler::Reload();
  return config::ConfigHandler::GetSharedConfig();
}

TF_DISPLAYATTRIBUTE CreateInputAttributeFromConfig() {
  TF_DISPLAYATTRIBUTE attr = kInputAttribute;

  const std::shared_ptr<const config::Config> config = ReloadAndGetConfig();
  if (config == nullptr) {
    return attr;
  }

  attr.crText =
      config->use_custom_preedit_text_color()
          ? CustomColor(config->preedit_text_color())
          : NoColor();

  attr.crBk =
      config->use_custom_preedit_background_color()
          ? CustomColor(config->preedit_background_color())
          : NoColor();

  attr.crLine =
      config->use_custom_preedit_underline_color()
          ? CustomColor(config->preedit_underline_color())
          : NoColor();

  ApplyGeckoDisplayCompatibilityBackground(&attr);
  return attr;
}

TF_DISPLAYATTRIBUTE CreateConvertedAttributeFromConfig() {
  TF_DISPLAYATTRIBUTE attr = kConvertedAttribute;

  const std::shared_ptr<const config::Config> config = ReloadAndGetConfig();
  if (config == nullptr) {
    return attr;
  }

  attr.crText =
      config->use_custom_preedit_target_text_color()
          ? CustomColor(config->preedit_target_text_color())
          : NoColor();

  attr.crBk =
      config->use_custom_preedit_target_background_color()
          ? CustomColor(config->preedit_target_background_color())
          : NoColor();

  attr.crLine =
      config->use_custom_preedit_target_underline_color()
          ? CustomColor(config->preedit_target_underline_color())
          : NoColor();

  ApplyGeckoDisplayCompatibilityBackground(&attr);
  return attr;
}

#ifdef GOOGLE_JAPANESE_INPUT_BUILD

// {DDF5CDBA-C3FF-4BAF-B817-CC9210FAD27E}
constexpr GUID kDisplayAttributeInput = {
    0xddf5cdba,
    0xc3ff,
    0x4baf,
    {0xb8, 0x17, 0xcc, 0x92, 0x10, 0xfa, 0xd2, 0x7e}};

// {F829C8C0-0EBB-4D29-BD2F-E413A944B7E4}
constexpr GUID kDisplayAttributeConverted = {
    0xf829c8c0,
    0x0ebb,
    0x4d29,
    {0xbd, 0x2f, 0xe4, 0x13, 0xa9, 0x44, 0xb7, 0xe4}};

// {08E60AFF-F2EB-461D-A053-67C971B4D6CC}
constexpr GUID kDisplayAttributePendingRoman = {
    0x08e60aff,
    0xf2eb,
    0x461d,
    {0xa0, 0x53, 0x67, 0xc9, 0x71, 0xb4, 0xd6, 0xcc}};

#else  // GOOGLE_JAPANESE_INPUT_BUILD

// {84CA1E7E-3020-4D1C-8968-DDA372D1E067}
constexpr GUID kDisplayAttributeInput = {
    0x84ca1e7e,
    0x3020,
    0x4d1c,
    {0x89, 0x68, 0xdd, 0xa3, 0x72, 0xd1, 0xe0, 0x67}};

// {8A4028E5-2DCD-4365-A5DC-71F67E797437}
constexpr GUID kDisplayAttributeConverted = {
    0x8a4028e5,
    0x2dcd,
    0x4365,
    {0xa5, 0xdc, 0x71, 0xf6, 0x7e, 0x79, 0x74, 0x37}};

// {519662D0-BEF1-459D-BD03-B02F1EF8FD88}
constexpr GUID kDisplayAttributePendingRoman = {
    0x519662d0,
    0xbef1,
    0x459d,
    {0xbd, 0x03, 0xb0, 0x2f, 0x1e, 0xf8, 0xfd, 0x88}};

#endif  // !GOOGLE_JAPANESE_INPUT_BUILD

}  // namespace

void SetPendingRomanDisplayAttributeSystemGray() {
  g_pending_roman_display_state.mode =
      PendingRomanDisplayMode::kSystemGrayText;
}

void SetGeckoDisplayCompatibilityBackground(
    const COLORREF background_color,
    const bool use_explicit_background) {
  g_gecko_display_compatibility_state.has_background = true;
  g_gecko_display_compatibility_state.use_explicit_background =
      use_explicit_background;
  g_gecko_display_compatibility_state.background_color =
      background_color;
}

void ClearGeckoDisplayCompatibilityBackground() {
  g_gecko_display_compatibility_state =
      GeckoDisplayCompatibilityState();
}

void ApplyGeckoDisplayCompatibilityBackground(
    TF_DISPLAYATTRIBUTE* attribute) {
  if (attribute == nullptr ||
      !g_gecko_display_compatibility_state.has_background ||
      !g_gecko_display_compatibility_state.use_explicit_background) {
    return;
  }
  if (attribute->crText.type != TF_CT_NONE &&
      attribute->crBk.type == TF_CT_NONE) {
    attribute->crBk = ColorRefColor(
        g_gecko_display_compatibility_state.background_color);
  }
}

void SetPendingRomanDisplayAttributeSampledColors(
    const COLORREF text_color, const COLORREF background_color,
    const bool use_explicit_background) {
  g_pending_roman_display_state.mode =
      PendingRomanDisplayMode::kSampledColors;
  g_pending_roman_display_state.text_color = text_color;
  g_pending_roman_display_state.background_color = background_color;
  g_pending_roman_display_state.use_explicit_background =
      use_explicit_background;
}

void SetPendingRomanDisplayAttributeCompatibilityFallback() {
  g_pending_roman_display_state.mode =
      PendingRomanDisplayMode::kCompatibilityFallback;
}

TipDisplayAttribute::TipDisplayAttribute(const GUID& guid,
                                         const TF_DISPLAYATTRIBUTE& attribute,
                                         const std::wstring_view description)
    : guid_(guid),
      description_(description),
      attribute_(attribute),
      original_attribute_(attribute) {}

STDMETHODIMP TipDisplayAttribute::GetGUID(GUID* absl_nullable guid) {
  return SaveToOutParam(guid_, guid);
}

STDMETHODIMP
TipDisplayAttribute::GetDescription(BSTR* absl_nullable description) {
  return SaveToOutParam(MakeUniqueBSTR(description_), description);
}

STDMETHODIMP
TipDisplayAttribute::GetAttributeInfo(
    TF_DISPLAYATTRIBUTE* absl_nullable attribute) {
  return SaveToOutParam(attribute_, attribute);
}

STDMETHODIMP
TipDisplayAttribute::SetAttributeInfo(
    const TF_DISPLAYATTRIBUTE* absl_nullable attribute) {
  if (attribute == nullptr) {
    return E_INVALIDARG;
  }
  attribute_ = *attribute;
  return S_OK;
}

STDMETHODIMP TipDisplayAttribute::Reset() {
  attribute_ = original_attribute_;
  return S_OK;
}

TipDisplayAttributeInput::TipDisplayAttributeInput()
    : TipDisplayAttribute(kDisplayAttributeInput, kInputAttribute,
                          kInputDescription) {}

STDMETHODIMP TipDisplayAttributeInput::GetAttributeInfo(
    TF_DISPLAYATTRIBUTE* absl_nullable attribute) {
  return SaveToOutParam(CreateInputAttributeFromConfig(), attribute);
}

const GUID& TipDisplayAttributeInput::guid() { return kDisplayAttributeInput; }

TipDisplayAttributeConverted::TipDisplayAttributeConverted()
    : TipDisplayAttribute(kDisplayAttributeConverted, kConvertedAttribute,
                          kConvertedDescription) {}

STDMETHODIMP TipDisplayAttributeConverted::GetAttributeInfo(
    TF_DISPLAYATTRIBUTE* absl_nullable attribute) {
  return SaveToOutParam(CreateConvertedAttributeFromConfig(), attribute);
}

const GUID& TipDisplayAttributeConverted::guid() {
  return kDisplayAttributeConverted;
}

TipDisplayAttributePendingRoman::TipDisplayAttributePendingRoman()
    : TipDisplayAttribute(kDisplayAttributePendingRoman,
                          CreatePendingRomanAttribute(),
                          kPendingRomanDescription) {}

STDMETHODIMP TipDisplayAttributePendingRoman::GetAttributeInfo(
    TF_DISPLAYATTRIBUTE* absl_nullable attribute) {
  return SaveToOutParam(CreatePendingRomanAttribute(), attribute);
}

const GUID& TipDisplayAttributePendingRoman::guid() {
  return kDisplayAttributePendingRoman;
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
