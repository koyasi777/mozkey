#include "session/zenz_feedback_store.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <istream>
#include <map>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace mozc {
namespace session {
namespace {

constexpr int kAcceptThreshold = 1;

// Stored records are full-sequence observations for one Zenz request/response
// pair.  Do not add segment-local or lexical-unit records here; those should be
// learned through Mozc history so phrase-boundary and dictionary semantics stay
// owned by the converter/history layers.
//
// Feedback is interpreted as a ranking signal.  Accepted feedback is a strong
// positive observation.  Rejected feedback is usually a weak or medium negative
// observation: Space after a visible Zenz result often means "show me the
// normal candidates now", not "never show this candidate again".
constexpr int kAcceptedFeedbackWeight = 1000;
constexpr int kSpaceRevertRejectWeight = 150;
constexpr int kPredictAfterZenzRejectWeight = 200;
constexpr int kExplicitConversionRejectWeight = 400;
constexpr int kLegacyRejectWeight = 400;
constexpr int kHardRejectWeight = 2000;

#if defined(_WIN32)

std::wstring Utf8ToWide(absl::string_view s) {
  if (s.empty()) {
    return std::wstring();
  }

  const int input_size = static_cast<int>(s.size());
  const int wide_size =
      ::MultiByteToWideChar(CP_UTF8, 0, s.data(), input_size, nullptr, 0);
  if (wide_size <= 0) {
    return L"<invalid utf8>";
  }

  std::wstring w(wide_size, L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), input_size, w.data(), wide_size);
  return w;
}

std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) {
    return "";
  }

  const int utf8_size = ::WideCharToMultiByte(
      CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr,
      nullptr);
  if (utf8_size <= 0) {
    return "";
  }

  std::string s(utf8_size, '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        s.data(), utf8_size, nullptr, nullptr);
  return s;
}

void StoreDebugOutputWide(const std::wstring& message) {
  std::wstring line = L"[zenz-feedback-store] ";
  line.append(message);
  line.push_back(L'\n');
  ::OutputDebugStringW(line.c_str());
}

void StoreDebugOutput(absl::string_view message) {
  StoreDebugOutputWide(Utf8ToWide(message));
}

std::wstring RedactedWidePathStats(const wchar_t* label,
                                   const std::wstring& path) {
  std::wstring output(label);
  output.append(L"_chars=");
  output.append(std::to_wstring(path.size()));
  return output;
}

bool EnsureDirectoryExists(const std::wstring& dir);
bool IsWritableDirectory(const std::wstring& dir);

std::wstring GetUserProfileDir() {
  wchar_t buffer[MAX_PATH] = {};
  const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return L"";
  }
  return std::wstring(buffer, n);
}

std::wstring GetLocalLowAppDataDir() {
  const std::wstring user_profile = GetUserProfileDir();
  if (user_profile.empty()) {
    return L"";
  }
  return user_profile + L"\\AppData\\LocalLow";
}

bool IsWritableDirectory(const std::wstring& dir) {
  if (dir.empty()) {
    return false;
  }

  const DWORD attr = ::GetFileAttributesW(dir.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES ||
      !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    StoreDebugOutputWide(
        std::wstring(L"writable probe failed: not directory ")
            .append(RedactedWidePathStats(L"dir", dir)));
    return false;
  }

  const std::wstring probe_path =
      dir + L"\\mozc_zenz_feedback_probe_" +
      std::to_wstring(::GetCurrentProcessId()) + L".tmp";

  HANDLE file = ::CreateFileW(
      probe_path.c_str(),
      GENERIC_WRITE,
      0,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
      nullptr);

  if (file == INVALID_HANDLE_VALUE) {
    const DWORD error = ::GetLastError();
    StoreDebugOutputWide(
      std::wstring(L"writable probe failed error=")
          .append(std::to_wstring(error))
          .append(L" ")
          .append(RedactedWidePathStats(L"dir", dir))
          .append(L" ")
          .append(RedactedWidePathStats(L"probe", probe_path)));
    return false;
  }

  ::CloseHandle(file);

  StoreDebugOutputWide(
    std::wstring(L"writable probe ok ")
        .append(RedactedWidePathStats(L"dir", dir)));
  return true;
}

std::wstring GetFeedbackDirWide() {
  const std::wstring local_low = GetLocalLowAppDataDir();
  if (local_low.empty()) {
    StoreDebugOutputWide(L"LocalLow path unavailable");
    return L"";
  }

  const std::wstring dir = local_low + L"\\Mozc";

  if (!EnsureDirectoryExists(dir)) {
    StoreDebugOutputWide(
      std::wstring(L"LocalLow Mozc dir cannot be created ")
          .append(RedactedWidePathStats(L"dir", dir)));
    return L"";
  }

  if (!IsWritableDirectory(dir)) {
    StoreDebugOutputWide(
      std::wstring(L"LocalLow Mozc dir not writable ")
          .append(RedactedWidePathStats(L"dir", dir)));
    return L"";
  }

  StoreDebugOutputWide(
    std::wstring(L"selected LocalLow Mozc dir ")
        .append(RedactedWidePathStats(L"dir", dir)));
  return dir;
}

