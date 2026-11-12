#pragma once

#include "alpm_session.h"
#include "holonight_packages_domain/transaction_progress.h"
#include "holonight_packages_transaction/transaction_config.h"
#include "preparation_control.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace holonight_packages_transaction {

struct SyncOutcome {
  // Empty on success.
  std::optional<holonight_packages_domain::TransactionFailure> failure;
  // libalpm's download could not be stopped (timeout, size cap, cancel): the process must end (REQ-F-003).
  bool worker_stuck = false;
};

// The one place that refreshes sync databases (rule H-004), and only those of a scratch handle (system-upgrade
// DESIGN 5.3). The database update runs on a detached worker; the caller waits for the end, the internal timeout, the
// scratch size cap or a cancel, whichever comes first. A worker that is still downloading then keeps the session alive
// until the process ends, because libalpm cannot interrupt a download (helper SPIKES S-2).
class SyncRefresher {
 public:
  // `seeded_bytes` count towards the cap. `progress` gets one Syncing update per repository whose database finished.
  SyncRefresher(std::shared_ptr<AlpmSession> session, UpgradeLimits limits, std::uint64_t seeded_bytes,
                holonight_packages_domain::ProgressSink progress, LogSink journal);

  [[nodiscard]] SyncOutcome run(PreparationControl& control);

 private:
  [[nodiscard]] holonight_packages_domain::TransactionErrorCode failureCode(alpm_errno_t error) const;
  void journal(const std::string& text) const;

  std::shared_ptr<AlpmSession> session_;
  UpgradeLimits limits_;
  std::uint64_t seeded_bytes_;
  holonight_packages_domain::ProgressSink progress_;
  LogSink journal_;
};

}  // namespace holonight_packages_transaction
