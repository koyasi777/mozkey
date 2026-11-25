// Copyright 2026 Mozkey contributors.
#include "config/external_dictionary_config.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace mozc::config {
namespace {
constexpr size_t kMaxSources = 32;
}  // namespace

std::vector<ExternalDictionarySourceOptions> GetExternalDictionarySources(
    const Config& config) {
  std::vector<ExternalDictionarySourceOptions> sources;
  std::unordered_set<std::string> seen;
  const auto append = [&](const std::string& path) {
    if (path.empty() || sources.size() >= kMaxSources ||
        !seen.insert(path).second) {
      return;
    }
    const auto& metadata = config.external_dictionary_source_options();
    const auto it = std::find_if(metadata.begin(), metadata.end(),
                                 [&path](const auto& item) {
                                   return item.path() == path;
                                 });
    if (it == metadata.end()) {
      sources.emplace_back().set_path(path);
    } else {
      sources.push_back(*it);
    }
  };
  if (config.external_dictionary_paths_size() != 0) {
    for (const auto& path : config.external_dictionary_paths()) {
      append(path);
      if (sources.size() >= kMaxSources) break;
    }
  } else {
    append(config.external_dictionary_path());
  }
  return sources;
}

void SetExternalDictionarySources(
    const std::vector<ExternalDictionarySourceOptions>& sources,
    Config* config) {
  config->clear_external_dictionary_path();
  config->clear_external_dictionary_paths();
  config->clear_external_dictionary_source_options();
  std::unordered_set<std::string> seen;
  for (const auto& source : sources) {
    if (source.path().empty() || !seen.insert(source.path()).second) continue;
    if (config->external_dictionary_paths_size() >= kMaxSources) break;
    if (config->external_dictionary_paths_size() == 0) {
      config->set_external_dictionary_path(source.path());
    }
    config->add_external_dictionary_paths(source.path());
    *config->add_external_dictionary_source_options() = source;
  }
}

}  // namespace mozc::config
