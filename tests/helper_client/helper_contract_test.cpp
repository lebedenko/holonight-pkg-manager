// Runs the GUI's adapter against the real HelperService (over PlanService with fake engine and authorizer), so the
// GUI's copy of the wire structures and the helper's own encoder cannot drift apart (REQ-C-004).
#include "DBusCallerIdentity.h"
#include "DBusTransactionService.h"
#include "HelperService.h"
#include "QtWorkRunner.h"
#include "WireTypes.h"
#include "fake_authorizer.h"
#include "fake_plan_id_generator.h"
#include "fake_transaction_engine.h"
#include "helper_bus_fixture.h"
#include "holonight_packages_application/update_check_runtime.h"
#include "recording_terminator.h"

#include <optional>

namespace {

using namespace holonight_packages_domain;
using holonight_packages_application::AuthorizationDecision;
using holonight_packages_application::PlanService;
using holonight_packages_application::SystemClock;
using holonight_packages_testing::FakeAuthorizer;
using holonight_packages_testing::FakePlanIdGenerator;
using holonight_packages_testing::FakeTransactionEngine;
using holonight_packages_testing::HelperBusFixture;
using holonight_packages_testing::RecordingTerminator;

class HelperContract : public HelperBusFixture {
 public:
  void SetUp() override {
    HelperBusFixture::SetUp();
    holonight_helper::registerWireTypes();
    server_ = std::make_unique<QDBusConnection>(connect(QStringLiteral("server")));
    ResolvedPlan plan;
    plan.action = PlanAction::Install;
    plan.changes = {
        PlannedChange{
            .name = "app",
            .kind = ChangeKind::Target,
            .old_version = "",
            .new_version = "1.0-1",
            .repository = "core",
            .download_bytes = 0,
        },
        PlannedChange{
            .name = "lib",
            .kind = ChangeKind::Dependency,
            .old_version = "",
            .new_version = "2.0-1",
            .repository = "extra",
            .download_bytes = 0,
        },
    };
    plan.hooks = {HookPreview{.name = "ldconfig", .conditional = true}};
    engine_.resolveResult = plan;
    identities_ = std::make_unique<holonight_helper::DBusCallerIdentity>(*server_);
    plans_ = std::make_unique<PlanService>(PlanService::Dependencies{
        .engine = engine_,
        .authorizer = authorizer_,
        .identities = *identities_,
        .ids = ids_,
        .clock = clock_,
        .runner = runner_,
        .terminator = terminator_,
    });
    service_ = std::make_unique<holonight_helper::HelperService>(*server_, *plans_);
    ASSERT_TRUE(service_->start());
    adapter_ = std::make_unique<DBusTransactionService>([this] { return connect(QStringLiteral("adapter")); });
    adapter_->setProgressHandler(
        [this](const std::string& plan_id, const ProgressUpdate& update) { progress_.emplace_back(plan_id, update); });
  }

  void TearDown() override {
    adapter_.reset();
    service_.reset();
    plans_.reset();
    HelperBusFixture::TearDown();
  }

  [[nodiscard]] PrepareReply prepare(const PlanRequest& request) const {
    std::optional<PrepareReply> reply;
    adapter_->preparePlan(request, [&](PrepareReply done) { reply = std::move(done); });
    EXPECT_TRUE(QTest::qWaitFor([&] { return reply.has_value(); }, 10000));
    return reply ? std::move(*reply) : PrepareReply(std::unexpected(ServiceError{}));
  }
  [[nodiscard]] ExecuteReply execute(const std::string& plan_id) const {
    std::optional<ExecuteReply> reply;
    adapter_->execute(plan_id, [&](ExecuteReply done) { reply = std::move(done); });
    EXPECT_TRUE(QTest::qWaitFor([&] { return reply.has_value(); }, 10000));
    return reply ? std::move(*reply) : ExecuteReply(std::unexpected(ServiceError{}));
  }

