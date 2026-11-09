#pragma once

#include "holonight_packages_application/terminator.h"

#include <chrono>
#include <filesystem>
#include <functional>

namespace holonight_helper {

// Ends the helper after a cancel (REQ-F-056). It first removes the lock file that this process's own transaction holds,
// so the next package-manager run is not blocked, then waits a short grace period for the bus to carry the `Cancelled`
// reply and exits. The bus starts a new helper on the next call. The exit function is injectable for tests.
class HelperTerminator final : public holonight_packages_application::Terminator {
 public:
  using ExitFunction = std::function<void()>;

  explicit HelperTerminator(std::filesystem::path lockFile, ExitFunction exit = {},
                            std::chrono::milliseconds grace = std::chrono::milliseconds(150));
  void terminateAfterReply() override;

 private:
  std::filesystem::path lockFile_;
  ExitFunction exit_;
  std::chrono::milliseconds grace_;
};

}  // namespace holonight_helper
