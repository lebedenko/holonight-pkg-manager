#include "holonight_packages_application/idle_monitor.h"

#include <algorithm>

namespace holonight_packages_application {

IdleMonitor::IdleMonitor(const Clock& clock, OneShotTimer& idleTimer, OneShotTimer& expiryTimer, Hooks hooks,
                         std::chrono::seconds idle)
    : clock_(&clock), idleTimer_(&idleTimer), expiryTimer_(&expiryTimer), hooks_(std::move(hooks)), idle_(idle) {}

void IdleMonitor::evaluate() {
  if (const auto next = hooks_.next_expiry()) {
    // One millisecond past the deadline: the plan is expired only when it is older than its lifetime.
    const auto wait = std::max(std::chrono::duration_cast<std::chrono::milliseconds>(*next - clock_->now()),
                               std::chrono::milliseconds(0)) +
                      std::chrono::milliseconds(1);
    expiryTimer_->start(wait, [this] {
      hooks_.sweep_expired();
      evaluate();
    });
  } else {
    expiryTimer_->stop();
  }

  if (hooks_.has_activity()) {
    idleTimer_->stop();
  } else if (!idleTimer_->active()) {
    idleTimer_->start(std::chrono::duration_cast<std::chrono::milliseconds>(idle_), [this] {
      if (!hooks_.has_activity()) {
        hooks_.on_idle();
      }
    });
  }
}

}  // namespace holonight_packages_application
