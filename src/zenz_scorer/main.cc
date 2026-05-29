#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <sddl.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Winhttp.lib")

namespace {

std::atomic<bool> g_llama_launch_started{false};
std::atomic<bool> g_llama_ready_probe_started{false};
std::atomic<bool> g_llama_server_ready{false};
std::atomic<bool> g_shutdown_requested{false};

std::mutex g_llama_process_mutex;
HANDLE g_llama_process = nullptr;
HANDLE g_llama_job = nullptr;

constexpr wchar_t kDefaultPipeName[] = L"\\\\.\\pipe\\mozc_zenz_scorer";
constexpr wchar_t kSingleInstanceMutexName[] =
    L"Local\\MozcZenzScorerSingleInstance";

constexpr wchar_t kDefaultHost[] = L"127.0.0.1";
constexpr int kDefaultPort = 18080;
constexpr int kDefaultCtx = 256;
constexpr int kDefaultThreads = 4;
constexpr int kDefaultNPredict = 64;

constexpr uint32_t kZenzWireMagic = 0x315A4E5A;  // "ZNZ1"
constexpr uint16_t kZenzWireVersion = 1;
constexpr uint16_t kZenzWireKindRequest = 1;
constexpr uint16_t kZenzWireKindResponse = 2;

constexpr uint32_t kStatusOk = 0;
constexpr uint32_t kStatusError = 1;
constexpr uint32_t kStatusTimeout = 2;

// Minimum wait budget used only while llama-server is still loading.
// The actual completion request still uses the request timeout from Mozc.
constexpr uint32_t kMinLlamaReadyWaitMsec = 1500;

// Hard caps for the named-pipe protocol.  The pipe is restricted to the current
// user, but the scorer still must not trust client-provided lengths/timeouts.
constexpr uint32_t kMaxPromptBytes = 8192;
constexpr uint32_t kMaxOutputChars = 256;
constexpr uint32_t kMaxRequestTimeoutMsec = 5000;
constexpr size_t kMaxHttpResponseBytes = 65536;

// Hard caps for environment-controlled runtime knobs.
constexpr int kMaxCtx = 1024;
constexpr int kMaxThreads = 16;
constexpr int kMaxNPredict = 256;

#pragma pack(push, 1)
struct ZenzWireRequestHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t kind;
  uint32_t generation;
  uint32_t timeout_msec;
  uint32_t max_output_chars;
  uint32_t prompt_size;
};

struct ZenzWireResponseHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t kind;
  uint32_t generation;
  uint32_t status;
  uint32_t latency_msec;
  uint32_t value_size;
  uint32_t debug_size;
};
#pragma pack(pop)

struct Options {
  std::wstring pipe_name = kDefaultPipeName;
  std::wstring host = kDefaultHost;
  int port = kDefaultPort;
  int ctx = kDefaultCtx;
  int threads = kDefaultThreads;
  int n_predict = kDefaultNPredict;

  std::wstring llama_server_path;
  std::wstring model_path;
};

void Debug(const std::wstring& message) {
  std::wstring line = L"[mozc-zenz-scorer] ";
  line.append(message);
  line.push_back(L'\n');
  OutputDebugStringW(line.c_str());

  std::wcerr << line;
}

std::wstring RedactedBytes(const wchar_t* label, size_t bytes) {
  std::wstring output(label);
  output.append(L"_bytes=");
  output.append(std::to_wstring(bytes));
  return output;
}

std::wstring RedactedWideChars(const wchar_t* label,
                               const std::wstring& text) {
  std::wstring output(label);
  output.append(L"_chars=");
  output.append(std::to_wstring(text.size()));
  return output;
}

std::wstring RedactedUtf8Bytes(const wchar_t* label,
                               const std::string& text) {
  return RedactedBytes(label, text.size());
}

