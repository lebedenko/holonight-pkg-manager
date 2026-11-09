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

}  // namespace holonight_packages_domain
