#pragma once

#include "holonight_packages_domain/transaction_error.h"

#include <alpm.h>
#include <cstddef>
#include <expected>
#include <set>
#include <string>
#include <vector>

namespace holonight_packages_transaction {

inline constexpr std::size_t kMaxProtectedSetSize = 4096;

// REQ-F-035: the dependency closure of the `base` package (from the local database when it is installed, from the sync
// databases otherwise) together with every HoldPkg name. Each package is visited once, so cyclic graphs terminate
// (REQ-NF-003). A closure above 4096 packages is an InternalError (OI-03).
[[nodiscard]] std::expected<std::set<std::string>, holonight_packages_domain::TransactionFailure> computeProtectedSet(
    alpm_handle_t* handle, const std::vector<std::string>& holdPkgs);

}  // namespace holonight_packages_transaction
