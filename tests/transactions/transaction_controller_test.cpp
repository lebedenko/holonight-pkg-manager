#include "TransactionController.h"
#include "fake_package_list_refresh.h"
#include "fake_transaction_service.h"
#include "manifest.h"

#include <QMetaEnum>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

using namespace holonight_packages_domain;
using holonight_packages_testing::FakePackageListRefresh;
using holonight_packages_testing::FakeTransactionService;
using State = TransactionController::State;

ResolvedPlan installPlan() {
  ResolvedPlan plan;
  plan.action = PlanAction::Install;
  plan.changes = {
      PlannedChange{
          .name = "app",
          .kind = ChangeKind::Target,
          .old_version = "",
          .new_version = "1-1",
          .repository = "extra",
      },
      PlannedChange{
          .name = "lib",
          .kind = ChangeKind::Dependency,
          .old_version = "",
          .new_version = "2-1",
          .repository = "core",
      },
  };
  plan.hooks = {HookPreview{.name = "ldconfig", .conditional = true}};
  return plan;
}

ResolvedPlan removePlan() {
  ResolvedPlan plan;
  plan.action = PlanAction::Remove;
  plan.changes = {
      PlannedChange{
          .name = "app",
          .kind = ChangeKind::Target,
          .old_version = "1-1",
          .new_version = "",
          .repository = "",
      },
      PlannedChange{
          .name = "lib",
          .kind = ChangeKind::Orphan,
          .old_version = "2-1",
          .new_version = "",
          .repository = "",
      },
      PlannedChange{
          .name = "util",
          .kind = ChangeKind::Orphan,
          .old_version = "3-1",
          .new_version = "",
          .repository = "",
      },
  };
  return plan;
}

class ControllerFixture : public testing::Test {
 public:
  ControllerFixture() : controller(service, refresh) {}

  void toPreviewed(const ResolvedPlan& plan, bool remove = false) {
    ASSERT_TRUE(remove ? controller.requestRemove(QStringLiteral("app"))
                       : controller.requestInstall(QStringLiteral("app")));
    ASSERT_TRUE(service.completePrepare(FakeTransactionService::previewOf("plan-1", plan)));
    ASSERT_EQ(controller.state(), State::Previewed);
  }
  void toExecuting() {
    toPreviewed(installPlan());
    controller.confirm();
    service.emitProgress("plan-1",
                         ProgressUpdate{.phase = ProgressPhase::Downloading, .current = 3, .total = 10, .item = "foo"});
    ASSERT_EQ(controller.state(), State::Executing);
  }

  FakeTransactionService service;
  FakePackageListRefresh refresh;
  TransactionController controller;
};

class TransactionControllerPreview : public ControllerFixture {};
class TransactionControllerResult : public ControllerFixture {};
class TransactionIsolation : public ControllerFixture {};

TEST_F(TransactionControllerPreview, StateEnumMirrorsTheElevenFlowStates) {
  const QMetaEnum meta = QMetaEnum::fromType<TransactionController::State>();
  EXPECT_EQ(meta.keyCount(), 11);
  const QMetaEnum pages = QMetaEnum::fromType<TransactionController::Page>();
  EXPECT_EQ(pages.keyCount(), 5);
}

TEST_F(TransactionControllerPreview, RequestInstallEntersPreparingWithOnePrepareCall) {
  EXPECT_TRUE(controller.requestInstall(QStringLiteral("app")));
  EXPECT_EQ(controller.state(), State::Preparing);
  EXPECT_EQ(controller.page(), TransactionController::Page::Preparing);
  EXPECT_EQ(service.prepareCalls(), 1U);
  EXPECT_EQ(controller.actionKind(), QStringLiteral("install"));
  EXPECT_EQ(controller.packageName(), QStringLiteral("app"));
  EXPECT_FALSE(controller.allowAction());
  EXPECT_FALSE(controller.closable());
}

TEST_F(TransactionControllerPreview, RemoveRequestUsesRemove) {
  EXPECT_TRUE(controller.requestRemove(QStringLiteral("app")));
  EXPECT_EQ(service.calls().front().request.action, PlanAction::Remove);
  EXPECT_EQ(controller.actionKind(), QStringLiteral("remove"));
}

TEST_F(TransactionControllerPreview, AnInstallPreviewListsRowsHooksAndTheExpiry) {
  toPreviewed(installPlan());
  EXPECT_EQ(controller.page(), TransactionController::Page::Confirm);
  EXPECT_EQ(controller.previewRows()->rowCount(), 2);
  EXPECT_EQ(controller.hookRows()->rowCount(), 1);
  EXPECT_EQ(controller.confirmLabel(), QStringLiteral("Install"));
  EXPECT_TRUE(controller.confirmEnabled());
  EXPECT_EQ(controller.actions(), (QStringList{QStringLiteral("confirm"), QStringLiteral("cancel")}));
  EXPECT_EQ(controller.title(), QStringLiteral("Install app"));
  EXPECT_FALSE(controller.expiresText().isEmpty());
}

