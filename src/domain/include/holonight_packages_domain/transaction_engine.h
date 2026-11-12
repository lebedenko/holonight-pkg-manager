#pragma once

#include "holonight_packages_domain/transaction_error.h"
#include "holonight_packages_domain/transaction_plan.h"
#include "holonight_packages_domain/transaction_progress.h"
#include "holonight_packages_domain/transaction_request.h"
#include "holonight_packages_domain/transaction_result.h"
#include "holonight_packages_domain/upgrade_plan.h"

#include <expected>
#include <filesystem>
#include <functional>
#include <vector>

namespace holonight_packages_domain {

// REQ-F-056..058. Accepted: the run stops before any change to the system and ends Cancelled.
enum class CancelDecision : std::uint8_t { Accepted, PastPointOfNoReturn };

// The handle the engine publishes for the duration of one execution, so the service can cancel it from another thread.
class ExecutionControl {
 public:
  ExecutionControl() = default;
  ExecutionControl(const ExecutionControl&) = delete;
  ExecutionControl& operator=(const ExecutionControl&) = delete;
  ExecutionControl(ExecutionControl&&) = delete;
  ExecutionControl& operator=(ExecutionControl&&) = delete;
  virtual ~ExecutionControl() = default;
  // Thread-safe and idempotent.
  virtual CancelDecision requestCancel() noexcept = 0;
};

struct ExecutionContext {
  // Called on the worker thread for every progress update.
  ProgressSink progress;
  // Called once with the control when the execution starts and once with nullptr when it ends.
  std::function<void(ExecutionControl*)> control_changed;
};

// A prepared system upgrade: the preview and the guard snapshot of the live sync databases (system-upgrade DESIGN 4.2).
struct UpgradePrepared {
  ResolvedPlan plan;
  std::vector<SyncFileStamp> guard;
};

struct UpgradePrepareOutcome {
  std::expected<UpgradePrepared, TransactionFailure> result =
      std::unexpected(TransactionFailure{.code = TransactionErrorCode::InternalError, .names = {}, .choices = {}});
  // A libalpm call could not be stopped (sync timeout, size cap or a cancel during the sync): the process must end
  // after the reply, because the worker would otherwise hold the work slot (REQ-F-003).
  bool worker_stuck = false;
};

// The port the application layer drives. The libalpm implementation lives in holonight_packages_transaction.
class TransactionEngine {
 public:
  TransactionEngine() = default;
  TransactionEngine(const TransactionEngine&) = delete;
  TransactionEngine& operator=(const TransactionEngine&) = delete;
  TransactionEngine(TransactionEngine&&) = delete;
  TransactionEngine& operator=(TransactionEngine&&) = delete;
  virtual ~TransactionEngine() = default;

  // Resolves a request against the current databases without changing anything (REQ-F-001). Runs on the caller's
  // thread.
  [[nodiscard]] virtual std::expected<ResolvedPlan, TransactionFailure> resolve(const PlanRequest& request) = 0;

  // Re-resolves `approved`, compares and runs it (REQ-F-044..048). Blocks until the transaction ends.
  [[nodiscard]] virtual ExecutionResult execute(const PlanRequest& request, const ResolvedPlan& approved,
                                                const ExecutionContext& context) = 0;

  // system-upgrade: syncs the databases into `scratch_root` (created by the caller), resolves the upgrade there and
  // takes the guard snapshot. Reports Syncing through context.progress and publishes a control, so a preparation can
  // be cancelled. Never touches the live sync or local directories (REQ-F-007). Blocks until it ends.
  [[nodiscard]] virtual UpgradePrepareOutcome prepareUpgrade(const std::filesystem::path& scratch_root,
                                                             const ExecutionContext& context) = 0;

  // Fetch, live lock, guard, re-resolution, promotion, keyring step, main step (DESIGN 5.6). Blocks until it ends.
  [[nodiscard]] virtual ExecutionResult executeUpgrade(const ResolvedPlan& approved, const UpgradeWorkspace& workspace,
                                                       const ExecutionContext& context) = 0;
};

}  // namespace holonight_packages_domain
