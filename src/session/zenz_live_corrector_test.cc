#include "session/zenz_live_corrector.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "testing/gunit.h"

namespace mozc {
namespace session {
namespace {

class BlockingZenzClient final : public ZenzClient {
 public:
  bool IsAvailable() const override { return true; }

  ZenzLiveResponse Convert(const ZenzLiveRequest& request) override {
    ScopedInterrupt interrupt(*this, [this] { cv_.notify_all(); });
    {
      std::unique_lock<std::mutex> lock(mu_);
      entered_.push_back(request.generation);
      const bool first_call = entered_.size() == 1;
      cv_.notify_all();
      cv_.wait(lock, [&] {
        return IsStopRequested() ||
               (first_call ? release_first_ : release_second_);
      });
    }
    ZenzLiveResponse response;
    response.generation = request.generation;
    response.key = request.key;
    response.ok = !IsStopRequested();
    response.value = "synthetic-result";
    return response;
  }

  bool WaitForEntries(std::size_t count) {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, std::chrono::seconds(5), [&] {
      return entered_.size() >= count;
    });
  }
  void ReleaseFirst() {
    std::lock_guard<std::mutex> lock(mu_);
    release_first_ = true;
    cv_.notify_all();
  }
  void ReleaseSecond() {
    std::lock_guard<std::mutex> lock(mu_);
    release_second_ = true;
    cv_.notify_all();
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  bool release_first_ = false;
  bool release_second_ = false;
  std::vector<uint32_t> entered_;
};

ZenzLiveRequest SyntheticRequest(uint32_t generation) {
  ZenzLiveRequest request;
  request.generation = generation;
  request.key = "test-only";
  return request;
}

std::optional<ZenzLiveResponse> WaitForResult(ZenzLiveCorrector* corrector,
                                               uint32_t generation) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    auto response = corrector->TakeResult(generation);
    if (response) {
      return response;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return std::nullopt;
}

TEST(ZenzLiveCorrectorTest, CancelledInFlightResultIsNotRepublished) {
  auto client = std::make_unique<BlockingZenzClient>();
  BlockingZenzClient* fake = client.get();
  ZenzLiveCorrector corrector(std::move(client));
  corrector.Submit(SyntheticRequest(1));
  ASSERT_TRUE(fake->WaitForEntries(1));

  corrector.CancelPending();
  corrector.Submit(SyntheticRequest(2));
  fake->ReleaseFirst();

  // Entry into the second Convert() proves the first Convert() has returned
  // and its worker publication step has completed.
  ASSERT_TRUE(fake->WaitForEntries(2));
  EXPECT_FALSE(corrector.TakeResult(1).has_value());

  fake->ReleaseSecond();
  auto result = WaitForResult(&corrector, 2);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok);
  EXPECT_EQ(result->generation, 2);
}

TEST(ZenzLiveCorrectorTest, SupersededSameGenerationIsNotRepublished) {
  auto client = std::make_unique<BlockingZenzClient>();
  BlockingZenzClient* fake = client.get();
  ZenzLiveCorrector corrector(std::move(client));
  corrector.Submit(SyntheticRequest(1));
  ASSERT_TRUE(fake->WaitForEntries(1));

  // Generation 1 is deliberately reused. Session normally increments its
  // generation, but the worker should invalidate by submission identity.
  corrector.Submit(SyntheticRequest(1));
  fake->ReleaseFirst();
  ASSERT_TRUE(fake->WaitForEntries(2));
  EXPECT_FALSE(corrector.TakeResult(1).has_value());

  fake->ReleaseSecond();
  auto result = WaitForResult(&corrector, 1);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok);
}

// Models a transport whose blocking receive is interrupted through the
// client's ScopedInterrupt, as in ZenzUnixSocketClient::Convert(). No model,
// process or system socket is used.
class CancelAwareZenzClient final : public ZenzClient {
 public:
  bool IsAvailable() const override { return true; }

  ZenzLiveResponse Convert(const ZenzLiveRequest& request) override {
    ScopedInterrupt interrupt(*this, [this] { cv_.notify_all(); });
    std::unique_lock<std::mutex> lock(mu_);
    entered_.push_back(request.generation);
    cv_.notify_all();
    if (entered_.size() == 1) {
      // Bounded wait also prevents test teardown from hanging after a failure.
      cv_.wait_for(lock, std::chrono::seconds(4), [this] {
        return IsStopRequested() || IsCancelRequested();
      });
      first_interrupted_ = IsCancelRequested();
    }
    ZenzLiveResponse response;
    response.generation = request.generation;
    response.key = request.key;
    response.ok = !IsStopRequested() && !IsCancelRequested();
    response.value = "synthetic-result";
    return response;
  }

