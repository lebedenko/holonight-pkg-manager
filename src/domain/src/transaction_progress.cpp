#include "holonight_packages_domain/transaction_progress.h"

namespace holonight_packages_domain {

std::string_view progressPhaseName(ProgressPhase phase) noexcept {
  switch (phase) {
    case ProgressPhase::Resolving:
      return "Resolving";
    case ProgressPhase::Downloading:
      return "Downloading";
    case ProgressPhase::Verifying:
      return "Verifying";
    case ProgressPhase::Hooks:
      return "Hooks";
    case ProgressPhase::Installing:
      return "Installing";
    case ProgressPhase::Removing:
      return "Removing";
    case ProgressPhase::Done:
      return "Done";
  }
  return "Resolving";
}

std::optional<ProgressPhase> parseProgressPhase(std::string_view name) noexcept {
  for (const auto phase : {
           ProgressPhase::Resolving,
           ProgressPhase::Downloading,
           ProgressPhase::Verifying,
           ProgressPhase::Hooks,
           ProgressPhase::Installing,
           ProgressPhase::Removing,
           ProgressPhase::Done,
       }) {
    if (progressPhaseName(phase) == name) {
      return phase;
    }
  }
  return std::nullopt;
}

}  // namespace holonight_packages_domain
