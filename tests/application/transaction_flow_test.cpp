#include "holonight_packages_application/transaction_flow.h"

#include "fake_package_list_refresh.h"
#include "fake_transaction_service.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

namespace {

using namespace holonight_packages_application;
using namespace holonight_packages_domain;
using holonight_packages_testing::FakePackageListRefresh;
using holonight_packages_testing::FakeTransactionService;

PlanRequest installOf(const std::string& name) { return PlanRequest{.action = PlanAction::Install, .names = {name}}; }
PlanRequest removeOf(const std::string& name) { return PlanRequest{.action = PlanAction::Remove, .names = {name}}; }

ResolvedPlan onePackagePlan() {
  ResolvedPlan plan;
  plan.changes.push_back(PlannedChange{.name = "app", .kind = ChangeKind::Target, .new_version = "1-1"});
  return plan;
}

std::vector<FlowAction> actionsOf(const TransactionFlow& flow) {
  const auto span = flow.actions();
  return {span.begin(), span.end()};
}

class FlowTest : public testing::Test {
 public:
  FlowTest() : flow(service, refresh) {
    flow.setObserver([this] { ++notifications; });
    flow.setCloseReadyHandler([this] { ++close_ready; });
    service.setProgressHandler(
        [this](const std::string& plan_id, const ProgressUpdate& update) { flow.onProgress(plan_id, update); });
  }

  void toPreviewed(const PlanRequest& request = installOf("app")) {
    ASSERT_TRUE(flow.request(request));
    ASSERT_TRUE(service.completePrepare(FakeTransactionService::previewOf("plan-1", onePackagePlan())));
    ASSERT_EQ(flow.state(), FlowState::Previewed);
  }
  void toAuthorizing() {
    toPreviewed();
    flow.confirm();
    ASSERT_EQ(flow.state(), FlowState::Authorizing);
  }
  void progress(ProgressPhase phase, std::uint32_t current = 0, std::uint32_t total = 0,
                const std::string& item = "") const {
    service.emitProgress("plan-1", ProgressUpdate{.phase = phase, .current = current, .total = total, .item = item});
  }

