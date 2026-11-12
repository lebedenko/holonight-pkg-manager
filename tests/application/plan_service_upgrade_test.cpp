#include "fake_authorizer.h"
#include "fake_caller_identity.h"
#include "fake_clock.h"
#include "fake_plan_id_generator.h"
#include "fake_scratch_roots.h"
#include "fake_transaction_engine.h"
#include "holonight_packages_application/plan_service.h"
#include "manual_work_runner.h"
#include "recording_terminator.h"

#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::CancelDecision;
using holonight_packages_domain::ExecutionContext;
using holonight_packages_domain::ExecutionControl;
using holonight_packages_domain::ExecutionOutcome;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::HeldBackEntry;
using holonight_packages_domain::NewDependencyEntry;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::ProgressUpdate;
using holonight_packages_domain::ReplacementEntry;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;
using holonight_packages_domain::UpgradeDetail;
using holonight_packages_domain::UpgradeEntry;
using holonight_packages_domain::UpgradePrepared;
using holonight_packages_domain::UpgradePrepareOutcome;
using holonight_packages_domain::UpgradeWorkspace;
using holonight_packages_testing::FakeAuthorizer;
using holonight_packages_testing::FakeCallerIdentity;
using holonight_packages_testing::FakeClock;
using holonight_packages_testing::FakePlanIdGenerator;
using holonight_packages_testing::FakeScratchRoots;
using holonight_packages_testing::FakeTransactionEngine;
using holonight_packages_testing::ManualWorkRunner;
using holonight_packages_testing::RecordingTerminator;
using namespace std::chrono_literals;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

struct RecordingControl final : ExecutionControl {
  CancelDecision decision = CancelDecision::Accepted;
  int calls = 0;
  CancelDecision requestCancel() noexcept override {
    ++calls;
    return decision;
  }
};

UpgradePrepared upgradePlan() {
  ResolvedPlan plan;
  plan.action = PlanAction::Upgrade;
  plan.upgrade = UpgradeDetail{
      .upgrades = {UpgradeEntry{.name = "app", .old_version = "1", .new_version = "2", .repository = "core"},
                   UpgradeEntry{.name = "lib", .old_version = "1", .new_version = "2", .repository = "core"}},
      .replacements = {ReplacementEntry{
          .old_name = "old", .old_version = "1", .new_name = "new", .new_version = "1", .repository = "core"}},
      .new_dependencies = {NewDependencyEntry{.name = "dep", .version = "1", .repository = "core"}},
      .held_back = {HeldBackEntry{.name = "kept", .installed_version = "1"}},
      .keyring_packages = {},
      .download_bytes = 0,
      .size_change_bytes = 0,
  };
  return UpgradePrepared{.plan = plan, .guard = {}};
}

class PlanServiceUpgradeTest : public ::testing::Test {
 public:
  PlanServiceUpgradeTest()
      : service(PlanService::Dependencies{
            .engine = engine,
            .authorizer = authorizer,
            .identities = identities,
            .ids = ids,
            .clock = clock,
            .runner = runner,
            .terminator = terminator,
            .scratch = &scratch,
        }) {
    identities.set(":1.10", 1000);
    identities.set(":1.11", 1001);
    engine.prepareUpgradeResult.result = upgradePlan();
    service.setProgressForward([this](const std::string& owner, const PlanId& plan_id, const ProgressUpdate& update) {
      forwarded.emplace_back(owner, plan_id, update.phase);
    });
  }

  struct Outcome {
    bool replied = false;
    std::optional<PlanService::Prepared> prepared;
    std::optional<TransactionFailure> failure;
  };

  void prepareInto(Outcome& outcome, const std::string& caller = ":1.10", const std::vector<std::string>& keys = {}) {
    service.prepareUpgrade(caller, keys, [&outcome](auto result) {
      outcome.replied = true;
      if (result) {
        outcome.prepared = std::move(*result);
      } else {
        outcome.failure = std::move(result.error());
      }
    });
  }

  Outcome prepare(const std::string& caller = ":1.10", const std::vector<std::string>& keys = {}) {
    Outcome outcome;
    prepareInto(outcome, caller, keys);
    return outcome;
  }

