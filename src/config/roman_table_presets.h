#ifndef MOZC_CONFIG_ROMAN_TABLE_PRESETS_H_
#define MOZC_CONFIG_ROMAN_TABLE_PRESETS_H_

#include "absl/strings/string_view.h"
#include "protocol/config.pb.h"

namespace mozc {
namespace config {

// Returns the exact bundled TSV bytes for a known preset ID; empty for unknown.
// These IDs are stable settings values and must never be reused.
absl::string_view GetRomanTablePreset(absl::string_view id);

// Applies the latest bundled preset iff the saved snapshot fingerprint still
// matches. Unknown presets and externally edited tables become custom.
// Resolve a builtin or local (user:<UUID>) preset against this config.
// Returned view is valid only while config's preset storage stays unchanged.
absl::string_view ResolveRomanTablePreset(const Config& config,
                                         absl::string_view id);

void NormalizeRomanTablePreset(Config* config);

}  // namespace config
}  // namespace mozc

#endif  // MOZC_CONFIG_ROMAN_TABLE_PRESETS_H_