  FakeTransactionService service;
  FakePackageListRefresh refresh;
  TransactionFlow flow;
  int notifications = 0;
  int close_ready = 0;
};

class TransactionFlowPrepare : public FlowTest {};
class TransactionFlowExecute : public FlowTest {};
class TransactionFlowProgress : public FlowTest {};
class TransactionFlowClose : public FlowTest {};

// ---- Preparation side (T-003) -------------------------------------------------------------------------------------

TEST_F(TransactionFlowPrepare, StartsIdleWithNoDialog) {
  EXPECT_EQ(flow.state(), FlowState::Idle);
  EXPECT_EQ(flow.openDialogCount(), 0);
  EXPECT_TRUE(flow.allowNewRequest());
}

TEST_F(TransactionFlowPrepare, InstallRequestCallsPreparePlanOnceAndEntersPreparing) {
  FakeTransactionService service;
  FakePackageListRefresh refresh;
  TransactionFlow flow(service, refresh);
  EXPECT_TRUE(flow.request(installOf("app")));
  EXPECT_EQ(flow.state(), FlowState::Preparing);
  ASSERT_EQ(service.prepareCalls(), 1U);
  EXPECT_EQ(service.calls().front().request, installOf("app"));
}

TEST_F(TransactionFlowPrepare, RemoveRequestCallsPreparePlanWithRemove) {
  FakeTransactionService service;
  FakePackageListRefresh refresh;
  TransactionFlow flow(service, refresh);
  EXPECT_TRUE(flow.request(removeOf("app")));
  EXPECT_EQ(service.calls().front().request, removeOf("app"));
  EXPECT_EQ(service.executeCalls(), 0U);
}

TEST_F(TransactionFlowPrepare, RejectsRequestsThatAreNotExactlyOneName) {
  FakeTransactionService service;
  FakePackageListRefresh refresh;
  TransactionFlow flow(service, refresh);
  EXPECT_FALSE(flow.request(PlanRequest{.action = PlanAction::Install, .names = {}}));
  EXPECT_FALSE(flow.request(PlanRequest{.action = PlanAction::Install, .names = {"a", "b"}}));
  EXPECT_FALSE(flow.request(installOf("")));
  EXPECT_EQ(service.prepareCalls(), 0U);
  EXPECT_EQ(flow.state(), FlowState::Idle);
}

TEST_F(TransactionFlowPrepare, SecondRequestDuringPreparingLeavesOnePreparePlanCall) {
  ASSERT_TRUE(flow.request(installOf("app")));
  EXPECT_FALSE(flow.request(removeOf("other")));
  EXPECT_EQ(service.prepareCalls(), 1U);
}

TEST_F(TransactionFlowPrepare, RequestsAreIgnoredWhileExecuting) {
  toAuthorizing();
  progress(ProgressPhase::Downloading, 1, 2, "x");
  ASSERT_EQ(flow.state(), FlowState::Executing);
  EXPECT_FALSE(flow.request(removeOf("other")));
  EXPECT_EQ(service.prepareCalls(), 1U);
  EXPECT_FALSE(flow.allowNewRequest());
}

TEST_F(TransactionFlowPrepare, PreviewedStoresThePlanAndOffersConfirmAndCancel) {
  toPreviewed();
  ASSERT_TRUE(flow.prepared().has_value());
  EXPECT_EQ(flow.prepared()->plan_id, "plan-1");
  EXPECT_EQ(actionsOf(flow), (std::vector<FlowAction>{FlowAction::Confirm, FlowAction::Cancel}));
  EXPECT_FALSE(flow.allowNewRequest());
  EXPECT_EQ(flow.openDialogCount(), 1);
}

TEST_F(TransactionFlowPrepare, DismissingAPreviewCancelsThePlanAndNeverExecutes) {
  toPreviewed();
  flow.dismiss();
  EXPECT_EQ(flow.state(), FlowState::Idle);
  EXPECT_EQ(service.cancelCalls(), 1U);
  EXPECT_EQ(service.calls().back().plan_id, "plan-1");
  EXPECT_EQ(service.executeCalls(), 0U);
  EXPECT_FALSE(flow.prepared().has_value());
}

TEST_F(TransactionFlowPrepare, EveryTaxonomyCodeRefusalOffersCloseOnlyUnlessRetryable) {
  for (std::size_t index = 0; index < kTransactionErrorCodeCount; ++index) {
    const auto code = static_cast<TransactionErrorCode>(index);
    FakeTransactionService local_service;
    FakePackageListRefresh local_refresh;
    TransactionFlow local(local_service, local_refresh);
    ASSERT_TRUE(local.request(installOf("app")));
    ASSERT_TRUE(local_service.completePrepare(FakeTransactionService::refusal(code, {"app"})));
    EXPECT_EQ(local.state(), FlowState::Refused) << transactionErrorName(code);
    EXPECT_EQ(local_service.executeCalls(), 0U) << transactionErrorName(code);
    const bool retry = code == TransactionErrorCode::Busy || code == TransactionErrorCode::PlanNotFound ||
                       code == TransactionErrorCode::PlanExpired;
    const std::vector<FlowAction> expected = retry ? std::vector<FlowAction>{FlowAction::TryAgain, FlowAction::Close}
                                                   : std::vector<FlowAction>{FlowAction::Close};
    EXPECT_EQ(actionsOf(local), expected) << transactionErrorName(code);
    ASSERT_TRUE(local.failure().has_value());
    EXPECT_EQ(local.failure()->code, code);
    EXPECT_EQ(local.refreshRequests(), 0);
  }
}

TEST_F(TransactionFlowPrepare, BusyThenTryAgainRunsPreparePlanAgainWithTheSameRequest) {
  ASSERT_TRUE(flow.request(installOf("app")));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::Busy));
  ASSERT_EQ(flow.state(), FlowState::Refused);
  flow.tryAgain();
  EXPECT_EQ(flow.state(), FlowState::Preparing);
  ASSERT_EQ(service.prepareCalls(), 2U);
  EXPECT_EQ(service.calls().back().request, installOf("app"));
}