  std::optional<ExecutionResult> execute(const std::string& caller, const PlanId& plan_id) {
    std::optional<ExecutionResult> result;
    service.execute(caller, plan_id, [&result](ExecutionResult outcome) { result = std::move(outcome); });
    return result;
  }

  FakeClock clock;
  FakeTransactionEngine engine;
  FakeAuthorizer authorizer;
  FakeCallerIdentity identities;
  FakePlanIdGenerator ids;
  ManualWorkRunner runner;
  RecordingTerminator terminator;
  FakeScratchRoots scratch;
  PlanService service;
  std::vector<std::tuple<std::string, PlanId, ProgressPhase>> forwarded;
};

// ---- T-043: preparation ----

TEST_F(PlanServiceUpgradeTest, PreparingNeedsNoAuthorizationAndKeepsTheScratchRoot) {
  authorizer.answer = AuthorizationDecision::NotAuthorized;
  const auto outcome = prepare();
  ASSERT_TRUE(outcome.prepared.has_value());
  EXPECT_EQ(outcome.prepared->id.size(), 32U);
  EXPECT_THAT(authorizer.requests(), IsEmpty());
  EXPECT_EQ(scratch.created(), 1);
  ASSERT_EQ(engine.scratchRoots().size(), 1U);
  EXPECT_TRUE(scratch.exists(engine.scratchRoots().front()));
  EXPECT_EQ(outcome.prepared->expires_at, clock.now() + 300s);
}

TEST_F(PlanServiceUpgradeTest, AnyOptionIsInvalidAndReachesNeitherScratchNorEngine) {
  const auto outcome = prepare(":1.10", {"noconfirm"});
  ASSERT_TRUE(outcome.failure.has_value());
  EXPECT_EQ(outcome.failure->code, TransactionErrorCode::InvalidArgument);
  EXPECT_EQ(scratch.created(), 0);
  EXPECT_EQ(engine.prepareUpgradeCalls(), 0);
}

TEST_F(PlanServiceUpgradeTest, ASecondPreparationAndAnInstallDuringAHeldPreparationAreBusy) {
  runner.deferred = true;
  Outcome first;
  prepareInto(first);
  ASSERT_TRUE(runner.pending());
  const auto second = prepare(":1.11");
  ASSERT_TRUE(second.failure.has_value());
  EXPECT_EQ(second.failure->code, TransactionErrorCode::Busy);
  std::optional<TransactionFailure> install;
  service.prepare(":1.11", "install", {"app"}, {}, [&install](auto result) {
    if (!result) {
      install = result.error();
    }
  });
  ASSERT_TRUE(install.has_value());
  EXPECT_EQ(install->code, TransactionErrorCode::Busy);
  EXPECT_EQ(scratch.created(), 1);
  runner.runPending();
  EXPECT_TRUE(first.prepared.has_value());
}

TEST_F(PlanServiceUpgradeTest, TheSeventeenthPlanOfOneUidIsBusyBeforeAnyScratchRoot) {
  for (int i = 0; i < 16; ++i) {
    ASSERT_TRUE(prepare().prepared.has_value());
  }
  const auto seventeenth = prepare();
  ASSERT_TRUE(seventeenth.failure.has_value());
  EXPECT_EQ(seventeenth.failure->code, TransactionErrorCode::Busy);
  EXPECT_EQ(scratch.created(), 16);
}

TEST_F(PlanServiceUpgradeTest, ARefusalRemovesTheScratchRootAndStoresNoPlan) {
  engine.prepareUpgradeResult.result =
      std::unexpected(TransactionFailure{.code = TransactionErrorCode::NothingToDo, .names = {}, .choices = {}});
  const auto outcome = prepare();
  ASSERT_TRUE(outcome.failure.has_value());
  EXPECT_EQ(outcome.failure->code, TransactionErrorCode::NothingToDo);
  EXPECT_EQ(service.planCount(), 0U);
  EXPECT_EQ(scratch.liveCount(), 0U);
  EXPECT_EQ(terminator.calls(), 0);
}

