#pragma once

#include "holonight_packages_domain/transaction_error.h"

#include <alpm.h>

namespace holonight_packages_transaction {

// Where in an operation libalpm reported the failure (DESIGN 10.2).
enum class FailureContext : unsigned char {
  Resolve,                    // inside the resolver or a re-resolution
  BeforeCommitPoint,          // inside the commit, before the first HOOK_START(PRE) / TRANSACTION_START
  AtCommitPointBeforeChange,  // at or after that event, before the first PACKAGE_OPERATION_START
  AfterChange,                // a package change has started
};

// The only alpm_errno_t to TransactionErrorCode table. `downloadCode` is the classifier's verdict for a download
// failure (SyncDatabaseStale, DownloadFailed or SignatureInvalid for a missing .sig); it is used for the download
// errnos only.
[[nodiscard]] holonight_packages_domain::TransactionErrorCode mapAlpmError(
    alpm_errno_t error, FailureContext context,
    holonight_packages_domain::TransactionErrorCode downloadCode =
        holonight_packages_domain::TransactionErrorCode::DownloadFailed) noexcept;

}  // namespace holonight_packages_transaction
