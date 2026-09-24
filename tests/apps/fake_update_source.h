#pragma once

#include "holonight_packages_domain/update_source.h"

#include <condition_variable>
#include <deque>
#include <expected>
#include <mutex>

namespace holonight_packages_testing {

// Controllable UpdateSource for model and view tests. Each loadUpdates() call pops the next queued result (an
// empty, databases-found snapshot when the queue is empty). While blocking, every call waits for release().
class FakeUpdateSource : public holonight_packages_domain::UpdateSource {
 public:
  using LoadResult =
      std::expected<holonight_packages_domain::UpdateSnapshot, holonight_packages_domain::UpdateSourceError>;

  FakeUpdateSource() = default;
  FakeUpdateSource(const FakeUpdateSource&) = delete;
  FakeUpdateSource& operator=(const FakeUpdateSource&) = delete;
  FakeUpdateSource(FakeUpdateSource&&) = delete;
  FakeUpdateSource& operator=(FakeUpdateSource&&) = delete;
  ~FakeUpdateSource() override = default;

  void enqueue(LoadResult result) {
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

  [[nodiscard]] LoadResult loadUpdates() const override {
    std::unique_lock lock(mutex_);
    ++call_count_;
    if (blocking_) {
      released_.wait(lock, [this] { return released_calls_ > 0; });
      --released_calls_;
    }
    if (results_.empty()) {
      return holonight_packages_domain::UpdateSnapshot{};
    }
    LoadResult result = std::move(results_.front());
    results_.pop_front();
    return result;
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable released_;
  mutable std::deque<LoadResult> results_;
  mutable int call_count_ = 0;
  mutable int released_calls_ = 0;
  bool blocking_ = false;
};

}  // namespace holonight_packages_testing
