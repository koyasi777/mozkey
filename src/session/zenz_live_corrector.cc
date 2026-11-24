#include "session/zenz_live_corrector.h"

#include <utility>

#include "absl/log/log.h"

namespace mozc {
namespace session {

ZenzLiveCorrector::ZenzLiveCorrector(std::unique_ptr<ZenzClient> client)
    : client_(std::move(client)) {}

ZenzLiveCorrector::~ZenzLiveCorrector() {
  Stop();
}

void ZenzLiveCorrector::Start() {
  std::lock_guard<std::mutex> lock(mu_);
  if (started_) {
    return;
  }
  stop_ = false;
  if (client_) {
    client_->ResetStop();
  }
  started_ = true;
  worker_ = std::thread(&ZenzLiveCorrector::WorkerLoop, this);
}

void ZenzLiveCorrector::Stop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!started_) {
      return;
    }
    stop_ = true;
    if (client_) {
      client_->RequestStop();
    }
    latest_request_.reset();
    latest_result_.reset();
  }
  cv_.notify_all();

  if (worker_.joinable()) {
    worker_.join();
  }

  std::lock_guard<std::mutex> lock(mu_);
  started_ = false;
}

void ZenzLiveCorrector::Submit(ZenzLiveRequest request) {
  Start();

  {
    std::lock_guard<std::mutex> lock(mu_);
    ++request_epoch_;
    // Wake a superseded in-flight Convert(). There is only one worker;
    // ResetCancel() is performed when that worker dequeues the next request.
    if (client_) {
      client_->RequestCancel();
    }
    latest_request_ = std::move(request);
    latest_result_.reset();
  }
  cv_.notify_one();
}

void ZenzLiveCorrector::CancelPending() {
  std::lock_guard<std::mutex> lock(mu_);
  ++request_epoch_;
  if (client_) {
    client_->RequestCancel();
  }
  latest_request_.reset();
  latest_result_.reset();
}

std::optional<ZenzLiveResponse> ZenzLiveCorrector::TakeResult(
    uint32_t generation) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!latest_result_.has_value()) {
    return std::nullopt;
  }
  if (latest_result_->generation != generation) {
    latest_result_.reset();
    return std::nullopt;
  }

  std::optional<ZenzLiveResponse> result = std::move(latest_result_);
  latest_result_.reset();
  return result;
}

void ZenzLiveCorrector::WorkerLoop() {
  while (true) {
    std::optional<ZenzLiveRequest> request;
    uint64_t request_epoch = 0;

    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [this] {
        return stop_ || latest_request_.has_value();
      });

      if (stop_) {
        return;
      }

      request_epoch = request_epoch_;
      // Serialize ResetCancel() with Submit()/CancelPending() under mu_;
      // otherwise a new request could clear an in-flight cancellation.
      if (client_) {
        client_->ResetCancel();
      }
      request = std::move(latest_request_);
      latest_request_.reset();
    }

    if (!request.has_value()) {
      continue;
    }

    ZenzLiveResponse response;
    response.generation = request->generation;
    response.key = request->key;

    if (!client_ || !client_->IsAvailable()) {
      response.ok = false;
      response.debug = "zenz_client_unavailable";
    } else {
      response = client_->Convert(*request);
    }

    {
      std::lock_guard<std::mutex> lock(mu_);

      // A cancellation or superseding submission must invalidate the result
      // even when an in-flight Convert() eventually returns. Generation alone
      // is insufficient here because a client may reuse it in a later submit.
      if (!stop_ && request_epoch == request_epoch_) {
        latest_result_ = std::move(response);
      }
    }
  }
}

}  // namespace session
}  // namespace mozc