std::wstring Utf8ToWide(const std::string& input) {
  if (input.empty()) {
    return L"";
  }

  const int size = MultiByteToWideChar(
      CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
  if (size <= 0) {
    return L"<invalid utf8>";
  }

  std::wstring output(size, L'\0');
  MultiByteToWideChar(
      CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
      output.data(), size);
  return output;
}

std::string WideToUtf8(const std::wstring& input) {
  if (input.empty()) {
    return "";
  }

  const int size = WideCharToMultiByte(
      CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
      nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return "";
  }

  std::string output(size, '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
      output.data(), size, nullptr, nullptr);
  return output;
}

std::wstring GetEnvWide(const wchar_t* name) {
  DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
  if (size == 0) {
    return L"";
  }

  std::wstring value(size, L'\0');
  DWORD written = GetEnvironmentVariableW(name, value.data(), size);
  if (written == 0) {
    return L"";
  }

  value.resize(written);
  return value;
}

int GetEnvInt(const wchar_t* name, int default_value) {
  std::wstring value = GetEnvWide(name);
  if (value.empty()) {
    return default_value;
  }

  wchar_t* end = nullptr;
  const long parsed = std::wcstol(value.c_str(), &end, 10);
  if (end == value.c_str() || parsed <= 0) {
    return default_value;
  }

  return static_cast<int>(parsed);
}

std::wstring GetExeDirectory() {
  wchar_t path[MAX_PATH] = {};
  DWORD size = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (size == 0 || size >= MAX_PATH) {
    return L".";
  }

  std::wstring full(path, size);
  const size_t pos = full.find_last_of(L"\\/");
  if (pos == std::wstring::npos) {
    return L".";
  }

  return full.substr(0, pos);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& file) {
  if (dir.empty()) {
    return file;
  }

  if (dir.back() == L'\\' || dir.back() == L'/') {
    return dir + file;
  }

  return dir + L"\\" + file;
}

bool FileExists(const std::wstring& path) {
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES &&
         (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

Options LoadOptions() {
  Options options;

  const std::wstring exe_dir = GetExeDirectory();

  options.llama_server_path = GetEnvWide(L"MOZC_ZENZ_LLAMA_SERVER");
  if (options.llama_server_path.empty()) {
    options.llama_server_path = JoinPath(exe_dir, L"llama-server.exe");
  }

  options.model_path = GetEnvWide(L"MOZC_ZENZ_MODEL");
  if (options.model_path.empty()) {
    options.model_path =
        JoinPath(JoinPath(exe_dir, L"models"), L"zenz-v3.2-small-Q5_K_M.gguf");
  }

  options.port = std::clamp(
      GetEnvInt(L"MOZC_ZENZ_PORT", kDefaultPort),
      1,
      65535);
  options.ctx = std::clamp(
      GetEnvInt(L"MOZC_ZENZ_CTX", kDefaultCtx),
      64,
      kMaxCtx);
  options.threads = std::clamp(
      GetEnvInt(L"MOZC_ZENZ_THREADS", kDefaultThreads),
      1,
      kMaxThreads);
  options.n_predict = std::clamp(
      GetEnvInt(L"MOZC_ZENZ_N_PREDICT", kDefaultNPredict),
      4,
      kMaxNPredict);

  return options;
}

void AppendUtf8(uint32_t codepoint, std::string* output) {
  if (codepoint <= 0x7F) {
    output->push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output->push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output->push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output->push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

int HexValue(char c) {
  if ('0' <= c && c <= '9') return c - '0';
  if ('a' <= c && c <= 'f') return c - 'a' + 10;
  if ('A' <= c && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string JsonEscapeUtf8(const std::string& input) {
  std::string output;
  output.reserve(input.size() + 32);

  for (unsigned char c : input) {
    switch (c) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8] = {};
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          output += buf;
        } else {
          output.push_back(static_cast<char>(c));
        }
        break;
    }
  }

  return output;
}

bool ExtractJsonStringField(
    const std::string& json,
    const std::string& field,
    std::string* output) {
  output->clear();

  const std::string needle = "\"" + field + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) {
    return false;
  }

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) {
    return false;
  }

  ++pos;
  while (pos < json.size() &&
         (json[pos] == ' ' || json[pos] == '\t' ||
          json[pos] == '\r' || json[pos] == '\n')) {
    ++pos;
  }

  if (pos >= json.size() || json[pos] != '"') {
    return false;
  }

  ++pos;

  while (pos < json.size()) {
    const char c = json[pos++];

    if (c == '"') {
      return true;
    }

    if (c != '\\') {
      output->push_back(c);
      continue;
    }

    if (pos >= json.size()) {
      return false;
    }

    const char esc = json[pos++];
    switch (esc) {
      case '"':
        output->push_back('"');
        break;
      case '\\':
        output->push_back('\\');
        break;
      case '/':
        output->push_back('/');
        break;
      case 'b':
        output->push_back('\b');
        break;
      case 'f':
        output->push_back('\f');
        break;
      case 'n':
        output->push_back('\n');
        break;
      case 'r':
        output->push_back('\r');
        break;
      case 't':
        output->push_back('\t');
        break;
      case 'u': {
        if (pos + 4 > json.size()) {
          return false;
        }
        uint32_t cp = 0;
        for (int i = 0; i < 4; ++i) {
          const int v = HexValue(json[pos++]);
          if (v < 0) {
            return false;
          }
          cp = (cp << 4) | static_cast<uint32_t>(v);
        }
        AppendUtf8(cp, output);
        break;
      }
      default:
        return false;
    }
  }

  return false;
}

std::string TrimAsciiWhitespace(std::string s) {
  while (!s.empty()) {
    const unsigned char c = static_cast<unsigned char>(s.front());
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      break;
    }
    s.erase(s.begin());
  }

  while (!s.empty()) {
    const unsigned char c = static_cast<unsigned char>(s.back());
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      break;
    }
    s.pop_back();
  }

  return s;
}

