#pragma once

#include "holonight_packages_domain/transaction_error.h"
#include "holonight_packages_domain/transaction_progress.h"
#include "holonight_packages_domain/transaction_request.h"

#include <QString>
#include <QtGlobal>

#include <cstddef>
#include <string>
#include <vector>

// The one fixed, translatable table of every message the transaction dialogs show (REQ-F-048). The GUI never shows
// free text from the helper: only these strings, with package names and versions inserted as plain text (REQ-F-049).
namespace transaction_messages {

// Notes and states that are not helper codes.
enum class Note : quint8 {
  Succeeded,
  Cancelled,
  Unknown,
  HelperUnavailable,
  HookWarning,
  NotAuthorized,
  CloseUnavailable,
  Cancelling,
  Preparing,
  Authorizing,
};

inline constexpr std::size_t kNoteCount = 10;

// One entry per TransactionErrorCode (24). `names` are package or repository names, inserted as plain text.
// `action` selects the wording of PackageNotFound (install vs remove); `changed_empty` selects the wording of
// TransactionFailed.
[[nodiscard]] QString failureText(holonight_packages_domain::TransactionErrorCode code,
                                  const std::vector<std::string>& names, holonight_packages_domain::PlanAction action,
                                  bool changed_empty);
[[nodiscard]] QString noteText(Note note);
[[nodiscard]] QString phaseLabel(holonight_packages_domain::ProgressPhase phase);
[[nodiscard]] QString removeConfirmLabel(int package_count);  // "Remove 1 package" | "Remove N packages"

}  // namespace transaction_messages
