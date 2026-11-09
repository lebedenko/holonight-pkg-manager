#pragma once

#include "holonight_packages_application/terminator.h"

#include <functional>

namespace holonight_packages_testing {

// Records that the helper would have ended, and what happened before that.
class RecordingTerminator final : public holonight_packages_application::Terminator {
 public:
  // Runs inside terminateAfterReply(), for tests that need to observe the order of events.
  std::function<void()> onTerminate;

  void terminateAfterReply() override {
    ++calls_;
    if (onTerminate) {
      onTerminate();
    }
  }

  [[nodiscard]] int calls() const { return calls_; }

 public:
  int calls_ = 0;
};

}  // namespace holonight_packages_testing
