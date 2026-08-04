#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <netinet/in.h>
#include <spawn.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

extern char **environ;

namespace {

std::atomic<bool> g_shutdown_requested{false};
std::atomic<bool> g_llama_server_ready{false};

std::mutex g_llama_process_mutex;
pid_t g_llama_process = -1;

constexpr char kDefaultUnixSocketEndpoint[] = "/tmp/mozc_zenz_scorer.sock";
constexpr char kDefaultHost[] = "127.0.0.1";
constexpr int kDefaultPort = 18080;
constexpr int kDefaultCtx = 256;
constexpr int kDefaultThreads = 4;
constexpr int kDefaultNPredict = 64;
constexpr char kDefaultRuntimeRelativePath[] = "zenz_runtime";
constexpr char kDefaultLlamaServerBinary[] = "llama-server";
constexpr char kDefaultModelRelativePath[] = "models/zenz-v3.2-small-Q5_K_M.gguf";

constexpr uint32_t kZenzWireMagic = 0x315A4E5A;  // "ZNZ1"
constexpr uint16_t kZenzWireVersion = 1;
constexpr uint16_t kZenzWireKindRequest = 1;
constexpr uint16_t kZenzWireKindResponse = 2;

constexpr uint32_t kStatusOk = 0;
constexpr uint32_t kStatusError = 1;
constexpr uint32_t kStatusTimeout = 2;

constexpr uint32_t kMaxPromptBytes = 8192;
constexpr uint32_t kMaxOutputChars = 256;
constexpr uint32_t kMaxRequestTimeoutMsec = 5000;
constexpr uint32_t kMinLlamaReadyWaitMsec = 1500;
constexpr size_t kMaxHttpResponseBytes = 65536;
constexpr int kMaxCtx = 1024;
constexpr int kMaxThreads = 16;
constexpr int kMaxNPredict = 256;

struct Options {
  std::string socket_path = kDefaultUnixSocketEndpoint;
  std::string host = kDefaultHost;
  int port = kDefaultPort;
  int ctx = kDefaultCtx;
  int threads = kDefaultThreads;
  int n_predict = kDefaultNPredict;
  std::string llama_server_path;
  std::string model_path;
};

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

void SignalHandler(int) {
  g_shutdown_requested = true;
}

int GetEnvInt(const char* name, int default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return default_value;
  }
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (end == value || parsed <= 0) {
    return default_value;
  }
  return static_cast<int>(parsed);
}

std::string GetEnvString(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return "";
  }
  return value;
}

std::string JoinPath(const std::string& dir, const std::string& file) {
  if (dir.empty()) {
    return file;
  }
  if (dir.back() == '/') {
    return dir + file;
  }
  return dir + "/" + file;
}

std::string GetExeDirectory() {
  uint32_t size = 0;
  if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0) {
    return ".";
  }

  std::string path(size, '\0');
  if (_NSGetExecutablePath(path.data(), &size) != 0 || size == 0) {
    return ".";
  }
  if (!path.empty() && path.back() == '\0') {
    path.pop_back();
  }

  const size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) {
    return ".";
  }
  return path.substr(0, pos);
}

