#pragma once

#include "alpm_session.h"
#include "holonight_packages_domain/transaction_error.h"
#include "holonight_packages_domain/upgrade_plan.h"
#include "plan_resolver.h"

#include <expected>
#include <string>
#include <vector>

namespace holonight_packages_transaction {

// Resolves a full system upgrade (alpm_sync_sysupgrade) on a session and classifies the transaction into the preview
// lists (system-upgrade DESIGN 5.4, 5.5). Preview runs NOLOCK and releases the transaction; Execute keeps the
// prepared, locked transaction open in the session so the caller can commit exactly what was compared.
class UpgradeResolver {
 public:
  UpgradeResolver(AlpmSession& session, LogSink journal) : session_(&session), journal_(std::move(journal)) {}

  [[nodiscard]] std::expected<holonight_packages_domain::UpgradeDetail, holonight_packages_domain::TransactionFailure>
  resolve(ResolveMode mode);

  // The same in the transaction that is already open in the session, which stays open whatever the result.
  [[nodiscard]] std::expected<holonight_packages_domain::UpgradeDetail, holonight_packages_domain::TransactionFailure>
  resolveInOpenTransaction();

  // Classifies the transaction that is open in the session; `protectedNames` are HoldPkg and its friends.
  [[nodiscard]] std::expected<holonight_packages_domain::UpgradeDetail, holonight_packages_domain::TransactionFailure>
  classify(const std::vector<std::string>& protectedNames);

  // The package that names the keyring step (REQ-F-023).
  static constexpr const char* kKeyringPackage = "archlinux-keyring";

 private:
  AlpmSession* session_;
  LogSink journal_;
};

}  // namespace holonight_packages_transaction
