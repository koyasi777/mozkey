#ifndef MOZC_SESSION_ZENZ_LIVE_CORRECTOR_H_
#define MOZC_SESSION_ZENZ_LIVE_CORRECTOR_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "absl/time/time.h"

namespace mozc {
namespace session {

struct ZenzLiveRequest {
  uint32_t generation = 0;

  // Original Mozc reading, usually hiragana.
  std::string key;

  // zenz prompt:
  //   U+EE02 + left_context + U+EE00 + reading_katakana + U+EE01
  std::string prompt;

  std::string reading_katakana;
  std::string left_context;

  // Current visible Mozc live conversion result.
  std::string mozc_value;

  // Runtime options.
  std::string pipe_name;
  uint32_t timeout_msec = 180;
  uint32_t max_output_chars = 128;

  absl::Time issued_at;
};

struct ZenzLiveResponse {
  uint32_t generation = 0;
  std::string key;
  std::string value;
  std::string debug;

  bool ok = false;
  bool timeout = false;

  absl::Duration latency = absl::ZeroDuration();
};

class ZenzClient {
 public:
  virtual ~ZenzClient() = default;

  virtual bool IsAvailable() const = 0;

  virtual ZenzLiveResponse Convert(const ZenzLiveRequest& request) = 0;

  // Sticky until Stop() has joined the worker.  A transport registers only a
  // wakeup callback; ownership of pipe/socket handles remains on the worker.
  void RequestStop() {
    std::lock_guard<std::mutex> lock(stop_mutex_);
    stop_requested_.store(true);
    if (interrupt_) {
      interrupt_();
    }
  }

  void ResetStop() { stop_requested_.store(false); }
  bool IsStopRequested() const { return stop_requested_.load(); }

  class ScopedInterrupt {
   public:
    ScopedInterrupt(ZenzClient& client, std::function<void()> interrupt)
        : client_(client) {
      std::lock_guard<std::mutex> lock(client_.stop_mutex_);
      client_.interrupt_ = std::move(interrupt);
      if (client_.IsStopRequested()) {
        client_.interrupt_();
      }
    }

    ~ScopedInterrupt() {
      std::lock_guard<std::mutex> lock(client_.stop_mutex_);
      client_.interrupt_ = nullptr;
    }

    ScopedInterrupt(const ScopedInterrupt&) = delete;
    ScopedInterrupt& operator=(const ScopedInterrupt&) = delete;

   private:
    ZenzClient& client_;
  };

 private:
  std::atomic<bool> stop_requested_{false};
  std::mutex stop_mutex_;
  std::function<void()> interrupt_;
};

class ZenzLiveCorrector {
 public:
  explicit ZenzLiveCorrector(std::unique_ptr<ZenzClient> client);
  ~ZenzLiveCorrector();

  ZenzLiveCorrector(const ZenzLiveCorrector&) = delete;
  ZenzLiveCorrector& operator=(const ZenzLiveCorrector&) = delete;

  void Start();
  void Stop();

  // Latest-only submission. A queued old request is overwritten.
  void Submit(ZenzLiveRequest request);

  // Clears queued request/result. Running inference is not forcibly cancelled;
  // stale discard is handled by generation check.
  void CancelPending();

  // Returns the latest result only if its generation matches.
  std::optional<ZenzLiveResponse> TakeResult(uint32_t generation);

 private:
  void WorkerLoop();

  std::unique_ptr<ZenzClient> client_;

  std::mutex mu_;
  std::condition_variable cv_;

  bool started_ = false;
  bool stop_ = false;

  std::optional<ZenzLiveRequest> latest_request_;
  std::optional<ZenzLiveResponse> latest_result_;

  std::thread worker_;
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ZENZ_LIVE_CORRECTOR_H_
