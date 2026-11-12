#include "holonight_packages_application/plan_service.h"

#include "fake_authorizer.h"
#include "fake_caller_identity.h"
#include "fake_clock.h"
#include "fake_plan_id_generator.h"
#include "fake_transaction_engine.h"
#include "manual_work_runner.h"
#include "recording_terminator.h"

#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::CancelDecision;
using holonight_packages_domain::ChangeKind;
using holonight_packages_domain::ExecutionContext;
using holonight_packages_domain::ExecutionControl;
using holonight_packages_domain::ExecutionOutcome;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlannedChange;
using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::ProgressUpdate;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;
using holonight_packages_testing::FakeAuthorizer;
using holonight_packages_testing::FakeCallerIdentity;
using holonight_packages_testing::FakeClock;
using holonight_packages_testing::FakePlanIdGenerator;
using holonight_packages_testing::FakeTransactionEngine;
using holonight_packages_testing::ManualWorkRunner;
using holonight_packages_testing::RecordingTerminator;
using namespace std::chrono_literals;
using ::testing::ElementsAre;

struct RecordingControl final : ExecutionControl {
  CancelDecision decision = CancelDecision::Accepted;
  int calls = 0;
  CancelDecision requestCancel() noexcept override {
    ++calls;
    return decision;
  }
};

class PlanServiceTest : public ::testing::Test {
 public:
  PlanServiceTest()
      : service(PlanService::Dependencies{
            .engine = engine,
            .authorizer = authorizer,
            .identities = identities,
            .ids = ids,
            .clock = clock,
            .runner = runner,
            .terminator = terminator,
        }) {
    identities.set(":1.10", 1000);
    identities.set(":1.11", 1001);
    ResolvedPlan plan;
    plan.action = PlanAction::Install;
    plan.changes = {
        PlannedChange{
            .name = "app",
            .kind = ChangeKind::Target,
            .old_version = {},
            .new_version = "1.0-1",
            .repository = "core",
            .download_bytes = 0,
        },
    };
    engine.resolveResult = plan;
  }

  struct PrepareOutcome {
    std::optional<PlanService::Prepared> prepared;
    std::optional<TransactionFailure> failure;
  };

  PrepareOutcome prepare(const std::string& caller = ":1.10", const std::string& action = "install",
                         const std::vector<std::string>& names = {"app"}) {
    PrepareOutcome outcome;
    service.prepare(caller, action, names, {}, [&outcome](auto result) {
      if (result) {
        outcome.prepared = std::move(*result);
      } else {
        outcome.failure = std::move(result.error());
      }
    });
    return outcome;
  }

  std::optional<ExecutionResult> execute(const std::string& caller, const PlanId& plan_id) {
    std::optional<ExecutionResult> result;
    service.execute(caller, plan_id, [&result](ExecutionResult outcome) { result = std::move(outcome); });
    return result;
  }

  PlanId preparedId(const std::string& caller = ":1.10") {
    const auto outcome = prepare(caller);
    EXPECT_TRUE(outcome.prepared.has_value());
    return outcome.prepared ? outcome.prepared->id : PlanId();
  }

  FakeClock clock;
  FakeTransactionEngine engine;
  FakeAuthorizer authorizer;
  FakeCallerIdentity identities;
  FakePlanIdGenerator ids;
  ManualWorkRunner runner;
  RecordingTerminator terminator;
  PlanService service;
};

// ---- preparation (T-030) ----

TEST_F(PlanServiceTest, PreparingNeedsNoAuthorizationAndReturnsDistinctIds) {
  authorizer.answer = AuthorizationDecision::NotAuthorized;
  const auto first = prepare();
  const auto second = prepare();
  ASSERT_TRUE(first.prepared && second.prepared);
  EXPECT_NE(first.prepared->id, second.prepared->id);
  EXPECT_EQ(first.prepared->id.size(), 32U);
  EXPECT_THAT(authorizer.requests(), ::testing::IsEmpty());
  EXPECT_EQ(first.prepared->plan.changes.size(), 1U);
  EXPECT_EQ(first.prepared->expires_at, clock.now() + 300s);
}