std::wstring GetFeedbackPathWideFromDir(const std::wstring& dir) {
  if (dir.empty()) {
    return L"";
  }

  return dir + L"\\zenz_feedback.tsv";
}

std::wstring GetFeedbackPathWide() {
  return GetFeedbackPathWideFromDir(GetFeedbackDirWide());
}

bool EnsureDirectoryExists(const std::wstring& dir) {
  if (dir.empty()) {
    StoreDebugOutputWide(L"directory path is empty");
    return false;
  }

  const DWORD attr = ::GetFileAttributesW(dir.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES) {
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
      StoreDebugOutputWide(
        std::wstring(L"directory already exists ")
            .append(RedactedWidePathStats(L"dir", dir)));
      return true;
    }
    StoreDebugOutputWide(
      std::wstring(L"path exists but is not directory ")
          .append(RedactedWidePathStats(L"dir", dir)));
    return false;
  }

  if (::CreateDirectoryW(dir.c_str(), nullptr)) {
    StoreDebugOutputWide(
      std::wstring(L"CreateDirectoryW ok ")
          .append(RedactedWidePathStats(L"dir", dir)));
    return true;
  }

  const DWORD error = ::GetLastError();
  if (error == ERROR_ALREADY_EXISTS) {
    StoreDebugOutputWide(
      std::wstring(L"CreateDirectoryW already exists ")
          .append(RedactedWidePathStats(L"dir", dir)));
    return true;
  }

  StoreDebugOutputWide(
    std::wstring(L"CreateDirectoryW failed error=")
        .append(std::to_wstring(error))
        .append(L" ")
        .append(RedactedWidePathStats(L"dir", dir)));
  return false;
}

#else

void StoreDebugOutput(absl::string_view) {}

#endif

std::string EscapeTsv(absl::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '\t':
        out.append("\\t");
        break;
      case '\r':
        out.append("\\r");
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\\':
        out.append("\\\\");
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

std::vector<std::string> SplitTab(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;

  for (char c : line) {
    if (c == '\t') {
      fields.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }

  fields.push_back(current);
  return fields;
}

void StripUtf8BomFromFirstField(std::vector<std::string>* fields) {
  if (fields == nullptr || fields->empty()) {
    return;
  }

  std::string& first = (*fields)[0];
  constexpr absl::string_view kUtf8Bom = "\xEF\xBB\xBF";
  if (absl::StartsWith(first, kUtf8Bom)) {
    first.erase(0, kUtf8Bom.size());
  }
}

std::string RedactedStats(absl::string_view label, absl::string_view text) {
  return absl::StrCat(label, "_bytes=", text.size());
}

std::string UnescapeTsv(absl::string_view s) {
  std::string out;
  out.reserve(s.size());

  bool escaping = false;
  for (const char c : s) {
    if (!escaping) {
      if (c == '\\') {
        escaping = true;
      } else {
        out.push_back(c);
      }
      continue;
    }

    switch (c) {
      case 't':
        out.push_back('\t');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case '\\':
        out.push_back('\\');
        break;
      default:
        out.push_back(c);
        break;
    }
    escaping = false;
  }

  if (escaping) {
    out.push_back('\\');
  }

  return out;
}

bool ContainsUnsafeTsvTextChar(absl::string_view s) {
  for (const char c : s) {
    if (c == '\t' || c == '\r' || c == '\n') {
      return true;
    }
  }
  return false;
}

#if defined(_WIN32)
bool IsValidUtf8ForFeedback(absl::string_view s) {
  if (s.empty()) {
    return true;
  }

  const int input_size = static_cast<int>(s.size());
  const int wide_size = ::MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), input_size, nullptr, 0);
  return wide_size > 0;
}
#else
bool IsValidUtf8ForFeedback(absl::string_view) {
  return true;
}
#endif

bool IsKnownContextClass(absl::string_view context_class) {
  return context_class == "empty" ||
         context_class == "japanese_only" ||
         context_class == "japanese_with_punctuation" ||
         context_class == "mixed_japanese_ascii" ||
         context_class == "symbol_or_other" ||
         context_class == "ascii_or_digit" ||
         context_class == "sensitive_like" ||
         context_class == "legacy";
}

std::string NormalizeContextClass(absl::string_view context_class) {
  return context_class.empty() ? std::string("empty")
                               : std::string(context_class);
}

struct ParsedFeedbackRecord {
  std::string action;
  std::string key;
  std::string context_class;
  std::string value;
  std::string reason;
};

bool IsSafeFeedbackRecord(const ParsedFeedbackRecord& record) {
  if (record.action != "accepted" && record.action != "rejected") {
    return false;
  }

  if (record.key.empty() || record.value.empty()) {
    return false;
  }

  if (!IsKnownContextClass(record.context_class)) {
    return false;
  }

  constexpr size_t kMaxKeyBytes = 512;
  constexpr size_t kMaxContextClassBytes = 64;
  constexpr size_t kMaxValueBytes = 512;
  constexpr size_t kMaxReasonBytes = 128;

  if (record.key.size() > kMaxKeyBytes ||
      record.context_class.size() > kMaxContextClassBytes ||
      record.value.size() > kMaxValueBytes ||
      record.reason.size() > kMaxReasonBytes) {
    return false;
  }

  if (ContainsUnsafeTsvTextChar(record.action) ||
      ContainsUnsafeTsvTextChar(record.key) ||
      ContainsUnsafeTsvTextChar(record.context_class) ||
      ContainsUnsafeTsvTextChar(record.value) ||
      ContainsUnsafeTsvTextChar(record.reason)) {
    return false;
  }

  return IsValidUtf8ForFeedback(record.key) &&
         IsValidUtf8ForFeedback(record.context_class) &&
         IsValidUtf8ForFeedback(record.value) &&
         IsValidUtf8ForFeedback(record.reason);
}