TEST_F(TransactionControllerPreview, RemoveWithOrphansLabelsTheConfirmButtonWithTheCount) {
  toPreviewed(removePlan(), true);
  EXPECT_EQ(controller.confirmLabel(), QStringLiteral("Remove 3 packages"));
  EXPECT_EQ(controller.previewRows()->rowCount(), 3);
}

TEST_F(TransactionControllerPreview, RemoveOfASinglePackageLabelsItSingular) {
  ResolvedPlan plan = removePlan();
  plan.changes.resize(1);
  toPreviewed(plan, true);
  EXPECT_EQ(controller.confirmLabel(), QStringLiteral("Remove 1 package"));
}

TEST_F(TransactionControllerPreview, ConfirmExecutesAndTheProgressPropertiesFollowTheEvents) {
  toExecuting();
  EXPECT_EQ(controller.page(), TransactionController::Page::Progress);
  EXPECT_EQ(controller.phaseLabel(), QStringLiteral("Downloading"));
  EXPECT_EQ(controller.currentItem(), QStringLiteral("foo"));
  EXPECT_DOUBLE_EQ(controller.progressValue(), 0.3);
  EXPECT_FALSE(controller.progressIndeterminate());
  EXPECT_TRUE(controller.cancelEnabled());
  EXPECT_FALSE(controller.closable());
  EXPECT_FALSE(controller.allowAction());

  service.emitProgress("plan-1",
                       ProgressUpdate{.phase = ProgressPhase::Resolving, .current = 0, .total = 0, .item = ""});
  EXPECT_TRUE(controller.progressIndeterminate());
  EXPECT_DOUBLE_EQ(controller.progressValue(), 0.0);
}

TEST_F(TransactionControllerPreview, AuthorizingShowsTheAuthorizationLabelAndAnIndeterminateBar) {
  toPreviewed(installPlan());
  controller.confirm();
  EXPECT_EQ(controller.state(), State::Authorizing);
  EXPECT_FALSE(controller.phaseLabel().isEmpty());
  EXPECT_TRUE(controller.progressIndeterminate());
  EXPECT_TRUE(controller.cancelEnabled());
}

TEST_F(TransactionControllerPreview, HooksDisableCancelAndCancelRunShowsCancelling) {
  toExecuting();
  controller.cancelRun();
  EXPECT_FALSE(controller.cancelEnabled());
  EXPECT_EQ(controller.cancelLabel(), QStringLiteral("Cancelling"));
  EXPECT_EQ(service.cancelCalls(), 1U);
}

TEST_F(TransactionControllerPreview, HooksTurnTheCancelControlOff) {
  toExecuting();
  service.emitProgress("plan-1", ProgressUpdate{.phase = ProgressPhase::Hooks, .current = 0, .total = 0, .item = ""});
  EXPECT_FALSE(controller.cancelEnabled());
  EXPECT_EQ(controller.cancelLabel(), QStringLiteral("Cancel"));
}

TEST_F(TransactionControllerPreview, DismissingAPreviewCancelsThePlanAndReturnsToIdle) {
  toPreviewed(installPlan());
  controller.dismiss();
  EXPECT_EQ(controller.state(), State::Idle);
  EXPECT_FALSE(controller.dialogOpen());
  EXPECT_EQ(controller.openDialogCount(), 0);
  EXPECT_TRUE(controller.allowAction());
  EXPECT_EQ(service.cancelCalls(), 1U);
}

TEST_F(TransactionControllerPreview, AChangeEmitsTheCoarseChangedSignal) {
  QSignalSpy spy(&controller, &TransactionController::changed);
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
  EXPECT_GE(spy.count(), 1);
}

TEST_F(TransactionControllerPreview, ASecondRequestWhileBusyIsIgnored) {
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
  EXPECT_FALSE(controller.requestRemove(QStringLiteral("other")));
  EXPECT_EQ(service.prepareCalls(), 1U);
}

TEST_F(TransactionControllerPreview, CloseDecisionsAreForwardedFromTheFlow) {
  toExecuting();
  EXPECT_EQ(controller.handleCloseRequest(), holonight_packages_application::CloseDecision::DeferUntilCancelReply);
  QSignalSpy ready(&controller, &TransactionController::closeReady);
  service.completeCancel({});
  EXPECT_EQ(ready.count(), 1);
}