TEST_F(TransactionFlowPrepare, TryAgainIsIgnoredForNonRetryableCodes) {
  ASSERT_TRUE(flow.request(installOf("app")));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::PartialUpgradeRequired, {"glibc"}));
  flow.tryAgain();
  EXPECT_EQ(flow.state(), FlowState::Refused);
  EXPECT_EQ(service.prepareCalls(), 1U);
}

TEST_F(TransactionFlowPrepare, HelperUnavailableOnPrepareCallsNothingElse) {
  ASSERT_TRUE(flow.request(installOf("app")));
  service.completePrepare(std::unexpected(ServiceError{.kind = ServiceError::Kind::HelperUnavailable, .failure = {}}));
  EXPECT_EQ(flow.state(), FlowState::HelperUnavailable);
  EXPECT_EQ(service.executeCalls(), 0U);
  EXPECT_EQ(service.calls().size(), 1U);
  EXPECT_EQ(actionsOf(flow), std::vector<FlowAction>{FlowAction::Close});
}

TEST_F(TransactionFlowPrepare, NoReplyAndRejectedOnPrepareBecomeInternalError) {
  for (const auto kind : {ServiceError::Kind::NoReply, ServiceError::Kind::Rejected}) {
    FakeTransactionService local_service;
    FakePackageListRefresh local_refresh;
    TransactionFlow local(local_service, local_refresh);
    ASSERT_TRUE(local.request(installOf("app")));
    local_service.completePrepare(std::unexpected(ServiceError{.kind = kind, .failure = {}}));
    EXPECT_EQ(local.state(), FlowState::Refused);
    EXPECT_EQ(local.failure()->code, TransactionErrorCode::InternalError);
    EXPECT_EQ(local_service.executeCalls(), 0U);
  }
}

TEST_F(TransactionFlowPrepare, OpenDialogCountNeverExceedsOneAcrossTheFullSequence) {
  int max_open = 0;
  flow.setObserver([&] { max_open = std::max(max_open, flow.openDialogCount()); });
  toAuthorizing();
  progress(ProgressPhase::Downloading, 1, 2);
  progress(ProgressPhase::Installing, 1, 1);
  service.completeExecute(ExecutionResult::succeeded());
  flow.dismiss();
  EXPECT_EQ(max_open, 1);
  EXPECT_EQ(flow.openDialogCount(), 0);
}

TEST_F(TransactionFlowPrepare, DismissingATerminalStateReturnsToIdleWithoutServiceCalls) {
  ASSERT_TRUE(flow.request(installOf("app")));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::NothingToDo));
  const auto calls_before = service.calls().size();
  flow.dismiss();
  EXPECT_EQ(flow.state(), FlowState::Idle);
  EXPECT_EQ(service.calls().size(), calls_before);
}

TEST_F(TransactionFlowPrepare, AStaleReplyFromASupersededRequestIsDropped) {
  ASSERT_TRUE(flow.request(installOf("app")));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::Busy));
  flow.tryAgain();
  // Complete the new prepare, then a late completion of an unrelated generation cannot happen: held one only.
  EXPECT_TRUE(service.completePrepare(FakeTransactionService::previewOf("plan-2", onePackagePlan())));
  EXPECT_EQ(flow.prepared()->plan_id, "plan-2");
}

TEST_F(TransactionFlowPrepare, FlowDestroyedBeforeTheReplyIgnoresTheCallback) {
  FakeTransactionService service;
  FakePackageListRefresh refresh;
  {
    TransactionFlow flow(service, refresh);
    ASSERT_TRUE(flow.request(installOf("app")));
  }
  EXPECT_TRUE(service.completePrepare(FakeTransactionService::previewOf("plan-1", onePackagePlan())));
  EXPECT_EQ(refresh.count(), 0);
}

