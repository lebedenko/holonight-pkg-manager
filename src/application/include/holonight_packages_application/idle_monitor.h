#pragma once

#include "holonight_packages_application/update_check_ports.h"

#include <chrono>
#include <functional>
#include <optional>

namespace holonight_packages_application {

// Ends the helper when it has nothing to do (REQ-F-068). It owns two timers:
//   - the idle timer: started when no plan exists and the work slot is free, stopped by any activity; when it fires
//     after `idle` (60 s) the helper exits;
//   - the expiry timer: armed for the next pending plan's expiry, so an unused plan is swept, stops counting as
//     activity, and the idle period can begin only then.
// Call evaluate() after every change to the plan store or the work slot.
class IdleMonitor {
 public:
  struct Hooks {
    std::function<bool()> has_activity;
    std::function<std::optional<std::chrono::system_clock::time_point>()> next_expiry;
    std::function<void()> sweep_expired;
    std::function<void()> on_idle;
  };

  IdleMonitor(const Clock& clock, OneShotTimer& idleTimer, OneShotTimer& expiryTimer, Hooks hooks,
              std::chrono::seconds idle = std::chrono::seconds(60));

  void evaluate();

 private:
  const Clock* clock_;
  OneShotTimer* idleTimer_;
  OneShotTimer* expiryTimer_;
  Hooks hooks_;
  std::chrono::seconds idle_;
};

}  // namespace holonight_packages_application
