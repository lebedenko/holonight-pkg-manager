#pragma once

#include "holonight_packages_application/update_check_policy.h"
#include "holonight_packages_application/update_check_ports.h"
#include "holonight_packages_application/update_check_service.h"

#include <QObject>

#include <memory>

namespace holonight_packages_application {

// Schedules automatic checks: a startup delay, then interval plus jitter, with fixed backoff after failures. The
// timer is armed only after a check finishes, so there is never a second check pending while one runs.
class UpdateCheckScheduler : public QObject {
  Q_OBJECT

 public:
  UpdateCheckScheduler(UpdateCheckService& service, std::unique_ptr<OneShotTimer> timer,
                       std::shared_ptr<RandomSource> random, UpdateCheckPolicy policy);

  // Arms the startup delay. Does nothing when the backend cannot check online.
  void start();

 private slots:
  void onCheckCompleted(const holonight_packages_application::UpdateCheckOutcome& outcome);

 private:
  void arm(int consecutiveFailures);
  void fire();

  UpdateCheckService& service_;
  std::unique_ptr<OneShotTimer> timer_;
  std::shared_ptr<RandomSource> random_;
  UpdateCheckPolicy policy_;
  int consecutive_failures_ = 0;
};

}  // namespace holonight_packages_application
