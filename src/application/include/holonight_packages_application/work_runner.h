#pragma once

#include <functional>

namespace holonight_packages_application {

// Runs blocking transaction work off the service thread. `work` runs on a worker thread; `done` is called afterwards on
// the service thread. The two share state through captured pointers. Production: QtWorkRunner. Tests: a runner that
// runs inline or on demand.
class WorkRunner {
 public:
  WorkRunner() = default;
  WorkRunner(const WorkRunner&) = delete;
  WorkRunner& operator=(const WorkRunner&) = delete;
  WorkRunner(WorkRunner&&) = delete;
  WorkRunner& operator=(WorkRunner&&) = delete;
  virtual ~WorkRunner();

  virtual void run(std::function<void()> work, std::function<void()> done) = 0;
};

}  // namespace holonight_packages_application
