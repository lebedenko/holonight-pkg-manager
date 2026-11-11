#include "TransactionController.h"
#include "WindowCloseGuard.h"
#include "fake_package_list_refresh.h"
#include "fake_transaction_service.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QWindow>

#include <gtest/gtest.h>

namespace {

using namespace holonight_packages_domain;
using holonight_packages_testing::FakePackageListRefresh;
using holonight_packages_testing::FakeTransactionService;

ResolvedPlan plan() {
  ResolvedPlan resolved;
  resolved.changes = {
      PlannedChange{
          .name = "app",
          .kind = ChangeKind::Target,
          .old_version = "",
          .new_version = "1-1",
          .repository = "core",
      },
  };
  return resolved;
}

class WindowCloseGuardFixture : public testing::Test {
 public:
  WindowCloseGuardFixture() : controller(service, refresh), guard(controller, window) {
    window.resize(100, 100);
    window.show();
  }

  // A QCoreApplication::sendEvent of a close event is not pointer or focus automation (REQ-NF-004).
  bool closeAccepted() {
    QCloseEvent event;
    QCoreApplication::sendEvent(&window, &event);
    return event.isAccepted();
  }
  void toExecuting(ProgressPhase phase) {
    ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
    service.completePrepare(FakeTransactionService::previewOf("plan-1", plan()));
    controller.confirm();
    service.emitProgress("plan-1", ProgressUpdate{.phase = phase, .current = 1, .total = 2, .item = "x"});
  }

  FakeTransactionService service;
  FakePackageListRefresh refresh;
  TransactionController controller;
  QWindow window;
  WindowCloseGuard guard;
};

class WindowCloseGuardTest : public WindowCloseGuardFixture {};

TEST_F(WindowCloseGuardTest, ACloseWhileIdleIsAccepted) {
  EXPECT_TRUE(closeAccepted());
  EXPECT_EQ(service.cancelCalls(), 0U);
}

TEST_F(WindowCloseGuardTest, ACloseWhilePreparingIsAcceptedWithNoCancelCall) {
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
  EXPECT_TRUE(closeAccepted());
  EXPECT_EQ(service.cancelCalls(), 0U);
  EXPECT_EQ(service.executeCalls(), 0U);
}

TEST_F(WindowCloseGuardTest, ACloseBeforeTheCommitPointCancelsAndKeepsTheWindowUntilTheCancelReply) {
  toExecuting(ProgressPhase::Downloading);
  EXPECT_FALSE(closeAccepted());
  EXPECT_EQ(service.cancelCalls(), 1U);
  EXPECT_TRUE(window.isVisible()) << "kept open until the fake Cancel reply";
  service.completeCancel({});
  EXPECT_FALSE(window.isVisible()) << "closed after the Cancel reply";
}

TEST_F(WindowCloseGuardTest, ACloseAfterHooksIsIgnoredWithTheCloseUnavailableNotice) {
  toExecuting(ProgressPhase::Hooks);
  EXPECT_FALSE(closeAccepted());
  EXPECT_EQ(service.cancelCalls(), 0U);
  EXPECT_TRUE(window.isVisible());
  EXPECT_TRUE(controller.noticeText().contains(QStringLiteral("Closing is unavailable")));
}

TEST_F(WindowCloseGuardTest, ThePastPointOfNoReturnReplyKeepsTheWindowOpen) {
  toExecuting(ProgressPhase::Downloading);
  EXPECT_FALSE(closeAccepted());
  service.completeCancel(std::unexpected(ServiceError{
      .kind = ServiceError::Kind::Refused,
      .failure = TransactionFailure{.code = TransactionErrorCode::PastPointOfNoReturn, .names = {}, .choices = {}},
  }));
  EXPECT_TRUE(window.isVisible());
  EXPECT_FALSE(closeAccepted()) << "a later close is ignored as well";
}

TEST_F(WindowCloseGuardTest, TheWindowClosesOnceTheTransactionHasFinished) {
  toExecuting(ProgressPhase::Installing);
  EXPECT_FALSE(closeAccepted());
  service.completeExecute(ExecutionResult::succeeded());
  EXPECT_TRUE(closeAccepted());
}

TEST_F(WindowCloseGuardTest, ACloseWhilePreviewedCancelsThePlanThenCloses) {
  ASSERT_TRUE(controller.requestInstall(QStringLiteral("app")));
  service.completePrepare(FakeTransactionService::previewOf("plan-1", plan()));
  EXPECT_FALSE(closeAccepted());
  EXPECT_EQ(service.cancelCalls(), 1U);
  service.completeCancel({});
  EXPECT_FALSE(window.isVisible());
}

}  // namespace