std::string TruncateUtf8ByChars(const std::string& input, uint32_t max_chars) {
  if (max_chars == 0) {
    return "";
  }

  size_t pos = 0;
  uint32_t count = 0;

  while (pos < input.size() && count < max_chars) {
    const unsigned char c = static_cast<unsigned char>(input[pos]);
    size_t char_len = 1;

    if ((c & 0x80) == 0) {
      char_len = 1;
    } else if ((c & 0xE0) == 0xC0) {
      char_len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      char_len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      char_len = 4;
    } else {
      break;
    }

    if (pos + char_len > input.size()) {
      break;
    }

    pos += char_len;
    ++count;
  }

  return input.substr(0, pos);
}

std::string CleanGeneratedText(std::string text, uint32_t max_output_chars) {
  const std::vector<std::string> stop_markers = {
      "\xEE\xB8\x80",  // U+EE00
      "\xEE\xB8\x81",  // U+EE01
      "\xEE\xB8\x82",  // U+EE02
      "\xEE\xB8\x83",  // U+EE03
      "\xEE\xB8\x84",  // U+EE04
      "\xEE\xB8\x85",  // U+EE05
      "\xEE\xB8\x86",  // U+EE06
      "\xEE\xB8\x87",  // U+EE07
      "\xEE\xB8\x88",  // U+EE08
      "\xEE\xB8\x89",  // U+EE09
      "\xEE\xB8\x8A",  // U+EE0A
      "\xEE\xB8\x8B",  // U+EE0B
      "\xEE\xB8\x8C",  // U+EE0C
      "\xEE\xB8\x8D",  // U+EE0D
      "\xEE\xB8\x8E",  // U+EE0E
      "\xEE\xB8\x8F",  // U+EE0F
      "<s>",
      "</s>",
      "<unk>",
      "<|endoftext|>",
      "\r",
      "\n",
  };

  size_t end = text.size();
  for (const std::string& marker : stop_markers) {
    const size_t pos = text.find(marker);
    if (pos != std::string::npos) {
      end = std::min(end, pos);
    }
  }

  text = text.substr(0, end);
  text = TrimAsciiWhitespace(text);

  if (max_output_chars > 0) {
    text = TruncateUtf8ByChars(text, max_output_chars);
  }

  return text;
}

