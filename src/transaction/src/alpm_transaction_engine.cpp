#include "holonight_packages_transaction/alpm_transaction_engine.h"

#include "alpm_session.h"
#include "execution_driver.h"
#include "fetch_seam.h"
#include "plan_resolver.h"
#include "preparation_control.h"
#include "scratch_dbpath.h"
#include "sync_refresher.h"
#include "upgrade_driver.h"
#include "upgrade_resolver.h"

#include <memory>

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

namespace {

using holonight_packages_domain::UpgradePrepareOutcome;

UpgradePrepareOutcome refused(TransactionFailure failure, bool stuck = false) {
  UpgradePrepareOutcome outcome;
  outcome.result = std::unexpected(std::move(failure));
  outcome.worker_stuck = stuck;
  return outcome;
}

UpgradePrepareOutcome refused(TransactionErrorCode code) {
  return refused(TransactionFailure{.code = code, .names = {}, .choices = {}});
}

}  // namespace

UpgradePrepareOutcome AlpmTransactionEngine::prepareUpgrade(const std::filesystem::path& scratch_root,
                                                            const ExecutionContext& context) {
  PreparationControl control;
  const ControlPublication publication(context, &control);

  // The configuration checks come before anything is created or downloaded (DESIGN 5.2).
  auto settings = AlpmSession::loadUpgradeSettings(config_, journal_);
  if (!settings) {
    return refused(std::move(settings.error()));
  }
  std::vector<std::string> repositories;
  for (const auto& repository : settings->repositories.repositories) {
    repositories.push_back(repository.name);
  }
  const ScratchDbpath scratch{.root = scratch_root};
  auto seeded = prepareScratchDbpath(config_.database_path, scratch, repositories, journal_);
  if (!seeded) {
    return refused(std::move(seeded.error()));
  }
  if (control.cancelled()) {
    return refused(TransactionErrorCode::Cancelled);
  }
  auto opened =
      AlpmSession::openUpgrade(config_, AlpmSession::UpgradeRole::Scratch, scratch.dbpath(), &seams_, journal_);
  if (!opened) {
    return refused(std::move(opened.error()));
  }
  installFetchSeam((*opened)->handle(), seams_);
  const std::shared_ptr<AlpmSession> session(std::move(*opened));
  SyncRefresher refresher(session, config_.upgrade_limits, seeded->seeded_bytes, context.progress, journal_);
  const SyncOutcome synced = refresher.run(control);
  if (synced.failure) {
    return refused(*synced.failure, synced.worker_stuck);
  }
  if (control.cancelled()) {
    return refused(TransactionErrorCode::Cancelled);
  }
  UpgradeResolver resolver(*session, journal_);
  auto detail = resolver.resolve(ResolveMode::Preview);
  if (!detail) {
    return refused(std::move(detail.error()));
  }
  ResolvedPlan plan;
  plan.action = holonight_packages_domain::PlanAction::Upgrade;
  plan.upgrade = std::move(*detail);
  UpgradePrepareOutcome outcome;
  outcome.result =
      holonight_packages_domain::UpgradePrepared{.plan = std::move(plan), .guard = std::move(seeded->guard)};
  return outcome;
}

ExecutionResult AlpmTransactionEngine::executeUpgrade(const ResolvedPlan& approved,
                                                      const holonight_packages_domain::UpgradeWorkspace& workspace,
                                                      const ExecutionContext& context) {
  UpgradeDriver driver(config_, journal_, seams_, approved, workspace, context);
  return driver.run();
}

ExecutionResult AlpmTransactionEngine::execute(const PlanRequest& request, const ResolvedPlan& approved,
                                               const ExecutionContext& context) {
  ExecutionDriver driver(config_, journal_, seams_, request, approved, context);
  return driver.run();
}

}  // namespace holonight_packages_transaction