bool ParseFeedbackRecord(const std::vector<std::string>& fields,
                         ParsedFeedbackRecord* record) {
  if (record == nullptr) {
    return false;
  }

  // v2:
  //   v2  accepted|rejected  key  context_class  value  reason
  //
  // The key/value fields are full-sequence reading/correction pairs.  The TSV
  // intentionally has no columns for segment-local evidence or raw left
  // context.  Context is persisted only as a coarse non-reversible class.
  if (fields.size() >= 5 && fields[0] == "v2") {
    ParsedFeedbackRecord parsed;
    parsed.action = UnescapeTsv(fields[1]);
    parsed.key = UnescapeTsv(fields[2]);
    parsed.context_class =
        NormalizeContextClass(UnescapeTsv(fields[3]));
    parsed.value = UnescapeTsv(fields[4]);
    parsed.reason = fields.size() >= 6 ? UnescapeTsv(fields[5]) : "";

    if (!IsSafeFeedbackRecord(parsed)) {
      return false;
    }

    *record = std::move(parsed);
    return true;
  }

  // v1 legacy:
  //   accepted|rejected  key  context  value  reason
  //
  // The v1 context field may contain raw or reversible left context.  Never use
  // it as a lookup key after the privacy migration.  Keep only a coarse legacy
  // bucket so old feedback can still influence non-contextual decisions without
  // preserving or comparing raw context.
  if (fields.size() >= 4 &&
      (fields[0] == "accepted" || fields[0] == "rejected")) {
    ParsedFeedbackRecord parsed;
    parsed.action = UnescapeTsv(fields[0]);
    parsed.key = UnescapeTsv(fields[1]);
    parsed.context_class = "legacy";
    parsed.value = UnescapeTsv(fields[3]);
    parsed.reason = fields.size() >= 5 ? UnescapeTsv(fields[4]) : "";

    if (!IsSafeFeedbackRecord(parsed)) {
      return false;
    }

    *record = std::move(parsed);
    return true;
  }

  return false;
}

struct Counts {
  int accepted = 0;
  int rejected = 0;
  int auto_block_rejected = 0;
  int positive_score = 0;
  int negative_score = 0;
  bool hard_rejected = false;
};

int TotalScore(const Counts& c) {
  return c.positive_score - c.negative_score;
}

bool IsHardRejectReason(absl::string_view reason) {
  return reason == "hard_reject" ||
         reason == "user_hard_reject" ||
         reason == "manual_hard_reject" ||
         reason == "explicit_hard_reject";
}

int RejectWeightForReason(absl::string_view reason) {
  if (IsHardRejectReason(reason)) {
    return kHardRejectWeight;
  }
  if (reason == "space_revert_zenz_to_mozc" ||
      reason == "auto_block_shadow_compare") {
    return kSpaceRevertRejectWeight;
  }
  if (reason == "predict_after_zenz") {
    return kPredictAfterZenzRejectWeight;
  }
  if (reason == "explicit_conversion_after_zenz") {
    return kExplicitConversionRejectWeight;
  }
  return kLegacyRejectWeight;
}

void AddAccepted(Counts* c) {
  ++c->accepted;
  c->positive_score += kAcceptedFeedbackWeight;
}

void AddRejected(absl::string_view reason, Counts* c) {
  ++c->rejected;
  c->negative_score += RejectWeightForReason(reason);
  if (!IsHardRejectReason(reason)) {
    ++c->auto_block_rejected;
  }
  c->hard_rejected |= IsHardRejectReason(reason);
}

void MergeCounts(const Counts& src, Counts* dest) {
  dest->accepted += src.accepted;
  dest->rejected += src.rejected;
  dest->auto_block_rejected += src.auto_block_rejected;
  dest->positive_score += src.positive_score;
  dest->negative_score += src.negative_score;
  dest->hard_rejected |= src.hard_rejected;
}

ZenzFeedbackAutoBlockPolicy NormalizeAutoBlockPolicy(
    ZenzFeedbackAutoBlockPolicy policy) {
  if (!policy.enabled || policy.minimum_reject_count <= 0 ||
      policy.minimum_reject_percentage <= 0) {
    return ZenzFeedbackAutoBlockPolicy();
  }
  policy.minimum_reject_percentage =
      std::min(policy.minimum_reject_percentage, 100);
  return policy;
}

int AutoBlockRejectPercentage(const Counts& c) {
  const int64_t accepted = c.accepted;
  const int64_t rejected = c.auto_block_rejected;
  const int64_t observations = accepted + rejected;
  if (observations <= 0) {
    return 0;
  }
  return static_cast<int>((rejected * 100) / observations);
}

