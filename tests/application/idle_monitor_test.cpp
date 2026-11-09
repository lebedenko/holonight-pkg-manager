#include "holonight_packages_application/idle_monitor.h"

#include "fake_authorizer.h"
#include "fake_caller_identity.h"
#include "fake_clock.h"
#include "fake_plan_id_generator.h"
#include "fake_transaction_engine.h"
#include "holonight_packages_application/plan_service.h"
#include "manual_work_runner.h"
#include "recording_terminator.h"

#include <chrono>
#include <gtest/gtest.h>

namespace holonight_packages_application {
namespace {

using holonight_packages_testing::FakeAuthorizer;
using holonight_packages_testing::FakeCallerIdentity;
using holonight_packages_testing::FakeClock;
using holonight_packages_testing::FakePlanIdGenerator;
using holonight_packages_testing::FakeTransactionEngine;
using holonight_packages_testing::ManualWorkRunner;
using holonight_packages_testing::RecordingTerminator;
using namespace std::chrono_literals;

class IdleMonitorTest : public ::testing::Test {
 public:
  IdleMonitorTest()
      : service(PlanService::Dependencies{
            .engine = engine,
            .authorizer = authorizer,
            .identities = identities,
            .ids = ids,
            .clock = clock,
            .runner = runner,
            .terminator = terminator,
        }),
        idleTimer(clock.makeTimer()),
        expiryTimer(clock.makeTimer()),
        monitor(clock, *idleTimer, *expiryTimer,
                IdleMonitor::Hooks{
                    .has_activity = [this] { return service.hasActivity(); },
                    .next_expiry = [this] { return service.nextExpiry(); },
                    .sweep_expired = [this] { service.sweepExpired(); },
                    .on_idle = [this] { ++idleExits; },
                }) {
    identities.set(":1.10", 1000);
    service.setActivityObserver([this] { monitor.evaluate(); });
    monitor.evaluate();
  }

  PlanId prepare() {
    PlanId plan_id;
    service.prepare(":1.10", "install", {"app"}, {}, [&plan_id](auto result) { plan_id = result->id; });
    return plan_id;
  }

  FakeClock clock;
  FakeTransactionEngine engine;
  FakeAuthorizer authorizer;
  FakeCallerIdentity identities;
  FakePlanIdGenerator ids;
  ManualWorkRunner runner;
  RecordingTerminator terminator;
  PlanService service;
  std::unique_ptr<holonight_packages_testing::FakeTimer> idleTimer;
  std::unique_ptr<holonight_packages_testing::FakeTimer> expiryTimer;
  IdleMonitor monitor;
  int idleExits = 0;
};

TEST_F(IdleMonitorTest, AnIdleHelperExitsAtSixtySecondsNotBefore) {
  clock.advance(59s);
  EXPECT_EQ(idleExits, 0);
  clock.advance(1s);
  EXPECT_EQ(idleExits, 1);
}

TEST_F(IdleMonitorTest, APendingPlanKeepsTheHelperAliveAndTheIdlePeriodStartsAfterItExpires) {
  clock.advance(30s);
  prepare();
  clock.advance(299s);  // the plan is 299 s old: still pending
  EXPECT_EQ(idleExits, 0);
  clock.advance(2s);  // 301 s: swept by the expiry timer
  EXPECT_EQ(service.planCount(), 0U);
  EXPECT_EQ(idleExits, 0);
  clock.advance(58s);
  EXPECT_EQ(idleExits, 0);
  clock.advance(2s);
  EXPECT_EQ(idleExits, 1);
}

TEST_F(IdleMonitorTest, ActivityRestartsTheIdleCountdown) {
  clock.advance(50s);
  const PlanId plan_id = prepare();
  ASSERT_TRUE(service.cancel(":1.10", plan_id).has_value());
  clock.advance(59s);
  EXPECT_EQ(idleExits, 0);
  clock.advance(2s);
  EXPECT_EQ(idleExits, 1);
}

TEST_F(IdleMonitorTest, ARunningTransactionKeepsTheHelperAlive) {
  const PlanId plan_id = prepare();
  runner.deferred = true;
  service.execute(":1.10", plan_id, [](const auto&) {});
  clock.advance(10min);
  EXPECT_EQ(idleExits, 0);
  runner.runPending();
  clock.advance(59s);
  EXPECT_EQ(idleExits, 0);
  clock.advance(2s);
  EXPECT_EQ(idleExits, 1);
}

}  // namespace
}  // namespace holonight_packages_application