  std::unique_ptr<QDBusConnection> server_;
  FakeTransactionEngine engine_;
  FakeAuthorizer authorizer_;
  FakePlanIdGenerator ids_;
  SystemClock clock_;
  holonight_helper::QtWorkRunner runner_;
  RecordingTerminator terminator_;
  std::unique_ptr<holonight_helper::DBusCallerIdentity> identities_;
  std::unique_ptr<PlanService> plans_;
  std::unique_ptr<holonight_helper::HelperService> service_;
  std::unique_ptr<DBusTransactionService> adapter_;
  std::vector<std::pair<std::string, ProgressUpdate>> progress_;
};

TEST_F(HelperContract, APrepareDecodesTheHelpersOwnPreviewEncoding) {
  const PrepareReply prepared = prepare(PlanRequest{.action = PlanAction::Install, .names = {"app"}});
  ASSERT_TRUE(prepared.has_value());
  EXPECT_FALSE(prepared->plan_id.empty());
  ASSERT_EQ(prepared->plan.changes.size(), 2U);
  EXPECT_EQ(prepared->plan.changes[0].name, "app");
  EXPECT_EQ(prepared->plan.changes[0].kind, ChangeKind::Target);
  EXPECT_EQ(prepared->plan.changes[0].repository, "core");
  EXPECT_EQ(prepared->plan.changes[1].kind, ChangeKind::Dependency);
  ASSERT_EQ(prepared->plan.hooks.size(), 1U);
  EXPECT_TRUE(prepared->plan.hooks[0].conditional);
  EXPECT_GT(prepared->expires_at.time_since_epoch().count(), 0);
}

TEST_F(HelperContract, ARefusalDecodesToItsCodeAndNames) {
  engine_.resolveResult = std::unexpected(
      TransactionFailure{.code = TransactionErrorCode::PartialUpgradeRequired, .names = {"glibc"}, .choices = {}});
  const PrepareReply prepared = prepare(PlanRequest{.action = PlanAction::Install, .names = {"app"}});
  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(prepared.error().kind, ServiceError::Kind::Refused);
  EXPECT_EQ(prepared.error().failure.code, TransactionErrorCode::PartialUpgradeRequired);
  EXPECT_EQ(prepared.error().failure.names, std::vector<std::string>{"glibc"});
}

TEST_F(HelperContract, AnAllowedExecuteDecodesTheChangedListAndStreamsProgress) {
  engine_.executeScript = [](const auto&, const auto&, const ExecutionContext& context) {
    context.progress(ProgressUpdate{.phase = ProgressPhase::Downloading, .current = 1, .total = 2, .item = "app"});
    context.progress(ProgressUpdate{.phase = ProgressPhase::Done, .current = 1, .total = 1, .item = {}});
    auto result = ExecutionResult::succeeded(
        {ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1.0-1"}});
    result.warnings = {WarningToken::HookFailed};
    return result;
  };
  const PrepareReply prepared = prepare(PlanRequest{.action = PlanAction::Install, .names = {"app"}});
  ASSERT_TRUE(prepared.has_value());
  const ExecuteReply executed = execute(prepared->plan_id);
  ASSERT_TRUE(executed.has_value());
  EXPECT_EQ(executed->outcome, ExecutionOutcome::Succeeded);
  ASSERT_EQ(executed->changed.size(), 1U);
  EXPECT_EQ(executed->changed[0].new_version, "1.0-1");
  EXPECT_EQ(executed->warnings, std::vector<WarningToken>{WarningToken::HookFailed});
  QTest::qWait(200);
  ASSERT_GE(progress_.size(), 2U);
  EXPECT_EQ(progress_.front().second.phase, ProgressPhase::Downloading);
  EXPECT_EQ(progress_.front().second.item, "app");
  EXPECT_EQ(progress_.back().second.phase, ProgressPhase::Done);
}

TEST_F(HelperContract, ADeniedExecuteDecodesToNotAuthorized) {
  authorizer_.answer = AuthorizationDecision::NotAuthorized;
  const PrepareReply prepared = prepare(PlanRequest{.action = PlanAction::Install, .names = {"app"}});
  ASSERT_TRUE(prepared.has_value());
  const ExecuteReply executed = execute(prepared->plan_id);
  ASSERT_TRUE(executed.has_value());
  EXPECT_EQ(executed->outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(executed->failure.code, TransactionErrorCode::NotAuthorized);
  EXPECT_EQ(engine_.executeCalls(), 0);
}

TEST_F(HelperContract, ACancelOfAPendingPlanDecodesToSuccessAndTheNextExecuteFindsNoPlan) {
  const PrepareReply prepared = prepare(PlanRequest{.action = PlanAction::Install, .names = {"app"}});
  ASSERT_TRUE(prepared.has_value());
  std::optional<CancelReply> cancelled;
  adapter_->cancel(prepared->plan_id, [&](CancelReply done) { cancelled = std::move(done); });
  ASSERT_TRUE(QTest::qWaitFor([&] { return cancelled.has_value(); }, 10000));
  EXPECT_TRUE(cancelled->has_value());
  const ExecuteReply executed = execute(prepared->plan_id);
  ASSERT_TRUE(executed.has_value());
  EXPECT_EQ(executed->failure.code, TransactionErrorCode::PlanNotFound);
}

}  // namespace
