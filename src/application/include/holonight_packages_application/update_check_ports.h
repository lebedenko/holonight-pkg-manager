#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace holonight_packages_application {

// Wall clock for timestamps and cooldown.
class Clock {
 public:
  Clock() = default;
  Clock(const Clock&) = default;
  Clock(Clock&&) = default;
  Clock& operator=(const Clock&) = default;
  Clock& operator=(Clock&&) = default;
  virtual ~Clock();

  // Thread-safe.
  [[nodiscard]] virtual std::chrono::system_clock::time_point now() const = 0;
};

class RandomSource {
 public:
  RandomSource() = default;
  RandomSource(const RandomSource&) = default;
  RandomSource(RandomSource&&) = default;
  RandomSource& operator=(const RandomSource&) = default;
  RandomSource& operator=(RandomSource&&) = default;
  virtual ~RandomSource();

  // Inclusive on both ends.
  [[nodiscard]] virtual std::int64_t uniform(std::int64_t low, std::int64_t high) = 0;
};

class OneShotTimer {
 public:
  OneShotTimer() = default;
  OneShotTimer(const OneShotTimer&) = default;
  OneShotTimer(OneShotTimer&&) = default;
  OneShotTimer& operator=(const OneShotTimer&) = default;
  OneShotTimer& operator=(OneShotTimer&&) = default;
  virtual ~OneShotTimer();

  // Restarts the timer when already active.
  virtual void start(std::chrono::milliseconds delay, std::function<void()> onTimeout) = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual bool active() const = 0;
};

}  // namespace holonight_packages_application