TEST_F(PlanServiceTest, InvalidRequestsNeverReachTheEngine) {
  EXPECT_EQ(prepare(":1.10", "upgrade").failure->code, TransactionErrorCode::InvalidArgument);
  EXPECT_EQ(prepare(":1.10", "install", {}).failure->code, TransactionErrorCode::InvalidArgument);
  EXPECT_EQ(prepare(":1.10", "install", {"/tmp/x.pkg.tar.zst"}).failure->code, TransactionErrorCode::InvalidArgument);
  EXPECT_EQ(prepare(":1.10", "install", {"https://example.org/a"}).failure->code,
            TransactionErrorCode::InvalidArgument);
  EXPECT_EQ(engine.resolveCalls(), 0);
  EXPECT_EQ(service.planCount(), 0U);
}

TEST_F(PlanServiceTest, EngineRefusalsPassThroughAndCreateNoPlan) {
  engine.resolveResult = std::unexpected(
      TransactionFailure{.code = TransactionErrorCode::PackageNotFound, .names = {"app"}, .choices = {}});
  const auto outcome = prepare();
  ASSERT_TRUE(outcome.failure.has_value());
  EXPECT_EQ(outcome.failure->code, TransactionErrorCode::PackageNotFound);
  EXPECT_THAT(outcome.failure->names, ElementsAre("app"));
  EXPECT_EQ(service.planCount(), 0U);
  EXPECT_FALSE(service.slotBusy());
}

TEST_F(PlanServiceTest, CapacityIsCheckedBeforeTheEngineRuns) {
  for (int i = 0; i < 16; ++i) {
    ASSERT_TRUE(prepare().prepared.has_value());
  }
  EXPECT_EQ(engine.resolveCalls(), 16);
  const auto seventeenth = prepare();
  ASSERT_TRUE(seventeenth.failure.has_value());
  EXPECT_EQ(seventeenth.failure->code, TransactionErrorCode::Busy);
  EXPECT_EQ(engine.resolveCalls(), 16);                // no libalpm work for a refused request
  EXPECT_TRUE(prepare(":1.11").prepared.has_value());  // another user still has room
}

TEST_F(PlanServiceTest, AnUnknownCallerCannotPrepare) {
  EXPECT_EQ(prepare(":1.99").failure->code, TransactionErrorCode::InternalError);
  EXPECT_EQ(engine.resolveCalls(), 0);
}

TEST_F(PlanServiceTest, PreparingWhileAnotherPlanIsPreparingOrRunningIsBusy) {
  runner.deferred = true;
  PrepareOutcome first;
  service.prepare(":1.10", "install", {"app"}, {}, [&first](auto result) { first.prepared = std::move(*result); });
  EXPECT_TRUE(service.slotBusy());
  EXPECT_EQ(prepare(":1.11").failure->code, TransactionErrorCode::Busy);
  runner.runPending();
  ASSERT_TRUE(first.prepared.has_value());
  EXPECT_FALSE(service.slotBusy());
}

// ---- lookup, expiry, consumption (T-029/T-033/T-044) ----

TEST_F(PlanServiceTest, UnknownAndForeignPlansReplyIdenticallyAndAskNobody) {
  const PlanId plan_id = preparedId();
  const auto unknown = execute(":1.10", "00000000000000000000000000000fff");
  const auto foreign = execute(":1.11", plan_id);
  ASSERT_TRUE(unknown && foreign);
  EXPECT_EQ(unknown->failure, foreign->failure);
  EXPECT_EQ(unknown->failure.code, TransactionErrorCode::PlanNotFound);
  EXPECT_THAT(authorizer.requests(), ::testing::IsEmpty());
  EXPECT_EQ(engine.executeCalls(), 0);
}

TEST_F(PlanServiceTest, ExpiredPlansReplyPlanExpiredOnceAndThenPlanNotFound) {
  const PlanId plan_id = preparedId();
  clock.advance(299s);
  EXPECT_EQ(authorizer.requests().size(), 0U);
  clock.advance(2s);
  EXPECT_EQ(execute(":1.10", plan_id)->failure.code, TransactionErrorCode::PlanExpired);
  EXPECT_EQ(execute(":1.10", plan_id)->failure.code, TransactionErrorCode::PlanNotFound);
  EXPECT_THAT(authorizer.requests(), ::testing::IsEmpty());
}

