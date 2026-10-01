// Copyright 2010-2021, Google Inc.
// All rights reserved.

#include "session/zenz_local_correction_store.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <unistd.h>

#include "base/file_stream.h"
#include "base/file_util.h"
#include "base/system_util.h"
#endif

namespace mozc::session {
namespace {

constexpr size_t kMaxPersistedObservations = 4096;
constexpr size_t kMaxFieldBytes = 4096;

// All Mozc Session instances in one server process share the same persistent
// local-correction file. Serialize read-modify-write operations so one Session
// cannot overwrite observations written by another stale store instance.
std::mutex g_local_correction_store_mutex;

bool IsSafeField(absl::string_view value) {
  return !value.empty() && value.size() <= kMaxFieldBytes &&
         value.find('\0') == absl::string_view::npos;
}

std::string EscapeTsv(absl::string_view value) {
  std::string output;
  output.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '\\':
        output.append("\\\\");
        break;
      case '\t':
        output.append("\\t");
        break;
      case '\r':
        output.append("\\r");
        break;
      case '\n':
        output.append("\\n");
        break;
      default:
        output.push_back(c);
        break;
    }
  }
  return output;
}

bool UnescapeTsv(absl::string_view value, std::string* output) {
  if (output == nullptr) {
    return false;
  }
  output->clear();
  output->reserve(value.size());
  bool escaped = false;
  for (const char c : value) {
    if (!escaped) {
      if (c == '\\') {
        escaped = true;
      } else {
        output->push_back(c);
      }
      continue;
    }

    switch (c) {
      case '\\':
        output->push_back('\\');
        break;
      case 't':
        output->push_back('\t');
        break;
      case 'r':
        output->push_back('\r');
        break;
      case 'n':
        output->push_back('\n');
        break;
      default:
        return false;
    }
    escaped = false;
  }
  return !escaped;
}

