#pragma once

#include "holonight_packages_domain/transaction_error.h"
#include "holonight_packages_domain/transaction_plan.h"
#include "holonight_packages_domain/transaction_progress.h"
#include "holonight_packages_domain/transaction_request.h"
#include "holonight_packages_domain/transaction_result.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>

namespace holonight_packages_domain {

// A plan the helper prepared and the caller may confirm. `plan` is the helper's preview: targets, dependency installs,
// removals and orphans as PlannedChange (kind Target/Dependency/Orphan), the hooks, and the action.
struct PreparedPlan {
  std::string plan_id;  // opaque; held in memory only
  ResolvedPlan plan;
  std::chrono::system_clock::time_point expires_at;  // whole seconds, as sent on the wire
};

// Why a call produced no domain answer, or a refusal the helper returned as a normal reply.
struct ServiceError {
  enum class Kind : std::uint8_t {
    Refused,  // a normal reply carrying a code: PreparePlan refusals, Cancel's PlanNotFound/PastPointOfNoReturn
    HelperUnavailable,  // the helper's name has no owner and cannot be activated, or no bus connection
    NoReply,            // timeout, peer vanished, or a reply that cannot be decoded: the outcome cannot be established
    Rejected,           // the bus returned an error reply (a protocol fault); the helper did not act on the call
  };
  Kind kind = Kind::Rejected;
  TransactionFailure failure;  // meaningful when kind == Refused

  bool operator==(const ServiceError&) const = default;
};

using PrepareReply = std::expected<PreparedPlan, ServiceError>;
// A domain failure of Execute (NotAuthorized, PlanExpired, ...) is a successful reply whose outcome is Failed.
using ExecuteReply = std::expected<ExecutionResult, ServiceError>;
using CancelReply = std::expected<void, ServiceError>;

// The port the GUI drives. Implemented by DBusTransactionService (apps/packages) and by FakeTransactionService (tests).
// Contract: every completion callback is called exactly once, on the caller's thread, and never from inside the call
// that started it. The port has no `options` parameter: the adapter always sends an empty map (REQ-C-004).
class TransactionService {
 public:
  using PrepareDone = std::function<void(PrepareReply)>;
  using ExecuteDone = std::function<void(ExecuteReply)>;
  using CancelDone = std::function<void(CancelReply)>;
  // Called for every Progress signal of the plan the service prepared last (REQ-F-044), in arrival order.
  using ProgressHandler = std::function<void(const std::string& plan_id, const ProgressUpdate&)>;

  TransactionService() = default;
  TransactionService(const TransactionService&) = delete;
  TransactionService& operator=(const TransactionService&) = delete;
  TransactionService(TransactionService&&) = delete;
  TransactionService& operator=(TransactionService&&) = delete;
  virtual ~TransactionService();

  virtual void setProgressHandler(ProgressHandler handler) = 0;
  virtual void preparePlan(const PlanRequest& request, PrepareDone done) = 0;
  // Only the plan ID goes over the wire (REQ-F-005).
  virtual void execute(const std::string& plan_id, ExecuteDone done) = 0;
  virtual void cancel(const std::string& plan_id, CancelDone done) = 0;
};

}  // namespace holonight_packages_domain