TEST_F(PlanServiceUpgradeTest, AStuckSyncRepliesFirstAndThenEndsTheProcess) {
  engine.prepareUpgradeResult.result =
      std::unexpected(TransactionFailure{.code = TransactionErrorCode::SyncFailed, .names = {}, .choices = {}});
  engine.prepareUpgradeResult.worker_stuck = true;
  Outcome outcome;
  terminator.onTerminate = [&outcome] { EXPECT_TRUE(outcome.replied); };
  prepareInto(outcome);
  EXPECT_EQ(outcome.failure->code, TransactionErrorCode::SyncFailed);
  EXPECT_EQ(terminator.calls(), 1);
  EXPECT_EQ(scratch.liveCount(), 0U);
}

TEST_F(PlanServiceUpgradeTest, PreparationProgressGoesToTheCallerWithAnEmptyPlanId) {
  engine.prepareUpgradeScript = [this](const std::filesystem::path&, const ExecutionContext& context) {
    context.progress(ProgressUpdate{.phase = ProgressPhase::Syncing, .current = 1, .total = 2, .item = "core"});
    return engine.prepareUpgradeResult;
  };
  ASSERT_TRUE(prepare().prepared.has_value());
  ASSERT_EQ(forwarded.size(), 1U);
  EXPECT_EQ(std::get<0>(forwarded.front()), ":1.10");
  EXPECT_EQ(std::get<1>(forwarded.front()), "");
}

// ---- T-044: every discard removes the scratch root ----

TEST_F(PlanServiceUpgradeTest, CancelDisconnectAndExpiryEachRemoveTheScratchRoot) {
  const auto cancelled = prepare();
  ASSERT_TRUE(service.cancel(":1.10", cancelled.prepared->id).has_value());
  EXPECT_EQ(scratch.removed().size(), 1U);

  ASSERT_TRUE(prepare().prepared.has_value());
  service.callerGone(":1.10");
  EXPECT_EQ(scratch.removed().size(), 2U);

  const auto expiring = prepare();
  clock.advance(301s);
  const auto result = execute(":1.10", expiring.prepared->id);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->failure.code, TransactionErrorCode::PlanExpired);
  EXPECT_THAT(authorizer.requests(), IsEmpty());
  EXPECT_EQ(scratch.removed().size(), 3U);
  EXPECT_EQ(scratch.liveCount(), 0U);
}

TEST_F(PlanServiceUpgradeTest, ExactlyThreeHundredSecondsReachesAuthorization) {
  const auto plan = prepare();
  clock.advance(300s);
  const auto result = execute(":1.10", plan.prepared->id);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(authorizer.requests().size(), 1U);
  EXPECT_EQ(result->outcome, ExecutionOutcome::Succeeded);
  EXPECT_EQ(execute(":1.10", plan.prepared->id)->failure.code, TransactionErrorCode::PlanNotFound);
}

TEST_F(PlanServiceUpgradeTest, TheScratchRootIsRemovedBeforeTheExecuteReply) {
  const auto plan = prepare();
  std::optional<bool> existedAtReply;
  const auto root = engine.scratchRoots().front();
  service.execute(":1.10", plan.prepared->id, [&](const ExecutionResult&) { existedAtReply = scratch.exists(root); });
  ASSERT_TRUE(existedAtReply.has_value());
  EXPECT_FALSE(*existedAtReply);
}

// ---- T-045: authorization ----

TEST_F(PlanServiceUpgradeTest, TheUpgradeActionGetsCountsAndNoNames) {
  const auto plan = prepare();
  ASSERT_TRUE(execute(":1.10", plan.prepared->id).has_value());
  ASSERT_EQ(authorizer.requests().size(), 1U);
  const AuthorizationRequest& request = authorizer.requests().front();
  EXPECT_EQ(request.action_id, "org.holonight.packages.upgrade");
  EXPECT_EQ(request.subject, SubjectKind::SystemBusName);
  EXPECT_EQ(request.bus_name, ":1.10");
  EXPECT_TRUE(request.allow_user_interaction);
  EXPECT_EQ(request.cancellation_id, plan.prepared->id);
  EXPECT_THAT(request.details, ElementsAre(std::pair<std::string, std::string>{"upgrades", "2"},
                                           std::pair<std::string, std::string>{"replacements", "1"},
                                           std::pair<std::string, std::string>{"new_packages", "1"}));
  EXPECT_EQ(engine.executeUpgradeCalls(), 1);
  EXPECT_EQ(engine.executeCalls(), 0);
}

