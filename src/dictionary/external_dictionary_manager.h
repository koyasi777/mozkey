// Copyright 2026 Mozkey contributors.
#ifndef MOZC_DICTIONARY_EXTERNAL_DICTIONARY_MANAGER_H_
#define MOZC_DICTIONARY_EXTERNAL_DICTIONARY_MANAGER_H_

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "base/file_util.h"

namespace mozc::dictionary {

// An immutable overlay on top of normal conversion. Prefix matching is
// opt-in per source; neither lookup performs filesystem access.
class ExternalDictionaryManager final {
 public:
  struct SourceSpec {
    std::string path;
    bool allow_prefix = false;
  };

  struct Snapshot {
    struct PrefixEntry {
      std::string key;
      // Indices into entries[key], in prefix-enabled source order.
      std::vector<size_t> value_indices;
    };
    std::unordered_map<std::string, std::vector<std::string>> entries;
    size_t entry_count = 0;
    // Sorted by UTF-8 byte order. Only admitted pairs from prefix-enabled
    // sources are indexed; values are not copied into a second dictionary.
    std::vector<PrefixEntry> prefix_entries;

    // Strict prefix only, at least three Unicode code points. Excludes values
    // already available by exact match. Bounded to ten unique results and
    // 256 inspected value references per query.
    std::vector<std::string> LookupPrefix(absl::string_view key) const;
  };

  enum class State { kDisabled, kLoading, kReady, kError };
  struct Status {
    State state = State::kDisabled;
    // Preserved for consumers of the original single-path API.
    std::string path;
    std::string error;
    uint64_t revision = 0;
    size_t entry_count = 0;
    bool serving_stale_data = false;
  };

  explicit ExternalDictionaryManager(
      std::chrono::milliseconds poll_interval = std::chrono::seconds(3),
      bool start_worker = true);
  ExternalDictionaryManager(const ExternalDictionaryManager&) = delete;
  ExternalDictionaryManager& operator=(const ExternalDictionaryManager&) = delete;
  ~ExternalDictionaryManager();

  // Backward-compatible entry point for existing callers and tests.
  void SetPath(std::string path);
  // Ordering is intentional: earlier files' values precede later files'.
  // Duplicate paths are ignored; an empty list disables the overlay.
  void SetPaths(std::vector<std::string> paths);
  void SetSources(std::vector<SourceSpec> sources);
  std::shared_ptr<const Snapshot> GetSnapshot() const;
  std::vector<std::string> LookupExact(absl::string_view key) const;
  Status GetStatus() const;
  absl::Status PollOnceForTesting();
  void Stop();

 private:
  struct Source {
    std::string path;
    bool allow_prefix = false;
    std::shared_ptr<const Snapshot> good;
    std::optional<FileTimeStamp> modified_at;
    std::chrono::steady_clock::time_point full_read_at{};
    bool has_full_read = false;
    std::shared_ptr<const std::string> last_contents;
    std::string error;
  };

  void WorkerLoop();
  absl::Status PollOnce(bool force_read);
  void RebuildSnapshotLocked();
  void ReclaimRetiredSnapshotsLocked();
  void RefreshStatusLocked();
  static absl::StatusOr<std::shared_ptr<const Snapshot>> ParseTsv(
      absl::string_view contents);

  const std::chrono::milliseconds poll_interval_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::mutex poll_mu_;
  std::thread worker_;
  bool stopped_ = false;
  uint64_t generation_ = 0;
  std::vector<Source> sources_;
  Status status_;
  // Lock order: mu_ (writer state) before snapshot_mu_ (publication).
  // Readers take only snapshot_mu_; no rebuilding or destruction under it.
  mutable std::mutex snapshot_mu_;
  std::shared_ptr<const Snapshot> snapshot_;
  // Protected by mu_. Retain displaced snapshots until no reader owns them.
  // Only the manager obtains new strong references to a published snapshot;
  // retired snapshots cannot be acquired via GetSnapshot().
  std::vector<std::shared_ptr<const Snapshot>> retired_snapshots_;
};

}  // namespace mozc::dictionary
#endif  // MOZC_DICTIONARY_EXTERNAL_DICTIONARY_MANAGER_H_
