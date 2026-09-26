#ifndef MOZC_ZENZ_SCORER_SYNCHRONOUS_IO_DEADLINE_H_
#define MOZC_ZENZ_SCORER_SYNCHRONOUS_IO_DEADLINE_H_

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace mozc::zenz {

// Bounds a connected pipe's synchronous I/O, including FlushFileBuffers.
// The issuing thread retains ownership of every I/O buffer and pipe handle.
// Join the watchdog before leaving the connection, so cancellation cannot
// affect a subsequent connection or a recycled thread/handle.
class SynchronousIoDeadline {
 public:
  explicit SynchronousIoDeadline(std::chrono::milliseconds timeout)
      : SynchronousIoDeadline(nullptr, timeout) {}

  SynchronousIoDeadline(HANDLE io_handle,
                        std::chrono::milliseconds timeout)
      : io_handle_(io_handle) {
    // CancelSynchronousIo requires THREAD_TERMINATE on the target handle.
    // Open it explicitly instead of relying on the pseudo-handle's inherited
    // access mask, which can be insufficient under a restricted token.
    thread_handle_ =
        ::OpenThread(THREAD_TERMINATE, FALSE, ::GetCurrentThreadId());
    if (thread_handle_ == nullptr) return;
    watchdog_ = std::thread([this, timeout] {
      std::unique_lock<std::mutex> lock(mutex_);
      if (cv_.wait_for(lock, timeout, [this] { return done_; })) {
        return;
      }
      // Repeat after expiry: the issuing thread may be between two I/O calls
      // when cancellation first runs. ERROR_NOT_FOUND is harmless in that gap.
      while (!done_) {
        if (io_handle_ != nullptr) {
          ::CancelIoEx(io_handle_, nullptr);
        }
        ::CancelSynchronousIo(thread_handle_);
        cv_.wait_for(lock, std::chrono::milliseconds(10),
                     [this] { return done_; });
      }
    });
  }

  ~SynchronousIoDeadline() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      done_ = true;
    }
    cv_.notify_one();
    if (watchdog_.joinable()) {
      watchdog_.join();
    }
    if (thread_handle_ != nullptr) {
      ::CloseHandle(thread_handle_);
    }
  }

  SynchronousIoDeadline(const SynchronousIoDeadline&) = delete;
  SynchronousIoDeadline& operator=(const SynchronousIoDeadline&) = delete;
  bool valid() const { return thread_handle_ != nullptr; }

 private:
  // The issuing thread retains ownership and keeps this handle open until the
  // deadline object is destroyed and its watchdog has joined.
  HANDLE io_handle_ = nullptr;
  HANDLE thread_handle_ = nullptr;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool done_ = false;
  std::thread watchdog_;
};

}  // namespace mozc::zenz

#endif  // MOZC_ZENZ_SCORER_SYNCHRONOUS_IO_DEADLINE_H_