bool ReadAll(HANDLE handle, void* data, uint32_t size) {
  uint8_t* ptr = static_cast<uint8_t*>(data);
  uint32_t remaining = size;

  while (remaining > 0) {
    DWORD read = 0;
    if (!ReadFile(handle, ptr, remaining, &read, nullptr)) {
      return false;
    }
    if (read == 0) {
      return false;
    }
    ptr += read;
    remaining -= read;
  }

  return true;
}

bool WriteAll(HANDLE handle, const void* data, uint32_t size) {
  const uint8_t* ptr = static_cast<const uint8_t*>(data);
  uint32_t remaining = size;

  while (remaining > 0) {
    DWORD written = 0;
    if (!WriteFile(handle, ptr, remaining, &written, nullptr)) {
      return false;
    }
    if (written == 0) {
      return false;
    }
    ptr += written;
    remaining -= written;
  }

  return true;
}

bool GetCurrentUserSidString(std::wstring* sid_string) {
  if (sid_string == nullptr) {
    return false;
  }

  sid_string->clear();

  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    Debug(L"OpenProcessToken failed error=" + std::to_wstring(GetLastError()));
    return false;
  }

  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  if (size == 0) {
    Debug(L"GetTokenInformation size failed error=" +
          std::to_wstring(GetLastError()));
    CloseHandle(token);
    return false;
  }

  std::vector<uint8_t> buffer(size);
  TOKEN_USER* token_user = reinterpret_cast<TOKEN_USER*>(buffer.data());
  if (!GetTokenInformation(token, TokenUser, token_user, size, &size)) {
    Debug(L"GetTokenInformation failed error=" +
          std::to_wstring(GetLastError()));
    CloseHandle(token);
    return false;
  }

  LPWSTR raw_sid_string = nullptr;
  if (!ConvertSidToStringSidW(token_user->User.Sid, &raw_sid_string)) {
    Debug(L"ConvertSidToStringSidW failed error=" +
          std::to_wstring(GetLastError()));
    CloseHandle(token);
    return false;
  }

  *sid_string = raw_sid_string;
  LocalFree(raw_sid_string);
  CloseHandle(token);
  return !sid_string->empty();
}

bool BuildCurrentUserOnlyPipeSecurityDescriptor(
    PSECURITY_DESCRIPTOR* security_descriptor) {
  if (security_descriptor == nullptr) {
    return false;
  }

  *security_descriptor = nullptr;

  std::wstring user_sid;
  if (!GetCurrentUserSidString(&user_sid)) {
    return false;
  }

  // Protected DACL:
  //   current user: Generic Read + Generic Write
  //   SYSTEM: Generic All
  //
  // Do not grant Everyone/World access.  Do not add a Low Integrity label.
  const std::wstring sddl =
      L"D:P"
      L"(A;;GRGW;;;" + user_sid + L")"
      L"(A;;GA;;;SY)";

  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(),
          SDDL_REVISION_1,
          security_descriptor,
          nullptr)) {
    Debug(L"ConvertStringSecurityDescriptorToSecurityDescriptorW failed error=" +
          std::to_wstring(GetLastError()));
    *security_descriptor = nullptr;
    return false;
  }

  return true;
}

void ResetLlamaReadyState() {
  g_llama_launch_started = false;
  g_llama_ready_probe_started = false;
  g_llama_server_ready = false;
}

void StopLlamaServer() {
  std::lock_guard<std::mutex> lock(g_llama_process_mutex);

  if (g_llama_job != nullptr) {
    ::TerminateJobObject(g_llama_job, 0);
  }

  if (g_llama_process != nullptr) {
    ::TerminateProcess(g_llama_process, 0);
    ::WaitForSingleObject(g_llama_process, 3000);
    ::CloseHandle(g_llama_process);
    g_llama_process = nullptr;
  }

  if (g_llama_job != nullptr) {
    ::CloseHandle(g_llama_job);
    g_llama_job = nullptr;
  }

  ResetLlamaReadyState();
}

