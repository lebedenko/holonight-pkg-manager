#include "holonight_packages_application/update_check_runtime.h"

#include <utility>

namespace holonight_packages_application {

std::int64_t MtRandomSource::uniform(std::int64_t low, std::int64_t high) {
  const std::scoped_lock lock(mutex_);
  return std::uniform_int_distribution<std::int64_t>(low, high)(engine_);
}

QtOneShotTimer::QtOneShotTimer() {
  timer_.setSingleShot(true);
  timer_.setTimerType(Qt::PreciseTimer);
  QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] {
    if (on_timeout_) {
      auto callback = on_timeout_;
      callback();
    }
  });
}

void QtOneShotTimer::start(std::chrono::milliseconds delay, std::function<void()> onTimeout) {
  on_timeout_ = std::move(onTimeout);
  timer_.start(delay);
}

void QtOneShotTimer::stop() { timer_.stop(); }

}  // namespace holonight_packages_application
