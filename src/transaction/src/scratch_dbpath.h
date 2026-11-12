#pragma once

#include "holonight_packages_domain/transaction_error.h"
#include "holonight_packages_domain/upgrade_plan.h"
#include "holonight_packages_transaction/log_sink.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace holonight_packages_transaction {

// The private layout of one upgrade plan under its scratch root `S` (system-upgrade DESIGN 5.3):
//   S/dbpath/local     symlink to the live local/ (read only through it)
//   S/dbpath/sync/     seeded copies of the live databases, then refreshed by the sync
//   S/dbpath/db.lck    libalpm's lock of the scratch handle
//   S/rollback/        copies of the live files that promotion replaces
// Every directory is mode 0711, so libalpm's download sandbox can traverse it but no one can list it (SPIKES S-1 f).
struct ScratchDbpath {
  std::filesystem::path root;

  [[nodiscard]] std::filesystem::path dbpath() const { return root / "dbpath"; }
  [[nodiscard]] std::filesystem::path sync() const { return dbpath() / "sync"; }
  [[nodiscard]] std::filesystem::path rollback() const { return root / "rollback"; }
};

struct SeededScratch {
  // The live sync files as they were when the copy was taken: the guard of REQ-F-051.
  std::vector<holonight_packages_domain::SyncFileStamp> guard;
  // Bytes copied into the scratch sync directory; they count towards the scratch size cap (REQ-F-010).
  std::uint64_t seeded_bytes = 0;
};

// Builds the layout and seeds the scratch sync directory with the live `<repository>.db` and `.db.sig` files, keeping
// their modification times so the sync sends If-Modified-Since (SPIKES S-1 e). A present live db.lck is Busy and
// creates nothing (DESIGN D-10). If the live files change while they are copied, the copy is repeated once; a second
// change is SyncFailed. The live directories are only read (REQ-F-007).
[[nodiscard]] std::expected<SeededScratch, holonight_packages_domain::TransactionFailure> prepareScratchDbpath(
    const std::filesystem::path& live_dbpath, const ScratchDbpath& scratch,
    const std::vector<std::string>& repositories, const LogSink& journal);

}  // namespace holonight_packages_transaction