BOOL WINAPI ConsoleCtrlHandler(DWORD control_type) {
  switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      g_shutdown_requested = true;
      StopLlamaServer();
      return TRUE;
    default:
      return FALSE;
  }
}

void RememberLlamaProcess(PROCESS_INFORMATION* process) {
  if (process == nullptr || process->hProcess == nullptr) {
    return;
  }

  HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
  if (job != nullptr) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    if (!::SetInformationJobObject(
            job,
            JobObjectExtendedLimitInformation,
            &info,
            sizeof(info))) {
      ::CloseHandle(job);
      job = nullptr;
    }
  }

  if (job != nullptr && !::AssignProcessToJobObject(job, process->hProcess)) {
    Debug(L"AssignProcessToJobObject failed error=" +
          std::to_wstring(::GetLastError()));
    ::CloseHandle(job);
    job = nullptr;
  }

  std::lock_guard<std::mutex> lock(g_llama_process_mutex);

  if (g_llama_process != nullptr) {
    ::CloseHandle(g_llama_process);
    g_llama_process = nullptr;
  }

  if (g_llama_job != nullptr) {
    ::CloseHandle(g_llama_job);
    g_llama_job = nullptr;
  }

  g_llama_process = process->hProcess;
  process->hProcess = nullptr;

  g_llama_job = job;
}

bool LaunchLlamaServer(const Options& options, std::wstring* error) {
  if (!FileExists(options.llama_server_path)) {
    *error = L"llama_server_not_found";
    return false;
  }

  if (!FileExists(options.model_path)) {
    *error = L"model_not_found";
    return false;
  }

  std::wstring cmd;
  cmd += L"\"";
  cmd += options.llama_server_path;
  cmd += L"\" -m \"";
  cmd += options.model_path;
  cmd += L"\" -c ";
  cmd += std::to_wstring(options.ctx);
  cmd += L" -t ";
  cmd += std::to_wstring(options.threads);
  cmd += L" --host 127.0.0.1 --port ";
  cmd += std::to_wstring(options.port);

  Debug(L"launch llama-server");

  std::vector<wchar_t> cmd_buffer(cmd.begin(), cmd.end());
  cmd_buffer.push_back(L'\0');

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;

  PROCESS_INFORMATION process = {};

  const std::wstring work_dir = GetExeDirectory();

  if (!CreateProcessW(
          nullptr,
          cmd_buffer.data(),
          nullptr,
          nullptr,
          FALSE,
          CREATE_NO_WINDOW,
          nullptr,
          work_dir.c_str(),
          &startup,
          &process)) {
    const DWORD err = GetLastError();
    *error = L"CreateProcessW failed error=" + std::to_wstring(err);
    return false;
  }

  ::CloseHandle(process.hThread);
  RememberLlamaProcess(&process);

  if (process.hProcess != nullptr) {
    ::CloseHandle(process.hProcess);
  }

  return true;
}

