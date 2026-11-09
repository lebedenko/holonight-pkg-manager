#pragma once

#include "holonight_packages_domain/transaction_engine.h"

#include <mutex>

namespace holonight_packages_transaction {

// Orders a cancel against the commit point (REQ-F-056..058). A mutex makes the two transitions atomic:
//   Pending         -> CancelRequested   by requestCancel() before the commit point
//   Pending         -> Committed         by tryCommit() at the commit point
// Once Committed, requestCancel() answers PastPointOfNoReturn and changes nothing.
class CommitGate final : public holonight_packages_domain::ExecutionControl {
 public:
  enum class State : unsigned char { Pending, CancelRequested, Committed };

  // Thread-safe and idempotent.
  holonight_packages_domain::CancelDecision requestCancel() noexcept override;

  // Called by the execution at the commit point. True when the execution may go on (and can no longer be cancelled);
  // false when a cancel won the race and the execution must stop instead.
  [[nodiscard]] bool tryCommit() noexcept;

  [[nodiscard]] State state() const noexcept;
  [[nodiscard]] bool cancelRequested() const noexcept { return state() == State::CancelRequested; }

 private:
  mutable std::mutex mutex_;
  State state_ = State::Pending;
};

}  // namespace holonight_packages_transaction
