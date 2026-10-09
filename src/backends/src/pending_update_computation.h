#pragma once

#include "holonight_packages_domain/pending_update.h"
#include "pacman_config.h"

#include <alpm.h>
#include <vector>

namespace holonight_packages_backends {

// Compares the installed packages with the first sync database (in the given order) that has the package. Foreign
// packages (in no sync database) and packages that are not older are skipped. The single comparison used by both
// the local-only AlpmUpdateSource and the online AlpmUpdateChecker.
[[nodiscard]] std::vector<holonight_packages_domain::PendingUpdate> computePendingUpdates(
    alpm_list_t* installed_packages, const std::vector<alpm_db_t*>& sync_databases, const PacmanConfig& ignore_rules);

}  // namespace holonight_packages_backends