bool HttpPostCompletion(
    const Options& options,
    const std::string& prompt,
    uint32_t timeout_msec,
    uint32_t max_output_chars,
    std::string* value,
    std::string* debug) {
  value->clear();
  debug->clear();

  HINTERNET session = WinHttpOpen(
      L"mozc_zenz_scorer/1.0",
      WINHTTP_ACCESS_TYPE_NO_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS,
      0);

  if (!session) {
    *debug = "winhttp_open_failed";
    return false;
  }

  const int timeout = static_cast<int>(std::max<uint32_t>(timeout_msec, 50));
  WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

  HINTERNET connect = WinHttpConnect(
      session,
      options.host.c_str(),
      static_cast<INTERNET_PORT>(options.port),
      0);

  if (!connect) {
    WinHttpCloseHandle(session);
    *debug = "winhttp_connect_failed";
    return false;
  }

  HINTERNET request = WinHttpOpenRequest(
      connect,
      L"POST",
      L"/completion",
      nullptr,
      WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      0);

  if (!request) {
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    *debug = "winhttp_open_request_failed";
    return false;
  }

  const int requested_n_predict =
      max_output_chars > 0 ? static_cast<int>(max_output_chars)
                          : options.n_predict;
  const int n_predict =
      std::max(4, std::min(options.n_predict, requested_n_predict));

  std::string body;
  body += "{";
  body += "\"prompt\":\"";
  body += JsonEscapeUtf8(prompt);
  body += "\",";
  body += "\"n_predict\":";
  body += std::to_string(n_predict);
  body += ",";
  body += "\"temperature\":0.0,";
  body += "\"top_k\":1,";
  body += "\"top_p\":1.0,";
  body += "\"stream\":false,";
  body += "\"cache_prompt\":true,";
  body += "\"stop\":["
          "\"\\n\","
          "\"\\r\""
          "]";
  body += "}";

  const wchar_t headers[] = L"Content-Type: application/json; charset=utf-8\r\n";

  BOOL ok = WinHttpSendRequest(
      request,
      headers,
      static_cast<DWORD>(-1L),
      body.data(),
      static_cast<DWORD>(body.size()),
      static_cast<DWORD>(body.size()),
      0);

  if (!ok) {
    const DWORD err = GetLastError();
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    *debug = "winhttp_send_request_failed_" + std::to_string(err);
    return false;
  }

  ok = WinHttpReceiveResponse(request, nullptr);
  if (!ok) {
    const DWORD err = GetLastError();
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    *debug = "winhttp_receive_response_failed_" + std::to_string(err);
    return false;
  }

  std::string response_body;

  while (true) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available)) {
      const DWORD err = GetLastError();
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      *debug = "winhttp_query_data_available_failed_" + std::to_string(err);
      return false;
    }

    if (available == 0) {
      break;
    }

    std::string buffer(available, '\0');
    DWORD read = 0;
    if (!WinHttpReadData(request, buffer.data(), available, &read)) {
      const DWORD err = GetLastError();
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      *debug = "winhttp_read_data_failed_" + std::to_string(err);
      return false;
    }

    buffer.resize(read);

    if (response_body.size() + buffer.size() > kMaxHttpResponseBytes) {
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      WinHttpCloseHandle(session);
      *debug = "http_response_too_large";
      return false;
    }

    response_body += buffer;
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);

  std::string content;
  if (!ExtractJsonStringField(response_body, "content", &content)) {
    *debug = "content_field_not_found";
    return false;
  }

  content = CleanGeneratedText(content, max_output_chars);
  if (content.empty()) {
    *debug = "empty_content";
    return false;
  }

  *value = std::move(content);
  *debug = "ok";
  return true;
}

void StartLlamaReadyProbeInBackground(const Options& options);

void StartLlamaServerInBackground(const Options& options) {
  bool expected = false;
  if (!g_llama_launch_started.compare_exchange_strong(expected, true)) {
    StartLlamaReadyProbeInBackground(options);
    return;
  }

  std::thread([options]() {
    std::wstring launch_error;
    if (!LaunchLlamaServer(options, &launch_error)) {
      Debug(L"background launch failed: " + launch_error);
      g_llama_launch_started = false;
      g_llama_ready_probe_started = false;
      g_llama_server_ready = false;
      return;
    }

    Debug(L"background launch requested");
    StartLlamaReadyProbeInBackground(options);
  }).detach();
}

void StartLlamaReadyProbeInBackground(const Options& options) {
  bool expected = false;
  if (!g_llama_ready_probe_started.compare_exchange_strong(expected, true)) {
    return;
  }

  std::thread([options]() {
    Debug(L"ready probe started");

    // Model loading + warmup may take several seconds on cold start.
    // This probe is intentionally outside Mozc's request path.
    for (int i = 0; i < 120; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      std::string value;
      std::string local_debug;

      if (HttpPostCompletion(
              options,
              "\xEE\xB8\x82\xEE\xB8\x80テスト\xEE\xB8\x81",
              1500,
              8,
              &value,
              &local_debug)) {
        g_llama_server_ready = true;
        g_llama_ready_probe_started = false;
        Debug(L"ready probe succeeded");
        return;
      }

      if (i % 10 == 0) {
        Debug(L"ready probe waiting");
      }
    }

    Debug(L"ready probe timeout");
    g_llama_ready_probe_started = false;
    g_llama_server_ready = false;
  }).detach();
}

