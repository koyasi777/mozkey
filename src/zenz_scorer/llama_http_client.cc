#include "zenz_scorer/llama_http_client.h"

#if defined(_WIN32)
#error "llama_http_client.cc must not be built on Windows"
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mozc {
namespace zenz_scorer {
namespace {

constexpr uint32_t kDefaultTimeoutMsec = 5000;
constexpr uint32_t kMinTimeoutMsec = 50;
constexpr uint32_t kMaxTimeoutMsec = 5000;
constexpr uint32_t kDefaultMaxOutputChars = 256;
constexpr uint32_t kMaxOutputChars = 256;
constexpr int kDefaultNPredict = 64;
constexpr int kMaxNPredict = 256;
constexpr size_t kMaxPromptBytes = 8192;
constexpr size_t kMaxApiKeyBytes = 256;
constexpr size_t kMaxHttpHeaderBytes = 16 * 1024;
constexpr size_t kMaxHttpResponseBytes = 64 * 1024;

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

enum class IoResult {
  kOk,
  kEof,
  kTimeout,
  kError,
};

enum class ChunkDecodeResult {
  kComplete,
  kNeedMore,
  kError,
};

struct HttpResponseParts {
  int status_code = 0;
  std::optional<size_t> content_length;
  bool chunked = false;
  std::string body_prefix;
};

LlamaHttpCompletionResponse Error(std::string debug) {
  LlamaHttpCompletionResponse response;
  response.status = LlamaHttpCompletionStatus::kError;
  response.debug = std::move(debug);
  return response;
}

LlamaHttpCompletionResponse Timeout(std::string debug) {
  LlamaHttpCompletionResponse response;
  response.status = LlamaHttpCompletionStatus::kTimeout;
  response.debug = std::move(debug);
  return response;
}

std::string ErrnoTag(const char* prefix) {
  return std::string(prefix) + "_errno_" + std::to_string(errno);
}

bool SetCloseOnExec(int fd) {
  const int flags = ::fcntl(fd, F_GETFD, 0);
  return flags >= 0 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool SetNonBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool DisableSigPipe(int fd) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  return ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                      sizeof(enabled)) == 0;
#else
  static_cast<void>(fd);
  return true;
#endif
}

int SendFlags() {
#if defined(MSG_NOSIGNAL)
  return MSG_NOSIGNAL;
#else
  return 0;
#endif
}

int RemainingMsec(Deadline deadline) {
  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                            Clock::now());
  if (remaining.count() <= 0) {
    return 0;
  }
  return static_cast<int>(std::min<int64_t>(
      std::max<int64_t>(1, remaining.count()),
      std::numeric_limits<int>::max()));
}

IoResult WaitForFd(int fd, short events, Deadline deadline) {
  while (true) {
    const int timeout_msec = RemainingMsec(deadline);
    if (timeout_msec == 0) {
      return IoResult::kTimeout;
    }

    pollfd descriptor = {};
    descriptor.fd = fd;
    descriptor.events = events;
    const int result = ::poll(&descriptor, 1, timeout_msec);
    if (result > 0) {
      if ((descriptor.revents & events) != 0 ||
          ((events & POLLIN) != 0 &&
           (descriptor.revents & POLLHUP) != 0)) {
        return IoResult::kOk;
      }
      return IoResult::kError;
    }
    if (result == 0) {
      return IoResult::kTimeout;
    }
    if (errno != EINTR) {
      return IoResult::kError;
    }
  }
}

IoResult ConnectLoopback(int fd, int port, Deadline deadline) {
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port));
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) == 0) {
    return IoResult::kOk;
  }
  if (errno != EINPROGRESS && errno != EALREADY && errno != EINTR) {
    return IoResult::kError;
  }

  const IoResult wait_result = WaitForFd(fd, POLLOUT, deadline);
  if (wait_result != IoResult::kOk) {
    return wait_result;
  }

  int socket_error = 0;
  socklen_t socket_error_size = sizeof(socket_error);
  if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                   &socket_error_size) != 0) {
    return IoResult::kError;
  }
  if (socket_error != 0) {
    errno = socket_error;
    return IoResult::kError;
  }
  return IoResult::kOk;
}

