#pragma once

#include "holonight_packages_domain/history_source.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace holonight_test {

// A HistorySource double that returns queued results, records the mode and calling thread of every read, and can
// hold a read until it is released (REQ-F-017, REQ-F-022, REQ-F-024).
class CountingHistorySource final : public holonight_packages_domain::HistorySource {
 public:
  holonight_packages_domain::HistoryReadResult read(holonight_packages_domain::HistoryReadMode mode) override {
    std::unique_lock lock(mutex_);
    modes_.push_back(mode);
    threads_.push_back(std::this_thread::get_id());
    ++started_;
    changed_.notify_all();
    changed_.wait(lock, [this] { return !blocked_; });
    if (results_.empty()) {
      holonight_packages_domain::HistoryReadResult empty;
      empty.reset = mode == holonight_packages_domain::HistoryReadMode::Full;
      return empty;
    }
    auto result = std::move(results_.front());
    results_.pop_front();
    return result;
  }

  void enqueue(holonight_packages_domain::HistoryReadResult result) {
    const std::scoped_lock lock(mutex_);
    results_.push_back(std::move(result));
  }

  // While blocked, read() waits after recording the call.
  void block() {
    const std::scoped_lock lock(mutex_);
    blocked_ = true;
  }
  void release() {
    const std::scoped_lock lock(mutex_);
    blocked_ = false;
    changed_.notify_all();
  }
  // Waits until `count` reads have started.
  void waitForStarted(std::size_t count) {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this, count] { return started_ >= count; });
  }

  [[nodiscard]] std::vector<holonight_packages_domain::HistoryReadMode> modes() const {
    const std::scoped_lock lock(mutex_);
    return modes_;
  }
  [[nodiscard]] std::vector<std::thread::id> threads() const {
    const std::scoped_lock lock(mutex_);
    return threads_;
  }
  [[nodiscard]] std::size_t calls() const {
    const std::scoped_lock lock(mutex_);
    return modes_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<holonight_packages_domain::HistoryReadResult> results_;
  std::vector<holonight_packages_domain::HistoryReadMode> modes_;
  std::vector<std::thread::id> threads_;
  std::size_t started_ = 0;
  bool blocked_ = false;
};

}  // namespace holonight_test
