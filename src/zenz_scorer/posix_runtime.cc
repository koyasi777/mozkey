#include "zenz_scorer/posix_runtime.h"

#if defined(_WIN32)
#error "posix_runtime.cc must not be built on Windows"
#endif

#include <memory>
#include <string>
#include <utility>

#include "zenz/zenz_wire_protocol.h"
#include "zenz_scorer/llama_backend.h"
#include "zenz_scorer/posix_server.h"

namespace mozc {
namespace zenz_scorer {
namespace {

constexpr int kMinimumNPredict = 4;
constexpr int kMaximumNPredict = 256;

PosixZenzResponse ChildUnavailableResponse() {
  PosixZenzResponse response;
  response.status = ::mozc::zenz::kZenzWireStatusError;
  response.debug = "llama_server_not_running";
  return response;
}

}  // namespace

PosixScorerRuntime::PosixScorerRuntime(PosixScorerRuntimeOptions options)
    : options_(std::move(options)), llama_process_(options_.llama_server) {}

PosixScorerRuntime::~PosixScorerRuntime() { Stop(); }

bool PosixScorerRuntime::Start(std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();

  if (server_ != nullptr || llama_process_.running()) {
    *error = "already_started";
    return false;
  }
  if (options_.n_predict < kMinimumNPredict ||
      options_.n_predict > kMaximumNPredict) {
    *error = "invalid_n_predict";
    return false;
  }

  if (!llama_process_.Start(error)) {
    return false;
  }

  LlamaBackendOptions backend_options;
  backend_options.port = llama_process_.port();
  backend_options.api_key = llama_process_.api_key();
  backend_options.n_predict = options_.n_predict;
  PosixZenzBackend http_backend =
      CreateLlamaHttpBackend(std::move(backend_options));

  server_ = std::make_unique<PosixZenzScorerServer>(
      options_.socket_path,
      [this, http_backend = std::move(http_backend)](
          const PosixZenzRequest& request) mutable -> PosixZenzResponse {
        if (!llama_process_.running()) {
          return ChildUnavailableResponse();
        }
        return http_backend(request);
      });

  if (!server_->Start(error)) {
    server_.reset();
    llama_process_.Stop();
    return false;
  }
  return true;
}

bool PosixScorerRuntime::ServeOne(int timeout_msec, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  error->clear();

  if (server_ == nullptr) {
    *error = "not_started";
    return false;
  }
  if (!llama_process_.running()) {
    *error = "llama_server_exited";
    return false;
  }
  return server_->ServeOne(timeout_msec, error);
}

void PosixScorerRuntime::Stop() {
  // Remove the public-facing socket before stopping the child so no new
  // requests can enter while the localhost backend is shutting down.
  server_.reset();
  llama_process_.Stop();
}

bool PosixScorerRuntime::running() {
  return server_ != nullptr && llama_process_.running();
}

}  // namespace zenz_scorer
}  // namespace mozc
