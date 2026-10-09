#include "holonight_packages_application/update_check_scheduler.h"

#include <utility>

namespace holonight_packages_application {

UpdateCheckScheduler::UpdateCheckScheduler(UpdateCheckService& service, std::unique_ptr<OneShotTimer> timer,
                                           std::shared_ptr<RandomSource> random, UpdateCheckPolicy policy)
    : QObject(&service), service_(service), timer_(std::move(timer)), random_(std::move(random)), policy_(policy) {
  connect(&service_, &UpdateCheckService::checkCompleted, this, &UpdateCheckScheduler::onCheckCompleted);
}

void UpdateCheckScheduler::start() {
  if (!service_.canCheckForUpdates()) {
    return;
  }
  timer_->start(std::chrono::duration_cast<std::chrono::milliseconds>(policy_.startupDelay), [this] { fire(); });
}

void UpdateCheckScheduler::fire() { service_.requestCheck(CheckOrigin::Automatic); }

void UpdateCheckScheduler::arm(int consecutiveFailures) {
  timer_->start(nextAutomaticDelay(policy_, consecutiveFailures, *random_), [this] { fire(); });
}

void UpdateCheckScheduler::onCheckCompleted(const UpdateCheckOutcome& outcome) {
  if (outcome.origin == CheckOrigin::Automatic) {
    consecutive_failures_ = outcome.succeeded ? 0 : consecutive_failures_ + 1;
    arm(consecutive_failures_);
    return;
  }
  // An on-demand failure changes nothing. An on-demand success proves connectivity and refreshes the data, so it
  // resets the counter and pushes the next automatic check out by a full interval.
  if (outcome.succeeded) {
    consecutive_failures_ = 0;
    arm(0);
  } else if (!timer_->active()) {
    // The pending automatic tick fired while this check ran and joined it; keep the schedule alive.
    arm(consecutive_failures_);
  }
}

}  // namespace holonight_packages_application
