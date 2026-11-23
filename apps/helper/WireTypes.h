#pragma once

#include "HelperProtocol.h"
#include "holonight_packages_domain/transaction_engine.h"

#include <chrono>

namespace holonight_helper {

using helper_wire::ChangeWire;
using helper_wire::ChoiceWire;
using helper_wire::HeldBackWire;
using helper_wire::HookItemWire;
using helper_wire::NewDependencyWire;
using helper_wire::PlanItemWire;
using helper_wire::PreviewWire;
using helper_wire::ReplacementWire;
using helper_wire::ResultWire;
using helper_wire::StatusWire;
using helper_wire::UpgradeItemWire;
using helper_wire::UpgradePreviewWire;

[[nodiscard]] StatusWire okStatus();
[[nodiscard]] StatusWire toWire(const holonight_packages_domain::TransactionFailure& failure);
[[nodiscard]] PreviewWire toWire(const holonight_packages_domain::ResolvedPlan& plan,
                                 std::chrono::system_clock::time_point expires_at);
[[nodiscard]] ResultWire toWire(const holonight_packages_domain::ExecutionResult& result);
// The preview of an upgrade plan; an empty preview when the plan carries no upgrade detail.
[[nodiscard]] UpgradePreviewWire toUpgradeWire(const holonight_packages_domain::ResolvedPlan& plan,
                                               std::chrono::system_clock::time_point expires_at);

}  // namespace holonight_helper
