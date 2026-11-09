#pragma once

#include "holonight_packages_application/work_runner.h"

#include <functional>
#include <utility>

namespace holonight_packages_testing {

// Runs work inline, or holds it until runPending() so a test can observe the service while work is "running".
class ManualWorkRunner final : public holonight_packages_application::WorkRunner {
 public:
  bool deferred = false;

  void run(std::function<void()> work, std::function<void()> done) override {
    if (!deferred) {
      work();
      done();
      return;
    }
    work_ = std::move(work);
    done_ = std::move(done);
  }

  [[nodiscard]] bool pending() const { return static_cast<bool>(work_); }

  // Runs the held work and then its completion.
  void runPending() {
    auto work = std::exchange(work_, nullptr);
    auto done = std::exchange(done_, nullptr);
    if (work) {
      work();
    }
    if (done) {
      done();
    }
  }

 public:
  std::function<void()> work_;
  std::function<void()> done_;
};

}  // namespace holonight_packages_testing