bool IsAutoBlockedByPolicy(
    const Counts& c,
    const ZenzFeedbackAutoBlockPolicy& auto_block_policy) {
  const ZenzFeedbackAutoBlockPolicy policy =
      NormalizeAutoBlockPolicy(auto_block_policy);
  if (!policy.enabled ||
      c.auto_block_rejected < policy.minimum_reject_count) {
    return false;
  }

  const int64_t accepted = c.accepted;
  const int64_t rejected = c.auto_block_rejected;
  const int64_t observations = accepted + rejected;
  return observations > 0 &&
         rejected * 100 >=
             static_cast<int64_t>(policy.minimum_reject_percentage) *
                 observations;
}

bool IsRejectCountDominant(const Counts& c) {
  return c.auto_block_rejected > c.accepted;
}

void ApplyRejectDominanceDecision(const Counts& exact_counts,
                                  ZenzFeedbackDecision* decision) {
  if (decision == nullptr || decision->hard_rejected ||
      decision->auto_blocked ||
      decision->action != ZenzFeedbackAction::kPrefer) {
    return;
  }

  decision->auto_block_reject_count = exact_counts.auto_block_rejected;
  if (!IsRejectCountDominant(exact_counts)) {
    return;
  }

  decision->action = ZenzFeedbackAction::kNeutral;
  decision->reason = "feedback_reject_count_dominant";
}

void ApplyAutoBlockDecision(
    const Counts& exact_counts,
    const ZenzFeedbackAutoBlockPolicy& auto_block_policy,
    ZenzFeedbackDecision* decision) {
  if (decision == nullptr || decision->hard_rejected) {
    return;
  }

  decision->auto_block_reject_count = exact_counts.auto_block_rejected;
  if (!IsAutoBlockedByPolicy(exact_counts, auto_block_policy)) {
    return;
  }

  decision->action = ZenzFeedbackAction::kReject;
  decision->reason = "feedback_auto_blocked";
  decision->auto_blocked = true;
}

using FeedbackKey = std::tuple<std::string, std::string, std::string>;

#if defined(_WIN32)
struct FeedbackFileStamp {
  bool exists = false;
  FILETIME last_write_time = {};
  uint64_t file_size = 0;
};

struct FeedbackRecordsCache {
  bool valid = false;
  std::wstring path;
  FeedbackFileStamp stamp;
  std::vector<ParsedFeedbackRecord> records;
};

std::mutex g_feedback_records_cache_mutex;
FeedbackRecordsCache g_feedback_records_cache;

bool SameFileTime(const FILETIME& lhs, const FILETIME& rhs) {
  return ::CompareFileTime(&lhs, &rhs) == 0;
}

bool SameFeedbackFileStamp(const FeedbackFileStamp& lhs,
                           const FeedbackFileStamp& rhs) {
  return lhs.exists == rhs.exists &&
         lhs.file_size == rhs.file_size &&
         SameFileTime(lhs.last_write_time, rhs.last_write_time);
}