IoResult WriteAll(int fd, std::string_view data, Deadline deadline) {
  size_t offset = 0;
  while (offset < data.size()) {
    const IoResult wait_result = WaitForFd(fd, POLLOUT, deadline);
    if (wait_result != IoResult::kOk) {
      return wait_result;
    }
    const ssize_t written =
        ::send(fd, data.data() + offset, data.size() - offset, SendFlags());
    if (written > 0) {
      offset += static_cast<size_t>(written);
      continue;
    }
    if (written == 0) {
      return IoResult::kError;
    }
    if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
      return IoResult::kError;
    }
  }
  return IoResult::kOk;
}

IoResult ReadSome(int fd, char* buffer, size_t capacity, Deadline deadline,
                  size_t* bytes_read) {
  *bytes_read = 0;
  while (true) {
    const IoResult wait_result = WaitForFd(fd, POLLIN, deadline);
    if (wait_result != IoResult::kOk) {
      return wait_result;
    }

    const ssize_t result = ::recv(fd, buffer, capacity, 0);
    if (result > 0) {
      *bytes_read = static_cast<size_t>(result);
      return IoResult::kOk;
    }
    if (result == 0) {
      return IoResult::kEof;
    }
    if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
      return IoResult::kError;
    }
  }
}

std::string AsciiLower(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (const unsigned char c : input) {
    if ('A' <= c && c <= 'Z') {
      output.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      output.push_back(static_cast<char>(c));
    }
  }
  return output;
}

std::string_view TrimAscii(std::string_view input) {
  while (!input.empty() &&
         (input.front() == ' ' || input.front() == '\t' ||
          input.front() == '\r' || input.front() == '\n')) {
    input.remove_prefix(1);
  }
  while (!input.empty() &&
         (input.back() == ' ' || input.back() == '\t' ||
          input.back() == '\r' || input.back() == '\n')) {
    input.remove_suffix(1);
  }
  return input;
}

bool ParseDecimalSize(std::string_view text, size_t* value) {
  text = TrimAscii(text);
  if (text.empty()) {
    return false;
  }
  size_t parsed = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    const size_t digit = static_cast<size_t>(c - '0');
    if (parsed > (std::numeric_limits<size_t>::max() - digit) / 10) {
      return false;
    }
    parsed = parsed * 10 + digit;
  }
  *value = parsed;
  return true;
}

bool ParseHexSize(std::string_view text, size_t* value) {
  text = TrimAscii(text);
  const size_t semicolon = text.find(';');
  if (semicolon != std::string_view::npos) {
    text = TrimAscii(text.substr(0, semicolon));
  }
  if (text.empty()) {
    return false;
  }

  size_t parsed = 0;
  for (const char c : text) {
    int digit = -1;
    if ('0' <= c && c <= '9') {
      digit = c - '0';
    } else if ('a' <= c && c <= 'f') {
      digit = c - 'a' + 10;
    } else if ('A' <= c && c <= 'F') {
      digit = c - 'A' + 10;
    }
    if (digit < 0 ||
        parsed > (std::numeric_limits<size_t>::max() -
                  static_cast<size_t>(digit)) /
                     16) {
      return false;
    }
    parsed = parsed * 16 + static_cast<size_t>(digit);
  }
  *value = parsed;
  return true;
}

