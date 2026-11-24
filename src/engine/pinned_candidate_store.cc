// Copyright 2026 Mozkey contributors.
#include "engine/pinned_candidate_store.h"

#include <cerrno>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "base/file_util.h"
#include "base/system_util.h"

namespace mozc::engine {
namespace {

constexpr size_t kMaxFieldBytes = 4096;
constexpr size_t kMaxEntries = 4096;
constexpr size_t kMaxFileBytes = 8 * 1024 * 1024;

// Each mutation holds both the instance mutex AND an OS-level lock from
// reload through atomic rename. A lock file persists across process restarts;
// never unlink it (unlinking would permit distinct lock inodes on POSIX).
// External tools that do not participate in this protocol are not protected.
class ScopedPinFileLock final {
 public:
  explicit ScopedPinFileLock(const std::string& path) {
    if (path.empty()) return;
    const std::string lock_path = path + ".lock";
#ifdef _WIN32
    const int chars = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, lock_path.data(),
        static_cast<int>(lock_path.size()), nullptr, 0);
    if (chars <= 0) return;
    std::wstring wide_path(chars, L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                              lock_path.data(),
                              static_cast<int>(lock_path.size()),
                              wide_path.data(), chars) != chars) {
      return;
    }
    handle_ = ::CreateFileW(wide_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) return;
    locked_ = ::LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0,
                           MAXDWORD, MAXDWORD, &overlapped_) != 0;
#else
    fd_ = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd_ < 0) return;
    while (::flock(fd_, LOCK_EX) < 0) {
      if (errno != EINTR) return;
    }
    locked_ = true;
#endif
  }
  ScopedPinFileLock(const ScopedPinFileLock&) = delete;
  ScopedPinFileLock& operator=(const ScopedPinFileLock&) = delete;
  ~ScopedPinFileLock() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) {
      if (locked_) {
        ::UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped_);
      }
      ::CloseHandle(handle_);
    }
#else
    if (fd_ >= 0) {
      if (locked_) ::flock(fd_, LOCK_UN);
      ::close(fd_);
    }
#endif
  }
  bool locked() const { return locked_; }

 private:
  bool locked_ = false;
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  OVERLAPPED overlapped_ = {};
#else
  int fd_ = -1;
#endif
};

bool IsValidField(absl::string_view field) {
  if (field.empty() || field.size() > kMaxFieldBytes) return false;
  for (const unsigned char c : field) {
    if (c < 0x20 || c == 0x7f) return false;
  }
  return true;
}

}  // namespace

PinnedCandidateStore::PinnedCandidateStore(std::string path)
    : path_(std::move(path)) {}

PinnedCandidateStore& PinnedCandidateStore::Instance() {
  static PinnedCandidateStore* const store = [] {
    const std::string profile = SystemUtil::GetUserProfileDirectory();
    return new PinnedCandidateStore(
        profile.empty() ? std::string()
                        : FileUtil::JoinPath(profile, "pinned_candidates.tsv"));
  }();
  return *store;
}

bool PinnedCandidateStore::LoadLocked() {
  if (loaded_) return true;
  if (path_.empty()) return false;

  // A genuinely absent file is the only acceptable empty initial state.
  // In particular, do not use a failed GetContents() as evidence of absence:
  // permission errors and transient I/O failures must not erase old pins.
  const absl::Status exists = FileUtil::FileExists(path_);
  if (absl::IsNotFound(exists)) {
    entries_.clear();
    loaded_ = true;
    return true;
  }
  if (!exists.ok()) return false;

  // FileExists() also succeeds for directories on POSIX. A directory must
  // not be passed into GetContents(), which expects a regular file stream.
  // Fail closed on unexpected directory-status errors as well.
  const absl::Status directory = FileUtil::DirectoryExists(path_);
  if (directory.ok() || !absl::IsNotFound(directory)) return false;

  const auto file = FileUtil::GetContents(path_);
  if (!file.ok() || file->size() > kMaxFileBytes) return false;

  // Parse transactionally. An invalid row, duplicate key, or excess entry
  // count makes the *whole* file unusable; keep the original bytes intact.
  std::map<std::string, std::string> parsed;
  size_t begin = 0;
  while (begin < file->size()) {
    const size_t end = file->find('\n', begin);
    const size_t length = (end == std::string::npos ? file->size() : end) - begin;
    const absl::string_view row(file->data() + begin, length);
    const size_t tab = row.find('\t');
    if (tab == absl::string_view::npos ||
        row.find('\t', tab + 1) != absl::string_view::npos ||
        parsed.size() >= kMaxEntries) {
      return false;
    }
    const absl::string_view reading = row.substr(0, tab);
    const absl::string_view value = row.substr(tab + 1);
    if (!IsValidField(reading) || !IsValidField(value) ||
        !parsed.emplace(std::string(reading), std::string(value)).second) {
      return false;
    }
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  entries_ = std::move(parsed);
  loaded_ = true;
  return true;
}

bool PinnedCandidateStore::SaveLocked(
    const std::map<std::string, std::string>& entries) {
  if (path_.empty() || entries.size() > kMaxEntries) return false;
  std::string content;
  for (const auto& [reading, value] : entries) {
    content.append(reading).append("\t").append(value).append("\n");
  }
  if (content.size() > kMaxFileBytes) return false;
  const std::string temp = path_ + ".tmp";
  if (!FileUtil::SetContents(temp, content).ok()) {
    FileUtil::UnlinkOrLogError(temp);
    return false;
  }
  if (!FileUtil::AtomicRename(temp, path_).ok()) {
    FileUtil::UnlinkOrLogError(temp);
    return false;
  }
  return true;
}

std::optional<std::string> PinnedCandidateStore::Lookup(
    absl::string_view reading) {
  if (!IsValidField(reading)) return std::nullopt;
  std::lock_guard<std::mutex> lock(mu_);
  if (!LoadLocked()) return std::nullopt;
  auto it = entries_.find(std::string(reading));
  if (it == entries_.end()) return std::nullopt;
  return it->second;
}

bool PinnedCandidateStore::Pin(absl::string_view reading,
                               absl::string_view value) {
  if (!IsValidField(reading) || !IsValidField(value)) return false;
  std::lock_guard<std::mutex> lock(mu_);
  // Serialize cooperating processes through the entire read-modify-rename.
  // A thread mutex or a unique temp filename alone cannot prevent lost edits.
  const ScopedPinFileLock file_lock(path_);
  if (!file_lock.locked()) return false;
  loaded_ = false;
  if (!LoadLocked()) return false;
  std::map<std::string, std::string> updated = entries_;
  updated[std::string(reading)] = std::string(value);
  if (!SaveLocked(updated)) return false;
  entries_ = std::move(updated);
  return true;
}

bool PinnedCandidateStore::Unpin(absl::string_view reading,
                                 absl::string_view expected_value) {
  if (!IsValidField(reading) || !IsValidField(expected_value)) return false;
  std::lock_guard<std::mutex> lock(mu_);
  const ScopedPinFileLock file_lock(path_);
  if (!file_lock.locked()) return false;
  loaded_ = false;
  if (!LoadLocked()) return false;
  auto it = entries_.find(std::string(reading));
  if (it == entries_.end() || it->second != expected_value) return false;
  std::map<std::string, std::string> updated = entries_;
  updated.erase(std::string(reading));
  if (!SaveLocked(updated)) return false;
  entries_ = std::move(updated);
  return true;
}

}  // namespace mozc::engine
