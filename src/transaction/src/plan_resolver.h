#pragma once

#include "alpm_session.h"
#include "holonight_packages_domain/transaction_plan.h"
#include "holonight_packages_domain/transaction_request.h"

#include <expected>

namespace holonight_packages_transaction {

enum class ResolveMode : unsigned char {
  // Reads the databases without taking libalpm's lock and releases the transaction afterwards.
  Preview,
  // Re-resolution before a commit: libalpm's lock is taken by alpm_trans_init and the prepared transaction stays open
  // in the session, so the caller can commit exactly what it compared.
  Execute,
};

// Resolves an install or remove request to a ResolvedPlan, following the refusal order of REQ-F-030 (DESIGN 6.3):
// stage one before alpm_trans_prepare, stage two after it. The first condition that applies is returned.
class PlanResolver {
 public:
  PlanResolver(AlpmSession& session, LogSink journal) : session_(&session), journal_(std::move(journal)) {}

  [[nodiscard]] std::expected<holonight_packages_domain::ResolvedPlan, holonight_packages_domain::TransactionFailure>
  resolve(const holonight_packages_domain::PlanRequest& request, ResolveMode mode);

 private:
  AlpmSession* session_;
  LogSink journal_;
};

}  // namespace holonight_packages_transaction
