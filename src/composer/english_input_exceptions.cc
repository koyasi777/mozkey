// User-defined, explicit exceptions to ordinary Roman input.
#include "composer/english_input_exceptions.h"

#include <cstddef>

namespace mozc::composer {

bool EnglishInputExceptions::IsValidInput(absl::string_view input) {
  if (input.size() < 2 || input.size() > 64) return false;
  for (char ch : input) {
    if (ch < 'a' || ch > 'z') return false;
  }
  return true;
}

bool EnglishInputExceptions::IsValidPreferred(absl::string_view preferred) {
  if (preferred.empty() || preferred.size() > 128) return false;
  // No control characters, tabs, newlines, or whitespace (TSV / JSON import).
  // Allow mixed-case ASCII technical notation such as C++, .NET, and GPT-6
  // as a *surface* but not yet as a typed trigger.
  for (unsigned char ch : preferred) {
    if (ch < 0x21 || ch > 0x7e) return false;
  }
  return true;
}

bool EnglishInputExceptions::IsValidEntry(const Entry& entry) {
  return IsValidInput(entry.input()) &&
         IsValidPreferred(entry.preferred()) &&
         (entry.behavior() == Entry::AUTO_ASCII ||
          entry.behavior() == Entry::CANDIDATE_ONLY);
}

const EnglishInputExceptions::Entry* EnglishInputExceptions::Find(
    const config::Config& config, absl::string_view raw) {
  if (!config.enable_english_input_exceptions() ||
      !IsValidInput(raw) || config.english_input_exceptions_size() > 4096) {
    return nullptr;
  }
  const Entry* result = nullptr;
  for (const Entry& entry : config.english_input_exceptions()) {
    if (entry.input() != raw) continue;
    // A duplicate (even disabled) is ambiguous: do not take precedence.
    if (result != nullptr) return nullptr;
    result = &entry;
  }
  return result != nullptr && result->enabled() && IsValidEntry(*result)
             ? result : nullptr;
}

EnglishInputExceptions::Index::Index(const config::Config& config) {
  if (!config.enable_english_input_exceptions() ||
      config.english_input_exceptions_size() > 4096) {
    return;
  }
  entries_.reserve(config.english_input_exceptions_size());
  for (const Entry& entry : config.english_input_exceptions()) {
    if (!EnglishInputExceptions::IsValidInput(entry.input())) continue;
    const auto [it, inserted] = entries_.emplace(entry.input(), &entry);
    if (!inserted) {
      // Preserve the fail-closed rule even if a duplicate is disabled or
      // invalid, and even when the same input appears three or more times.
      it->second = nullptr;
    }
  }
}

const EnglishInputExceptions::Entry* EnglishInputExceptions::Index::Find(
    absl::string_view raw) const {
  if (!EnglishInputExceptions::IsValidInput(raw)) return nullptr;
  const auto it = entries_.find(raw);
  if (it == entries_.end() || it->second == nullptr ||
      !it->second->enabled() ||
      !EnglishInputExceptions::IsValidEntry(*it->second)) {
    return nullptr;
  }
  return it->second;
}

}  // namespace mozc::composer