std::vector<std::string> SplitTab(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  for (const char c : line) {
    if (c == '\t') {
      fields.push_back(std::move(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  fields.push_back(std::move(current));
  return fields;
}

bool ParseRecord(const std::string& line, ZenzLocalCorrectionEntry* record) {
  if (record == nullptr || line.empty()) {
    return false;
  }
  const std::vector<std::string> fields = SplitTab(line);
  if (fields.size() != 4 || fields[0] != "v1") {
    return false;
  }

  ZenzLocalCorrectionEntry parsed;
  if (!UnescapeTsv(fields[1], &parsed.reading) ||
      !UnescapeTsv(fields[2], &parsed.rejected_value) ||
      !UnescapeTsv(fields[3], &parsed.accepted_value) ||
      !IsSafeField(parsed.reading) ||
      !IsSafeField(parsed.rejected_value) ||
      !IsSafeField(parsed.accepted_value)) {
    return false;
  }
  parsed.observation_count = 1;
  *record = std::move(parsed);
  return true;
}

void WriteRecord(const ZenzLocalCorrectionEntry& record,
                 std::ostream* output) {
  *output << "v1" << '\t'
          << EscapeTsv(record.reading) << '\t'
          << EscapeTsv(record.rejected_value) << '\t'
          << EscapeTsv(record.accepted_value) << '\n';
}

#if defined(_WIN32)

std::wstring JoinPath(const std::wstring& lhs, const std::wstring& rhs) {
  if (lhs.empty()) {
    return rhs;
  }
  if (lhs.back() == L'\\') {
    return lhs + rhs;
  }
  return lhs + L"\\" + rhs;
}

bool EnsureDirectory(const std::wstring& path) {
  if (path.empty()) {
    return false;
  }
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES) {
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  }
  if (::CreateDirectoryW(path.c_str(), nullptr)) {
    return true;
  }
  return ::GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring GetStoreDirectory() {
  wchar_t profile[32767] = {};
  const DWORD size =
      ::GetEnvironmentVariableW(L"USERPROFILE", profile, 32767);
  if (size == 0 || size >= 32767) {
    return L"";
  }
  const std::wstring local_low =
      JoinPath(JoinPath(std::wstring(profile, size), L"AppData"), L"LocalLow");
  if (!EnsureDirectory(local_low)) {
    return L"";
  }
  const std::wstring mozc = JoinPath(local_low, L"Mozc");
  if (!EnsureDirectory(mozc)) {
    return L"";
  }
  return mozc;
}

std::wstring GetStorePath() {
  const std::wstring dir = GetStoreDirectory();
  return dir.empty() ? L"" : JoinPath(dir, L"zenz_local_corrections.tsv");
}

std::vector<ZenzLocalCorrectionEntry> LoadRecords() {
  const std::wstring path = GetStorePath();
  if (path.empty()) {
    return {};
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }

  std::vector<ZenzLocalCorrectionEntry> records;
  std::string line;
  while (std::getline(input, line)) {
    ZenzLocalCorrectionEntry record;
    if (ParseRecord(line, &record)) {
      records.push_back(std::move(record));
    }
  }
  return records;
}

bool WriteRecordsAtomically(
    const std::vector<ZenzLocalCorrectionEntry>& records) {
  const std::wstring path = GetStorePath();
  if (path.empty()) {
    return false;
  }

  if (records.empty()) {
    if (::DeleteFileW(path.c_str())) {
      return true;
    }
    const DWORD error = ::GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
  }

  const std::wstring temp =
      path + L".tmp." + std::to_wstring(::GetCurrentProcessId());
  {
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    for (const ZenzLocalCorrectionEntry& record : records) {
      WriteRecord(record, &output);
    }
    output.flush();
    if (!output) {
      ::DeleteFileW(temp.c_str());
      return false;
    }
  }

  if (!::MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ::DeleteFileW(temp.c_str());
    return false;
  }
  return true;
}

#elif defined(__APPLE__)

std::string GetStorePath() {
  const std::string dir = SystemUtil::GetUserProfileDirectory();
  if (dir.empty()) {
    return "";
  }
  if (!FileUtil::DirectoryExists(dir).ok() &&
      !FileUtil::CreateDirectory(dir).ok() &&
      !FileUtil::DirectoryExists(dir).ok()) {
    return "";
  }
  return FileUtil::JoinPath(dir, "zenz_local_corrections.tsv");
}

std::vector<ZenzLocalCorrectionEntry> LoadRecords() {
  const std::string path = GetStorePath();
  if (path.empty()) {
    return {};
  }
  InputFileStream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    return {};
  }

  std::vector<ZenzLocalCorrectionEntry> records;
  std::string line;
  while (std::getline(input, line)) {
    ZenzLocalCorrectionEntry record;
    if (ParseRecord(line, &record)) {
      records.push_back(std::move(record));
    }
  }
  return records;
}

bool WriteRecordsAtomically(
    const std::vector<ZenzLocalCorrectionEntry>& records) {
  const std::string path = GetStorePath();
  if (path.empty()) {
    return false;
  }

  if (records.empty()) {
    return FileUtil::UnlinkIfExists(path).ok();
  }

  const std::string temp =
      path + ".tmp." + std::to_string(static_cast<uint64_t>(::getpid()));
  {
    OutputFileStream output(
        temp, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    for (const ZenzLocalCorrectionEntry& record : records) {
      WriteRecord(record, &output);
    }
    output.flush();
    if (!output) {
      FileUtil::UnlinkOrLogError(temp);
      return false;
    }
  }

  if (!FileUtil::AtomicRename(temp, path).ok()) {
    FileUtil::UnlinkOrLogError(temp);
    return false;
  }
  return true;
}

#else

std::vector<ZenzLocalCorrectionEntry> LoadRecords() { return {}; }

bool WriteRecordsAtomically(
    const std::vector<ZenzLocalCorrectionEntry>&) {
  return false;
}

#endif

}  // namespace

void ZenzLocalCorrectionStore::RecordCorrection(
    absl::string_view reading,
    absl::string_view rejected_value,
    absl::string_view accepted_value) {
  if (!IsSafeField(reading) ||
      !IsSafeField(rejected_value) ||
      !IsSafeField(accepted_value) ||
      rejected_value == accepted_value) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_local_correction_store_mutex);
  std::vector<ZenzLocalCorrectionEntry> records = LoadRecords();
  records.push_back({
      std::string(reading),
      std::string(rejected_value),
      std::string(accepted_value),
      1,
  });

  if (records.size() > kMaxPersistedObservations) {
    records.erase(
        records.begin(),
        records.begin() +
            static_cast<std::ptrdiff_t>(
                records.size() - kMaxPersistedObservations));
  }

  // Persistence is fail-closed for future processes. The current observation
  // is intentionally not cached if the atomic rewrite fails.
  (void)WriteRecordsAtomically(records);
}

std::optional<ZenzLocalCorrectionEntry> ZenzLocalCorrectionStore::Lookup(
    absl::string_view reading,
    absl::string_view rejected_value,
    absl::string_view accepted_value) const {
  std::lock_guard<std::mutex> lock(g_local_correction_store_mutex);
  const std::vector<ZenzLocalCorrectionEntry> records = LoadRecords();

  int count = 0;
  for (const ZenzLocalCorrectionEntry& record : records) {
    if (record.reading == reading &&
        record.rejected_value == rejected_value &&
        record.accepted_value == accepted_value) {
      ++count;
    }
  }

  if (count == 0) {
    return std::nullopt;
  }

  return ZenzLocalCorrectionEntry{
      std::string(reading),
      std::string(rejected_value),
      std::string(accepted_value),
      count,
  };
}

std::vector<ZenzLocalCorrectionEntry>
ZenzLocalCorrectionStore::ListEntries() const {
  std::lock_guard<std::mutex> lock(g_local_correction_store_mutex);
  const std::vector<ZenzLocalCorrectionEntry> records = LoadRecords();

  using Key = std::tuple<std::string, std::string, std::string>;
  std::map<Key, int> counts;
  for (const ZenzLocalCorrectionEntry& record : records) {
    ++counts[Key(record.reading,
                 record.rejected_value,
                 record.accepted_value)];
  }

  std::vector<ZenzLocalCorrectionEntry> entries;
  entries.reserve(counts.size());
  for (const auto& [key, count] : counts) {
    entries.push_back({
        std::get<0>(key),
        std::get<1>(key),
        std::get<2>(key),
        count,
    });
  }
  return entries;
}

bool ZenzLocalCorrectionStore::DeleteEntry(
    absl::string_view reading,
    absl::string_view rejected_value,
    absl::string_view accepted_value) {
  if (!IsSafeField(reading) ||
      !IsSafeField(rejected_value) ||
      !IsSafeField(accepted_value)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_local_correction_store_mutex);
  std::vector<ZenzLocalCorrectionEntry> records = LoadRecords();

  const auto new_end =
      std::remove_if(records.begin(), records.end(),
                     [&](const ZenzLocalCorrectionEntry& record) {
                       return record.reading == reading &&
                              record.rejected_value == rejected_value &&
                              record.accepted_value == accepted_value;
                     });
  if (new_end == records.end()) {
    // Do not rewrite the file when the selected exact triple disappeared
    // between ListEntries() and the user's delete action. This is both a
    // useful stale-selection signal to the UI and avoids destructive writes
    // after an unexpected read failure.
    return false;
  }

  records.erase(new_end, records.end());
  return WriteRecordsAtomically(records);
}

bool ZenzLocalCorrectionStore::ClearAll() {
  std::lock_guard<std::mutex> lock(g_local_correction_store_mutex);
  return WriteRecordsAtomically({});
}

}  // namespace mozc::session