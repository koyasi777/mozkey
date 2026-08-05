#include "zenz_scorer/llama_backend.h"

#if defined(_WIN32)
#error "llama_backend.cc must not be built on Windows"
#endif

#include <utility>

#include "zenz/zenz_wire_protocol.h"
#include "zenz_scorer/llama_http_client.h"

namespace mozc {
namespace zenz_scorer {
namespace {

PosixZenzResponse ConvertResponse(
    LlamaHttpCompletionResponse completion) {
  PosixZenzResponse response;
  response.value = std::move(completion.value);
  response.debug = completion.debug.empty() ? "llama_http_unknown"
                                            : std::move(completion.debug);

  switch (completion.status) {
    case LlamaHttpCompletionStatus::kOk:
      response.status = ::mozc::zenz::kZenzWireStatusOk;
      break;
    case LlamaHttpCompletionStatus::kTimeout:
      response.status = ::mozc::zenz::kZenzWireStatusTimeout;
      response.value.clear();
      break;
    case LlamaHttpCompletionStatus::kError:
      response.status = ::mozc::zenz::kZenzWireStatusError;
      response.value.clear();
      break;
  }
  return response;
}

}  // namespace

PosixZenzBackend CreateLlamaHttpBackend(LlamaBackendOptions options) {
  return [options = std::move(options)](
             const PosixZenzRequest& request) -> PosixZenzResponse {
    LlamaHttpCompletionRequest completion_request;
    completion_request.port = options.port;
    completion_request.api_key = options.api_key;
    completion_request.n_predict = options.n_predict;
    completion_request.timeout_msec = request.timeout_msec;
    completion_request.max_output_chars = request.max_output_chars;
    completion_request.prompt = request.prompt;
    return ConvertResponse(
        PostLlamaHttpCompletion(completion_request));
  };
}

}  // namespace zenz_scorer
}  // namespace mozc