TEST_F(PlanServiceTest, ASuccessfulExecuteConsumesThePlan) {
  const PlanId plan_id = preparedId();
  engine.executeResult = ExecutionResult::succeeded({
      holonight_packages_domain::ChangedPackage{
          .name = "app",
          .action = holonight_packages_domain::ChangeAction::Install,
          .old_version = "",
          .new_version = "1.0-1",
      },
  });
  const auto result = execute(":1.10", plan_id);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, ExecutionOutcome::Succeeded);
  EXPECT_EQ(result->changed.size(), 1U);
  EXPECT_EQ(engine.executeCalls(), 1);
  EXPECT_EQ(execute(":1.10", plan_id)->failure.code, TransactionErrorCode::PlanNotFound);
  EXPECT_EQ(engine.executeCalls(), 1);
  EXPECT_EQ(service.planCount(), 0U);
}

TEST_F(PlanServiceTest, TheAuthorizationRequestNamesTheBusSubjectAndTheActionOfThePlan) {
  const PlanId installId = preparedId();
  ASSERT_TRUE(execute(":1.10", installId).has_value());
  const PlanId removeId = prepare(":1.10", "remove", {"app"}).prepared->id;
  ASSERT_TRUE(execute(":1.10", removeId).has_value());
  ASSERT_EQ(authorizer.requests().size(), 2U);
  const AuthorizationRequest& install = authorizer.requests()[0];
  EXPECT_EQ(install.subject, SubjectKind::SystemBusName);
  EXPECT_EQ(install.bus_name, ":1.10");
  EXPECT_EQ(install.action_id, "org.holonight.packages.install");
  EXPECT_TRUE(install.allow_user_interaction);
  EXPECT_EQ(install.cancellation_id, installId);
  EXPECT_THAT(install.details, ElementsAre(std::pair<std::string, std::string>{"packages", "app"}));
  EXPECT_EQ(authorizer.requests()[1].action_id, "org.holonight.packages.remove");
}

TEST_F(PlanServiceTest, AuthorizationHappensBeforeTheEngineRunsAndItsMutationsAreVisible) {
  std::vector<std::string> events;
  authorizer.beforeAnswer = [&events](const AuthorizationRequest&) { events.emplace_back("authorized"); };
  engine.executeScript = [&events](const auto&, const auto&, const ExecutionContext&) {
    events.emplace_back("engine");
    return ExecutionResult::succeeded();
  };
  ASSERT_TRUE(execute(":1.10", preparedId()).has_value());
  EXPECT_EQ(events, (std::vector<std::string>{"authorized", "engine"}));
}

TEST_F(PlanServiceTest, DeniedChallengedAndFailedAuthorizationAreNotAuthorizedAndKeepThePlan) {
  const PlanId plan_id = preparedId();
  for (const AuthorizationDecision decision :
       {AuthorizationDecision::NotAuthorized, AuthorizationDecision::Challenge, AuthorizationDecision::Error}) {
    authorizer.answer = decision;
    const auto result = execute(":1.10", plan_id);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->outcome, ExecutionOutcome::Failed);
    EXPECT_EQ(result->failure.code, TransactionErrorCode::NotAuthorized);
  }
  EXPECT_EQ(engine.executeCalls(), 0);
  authorizer.answer = AuthorizationDecision::Authorized;
  EXPECT_EQ(execute(":1.10", plan_id)->outcome, ExecutionOutcome::Succeeded);
}

TEST_F(PlanServiceTest, AHeldDatabaseLockKeepsThePlanButEveryOtherFailureConsumesIt) {
  const PlanId plan_id = preparedId();
  engine.executeResult = ExecutionResult::failed(TransactionErrorCode::Busy);
  EXPECT_EQ(execute(":1.10", plan_id)->failure.code, TransactionErrorCode::Busy);
  engine.executeResult = ExecutionResult::succeeded();
  EXPECT_EQ(execute(":1.10", plan_id)->outcome, ExecutionOutcome::Succeeded);  // still there

  for (const TransactionErrorCode code : {
           TransactionErrorCode::PlanChanged,
           TransactionErrorCode::ConfigurationError,
           TransactionErrorCode::DatabaseUnavailable,
           TransactionErrorCode::TransactionFailed,
       }) {
    const PlanId other = preparedId();
    engine.executeResult = ExecutionResult::failed(code);
    EXPECT_EQ(execute(":1.10", other)->failure.code, code);
    EXPECT_EQ(execute(":1.10", other)->failure.code, TransactionErrorCode::PlanNotFound) << static_cast<int>(code);
  }
  const PlanId cancelled = preparedId();
  engine.executeResult = ExecutionResult::cancelled();
  EXPECT_EQ(execute(":1.10", cancelled)->outcome, ExecutionOutcome::Cancelled);
  EXPECT_EQ(execute(":1.10", cancelled)->failure.code, TransactionErrorCode::PlanNotFound);
}

