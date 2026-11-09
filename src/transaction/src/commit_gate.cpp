#include "commit_gate.h"

namespace holonight_packages_transaction {

holonight_packages_domain::CancelDecision CommitGate::requestCancel() noexcept {
  const std::scoped_lock lock(mutex_);
  if (state_ == State::Committed) {
    return holonight_packages_domain::CancelDecision::PastPointOfNoReturn;
  }
  state_ = State::CancelRequested;
  return holonight_packages_domain::CancelDecision::Accepted;
}

bool CommitGate::tryCommit() noexcept {
  const std::scoped_lock lock(mutex_);
  if (state_ == State::CancelRequested) {
    return false;
  }
  state_ = State::Committed;
  return true;
}

CommitGate::State CommitGate::state() const noexcept {
  const std::scoped_lock lock(mutex_);
  return state_;
}

}  // namespace holonight_packages_transaction
