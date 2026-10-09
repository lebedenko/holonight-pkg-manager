#pragma once

#include "holonight_packages_application/update_check_ports.h"

#include <QTimer>

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <random>

namespace holonight_packages_application {

class SystemClock final : public Clock {
 public:
  [[nodiscard]] std::chrono::system_clock::time_point now() const override { return std::chrono::system_clock::now(); }
};

class MtRandomSource final : public RandomSource {
 public:
  MtRandomSource() : engine_(std::random_device{}()) {}
  [[nodiscard]] std::int64_t uniform(std::int64_t low, std::int64_t high) override;

 private:
  std::mutex mutex_;
  std::mt19937_64 engine_;
};

// Single-shot QTimer; must be used from the thread that owns it.
class QtOneShotTimer final : public OneShotTimer {
 public:
  QtOneShotTimer();
  void start(std::chrono::milliseconds delay, std::function<void()> onTimeout) override;
  void stop() override;
  [[nodiscard]] bool active() const override { return timer_.isActive(); }
  [[nodiscard]] Qt::TimerType timerType() const { return timer_.timerType(); }

 private:
  QTimer timer_;
  std::function<void()> on_timeout_;
};

}  // namespace holonight_packages_application
