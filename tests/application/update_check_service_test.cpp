#include "holonight_packages_application/update_check_service.h"

#include "fake_clock.h"
#include "fake_snapshot_store.h"
#include "fake_update_checker.h"
#include "update_check_test_support.h"

#include <QSignalSpy>
#include <QtLogging>

#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::CheckedSnapshot;
using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateCheckError;
using holonight_packages_domain::UpdateCheckErrorCode;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_testing::FakeClock;
using holonight_packages_testing::FakeSnapshotStore;
using holonight_packages_testing::FakeUpdateChecker;
using holonight_packages_testing::waitIdle;
using std::chrono::hours;
using std::chrono::seconds;

UpdateSnapshot snapshotWithCount(int count) {
  UpdateSnapshot snapshot;
  for (int index = 0; index < count; ++index) {
    snapshot.updates.push_back(PendingUpdate{
        .name = "pkg-" + std::to_string(index),
        .installedVersion = "1",
        .availableVersion = "2",
        .downloadSizeBytes = 100,
    });
  }
  return snapshot;
}

std::unexpected<UpdateCheckError> failure(UpdateCheckErrorCode code) { return std::unexpected(UpdateCheckError{code}); }

class UpdateCheckServiceTest : public ::testing::Test {
 protected:
  void SetUp() override { make(); }

  void make(bool capable = true) {
    service_.reset();
    service_ = std::make_unique<UpdateCheckService>(
        UpdateCheckService::Dependencies{
            .checker = checker_,
            .store = store_,
            .clock = clock_,
            .capabilities = {.canCheckForUpdates = capable},
        },
        UpdateCheckPolicy{});
  }

  void runCheck(CheckOrigin origin = CheckOrigin::Automatic) {
    service_->requestCheck(origin);
    ASSERT_TRUE(waitIdle(*service_));
  }

  std::shared_ptr<FakeUpdateChecker> checker_ = std::make_shared<FakeUpdateChecker>();
  std::shared_ptr<FakeSnapshotStore> store_ = std::make_shared<FakeSnapshotStore>();
  std::shared_ptr<FakeClock> clock_ = std::make_shared<FakeClock>();
  std::unique_ptr<UpdateCheckService> service_;
};

TEST_F(UpdateCheckServiceTest, ReentrantIdleObserverStartsADistinctRun) {
  checker_->enqueue(snapshotWithCount(1));
  checker_->enqueue(failure(UpdateCheckErrorCode::Busy));
  std::vector<bool> first;
  std::vector<bool> second;
  bool restarted = false;
  QObject::connect(
      service_.get(), &UpdateCheckService::statusChanged, service_.get(), [&](const UpdateCheckStatus& status) {
        if (!status.checking && !restarted) {
          restarted = true;
          service_->requestCheck(CheckOrigin::Automatic,
                                 [&](const UpdateCheckOutcome& outcome) { second.push_back(outcome.succeeded); });
        }
      });
  service_->requestCheck(CheckOrigin::Automatic,
                         [&](const UpdateCheckOutcome& outcome) { first.push_back(outcome.succeeded); });
  ASSERT_TRUE(waitIdle(*service_));
  EXPECT_EQ(first, std::vector<bool>{true});
  EXPECT_EQ(second, std::vector<bool>{false});
}

// ---- T-009: status, last-good, success and failure --------------------------------------------------------------

TEST_F(UpdateCheckServiceTest, SuccessReplacesSnapshotAndEmitsOneCheckCompleted) {
  checker_->enqueue(snapshotWithCount(2));
  QSignalSpy completed(service_.get(), &UpdateCheckService::checkCompleted);

  runCheck();

  EXPECT_EQ(completed.count(), 1);
  ASSERT_TRUE(service_->status().count.has_value());
  EXPECT_EQ(*service_->status().count, 2);
  ASSERT_TRUE(service_->lastGood().has_value());
  EXPECT_EQ(service_->lastGood()->snapshot.updates.size(), 2U);
  EXPECT_TRUE(service_->status().lastCheckSucceeded);
  EXPECT_FALSE(service_->status().lastError.has_value());
}

TEST_F(UpdateCheckServiceTest, FailureKeepsLastGoodSnapshot) {
  checker_->enqueue(snapshotWithCount(3));
  runCheck();
  const auto before = service_->lastGood();
  clock_->advance(hours{1});

  checker_->enqueue(failure(UpdateCheckErrorCode::NetworkUnavailable));
  runCheck();

  EXPECT_EQ(*service_->status().count, 3);
  EXPECT_EQ(service_->lastGood(), before);
  EXPECT_FALSE(service_->status().lastCheckSucceeded);
  EXPECT_EQ(service_->status().lastError, UpdateCheckErrorCode::NetworkUnavailable);
}