// ---- Result side (T-015) ------------------------------------------------------------------------------------------

TEST_F(TransactionControllerResult, SuccessShowsTheChangedRowsAndCloseOnly) {
  toPreviewed(installPlan());
  controller.confirm();
  service.completeExecute(ExecutionResult::succeeded({
      ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1-1"},
      ChangedPackage{.name = "lib", .action = ChangeAction::Install, .old_version = "", .new_version = "2-1"},
  }));
  EXPECT_EQ(controller.state(), State::Succeeded);
  EXPECT_EQ(controller.page(), TransactionController::Page::Result);
  EXPECT_EQ(controller.resultRows()->rowCount(), 2);
  EXPECT_EQ(controller.actions(), QStringList{QStringLiteral("close")});
  EXPECT_FALSE(controller.hasHookWarning());
  EXPECT_FALSE(controller.message().isEmpty());
  EXPECT_TRUE(controller.closable());
  EXPECT_TRUE(controller.allowAction());
}

TEST_F(TransactionControllerResult, SuccessWithAHookFailureCarriesTheWarningNoteAndKeepsTheRows) {
  toPreviewed(installPlan());
  controller.confirm();
  ExecutionResult result = ExecutionResult::succeeded(
      {ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1-1"}});
  result.warnings = {WarningToken::HookFailed};
  service.completeExecute(result);
  EXPECT_TRUE(controller.hasHookWarning());
  EXPECT_TRUE(controller.noticeText().contains(QStringLiteral("hook")));
  EXPECT_EQ(controller.resultRows()->rowCount(), 1);
}

TEST_F(TransactionControllerResult, FailureShowsTheTableMessageAndTheChangedList) {
  toPreviewed(installPlan());
  controller.confirm();
  service.completeExecute(FakeTransactionService::failed(
      TransactionErrorCode::TransactionFailed,
      {ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1-1"}}));
  EXPECT_EQ(controller.state(), State::Failed);
  EXPECT_TRUE(controller.message().contains(QStringLiteral("changed before the failure")));
  EXPECT_EQ(controller.resultRows()->rowCount(), 1);
}

TEST_F(TransactionControllerResult, FailureWithNoChangesShowsNoRowsAndTheNothingChangedWording) {
  toPreviewed(installPlan());
  controller.confirm();
  service.completeExecute(FakeTransactionService::failed(TransactionErrorCode::DiskSpace));
  EXPECT_EQ(controller.resultRows()->rowCount(), 0);
  EXPECT_TRUE(controller.message().contains(QStringLiteral("disk space")));
}

TEST_F(TransactionControllerResult, CancelledShowsNoRowsEvenIfTheReplyHasSome) {
  toPreviewed(installPlan());
  controller.confirm();
  ExecutionResult result = ExecutionResult::cancelled();
  result.changed = {
      ChangedPackage{.name = "x", .action = ChangeAction::Install, .old_version = "", .new_version = "1"},
  };
  service.completeExecute(result);
  EXPECT_EQ(controller.state(), State::Cancelled);
  EXPECT_EQ(controller.resultRows()->rowCount(), 0);
  EXPECT_TRUE(controller.message().contains(QStringLiteral("Nothing was changed")));
}

TEST_F(TransactionControllerResult, ActionsAreExactlyCloseForEveryCodeExceptTheRetryable) {
  for (std::size_t index = 0; index < kTransactionErrorCodeCount; ++index) {
    const auto code = static_cast<TransactionErrorCode>(index);
    FakeTransactionService local_service;
    FakePackageListRefresh local_refresh;
    TransactionController local(local_service, local_refresh);
    ASSERT_TRUE(local.requestInstall(QStringLiteral("app")));
    local_service.completePrepare(FakeTransactionService::refusal(code, {"app"}));
    EXPECT_EQ(local.state(), State::Refused);
    const bool retry = code == TransactionErrorCode::Busy || code == TransactionErrorCode::PlanNotFound ||
                       code == TransactionErrorCode::PlanExpired || code == TransactionErrorCode::SyncDatabaseChanged;
    const bool partial = code == TransactionErrorCode::PartialUpgradeRequired;  // system-upgrade REQ-F-089
    QStringList expected{QStringLiteral("close")};
    if (partial) {
      expected.prepend(QStringLiteral("updateSystem"));
    } else if (retry) {
      expected.prepend(QStringLiteral("tryAgain"));
    }
    EXPECT_EQ(local.actions(), expected) << index;
    EXPECT_FALSE(local.message().isEmpty());
  }
}

// GUI REQ-F-022 as amended by system-upgrade REQ-F-089.
TEST_F(TransactionControllerResult, PartialUpgradeRequiredOffersUpdateSystemAndNamesThePackages) {
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::PartialUpgradeRequired, {"glibc"}));
  EXPECT_EQ(controller.actions(), (QStringList{QStringLiteral("updateSystem"), QStringLiteral("close")}));
  EXPECT_TRUE(controller.message().contains(QStringLiteral("glibc")));
  EXPECT_TRUE(controller.message().contains(QStringLiteral("Update system")));
}

TEST_F(TransactionControllerResult, TryAgainAfterBusyRecordsOnePrepare) {
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::Busy));
  controller.tryAgain();
  EXPECT_EQ(controller.state(), State::Preparing);
  EXPECT_EQ(service.prepareCalls(), 2U);
}

TEST_F(TransactionControllerResult, NotAuthorizedReturnsToThePreviewWithTheNote) {
  toPreviewed(installPlan());
  controller.confirm();
  service.completeExecute(FakeTransactionService::failed(TransactionErrorCode::NotAuthorized));
  EXPECT_EQ(controller.state(), State::Previewed);
  EXPECT_TRUE(controller.noticeText().contains(QStringLiteral("Authorization was not granted")));
  EXPECT_TRUE(controller.confirmEnabled());
}

TEST_F(TransactionControllerResult, UnknownOffersOnlyClose) {
  toPreviewed(installPlan());
  controller.confirm();
  service.completeExecute(std::unexpected(ServiceError{.kind = ServiceError::Kind::NoReply, .failure = {}}));
  EXPECT_EQ(controller.state(), State::Unknown);
  EXPECT_EQ(controller.actions(), QStringList{QStringLiteral("close")});
  EXPECT_TRUE(controller.message().contains(QStringLiteral("Installed list")));
}

TEST_F(TransactionControllerResult, HelperUnavailableOffersOnlyClose) {
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
  service.completePrepare(std::unexpected(ServiceError{.kind = ServiceError::Kind::HelperUnavailable, .failure = {}}));
  EXPECT_EQ(controller.state(), State::HelperUnavailable);
  EXPECT_EQ(controller.actions(), QStringList{QStringLiteral("close")});
}

TEST_F(TransactionControllerResult, PackageNamesWithMarkupAppearLiterally) {
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("<b>x</b>")));
  service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::PackageNotFound, {"<b>x</b>"}));
  EXPECT_TRUE(controller.message().contains(QStringLiteral("<b>x</b>")));
  EXPECT_EQ(controller.packageName(), QStringLiteral("<b>x</b>"));
  EXPECT_TRUE(controller.title().contains(QStringLiteral("<b>x</b>")));
}