TEST_F(PlanServiceTest, ExecuteWhileTheSlotIsBusyIsBusyAndLeavesThePlanAlone) {
  const PlanId running = preparedId();
  const PlanId waiting = preparedId(":1.11");
  runner.deferred = true;
  std::optional<ExecutionResult> first;
  service.execute(":1.10", running, [&first](ExecutionResult outcome) { first = std::move(outcome); });
  EXPECT_TRUE(service.slotBusy());
  EXPECT_EQ(execute(":1.11", waiting)->failure.code, TransactionErrorCode::Busy);
  EXPECT_EQ(prepare(":1.11").failure->code, TransactionErrorCode::Busy);
  runner.runPending();
  ASSERT_TRUE(first.has_value());
  runner.deferred = false;
  EXPECT_EQ(execute(":1.11", waiting)->outcome, ExecutionOutcome::Succeeded);  // the plan survived the Busy reply
}

TEST_F(PlanServiceTest, ProgressIsForwardedWithTheOwnersUniqueName) {
  std::vector<std::pair<std::string, ProgressPhase>> seen;
  service.setProgressForward([&seen](const std::string& owner, const PlanId&, const ProgressUpdate& update) {
    seen.emplace_back(owner, update.phase);
  });
  engine.executeScript = [](const auto&, const auto&, const ExecutionContext& context) {
    context.progress(ProgressUpdate{.phase = ProgressPhase::Downloading, .current = 1, .total = 2, .item = "app"});
    context.progress(ProgressUpdate{.phase = ProgressPhase::Done, .current = 1, .total = 1, .item = {}});
    return ExecutionResult::succeeded();
  };
  ASSERT_TRUE(execute(":1.10", preparedId()).has_value());
  EXPECT_THAT(seen, ElementsAre(std::pair<std::string, ProgressPhase>{":1.10", ProgressPhase::Downloading},
                                std::pair<std::string, ProgressPhase>{":1.10", ProgressPhase::Done}));
}

// ---- cancel and disconnect ----

TEST_F(PlanServiceTest, CancellingAPendingPlanDiscardsItWithoutTouchingTheEngine) {
  const PlanId plan_id = preparedId();
  EXPECT_TRUE(service.cancel(":1.10", plan_id).has_value());
  EXPECT_EQ(execute(":1.10", plan_id)->failure.code, TransactionErrorCode::PlanNotFound);
  EXPECT_EQ(engine.executeCalls(), 0);
  EXPECT_EQ(service.cancel(":1.10", plan_id).error().code, TransactionErrorCode::PlanNotFound);
  EXPECT_EQ(service.cancel(":1.11", preparedId()).error().code, TransactionErrorCode::PlanNotFound);  // foreign
}

TEST_F(PlanServiceTest, CancelDuringAuthorizationCancelsThePromptAndStartsNothing) {
  authorizer.defer = true;
  const PlanId plan_id = preparedId();
  std::optional<ExecutionResult> result;
  service.execute(":1.10", plan_id, [&result](ExecutionResult outcome) { result = std::move(outcome); });
  EXPECT_TRUE(service.slotBusy());
  EXPECT_TRUE(service.cancel(":1.10", plan_id).has_value());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, ExecutionOutcome::Cancelled);
  EXPECT_THAT(authorizer.cancelled(), ElementsAre(plan_id));
  EXPECT_EQ(engine.executeCalls(), 0);
  EXPECT_EQ(terminator.calls(), 0);  // nothing ran, so there is nothing to stop
  EXPECT_FALSE(service.slotBusy());
  EXPECT_EQ(execute(":1.10", plan_id)->failure.code, TransactionErrorCode::PlanNotFound);
}

TEST_F(PlanServiceTest, DisconnectDiscardsPendingPlansAndCancelsAnAuthorization) {
  const PlanId pending = preparedId();
  service.callerGone(":1.10");
  EXPECT_EQ(execute(":1.10", pending)->failure.code, TransactionErrorCode::PlanNotFound);

  authorizer.defer = true;
  const PlanId waiting = preparedId();
  std::optional<ExecutionResult> result;
  service.execute(":1.10", waiting, [&result](ExecutionResult outcome) { result = std::move(outcome); });
  service.callerGone(":1.10");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, ExecutionOutcome::Cancelled);
  EXPECT_EQ(engine.executeCalls(), 0);
}

