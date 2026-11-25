// Copyright 2026 Mozkey contributors.
#ifndef MOZC_CONFIG_EXTERNAL_DICTIONARY_CONFIG_H_
#define MOZC_CONFIG_EXTERNAL_DICTIONARY_CONFIG_H_

#include <vector>

#include "protocol/config.pb.h"

namespace mozc::config {

using ExternalDictionarySourceOptions = Config::ExternalDictionarySourceOptions;

// Return registered sources in legacy path-list order, with path-keyed metadata.
// Ignore empty paths, duplicate paths, and metadata for unregistered paths.
// This also works when the feature is disabled; callers control activation.
std::vector<ExternalDictionarySourceOptions> GetExternalDictionarySources(
    const Config& config);

// Update the legacy path fields and metadata together. Does not change the
// enable switch or any unrelated configuration fields.
void SetExternalDictionarySources(
    const std::vector<ExternalDictionarySourceOptions>& sources, Config* config);

}  // namespace mozc::config
#endif  // MOZC_CONFIG_EXTERNAL_DICTIONARY_CONFIG_H_
