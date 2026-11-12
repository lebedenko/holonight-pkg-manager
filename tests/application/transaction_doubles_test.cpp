#include "fake_package_list_refresh.h"
#include "fake_transaction_service.h"

#include <gtest/gtest.h>

namespace {

using namespace holonight_packages_domain;
using holonight_packages_testing::FakePackageListRefresh;
using holonight_packages_testing::FakeTransactionService;

TEST(TransactionDoubles, CallLogIsEmptyBeforeTheFirstCall) {
  const FakeTransactionService service;
  EXPECT_TRUE(service.calls().empty());
  EXPECT_EQ(service.heldPrepare(), 0U);
}

TEST(TransactionDoubles, HeldPrepareIsDeliveredExactlyOnceWhenReleased) {
  FakeTransactionService service;
  int deliveries = 0;
  service.preparePlan(PlanRequest{.action = PlanAction::Install, .names = {"app"}},
                      [&](const PrepareReply&) { ++deliveries; });
  EXPECT_EQ(deliveries, 0) << "the fake must not call back from inside the call";
  ASSERT_EQ(service.prepareCalls(), 1U);
  EXPECT_EQ(service.calls().front().request.names, std::vector<std::string>{"app"});

  EXPECT_TRUE(service.completePrepare(FakeTransactionService::previewOf("p1", ResolvedPlan{})));
  EXPECT_EQ(deliveries, 1);
  EXPECT_FALSE(service.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::Busy)));
  EXPECT_EQ(deliveries, 1);
}

TEST(TransactionDoubles, ExecuteCancelAndProgressAreRecordedAndForwarded) {
  FakeTransactionService service;
  std::vector<std::string> seen;
  service.setProgressHandler([&](const std::string& plan_id, const ProgressUpdate& update) {
    seen.push_back(plan_id + ":" + std::string(progressPhaseName(update.phase)));
  });
  bool executed = false;
  bool cancelled = false;
  service.execute("p1", [&](const ExecuteReply& reply) { executed = reply->outcome == ExecutionOutcome::Failed; });
  service.cancel("p1", [&](const CancelReply& reply) { cancelled = reply.has_value(); });
  service.emitProgress("p1", ProgressUpdate{.phase = ProgressPhase::Hooks});

  EXPECT_TRUE(service.completeExecute(FakeTransactionService::failed(TransactionErrorCode::NotAuthorized)));
  EXPECT_TRUE(service.completeCancel({}));
  EXPECT_TRUE(executed);
  EXPECT_TRUE(cancelled);
  EXPECT_EQ(service.executeCalls(), 1U);
  EXPECT_EQ(service.cancelCalls(), 1U);
  EXPECT_EQ(seen, std::vector<std::string>{"p1:Hooks"});
}

TEST(TransactionDoubles, RefreshCounterCountsRequests) {
  FakePackageListRefresh refresh;
  EXPECT_EQ(refresh.count(), 0);
  refresh.refreshAfterOutcome();
  refresh.refreshAfterOutcome();
  EXPECT_EQ(refresh.count(), 2);
}

// system-upgrade T-059: the probe and the upgrade refresh are recorded too.
TEST(TransactionDoubles, AProbeIsRecordedWithItsModeAndAnsweredWhenReleased) {
  holonight_packages_testing::FakeTransactionService service;
  std::optional<holonight_packages_domain::VersionReply> answer;
  service.setVersion(std::uint32_t{1});
  service.queryProtocolVersion(holonight_packages_domain::ProbeMode::AutoStart,
                               [&answer](holonight_packages_domain::VersionReply reply) { answer = reply; });
  EXPECT_FALSE(answer.has_value());
  ASSERT_EQ(service.probes().size(), 1U);
  EXPECT_EQ(service.probes().front(), holonight_packages_domain::ProbeMode::AutoStart);
  EXPECT_TRUE(service.calls().empty());  // a probe is not a transaction call
  service.completeProbes();
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(answer->value(), 1U);
}

TEST(TransactionDoubles, TheUpgradeRefreshIsCountedApart) {
  holonight_packages_testing::FakePackageListRefresh refresh;
  refresh.refreshAfterUpgrade();
  EXPECT_EQ(refresh.upgradeCount(), 1);
  EXPECT_EQ(refresh.count(), 0);
}

}  // namespace
