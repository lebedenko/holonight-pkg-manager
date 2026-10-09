#include "holonight_packages_application/update_check_scheduler.h"

#include "fake_clock.h"
#include "fake_random.h"
#include "fake_snapshot_store.h"
#include "fake_update_checker.h"
#include "update_check_test_support.h"

#include <QSignalSpy>

#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::UpdateCheckError;
using holonight_packages_domain::UpdateCheckErrorCode;
using holonight_packages_testing::FakeClock;
using holonight_packages_testing::FakeSnapshotStore;
using holonight_packages_testing::FakeUpdateChecker;
using holonight_packages_testing::SeededRandom;
using holonight_packages_testing::waitIdle;
using std::chrono::hours;
using std::chrono::milliseconds;
using std::chrono::minutes;
using std::chrono::seconds;

class UpdateCheckSchedulerTest : public ::testing::Test {
 protected:
  void build(UpdateCheckPolicy policy = {}, bool capable = true) {
    policy_ = policy;
    service_ = std::make_unique<UpdateCheckService>(
        UpdateCheckService::Dependencies{
            .checker = checker_,
            .store = store_,
            .clock = clock_,
            .capabilities = {.canCheckForUpdates = capable},
        },
        policy);
    scheduler_ = std::make_unique<UpdateCheckScheduler>(*service_, clock_->makeTimer(),
                                                        std::make_shared<SeededRandom>(), policy);
    connection_ = QObject::connect(service_.get(), &UpdateCheckService::checkCompleted,
                                   [this](const UpdateCheckOutcome& outcome) { outcomes_.push_back(outcome); });
    start_ = clock_->now();
  }

  void alwaysFail(UpdateCheckErrorCode code = UpdateCheckErrorCode::NetworkUnavailable, int times = 100) {
    for (int index = 0; index < times; ++index) {
      checker_->enqueue(std::unexpected(UpdateCheckError{code}));
    }
  }

  // Runs the fake clock to start + elapsed, letting every triggered check finish before time moves on.
  void runFor(milliseconds elapsed) {
    const auto end = start_ + elapsed;
    while (true) {
      const auto due = clock_->nextDue();
      if (!due || *due > end) {
        break;
      }
      clock_->advance(std::max(milliseconds{0}, std::chrono::duration_cast<milliseconds>(*due - clock_->now())));
      ASSERT_TRUE(waitIdle(*service_));
      QTest::qWait(1);
    }
    clock_->advance(std::chrono::duration_cast<milliseconds>(end - clock_->now()));
  }

  std::vector<milliseconds> gaps() const {
    std::vector<milliseconds> result;
    auto previous = start_;
    for (const auto& outcome : outcomes_) {
      result.push_back(std::chrono::duration_cast<milliseconds>(outcome.completedAt - previous));
      previous = outcome.completedAt;
    }
    return result;
  }

  UpdateCheckPolicy policy_;
  std::shared_ptr<FakeUpdateChecker> checker_ = std::make_shared<FakeUpdateChecker>();
  std::shared_ptr<FakeSnapshotStore> store_ = std::make_shared<FakeSnapshotStore>();
  std::shared_ptr<FakeClock> clock_ = std::make_shared<FakeClock>();
  std::unique_ptr<UpdateCheckService> service_;
  std::unique_ptr<UpdateCheckScheduler> scheduler_;
  QMetaObject::Connection connection_;
  std::vector<UpdateCheckOutcome> outcomes_;
  std::chrono::system_clock::time_point start_;
};

// ---- T-012 ------------------------------------------------------------------------------------------------------

TEST_F(UpdateCheckSchedulerTest, NoCheckBeforeStartupDelayThenExactlyOneAtIt) {
  build();
  scheduler_->start();
  clock_->advance(seconds{59});
  QTest::qWait(20);
  EXPECT_EQ(checker_->callCount(), 0);
  clock_->advance(seconds{1});
  ASSERT_TRUE(waitIdle(*service_));
  EXPECT_EQ(checker_->callCount(), 1);
}

TEST_F(UpdateCheckSchedulerTest, ConfiguredTwoHourIntervalChecksEveryTwoHoursWithinJitter) {
  UpdateCheckPolicy policy;
  policy.interval = minutes{120};
  build(policy);
  scheduler_->start();
  runFor(hours{12});

  ASSERT_GE(outcomes_.size(), 5U);
  const auto all = gaps();
  for (std::size_t index = 1; index < all.size(); ++index) {
    EXPECT_GE(all[index], milliseconds{minutes{120}} - milliseconds{minutes{10}});
    EXPECT_LE(all[index], milliseconds{minutes{120}} + milliseconds{minutes{10}});
  }
}

TEST_F(UpdateCheckSchedulerTest, OnDemandFailureLeavesCounterAndPendingTimerUnchanged) {
  build();
  scheduler_->start();
  const auto before = clock_->nextDue();
  alwaysFail(UpdateCheckErrorCode::NetworkUnavailable, 1);

  service_->requestCheck(CheckOrigin::OnDemand);
  ASSERT_TRUE(waitIdle(*service_));
  QTest::qWait(5);

  EXPECT_EQ(clock_->nextDue(), before);
  ASSERT_EQ(outcomes_.size(), 1U);
  EXPECT_FALSE(outcomes_[0].succeeded);
}

