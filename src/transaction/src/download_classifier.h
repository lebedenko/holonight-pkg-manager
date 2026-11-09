#pragma once

#include "holonight_packages_domain/transaction_error.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace holonight_packages_transaction {

struct DownloadClassification {
  holonight_packages_domain::TransactionErrorCode code =
      holonight_packages_domain::TransactionErrorCode::DownloadFailed;
  std::string file_name;  // the file that failed first; empty when no log line named one
};

// Whether `path` exists, as a stat(2) would tell. Injectable so the rule can be tested without a file system.
using PathExists = std::function<bool(const std::filesystem::path&)>;
// The errno of stat(2) on `path`, 0 when it exists.
using StatError = std::function<int(const std::filesystem::path&)>;

// REQ-F-028 and REQ-F-029 (DESIGN 10.3), from the libalpm log lines of a failed download. libalpm exposes no HTTP
// status, so the lines are all there is. Rules, first match wins (strings recorded by spike S-4 for pacman 7.1):
//   0. the failed file is a detached signature (`.sig`)                           -> SignatureInvalid (S-6)
//   1. the line says `returned error: 404` or `404 Not Found`                     -> SyncDatabaseStale
//   2. `Could not open file <path>` (file:// server): stat(path) is ENOENT/ENOTDIR -> SyncDatabaseStale
//   3. anything else, including 5xx, timeouts, TLS and DNS errors and EACCES       -> DownloadFailed
// Text that matches no rule is never classified as stale.
[[nodiscard]] DownloadClassification classifyDownloadFailure(const std::vector<std::string>& logLines,
                                                             const StatError& statError);

// stat(2)-based default for production.
[[nodiscard]] int systemStatError(const std::filesystem::path& path);

}  // namespace holonight_packages_transaction