TEST_F(TransactionControllerResult, TheCloseUnavailableNoticeShowsAfterHooks) {
  toExecuting();
  service.emitProgress("plan-1", ProgressUpdate{.phase = ProgressPhase::Hooks, .current = 0, .total = 0, .item = ""});
  EXPECT_EQ(controller.handleCloseRequest(), holonight_packages_application::CloseDecision::Ignore);
  EXPECT_TRUE(controller.noticeText().contains(QStringLiteral("Closing is unavailable")));
}

// ---- Nothing is written to disk (REQ-NF-002) ----------------------------------------------------------------------

TEST_F(TransactionIsolation, AFullInstallAndAFullRemoveWriteNothingToTheIsolatedDirectories) {
  QTemporaryDir isolation;
  ASSERT_TRUE(isolation.isValid());
  const QByteArray saved_home = qgetenv("HOME");
  qputenv("HOME", isolation.path().toUtf8());
  for (const char* name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) {
    qputenv(name, (isolation.path() + QLatin1Char('/') + QLatin1String(name)).toUtf8());
  }
  const auto before = holonight_packages_testing::manifestOf(isolation.path().toStdString());

  toPreviewed(installPlan());
  controller.confirm();
  service.emitProgress("plan-1",
                       ProgressUpdate{.phase = ProgressPhase::Installing, .current = 1, .total = 1, .item = "app"});
  service.completeExecute(ExecutionResult::succeeded(
      {ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1-1"}}));
  controller.dismiss();
  toPreviewed(removePlan(), true);
  controller.confirm();
  service.completeExecute(ExecutionResult::succeeded(
      {ChangedPackage{.name = "app", .action = ChangeAction::Remove, .old_version = "1-1", .new_version = ""}}));
  controller.dismiss();

  const auto after = holonight_packages_testing::manifestOf(isolation.path().toStdString());
  EXPECT_EQ(before, after);
  qputenv("HOME", saved_home);
}

}  // namespace
