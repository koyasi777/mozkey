// Copyright 2026 Mozkey contributors.
#include "dictionary/external_dictionary_manager.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "base/file_util.h"
#include "base/strings/unicode.h"

namespace mozc::dictionary {
namespace {
constexpr size_t kMaxSources = 32;
constexpr size_t kMaxFileBytes = 16 * 1024 * 1024;
constexpr size_t kMaxEntries = 200000;
constexpr size_t kMaxValuesPerKey = 64;
constexpr size_t kMaxKeyBytes = 256;
constexpr size_t kMaxValueBytes = 1024;
constexpr auto kFullReadInterval = std::chrono::seconds(30);

bool IsValidField(absl::string_view field, size_t maximum_bytes) {
  if (field.empty() || field.size() > maximum_bytes ||
      !strings::IsValidUtf8(field)) {
    return false;
  }
  for (unsigned char c : field) {
    if (c < 0x20 || c == 0x7f) return false;
  }
  return true;
}
}  // namespace

ExternalDictionaryManager::ExternalDictionaryManager(
    std::chrono::milliseconds poll_interval, bool start_worker)
    : poll_interval_(std::max(poll_interval, std::chrono::milliseconds(1))),
      snapshot_(std::make_shared<const Snapshot>()) {
  if (start_worker) worker_ = std::thread([this] { WorkerLoop(); });
}

ExternalDictionaryManager::~ExternalDictionaryManager() { Stop(); }

void ExternalDictionaryManager::SetPath(std::string path) {
  if (path.empty()) {
    SetPaths({});
  } else {
    SetPaths({std::move(path)});
  }
}

void ExternalDictionaryManager::SetPaths(std::vector<std::string> paths) {
  std::vector<std::string> normalized;
  std::unordered_set<std::string> seen;
  for (std::string& path : paths) {
    if (path.empty() || !seen.insert(path).second) continue;
    // Refuse excessive input rather than spawning arbitrary file reads from
    // damaged/malicious settings; the UI enforces the same 32-file limit.
    if (normalized.size() >= kMaxSources) break;
    normalized.push_back(std::move(path));
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (stopped_) return;
    bool unchanged = normalized.size() == sources_.size();
    if (unchanged) {
      for (size_t i = 0; i < normalized.size(); ++i) {
        if (sources_[i].path != normalized[i]) { unchanged = false; break; }
      }
    }
    if (unchanged) return;
    ++generation_;
    // Preserve verified snapshots for sources that remain enabled. Removed
    // sources disappear immediately, including their previously stale values.
    std::vector<Source> next;
    next.reserve(normalized.size());
    for (const std::string& path : normalized) {
      auto it = std::find_if(sources_.begin(), sources_.end(),
                             [&path](const Source& s) { return s.path == path; });
      if (it != sources_.end()) {
        next.push_back(std::move(*it));
      } else {
        Source source;
        source.path = path;
        next.push_back(std::move(source));
      }
    }
    sources_ = std::move(next);
    RebuildSnapshotLocked();
    RefreshStatusLocked();
  }
  cv_.notify_all();
}

std::shared_ptr<const ExternalDictionaryManager::Snapshot>
ExternalDictionaryManager::GetSnapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return snapshot_;
}

std::vector<std::string> ExternalDictionaryManager::LookupExact(
    absl::string_view key) const {
  const auto snapshot = GetSnapshot();
  const auto it = snapshot->entries.find(std::string(key));
  return it == snapshot->entries.end() ? std::vector<std::string>() : it->second;
}

ExternalDictionaryManager::Status ExternalDictionaryManager::GetStatus() const {
  std::lock_guard<std::mutex> lock(mu_);
  return status_;
}

void ExternalDictionaryManager::RebuildSnapshotLocked() {
  auto merged = std::make_shared<Snapshot>();
  for (const Source& source : sources_) {
    if (!source.good) continue;
    for (const auto& [key, values] : source.good->entries) {
      auto& dest = merged->entries[key];
      for (const std::string& value : values) {
        if (std::find(dest.begin(), dest.end(), value) != dest.end()) continue;
        if (dest.size() >= kMaxValuesPerKey ||
            merged->entry_count >= kMaxEntries) continue;
        dest.push_back(value);
        ++merged->entry_count;
      }
    }
  }
  snapshot_ = std::move(merged);
  status_.entry_count = snapshot_->entry_count;
}

void ExternalDictionaryManager::RefreshStatusLocked() {
  status_.path = sources_.empty() ? "" : sources_.front().path;
  status_.error.clear();
  status_.serving_stale_data = false;
  status_.state = sources_.empty() ? State::kDisabled : State::kReady;
  for (const Source& source : sources_) {
    if (!source.error.empty()) {
      status_.state = State::kError;
      status_.serving_stale_data |= (source.good != nullptr);
      if (!status_.error.empty()) status_.error += "; ";
      status_.error += source.path + ": " + source.error;
    } else if (!source.good && status_.state != State::kError) {
      status_.state = State::kLoading;
    }
  }
}

