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

// An immutable, exact-match overlay on top of the normal Mozc conversion.
// One watcher handles all paths; no filesystem access occurs in LookupExact.
class ExternalDictionaryManager final {
 public:
  struct Snapshot {
    std::unordered_map<std::string, std::vector<std::string>> entries;
    size_t entry_count = 0;
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
  std::shared_ptr<const Snapshot> GetSnapshot() const;
  std::vector<std::string> LookupExact(absl::string_view key) const;
  Status GetStatus() const;
  absl::Status PollOnceForTesting();
  void Stop();

 private:
  struct Source {
    std::string path;
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
  std::shared_ptr<const Snapshot> snapshot_;
};

}  // namespace mozc::dictionary
#endif  // MOZC_DICTIONARY_EXTERNAL_DICTIONARY_MANAGER_H_
