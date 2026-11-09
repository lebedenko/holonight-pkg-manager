#pragma once

#include "holonight_packages_domain/transaction_error.h"
#include "holonight_packages_transaction/log_sink.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace holonight_packages_transaction {

struct RepositoryLevel {
  std::string name;
  std::uint32_t level = 0;      // alpm_siglevel_t as read back from libalpm after registration
  bool explicit_level = false;  // pacman.conf set a SigLevel (SI-14)
};

// True only for packages that must be signed (Required) by a trusted key (TrustedOnly): the signature bit set, and the
// optional, marginal-ok and unknown-ok bits clear (REQ-F-039, OI-05).
[[nodiscard]] bool isRequiredTrustedOnlyPackageLevel(std::uint32_t level) noexcept;

// REQ-F-039..F-041: refuses every install when any enabled repository is weaker than Required + TrustedOnly, or has no
// explicit SigLevel at all. Database-level optional/never settings are accepted and journaled (OI-05). The
// configuration is only ever read.
[[nodiscard]] std::optional<holonight_packages_domain::TransactionFailure> checkInstallSignatureLevels(
    std::span<const RepositoryLevel> repositories, const LogSink& journal);

}  // namespace holonight_packages_transaction