FeedbackFileStamp GetFeedbackFileStamp(const std::wstring& path) {
  FeedbackFileStamp stamp;

  if (path.empty()) {
    return stamp;
  }

  WIN32_FILE_ATTRIBUTE_DATA data = {};
  if (!::GetFileAttributesExW(
          path.c_str(), GetFileExInfoStandard, &data)) {
    return stamp;
  }

  stamp.exists = true;
  stamp.last_write_time = data.ftLastWriteTime;
  stamp.file_size =
      (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
      static_cast<uint64_t>(data.nFileSizeLow);
  return stamp;
}

void InvalidateFeedbackRecordsCache() {
  std::lock_guard<std::mutex> lock(g_feedback_records_cache_mutex);
  g_feedback_records_cache = FeedbackRecordsCache();
}
#else
void InvalidateFeedbackRecordsCache() {}
#endif

bool IsSensitiveFeedbackContextClass(absl::string_view context_class) {
  return context_class == "sensitive_like";
}

bool IsSharedPromotionContextClass(absl::string_view context_class) {
  // These buckets do not contain raw left context.  They are coarse classes
  // only, so feedback learned in one safe text context can be reused in normal
  // conversion.  This is important for cases such as:
  //
  //   learned: key + japanese_only
  //   lookup:  key + empty / symbol_or_other
  //
  // The correction itself is key/value feedback and should not be lost merely
  // because the later conversion has no preceding text or because the preceding
  // text was rejected as non-Japanese context.
  return context_class == "empty" ||
         context_class == "japanese_only" ||
         context_class == "japanese_with_punctuation" ||
         context_class == "mixed_japanese_ascii" ||
         context_class == "symbol_or_other" ||
         context_class == "legacy";
}

bool IsPromotionCompatibleContextClass(
    absl::string_view requested_context_class,
    absl::string_view record_context_class) {
  // Exact bucket reuse is allowed even for sensitive_like.
  //
  // This does not expose or compare raw left context.  It only reuses the
  // non-reversible context class already stored in feedback TSV.
  //
  // Important:
  //   sensitive_like -> sensitive_like is allowed for live fast path.
  //   sensitive_like -> normal context is still forbidden.
  //   normal context -> sensitive_like is still forbidden.
  if (requested_context_class == record_context_class) {
    return true;
  }

  if (IsSensitiveFeedbackContextClass(requested_context_class) ||
      IsSensitiveFeedbackContextClass(record_context_class)) {
    return false;
  }

  if (IsSharedPromotionContextClass(requested_context_class) &&
      IsSharedPromotionContextClass(record_context_class)) {
    return true;
  }

  return false;
}

bool IsDecisionCompatibleContextClass(
    absl::string_view requested_context_class,
    absl::string_view record_context_class) {
  if (requested_context_class == record_context_class) {
    return true;
  }

  if (IsSensitiveFeedbackContextClass(requested_context_class) ||
      IsSensitiveFeedbackContextClass(record_context_class)) {
    return false;
  }

  if (IsSharedPromotionContextClass(requested_context_class) &&
      IsSharedPromotionContextClass(record_context_class)) {
    return true;
  }

  return false;
}

ZenzFeedbackDecision BuildDecisionFromCounts(const Counts& c) {
  ZenzFeedbackDecision decision;
  decision.accepted_count = c.accepted;
  decision.rejected_count = c.rejected;
  decision.auto_block_reject_count = c.auto_block_rejected;
  decision.positive_score = c.positive_score;
  decision.negative_score = c.negative_score;
  decision.total_score = TotalScore(c);
  decision.hard_rejected = c.hard_rejected;

  if (c.hard_rejected) {
    decision.action = ZenzFeedbackAction::kReject;
    decision.reason = "feedback_hard_rejected";
    return decision;
  }

  if (c.accepted >= kAcceptThreshold && decision.total_score > 0) {
    decision.action = ZenzFeedbackAction::kPrefer;
    decision.reason = "feedback_preferred";
    return decision;
  }

  decision.action = ZenzFeedbackAction::kNeutral;
  decision.reason = c.rejected > 0 ? "feedback_downgraded"
                                   : "feedback_neutral";
  return decision;
}

void WriteRecordToStream(const ParsedFeedbackRecord& record,
                         std::ostream* output) {
  *output << "v2" << '\t'
          << EscapeTsv(record.action) << '\t'
          << EscapeTsv(record.key) << '\t'
          << EscapeTsv(NormalizeContextClass(record.context_class)) << '\t'
          << EscapeTsv(record.value) << '\t'
          << EscapeTsv(record.reason) << '\n';
}

bool WriteRecordsToStream(const std::vector<ParsedFeedbackRecord>& records,
                          std::ostream* output) {
  if (output == nullptr) {
    return false;
  }

  for (const ParsedFeedbackRecord& record : records) {
    if (!IsSafeFeedbackRecord(record)) {
      return false;
    }
    WriteRecordToStream(record, output);
  }

  output->flush();
  return static_cast<bool>(*output);
}

bool LoadRecordsFromStream(std::istream* input,
                           bool strict,
                           std::vector<ParsedFeedbackRecord>* records) {
  if (input == nullptr || records == nullptr) {
    return false;
  }

  records->clear();

  bool ok = true;
  std::string line;
  while (std::getline(*input, line)) {
    if (line.empty()) {
      continue;
    }

    std::vector<std::string> fields = SplitTab(line);
    StripUtf8BomFromFirstField(&fields);

    ParsedFeedbackRecord record;
    if (!ParseFeedbackRecord(fields, &record)) {
      ok = false;
      if (strict) {
        return false;
      }
      continue;
    }

    records->push_back(std::move(record));
  }

  return ok || !strict;
}

std::vector<ParsedFeedbackRecord> LoadFeedbackRecords() {
#if defined(_WIN32)
  const std::wstring path_w = GetFeedbackPathWide();
  const FeedbackFileStamp stamp = GetFeedbackFileStamp(path_w);

  StoreDebugOutputWide(
      std::wstring(L"load feedback file ")
          .append(RedactedWidePathStats(L"path", path_w)));

  std::lock_guard<std::mutex> lock(g_feedback_records_cache_mutex);

  if (g_feedback_records_cache.valid &&
      g_feedback_records_cache.path == path_w &&
      SameFeedbackFileStamp(g_feedback_records_cache.stamp, stamp)) {
    return g_feedback_records_cache.records;
  }

  std::vector<ParsedFeedbackRecord> records;

  if (path_w.empty()) {
    g_feedback_records_cache.valid = true;
    g_feedback_records_cache.path = path_w;
    g_feedback_records_cache.stamp = stamp;
    g_feedback_records_cache.records.clear();
    return records;
  }

  if (!stamp.exists) {
    StoreDebugOutput("load skipped: file not found");
    g_feedback_records_cache.valid = true;
    g_feedback_records_cache.path = path_w;
    g_feedback_records_cache.stamp = stamp;
    g_feedback_records_cache.records.clear();
    return records;
  }

  std::ifstream file(path_w, std::ios::binary);
  if (!file) {
    StoreDebugOutput("load skipped: open failed");
    return records;
  }

  LoadRecordsFromStream(&file, false, &records);

  g_feedback_records_cache.valid = true;
  g_feedback_records_cache.path = path_w;
  g_feedback_records_cache.stamp = stamp;
  g_feedback_records_cache.records = records;

  return records;
#else
  StoreDebugOutput("load skipped: zenz feedback store is Windows-only");
  return {};
#endif
}

std::map<FeedbackKey, Counts> LoadCounts() {
  std::map<FeedbackKey, Counts> counts;

  for (const ParsedFeedbackRecord& record : LoadFeedbackRecords()) {
    Counts& c = counts[FeedbackKey(
        record.key, record.context_class, record.value)];

    if (record.action == "accepted") {
      AddAccepted(&c);
    } else if (record.action == "rejected") {
      AddRejected(record.reason, &c);
    }
  }

  return counts;
}

#if defined(_WIN32)
bool WriteRecordsToPath(const std::wstring& path,
                        const std::vector<ParsedFeedbackRecord>& records) {
  if (path.empty()) {
    return false;
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    StoreDebugOutputWide(
        std::wstring(L"write open failed ")
            .append(RedactedWidePathStats(L"path", path)));
    return false;
  }

  return WriteRecordsToStream(records, &file);
}

bool WriteFeedbackRecordsAtomically(
    const std::vector<ParsedFeedbackRecord>& records) {
  const std::wstring dir_w = GetFeedbackDirWide();
  const std::wstring path_w = GetFeedbackPathWideFromDir(dir_w);

  if (dir_w.empty() || path_w.empty()) {
    StoreDebugOutput("atomic write failed: empty path");
    return false;
  }

  if (records.empty()) {
    if (::DeleteFileW(path_w.c_str())) {
      StoreDebugOutput("clear ok: file removed");
      InvalidateFeedbackRecordsCache();
      return true;
    }

    const DWORD error = ::GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      StoreDebugOutput("clear ok: file already absent");
      InvalidateFeedbackRecordsCache();
      return true;
    }

    StoreDebugOutputWide(
        std::wstring(L"clear failed error=")
            .append(std::to_wstring(error))
            .append(L" ")
            .append(RedactedWidePathStats(L"path", path_w)));
    return false;
  }

  const std::wstring tmp_path_w =
      path_w + L".tmp." + std::to_wstring(::GetCurrentProcessId());

  if (!WriteRecordsToPath(tmp_path_w, records)) {
    ::DeleteFileW(tmp_path_w.c_str());
    return false;
  }

  if (!::MoveFileExW(tmp_path_w.c_str(),
                     path_w.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD error = ::GetLastError();
    StoreDebugOutputWide(
        std::wstring(L"atomic replace failed error=")
            .append(std::to_wstring(error))
            .append(L" ")
            .append(RedactedWidePathStats(L"path", path_w)));
    ::DeleteFileW(tmp_path_w.c_str());
    return false;
  }

  StoreDebugOutputWide(
      std::wstring(L"atomic write ok ")
          .append(RedactedWidePathStats(L"path", path_w)));
  InvalidateFeedbackRecordsCache();
  return true;
}
#else
bool WriteFeedbackRecordsAtomically(
    const std::vector<ParsedFeedbackRecord>&) {
  StoreDebugOutput("atomic write failed: zenz feedback store is Windows-only");
  return false;
}
#endif

void AppendRecord(absl::string_view action,
                  absl::string_view key,
                  absl::string_view context_class,
                  absl::string_view value,
                  absl::string_view reason) {
  ParsedFeedbackRecord record;
  record.action = std::string(action);
  record.key = std::string(key);
  record.context_class = NormalizeContextClass(context_class);
  record.value = std::string(value);
  record.reason = std::string(reason);

  if (!IsSafeFeedbackRecord(record)) {
    StoreDebugOutput(absl::StrCat(
        "append rejected invalid record action=", action,
        " ", RedactedStats("key", key),
        " context_class=", record.context_class,
        " ", RedactedStats("value", value),
        " reason=", reason));
    return;
  }

#if defined(_WIN32)
  const std::wstring dir_w = GetFeedbackDirWide();
  const std::wstring path_w = GetFeedbackPathWideFromDir(dir_w);

  StoreDebugOutputWide(
      std::wstring(L"append feedback dir ")
          .append(RedactedWidePathStats(L"dir", dir_w)));
  StoreDebugOutputWide(
      std::wstring(L"append feedback path ")
          .append(RedactedWidePathStats(L"path", path_w)));

  if (dir_w.empty() || path_w.empty()) {
    StoreDebugOutput("append failed: empty path");
    return;
  }

  const DWORD dir_attr = ::GetFileAttributesW(dir_w.c_str());
  if (dir_attr == INVALID_FILE_ATTRIBUTES ||
      !(dir_attr & FILE_ATTRIBUTE_DIRECTORY)) {
    if (!EnsureDirectoryExists(dir_w)) {
      StoreDebugOutput(
          "append failed: directory does not exist and cannot be created");
      return;
    }
  }

  std::ofstream file(path_w, std::ios::binary | std::ios::app);
#else
  std::ofstream file;
#endif

  if (!file) {
#if defined(_WIN32)
    StoreDebugOutputWide(
        std::wstring(L"append open failed ")
            .append(RedactedWidePathStats(L"path", path_w)));
#else
    StoreDebugOutput("append open failed");
#endif
    return;
  }

  WriteRecordToStream(record, &file);
  file.flush();

  if (!file) {
    StoreDebugOutput("append write/flush failed");
    return;
  }

  InvalidateFeedbackRecordsCache();

  StoreDebugOutput(absl::StrCat(
      "append ok action=", action,
      " ", RedactedStats("key", key),
      " context_class=", record.context_class,
      " ", RedactedStats("value", value),
      " reason=", reason));
}

}  // namespace

