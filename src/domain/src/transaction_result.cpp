#include "holonight_packages_domain/transaction_result.h"

namespace holonight_packages_domain {

std::string_view executionOutcomeName(ExecutionOutcome outcome) noexcept {
  switch (outcome) {
    case ExecutionOutcome::Succeeded:
      return "Succeeded";
    case ExecutionOutcome::Failed:
      return "Failed";
    case ExecutionOutcome::Cancelled:
      return "Cancelled";
  }
  return "Failed";
}

std::string_view changeActionName(ChangeAction action) noexcept {
  return action == ChangeAction::Install ? "install" : "remove";
}

std::string_view warningTokenName(WarningToken /*token*/) noexcept {
  return "HookFailed";  // the only warning there is
}

std::optional<ExecutionOutcome> parseExecutionOutcome(std::string_view name) noexcept {
  for (const auto outcome : {ExecutionOutcome::Succeeded, ExecutionOutcome::Failed, ExecutionOutcome::Cancelled}) {
    if (executionOutcomeName(outcome) == name) {
      return outcome;
    }
  }
  return std::nullopt;
}

std::optional<ChangeAction> parseChangeAction(std::string_view name) noexcept {
  for (const auto action : {ChangeAction::Install, ChangeAction::Remove}) {
    if (changeActionName(action) == name) {
      return action;
    }
  }
  return std::nullopt;
}

std::optional<WarningToken> parseWarningToken(std::string_view name) noexcept {
  if (name == warningTokenName(WarningToken::HookFailed)) {
    return WarningToken::HookFailed;
  }
  return std::nullopt;
}

}  // namespace holonight_packages_domain
