#include "holonight_packages_application/plan_store.h"

#include "fake_clock.h"
#include "fake_plan_id_generator.h"

#include <chrono>
#include <gtest/gtest.h>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlanRequest;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_testing::FakeClock;
using holonight_packages_testing::FakePlanIdGenerator;
using namespace std::chrono_literals;

class PlanStoreTest : public ::testing::Test {
 public:
  PlanStore store{clock, generator};
  PlanId add(const CallerIdentity& owner) {
    const auto plan_id = store.add(owner, PlanRequest{.action = PlanAction::Install, .names = {"app"}}, ResolvedPlan{});
    EXPECT_TRUE(plan_id.has_value());
    return plan_id.value_or(PlanId());
  }

  FakeClock clock;
  FakePlanIdGenerator generator;
  const CallerIdentity alice{.unique_name = ":1.10", .uid = 1000};
  const CallerIdentity bob{.unique_name = ":1.11", .uid = 1001};
};

TEST_F(PlanStoreTest, FindsAPlanForItsOwnerOnly) {
  const PlanId plan_id = add(alice);
  EXPECT_EQ(store.find(plan_id, alice).kind, PlanLookup::Kind::Found);
  EXPECT_EQ(store.find(plan_id, bob).kind, PlanLookup::Kind::NotFound);
  EXPECT_EQ(store.find(plan_id, CallerIdentity{.unique_name = ":1.10", .uid = 1001}).kind,
            PlanLookup::Kind::NotFound);  // same name, other uid
  EXPECT_EQ(store.find(plan_id, CallerIdentity{.unique_name = ":1.99", .uid = 1000}).kind,
            PlanLookup::Kind::NotFound);  // same uid, other name
}

TEST_F(PlanStoreTest, UnknownAndForeignPlansAreIndistinguishable) {
  const PlanId plan_id = add(alice);
  const PlanLookup foreign = store.find(plan_id, bob);
  const PlanLookup unknown = store.find("0000000000000000000000000000ffff", bob);
  EXPECT_EQ(foreign.kind, unknown.kind);
  EXPECT_EQ(foreign.plan, unknown.plan);
  EXPECT_NE(store.peek(plan_id), nullptr);  // the foreign probe did not touch it
}

TEST_F(PlanStoreTest, AgeBoundaryIsExactlyThreeHundredSecondsInclusive) {
  const PlanId plan_id = add(alice);
  clock.advance(299s);
  EXPECT_EQ(store.find(plan_id, alice).kind, PlanLookup::Kind::Found);
  clock.advance(1s);  // exactly 300 s
  EXPECT_EQ(store.find(plan_id, alice).kind, PlanLookup::Kind::Found);
  clock.advance(1s);  // 301 s
  EXPECT_EQ(store.find(plan_id, alice).kind, PlanLookup::Kind::Expired);
}

TEST_F(PlanStoreTest, AnExpiredPlanIsDiscardedAndThenReadsAsNotFound) {
  const PlanId plan_id = add(alice);
  clock.advance(301s);
  EXPECT_EQ(store.find(plan_id, alice).kind, PlanLookup::Kind::Expired);
  EXPECT_EQ(store.find(plan_id, alice).kind, PlanLookup::Kind::NotFound);
  EXPECT_EQ(store.size(), 0U);
}

TEST_F(PlanStoreTest, ConsumedPlansReadAsNotFound) {
  const PlanId plan_id = add(alice);
  store.consume(plan_id);
  EXPECT_EQ(store.find(plan_id, alice).kind, PlanLookup::Kind::NotFound);
  EXPECT_FALSE(store.hasActivity());
}

TEST_F(PlanStoreTest, OnlyPendingPlansExpireSoARunningExecuteIsNeverCutOff) {
  const PlanId plan_id = add(alice);
  store.setState(plan_id, PlanState::Running);
  clock.advance(1h);
  EXPECT_EQ(store.find(plan_id, alice).kind, PlanLookup::Kind::Found);
  EXPECT_EQ(store.sweepExpired(), 0U);
}

TEST_F(PlanStoreTest, SixteenPlansPerUidAndSixtyFourInTotalThenNoRoom) {
  for (int i = 0; i < 16; ++i) {
    add(CallerIdentity{.unique_name = ":1." + std::to_string(i), .uid = 2000});
  }
  EXPECT_FALSE(store.hasRoomFor(2000));
  EXPECT_FALSE(
      store.add(CallerIdentity{.unique_name = ":1.50", .uid = 2000}, PlanRequest{}, ResolvedPlan{}).has_value());
  EXPECT_TRUE(store.hasRoomFor(2001));
  for (std::uint32_t uid = 3000; uid < 3003; ++uid) {
    for (int i = 0; i < 16; ++i) {
      add(CallerIdentity{.unique_name = ":2." + std::to_string(uid) + "." + std::to_string(i), .uid = uid});
    }
  }
  EXPECT_EQ(store.size(), 64U);
  EXPECT_FALSE(store.hasRoomFor(4000));
}

TEST_F(PlanStoreTest, ExpiredPlansDoNotCountTowardsTheLimits) {
  for (int i = 0; i < 16; ++i) {
    add(alice);
  }
  EXPECT_FALSE(store.hasRoomFor(alice.uid));
  clock.advance(301s);  // not swept yet
  EXPECT_TRUE(store.hasRoomFor(alice.uid));
  EXPECT_TRUE(store.add(alice, PlanRequest{}, ResolvedPlan{}).has_value());
}

TEST_F(PlanStoreTest, DisconnectDiscardsPendingPlansOfThatNameOnly) {
  const PlanId pending = add(alice);
  const PlanId running = add(alice);
  const PlanId other = add(bob);
  store.setState(running, PlanState::Running);
  EXPECT_EQ(store.discardPendingFor(alice.unique_name), 1U);
  EXPECT_EQ(store.peek(pending), nullptr);
  EXPECT_NE(store.peek(running), nullptr);
  EXPECT_NE(store.peek(other), nullptr);
}

TEST_F(PlanStoreTest, SweepAndNextExpiryFollowThePendingPlans) {
  EXPECT_FALSE(store.nextExpiry().has_value());
  const auto start = clock.now();
  add(alice);
  clock.advance(100s);
  add(bob);
  EXPECT_EQ(store.nextExpiry(), start + 300s);
  clock.advance(201s);  // the first is 301 s old, the second 201 s
  EXPECT_EQ(store.sweepExpired(), 1U);
  EXPECT_EQ(store.size(), 1U);
  EXPECT_EQ(store.nextExpiry(), start + 400s);
}

TEST_F(PlanStoreTest, ActivityMeansAnyPlanAtAll) {
  EXPECT_FALSE(store.hasActivity());
  const PlanId plan_id = add(alice);
  EXPECT_TRUE(store.hasActivity());
  store.setState(plan_id, PlanState::Authorizing);
  EXPECT_TRUE(store.hasActivity());
  store.consume(plan_id);
  EXPECT_FALSE(store.hasActivity());
}

TEST_F(PlanStoreTest, IdsAreUniqueAndRetryOnCollision) {
  struct RepeatingGenerator final : PlanIdGenerator {
    PlanId next() override {
      return calls++ < 2 ? "00000000000000000000000000000001" : "00000000000000000000000000000002";
    }
    int calls = 0;
  } repeating;
  PlanStore local(clock, repeating);
  const auto first = local.add(alice, PlanRequest{}, ResolvedPlan{});
  const auto second = local.add(alice, PlanRequest{}, ResolvedPlan{});
  ASSERT_TRUE(first && second);
  EXPECT_NE(*first, *second);
}

}  // namespace
}  // namespace holonight_packages_application
