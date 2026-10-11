#pragma once

#include "holonight_packages_domain/pending_update.h"

#include <cstdint>
#include <optional>

namespace holonight_packages_application {

enum class SnapshotChoice : std::uint8_t { Local, Online };

// Evaluated results and explicitly historical fallback rows take precedence over saved online rows.
// Ports without either marker retain the aggregate timestamp rule; a missing side forces the choice.
[[nodiscard]] SnapshotChoice selectFresherSnapshot(
    const std::optional<holonight_packages_domain::UpdateSnapshot>& local,
    const std::optional<holonight_packages_domain::UpdateSnapshot>& online);

}  // namespace holonight_packages_application
