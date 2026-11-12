#include "fake_package_list_refresh.h"
#include "fake_transaction_service.h"
#include "holonight_packages_application/transaction_flow.h"

#include <gtest/gtest.h>
#include <vector>

namespace {

using namespace holonight_packages_application;
using namespace holonight_packages_domain;
using holonight_packages_testing::FakePackageListRefresh;
using holonight_packages_testing::FakeTransactionService;

ResolvedPlan upgradePlan(bool keyring = false) {
  ResolvedPlan plan;
  plan.action = PlanAction::Upgrade;
  UpgradeDetail detail;
  detail.upgrades = {UpgradeEntry{.name = "app", .old_version = "1", .new_version = "2", .repository = "core"}};
  if (keyring) {
    detail.upgrades.push_back(
        UpgradeEntry{.name = "archlinux-keyring", .old_version = "1", .new_version = "2", .repository = "core"});
    detail.keyring_packages = {"archlinux-keyring"};
  }
  plan.upgrade = detail;
  return plan;
}

std::vector<FlowAction> actionsOf(const TransactionFlow& flow) {
  const auto span = flow.actions();
  return {span.begin(), span.end()};
}

class UpgradeFlowTest : public testing::Test {
 public:
  UpgradeFlowTest() : flow(service, refresh) {
    service.setProgressHandler(
        [this](const std::string& plan_id, const ProgressUpdate& update) { flow.onProgress(plan_id, update); });
  }

  void toPreviewed(bool keyring = false) {
    ASSERT_TRUE(flow.requestUpgrade());
    ASSERT_TRUE(service.completePrepare(FakeTransactionService::previewOf("plan-u", upgradePlan(keyring))));
    ASSERT_EQ(flow.state(), FlowState::Previewed);
  }
  void toRunning(bool keyring = false) {
    toPreviewed(keyring);
    flow.confirm();
    ASSERT_EQ(flow.state(), FlowState::Authorizing);
  }
  void progress(ProgressPhase phase, std::uint32_t current = 0, std::uint32_t total = 0,
                const std::string& plan = "plan-u") const {
    service.emitProgress(plan, ProgressUpdate{.phase = phase, .current = current, .total = total, .item = {}});
  }

  FakeTransactionService service;
  FakePackageListRefresh refresh;
  TransactionFlow flow;
};

class TransactionFlowUpgradePrepare : public UpgradeFlowTest {};
class TransactionFlowUpgradeExecute : public UpgradeFlowTest {};
class TransactionFlowUpgradeCancel : public UpgradeFlowTest {};
class TransactionFlowUpgradeOutcome : public UpgradeFlowTest {};
class TransactionFlowUpgradeRefusal : public UpgradeFlowTest {};

// ---- T-060: preparation ----

TEST_F(TransactionFlowUpgradePrepare, AnUpgradeTakesNoNamesAndOnlyAnUpgradeTakesNone) {
  EXPECT_FALSE(flow.request(PlanRequest{.action = PlanAction::Upgrade, .names = {"app"}}));
  EXPECT_FALSE(flow.request(PlanRequest{.action = PlanAction::Install, .names = {}}));
  EXPECT_TRUE(flow.requestUpgrade());
  ASSERT_EQ(service.prepareCalls(), 1U);
  EXPECT_EQ(service.calls().front().request.action, PlanAction::Upgrade);
  EXPECT_TRUE(service.calls().front().request.names.empty());
  EXPECT_EQ(flow.state(), FlowState::Preparing);
}

TEST_F(TransactionFlowUpgradePrepare, SyncingProgressIsShownWhilePreparing) {
  ASSERT_TRUE(flow.requestUpgrade());
  progress(ProgressPhase::Syncing, 1, 2, "");
  EXPECT_EQ(flow.progress().phase, ProgressPhase::Syncing);
  EXPECT_EQ(flow.progress().current, 1U);
  EXPECT_EQ(flow.progress().total, 2U);
  progress(ProgressPhase::Syncing, 2, 2, "");
  EXPECT_EQ(flow.progress().current, 2U);
  // Progress of another plan is ignored.
  progress(ProgressPhase::Syncing, 9, 9, "other-plan");
  EXPECT_EQ(flow.progress().current, 2U);
}

TEST_F(TransactionFlowUpgradePrepare, CancelSendsCancelWithAnEmptyIdOnceAndTheCancelledReplyEndsWithCloseOnly) {
  ASSERT_TRUE(flow.requestUpgrade());
  EXPECT_TRUE(flow.preparationCancelEnabled());
  flow.cancelPreparation();
  flow.cancelPreparation();  // once only
  ASSERT_EQ(service.cancelCalls(), 1U);
  EXPECT_EQ(service.calls().back().plan_id, "");
  EXPECT_FALSE(flow.preparationCancelEnabled());
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::Cancelled));
  EXPECT_EQ(flow.state(), FlowState::Cancelled);
  EXPECT_EQ(actionsOf(flow), std::vector<FlowAction>{FlowAction::Close});
  EXPECT_EQ(service.executeCalls(), 0U);
}

