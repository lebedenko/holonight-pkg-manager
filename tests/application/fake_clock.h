#pragma once

#include "holonight_packages_application/update_check_ports.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace holonight_packages_testing {

class FakeClock;

// OneShotTimer driven by FakeClock::advance(); never waits in real time.
class FakeTimer final : public holonight_packages_application::OneShotTimer {
 public:
  explicit FakeTimer(FakeClock* clock);
  FakeTimer(const FakeTimer&) = delete;
  FakeTimer& operator=(const FakeTimer&) = delete;
  FakeTimer(FakeTimer&&) = delete;
  FakeTimer& operator=(FakeTimer&&) = delete;
  ~FakeTimer() override;

  void start(std::chrono::milliseconds delay, std::function<void()> onTimeout) override;
  void stop() override;
  [[nodiscard]] bool active() const override { return active_; }
  [[nodiscard]] std::chrono::system_clock::time_point due() const { return due_; }

 private:
  friend class FakeClock;
  FakeClock* clock_;
  bool active_ = false;
  std::chrono::system_clock::time_point due_;
  std::function<void()> onTimeout_;
};

class FakeClock final : public holonight_packages_application::Clock {
 public:
  explicit FakeClock(std::chrono::system_clock::time_point start =
                         std::chrono::system_clock::time_point{std::chrono::seconds{1'760'000'000}})
      : now_(start) {}

  [[nodiscard]] std::chrono::system_clock::time_point now() const override {
    const std::scoped_lock lock(mutex_);
    return now_;
  }

  void set(std::chrono::system_clock::time_point value) {
    const std::scoped_lock lock(mutex_);
    now_ = value;
  }

  [[nodiscard]] std::unique_ptr<FakeTimer> makeTimer() { return std::make_unique<FakeTimer>(this); }

  // Earliest due time of an active timer, if any.
  [[nodiscard]] std::optional<std::chrono::system_clock::time_point> nextDue() const;

  // Moves time forward, firing due timers in due order. Timers armed by a callback fire too when they fall inside
  // the advanced window.
  void advance(std::chrono::milliseconds delta) {
    const auto target = now() + delta;
    while (true) {
      FakeTimer* next = nullptr;
      for (FakeTimer* timer : timers_) {
        if (timer->active_ && timer->due_ <= target && (next == nullptr || timer->due_ < next->due_)) {
          next = timer;
        }
      }
      if (next == nullptr) {
        break;
      }
      set(std::max(now(), next->due_));
      next->active_ = false;
      auto callback = next->onTimeout_;
      if (callback) {
        callback();
      }
    }
    set(target);
  }

 private:
  friend class FakeTimer;
  mutable std::mutex mutex_;
  std::chrono::system_clock::time_point now_;
  std::vector<FakeTimer*> timers_;
};

inline std::optional<std::chrono::system_clock::time_point> FakeClock::nextDue() const {
  std::optional<std::chrono::system_clock::time_point> result;
  for (const FakeTimer* timer : timers_) {
    if (timer->active_ && (!result || timer->due_ < *result)) {
      result = timer->due_;
    }
  }
  return result;
}

inline FakeTimer::FakeTimer(FakeClock* clock) : clock_(clock) { clock_->timers_.push_back(this); }

inline FakeTimer::~FakeTimer() { std::erase(clock_->timers_, this); }

inline void FakeTimer::start(std::chrono::milliseconds delay, std::function<void()> onTimeout) {
  due_ = clock_->now() + delay;
  onTimeout_ = std::move(onTimeout);
  active_ = true;
}

inline void FakeTimer::stop() { active_ = false; }

}  // namespace holonight_packages_testing
