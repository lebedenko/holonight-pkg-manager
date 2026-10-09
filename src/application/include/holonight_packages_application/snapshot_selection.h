#pragma once

#include "holonight_packages_domain/pending_update.h"

#include <cstdint>
#include <optional>

namespace holonight_packages_application {

enum class SnapshotChoice : std::uint8_t { Local, Online };

// Freshness rule: the online snapshot wins unless the local result's dataAsOf (oldest real sync database mtime) is
// greater than or equal to the online snapshot's dataAsOf. Without an online snapshot, or when the local databases
// are missing, the choice is forced.
[[nodiscard]] SnapshotChoice selectFresherSnapshot(
    const std::optional<holonight_packages_domain::UpdateSnapshot>& local,
    const std::optional<holonight_packages_domain::UpdateSnapshot>& online);

}  // namespace holonight_packages_application
