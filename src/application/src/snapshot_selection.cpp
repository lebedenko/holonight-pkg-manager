#include "holonight_packages_application/snapshot_selection.h"

namespace holonight_packages_application {

SnapshotChoice selectFresherSnapshot(const std::optional<holonight_packages_domain::UpdateSnapshot>& local,
                                     const std::optional<holonight_packages_domain::UpdateSnapshot>& online) {
  if (local && local->previously_loaded) {
    return SnapshotChoice::Local;
  }
  if (local && local->evaluated) {
    return online && online->repositories.empty() ? SnapshotChoice::Online : SnapshotChoice::Local;
  }
  if (!online.has_value()) {
    return SnapshotChoice::Local;
  }
  if (!local.has_value() || !local->databasesFound) {
    return SnapshotChoice::Online;
  }
  return local->dataAsOf >= online->dataAsOf ? SnapshotChoice::Local : SnapshotChoice::Online;
}

}  // namespace holonight_packages_application
