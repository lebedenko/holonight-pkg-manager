#pragma once

#include "holonight_packages_domain/transaction_error.h"
#include "holonight_packages_domain/transaction_progress.h"
#include "holonight_packages_domain/transaction_request.h"

#include <QString>
#include <QtGlobal>

#include <cstddef>
#include <cstdint>
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

// One entry per TransactionErrorCode (29). `names` are package or repository names, inserted as plain text.
// `action` selects the wording of PackageNotFound (install vs remove); `changed_empty` selects the wording of
// TransactionFailed.
[[nodiscard]] QString failureText(holonight_packages_domain::TransactionErrorCode code,
                                  const std::vector<std::string>& names, holonight_packages_domain::PlanAction action,
                                  bool changed_empty);
[[nodiscard]] QString noteText(Note note);
[[nodiscard]] QString phaseLabel(holonight_packages_domain::ProgressPhase phase);
[[nodiscard]] QString removeConfirmLabel(int package_count);  // "Remove 1 package" | "Remove N packages"

// system-upgrade REQ-F-094: the upgrade entries of the same table. Only names, versions, counts and step numbers are
// inserted, as plain text.
enum class UpgradeKey : quint8 {
  HelperTooOld,
  Syncing,         // "Syncing repository %1 of %2" (+ the repository name)
  SystemUpToDate,  // also NothingToDo.Upgrade
  Preview,         // the preview's heading
  HeldBack,        // the held-back section
  Keyring,         // the keyring notice, %1 = the keyring packages
  PartialKeyring,  // UpgradePartiallyApplied.keyring
  PartialMain,     // UpgradePartiallyApplied.main
  Succeeded,       // "System updated"
  KeyringUpdated,  // appended to a success or partial result whose changed list holds the keyring package
  Step,            // "Step %1 of %2"
  Title,           // "Update system"
  ConfirmLabel,    // "Update"
};
inline constexpr std::size_t kUpgradeKeyCount = 13;

[[nodiscard]] QString upgradeText(UpgradeKey key);
[[nodiscard]] QString syncingText(std::uint32_t current, std::uint32_t total, const std::string& repository);
[[nodiscard]] QString stepText(std::uint32_t index, std::uint32_t count);

}  // namespace transaction_messages
