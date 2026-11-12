#pragma once

#include "alpm_session.h"
#include "commit_gate.h"
#include "download_classifier.h"
#include "error_mapping.h"
#include "event_mapper.h"
#include "failure_reporter.h"
#include "holonight_packages_domain/transaction_engine.h"
#include "holonight_packages_transaction/transaction_seams.h"

namespace holonight_packages_transaction {

// One Execute (DESIGN 4.2, 6): opens a handle, re-resolves the approved request under libalpm's database lock,
// compares it with the approved plan and commits exactly that plan.
//
// The lock is libalpm's own: it takes db.lck in alpm_trans_init and removes it in alpm_trans_release (spike S-1: a
// transaction with NOLOCK cannot be committed). The re-resolution therefore runs inside the transaction that is then
// committed, so the lock covers both, and a lock held by another process is Busy before anything changed.
class ExecutionDriver {
 public:
  ExecutionDriver(const TransactionConfig& config, LogSink journal, const TransactionSeams& seams,
                  const holonight_packages_domain::PlanRequest& request,
                  const holonight_packages_domain::ResolvedPlan& approved,
                  const holonight_packages_domain::ExecutionContext& context);

  [[nodiscard]] holonight_packages_domain::ExecutionResult run();

 private:
  void observe(std::string_view stage) const;
  void onEvent(const alpm_event_t& event);
  struct Attempt {
    holonight_packages_domain::ExecutionResult result;
    bool retry = false;
  };
  // One pass: re-resolve under the lock, compare, commit. `retry` is set when the pass failed before the commit point
  // because libalpm deleted a corrupt package file (REQ-F-055).
  [[nodiscard]] Attempt attempt(AlpmSession& session, int number);
  [[nodiscard]] holonight_packages_domain::ExecutionResult commit(AlpmSession& session);
  [[nodiscard]] holonight_packages_domain::ExecutionResult failureResult(AlpmSession& session, alpm_errno_t error,
                                                                         alpm_list_t* data) const;

  const TransactionConfig* config_;
  LogSink journal_;
  const TransactionSeams* seams_;
  const holonight_packages_domain::PlanRequest* request_;
  const holonight_packages_domain::ResolvedPlan* approved_;
  const holonight_packages_domain::ExecutionContext* context_;
  FailureReporter reporter_;
  CommitGate gate_;
  std::unique_ptr<EventMapper> mapper_;
  CommitStage stage_ = CommitStage::BeforeCommitPoint;
  bool interrupted_ = false;
  int injectedFault_ = 0;
  alpm_handle_t* handle_ = nullptr;
  std::vector<holonight_packages_domain::ChangedPackage> changed_;
  std::vector<holonight_packages_domain::WarningToken> warnings_;
};

}  // namespace holonight_packages_transaction