TEST_F(UpdateCheckServiceTest, NextSuccessClearsTheError) {
  checker_->enqueue(failure(UpdateCheckErrorCode::Busy));
  runCheck();
  EXPECT_EQ(service_->status().lastError, UpdateCheckErrorCode::Busy);
  clock_->advance(hours{1});
  checker_->enqueue(snapshotWithCount(1));
  runCheck();
  EXPECT_FALSE(service_->status().lastError.has_value());
  EXPECT_TRUE(service_->status().lastCheckSucceeded);
}

TEST_F(UpdateCheckServiceTest, SnapshotFetchedAtKeepsTheLastSuccessWhileLastCheckTimeAdvances) {
  const auto start = clock_->now();
  checker_->enqueue(snapshotWithCount(1));
  runCheck();
  clock_->advance(hours{3});
  checker_->enqueue(failure(UpdateCheckErrorCode::Unknown));
  runCheck();

  EXPECT_EQ(service_->status().snapshotFetchedAt, start);
  EXPECT_EQ(service_->status().lastCheckTime, start + hours{3});
}

TEST_F(UpdateCheckServiceTest, EmptyStoreLeavesSnapshotFetchedAtUnset) {
  service_->start();
  EXPECT_FALSE(service_->status().snapshotFetchedAt.has_value());
  EXPECT_FALSE(service_->status().hasCheckResult);
  EXPECT_FALSE(service_->status().count.has_value());
}

TEST_F(UpdateCheckServiceTest, BackendWithoutCapabilityIgnoresRequests) {
  make(false);
  service_->requestCheck(CheckOrigin::OnDemand);
  QTest::qWait(30);
  EXPECT_EQ(checker_->callCount(), 0);
  EXPECT_FALSE(service_->canCheckForUpdates());
}

// ---- T-010: single flight, join, cooldown, pool, logging --------------------------------------------------------

TEST_F(UpdateCheckServiceTest, FiftyConcurrentRequestsNeverRunTwoChecksAtOnce) {
  std::vector<std::thread> threads;
  threads.reserve(50);
  for (int index = 0; index < 50; ++index) {
    threads.emplace_back([this] { service_->requestCheck(CheckOrigin::Automatic); });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  ASSERT_TRUE(holonight_packages_testing::pumpUntil([this] { return checker_->callCount() >= 1; }));
  ASSERT_TRUE(waitIdle(*service_));
  QTest::qWait(30);
  ASSERT_TRUE(waitIdle(*service_));
  EXPECT_EQ(checker_->maxConcurrency(), 1);
}

TEST_F(UpdateCheckServiceTest, RequestsWhileRunningJoinTheRunningCheck) {
  checker_->setBlocking(true);
  checker_->enqueue(snapshotWithCount(1));
  std::vector<UpdateCheckOutcome> seen;
  service_->requestCheck(CheckOrigin::Automatic,
                         [&seen](const UpdateCheckOutcome& outcome) { seen.push_back(outcome); });
  ASSERT_TRUE(holonight_packages_testing::pumpUntil([this] { return checker_->callCount() == 1; }));
  service_->requestCheck(CheckOrigin::Automatic,
                         [&seen](const UpdateCheckOutcome& outcome) { seen.push_back(outcome); });
  service_->requestCheck(CheckOrigin::OnDemand,
                         [&seen](const UpdateCheckOutcome& outcome) { seen.push_back(outcome); });
  QTest::qWait(30);
  EXPECT_EQ(checker_->callCount(), 1);

  checker_->release();
  ASSERT_TRUE(waitIdle(*service_));

  EXPECT_EQ(checker_->callCount(), 1);
  ASSERT_EQ(seen.size(), 3U);
  EXPECT_EQ(seen[0], seen[1]);
  EXPECT_EQ(seen[1], seen[2]);
}

TEST_F(UpdateCheckServiceTest, OnDemandCooldownIgnoresRequestsInsideTenSeconds) {
  runCheck();
  EXPECT_EQ(checker_->callCount(), 1);

  clock_->advance(seconds{9});
  service_->requestCheck(CheckOrigin::OnDemand);
  QTest::qWait(30);
  EXPECT_EQ(checker_->callCount(), 1);

  clock_->advance(seconds{2});
  runCheck(CheckOrigin::OnDemand);
  EXPECT_EQ(checker_->callCount(), 2);
}

TEST_F(UpdateCheckServiceTest, AutomaticRequestsIgnoreTheCooldown) {
  runCheck();
  runCheck();
  EXPECT_EQ(checker_->callCount(), 2);
}

TEST_F(UpdateCheckServiceTest, CheckerNeverRunsOnTheHomeThread) {
  runCheck();
  runCheck();
  for (const auto& caller : checker_->callerThreads()) {
    EXPECT_NE(caller, std::this_thread::get_id());
  }
  EXPECT_EQ(checker_->callerThreads().size(), 2U);
}

TEST_F(UpdateCheckServiceTest, LogsStartAndFinishWithOutcomeAndDuration) {
  static std::mutex mutex;
  static std::vector<std::string> lines;
  lines.clear();
  const auto previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
    const std::scoped_lock lock(mutex);
    lines.push_back(message.toStdString());
  });
  checker_->enqueue(failure(UpdateCheckErrorCode::RepositoryUnreachable));
  runCheck(CheckOrigin::OnDemand);
  qInstallMessageHandler(previous);

  bool started = false;
  bool finished = false;
  for (const std::string& line : lines) {
    started = started || (line.contains("started") && line.contains("on-demand"));
    finished = finished ||
               (line.contains("finished") && line.contains("repository-unreachable") && line.contains("duration_ms"));
  }
  EXPECT_TRUE(started);
  EXPECT_TRUE(finished);
}