bool EnsureLlamaServerReadyWithinTimeout(const Options& options,
                                         uint32_t timeout_msec,
                                         std::string* debug) {
  if (g_llama_server_ready.load()) {
    *debug = "server_ready";
    return true;
  }

  StartLlamaServerInBackground(options);
  StartLlamaReadyProbeInBackground(options);

  const uint32_t wait_budget_msec =
      std::max<uint32_t>(
          std::max<uint32_t>(timeout_msec, 50),
          kMinLlamaReadyWaitMsec);

  constexpr uint32_t kReadyWaitStepMsec = 25;

  const DWORD start = GetTickCount();

  while (GetTickCount() - start < wait_budget_msec) {
    if (g_llama_server_ready.load()) {
      const DWORD waited = GetTickCount() - start;
      *debug = "server_ready_after_wait";
      Debug(L"server ready wait succeeded waited_msec=" +
            std::to_wstring(waited));
      return true;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(kReadyWaitStepMsec));
  }

  *debug = "server_loading";
  Debug(L"server ready wait timeout budget_msec=" +
        std::to_wstring(wait_budget_msec));
  return false;
}

bool MakePipeSecurityAttributes(
    PSECURITY_DESCRIPTOR* security_descriptor,
    SECURITY_ATTRIBUTES* security_attributes) {
  if (security_descriptor == nullptr || security_attributes == nullptr) {
    return false;
  }

  *security_descriptor = nullptr;
  *security_attributes = {};

  if (!BuildCurrentUserOnlyPipeSecurityDescriptor(security_descriptor)) {
    return false;
  }

  security_attributes->nLength = sizeof(*security_attributes);
  security_attributes->lpSecurityDescriptor = *security_descriptor;
  security_attributes->bInheritHandle = FALSE;
  return true;
}

void SendResponse(
    HANDLE pipe,
    uint32_t generation,
    uint32_t status,
    uint32_t latency_msec,
    const std::string& value,
    const std::string& debug) {
  ZenzWireResponseHeader header = {};
  header.magic = kZenzWireMagic;
  header.version = kZenzWireVersion;
  header.kind = kZenzWireKindResponse;
  header.generation = generation;
  header.status = status;
  header.latency_msec = latency_msec;
  header.value_size = static_cast<uint32_t>(value.size());
  header.debug_size = static_cast<uint32_t>(debug.size());

  WriteAll(pipe, &header, sizeof(header));

  if (!value.empty()) {
    WriteAll(pipe, value.data(), static_cast<uint32_t>(value.size()));
  }

  if (!debug.empty()) {
    WriteAll(pipe, debug.data(), static_cast<uint32_t>(debug.size()));
  }
}

