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
                      "This change would upgrade installed packages: %1. Update the system first, outside this app. "
                      "This app does not upgrade the system. Nothing was changed."),
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
  }
  return {};
}

QString removeConfirmLabel(int package_count) {
  if (package_count == 1) {
    return QCoreApplication::translate(kContext, "Remove 1 package");
  }
  return QCoreApplication::translate(kContext, "Remove %1 packages").arg(package_count);
}

}  // namespace transaction_messages