ZenzFeedbackDecision ZenzFeedbackStore::Decide(
    absl::string_view key,
    absl::string_view context_class,
    absl::string_view value) const {
  return Decide(key, context_class, value, ZenzFeedbackAutoBlockPolicy());
}

ZenzFeedbackDecision ZenzFeedbackStore::Decide(
    absl::string_view key,
    absl::string_view context_class,
    absl::string_view value,
    const ZenzFeedbackAutoBlockPolicy& auto_block_policy) const {
  const std::map<FeedbackKey, Counts> counts = LoadCounts();

  const std::string normalized_key(key);
  const std::string normalized_context_class =
      NormalizeContextClass(context_class);
  const std::string normalized_value(value);

  Counts aggregated;
  Counts exact;

  for (const auto& item : counts) {
    const FeedbackKey& feedback_key = item.first;
    const Counts& c = item.second;

    const std::string& record_key = std::get<0>(feedback_key);
    const std::string& record_context_class = std::get<1>(feedback_key);
    const std::string& record_value = std::get<2>(feedback_key);

    if (record_key != normalized_key || record_value != normalized_value) {
      continue;
    }

    if (record_context_class == normalized_context_class) {
      MergeCounts(c, &exact);
    }

    if (!IsDecisionCompatibleContextClass(
            normalized_context_class, record_context_class)) {
      continue;
    }

    MergeCounts(c, &aggregated);
  }

  ZenzFeedbackDecision decision = BuildDecisionFromCounts(aggregated);
  ApplyRejectDominanceDecision(exact, &decision);
  ApplyAutoBlockDecision(exact, auto_block_policy, &decision);
  return decision;
}

