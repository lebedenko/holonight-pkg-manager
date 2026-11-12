#pragma once

#include "alpm_session.h"
#include "commit_gate.h"
#include "event_mapper.h"
#include "failure_reporter.h"
#include "holonight_packages_domain/transaction_engine.h"
#include "holonight_packages_transaction/transaction_seams.h"
#include "scratch_dbpath.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace holonight_packages_transaction {

// One Execute of a system upgrade (system-upgrade DESIGN 5.6):
//   1 configuration re-check   2 pre-fetch re-resolution   3 disk-space estimate   4 DOWNLOADONLY fetch (scratch)
//   5 verification when the fetch deferred a missing key   6 release of the scratch handle
//   7 live lock   8 guard   9 re-resolution   10 point of no return   11 promotion   12 keyring step   13 main step
//   14 finish
// A cancel is honoured at every phase boundary up to 10; from 10 on it is refused and the upgrade runs to its end.
// Every failure after 10 is UpgradePartiallyApplied with the failed step.
class UpgradeDriver {
 public:
  UpgradeDriver(const TransactionConfig& config, LogSink journal, const TransactionSeams& seams,
                const holonight_packages_domain::ResolvedPlan& approved,
                const holonight_packages_domain::UpgradeWorkspace& workspace,
                const holonight_packages_domain::ExecutionContext& context);

  [[nodiscard]] holonight_packages_domain::ExecutionResult run();

 private:
  using Result = holonight_packages_domain::ExecutionResult;

  void observe(std::string_view stage) const;
  [[nodiscard]] int fault(std::string_view stage) const;
  void emit(holonight_packages_domain::ProgressPhase phase, std::uint32_t current, std::uint32_t total) const;
  [[nodiscard]] std::optional<Result> recheckAndResolveBeforeFetch(AlpmSession& scratch);
  [[nodiscard]] std::optional<Result> estimateSpace(const AlpmSession& scratch) const;
  [[nodiscard]] std::optional<Result> fetch(AlpmSession& scratch);
  [[nodiscard]] std::optional<Result> fetchOnce(AlpmSession& scratch, bool& retry);
  [[nodiscard]] std::optional<Result> verifyCached(AlpmSession& scratch, bool& retry);
  [[nodiscard]] std::optional<Result> lockGuardAndCompare(AlpmSession& live, AlpmSession& scratch);
  [[nodiscard]] std::optional<Result> keyringStep(AlpmSession& live);
  [[nodiscard]] std::optional<Result> mainStep(AlpmSession& live);
  [[nodiscard]] Result commitStep(AlpmSession& live, const char* step, std::size_t installs, std::size_t removals,
                                  bool last);
  [[nodiscard]] Result partial(const char* step) const;
  void onStepEvent(const alpm_event_t& event, alpm_handle_t* handle);

  const TransactionConfig* config_;
  LogSink journal_;
  const TransactionSeams* seams_;
  const holonight_packages_domain::ResolvedPlan* approved_;
  const holonight_packages_domain::UpgradeWorkspace* workspace_;
  const holonight_packages_domain::ExecutionContext* context_;
  ScratchDbpath scratch_;
  FailureReporter reporter_;
  CommitGate gate_;
  std::unique_ptr<EventMapper> mapper_;
  std::vector<holonight_packages_domain::ChangedPackage> changed_;
  std::vector<holonight_packages_domain::WarningToken> warnings_;
  int injectedFault_ = 0;
  bool keyDeferred_ = false;
};

}  // namespace holonight_packages_transaction