bool ParseHttpHeaders(const std::string& received, HttpResponseParts* parts,
                      std::string* debug) {
  const size_t header_end = received.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    *debug = "http_header_incomplete";
    return false;
  }

  const size_t status_end = received.find("\r\n");
  if (status_end == std::string::npos || status_end > header_end) {
    *debug = "http_status_line_invalid";
    return false;
  }
  const std::string_view status_line(received.data(), status_end);
  const size_t first_space = status_line.find(' ');
  if (first_space == std::string_view::npos || first_space + 4 > status_line.size()) {
    *debug = "http_status_line_invalid";
    return false;
  }
  const std::string_view status_text = status_line.substr(first_space + 1, 3);
  if (status_text[0] < '0' || status_text[0] > '9' ||
      status_text[1] < '0' || status_text[1] > '9' ||
      status_text[2] < '0' || status_text[2] > '9') {
    *debug = "http_status_line_invalid";
    return false;
  }
  parts->status_code = (status_text[0] - '0') * 100 +
                       (status_text[1] - '0') * 10 +
                       (status_text[2] - '0');

  size_t line_start = status_end + 2;
  while (line_start < header_end) {
    const size_t line_end = received.find("\r\n", line_start);
    if (line_end == std::string::npos || line_end > header_end) {
      *debug = "http_header_line_invalid";
      return false;
    }
    const std::string_view line(received.data() + line_start,
                                line_end - line_start);
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      *debug = "http_header_line_invalid";
      return false;
    }
    const std::string name = AsciiLower(TrimAscii(line.substr(0, colon)));
    const std::string_view value = TrimAscii(line.substr(colon + 1));
    if (name == "content-length") {
      size_t parsed_length = 0;
      if (!ParseDecimalSize(value, &parsed_length)) {
        *debug = "http_content_length_invalid";
        return false;
      }
      parts->content_length = parsed_length;
    } else if (name == "transfer-encoding") {
      const std::string lower_value = AsciiLower(value);
      if (lower_value.find("chunked") != std::string::npos) {
        parts->chunked = true;
      }
    }
    line_start = line_end + 2;
  }

  if (parts->chunked) {
    parts->content_length.reset();
  }
  parts->body_prefix = received.substr(header_end + 4);
  return true;
}

ChunkDecodeResult DecodeChunkedBody(std::string_view encoded,
                                    std::string* decoded,
                                    std::string* debug) {
  decoded->clear();
  size_t position = 0;
  while (true) {
    const size_t line_end = encoded.find("\r\n", position);
    if (line_end == std::string_view::npos) {
      return ChunkDecodeResult::kNeedMore;
    }
    size_t chunk_size = 0;
    if (!ParseHexSize(encoded.substr(position, line_end - position),
                      &chunk_size)) {
      *debug = "http_chunk_size_invalid";
      return ChunkDecodeResult::kError;
    }
    position = line_end + 2;

    if (chunk_size == 0) {
      const size_t trailer_end = encoded.find("\r\n\r\n", position);
      if (trailer_end != std::string_view::npos) {
        return ChunkDecodeResult::kComplete;
      }
      if (encoded.size() >= position + 2 &&
          encoded.substr(position, 2) == "\r\n") {
        return ChunkDecodeResult::kComplete;
      }
      return ChunkDecodeResult::kNeedMore;
    }

    if (chunk_size > kMaxHttpResponseBytes ||
        decoded->size() > kMaxHttpResponseBytes - chunk_size) {
      *debug = "http_response_too_large";
      return ChunkDecodeResult::kError;
    }
    if (encoded.size() < position + chunk_size + 2) {
      return ChunkDecodeResult::kNeedMore;
    }
    decoded->append(encoded.data() + position, chunk_size);
    position += chunk_size;
    if (encoded.substr(position, 2) != "\r\n") {
      *debug = "http_chunk_terminator_invalid";
      return ChunkDecodeResult::kError;
    }
    position += 2;
  }
}

