#include "holonight_packages_transaction/alpm_transaction_engine.h"

#include "alpm_session.h"
#include "execution_driver.h"
#include "plan_resolver.h"

namespace holonight_packages_transaction {

using holonight_packages_domain::ExecutionContext;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::PlanRequest;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;

AlpmTransactionEngine::AlpmTransactionEngine(TransactionConfig config, LogSink journal, TransactionSeams seams)
    : config_(std::move(config)), journal_(std::move(journal)), seams_(std::move(seams)) {}

std::expected<ResolvedPlan, TransactionFailure> AlpmTransactionEngine::resolve(const PlanRequest& request) {
  auto session = AlpmSession::open(config_, request.action, journal_);
  if (!session) {
    return std::unexpected(session.error());
  }
  PlanResolver resolver(**session, journal_);
  return resolver.resolve(request, ResolveMode::Preview);
}

ExecutionResult AlpmTransactionEngine::execute(const PlanRequest& request, const ResolvedPlan& approved,
                                               const ExecutionContext& context) {
  ExecutionDriver driver(config_, journal_, seams_, request, approved, context);
  return driver.run();
}

}  // namespace holonight_packages_transaction
