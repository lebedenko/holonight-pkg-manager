#pragma once

#include "holonight_packages_domain/update_checker.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <expected>
#include <mutex>
#include <thread>
#include <vector>

namespace holonight_packages_testing {

// Controllable UpdateChecker. Each call pops the next queued result (an empty snapshot when the queue is empty).
// While blocking, every call waits for release(). Records concurrency and the calling threads.
class FakeUpdateChecker final : public holonight_packages_domain::UpdateChecker {
 public:
  using CheckResult =
      std::expected<holonight_packages_domain::UpdateSnapshot, holonight_packages_domain::UpdateCheckError>;

  FakeUpdateChecker() = default;
  FakeUpdateChecker(const FakeUpdateChecker&) = delete;
  FakeUpdateChecker& operator=(const FakeUpdateChecker&) = delete;
  FakeUpdateChecker(FakeUpdateChecker&&) = delete;
  FakeUpdateChecker& operator=(FakeUpdateChecker&&) = delete;
  ~FakeUpdateChecker() override = default;

  void enqueue(CheckResult result) {
    const std::scoped_lock lock(mutex_);
    results_.push_back(std::move(result));
  }

  void setBlocking(bool blocking) {
    const std::scoped_lock lock(mutex_);
    blocking_ = blocking;
    released_calls_ = 0;
  }

  // Lets one pending (or the next) blocked call complete.
  void release() {
    {
      const std::scoped_lock lock(mutex_);
      ++released_calls_;
    }
    released_.notify_all();
  }

  [[nodiscard]] int callCount() const {
    const std::scoped_lock lock(mutex_);
    return call_count_;
  }

  [[nodiscard]] int maxConcurrency() const {
    const std::scoped_lock lock(mutex_);
    return max_concurrency_;
  }

  [[nodiscard]] std::vector<std::thread::id> callerThreads() const {
    const std::scoped_lock lock(mutex_);
    return callers_;
  }

  [[nodiscard]] CheckResult checkForUpdates() const override {
    std::unique_lock lock(mutex_);
    ++call_count_;
    ++running_;
    max_concurrency_ = std::max(max_concurrency_, running_);
    callers_.push_back(std::this_thread::get_id());
    if (blocking_) {
      released_.wait(lock, [this] { return released_calls_ > 0; });
      --released_calls_;
    }
    --running_;
    if (results_.empty()) {
      return holonight_packages_domain::UpdateSnapshot{};
    }
    CheckResult result = std::move(results_.front());
    results_.pop_front();
    return result;
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable released_;
  mutable std::deque<CheckResult> results_;
  mutable int call_count_ = 0;
  mutable int running_ = 0;
  mutable int max_concurrency_ = 0;
  mutable int released_calls_ = 0;
  mutable std::vector<std::thread::id> callers_;
  bool blocking_ = false;
};

}  // namespace holonight_packages_testing