bool FileExists(const std::string& path) {
  struct stat st = {};
  return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string ResolveBundledOrAdjacentPath(const std::string& exe_dir,
                                         const std::string& relative_path) {
  const std::string bundled_path =
      JoinPath(JoinPath(exe_dir, kDefaultRuntimeRelativePath), relative_path);
  if (FileExists(bundled_path)) {
    return bundled_path;
  }
  return JoinPath(exe_dir, relative_path);
}

bool IsProcessAlive(pid_t pid) {
  if (pid <= 0) {
    return false;
  }
  if (::kill(pid, 0) == 0) {
    return true;
  }
  return errno == EPERM;
}

std::string DescribeProcessExit(int status) {
  if (WIFEXITED(status)) {
    return "exit_" + std::to_string(WEXITSTATUS(status));
  }
  if (WIFSIGNALED(status)) {
    return "signal_" + std::to_string(WTERMSIG(status));
  }
  return "unknown";
}

void ResetLlamaReadyState() {
  g_llama_server_ready = false;
}

void StopLlamaServer() {
  std::lock_guard<std::mutex> lock(g_llama_process_mutex);

  if (g_llama_process > 0) {
    ::kill(g_llama_process, SIGTERM);
    for (int i = 0; i < 40; ++i) {
      int status = 0;
      const pid_t waited = ::waitpid(g_llama_process, &status, WNOHANG);
      if (waited == g_llama_process || waited == -1) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (IsProcessAlive(g_llama_process)) {
      ::kill(g_llama_process, SIGKILL);
    }
    int status = 0;
    ::waitpid(g_llama_process, &status, WNOHANG);
    g_llama_process = -1;
  }

  ResetLlamaReadyState();
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

bool ExtractJsonStringField(const std::string& json,
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
      case '"': output->push_back('"'); break;
      case '\\': output->push_back('\\'); break;
      case '/': output->push_back('/'); break;
      case 'b': output->push_back('\b'); break;
      case 'f': output->push_back('\f'); break;
      case 'n': output->push_back('\n'); break;
      case 'r': output->push_back('\r'); break;
      case 't': output->push_back('\t'); break;
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
  const std::vector<std::string> removable_markers = {
      "\xEE\xB8\x80", "\xEE\xB8\x81", "\xEE\xB8\x82", "\xEE\xB8\x83",
      "\xEE\xB8\x84", "\xEE\xB8\x85", "\xEE\xB8\x86", "\xEE\xB8\x87",
      "\xEE\xB8\x88", "\xEE\xB8\x89", "\xEE\xB8\x8A", "\xEE\xB8\x8B",
      "\xEE\xB8\x8C", "\xEE\xB8\x8D", "\xEE\xB8\x8E", "\xEE\xB8\x8F",
      "<s>", "</s>", "<unk>", "<|endoftext|>",
  };
  const std::vector<std::string> terminating_markers = {"\r", "\n"};

  for (const std::string& marker : removable_markers) {
    size_t pos = 0;
    while ((pos = text.find(marker, pos)) != std::string::npos) {
      text.erase(pos, marker.size());
    }
  }

  size_t end = text.size();
  for (const std::string& marker : terminating_markers) {
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

bool SetSocketTimeout(int fd, uint32_t timeout_msec) {
  const uint32_t capped = std::clamp<uint32_t>(timeout_msec == 0 ? 50 : timeout_msec,
                                               50, kMaxRequestTimeoutMsec);
  timeval timeout = {};
  timeout.tv_sec = capped / 1000;
  timeout.tv_usec = static_cast<suseconds_t>((capped % 1000) * 1000);
  return ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 &&
         ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
}

Options LoadOptions() {
  Options options;
  const std::string exe_dir = GetExeDirectory();
  const char* socket_path = std::getenv("MOZC_ZENZ_UNIX_SOCKET");
  if (socket_path != nullptr && *socket_path != '\0') {
    options.socket_path = socket_path;
  }
  options.port = std::clamp(GetEnvInt("MOZC_ZENZ_PORT", kDefaultPort), 1, 65535);
  options.ctx = std::clamp(GetEnvInt("MOZC_ZENZ_CTX", kDefaultCtx), 64, kMaxCtx);
  options.threads = std::clamp(GetEnvInt("MOZC_ZENZ_THREADS", kDefaultThreads), 1, kMaxThreads);
  options.n_predict = std::clamp(GetEnvInt("MOZC_ZENZ_N_PREDICT", kDefaultNPredict), 4, kMaxNPredict);
  options.llama_server_path = GetEnvString("MOZC_ZENZ_LLAMA_SERVER");
  if (options.llama_server_path.empty()) {
    options.llama_server_path =
        ResolveBundledOrAdjacentPath(exe_dir, kDefaultLlamaServerBinary);
  }
  options.model_path = GetEnvString("MOZC_ZENZ_MODEL");
  if (options.model_path.empty()) {
    options.model_path =
        ResolveBundledOrAdjacentPath(exe_dir, kDefaultModelRelativePath);
  }
  return options;
}

bool WriteAll(int fd, const void* data, size_t size) {
  const uint8_t* ptr = static_cast<const uint8_t*>(data);
  size_t remaining = size;

  while (remaining > 0) {
    const ssize_t written = ::send(fd, ptr, remaining, 0);
    if (written <= 0) {
      return false;
    }
    ptr += written;
    remaining -= static_cast<size_t>(written);
  }

  return true;
}

bool ReadAll(int fd, void* data, size_t size) {
  uint8_t* ptr = static_cast<uint8_t*>(data);
  size_t remaining = size;

  while (remaining > 0) {
    const ssize_t read = ::recv(fd, ptr, remaining, 0);
    if (read <= 0) {
      return false;
    }
    ptr += read;
    remaining -= static_cast<size_t>(read);
  }

  return true;
}

bool HttpGetHealth(const Options& options, uint32_t timeout_msec) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  if (!SetSocketTimeout(fd, timeout_msec)) {
    ::close(fd);
    return false;
  }

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(options.port));
  if (::inet_pton(AF_INET, options.host.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    return false;
  }
  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return false;
  }

  const std::string request =
      "GET /health HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(options.port) +
      "\r\nConnection: close\r\n\r\n";
  if (!WriteAll(fd, request.data(), request.size())) {
    ::close(fd);
    return false;
  }

  std::string response;
  char buffer[1024];
  while (true) {
    const ssize_t read = ::recv(fd, buffer, sizeof(buffer), 0);
    if (read == 0) {
      break;
    }
    if (read < 0) {
      ::close(fd);
      return false;
    }
    response.append(buffer, static_cast<size_t>(read));
    if (response.size() > 4096) {
      break;
    }
  }
  ::close(fd);

  return response.rfind("HTTP/1.1 200", 0) == 0 || response.rfind("HTTP/1.0 200", 0) == 0;
}

bool LaunchLlamaServer(const Options& options, std::string* error) {
  if (!FileExists(options.llama_server_path)) {
    *error = "llama_server_not_found";
    return false;
  }
  if (!FileExists(options.model_path)) {
    *error = "model_not_found";
    return false;
  }

  std::vector<std::string> args_storage = {
      options.llama_server_path,
      "-m",
      options.model_path,
      "-c",
      std::to_string(options.ctx),
      "-t",
      std::to_string(options.threads),
      "--host",
      options.host,
      "--port",
      std::to_string(options.port),
  };
  std::vector<char*> argv;
  argv.reserve(args_storage.size() + 1);
  for (std::string& arg : args_storage) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  posix_spawn_file_actions_t file_actions;
  if (posix_spawn_file_actions_init(&file_actions) != 0) {
    *error = "spawn_file_actions_init_failed";
    return false;
  }
  posix_spawn_file_actions_addopen(&file_actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&file_actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&file_actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

  posix_spawnattr_t attr;
  if (posix_spawnattr_init(&attr) != 0) {
    posix_spawn_file_actions_destroy(&file_actions);
    *error = "spawn_attr_init_failed";
    return false;
  }

  pid_t pid = -1;
  const int spawn_result = posix_spawn(
      &pid,
      options.llama_server_path.c_str(),
      &file_actions,
      &attr,
      argv.data(),
      environ);

  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&file_actions);

  if (spawn_result != 0) {
    *error = "posix_spawn_failed_" + std::to_string(spawn_result);
    return false;
  }

  g_llama_process = pid;
  ResetLlamaReadyState();
  return true;
}

bool EnsureLlamaServerReadyWithinTimeout(const Options& options,
                                        uint32_t timeout_msec,
                                        std::string* debug) {
  if (g_llama_server_ready.load()) {
    *debug = "server_ready";
    return true;
  }

  if (HttpGetHealth(options, 100)) {
    g_llama_server_ready = true;
    *debug = "server_ready";
    return true;
  }

  {
    std::lock_guard<std::mutex> lock(g_llama_process_mutex);
    if (!IsProcessAlive(g_llama_process)) {
      g_llama_process = -1;
      if (!LaunchLlamaServer(options, debug)) {
        return false;
      }
    }
  }

  const uint32_t wait_budget_msec = std::max<uint32_t>(timeout_msec, kMinLlamaReadyWaitMsec);
  constexpr uint32_t kReadyWaitStepMsec = 25;
  for (uint32_t waited = 0; waited < wait_budget_msec; waited += kReadyWaitStepMsec) {
    if (HttpGetHealth(options, kReadyWaitStepMsec)) {
      g_llama_server_ready = true;
      *debug = waited == 0 ? "server_ready" : "server_ready_after_wait";
      return true;
    }
    {
      std::lock_guard<std::mutex> lock(g_llama_process_mutex);
      if (g_llama_process > 0) {
        int status = 0;
        const pid_t waited_pid = ::waitpid(g_llama_process, &status, WNOHANG);
        if (waited_pid == g_llama_process) {
          g_llama_process = -1;
          *debug = "llama_server_" + DescribeProcessExit(status);
          return false;
        }
      }
    }
    if (g_shutdown_requested.load()) {
      *debug = "shutdown_requested";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kReadyWaitStepMsec));
  }

  *debug = "server_ready_timeout";
  return false;
}

void SendResponse(int fd,
                  uint32_t generation,
                  uint32_t status,
                  const std::string& value,
                  const std::string& debug) {
  ZenzWireResponseHeader response = {};
  response.magic = kZenzWireMagic;
  response.version = kZenzWireVersion;
  response.kind = kZenzWireKindResponse;
  response.generation = generation;
  response.status = status;
  response.value_size = static_cast<uint32_t>(value.size());
  response.debug_size = static_cast<uint32_t>(debug.size());

  WriteAll(fd, &response, sizeof(response));
  if (!value.empty()) {
    WriteAll(fd, value.data(), value.size());
  }
  if (!debug.empty()) {
    WriteAll(fd, debug.data(), debug.size());
  }
}

bool HttpPostCompletion(const Options& options,
                        const std::string& prompt,
                        uint32_t timeout_msec,
                        uint32_t max_output_chars,
                        std::string* value,
                        std::string* debug) {
  value->clear();
  debug->clear();

  if (!EnsureLlamaServerReadyWithinTimeout(options, timeout_msec, debug)) {
    return false;
  }

  const int requested_n_predict =
      max_output_chars > 0 ? static_cast<int>(max_output_chars) : options.n_predict;
  const int n_predict = std::max(4, std::min(options.n_predict, requested_n_predict));

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
  body += "\"stop\":[";
  body += "\"\\n\",\"\\r\"]";
  body += "}";

  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    *debug = "http_socket_open_failed";
    return false;
  }
  if (!SetSocketTimeout(fd, timeout_msec)) {
    ::close(fd);
    *debug = "http_socket_timeout_setup_failed";
    return false;
  }

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(options.port));
  if (::inet_pton(AF_INET, options.host.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    *debug = "invalid_host";
    return false;
  }

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    const int connect_errno = errno;
    ::close(fd);
    *debug = (connect_errno == ETIMEDOUT) ? "http_connect_timeout" : "http_connect_failed";
    return false;
  }

  std::string request;
  request += "POST /completion HTTP/1.1\r\n";
  request += "Host: 127.0.0.1:" + std::to_string(options.port) + "\r\n";
  request += "Content-Type: application/json; charset=utf-8\r\n";
  request += "Connection: close\r\n";
  request += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  request += body;

  if (!WriteAll(fd, request.data(), request.size())) {
    ::close(fd);
    *debug = "http_write_failed";
    return false;
  }

  std::string response;
  char buffer[4096];
  while (true) {
    const ssize_t read = ::recv(fd, buffer, sizeof(buffer), 0);
    if (read == 0) {
      break;
    }
    if (read < 0) {
      const int read_errno = errno;
      ::close(fd);
      *debug = (read_errno == EAGAIN || read_errno == EWOULDBLOCK)
                   ? "http_read_timeout"
                   : "http_read_failed";
      return false;
    }
    if (response.size() + static_cast<size_t>(read) > kMaxHttpResponseBytes) {
      ::close(fd);
      *debug = "http_response_too_large";
      return false;
    }
    response.append(buffer, static_cast<size_t>(read));
  }
  ::close(fd);

  const size_t header_end = response.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    *debug = "http_bad_response";
    return false;
  }
  if (response.rfind("HTTP/1.1 200", 0) != 0 && response.rfind("HTTP/1.0 200", 0) != 0) {
    *debug = "http_status_not_ok";
    return false;
  }

  std::string content;
  const std::string response_body = response.substr(header_end + 4);
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
  return true;
}