std::string JsonEscapeUtf8(std::string_view input) {
  std::string output;
  output.reserve(input.size() + 32);
  for (const unsigned char c : input) {
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
          char buffer[8] = {};
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          output += buffer;
        } else {
          output.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return output;
}

int HexValue(char c) {
  if ('0' <= c && c <= '9') {
    return c - '0';
  }
  if ('a' <= c && c <= 'f') {
    return c - 'a' + 10;
  }
  if ('A' <= c && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool AppendUtf8(uint32_t codepoint, std::string* output) {
  if (codepoint > 0x10FFFF ||
      (0xD800 <= codepoint && codepoint <= 0xDFFF)) {
    return false;
  }
  if (codepoint <= 0x7F) {
    output->push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output->push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output->push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output->push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return true;
}

bool ParseUnicodeEscape(const std::string& json, size_t* position,
                        uint32_t* codepoint) {
  if (*position + 4 > json.size()) {
    return false;
  }
  uint32_t first = 0;
  for (int i = 0; i < 4; ++i) {
    const int digit = HexValue(json[(*position)++]);
    if (digit < 0) {
      return false;
    }
    first = (first << 4) | static_cast<uint32_t>(digit);
  }

  if (first < 0xD800 || first > 0xDBFF) {
    if (0xDC00 <= first && first <= 0xDFFF) {
      return false;
    }
    *codepoint = first;
    return true;
  }

  if (*position + 6 > json.size() || json[*position] != '\\' ||
      json[*position + 1] != 'u') {
    return false;
  }
  *position += 2;
  uint32_t second = 0;
  for (int i = 0; i < 4; ++i) {
    const int digit = HexValue(json[(*position)++]);
    if (digit < 0) {
      return false;
    }
    second = (second << 4) | static_cast<uint32_t>(digit);
  }
  if (second < 0xDC00 || second > 0xDFFF) {
    return false;
  }
  *codepoint = 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
  return true;
}

bool ExtractJsonStringField(const std::string& json, std::string_view field,
                            std::string* output) {
  output->clear();
  const std::string needle = "\"" + std::string(field) + "\"";
  size_t position = json.find(needle);
  if (position == std::string::npos) {
    return false;
  }
  position = json.find(':', position + needle.size());
  if (position == std::string::npos) {
    return false;
  }
  ++position;
  while (position < json.size() &&
         (json[position] == ' ' || json[position] == '\t' ||
          json[position] == '\r' || json[position] == '\n')) {
    ++position;
  }
  if (position >= json.size() || json[position] != '"') {
    return false;
  }
  ++position;

  while (position < json.size()) {
    const char c = json[position++];
    if (c == '"') {
      return true;
    }
    if (c != '\\') {
      if (static_cast<unsigned char>(c) < 0x20) {
        return false;
      }
      output->push_back(c);
      continue;
    }
    if (position >= json.size()) {
      return false;
    }
    const char escaped = json[position++];
    switch (escaped) {
      case '"':
      case '\\':
      case '/':
        output->push_back(escaped);
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
        uint32_t codepoint = 0;
        if (!ParseUnicodeEscape(json, &position, &codepoint) ||
            !AppendUtf8(codepoint, output)) {
          return false;
        }
        break;
      }
      default:
        return false;
    }
  }
  return false;
}

std::string TrimAsciiWhitespace(std::string text) {
  const std::string_view trimmed = TrimAscii(text);
  return std::string(trimmed);
}

bool IsUtf8Continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

std::string TruncateUtf8ByChars(std::string_view input, uint32_t max_chars) {
  if (max_chars == 0) {
    return "";
  }
  size_t position = 0;
  uint32_t count = 0;
  while (position < input.size() && count < max_chars) {
    const unsigned char first = static_cast<unsigned char>(input[position]);
    size_t length = 0;
    if ((first & 0x80) == 0) {
      length = 1;
    } else if ((first & 0xE0) == 0xC0 && first >= 0xC2) {
      length = 2;
    } else if ((first & 0xF0) == 0xE0) {
      length = 3;
    } else if ((first & 0xF8) == 0xF0 && first <= 0xF4) {
      length = 4;
    } else {
      break;
    }
    if (position + length > input.size()) {
      break;
    }
    bool valid = true;
    for (size_t i = 1; i < length; ++i) {
      if (!IsUtf8Continuation(static_cast<unsigned char>(input[position + i]))) {
        valid = false;
        break;
      }
    }
    if (!valid) {
      break;
    }
    position += length;
    ++count;
  }
  return std::string(input.substr(0, position));
}

std::string CleanGeneratedText(std::string text, uint32_t max_output_chars) {
  const std::array<std::string_view, 22> stop_markers = {
      "\xEE\xB8\x80", "\xEE\xB8\x81", "\xEE\xB8\x82", "\xEE\xB8\x83",
      "\xEE\xB8\x84", "\xEE\xB8\x85", "\xEE\xB8\x86", "\xEE\xB8\x87",
      "\xEE\xB8\x88", "\xEE\xB8\x89", "\xEE\xB8\x8A", "\xEE\xB8\x8B",
      "\xEE\xB8\x8C", "\xEE\xB8\x8D", "\xEE\xB8\x8E", "\xEE\xB8\x8F",
      "<s>",          "</s>",         "<unk>",         "<|endoftext|>",
      "\r",           "\n",
  };
  size_t end = text.size();
  for (const std::string_view marker : stop_markers) {
    const size_t position = text.find(marker);
    if (position != std::string::npos) {
      end = std::min(end, position);
    }
  }
  text.resize(end);
  text = TrimAsciiWhitespace(std::move(text));
  return TruncateUtf8ByChars(text, max_output_chars);
}

std::string BuildJsonBody(const LlamaHttpCompletionRequest& request,
                          int n_predict) {
  std::string body;
  body.reserve(request.prompt.size() + 512);
  body += "{";
  body += "\"prompt\":\"";
  body += JsonEscapeUtf8(request.prompt);
  body += "\",";
  body += "\"n_predict\":" + std::to_string(n_predict) + ",";
  body += "\"temperature\":0.0,";
  body += "\"top_k\":1,";
  body += "\"top_p\":1.0,";
  body += "\"stream\":false,";
  body += "\"cache_prompt\":true,";
  body += "\"stop\":["
          "\"\\uee00\",\"\\uee01\",\"\\uee02\",\"\\uee03\","
          "\"\\uee04\",\"\\uee05\",\"\\uee06\",\"\\uee07\","
          "\"\\uee08\",\"\\uee09\",\"\\uee0a\",\"\\uee0b\","
          "\"\\uee0c\",\"\\uee0d\",\"\\uee0e\",\"\\uee0f\","
          "\"\\n\",\"\\r\"]";
  body += "}";
  return body;
}

std::string BuildHttpRequest(const LlamaHttpCompletionRequest& request,
                             int n_predict) {
  const std::string body = BuildJsonBody(request, n_predict);
  std::string output;
  output.reserve(body.size() + request.api_key.size() + 256);
  output += "POST /completion HTTP/1.1\r\n";
  output += "Host: 127.0.0.1:" + std::to_string(request.port) + "\r\n";
  output += "User-Agent: mozc_zenz_scorer/1.0\r\n";
  output += "Content-Type: application/json; charset=utf-8\r\n";
  output += "Authorization: Bearer " + request.api_key + "\r\n";
  output += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  output += "Connection: close\r\n\r\n";
  output += body;
  return output;
}

bool IsSafeApiKey(std::string_view api_key) {
  if (api_key.empty() || api_key.size() > kMaxApiKeyBytes) {
    return false;
  }
  for (const unsigned char c : api_key) {
    if (c < 0x21 || c > 0x7E || c == ':' || c == '\\') {
      return false;
    }
  }
  return true;
}

}  // namespace

LlamaHttpCompletionResponse PostLlamaHttpCompletion(
    const LlamaHttpCompletionRequest& request) {
  if (request.port <= 0 || request.port > 65535) {
    return Error("invalid_port");
  }
  if (!IsSafeApiKey(request.api_key)) {
    return Error("invalid_api_key");
  }
  if (request.prompt.empty()) {
    return Error("empty_prompt");
  }
  if (request.prompt.size() > kMaxPromptBytes) {
    return Error("prompt_too_large");
  }

  const uint32_t timeout_msec = std::clamp<uint32_t>(
      request.timeout_msec == 0 ? kDefaultTimeoutMsec : request.timeout_msec,
      kMinTimeoutMsec, kMaxTimeoutMsec);
  const uint32_t max_output_chars = std::clamp<uint32_t>(
      request.max_output_chars == 0 ? kDefaultMaxOutputChars
                                    : request.max_output_chars,
      1, kMaxOutputChars);
  const int configured_n_predict = std::clamp(
      request.n_predict <= 0 ? kDefaultNPredict : request.n_predict, 4,
      kMaxNPredict);
  const int n_predict = std::max(
      4, std::min(configured_n_predict,
                  static_cast<int>(max_output_chars)));

  const Deadline deadline =
      Clock::now() + std::chrono::milliseconds(timeout_msec);

  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return Error(ErrnoTag("socket_create_failed"));
  }
  const auto close_fd = [&fd]() {
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
  };
  if (!SetCloseOnExec(fd) || !SetNonBlocking(fd) || !DisableSigPipe(fd)) {
    const std::string debug = ErrnoTag("socket_configure_failed");
    close_fd();
    return Error(debug);
  }

  const IoResult connect_result = ConnectLoopback(fd, request.port, deadline);
  if (connect_result != IoResult::kOk) {
    const std::string debug = connect_result == IoResult::kTimeout
                                  ? "http_connect_timeout"
                                  : ErrnoTag("http_connect_failed");
    close_fd();
    return connect_result == IoResult::kTimeout ? Timeout(debug)
                                                 : Error(debug);
  }

  const std::string http_request = BuildHttpRequest(request, n_predict);
  const IoResult write_result = WriteAll(fd, http_request, deadline);
  if (write_result != IoResult::kOk) {
    const std::string debug = write_result == IoResult::kTimeout
                                  ? "http_write_timeout"
                                  : ErrnoTag("http_write_failed");
    close_fd();
    return write_result == IoResult::kTimeout ? Timeout(debug)
                                               : Error(debug);
  }

  std::string received;
  received.reserve(4096);
  HttpResponseParts parts;
  bool headers_parsed = false;
  std::string chunked_body;
  std::array<char, 4096> buffer = {};

  while (true) {
    if (headers_parsed) {
      if (parts.content_length.has_value()) {
        if (*parts.content_length > kMaxHttpResponseBytes) {
          close_fd();
          return Error("http_response_too_large");
        }
        if (parts.body_prefix.size() >= *parts.content_length) {
          parts.body_prefix.resize(*parts.content_length);
          break;
        }
      } else if (parts.chunked) {
        std::string debug;
        const ChunkDecodeResult decode_result =
            DecodeChunkedBody(parts.body_prefix, &chunked_body, &debug);
        if (decode_result == ChunkDecodeResult::kComplete) {
          break;
        }
        if (decode_result == ChunkDecodeResult::kError) {
          close_fd();
          return Error(debug);
        }
      }
    }

    size_t bytes_read = 0;
    const IoResult read_result =
        ReadSome(fd, buffer.data(), buffer.size(), deadline, &bytes_read);
    if (read_result == IoResult::kTimeout) {
      close_fd();
      return Timeout("http_read_timeout");
    }
    if (read_result == IoResult::kError) {
      const std::string debug = ErrnoTag("http_read_failed");
      close_fd();
      return Error(debug);
    }
    if (read_result == IoResult::kEof) {
      if (!headers_parsed) {
        close_fd();
        return Error("http_response_truncated");
      }
      if (parts.content_length.has_value() &&
          parts.body_prefix.size() < *parts.content_length) {
        close_fd();
        return Error("http_body_truncated");
      }
      if (parts.chunked) {
        std::string debug;
        const ChunkDecodeResult decode_result =
            DecodeChunkedBody(parts.body_prefix, &chunked_body, &debug);
        if (decode_result != ChunkDecodeResult::kComplete) {
          close_fd();
          return Error(decode_result == ChunkDecodeResult::kError
                           ? debug
                           : "http_chunked_body_truncated");
        }
      }
      break;
    }

    if (!headers_parsed) {
      if (received.size() + bytes_read >
          kMaxHttpHeaderBytes + kMaxHttpResponseBytes) {
        close_fd();
        return Error("http_response_too_large");
      }
      received.append(buffer.data(), bytes_read);
      const size_t header_end = received.find("\r\n\r\n");
      if (header_end == std::string::npos) {
        if (received.size() > kMaxHttpHeaderBytes) {
          close_fd();
          return Error("http_header_too_large");
        }
        continue;
      }
      std::string debug;
      if (!ParseHttpHeaders(received, &parts, &debug)) {
        close_fd();
        return Error(debug);
      }
      headers_parsed = true;
      received.clear();
      if (parts.body_prefix.size() > kMaxHttpResponseBytes) {
        close_fd();
        return Error("http_response_too_large");
      }
    } else {
      if (parts.body_prefix.size() + bytes_read > kMaxHttpResponseBytes) {
        close_fd();
        return Error("http_response_too_large");
      }
      parts.body_prefix.append(buffer.data(), bytes_read);
    }
  }
  close_fd();

  if (parts.status_code != 200) {
    return Error("http_status_" + std::to_string(parts.status_code));
  }

  const std::string& response_body = parts.chunked ? chunked_body
                                                    : parts.body_prefix;
  std::string content;
  if (!ExtractJsonStringField(response_body, "content", &content)) {
    return Error("content_field_not_found");
  }
  content = CleanGeneratedText(std::move(content), max_output_chars);
  if (content.empty()) {
    return Error("empty_content");
  }

  LlamaHttpCompletionResponse response;
  response.status = LlamaHttpCompletionStatus::kOk;
  response.value = std::move(content);
  response.debug = "ok";
  return response;
}

}  // namespace zenz_scorer
}  // namespace mozc
