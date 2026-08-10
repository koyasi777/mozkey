// Copyright 2026
// Licensed under the same terms as Mozc.

#ifndef MOZC_WIN32_TIP_TIP_ZENZ_CONTEXT_REQUEST_H_
#define MOZC_WIN32_TIP_TIP_ZENZ_CONTEXT_REQUEST_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "protocol/commands.pb.h"

namespace mozc {
namespace win32 {
namespace tsf {

// Client-side copy of the server's bounded request for one physical key event.
// The server currently caps each semantic Unicode-character budget at 128, and
// the Windows client clamps again defensively before using it for a synchronous
// TSF read.
struct TipZenzContextRequest {
  uint32_t preceding_length = 0;
  uint32_t following_length = 0;

  bool empty() const {
    return preceding_length == 0 && following_length == 0;
  }
};

// One-shot request state bridging OnTestKey to the matching OnKey.
//
// OnTestKey must Reset() before any early return, then UpdateFromOutput() after
// a successful TestSendKey path. OnKey must Take() at entry. This deliberately
// does not reuse TipPrivateContext::last_output(), whose value can legitimately
// survive key events that are not sent to the server.
class TipZenzContextRequestState {
 public:
  void Reset();
  void UpdateFromOutput(const commands::Output& output);
  TipZenzContextRequest Take();

 private:
  TipZenzContextRequest request_;
};

// Returns a conservative TSF native range budget. Windows text ranges are backed
// by UTF-16 text, so one Unicode scalar may occupy two native code units.
size_t GetZenzTsfNativeAcquisitionLength(
    const TipZenzContextRequest& request);

// Helpers used after converting Windows surrounding text to UTF-8. They count
// Unicode characters rather than bytes and never split a UTF-8 character.
bool HasAtLeastZenzContextCharacters(std::string_view text,
                                     size_t character_count);
std::string TakeLeadingZenzContextCharacters(std::string_view text,
                                              size_t max_characters);
std::string TakeTrailingZenzContextCharacters(std::string_view text,
                                               size_t max_characters);

}  // namespace tsf
}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_TIP_TIP_ZENZ_CONTEXT_REQUEST_H_