// ---- Execution side (T-004) ---------------------------------------------------------------------------------------

TEST_F(TransactionFlowExecute, ConfirmExecutesOnlyThePlanId) {
  toPreviewed();
  flow.confirm();
  EXPECT_EQ(flow.state(), FlowState::Authorizing);
  ASSERT_EQ(service.executeCalls(), 1U);
  EXPECT_EQ(service.calls().back().plan_id, "plan-1");
}

TEST_F(TransactionFlowExecute, SucceededShowsChangedRowsAndRefreshesOnce) {
  toAuthorizing();
  ExecutionResult result = ExecutionResult::succeeded({
      ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1-1"},
      ChangedPackage{.name = "lib", .action = ChangeAction::Install, .old_version = "", .new_version = "2-1"},
  });
  service.completeExecute(result);
  EXPECT_EQ(flow.state(), FlowState::Succeeded);
  ASSERT_EQ(flow.changed().size(), 2U);
  EXPECT_EQ(flow.changed()[1].name, "lib");
  EXPECT_EQ(refresh.count(), 1);
  EXPECT_TRUE(flow.warnings().empty());
  EXPECT_EQ(actionsOf(flow), std::vector<FlowAction>{FlowAction::Close});
}

TEST_F(TransactionFlowExecute, SucceededWithHookWarningKeepsTheRows) {
  toAuthorizing();
  ExecutionResult result = ExecutionResult::succeeded(
      {ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1-1"}});
  result.warnings = {WarningToken::HookFailed};
  service.completeExecute(result);
  EXPECT_EQ(flow.state(), FlowState::Succeeded);
  EXPECT_EQ(flow.warnings(), std::vector<WarningToken>{WarningToken::HookFailed});
  EXPECT_EQ(flow.changed().size(), 1U);
}

TEST_F(TransactionFlowExecute, FailedKeepsTheChangedListAndRefreshesOnce) {
  toAuthorizing();
  service.completeExecute(FakeTransactionService::failed(
      TransactionErrorCode::TransactionFailed,
      {ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1-1"}}));
  EXPECT_EQ(flow.state(), FlowState::Failed);
  EXPECT_EQ(flow.failure()->code, TransactionErrorCode::TransactionFailed);
  EXPECT_EQ(flow.changed().size(), 1U);
  EXPECT_EQ(refresh.count(), 1);
}

TEST_F(TransactionFlowExecute, DiskSpaceFailureWithNoChangesShowsNoRows) {
  toAuthorizing();
  service.completeExecute(FakeTransactionService::failed(TransactionErrorCode::DiskSpace));
  EXPECT_EQ(flow.state(), FlowState::Failed);
  EXPECT_TRUE(flow.changed().empty());
}

TEST_F(TransactionFlowExecute, CancelledOutcomeShowsNoRowsEvenIfTheReplyHasSome) {
  toAuthorizing();
  ExecutionResult result = ExecutionResult::cancelled();
  result.changed = {
      ChangedPackage{.name = "x", .action = ChangeAction::Install, .old_version = "", .new_version = "1"},
  };
  service.completeExecute(result);
  EXPECT_EQ(flow.state(), FlowState::Cancelled);
  EXPECT_TRUE(flow.changed().empty());
  EXPECT_EQ(refresh.count(), 1);
}

TEST_F(TransactionFlowExecute, NoReplyIsUnknownWithOneExecuteAndNoRetry) {
  toAuthorizing();
  service.completeExecute(std::unexpected(ServiceError{.kind = ServiceError::Kind::NoReply, .failure = {}}));
  EXPECT_EQ(flow.state(), FlowState::Unknown);
  EXPECT_EQ(service.executeCalls(), 1U);
  EXPECT_EQ(service.prepareCalls(), 1U);
  EXPECT_EQ(actionsOf(flow), std::vector<FlowAction>{FlowAction::Close});
  EXPECT_EQ(refresh.count(), 1);
  flow.tryAgain();
  EXPECT_EQ(service.prepareCalls(), 1U);
}

TEST_F(TransactionFlowExecute, HelperUnavailableOnExecuteRefreshesNothingAndCallsNothingFurther) {
  toAuthorizing();
  service.completeExecute(std::unexpected(ServiceError{.kind = ServiceError::Kind::HelperUnavailable, .failure = {}}));
  EXPECT_EQ(flow.state(), FlowState::HelperUnavailable);
  EXPECT_EQ(refresh.count(), 0);
  EXPECT_EQ(service.calls().size(), 2U);
}

TEST_F(TransactionFlowExecute, RejectedExecuteIsAnInternalErrorFailure) {
  toAuthorizing();
  service.completeExecute(std::unexpected(ServiceError{.kind = ServiceError::Kind::Rejected, .failure = {}}));
  EXPECT_EQ(flow.state(), FlowState::Failed);
  EXPECT_EQ(flow.failure()->code, TransactionErrorCode::InternalError);
  EXPECT_EQ(refresh.count(), 1);
}

TEST_F(TransactionFlowExecute, NotAuthorizedReturnsToPreviewedWithoutRefreshAndConfirmReusesThePlan) {
  toAuthorizing();
  service.completeExecute(FakeTransactionService::failed(TransactionErrorCode::NotAuthorized));
  EXPECT_EQ(flow.state(), FlowState::Previewed);
  EXPECT_EQ(flow.notice(), FlowNotice::NotAuthorized);
  EXPECT_EQ(refresh.count(), 0);
  EXPECT_EQ(actionsOf(flow), (std::vector<FlowAction>{FlowAction::Confirm, FlowAction::Cancel}));

  flow.confirm();
  EXPECT_EQ(flow.state(), FlowState::Authorizing);
  ASSERT_EQ(service.executeCalls(), 2U);
  EXPECT_EQ(service.calls().back().plan_id, "plan-1");
  EXPECT_EQ(service.prepareCalls(), 1U);
}

TEST_F(TransactionFlowExecute, PlanExpiredAndPlanNotFoundOfferTryAgainWithoutExecute) {
  for (const auto code : {TransactionErrorCode::PlanExpired, TransactionErrorCode::PlanNotFound}) {
    FakeTransactionService local_service;
    FakePackageListRefresh local_refresh;
    TransactionFlow local(local_service, local_refresh);
    ASSERT_TRUE(local.request(installOf("app")));
    local_service.completePrepare(FakeTransactionService::previewOf("plan-1", onePackagePlan()));
    local.confirm();
    local_service.completeExecute(FakeTransactionService::failed(code));
    EXPECT_EQ(local.state(), FlowState::Failed);
    EXPECT_EQ(actionsOf(local), (std::vector<FlowAction>{FlowAction::TryAgain, FlowAction::Close}));
    local.tryAgain();
    EXPECT_EQ(local.state(), FlowState::Preparing);
    EXPECT_EQ(local_service.prepareCalls(), 2U);
    EXPECT_EQ(local_service.executeCalls(), 1U);
  }
}

TEST_F(TransactionFlowExecute, ExecuteIsCalledAtMostOncePerPlanAcrossOutcomes) {
  toAuthorizing();
  service.completeExecute(ExecutionResult::succeeded());
  EXPECT_EQ(service.executeCalls(), 1U);
  flow.confirm();  // not in Previewed any more
  EXPECT_EQ(service.executeCalls(), 1U);
}

TEST_F(TransactionFlowExecute, ARefreshIsRequestedAfterTheResultStateIsEntered) {
  FlowState state_at_refresh = FlowState::Idle;
  class Probe final : public PackageListRefresh {
   public:
    explicit Probe(std::function<void()> on_refresh) : on_refresh_(std::move(on_refresh)) {}
    void refreshAfterOutcome() override { on_refresh_(); }
    std::function<void()> on_refresh_;
  };
  FakeTransactionService local_service;
  TransactionFlow* flow_ptr = nullptr;
  Probe probe([&] { state_at_refresh = flow_ptr->state(); });
  TransactionFlow local(local_service, probe);
  flow_ptr = &local;
  ASSERT_TRUE(local.request(installOf("app")));
  local_service.completePrepare(FakeTransactionService::previewOf("plan-1", onePackagePlan()));
  local.confirm();
  local_service.completeExecute(ExecutionResult::succeeded());
  EXPECT_EQ(state_at_refresh, FlowState::Succeeded);
}

// ---- Progress and the cancel flag (T-005) -------------------------------------------------------------------------

TEST_F(TransactionFlowProgress, CancelIsEnabledWhileAuthorizing) {
  toAuthorizing();
  EXPECT_TRUE(flow.cancelEnabled());
}

TEST_F(TransactionFlowProgress, CancelFlagMatrixOverEveryPhase) {
  const std::vector<std::pair<ProgressPhase, bool>> matrix = {
      {ProgressPhase::Resolving, true}, {ProgressPhase::Downloading, true}, {ProgressPhase::Verifying, true},
      {ProgressPhase::Hooks, false},    {ProgressPhase::Installing, false}, {ProgressPhase::Removing, false},
      {ProgressPhase::Done, false},
  };
  for (const auto& [phase, enabled] : matrix) {
    FakeTransactionService local_service;
    FakePackageListRefresh local_refresh;
    TransactionFlow local(local_service, local_refresh);
    local_service.setProgressHandler(
        [&](const std::string& plan_id, const ProgressUpdate& update) { local.onProgress(plan_id, update); });
    ASSERT_TRUE(local.request(installOf("app")));
    local_service.completePrepare(FakeTransactionService::previewOf("plan-1", onePackagePlan()));
    local.confirm();
    local_service.emitProgress("plan-1", ProgressUpdate{.phase = phase, .current = 0, .total = 0, .item = ""});
    EXPECT_EQ(local.state(), FlowState::Executing);
    EXPECT_EQ(local.cancelEnabled(), enabled) << progressPhaseName(phase);
  }
}

TEST_F(TransactionFlowProgress, ALaterResolvingDoesNotReEnableCancel) {
  toAuthorizing();
  progress(ProgressPhase::Hooks);
  ASSERT_FALSE(flow.cancelEnabled());
  progress(ProgressPhase::Resolving);
  EXPECT_FALSE(flow.cancelEnabled());
}

TEST_F(TransactionFlowProgress, AnEventWithAnotherPlanIdChangesNothing) {
  toAuthorizing();
  progress(ProgressPhase::Downloading, 3, 10, "foo");
  service.emitProgress("other-plan",
                       ProgressUpdate{.phase = ProgressPhase::Installing, .current = 9, .total = 9, .item = "z"});
  EXPECT_EQ(flow.progress().phase, ProgressPhase::Downloading);
  EXPECT_EQ(flow.progress().current, 3U);
  EXPECT_EQ(flow.progress().item, "foo");
  EXPECT_TRUE(flow.cancelEnabled());
}

TEST_F(TransactionFlowProgress, ARegressionWithinOnePhaseOccurrenceIsIgnored) {
  toAuthorizing();
  progress(ProgressPhase::Downloading, 4, 10);
  progress(ProgressPhase::Downloading, 2, 10);
  EXPECT_EQ(flow.progress().current, 4U);
}

TEST_F(TransactionFlowProgress, HooksMayOccurTwiceWithCurrentStartingOverTheSecondTime) {
  toAuthorizing();
  progress(ProgressPhase::Hooks, 3, 3);
  progress(ProgressPhase::Installing, 1, 1);
  progress(ProgressPhase::Hooks, 1, 3);
  EXPECT_EQ(flow.progress().phase, ProgressPhase::Hooks);
  EXPECT_EQ(flow.progress().current, 1U);
}

TEST_F(TransactionFlowProgress, FirstEventMovesAuthorizingToExecutingAndStoresTheItem) {
  toAuthorizing();
  EXPECT_FALSE(flow.progress().seen);
  progress(ProgressPhase::Installing, 1, 2, "bar");
  EXPECT_EQ(flow.state(), FlowState::Executing);
  EXPECT_TRUE(flow.progress().seen);
  EXPECT_EQ(flow.progress().item, "bar");
}

TEST_F(TransactionFlowProgress, ProgressInOtherStatesIsIgnored) {
  toPreviewed();
  progress(ProgressPhase::Downloading, 1, 2);
  EXPECT_EQ(flow.state(), FlowState::Previewed);
  EXPECT_FALSE(flow.progress().seen);
}

TEST_F(TransactionFlowProgress, CancelRunCallsCancelOnceShowsCancellingAndDisablesTheControl) {
  toAuthorizing();
  progress(ProgressPhase::Downloading, 1, 2);
  flow.cancelRun();
  EXPECT_TRUE(flow.cancelling());
  EXPECT_FALSE(flow.cancelEnabled());
  ASSERT_EQ(service.cancelCalls(), 1U);
  EXPECT_EQ(service.calls().back().plan_id, "plan-1");
  flow.cancelRun();
  EXPECT_EQ(service.cancelCalls(), 1U);
  service.completeCancel({});
  EXPECT_EQ(flow.state(), FlowState::Executing);
  EXPECT_TRUE(flow.cancelling());
}

TEST_F(TransactionFlowProgress, CancelRunIsIgnoredAfterTheCommitPoint) {
  toAuthorizing();
  progress(ProgressPhase::Hooks);
  flow.cancelRun();
  EXPECT_EQ(service.cancelCalls(), 0U);
}

// ---- Window close (T-006) -----------------------------------------------------------------------------------------

TEST_F(TransactionFlowClose, ClosableOnlyOutsideTheRunningStates) {
  EXPECT_TRUE(flow.closable());
  ASSERT_TRUE(flow.request(installOf("app")));
  EXPECT_FALSE(flow.closable());
  service.completePrepare(FakeTransactionService::previewOf("plan-1", onePackagePlan()));
  EXPECT_TRUE(flow.closable());
  flow.confirm();
  EXPECT_FALSE(flow.closable());
  progress(ProgressPhase::Downloading, 1, 2);
  EXPECT_FALSE(flow.closable());
  service.completeExecute(ExecutionResult::succeeded());
  EXPECT_TRUE(flow.closable());
}

TEST_F(TransactionFlowClose, ClosingWhileIdleOrFinishedIsAcceptedAtOnce) {
  EXPECT_EQ(flow.closeRequested(), CloseDecision::AcceptNow);
}

TEST_F(TransactionFlowClose, ClosingWhilePreviewedCancelsThenSignalsReady) {
  toPreviewed();
  EXPECT_EQ(flow.closeRequested(), CloseDecision::DeferUntilCancelReply);
  ASSERT_EQ(service.cancelCalls(), 1U);
  EXPECT_EQ(close_ready, 0);
  service.completeCancel({});
  EXPECT_EQ(close_ready, 1);
}

TEST_F(TransactionFlowClose, ClosingWhilePreparingNeedsNoCancel) {
  ASSERT_TRUE(flow.request(installOf("app")));
  EXPECT_EQ(flow.closeRequested(), CloseDecision::AcceptNow);
  EXPECT_EQ(service.cancelCalls(), 0U);
  EXPECT_EQ(service.executeCalls(), 0U);
}

TEST_F(TransactionFlowClose, ClosingWhileAuthorizingOrBeforeTheCommitPointCancelsFirst) {
  toAuthorizing();
  progress(ProgressPhase::Downloading, 1, 2);
  EXPECT_EQ(flow.closeRequested(), CloseDecision::DeferUntilCancelReply);
  EXPECT_EQ(service.cancelCalls(), 1U);
  EXPECT_EQ(flow.closeRequested(), CloseDecision::DeferUntilCancelReply);
  EXPECT_EQ(service.cancelCalls(), 1U) << "no second Cancel call";
  service.completeCancel({});
  EXPECT_EQ(close_ready, 1);
}

TEST_F(TransactionFlowClose, ClosingAfterHooksIsIgnoredWithTheCloseUnavailableNotice) {
  toAuthorizing();
  progress(ProgressPhase::Hooks);
  EXPECT_EQ(flow.closeRequested(), CloseDecision::Ignore);
  EXPECT_EQ(flow.notice(), FlowNotice::CloseUnavailable);
  EXPECT_EQ(service.cancelCalls(), 0U);
  EXPECT_EQ(close_ready, 0);
}

TEST_F(TransactionFlowClose, ClosingIsAcceptedAfterTheTransactionFinishes) {
  toAuthorizing();
  progress(ProgressPhase::Installing, 1, 1);
  EXPECT_EQ(flow.closeRequested(), CloseDecision::Ignore);
  service.completeExecute(ExecutionResult::succeeded());
  EXPECT_EQ(flow.closeRequested(), CloseDecision::AcceptNow);
}

TEST_F(TransactionFlowClose, PastPointOfNoReturnReplyKeepsTheWindowAndTheRunning) {
  toAuthorizing();
  progress(ProgressPhase::Downloading, 1, 2);
  EXPECT_EQ(flow.closeRequested(), CloseDecision::DeferUntilCancelReply);
  service.completeCancel(std::unexpected(ServiceError{
      .kind = ServiceError::Kind::Refused,
      .failure = TransactionFailure{.code = TransactionErrorCode::PastPointOfNoReturn, .names = {}, .choices = {}},
  }));
  EXPECT_EQ(flow.state(), FlowState::Executing);
  EXPECT_EQ(close_ready, 0);
  EXPECT_EQ(flow.notice(), FlowNotice::CloseUnavailable);
  EXPECT_FALSE(flow.cancelEnabled());
  EXPECT_EQ(flow.closeRequested(), CloseDecision::Ignore);
}

TEST_F(TransactionFlowClose, PlanNotFoundFromCancelLetsAPendingCloseProceed) {
  toAuthorizing();
  EXPECT_EQ(flow.closeRequested(), CloseDecision::DeferUntilCancelReply);
  service.completeCancel(std::unexpected(ServiceError{
      .kind = ServiceError::Kind::Refused,
      .failure = TransactionFailure{.code = TransactionErrorCode::PlanNotFound, .names = {}, .choices = {}},
  }));
  EXPECT_EQ(close_ready, 1);
}

TEST_F(TransactionFlowClose, PlanNotFoundFromCancelWithoutAPendingCloseTakesNoAction) {
  toAuthorizing();
  progress(ProgressPhase::Downloading, 1, 2);
  flow.cancelRun();
  service.completeCancel(std::unexpected(ServiceError{
      .kind = ServiceError::Kind::Refused,
      .failure = TransactionFailure{.code = TransactionErrorCode::PlanNotFound, .names = {}, .choices = {}},
  }));
  EXPECT_EQ(flow.state(), FlowState::Executing);
  EXPECT_EQ(close_ready, 0);
  service.completeExecute(ExecutionResult::cancelled());
  EXPECT_EQ(flow.state(), FlowState::Cancelled);
}

TEST_F(TransactionFlowClose, ACancelFaultLetsAPendingCloseProceed) {
  toAuthorizing();
  EXPECT_EQ(flow.closeRequested(), CloseDecision::DeferUntilCancelReply);
  service.completeCancel(std::unexpected(ServiceError{.kind = ServiceError::Kind::NoReply, .failure = {}}));
  EXPECT_EQ(close_ready, 1);
}

}  // namespace
