#pragma once

#include "holonight_packages_transaction/transaction_config.h"

namespace holonight_packages_transaction {

// The start-up sweep of a system upgrade that a killed helper left behind (system-upgrade REQ-F-016, REQ-F-077): the
// temporary files of an interrupted promotion in the live sync directory. The live databases themselves are never
// touched; every replaced file was renamed into place atomically.
void sweepUpgradeLeftovers(const TransactionConfig& config) noexcept;

}  // namespace holonight_packages_transaction
