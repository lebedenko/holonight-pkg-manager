#pragma once

#include "holonight_packages_domain/transaction_error.h"
#include "holonight_packages_domain/transaction_plan.h"
#include "holonight_packages_domain/transaction_progress.h"
#include "holonight_packages_domain/transaction_request.h"
#include "holonight_packages_domain/transaction_result.h"

#include <expected>
#include <functional>

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
};

}  // namespace holonight_packages_domain
