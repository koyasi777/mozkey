#ifndef MOZC_ZENZ_SCORER_LLAMA_HTTP_CLIENT_H_
#define MOZC_ZENZ_SCORER_LLAMA_HTTP_CLIENT_H_

#include <cstdint>
#include <string>

namespace mozc {
namespace zenz_scorer {

enum class LlamaHttpCompletionStatus {
  kOk,
  kError,
  kTimeout,
};

struct LlamaHttpCompletionRequest {
  int port = 0;
  std::string api_key;
  int n_predict = 64;
  uint32_t timeout_msec = 0;
  uint32_t max_output_chars = 0;
  std::string prompt;
};

struct LlamaHttpCompletionResponse {
  LlamaHttpCompletionStatus status = LlamaHttpCompletionStatus::kError;
  std::string value;
  std::string debug;
};

// Sends one non-streaming completion request to a llama-server listening on
// 127.0.0.1.  The timeout is an absolute budget shared by connect, write, and
// read operations.
LlamaHttpCompletionResponse PostLlamaHttpCompletion(
    const LlamaHttpCompletionRequest& request);

}  // namespace zenz_scorer
}  // namespace mozc

#endif  // MOZC_ZENZ_SCORER_LLAMA_HTTP_CLIENT_H_
