#include "fake_authorizer.h"
#include "fake_caller_identity.h"
#include "fake_plan_id_generator.h"
#include "fake_transaction_engine.h"
#include "holonight_packages_application/plan_id_generator.h"

#include <algorithm>
#include <cctype>
#include <gtest/gtest.h>
#include <set>

namespace holonight_packages_application {
namespace {

using holonight_packages_testing::FakeAuthorizer;
using holonight_packages_testing::FakeCallerIdentity;
using holonight_packages_testing::FakePlanIdGenerator;
using holonight_packages_testing::FakeTransactionEngine;

bool isLowerHex32(const std::string& plan_id) {
  return plan_id.size() == 32 && std::ranges::all_of(plan_id, [](unsigned char character) {
           return std::isxdigit(character) != 0 && std::islower(character) + std::isdigit(character) > 0;
         });
}

TEST(PlanIdGeneratorTest, SystemIdsAreThirtyTwoLowercaseHexCharactersAndDistinct) {
  SystemPlanIdGenerator generator;
  std::set<std::string> seen;
  for (int i = 0; i < 200; ++i) {
    const std::string plan_id = generator.next();
    EXPECT_TRUE(isLowerHex32(plan_id)) << plan_id;
    seen.insert(plan_id);
  }
  EXPECT_EQ(seen.size(), 200U);
}

TEST(PlanIdGeneratorTest, TheFakeProducesTheSameShapeInSequence) {
  FakePlanIdGenerator generator;
  const std::string first = generator.next();
  const std::string second = generator.next();
  EXPECT_TRUE(isLowerHex32(first));
  EXPECT_NE(first, second);
}

TEST(FakeAuthorizerTest, RecordsRequestsAnswersSynchronouslyAndRunsTheHookFirst) {
  FakeAuthorizer authorizer;
  std::vector<std::string> events;
  authorizer.beforeAnswer = [&events](const AuthorizationRequest&) { events.emplace_back("hook"); };
  authorizer.answer = AuthorizationDecision::NotAuthorized;
  AuthorizationRequest request;
  request.bus_name = ":1.7";
  request.action_id = "org.holonight.packages.install";
  authorizer.check(request, [&events](AuthorizationDecision verdict) {
    events.emplace_back(verdict == AuthorizationDecision::NotAuthorized ? "denied" : "other");
  });
  ASSERT_EQ(authorizer.requests().size(), 1U);
  EXPECT_EQ(authorizer.requests()[0], request);
  EXPECT_EQ(events, (std::vector<std::string>{"hook", "denied"}));
}

TEST(FakeAuthorizerTest, DeferredChecksWaitForDeliveryAndCancelDropsThem) {
  FakeAuthorizer authorizer;
  authorizer.defer = true;
  int answered = 0;
  authorizer.check({}, [&answered](AuthorizationDecision) { ++answered; });
  EXPECT_TRUE(authorizer.hasPending());
  EXPECT_EQ(answered, 0);
  EXPECT_TRUE(authorizer.deliver(AuthorizationDecision::Authorized));
  EXPECT_EQ(answered, 1);
  EXPECT_FALSE(authorizer.deliver(AuthorizationDecision::Authorized));

  authorizer.check({}, [&answered](AuthorizationDecision) { ++answered; });
  authorizer.cancel("plan-1");
  EXPECT_FALSE(authorizer.hasPending());
  EXPECT_EQ(authorizer.cancelled(), std::vector<std::string>{"plan-1"});
  EXPECT_EQ(answered, 1);
}

TEST(FakeCallerIdentityTest, ResolvesKnownNamesOnly) {
  FakeCallerIdentity identity;
  identity.set(":1.5", 1000);
  EXPECT_EQ(identity.uidOf(":1.5"), 1000U);
  EXPECT_FALSE(identity.uidOf(":1.6").has_value());
  identity.forget(":1.5");
  EXPECT_FALSE(identity.uidOf(":1.5").has_value());
}

TEST(FakeTransactionEngineTest, CountsCallsAndFollowsItsScript) {
  FakeTransactionEngine engine;
  holonight_packages_domain::PlanRequest request{
      .action = holonight_packages_domain::PlanAction::Install,
      .names = {"app"},
  };
  ASSERT_TRUE(engine.resolve(request).has_value());
  engine.resolveResult = std::unexpected(holonight_packages_domain::TransactionFailure{
      .code = holonight_packages_domain::TransactionErrorCode::PackageNotFound,
      .names = {"app"},
      .choices = {},
  });
  EXPECT_FALSE(engine.resolve(request).has_value());
  EXPECT_EQ(engine.resolveCalls(), 2);

  engine.executeScript = [](const auto&, const auto&, const holonight_packages_domain::ExecutionContext& context) {
    context.progress(holonight_packages_domain::ProgressUpdate{
        .phase = holonight_packages_domain::ProgressPhase::Done,
        .current = 1,
        .total = 1,
        .item = {},
    });
    return holonight_packages_domain::ExecutionResult::cancelled();
  };
  int updates = 0;
  holonight_packages_domain::ExecutionContext context;
  context.progress = [&updates](const holonight_packages_domain::ProgressUpdate&) { ++updates; };
  const auto result = engine.execute(request, {}, context);
  EXPECT_EQ(result.outcome, holonight_packages_domain::ExecutionOutcome::Cancelled);
  EXPECT_EQ(updates, 1);
  EXPECT_EQ(engine.executeCalls(), 1);
}

}  // namespace
}  // namespace holonight_packages_application
