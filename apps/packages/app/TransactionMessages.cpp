#include "TransactionMessages.h"

#include <QCoreApplication>
#include <QStringList>

#include <array>

namespace transaction_messages {

namespace {

using holonight_packages_domain::PlanAction;
using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::TransactionErrorCode;

constexpr const char* kContext = "TransactionMessages";

// Indexed by TransactionErrorCode. `%1` is the list of names. The wording follows SPEC "Message table".
constexpr std::array<const char*, holonight_packages_domain::kTransactionErrorCodeCount> kFailureSource{
    QT_TRANSLATE_NOOP("TransactionMessages", "The request was rejected as invalid. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "A package was not found in the official repositories: %1."),
    QT_TRANSLATE_NOOP("TransactionMessages", "%1 is excluded by the system pacman configuration. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "%1 is a protected system package and cannot be changed here."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Removing this would break installed packages: %1. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "The change needs a choice (provider, replacement or conflict) that this app does not make. Use "
                      "pacman for it. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "This change would upgrade installed packages: %1. Update the system first with "
                      "\u201cUpdate system\u2026\u201d, then install again. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "A repository is configured without strict signature checks: %1. Installation is refused until "
                      "the configuration is fixed outside this app. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "The repository is out of date: %1 is listed but not available. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "Downloading %1 failed. Check the network connection. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "Files of %1 conflict with each other or with existing files. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "There is not enough disk space. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "The signature of %1 could not be verified. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "The package database is missing or damaged. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "The pacman configuration could not be read. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "The package is already installed at the current version."),
    QT_TRANSLATE_NOOP("TransactionMessages", "The confirmation is no longer valid."),
    QT_TRANSLATE_NOOP("TransactionMessages", "The confirmation expired after five minutes."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "The package situation changed before the change started. Nothing was changed. Start again "
                      "from the list."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Authorization was not granted. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Another package operation is running. Try again when it finishes."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Closing is unavailable until the transaction finishes."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "The transaction failed. The packages listed were changed before the failure."),
    QT_TRANSLATE_NOOP("TransactionMessages", "An unexpected error stopped the operation. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "The package databases could not be synchronized. Check the network connection. Nothing was "
                      "changed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "The package databases changed after the preview. Nothing was changed. Prepare the update "
                      "again."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "A signing key for %1 is not in the keyring. This app does not import keys. Nothing was "
                      "changed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "The update stopped partway. Some packages were updated. Press \u201cUpdate system\u2026\u201d "
                      "again to finish it."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Cancelled. Nothing was changed."),
};

constexpr const char* kPackageNotFoundRemove =
    QT_TRANSLATE_NOOP("TransactionMessages", "%1 is not installed. Nothing was changed.");
constexpr const char* kTransactionFailedEmpty =
    QT_TRANSLATE_NOOP("TransactionMessages", "The transaction failed. Nothing was changed.");

constexpr std::array<const char*, kNoteCount> kNoteSource{
    QT_TRANSLATE_NOOP("TransactionMessages", "Done. The packages listed were changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Cancelled. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "The helper did not answer, so the outcome is unknown. Check the Installed list before doing "
                      "anything else."),
    QT_TRANSLATE_NOOP("TransactionMessages", "The package helper is not available. Check that it is installed."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "Done, but a post-install hook failed. Details are in the system journal."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Authorization was not granted. Nothing was changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Closing is unavailable until the transaction finishes."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Cancelling"),
    QT_TRANSLATE_NOOP("TransactionMessages", "Preparing"),
    QT_TRANSLATE_NOOP("TransactionMessages", "Waiting for authorization"),
};

constexpr std::array<const char*, kUpgradeKeyCount> kUpgradeSource{
    QT_TRANSLATE_NOOP("TransactionMessages", "The package helper is too old for system updates."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Syncing repository %1 of %2"),
    QT_TRANSLATE_NOOP("TransactionMessages", "System is up to date."),
    QT_TRANSLATE_NOOP("TransactionMessages", "These packages will be updated:"),
    QT_TRANSLATE_NOOP("TransactionMessages", "Held back by the pacman configuration (IgnorePkg or IgnoreGroup):"),
    QT_TRANSLATE_NOOP("TransactionMessages", "The keyring is updated first: %1."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "The keyring update failed after the package databases were updated. No package was updated. "
                      "Press \u201cUpdate system\u2026\u201d again."),
    QT_TRANSLATE_NOOP("TransactionMessages",
                      "The update stopped partway: the packages listed were updated, the others were not. Press "
                      "\u201cUpdate system\u2026\u201d again to finish it."),
    QT_TRANSLATE_NOOP("TransactionMessages", "System updated. The packages listed were changed."),
    QT_TRANSLATE_NOOP("TransactionMessages", "The keyring was updated."),
    QT_TRANSLATE_NOOP("TransactionMessages", "Step %1 of %2"),
    QT_TRANSLATE_NOOP("TransactionMessages", "Update system"),
    QT_TRANSLATE_NOOP("TransactionMessages", "Update"),
};

QString tr(const char* source) { return QCoreApplication::translate(kContext, source); }

// Names are inserted as plain text: the result is never interpreted as markup.
QString joinNames(const std::vector<std::string>& names) {
  QStringList list;
  list.reserve(static_cast<qsizetype>(names.size()));
  for (const std::string& name : names) {
    list.push_back(QString::fromStdString(name));
  }
  return list.join(QStringLiteral(", "));
}

}  // namespace

QString failureText(TransactionErrorCode code, const std::vector<std::string>& names, PlanAction action,
                    bool changed_empty) {
  const auto index = static_cast<std::size_t>(code);
  if (index >= kFailureSource.size()) {
    return tr(kFailureSource.at(static_cast<std::size_t>(TransactionErrorCode::InternalError)));
  }
  const char* source = kFailureSource.at(index);
  if (action == PlanAction::Upgrade && code == TransactionErrorCode::NothingToDo) {
    return upgradeText(UpgradeKey::SystemUpToDate);  // NothingToDo.Upgrade
  }
  if (code == TransactionErrorCode::UpgradePartiallyApplied) {
    // The step token is the only name (REQ-F-060, REQ-F-061).
    const bool keyring = !names.empty() && names.front() == "keyring";
    return upgradeText(keyring ? UpgradeKey::PartialKeyring : UpgradeKey::PartialMain);
  }
  if (code == TransactionErrorCode::PackageNotFound && action == PlanAction::Remove) {
    source = kPackageNotFoundRemove;
  } else if (code == TransactionErrorCode::TransactionFailed && changed_empty) {
    source = kTransactionFailedEmpty;
  }
  const QString text = tr(source);
  // Entries without a placeholder (for example DiskSpace) take no names.
  return text.contains(QStringLiteral("%1")) ? text.arg(joinNames(names)) : text;
}

QString noteText(Note note) {
  const auto index = static_cast<std::size_t>(note);
  return index < kNoteSource.size() ? tr(kNoteSource.at(index)) : QString();
}

QString phaseLabel(ProgressPhase phase) {
  switch (phase) {
    case ProgressPhase::Resolving:
      return QCoreApplication::translate(kContext, "Resolving dependencies");
    case ProgressPhase::Downloading:
      return QCoreApplication::translate(kContext, "Downloading");
    case ProgressPhase::Verifying:
      return QCoreApplication::translate(kContext, "Verifying");
    case ProgressPhase::Hooks:
      return QCoreApplication::translate(kContext, "Running hooks");
    case ProgressPhase::Installing:
      return QCoreApplication::translate(kContext, "Installing");
    case ProgressPhase::Removing:
      return QCoreApplication::translate(kContext, "Removing");
    case ProgressPhase::Done:
      return QCoreApplication::translate(kContext, "Done");
    case ProgressPhase::Syncing:
      return QCoreApplication::translate(kContext, "Synchronizing package databases");
    case ProgressPhase::Promoting:
      return QCoreApplication::translate(kContext, "Updating package databases");
    case ProgressPhase::KeyringStep:
      return QCoreApplication::translate(kContext, "Updating the keyring");
    case ProgressPhase::MainStep:
      return QCoreApplication::translate(kContext, "Updating packages");
  }
  return {};
}

QString upgradeText(UpgradeKey key) {
  const auto index = static_cast<std::size_t>(key);
  return index < kUpgradeSource.size() ? tr(kUpgradeSource.at(index)) : QString();
}

QString syncingText(std::uint32_t current, std::uint32_t total, const std::string& repository) {
  const QString line = upgradeText(UpgradeKey::Syncing).arg(current).arg(total);
  return repository.empty() ? line : line + QStringLiteral(": ") + QString::fromStdString(repository);
}

QString stepText(std::uint32_t index, std::uint32_t count) {
  return upgradeText(UpgradeKey::Step).arg(index).arg(count);
}

QString removeConfirmLabel(int package_count) {
  if (package_count == 1) {
    return QCoreApplication::translate(kContext, "Remove 1 package");
  }
  return QCoreApplication::translate(kContext, "Remove %1 packages").arg(package_count);
}

}  // namespace transaction_messages
