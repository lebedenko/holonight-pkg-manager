#pragma once

#include "holonight_packages_domain/transaction_engine.h"

#include <functional>
#include <mutex>

namespace holonight_packages_transaction {

// The control a system upgrade preparation publishes (system-upgrade REQ-F-032). A preparation changes nothing live,
// so a cancel is always accepted; whoever waits on the preparation is woken through the registered waker.
class PreparationControl final : public holonight_packages_domain::ExecutionControl {
 public:
  holonight_packages_domain::CancelDecision requestCancel() noexcept override {
    std::function<void()> wake;
    {
      const std::scoped_lock lock(mutex_);
      cancelled_ = true;
      wake = waker_;
    }
    if (wake) {
      wake();
    }
    return holonight_packages_domain::CancelDecision::Accepted;
  }

  [[nodiscard]] bool cancelled() const noexcept {
    const std::scoped_lock lock(mutex_);
    return cancelled_;
  }

  // Called (without the control's lock) by every later requestCancel(); pass an empty function to clear it.
  void setWaker(std::function<void()> waker) {
    const std::scoped_lock lock(mutex_);
    waker_ = std::move(waker);
  }

 private:
  mutable std::mutex mutex_;
  bool cancelled_ = false;
  std::function<void()> waker_;
};

// Publishes `control` through the context for its lifetime: once with the control, once with nullptr at the end.
class ControlPublication {
 public:
  ControlPublication(const holonight_packages_domain::ExecutionContext& context,
                     holonight_packages_domain::ExecutionControl* control)
      : context_(&context) {
    if (context_->control_changed) {
      context_->control_changed(control);
    }
  }
  ~ControlPublication() {
    if (context_->control_changed) {
      context_->control_changed(nullptr);
    }
  }
  ControlPublication(const ControlPublication&) = delete;
  ControlPublication& operator=(const ControlPublication&) = delete;
  ControlPublication(ControlPublication&&) = delete;
  ControlPublication& operator=(ControlPublication&&) = delete;

 private:
  const holonight_packages_domain::ExecutionContext* context_;
};

}  // namespace holonight_packages_transaction