TEST_F(PlanServiceUpgradeTest, ADenialKeepsThePlanAndItsScratchRootAndASecondExecuteRuns) {
  const auto plan = prepare();
  authorizer.answer = AuthorizationDecision::NotAuthorized;
  EXPECT_EQ(execute(":1.10", plan.prepared->id)->failure.code, TransactionErrorCode::NotAuthorized);
  EXPECT_EQ(engine.executeUpgradeCalls(), 0);
  EXPECT_TRUE(scratch.exists(engine.scratchRoots().front()));
  authorizer.answer = AuthorizationDecision::Authorized;
  EXPECT_EQ(execute(":1.10", plan.prepared->id)->outcome, ExecutionOutcome::Succeeded);
  EXPECT_EQ(engine.executeUpgradeCalls(), 1);
}

TEST_F(PlanServiceUpgradeTest, TheEngineGetsTheWorkspaceOfThePlan) {
  engine.prepareUpgradeResult.result->guard = {
      holonight_packages_domain::SyncFileStamp{.name = "core.db", .sha256 = "ab", .size = 1, .mtime_ns = 2}};
  const auto plan = prepare();
  std::optional<UpgradeWorkspace> seen;
  engine.executeUpgradeScript = [&seen](const ResolvedPlan&, const UpgradeWorkspace& workspace,
                                        const ExecutionContext&) {
    seen = workspace;
    return ExecutionResult::succeeded();
  };
  ASSERT_TRUE(execute(":1.10", plan.prepared->id).has_value());
  ASSERT_TRUE(seen.has_value());
  EXPECT_EQ(seen->scratch_root, engine.scratchRoots().front());
  ASSERT_EQ(seen->guard.size(), 1U);
  EXPECT_EQ(seen->guard.front().name, "core.db");
}

TEST_F(PlanServiceUpgradeTest, PrepareAndExecuteAreBusyDuringAnUpgradeExecution) {
  const auto first = prepare();
  const auto second = prepare(":1.11");
  runner.deferred = true;
  std::optional<ExecutionResult> running;
  service.execute(":1.10", first.prepared->id, [&running](ExecutionResult result) { running = std::move(result); });
  ASSERT_TRUE(runner.pending());
  EXPECT_EQ(prepare(":1.11").failure->code, TransactionErrorCode::Busy);
  EXPECT_EQ(execute(":1.11", second.prepared->id)->failure.code, TransactionErrorCode::Busy);
  runner.runPending();
  runner.deferred = false;
  EXPECT_EQ(execute(":1.11", second.prepared->id)->outcome, ExecutionOutcome::Succeeded);
}

// ---- T-046: cancel and disconnect during preparation ----

TEST_F(PlanServiceUpgradeTest, CancelWithAnEmptyIdAnswersCancelledRemovesTheScratchAndThenEnds) {
  runner.deferred = true;
  RecordingControl control;
  engine.prepareUpgradeScript = [&control, this](const std::filesystem::path&, const ExecutionContext& context) {
    context.control_changed(&control);
    context.control_changed(nullptr);
    return engine.prepareUpgradeResult;
  };
  Outcome outcome;
  prepareInto(outcome);
  terminator.onTerminate = [&outcome] { EXPECT_TRUE(outcome.replied); };
  ASSERT_TRUE(service.cancel(":1.10", "").has_value());
  ASSERT_TRUE(outcome.failure.has_value());
  EXPECT_EQ(outcome.failure->code, TransactionErrorCode::Cancelled);
  EXPECT_EQ(scratch.liveCount(), 0U);
  EXPECT_EQ(terminator.calls(), 1);
  runner.runPending();          // the late result of the abandoned preparation is ignored
  EXPECT_EQ(control.calls, 1);  // the engine saw the cancel as soon as it published its control
  EXPECT_EQ(service.planCount(), 0U);
  EXPECT_TRUE(service.slotBusy());
  EXPECT_FALSE(service.shutdownRequested());
  EXPECT_EQ(prepare().failure->code, TransactionErrorCode::Busy);
}

