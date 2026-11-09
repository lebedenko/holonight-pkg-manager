#include "signature_policy.h"

#include <alpm.h>

namespace holonight_packages_transaction {

bool isRequiredTrustedOnlyPackageLevel(std::uint32_t level) noexcept {
  constexpr std::uint32_t kForbidden =
      ALPM_SIG_PACKAGE_OPTIONAL | ALPM_SIG_PACKAGE_MARGINAL_OK | ALPM_SIG_PACKAGE_UNKNOWN_OK;
  return (level & ALPM_SIG_PACKAGE) != 0 && (level & kForbidden) == 0;
}

std::optional<holonight_packages_domain::TransactionFailure> checkInstallSignatureLevels(
    std::span<const RepositoryLevel> repositories, const LogSink& journal) {
  holonight_packages_domain::TransactionFailure failure{
      .code = holonight_packages_domain::TransactionErrorCode::InsecureSigLevel,
      .names = {},
      .choices = {},
  };
  for (const RepositoryLevel& repository : repositories) {
    if (!repository.explicit_level || !isRequiredTrustedOnlyPackageLevel(repository.level)) {
      failure.names.push_back(repository.name);
      continue;
    }
    constexpr std::uint32_t kDatabaseNeeded = ALPM_SIG_DATABASE;
    if (journal &&
        ((repository.level & kDatabaseNeeded) == 0 || (repository.level & ALPM_SIG_DATABASE_OPTIONAL) != 0)) {
      journal(LogLevel::Info, "repository " + repository.name + " accepts unsigned or optionally signed databases");
    }
  }
  if (failure.names.empty()) {
    return std::nullopt;
  }
  return failure;
}

}  // namespace holonight_packages_transaction