// ---- T-011: persistence orchestration ---------------------------------------------------------------------------

TEST_F(UpdateCheckServiceTest, StartAdoptsValidStoredSnapshotBeforeAnyCheck) {
  const CheckedSnapshot stored{.snapshot = snapshotWithCount(4), .fetchedAt = clock_->now() - hours{2}};
  store_->setStored(stored);
  QSignalSpy adopted(service_.get(), &UpdateCheckService::snapshotAdopted);

  service_->start();

  ASSERT_EQ(adopted.count(), 1);
  EXPECT_EQ(checker_->callCount(), 0);
  EXPECT_EQ(*service_->status().count, 4);
  EXPECT_EQ(service_->status().snapshotFetchedAt, stored.fetchedAt);
  EXPECT_FALSE(service_->status().hasCheckResult);
}

TEST_F(UpdateCheckServiceTest, StartDiscardsAnInvalidStoredSnapshotExactlyOnce) {
  store_->setInvalid();
  service_->start();
  EXPECT_EQ(store_->discardCount(), 1);
  EXPECT_FALSE(service_->lastGood().has_value());
}

TEST_F(UpdateCheckServiceTest, StartWithValidOrAbsentSnapshotNeverDiscards) {
  service_->start();
  store_->setStored({.snapshot = snapshotWithCount(1), .fetchedAt = clock_->now()});
  service_->start();
  EXPECT_EQ(store_->discardCount(), 0);
}

TEST_F(UpdateCheckServiceTest, SuccessSavesAndFailureNeverSaves) {
  checker_->enqueue(failure(UpdateCheckErrorCode::NetworkUnavailable));
  runCheck();
  EXPECT_EQ(store_->saveCount(), 0);

  checker_->enqueue(snapshotWithCount(2));
  runCheck();
  EXPECT_EQ(store_->saveCount(), 1);
  ASSERT_TRUE(store_->stored().has_value());
  EXPECT_EQ(store_->stored()->snapshot.updates.size(), 2U);
}

TEST_F(UpdateCheckServiceTest, FailedSavePreservesPublishedResultAndLogsIt) {
  store_->failSaves(true);
  static std::mutex mutex;
  static std::vector<std::string> lines;
  lines.clear();
  const auto previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
    const std::scoped_lock lock(mutex);
    lines.push_back(message.toStdString());
  });
  checker_->enqueue(snapshotWithCount(5));
  runCheck();
  qInstallMessageHandler(previous);

  EXPECT_FALSE(service_->status().count.has_value());
  EXPECT_FALSE(service_->status().lastCheckSucceeded);
  bool logged = false;
  for (const std::string& line : lines) {
    logged = logged || line.contains("cannot save");
  }
  EXPECT_TRUE(logged);
}

}  // namespace
}  // namespace holonight_packages_application
