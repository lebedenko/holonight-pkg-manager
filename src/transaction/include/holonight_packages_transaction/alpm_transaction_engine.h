#pragma once

#include "holonight_packages_domain/transaction_engine.h"
#include "holonight_packages_transaction/log_sink.h"
#include "holonight_packages_transaction/transaction_config.h"
#include "holonight_packages_transaction/transaction_seams.h"

namespace holonight_packages_transaction {

// The libalpm implementation of the TransactionEngine port (DESIGN 6). Every operation opens its own handle from
// pacman.conf and the TransactionConfig; nothing is cached between plans.
class AlpmTransactionEngine final : public holonight_packages_domain::TransactionEngine {
 public:
  AlpmTransactionEngine(TransactionConfig config, LogSink journal, TransactionSeams seams = {});

  [[nodiscard]] std::expected<holonight_packages_domain::ResolvedPlan, holonight_packages_domain::TransactionFailure>
  resolve(const holonight_packages_domain::PlanRequest& request) override;

  [[nodiscard]] holonight_packages_domain::ExecutionResult execute(
      const holonight_packages_domain::PlanRequest& request, const holonight_packages_domain::ResolvedPlan& approved,
      const holonight_packages_domain::ExecutionContext& context) override;

  [[nodiscard]] holonight_packages_domain::UpgradePrepareOutcome prepareUpgrade(
      const std::filesystem::path& scratch_root, const holonight_packages_domain::ExecutionContext& context) override;

  [[nodiscard]] holonight_packages_domain::ExecutionResult executeUpgrade(
      const holonight_packages_domain::ResolvedPlan& approved,
      const holonight_packages_domain::UpgradeWorkspace& workspace,
      const holonight_packages_domain::ExecutionContext& context) override;

 private:
  TransactionConfig config_;
  LogSink journal_;
  TransactionSeams seams_;
};

}  // namespace holonight_packages_transaction