std::vector<ZenzFeedbackCandidate> ZenzFeedbackStore::GetRankedCandidates(
    absl::string_view key,
    absl::string_view context_class) const {
  return GetRankedCandidates(key, context_class,
                             ZenzFeedbackAutoBlockPolicy());
}

std::vector<ZenzFeedbackCandidate> ZenzFeedbackStore::GetRankedCandidates(
    absl::string_view key,
    absl::string_view context_class,
    const ZenzFeedbackAutoBlockPolicy& auto_block_policy) const {
  const std::map<FeedbackKey, Counts> counts = LoadCounts();

  const std::string normalized_key(key);
  const std::string normalized_context_class =
      NormalizeContextClass(context_class);

  std::map<std::string, Counts> value_counts;
  std::map<std::string, Counts> exact_value_counts;

  for (const auto& item : counts) {
    const FeedbackKey& feedback_key = item.first;
    const Counts& c = item.second;

    const std::string& record_key = std::get<0>(feedback_key);
    const std::string& record_context_class = std::get<1>(feedback_key);
    const std::string& record_value = std::get<2>(feedback_key);

    if (record_key != normalized_key) {
      continue;
    }

    if (record_context_class == normalized_context_class) {
      Counts& exact = exact_value_counts[record_value];
      MergeCounts(c, &exact);
    }

    if (!IsPromotionCompatibleContextClass(
            normalized_context_class, record_context_class)) {
      continue;
    }

    Counts& aggregated = value_counts[record_value];
    MergeCounts(c, &aggregated);
  }

  std::vector<ZenzFeedbackCandidate> candidates;

  for (const auto& item : value_counts) {
    const std::string& value = item.first;
    const Counts& c = item.second;

    const auto exact_it = exact_value_counts.find(value);
    const Counts exact_counts =
        exact_it == exact_value_counts.end() ? Counts() : exact_it->second;

    if (c.hard_rejected ||
        IsAutoBlockedByPolicy(exact_counts, auto_block_policy) ||
        IsRejectCountDominant(exact_counts) ||
        c.accepted < kAcceptThreshold ||
        TotalScore(c) <= 0) {
      continue;
    }

    ZenzFeedbackCandidate candidate;
    candidate.value = value;
    candidate.accepted_count = c.accepted;
    candidate.rejected_count = c.rejected;
    candidate.positive_score = c.positive_score;
    candidate.negative_score = c.negative_score;
    candidate.total_score = TotalScore(c);
    candidate.auto_block_reject_count = exact_counts.auto_block_rejected;
    candidate.hard_rejected = c.hard_rejected;
    candidate.auto_blocked = false;
    candidate.reason = "feedback_preferred";
    candidates.push_back(std::move(candidate));
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const ZenzFeedbackCandidate& a,
               const ZenzFeedbackCandidate& b) {
              if (a.total_score != b.total_score) {
                return a.total_score > b.total_score;
              }
              if (a.positive_score != b.positive_score) {
                return a.positive_score > b.positive_score;
              }
              if (a.negative_score != b.negative_score) {
                return a.negative_score < b.negative_score;
              }
              return a.value < b.value;
            });

  return candidates;
}

std::vector<ZenzFeedbackCandidate> ZenzFeedbackStore::GetAcceptedCandidates(
    absl::string_view key,
    absl::string_view context_class) const {
  return GetRankedCandidates(key, context_class);
}