TEST_F(UpdateCheckSchedulerTest, OnDemandSuccessRearmsTheTimerToIntervalAndResetsFailures) {
  build();
  scheduler_->start();
  alwaysFail(UpdateCheckErrorCode::NetworkUnavailable, 1);
  runFor(seconds{60});  // automatic failure -> 5 min backoff armed
  ASSERT_EQ(outcomes_.size(), 1U);

  clock_->advance(seconds{11});
  service_->requestCheck(CheckOrigin::OnDemand);
  ASSERT_TRUE(waitIdle(*service_));
  QTest::qWait(5);
  ASSERT_EQ(outcomes_.size(), 2U);
  ASSERT_TRUE(outcomes_[1].succeeded);

  const auto due = clock_->nextDue();
  ASSERT_TRUE(due.has_value());
  const auto delay = std::chrono::duration_cast<milliseconds>(*due - clock_->now());
  EXPECT_GE(delay, milliseconds{minutes{350}});
  EXPECT_LE(delay, milliseconds{minutes{370}});
}

TEST_F(UpdateCheckSchedulerTest, BackendWithoutCapabilityNeverSchedules) {
  build({}, false);
  scheduler_->start();
  EXPECT_FALSE(clock_->nextDue().has_value());
}

// ---- T-013 ------------------------------------------------------------------------------------------------------

TEST_F(UpdateCheckSchedulerTest, AlwaysFailingCheckerBacksOff5Then15Then60MinutesThenSixHours) {
  build();
  alwaysFail();
  scheduler_->start();
  runFor(hours{20});

  const auto all = gaps();
  ASSERT_GE(all.size(), 5U);
  EXPECT_EQ(all[0], milliseconds{seconds{60}});
  EXPECT_EQ(all[1], milliseconds{minutes{5}});
  EXPECT_EQ(all[2], milliseconds{minutes{15}});
  EXPECT_EQ(all[3], milliseconds{minutes{60}});
  EXPECT_GE(all[4], milliseconds{minutes{350}});
  EXPECT_LE(all[4], milliseconds{minutes{370}});
}

TEST_F(UpdateCheckSchedulerTest, SuccessResetsTheFailureCounter) {
  build();
  alwaysFail(UpdateCheckErrorCode::NetworkUnavailable, 2);
  checker_->enqueue(holonight_packages_domain::UpdateSnapshot{});
  alwaysFail(UpdateCheckErrorCode::NetworkUnavailable, 1);
  scheduler_->start();
  runFor(minutes{200});

  // 60 s start; fail -> 5 min; fail -> 15 min; success -> a full interval (not reached within 200 min).
  const auto all = gaps();
  ASSERT_EQ(all.size(), 3U);
  EXPECT_EQ(all[1], milliseconds{minutes{5}});
  EXPECT_EQ(all[2], milliseconds{minutes{15}});
  EXPECT_TRUE(outcomes_[2].succeeded);
  const auto due = clock_->nextDue();
  ASSERT_TRUE(due.has_value());
  const auto delay = std::chrono::duration_cast<milliseconds>(*due - outcomes_[2].completedAt);
  EXPECT_GE(delay, milliseconds{minutes{350}});
}

TEST_F(UpdateCheckSchedulerTest, FailSuccessFailGivesFiveMinuteGap) {
  UpdateCheckPolicy policy;
  policy.interval = minutes{15};
  build(policy);
  alwaysFail(UpdateCheckErrorCode::NetworkUnavailable, 2);
  checker_->enqueue(holonight_packages_domain::UpdateSnapshot{});
  alwaysFail(UpdateCheckErrorCode::NetworkUnavailable, 1);
  scheduler_->start();
  runFor(minutes{60});

  ASSERT_GE(outcomes_.size(), 5U);
  EXPECT_FALSE(outcomes_[0].succeeded);
  EXPECT_FALSE(outcomes_[1].succeeded);
  EXPECT_TRUE(outcomes_[2].succeeded);
  EXPECT_FALSE(outcomes_[3].succeeded);
  EXPECT_EQ(gaps()[4], milliseconds{minutes{5}});
}

TEST_F(UpdateCheckSchedulerTest, BusyFollowsTheSameBackoffAsAnyFailure) {
  build();
  alwaysFail(UpdateCheckErrorCode::Busy);
  scheduler_->start();
  runFor(hours{3});

  const auto all = gaps();
  ASSERT_GE(all.size(), 4U);
  EXPECT_EQ(all[1], milliseconds{minutes{5}});
  EXPECT_EQ(all[2], milliseconds{minutes{15}});
  EXPECT_EQ(all[3], milliseconds{minutes{60}});
}

TEST_F(UpdateCheckSchedulerTest, AlwaysFailingDayRecordsAtMostEightAutomaticAttempts) {
  build();
  alwaysFail();
  scheduler_->start();
  runFor(hours{24});
  EXPECT_LE(outcomes_.size(), 8U);
  EXPECT_GE(outcomes_.size(), 6U);
}

}  // namespace
}  // namespace holonight_packages_application