TEST_F(TransactionFlowUpgradePrepare, NothingToDoEndsWithCloseOnlyAndNoPreview) {
  ASSERT_TRUE(flow.requestUpgrade());
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::NothingToDo));
  EXPECT_EQ(flow.state(), FlowState::Refused);
  EXPECT_FALSE(flow.prepared().has_value());
  EXPECT_EQ(actionsOf(flow), std::vector<FlowAction>{FlowAction::Close});
  EXPECT_EQ(service.executeCalls(), 0U);
}

TEST_F(TransactionFlowUpgradePrepare, BusyPlanNotFoundAndPlanExpiredOfferTryAgain) {
  for (const auto code :
       {TransactionErrorCode::Busy, TransactionErrorCode::PlanNotFound, TransactionErrorCode::PlanExpired}) {
    FakeTransactionService local_service;
    FakePackageListRefresh local_refresh;
    TransactionFlow local(local_service, local_refresh);
    ASSERT_TRUE(local.requestUpgrade());
    local_service.completePrepare(FakeTransactionService::refusal(code));
    EXPECT_EQ(actionsOf(local), (std::vector<FlowAction>{FlowAction::TryAgain, FlowAction::Close}));
    EXPECT_EQ(local_service.executeCalls(), 0U);
  }
}

// REQ-F-088: every upgrade outcome refreshes the lists and the update count, refusals included.
TEST_F(TransactionFlowUpgradePrepare, EveryUpgradeOutcomeRefreshesTheListsAndTheCount) {
  ASSERT_TRUE(flow.requestUpgrade());
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::NothingToDo));
  EXPECT_EQ(refresh.upgradeCount(), 1);
  flow.dismiss();
  ASSERT_TRUE(flow.requestUpgrade());
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::SyncFailed));
  EXPECT_EQ(refresh.upgradeCount(), 2);
  flow.dismiss();
  toRunning();
  service.completeExecute(ExecutionResult::succeeded());
  EXPECT_EQ(refresh.upgradeCount(), 3);
  flow.dismiss();
  toRunning();
  service.completeExecute(ExecutionResult::cancelled());
  EXPECT_EQ(refresh.upgradeCount(), 4);
  EXPECT_EQ(refresh.count(), 0);  // never the install and remove refresh
}

// ---- T-061: preview and confirmation ----

TEST_F(TransactionFlowUpgradeExecute, ConfirmSendsOnlyThePlanIdAndADenialReturnsToThePreview) {
  toRunning();
  ASSERT_EQ(service.executeCalls(), 1U);
  EXPECT_EQ(service.calls().back().plan_id, "plan-u");
  service.completeExecute(FakeTransactionService::failed(TransactionErrorCode::NotAuthorized));
  EXPECT_EQ(flow.state(), FlowState::Previewed);
  EXPECT_EQ(flow.notice(), FlowNotice::NotAuthorized);
  flow.confirm();
  EXPECT_EQ(service.executeCalls(), 2U);
  EXPECT_EQ(service.calls().back().plan_id, "plan-u");
  EXPECT_EQ(service.prepareCalls(), 1U);
}

TEST_F(TransactionFlowUpgradeExecute, TheStepMarkersSetTheStepAndItStaysForTheFollowingUpdates) {
  toRunning(true);
  ASSERT_TRUE(flow.prepared()->plan.upgrade->keyring());
  progress(ProgressPhase::Downloading, 1, 2);
  EXPECT_EQ(flow.progress().step_count, 0);
  progress(ProgressPhase::Promoting);
  progress(ProgressPhase::KeyringStep, 1, 2);
  EXPECT_EQ(flow.progress().step_index, 1);
  EXPECT_EQ(flow.progress().step_count, 2);
  progress(ProgressPhase::Installing, 1, 1);
  EXPECT_EQ(flow.progress().step_index, 1);
  EXPECT_EQ(flow.progress().current, 1U);
  progress(ProgressPhase::MainStep, 2, 2);
  EXPECT_EQ(flow.progress().step_index, 2);
  EXPECT_EQ(flow.progress().step_count, 2);
}

// ---- T-062: cancel latch and close rules ----

TEST_F(TransactionFlowUpgradeCancel, CancelIsEnabledUntilPromotionAndClosingIsRefusedAfterIt) {
  toRunning();
  progress(ProgressPhase::Downloading, 0, 1);
  progress(ProgressPhase::Verifying, 0, 1);
  EXPECT_TRUE(flow.cancelEnabled());
  progress(ProgressPhase::Promoting);
  EXPECT_FALSE(flow.cancelEnabled());
  EXPECT_EQ(flow.closeRequested(), CloseDecision::Ignore);
  EXPECT_EQ(flow.notice(), FlowNotice::CloseUnavailable);
  EXPECT_EQ(service.cancelCalls(), 0U);
}