std::vector<ZenzFeedbackCandidate> ZenzFeedbackStore::GetAcceptedCandidates(
    absl::string_view key,
    absl::string_view context_class,
    const ZenzFeedbackAutoBlockPolicy& auto_block_policy) const {
  return GetRankedCandidates(key, context_class, auto_block_policy);
}

std::vector<ZenzFeedbackEntry> ZenzFeedbackStore::ListEntries() const {
  return ListEntries(ZenzFeedbackAutoBlockPolicy());
}

std::vector<ZenzFeedbackEntry> ZenzFeedbackStore::ListEntries(
    const ZenzFeedbackAutoBlockPolicy& auto_block_policy) const {
  const std::map<FeedbackKey, Counts> counts = LoadCounts();

  std::vector<ZenzFeedbackEntry> entries;
  entries.reserve(counts.size());

  for (const auto& item : counts) {
    const FeedbackKey& feedback_key = item.first;
    const Counts& c = item.second;
    ZenzFeedbackDecision decision = BuildDecisionFromCounts(c);
    ApplyRejectDominanceDecision(c, &decision);
    ApplyAutoBlockDecision(c, auto_block_policy, &decision);

    ZenzFeedbackEntry entry;
    entry.key = std::get<0>(feedback_key);
    entry.context_class = std::get<1>(feedback_key);
    entry.value = std::get<2>(feedback_key);
    entry.accepted_count = c.accepted;
    entry.rejected_count = c.rejected;
    entry.auto_block_reject_count = c.auto_block_rejected;
    entry.auto_block_reject_percentage = AutoBlockRejectPercentage(c);
    entry.hard_rejected = decision.hard_rejected;
    entry.auto_blocked = decision.auto_blocked;
    entry.reason = decision.reason;
    entries.push_back(std::move(entry));
  }

  std::sort(entries.begin(), entries.end(),
            [](const ZenzFeedbackEntry& a,
               const ZenzFeedbackEntry& b) {
              if (a.key != b.key) {
                return a.key < b.key;
              }
              if (a.value != b.value) {
                return a.value < b.value;
              }
              return a.context_class < b.context_class;
            });

  return entries;
}

bool ZenzFeedbackStore::ExportToFile(const std::wstring& path) const {
#if defined(_WIN32)
  const std::vector<ParsedFeedbackRecord> records = LoadFeedbackRecords();
  return WriteRecordsToPath(path, records);
#else
  StoreDebugOutput("export failed: zenz feedback store is Windows-only");
  return false;
#endif
}

bool ZenzFeedbackStore::ImportFromFile(
    const std::wstring& path,
    ZenzFeedbackImportMode mode) {
#if defined(_WIN32)
  if (path.empty()) {
    return false;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    StoreDebugOutputWide(
        std::wstring(L"import open failed ")
            .append(RedactedWidePathStats(L"path", path)));
    return false;
  }

  std::vector<ParsedFeedbackRecord> imported_records;
  if (!LoadRecordsFromStream(&file, true, &imported_records)) {
    StoreDebugOutputWide(
        std::wstring(L"import parse failed ")
            .append(RedactedWidePathStats(L"path", path)));
    return false;
  }

  std::vector<ParsedFeedbackRecord> new_records;
  if (mode == ZenzFeedbackImportMode::kAppend) {
    new_records = LoadFeedbackRecords();
  }

  new_records.insert(new_records.end(),
                     imported_records.begin(),
                     imported_records.end());

  return WriteFeedbackRecordsAtomically(new_records);
#else
  StoreDebugOutput("import failed: zenz feedback store is Windows-only");
  return false;
#endif
}

bool ZenzFeedbackStore::DeleteEntry(absl::string_view key,
                                    absl::string_view context_class,
                                    absl::string_view value) {
  std::vector<ParsedFeedbackRecord> records = LoadFeedbackRecords();

  const std::string normalized_key(key);
  const std::string normalized_context_class =
      NormalizeContextClass(context_class);
  const std::string normalized_value(value);

  const auto new_end =
      std::remove_if(records.begin(), records.end(),
                     [&](const ParsedFeedbackRecord& record) {
                       return record.key == normalized_key &&
                              record.context_class == normalized_context_class &&
                              record.value == normalized_value;
                     });

  if (new_end == records.end()) {
    return true;
  }

  records.erase(new_end, records.end());
  return WriteFeedbackRecordsAtomically(records);
}

bool ZenzFeedbackStore::ClearAll() {
  return WriteFeedbackRecordsAtomically({});
}

void ZenzFeedbackStore::RecordAccepted(
    absl::string_view key,
    absl::string_view context_class,
    absl::string_view value) {
  // The caller is responsible for passing the complete Zenz reading/correction
  // pair.  This store must not synthesize or persist segment-local derivatives.
  AppendRecord("accepted", key, context_class, value, "");
}

void ZenzFeedbackStore::RecordRejected(
    absl::string_view key,
    absl::string_view context_class,
    absl::string_view value,
    absl::string_view reason) {
  // A rejected record is also full-sequence scoped.  It may lower ranking of the
  // same full correction, but it must not become lexical-unit negative evidence.
  AppendRecord("rejected", key, context_class, value, reason);
}

}  // namespace session
}  // namespace mozc
