#pragma once

#include "holonight_packages_domain/transaction_error.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace holonight_packages_domain {

enum class ExecutionOutcome : std::uint8_t { Succeeded, Failed, Cancelled };

[[nodiscard]] std::string_view executionOutcomeName(ExecutionOutcome outcome) noexcept;
[[nodiscard]] std::optional<ExecutionOutcome> parseExecutionOutcome(std::string_view name) noexcept;

// What happened to one package whose change completed.
enum class ChangeAction : std::uint8_t { Install, Remove };

[[nodiscard]] std::string_view changeActionName(ChangeAction action) noexcept;
[[nodiscard]] std::optional<ChangeAction> parseChangeAction(std::string_view name) noexcept;

struct ChangedPackage {
  std::string name;
  ChangeAction action = ChangeAction::Install;
  std::string old_version;
  std::string new_version;

  bool operator==(const ChangedPackage&) const = default;
};

// Enumerated warnings; free text never enters a reply (REQ-F-051).
enum class WarningToken : std::uint8_t { HookFailed };

[[nodiscard]] std::string_view warningTokenName(WarningToken token) noexcept;
[[nodiscard]] std::optional<WarningToken> parseWarningToken(std::string_view name) noexcept;

struct ExecutionResult {
  ExecutionOutcome outcome = ExecutionOutcome::Failed;
  TransactionFailure failure;  // meaningful when outcome == Failed
  std::vector<ChangedPackage> changed;
  std::vector<WarningToken> warnings;

  [[nodiscard]] static ExecutionResult succeeded(std::vector<ChangedPackage> changed = {}) {
    ExecutionResult result;
    result.outcome = ExecutionOutcome::Succeeded;
    result.changed = std::move(changed);
    return result;
  }
  [[nodiscard]] static ExecutionResult failed(TransactionFailure failure) {
    ExecutionResult result;
    result.outcome = ExecutionOutcome::Failed;
    result.failure = std::move(failure);
    return result;
  }
  [[nodiscard]] static ExecutionResult failed(TransactionErrorCode code) {
    return failed(TransactionFailure{.code = code, .names = {}, .choices = {}});
  }
  [[nodiscard]] static ExecutionResult cancelled() {
    ExecutionResult result;
    result.outcome = ExecutionOutcome::Cancelled;
    return result;
  }
};

}  // namespace holonight_packages_domain
