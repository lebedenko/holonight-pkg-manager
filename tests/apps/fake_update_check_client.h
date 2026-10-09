#pragma once

#include "UpdateCheckClient.h"

namespace holonight_packages_testing {

// Counts checkNow() calls and lets a test drive the status and the signals.
class FakeUpdateCheckClient final : public UpdateCheckClient {
 public:
  using UpdateCheckClient::UpdateCheckClient;

  void checkNow() override { ++check_now_calls_; }
  [[nodiscard]] const UpdateCheckClientStatus& status() const override { return status_; }

  [[nodiscard]] int checkNowCalls() const { return check_now_calls_; }

  void setStatus(const UpdateCheckClientStatus& status) {
    status_ = status;
    emit statusChanged();
  }
  void completeCheck() { emit checkCompleted(); }
  void failCheckNow() { emit checkNowFailed(); }

 private:
  UpdateCheckClientStatus status_;
  int check_now_calls_ = 0;
};

}  // namespace holonight_packages_testing
