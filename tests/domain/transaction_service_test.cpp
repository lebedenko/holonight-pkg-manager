#include "holonight_packages_domain/transaction_service.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using namespace holonight_packages_domain;

// A double that implements only the port: it proves the header needs no transport type (REQ-F-038).
class PortOnlyDouble final : public TransactionService {
 public:
  void setProgressHandler(ProgressHandler handler) override { handler_ = std::move(handler); }
  void preparePlan(const PlanRequest& request, PrepareDone done) override {
    prepared.push_back(request.names);
    done(std::unexpected(ServiceError{.kind = ServiceError::Kind::HelperUnavailable, .failure = {}}));
  }
  void execute(const std::string& plan_id, ExecuteDone done) override {
    executed.push_back(plan_id);
    done(ExecutionResult::cancelled());
  }
  void cancel(const std::string& plan_id, CancelDone done) override {
    cancelled.push_back(plan_id);
    done({});
  }
  void queryProtocolVersion(ProbeMode mode, VersionDone done) override {
    probes.push_back(mode);
    done(std::uint32_t{2});
  }
  void emit(const std::string& plan_id, const ProgressUpdate& update) const { handler_(plan_id, update); }
  std::vector<ProbeMode> probes;

  std::vector<std::vector<std::string>> prepared;
  std::vector<std::string> executed;
  std::vector<std::string> cancelled;

  ProgressHandler handler_;
};

TEST(TransactionServicePort, DoubleDrivesAllFourCalls) {
  PortOnlyDouble service;
  bool unavailable = false;
  service.preparePlan(PlanRequest{.action = PlanAction::Install, .names = {"app"}}, [&](PrepareReply reply) {
    unavailable = !reply && reply.error().kind == ServiceError::Kind::HelperUnavailable;
  });
  EXPECT_TRUE(unavailable);

  ExecutionOutcome outcome = ExecutionOutcome::Succeeded;
  service.execute("plan-1", [&](ExecuteReply reply) { outcome = reply->outcome; });
  EXPECT_EQ(outcome, ExecutionOutcome::Cancelled);

  bool cancel_ok = false;
  service.cancel("plan-1", [&](CancelReply reply) { cancel_ok = reply.has_value(); });
  EXPECT_TRUE(cancel_ok);

  std::vector<ProgressUpdate> seen;
  service.setProgressHandler([&](const std::string&, const ProgressUpdate& update) { seen.push_back(update); });
  service.emit("plan-1", ProgressUpdate{.phase = ProgressPhase::Downloading, .current = 3, .total = 10, .item = "foo"});
  ASSERT_EQ(seen.size(), 1U);
  EXPECT_EQ(seen[0].item, "foo");
  EXPECT_EQ(service.prepared.at(0), std::vector<std::string>{"app"});
  EXPECT_EQ(service.executed, std::vector<std::string>{"plan-1"});
}

TEST(TransactionServiceParse, EveryProgressPhaseRoundTrips) {
  for (const auto phase : {
           ProgressPhase::Resolving,
           ProgressPhase::Downloading,
           ProgressPhase::Verifying,
           ProgressPhase::Hooks,
           ProgressPhase::Installing,
           ProgressPhase::Removing,
           ProgressPhase::Done,
       }) {
    EXPECT_EQ(parseProgressPhase(progressPhaseName(phase)), phase);
  }
  EXPECT_FALSE(parseProgressPhase("Bogus").has_value());
  EXPECT_FALSE(parseProgressPhase("").has_value());
}

TEST(TransactionServiceParse, EveryOutcomeActionAndWarningRoundTrips) {
  for (const auto outcome : {ExecutionOutcome::Succeeded, ExecutionOutcome::Failed, ExecutionOutcome::Cancelled}) {
    EXPECT_EQ(parseExecutionOutcome(executionOutcomeName(outcome)), outcome);
  }
  EXPECT_FALSE(parseExecutionOutcome("succeeded").has_value());
  for (const auto action : {ChangeAction::Install, ChangeAction::Remove}) {
    EXPECT_EQ(parseChangeAction(changeActionName(action)), action);
  }
  EXPECT_FALSE(parseChangeAction("upgrade").has_value());
  EXPECT_EQ(parseWarningToken("HookFailed"), WarningToken::HookFailed);
  EXPECT_FALSE(parseWarningToken("Other").has_value());
}

}  // namespace