TEST_F(PlanServiceTest, CancelWhileRunningAsksTheEnginesControl) {
  const PlanId plan_id = preparedId();
  RecordingControl control;
  std::optional<std::expected<void, TransactionFailure>> answer;
  engine.executeScript = [&](const auto&, const auto&, const ExecutionContext& context) {
    context.control_changed(&control);
    answer = service.cancel(":1.10", plan_id);
    context.control_changed(nullptr);
    return ExecutionResult::cancelled();
  };
  const auto result = execute(":1.10", plan_id);
  ASSERT_TRUE(answer.has_value());
  EXPECT_TRUE(answer->has_value());
  EXPECT_EQ(control.calls, 1);
  EXPECT_EQ(result->outcome, ExecutionOutcome::Cancelled);
  EXPECT_EQ(terminator.calls(), 1);
}

TEST_F(PlanServiceTest, CancelPastThePointOfNoReturnIsReportedAndTheRunContinues) {
  const PlanId plan_id = preparedId();
  RecordingControl control;
  control.decision = CancelDecision::PastPointOfNoReturn;
  std::optional<std::expected<void, TransactionFailure>> answer;
  engine.executeScript = [&](const auto&, const auto&, const ExecutionContext& context) {
    context.control_changed(&control);
    answer = service.cancel(":1.10", plan_id);
    context.control_changed(nullptr);
    return ExecutionResult::succeeded();
  };
  EXPECT_EQ(execute(":1.10", plan_id)->outcome, ExecutionOutcome::Succeeded);
  ASSERT_TRUE(answer.has_value());
  ASSERT_FALSE(answer->has_value());
  EXPECT_EQ(answer->error().code, TransactionErrorCode::PastPointOfNoReturn);
  EXPECT_EQ(terminator.calls(), 0);  // the run continues; the helper must not end
}

TEST_F(PlanServiceTest, CancelAfterControlRetirementPreservesTheExecutionResult) {
  const PlanId plan_id = preparedId();
  RecordingControl control;
  engine.executeScript = [&](const auto&, const auto&, const ExecutionContext& context) {
    context.control_changed(&control);
    context.control_changed(nullptr);
    return ExecutionResult::succeeded();
  };
  runner.deferred = true;
  std::optional<ExecutionResult> result;
  service.execute(":1.10", plan_id, [&](ExecutionResult value) { result = std::move(value); });
  ASSERT_TRUE(runner.work_);
  auto work = std::move(runner.work_);
  work();
  EXPECT_FALSE(result.has_value());
  const auto answer = service.cancel(":1.10", plan_id);
  ASSERT_FALSE(answer.has_value());
  EXPECT_EQ(answer.error().code, TransactionErrorCode::PastPointOfNoReturn);
  EXPECT_EQ(terminator.calls(), 0);
  runner.runPending();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, ExecutionOutcome::Succeeded);
}

TEST_F(PlanServiceTest, ACancelBeforeTheControlIsPublishedIsAppliedWhenItAppears) {
  const PlanId plan_id = preparedId();
  RecordingControl control;
  engine.executeScript = [&](const auto&, const auto&, const ExecutionContext& context) {
    EXPECT_TRUE(service.cancel(":1.10", plan_id).has_value());  // nothing to ask yet
    EXPECT_EQ(control.calls, 0);
    context.control_changed(&control);
    EXPECT_EQ(control.calls, 1);
    return ExecutionResult::cancelled();
  };
  EXPECT_EQ(execute(":1.10", plan_id)->outcome, ExecutionOutcome::Cancelled);
  EXPECT_EQ(terminator.calls(), 1);
}

// ---- cancel ends the helper after the reply (REQ-F-056) ----

