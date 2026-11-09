#include "holonight_packages_domain/transaction_error.h"

#include "holonight_packages_domain/transaction_progress.h"
#include "holonight_packages_domain/transaction_result.h"

#include <array>
#include <gtest/gtest.h>
#include <set>
#include <string>

namespace holonight_packages_domain {
namespace {

constexpr std::array<TransactionErrorCode, 24> kAll{
    TransactionErrorCode::InvalidArgument,
    TransactionErrorCode::PackageNotFound,
    TransactionErrorCode::PackageIgnored,
    TransactionErrorCode::ProtectedPackage,
    TransactionErrorCode::BreaksDependents,
    TransactionErrorCode::NeedsDecision,
    TransactionErrorCode::PartialUpgradeRequired,
    TransactionErrorCode::InsecureSigLevel,
    TransactionErrorCode::SyncDatabaseStale,
    TransactionErrorCode::DownloadFailed,
    TransactionErrorCode::FileConflicts,
    TransactionErrorCode::DiskSpace,
    TransactionErrorCode::SignatureInvalid,
    TransactionErrorCode::DatabaseUnavailable,
    TransactionErrorCode::ConfigurationError,
    TransactionErrorCode::NothingToDo,
    TransactionErrorCode::PlanNotFound,
    TransactionErrorCode::PlanExpired,
    TransactionErrorCode::PlanChanged,
    TransactionErrorCode::NotAuthorized,
    TransactionErrorCode::Busy,
    TransactionErrorCode::PastPointOfNoReturn,
    TransactionErrorCode::TransactionFailed,
    TransactionErrorCode::InternalError,
};

TEST(TransactionErrorTest, HasExactlyTheSpecifiedTwentyFourCodesWithUniqueNames) {
  static_assert(kAll.size() == kTransactionErrorCodeCount);
  std::set<std::string> names;
  for (const TransactionErrorCode code : kAll) {
    const std::string name(transactionErrorName(code));
    EXPECT_FALSE(name.empty());
    EXPECT_TRUE(names.insert(name).second) << "duplicate name " << name;
  }
  EXPECT_EQ(names.size(), 24U);
}

TEST(TransactionErrorTest, NamesRoundTripAndUnknownNamesAreRejected) {
  for (const TransactionErrorCode code : kAll) {
    EXPECT_EQ(parseTransactionErrorCode(transactionErrorName(code)), code);
  }
  EXPECT_FALSE(parseTransactionErrorCode("").has_value());
  EXPECT_FALSE(parseTransactionErrorCode("invalidargument").has_value());
  EXPECT_FALSE(parseTransactionErrorCode("Succeeded").has_value());
}

TEST(TransactionErrorTest, RefusalOrderMatchesRequirementThirty) {
  const auto one = stageOneRefusalOrder();
  const auto two = stageTwoRefusalOrder();
  ASSERT_EQ(one.size(), 8U);
  EXPECT_EQ(one.front(), TransactionErrorCode::InvalidArgument);
  EXPECT_EQ(one[1], TransactionErrorCode::ConfigurationError);
  EXPECT_EQ(one[2], TransactionErrorCode::PackageNotFound);
  EXPECT_EQ(one[3], TransactionErrorCode::PackageIgnored);
  EXPECT_EQ(one[4], TransactionErrorCode::InsecureSigLevel);
  EXPECT_EQ(one[5], TransactionErrorCode::ProtectedPackage);
  EXPECT_EQ(one[6], TransactionErrorCode::BreaksDependents);
  ASSERT_EQ(two.size(), 5U);
  EXPECT_EQ(two[0], TransactionErrorCode::PackageNotFound);
  EXPECT_EQ(two[1], TransactionErrorCode::NeedsDecision);
  EXPECT_EQ(two[2], TransactionErrorCode::ProtectedPackage);
  EXPECT_EQ(two[3], TransactionErrorCode::PartialUpgradeRequired);
  EXPECT_EQ(two[4], TransactionErrorCode::NothingToDo);
}

TEST(TransactionErrorTest, EnvironmentAndServiceCodesAreNotInTheRefusalOrder) {
  for (const auto& stage : {stageOneRefusalOrder(), stageTwoRefusalOrder()}) {
    for (const TransactionErrorCode code : stage) {
      EXPECT_NE(code, TransactionErrorCode::DatabaseUnavailable);
      EXPECT_NE(code, TransactionErrorCode::Busy);
      EXPECT_NE(code, TransactionErrorCode::PlanNotFound);
    }
  }
}

TEST(TransactionResultTest, OutcomeActionPhaseAndWarningNamesAreEnumerated) {
  EXPECT_EQ(executionOutcomeName(ExecutionOutcome::Succeeded), "Succeeded");
  EXPECT_EQ(executionOutcomeName(ExecutionOutcome::Failed), "Failed");
  EXPECT_EQ(executionOutcomeName(ExecutionOutcome::Cancelled), "Cancelled");
  EXPECT_EQ(warningTokenName(WarningToken::HookFailed), "HookFailed");
  EXPECT_EQ(progressPhaseName(ProgressPhase::Resolving), "Resolving");
  EXPECT_EQ(progressPhaseName(ProgressPhase::Done), "Done");
  EXPECT_EQ(changeActionName(ChangeAction::Remove), "remove");
}

TEST(TransactionResultTest, FactoriesFillTheFailureAndChangedList) {
  const ExecutionResult failure = ExecutionResult::failed(TransactionErrorCode::Busy);
  EXPECT_EQ(failure.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(failure.failure.code, TransactionErrorCode::Busy);
  EXPECT_TRUE(failure.changed.empty());
  EXPECT_EQ(ExecutionResult::cancelled().outcome, ExecutionOutcome::Cancelled);
  const ExecutionResult succeeded = ExecutionResult::succeeded(
      {ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1.0-1"}});
  EXPECT_EQ(succeeded.outcome, ExecutionOutcome::Succeeded);
  ASSERT_EQ(succeeded.changed.size(), 1U);
  EXPECT_EQ(succeeded.changed.front().name, "app");
}

}  // namespace
}  // namespace holonight_packages_domain