absl::StatusOr<std::shared_ptr<const ExternalDictionaryManager::Snapshot>>
ExternalDictionaryManager::ParseTsv(absl::string_view contents) {
  if (contents.size() > kMaxFileBytes) {
    return absl::ResourceExhaustedError("External TSV exceeds 16 MiB");
  }
  if (contents.starts_with("\xef\xbb\xbf")) contents.remove_prefix(3);
  auto snapshot = std::make_shared<Snapshot>();
  size_t line_number = 0;
  while (!contents.empty()) {
    ++line_number;
    const size_t end = contents.find('\n');
    absl::string_view line = contents.substr(
        0, end == absl::string_view::npos ? contents.size() : end);
    if (end == absl::string_view::npos) contents = {};
    else contents.remove_prefix(end + 1);
    if (line.ends_with("\r")) line.remove_suffix(1);
    if (line.empty() || line.starts_with("#")) continue;
    const size_t tab = line.find('\t');
    if (tab == absl::string_view::npos ||
        line.find('\t', tab + 1) != absl::string_view::npos) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid TSV column count at line ", line_number));
    }
    const absl::string_view key = line.substr(0, tab);
    const absl::string_view value = line.substr(tab + 1);
    if (!IsValidField(key, kMaxKeyBytes) ||
        !IsValidField(value, kMaxValueBytes)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid TSV field at line ", line_number));
    }
    auto& values = snapshot->entries[std::string(key)];
    const std::string v(value);
    if (std::find(values.begin(), values.end(), v) != values.end()) continue;
    if (values.size() == kMaxValuesPerKey) {
      return absl::ResourceExhaustedError(
          absl::StrCat("Too many values for key at line ", line_number));
    }
    if (snapshot->entry_count == kMaxEntries) {
      return absl::ResourceExhaustedError("Too many external TSV entries");
    }
    values.push_back(v);
    ++snapshot->entry_count;
  }
  return std::shared_ptr<const Snapshot>(std::move(snapshot));
}

absl::Status ExternalDictionaryManager::PollOnceForTesting() {
  return PollOnce(/*force_read=*/true);
}

absl::Status ExternalDictionaryManager::PollOnce(bool force_read) {
  std::lock_guard<std::mutex> probe_lock(poll_mu_);
  uint64_t generation;
  std::vector<Source> next;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (stopped_) return absl::FailedPreconditionError("Manager is stopped");
    if (sources_.empty()) return absl::OkStatus();
    generation = generation_;
    next = sources_;
  }
  absl::Status first_error = absl::OkStatus();
  size_t updated_sources = 0;
  for (Source& source : next) {
    const auto before_time = FileUtil::GetModificationTime(source.path);
    if (!before_time.ok()) {
      source.error = std::string(before_time.status().message());
      if (first_error.ok()) first_error = before_time.status();
      continue;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!force_read && source.error.empty() && source.modified_at.has_value() &&
        *source.modified_at == *before_time && source.has_full_read &&
        now - source.full_read_at < kFullReadInterval) {
      continue;
    }
    const auto contents = FileUtil::GetContents(source.path);
    if (!contents.ok()) {
      source.error = std::string(contents.status().message());
      if (first_error.ok()) first_error = contents.status();
      continue;
    }
    if (contents->size() > kMaxFileBytes) {
      const absl::Status err =
          absl::ResourceExhaustedError("External TSV exceeds 16 MiB");
      source.error = std::string(err.message());
      if (first_error.ok()) first_error = err;
      continue;
    }
    const auto after_time = FileUtil::GetModificationTime(source.path);
    if (!after_time.ok() || *after_time != *before_time) {
      const absl::Status err =
          absl::AbortedError("External TSV changed during read");
      source.error = std::string(err.message());
      if (first_error.ok()) first_error = err;
      continue;
    }
    // Periodic full-byte checks are cheap when the file hasn't changed.
    if (source.has_full_read && source.last_contents &&
        *source.last_contents == *contents) {
      source.modified_at = *after_time;
      source.full_read_at = now;
      source.error.clear();
      continue;
    }
    auto parsed = ParseTsv(*contents);
    if (!parsed.ok()) {
      source.error = std::string(parsed.status().message());
      if (first_error.ok()) first_error = parsed.status();
      continue;
    }
    source.good = std::move(*parsed);
    source.last_contents = std::make_shared<const std::string>(*contents);
    source.modified_at = *after_time;
    source.full_read_at = now;
    source.has_full_read = true;
    source.error.clear();
    ++updated_sources;
  }
  // Publish the entire coherent batch in one commit. If the configuration
  // changed while files were read, discard all results from the old list.
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (stopped_ || generation != generation_) {
      return absl::CancelledError("External TSV sources changed");
    }
    sources_ = std::move(next);
    if (updated_sources != 0) {
      RebuildSnapshotLocked();
      status_.revision += updated_sources;
    }
    RefreshStatusLocked();
  }
  return first_error;
}

void ExternalDictionaryManager::WorkerLoop() {
  std::unique_lock<std::mutex> lock(mu_);
  while (!stopped_) {
    if (sources_.empty()) {
      cv_.wait(lock, [this] { return stopped_ || !sources_.empty(); });
      continue;
    }
    const uint64_t generation = generation_;
    lock.unlock();
    (void)PollOnce(/*force_read=*/false);
    lock.lock();
    cv_.wait_for(lock, poll_interval_,
                 [this, generation] { return stopped_ || generation_ != generation; });
  }
}

void ExternalDictionaryManager::Stop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (stopped_) return;
    stopped_ = true;
    ++generation_;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}
}  // namespace mozc::dictionary