TEST_F(TransactionFlowUpgradeCancel, ALatePastPointOfNoReturnKeepsTheWindowOpenAndShowsTheOutcome) {
  toRunning();
  progress(ProgressPhase::Downloading, 0, 1);
  flow.cancelRun();
  ASSERT_EQ(service.cancelCalls(), 1U);
  service.completeCancel(
      std::unexpected(FakeTransactionService::refusalError(TransactionErrorCode::PastPointOfNoReturn)));
  EXPECT_EQ(flow.notice(), FlowNotice::CloseUnavailable);
  EXPECT_FALSE(flow.closable());
  service.completeExecute(ExecutionResult::succeeded());
  EXPECT_EQ(flow.state(), FlowState::Succeeded);
}

// ---- T-063: outcomes ----

TEST_F(TransactionFlowUpgradeOutcome, ChangedDatabasesOfferTryAgainWhichPreparesAgain) {
  toRunning();
  service.completeExecute(FakeTransactionService::failed(TransactionErrorCode::SyncDatabaseChanged));
  EXPECT_EQ(flow.state(), FlowState::Failed);
  EXPECT_EQ(actionsOf(flow), (std::vector<FlowAction>{FlowAction::TryAgain, FlowAction::Close}));
  flow.tryAgain();
  EXPECT_EQ(service.prepareCalls(), 2U);
  EXPECT_EQ(service.calls().back().request.action, PlanAction::Upgrade);
  EXPECT_EQ(service.executeCalls(), 1U);
}

TEST_F(TransactionFlowUpgradeOutcome, OtherUpgradeCodesOfferCloseOnly) {
  for (const auto code : {TransactionErrorCode::SyncFailed, TransactionErrorCode::KeyImportRequired,
                          TransactionErrorCode::DownloadFailed, TransactionErrorCode::UpgradePartiallyApplied}) {
    FakeTransactionService local_service;
    FakePackageListRefresh local_refresh;
    TransactionFlow local(local_service, local_refresh);
    ASSERT_TRUE(local.requestUpgrade());
    local_service.completePrepare(FakeTransactionService::previewOf("p", upgradePlan()));
    local.confirm();
    local_service.completeExecute(FakeTransactionService::failed(code));
    EXPECT_EQ(actionsOf(local), std::vector<FlowAction>{FlowAction::Close}) << transactionErrorName(code);
  }
}

TEST_F(TransactionFlowUpgradeOutcome, APartialResultKeepsTheChangedListAndTheStepToken) {
  toRunning(true);
  auto failed = FakeTransactionService::failed(
      TransactionErrorCode::UpgradePartiallyApplied,
      {ChangedPackage{
          .name = "archlinux-keyring", .action = ChangeAction::Install, .old_version = "1", .new_version = "2"}});
  failed->failure.names = {"main"};
  service.completeExecute(std::move(failed));
  ASSERT_TRUE(flow.failure().has_value());
  EXPECT_EQ(flow.failure()->names, std::vector<std::string>{"main"});
  ASSERT_EQ(flow.changed().size(), 1U);
  EXPECT_EQ(flow.changed().front().name, "archlinux-keyring");
}

// ---- T-064: the refusal button ----

TEST_F(TransactionFlowUpgradeRefusal, APartialUpgradeRefusalOffersUpdateSystemWhichStartsTheUpgradeOnly) {
  ASSERT_TRUE(flow.request(PlanRequest{.action = PlanAction::Install, .names = {"app"}}));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::PartialUpgradeRequired, {"glibc"}));
  EXPECT_EQ(actionsOf(flow), (std::vector<FlowAction>{FlowAction::UpdateSystem, FlowAction::Close}));
  std::vector<FlowState> seen;
  flow.setObserver([this, &seen] { seen.push_back(flow.state()); });
  flow.updateSystem();
  ASSERT_GE(seen.size(), 2U);
  EXPECT_EQ(seen.front(), FlowState::Idle);  // back to Idle before the new request
  EXPECT_EQ(flow.state(), FlowState::Preparing);
  EXPECT_EQ(service.prepareCalls(), 2U);
  EXPECT_EQ(service.calls().back().request.action, PlanAction::Upgrade);
  EXPECT_EQ(service.upgradePrepareCalls(), 1U);
  service.completePrepare(FakeTransactionService::previewOf("plan-u", upgradePlan()));
  EXPECT_EQ(service.prepareCalls(), 2U);  // the install is never retried
}

TEST_F(TransactionFlowUpgradeRefusal, OnlyAPartialUpgradeOfAnInstallOffersTheButton) {
  ASSERT_TRUE(flow.request(PlanRequest{.action = PlanAction::Install, .names = {"app"}}));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::PackageNotFound, {"app"}));
  flow.updateSystem();
  EXPECT_EQ(flow.state(), FlowState::Refused);
  EXPECT_EQ(service.prepareCalls(), 1U);
}

}  // namespace