TEST_F(PlanServiceUpgradeTest, AStuckPreparationKeepsTheSlotTerminalBeforeAndAfterTheReply) {
  const auto pending = prepare();
  engine.prepareUpgradeResult.result = std::unexpected(TransactionFailure{.code = TransactionErrorCode::SyncFailed});
  engine.prepareUpgradeResult.worker_stuck = true;
  terminator.onTerminate = [&] {
    EXPECT_TRUE(service.slotBusy());
    EXPECT_FALSE(service.shutdownRequested());
    EXPECT_EQ(prepare().failure->code, TransactionErrorCode::Busy);
  };
  EXPECT_EQ(prepare().failure->code, TransactionErrorCode::SyncFailed);
  EXPECT_EQ(terminator.calls(), 1);
  EXPECT_EQ(execute(":1.10", pending.prepared->id)->failure.code, TransactionErrorCode::Busy);
}

TEST_F(PlanServiceUpgradeTest, ShutdownDuringPreparationWaitsForTheTerminatorEvenAfterWorkerCompletion) {
  const auto pending = prepare();
  runner.deferred = true;
  Outcome outcome;
  prepareInto(outcome);
  EXPECT_FALSE(service.shutdownRequested());
  EXPECT_EQ(outcome.failure->code, TransactionErrorCode::Cancelled);
  EXPECT_EQ(prepare().failure->code, TransactionErrorCode::Busy);
  runner.runPending();
  EXPECT_TRUE(service.hasActivity());
  EXPECT_FALSE(service.shutdownRequested());
  EXPECT_EQ(prepare().failure->code, TransactionErrorCode::Busy);
  EXPECT_EQ(execute(":1.10", pending.prepared->id)->failure.code, TransactionErrorCode::Busy);
}

TEST_F(PlanServiceUpgradeTest, AnotherCallersEmptyCancelIsPlanNotFoundAndTheirPreparationGoesOn) {
  runner.deferred = true;
  Outcome outcome;
  prepareInto(outcome);
  const auto other = service.cancel(":1.11", "");
  ASSERT_FALSE(other.has_value());
  EXPECT_EQ(other.error().code, TransactionErrorCode::PlanNotFound);
  runner.runPending();
  EXPECT_TRUE(outcome.prepared.has_value());
  EXPECT_EQ(terminator.calls(), 0);
}

TEST_F(PlanServiceUpgradeTest, TheOwnerDisconnectingDuringPreparationIsACancel) {
  runner.deferred = true;
  Outcome outcome;
  prepareInto(outcome);
  service.callerGone(":1.10");
  EXPECT_EQ(outcome.failure->code, TransactionErrorCode::Cancelled);
  EXPECT_EQ(scratch.liveCount(), 0U);
  EXPECT_EQ(terminator.calls(), 1);
  runner.runPending();
}

TEST_F(PlanServiceUpgradeTest, AnEmptyCancelWithNoPreparationIsPlanNotFound) {
  EXPECT_EQ(service.cancel(":1.10", "").error().code, TransactionErrorCode::PlanNotFound);
}

TEST_F(PlanServiceUpgradeTest, WithoutScratchRootsAnUpgradeIsAnInternalError) {
  PlanService bare(PlanService::Dependencies{
      .engine = engine,
      .authorizer = authorizer,
      .identities = identities,
      .ids = ids,
      .clock = clock,
      .runner = runner,
      .terminator = terminator,
  });
  std::optional<TransactionFailure> failure;
  bare.prepareUpgrade(":1.10", {}, [&failure](auto result) {
    if (!result) {
      failure = result.error();
    }
  });
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->code, TransactionErrorCode::InternalError);
}

}  // namespace
}  // namespace holonight_packages_application
