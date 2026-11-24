// Limits for read-only dictionary word-cost queries over local IPC.
#ifndef MOZC_PROTOCOL_DICTIONARY_COST_LIMITS_H_
#define MOZC_PROTOCOL_DICTIONARY_COST_LIMITS_H_

#include <cstddef>
#include <cstdint>

namespace mozc::commands {

inline constexpr std::size_t kDictionaryCostMaxReadingBytes = 300;
inline constexpr std::uint32_t kDictionaryCostMaxEntriesPerSource = 256;
inline constexpr std::size_t kDictionaryCostMaxResponseBytes = 256 * 1024;

}  // namespace mozc::commands

#endif  // MOZC_PROTOCOL_DICTIONARY_COST_LIMITS_H_