TEST_F(PlanServiceTest, ACancelWhileTheWorkRunsRepliesCancelledFirstThenTerminatesOnce) {
  const PlanId plan_id = preparedId();
  runner.deferred = true;
  std::vector<std::string> events;
  std::optional<ExecutionResult> reply;
  service.execute(":1.10", plan_id, [&](ExecutionResult outcome) {
    events.emplace_back("reply");
    reply = std::move(outcome);
  });
  terminator.onTerminate = [&events] { events.emplace_back("terminate"); };
  ASSERT_TRUE(service.slotBusy());

  EXPECT_TRUE(service.cancel(":1.10", plan_id).has_value());
  EXPECT_EQ(events, (std::vector<std::string>{"reply", "terminate"}));
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->outcome, ExecutionOutcome::Cancelled);
  EXPECT_EQ(terminator.calls(), 1);
  EXPECT_EQ(service.planCount(), 0U);  // consumed
  EXPECT_EQ(service.cancel(":1.10", plan_id).error().code, TransactionErrorCode::PlanNotFound);
  EXPECT_EQ(terminator.calls(), 1);

  // The stalled worker finally returns (in production the process is gone): its result changes nothing.
  RecordingControl control;
  engine.executeScript = [&control](const auto&, const auto&, const ExecutionContext& context) {
    context.control_changed(&control);  // sees the cancel that arrived before it published
    context.control_changed(nullptr);
    return ExecutionResult::succeeded();
  };
  runner.runPending();
  EXPECT_EQ(control.calls, 1);
  EXPECT_EQ(events.size(), 2U);  // no second reply
  EXPECT_TRUE(service.slotBusy());
}

TEST_F(PlanServiceTest, ADisconnectWhileTheWorkRunsIsTheSameCancel) {
  const PlanId plan_id = preparedId();
  runner.deferred = true;
  std::optional<ExecutionResult> reply;
  service.execute(":1.10", plan_id, [&reply](ExecutionResult outcome) { reply = std::move(outcome); });
  service.callerGone(":1.10");
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->outcome, ExecutionOutcome::Cancelled);
  EXPECT_EQ(terminator.calls(), 1);
  runner.runPending();
}

TEST_F(PlanServiceTest, AFirstCancelWinsAndASecondOneDoesNotTerminateAgain) {
  const PlanId plan_id = preparedId();
  runner.deferred = true;
  service.execute(":1.10", plan_id, [](const ExecutionResult&) {});
  EXPECT_TRUE(service.cancel(":1.10", plan_id).has_value());
  service.callerGone(":1.10");
  EXPECT_EQ(terminator.calls(), 1);
  runner.runPending();
}

TEST_F(PlanServiceTest, ShutdownIsImmediateWhenIdleAndCancelsAnAuthorization) {
  EXPECT_TRUE(service.shutdownRequested());
  authorizer.defer = true;
  const PlanId plan_id = preparedId();
  std::optional<ExecutionResult> result;
  service.execute(":1.10", plan_id, [&result](ExecutionResult outcome) { result = std::move(outcome); });
  EXPECT_TRUE(service.shutdownRequested());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, ExecutionOutcome::Cancelled);
  EXPECT_EQ(terminator.calls(), 0);
}

TEST_F(PlanServiceTest, ShutdownBeforeTheCommitPointCancelsTheRunAndAfterItWaits) {
  const PlanId early = preparedId();
  runner.deferred = true;
  std::optional<ExecutionResult> first;
  service.execute(":1.10", early, [&first](ExecutionResult outcome) { first = std::move(outcome); });
  EXPECT_FALSE(service.shutdownRequested());  // the worker has not returned yet
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->outcome, ExecutionOutcome::Cancelled);
  EXPECT_EQ(terminator.calls(), 1);
  runner.runPending();
}

TEST_F(PlanServiceTest, ShutdownAfterTheCommitPointWaitsForCompletion) {
  const PlanId late = preparedId();
  RecordingControl control;
  control.decision = CancelDecision::PastPointOfNoReturn;
  bool answered = false;
  engine.executeScript = [&](const auto&, const auto&, const ExecutionContext& context) {
    context.control_changed(&control);
    answered = !service.shutdownRequested();  // past the point of no return: the run is not cut short
    context.control_changed(nullptr);
    return ExecutionResult::succeeded();
  };
  EXPECT_EQ(execute(":1.10", late)->outcome, ExecutionOutcome::Succeeded);
  EXPECT_TRUE(answered);
  EXPECT_EQ(terminator.calls(), 0);
}

TEST_F(PlanServiceTest, TheActivityObserverFiresWhenStoreOrSlotChange) {
  int notifications = 0;
  service.setActivityObserver([&notifications] { ++notifications; });
  const PlanId plan_id = preparedId();
  EXPECT_GT(notifications, 0);
  const int afterPrepare = notifications;
  EXPECT_TRUE(service.hasActivity());
  ASSERT_TRUE(execute(":1.10", plan_id).has_value());
  EXPECT_GT(notifications, afterPrepare);
  EXPECT_FALSE(service.hasActivity());
}

}  // namespace
}  // namespace holonight_packages_application