void HandleClient(int fd) {
  ZenzWireRequestHeader request = {};
  if (!ReadAll(fd, &request, sizeof(request))) {
    return;
  }

  if (request.magic != kZenzWireMagic ||
      request.version != kZenzWireVersion ||
      request.kind != kZenzWireKindRequest) {
    SendResponse(fd, request.generation, kStatusError, "", "bad_request_header");
    return;
  }

  if (request.prompt_size == 0) {
    SendResponse(fd, request.generation, kStatusError, "", "empty_prompt");
    return;
  }

  if (request.prompt_size > kMaxPromptBytes) {
    SendResponse(fd, request.generation, kStatusError, "", "prompt_too_large");
    return;
  }

  const uint32_t timeout_msec = std::clamp<uint32_t>(
      request.timeout_msec == 0 ? kMaxRequestTimeoutMsec : request.timeout_msec,
      50, kMaxRequestTimeoutMsec);
  const uint32_t max_output_chars = std::clamp<uint32_t>(
      request.max_output_chars == 0 ? kMaxOutputChars : request.max_output_chars,
      1, kMaxOutputChars);

  std::string prompt(request.prompt_size, '\0');
  if (request.prompt_size > 0 &&
      !ReadAll(fd, prompt.data(), request.prompt_size)) {
    SendResponse(fd, request.generation, kStatusError, "", "failed_to_read_prompt");
    return;
  }

  const Options options = LoadOptions();
  std::string value;
  std::string debug;
  const bool ok = HttpPostCompletion(
      options, prompt, timeout_msec, max_output_chars, &value, &debug);
  if (!ok) {
    const uint32_t status =
        (debug == "http_connect_timeout" || debug == "http_read_timeout")
            ? kStatusTimeout
            : kStatusError;
    SendResponse(fd, request.generation, status, "", debug);
    return;
  }

  SendResponse(fd, request.generation, kStatusOk, value, debug);
}

int RunServer(const Options& options) {
  if (options.socket_path.empty()) {
    return 1;
  }

  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  if (options.socket_path.size() >= sizeof(addr.sun_path)) {
    return 1;
  }
  std::memcpy(addr.sun_path, options.socket_path.c_str(), options.socket_path.size() + 1);

  const int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0) {
    return 1;
  }

  ::unlink(options.socket_path.c_str());
  const socklen_t addr_len = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + options.socket_path.size() + 1);
  if (::bind(server_fd, reinterpret_cast<const sockaddr*>(&addr), addr_len) != 0) {
    ::close(server_fd);
    return 1;
  }

  if (::listen(server_fd, 8) != 0) {
    ::close(server_fd);
    ::unlink(options.socket_path.c_str());
    return 1;
  }

  while (!g_shutdown_requested.load()) {
    const int client_fd = ::accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
      if (errno == EINTR && g_shutdown_requested.load()) {
        break;
      }
      continue;
    }

    HandleClient(client_fd);
    ::close(client_fd);
  }

  ::close(server_fd);
  ::unlink(options.socket_path.c_str());
  return 0;
}

}  // namespace

int main() {
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);
  const int exit_code = RunServer(LoadOptions());
  StopLlamaServer();
  return exit_code;
}