  bool WaitForEntries(size_t count) {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, std::chrono::seconds(3), [&] {
      return entered_.size() >= count;
    });
  }
  bool FirstWasInterrupted() {
    std::lock_guard<std::mutex> lock(mu_);
    return first_interrupted_;
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::vector<uint32_t> entered_;
  bool first_interrupted_ = false;
};

TEST(ZenzLiveCorrectorTest, CancelPendingInterruptsActiveConversion) {
  auto client = std::make_unique<CancelAwareZenzClient>();
  CancelAwareZenzClient* fake = client.get();
  ZenzLiveCorrector corrector(std::move(client));
  corrector.Submit(SyntheticRequest(101));
  ASSERT_TRUE(fake->WaitForEntries(1));

  corrector.CancelPending();
  // The next request can complete without explicitly releasing the first.
  corrector.Submit(SyntheticRequest(102));
  ASSERT_TRUE(fake->WaitForEntries(2));
  EXPECT_TRUE(fake->FirstWasInterrupted());
  auto result = WaitForResult(&corrector, 102);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok);  // Non-sticky cancel must not affect request B.
  EXPECT_EQ(result->generation, 102);
}

TEST(ZenzLiveCorrectorTest, NewSubmitInterruptsSupersededConversion) {
  auto client = std::make_unique<CancelAwareZenzClient>();
  CancelAwareZenzClient* fake = client.get();
  ZenzLiveCorrector corrector(std::move(client));
  corrector.Submit(SyntheticRequest(201));
  ASSERT_TRUE(fake->WaitForEntries(1));

  // No explicit CancelPending(): Submit itself invalidates the active A.
  corrector.Submit(SyntheticRequest(202));
  ASSERT_TRUE(fake->WaitForEntries(2));
  EXPECT_TRUE(fake->FirstWasInterrupted());
  auto result = WaitForResult(&corrector, 202);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok);
}

// Test the race where a client is cancelled after the worker dequeues a
// request, but just before the transport registers its wakeup callback.
class LateRegisterZenzClient final : public ZenzClient {
 public:
  bool IsAvailable() const override { return true; }
  ZenzLiveResponse Convert(const ZenzLiveRequest& request) override {
    {
      std::unique_lock<std::mutex> lock(mu_);
      before_registration_ = true;
      cv_.notify_all();
      cv_.wait_for(lock, std::chrono::seconds(4), [this] {
        return allow_registration_ || IsStopRequested();
      });
    }
    ScopedInterrupt interrupt(*this, [this] {
      std::lock_guard<std::mutex> lock(mu_);
      notified_ = true;
      cv_.notify_all();
    });
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait_for(lock, std::chrono::seconds(3), [this] {
        return notified_ || IsStopRequested();
      });
    }
    ZenzLiveResponse response;
    response.generation = request.generation;
    response.ok = !IsStopRequested() && !IsCancelRequested();
    return response;
  }
  void WaitForRegistrationPoint() {
    std::unique_lock<std::mutex> lock(mu_);
    EXPECT_TRUE(cv_.wait_for(lock, std::chrono::seconds(3), [this] {
      return before_registration_;
    }));
  }
  void AllowRegistration() {
    std::lock_guard<std::mutex> lock(mu_);
    allow_registration_ = true;
    cv_.notify_all();
  }
  bool WasNotified() {
    std::lock_guard<std::mutex> lock(mu_);
    return notified_;
  }
 private:
  std::mutex mu_;
  std::condition_variable cv_;
  bool before_registration_ = false;
  bool allow_registration_ = false;
  bool notified_ = false;
};

TEST(ZenzLiveCorrectorTest, CancelBeforeTransportRegistrationIsNotLost) {
  auto client = std::make_unique<LateRegisterZenzClient>();
  LateRegisterZenzClient* fake = client.get();
  ZenzLiveCorrector corrector(std::move(client));
  corrector.Submit(SyntheticRequest(301));
  fake->WaitForRegistrationPoint();
  corrector.CancelPending();
  fake->AllowRegistration();
  // Wait until the original conversion has returned. Cancellation must be
  // delivered even when the callback was registered after CancelPending().
  corrector.Submit(SyntheticRequest(302));
  auto second = WaitForResult(&corrector, 302);
  ASSERT_TRUE(second.has_value());
  EXPECT_TRUE(fake->WasNotified());
}

}  // namespace
}  // namespace session
}  // namespace mozc
