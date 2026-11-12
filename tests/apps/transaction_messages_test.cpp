#include "TransactionMessages.h"

#include <gtest/gtest.h>

namespace {

using namespace transaction_messages;
using holonight_packages_domain::kTransactionErrorCodeCount;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::TransactionErrorCode;

TEST(TransactionMessages, EveryTaxonomyCodeHasANonEmptyText) {
  for (std::size_t index = 0; index < kTransactionErrorCodeCount; ++index) {
    const auto code = static_cast<TransactionErrorCode>(index);
    EXPECT_FALSE(failureText(code, {"app"}, PlanAction::Install, false).isEmpty()) << index;
    EXPECT_FALSE(failureText(code, {"app"}, PlanAction::Remove, true).isEmpty()) << index;
  }
}

TEST(TransactionMessages, EveryNoteHasANonEmptyText) {
  for (std::size_t index = 0; index < kNoteCount; ++index) {
    EXPECT_FALSE(noteText(static_cast<Note>(index)).isEmpty()) << index;
  }
}

TEST(TransactionMessages, EveryPhaseHasALabel) {
  for (const auto phase : {
           ProgressPhase::Resolving,
           ProgressPhase::Downloading,
           ProgressPhase::Verifying,
           ProgressPhase::Hooks,
           ProgressPhase::Installing,
           ProgressPhase::Removing,
           ProgressPhase::Done,
       }) {
    EXPECT_FALSE(phaseLabel(phase).isEmpty());
  }
}

TEST(TransactionMessages, PackageNotFoundWordingDependsOnTheAction) {
  const QString install = failureText(TransactionErrorCode::PackageNotFound, {"zz"}, PlanAction::Install, false);
  const QString remove = failureText(TransactionErrorCode::PackageNotFound, {"zz"}, PlanAction::Remove, false);
  EXPECT_NE(install, remove);
  EXPECT_TRUE(install.contains(QStringLiteral("official repositories")));
  EXPECT_TRUE(remove.contains(QStringLiteral("not installed")));
}

TEST(TransactionMessages, TransactionFailedWordingDependsOnWhetherAnythingChanged) {
  EXPECT_NE(failureText(TransactionErrorCode::TransactionFailed, {}, PlanAction::Install, true),
            failureText(TransactionErrorCode::TransactionFailed, {}, PlanAction::Install, false));
}

TEST(TransactionMessages, PartialUpgradeRequiredNamesThePackagesAndPointsToUpdateSystem) {
  const QString text =
      failureText(TransactionErrorCode::PartialUpgradeRequired, {"glibc", "zlib"}, PlanAction::Install, false);
  EXPECT_TRUE(text.contains(QStringLiteral("glibc, zlib")));
  EXPECT_TRUE(text.contains(QStringLiteral("Update system")));  // system-upgrade REQ-F-089
}

TEST(TransactionMessages, BusyReadsAnotherPackageOperationIsRunning) {
  EXPECT_TRUE(failureText(TransactionErrorCode::Busy, {}, PlanAction::Install, false)
                  .contains(QStringLiteral("Another package operation is running")));
}

TEST(TransactionMessages, NamesAreInsertedAsPlainTextWithoutInterpretation) {
  const QString text = failureText(TransactionErrorCode::PackageIgnored, {"<b>x</b>"}, PlanAction::Install, false);
  EXPECT_TRUE(text.contains(QStringLiteral("<b>x</b>")));
}

TEST(TransactionMessages, RemoveConfirmLabelPluralises) {
  EXPECT_EQ(removeConfirmLabel(1), QStringLiteral("Remove 1 package"));
  EXPECT_EQ(removeConfirmLabel(3), QStringLiteral("Remove 3 packages"));
}

}  // namespace
