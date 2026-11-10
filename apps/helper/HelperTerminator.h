#pragma once

#include "holonight_packages_application/terminator.h"

#include <chrono>
#include <functional>

namespace holonight_helper {

// Schedules finalization after the bus has had time to carry the Cancelled reply.
// Production finalization synchronizes lock cleanup with the worker and never returns.
class HelperTerminator final : public holonight_packages_application::Terminator {
 public:
  using FinalizeFunction = std::function<void()>;

  explicit HelperTerminator(FinalizeFunction finalize,
                            std::chrono::milliseconds grace = std::chrono::milliseconds(150));
  void terminateAfterReply() override;

 private:
  FinalizeFunction finalize_;
  std::chrono::milliseconds grace_;
  bool scheduled_ = false;
};

}  // namespace holonight_helper
