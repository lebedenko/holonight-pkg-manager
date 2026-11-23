#include "holonight_packages_application/snapshot_selection.h"

#include <cassert>
#include <utility>

namespace holonight_packages_application {

namespace {

SnapshotChoice selectSnapshot(const holonight_packages_domain::UpdateSnapshot* local,
                              const holonight_packages_domain::UpdateSnapshot* online) {
  if (local != nullptr && local->previously_loaded) {
    return SnapshotChoice::Local;
  }
  if (local != nullptr && local->evaluated) {
    return SnapshotChoice::Local;
  }
  if (online == nullptr) {
    return SnapshotChoice::Local;
  }
  if (local == nullptr || !local->databasesFound) {
    return SnapshotChoice::Online;
  }
  return local->dataAsOf >= online->dataAsOf ? SnapshotChoice::Local : SnapshotChoice::Online;
}

}  // namespace

SnapshotChoice selectFresherSnapshot(const std::optional<holonight_packages_domain::UpdateSnapshot>& local,
                                     const std::optional<holonight_packages_domain::UpdateSnapshot>& online) {
  return selectSnapshot(local ? &*local : nullptr, online ? &*online : nullptr);
}

void UpdateSnapshotState::recordEvaluation(holonight_packages_domain::UpdateSnapshot snapshot) {
  evaluation_ = std::move(snapshot);
}

void UpdateSnapshotState::adoptChecked(const holonight_packages_domain::CheckedSnapshot& snapshot) {
  checked_ = snapshot;
  checked_->snapshot.evaluated = false;
  checked_->snapshot.previously_loaded = true;
}

void UpdateSnapshotState::clearChecked() { checked_.reset(); }

const std::optional<holonight_packages_domain::UpdateSnapshot>& UpdateSnapshotState::evaluation() const {
  return evaluation_;
}

const std::optional<holonight_packages_domain::CheckedSnapshot>& UpdateSnapshotState::checked() const {
  return checked_;
}

const holonight_packages_domain::UpdateSnapshot& UpdateSnapshotState::selected() const {
  const auto* evaluated = evaluation_ ? &*evaluation_ : nullptr;
  const auto* historical = checked_ ? &checked_->snapshot : nullptr;
  assert(evaluated || historical);
  return selectSnapshot(evaluated, historical) == SnapshotChoice::Online ? *historical : *evaluated;
}

void UpdateSnapshotState::invalidateInFlightEvaluation(bool running) { invalidated_ |= running; }

bool UpdateSnapshotState::takeInvalidation() { return std::exchange(invalidated_, false); }

}  // namespace holonight_packages_application