void HandleClient(HANDLE pipe, const Options& options) {
  const DWORD start = GetTickCount();

  ZenzWireRequestHeader request_header = {};
  if (!ReadAll(pipe, &request_header, sizeof(request_header))) {
    return;
  }

  if (request_header.magic != kZenzWireMagic ||
      request_header.version != kZenzWireVersion ||
      request_header.kind != kZenzWireKindRequest) {
    SendResponse(pipe, request_header.generation, kStatusError, 0, "",
                 "bad_request_header");
    return;
  }

  if (request_header.prompt_size == 0) {
    SendResponse(pipe, request_header.generation, kStatusError, 0, "",
                 "empty_prompt");
    return;
  }

  if (request_header.prompt_size > kMaxPromptBytes) {
    SendResponse(pipe, request_header.generation, kStatusError, 0, "",
                 "prompt_too_large");
    return;
  }

  const uint32_t timeout_msec = std::clamp<uint32_t>(
      request_header.timeout_msec == 0
          ? kMaxRequestTimeoutMsec
          : request_header.timeout_msec,
      50,
      kMaxRequestTimeoutMsec);

  const uint32_t max_output_chars = std::clamp<uint32_t>(
      request_header.max_output_chars == 0
          ? kMaxOutputChars
          : request_header.max_output_chars,
      1,
      kMaxOutputChars);

  std::string prompt(request_header.prompt_size, '\0');
  if (!ReadAll(pipe, prompt.data(), request_header.prompt_size)) {
    SendResponse(pipe, request_header.generation, kStatusError, 0, "",
                 "failed_to_read_prompt");
    return;
  }

  Debug(L"request gen=" + std::to_wstring(request_header.generation) +
        L" " + RedactedUtf8Bytes(L"prompt", prompt));

  std::string debug;
  if (!EnsureLlamaServerReadyWithinTimeout(
          options, timeout_msec, &debug)) {
    const DWORD latency = GetTickCount() - start;
    SendResponse(pipe, request_header.generation, kStatusTimeout, latency, "",
                 debug);
    return;
  }

  std::string value;
  if (!HttpPostCompletion(
          options,
          prompt,
          timeout_msec,
          max_output_chars,
          &value,
          &debug)) {
    const DWORD latency = GetTickCount() - start;
    SendResponse(pipe, request_header.generation, kStatusError, latency, "",
                 debug);
    return;
  }

  const DWORD latency = GetTickCount() - start;

  Debug(L"response gen=" + std::to_wstring(request_header.generation) +
        L" latency=" + std::to_wstring(latency) +
        L" " + RedactedUtf8Bytes(L"value", value));

  SendResponse(pipe, request_header.generation, kStatusOk, latency, value,
               debug);
}

int RunServer(const Options& options) {
  Debug(L"server start " +
        RedactedWideChars(L"pipe_name", options.pipe_name) +
        L" " +
        RedactedWideChars(L"llama_server_path", options.llama_server_path) +
        L" " +
        RedactedWideChars(L"model_path", options.model_path));
  Debug(L"port=" + std::to_wstring(options.port));
  Debug(L"n_predict=" + std::to_wstring(options.n_predict));

  // Start llama-server in the background. Never block Mozc's request path on
  // model loading.
  StartLlamaServerInBackground(options);

  while (!g_shutdown_requested.load()) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES sa = {};

    if (!MakePipeSecurityAttributes(&sd, &sa)) {
      Debug(L"MakePipeSecurityAttributes failed");
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      continue;
    }

    HANDLE pipe = ::CreateNamedPipeW(
        options.pipe_name.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        65536,
        65536,
        0,
        &sa);

    if (sd != nullptr) {
      ::LocalFree(sd);
      sd = nullptr;
    }

    if (pipe == INVALID_HANDLE_VALUE) {
      const DWORD err = ::GetLastError();
      Debug(L"CreateNamedPipeW failed error=" + std::to_wstring(err));
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      continue;
    }

    BOOL connected = ::ConnectNamedPipe(pipe, nullptr);
    if (!connected) {
      const DWORD err = ::GetLastError();
      if (err != ERROR_PIPE_CONNECTED) {
        Debug(L"ConnectNamedPipe failed error=" + std::to_wstring(err));
        ::CloseHandle(pipe);
        continue;
      }
    }

    HandleClient(pipe, options);

    ::FlushFileBuffers(pipe);
    ::DisconnectNamedPipe(pipe);
    ::CloseHandle(pipe);
  }

  StopLlamaServer();
  return 0;
}

}  // namespace

int wmain() {
  ::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

  HANDLE single_instance_mutex =
      ::CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);

  if (single_instance_mutex != nullptr &&
      ::GetLastError() == ERROR_ALREADY_EXISTS) {
    Debug(L"another scorer instance already exists");
    ::CloseHandle(single_instance_mutex);
    return 0;
  }

  const Options options = LoadOptions();
  const int result = RunServer(options);

  StopLlamaServer();

  if (single_instance_mutex != nullptr) {
    ::ReleaseMutex(single_instance_mutex);
    ::CloseHandle(single_instance_mutex);
  }

  return result;
}
