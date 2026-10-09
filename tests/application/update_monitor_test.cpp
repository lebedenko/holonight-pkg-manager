#include "holonight_packages_application/update_monitor.h"

#include "fake_update_source.h"

#include <QFile>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_domain::UpdateSourceError;
using holonight_packages_domain::UpdateSourceErrorCode;
using holonight_packages_testing::FakeUpdateSource;
using std::chrono::milliseconds;

constexpr int kWaitMs = 5000;

UpdateSnapshot snapshotWithCount(int count) {
  UpdateSnapshot snapshot;
  for (int index = 0; index < count; ++index) {
    snapshot.updates.push_back(PendingUpdate{.name = "pkg-" + std::to_string(index)});
  }
  return snapshot;
}

class UpdateMonitorTest : public ::testing::Test {
 protected:
  std::shared_ptr<FakeUpdateSource> source_ = std::make_shared<FakeUpdateSource>();
};

TEST_F(UpdateMonitorTest, StartRunsOneEvaluationAndPublishesReadyStatus) {
  source_->enqueue(snapshotWithCount(3));
  UpdateMonitor monitor(source_, UpdateMonitorOptions{.debounce = milliseconds{10}});
  QSignalSpy spy(&monitor, &UpdateMonitor::statusChanged);
  EXPECT_EQ(monitor.status().state, UpdateState::Loading);

  monitor.start();

  ASSERT_TRUE(spy.wait(kWaitMs));
  EXPECT_EQ(monitor.status().state, UpdateState::Ready);
  EXPECT_EQ(monitor.status().updateCount, 3);
  EXPECT_EQ(source_->callCount(), 1);
}

TEST_F(UpdateMonitorTest, TriggerDuringEvaluationCoalescesIntoOneFollowUp) {
  source_->setBlocking(true);
  source_->enqueue(snapshotWithCount(1));
  source_->enqueue(snapshotWithCount(2));
  UpdateMonitor monitor(source_, UpdateMonitorOptions{.debounce = milliseconds{10}});
  QSignalSpy spy(&monitor, &UpdateMonitor::statusChanged);

  monitor.refresh();
  monitor.refresh();
  monitor.refresh();
  source_->release();
  ASSERT_TRUE(spy.wait(kWaitMs));
  EXPECT_EQ(monitor.status().updateCount, 1);
  source_->release();
  ASSERT_TRUE(spy.wait(kWaitMs));

  EXPECT_EQ(monitor.status().updateCount, 2);
  EXPECT_EQ(source_->callCount(), 2);
}

TEST_F(UpdateMonitorTest, EventLoopStaysResponsiveWhileEvaluationIsBlocked) {
  source_->setBlocking(true);
  UpdateMonitor monitor(source_, UpdateMonitorOptions{.debounce = milliseconds{10}});
  QSignalSpy spy(&monitor, &UpdateMonitor::statusChanged);

  monitor.refresh();
  bool timer_fired = false;
  QTimer::singleShot(20, [&timer_fired] { timer_fired = true; });
  QTest::qWait(200);

  EXPECT_TRUE(timer_fired);
  EXPECT_EQ(spy.count(), 0);
  source_->release();
  ASSERT_TRUE(spy.wait(kWaitMs));
}

TEST_F(UpdateMonitorTest, FailureAfterSuccessKeepsCountAndReportsError) {
  source_->enqueue(snapshotWithCount(4));
  source_->enqueue(
      std::unexpected(UpdateSourceError{.code = UpdateSourceErrorCode::DatabaseOpenFailed, .message = "cannot open"}));
  UpdateMonitor monitor(source_, UpdateMonitorOptions{.debounce = milliseconds{10}});
  QSignalSpy spy(&monitor, &UpdateMonitor::statusChanged);

  monitor.refresh();
  ASSERT_TRUE(spy.wait(kWaitMs));
  monitor.refresh();
  ASSERT_TRUE(spy.wait(kWaitMs));

  EXPECT_EQ(monitor.status().state, UpdateState::Error);
  EXPECT_EQ(monitor.status().lastError, "cannot open");
  EXPECT_EQ(monitor.status().updateCount, 4);
}

TEST_F(UpdateMonitorTest, UnchangedResultDoesNotEmit) {
  source_->enqueue(snapshotWithCount(1));
  source_->enqueue(snapshotWithCount(1));
  UpdateMonitor monitor(source_, UpdateMonitorOptions{.debounce = milliseconds{10}});
  QSignalSpy spy(&monitor, &UpdateMonitor::statusChanged);

  monitor.refresh();
  ASSERT_TRUE(spy.wait(kWaitMs));
  monitor.refresh();
  ASSERT_TRUE(QTest::qWaitFor([&] { return (source_->callCount()) == (2); }, kWaitMs));
  QTest::qWait(50);

  EXPECT_EQ(spy.count(), 1);
}

class UpdateMonitorWatchTest : public UpdateMonitorTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    ASSERT_TRUE(QDir(dir_.path()).mkdir("sync"));
  }

  [[nodiscard]] QString syncDir() const { return dir_.filePath("sync"); }

  void replaceFile(const QString& name, const QByteArray& content) const {
    QSaveFile file(QDir(syncDir()).filePath(name));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(content);
    ASSERT_TRUE(file.commit());
  }

  [[nodiscard]] UpdateMonitorOptions options(milliseconds debounce) const {
    return UpdateMonitorOptions{.watchPaths = {syncDir()}, .debounce = debounce};
  }

  QTemporaryDir dir_;
};

TEST_F(UpdateMonitorWatchTest, BurstOfChangesInsideTheWindowGivesOneEvaluation) {
  UpdateMonitor monitor(source_, options(milliseconds{300}));
  monitor.start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return (source_->callCount()) == (1); }, kWaitMs));

  for (int index = 0; index < 20; ++index) {
    replaceFile(QStringLiteral("db-%1.db").arg(index), "x");
  }

  ASSERT_TRUE(QTest::qWaitFor([&] { return (source_->callCount()) == (2); }, kWaitMs));
  QTest::qWait(700);
  EXPECT_EQ(source_->callCount(), 2);
}

TEST_F(UpdateMonitorWatchTest, ReplacingAFileTwiceGivesTwoEvaluations) {
  UpdateMonitor monitor(source_, options(milliseconds{50}));
  monitor.start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return (source_->callCount()) == (1); }, kWaitMs));

  replaceFile(QStringLiteral("core.db"), "one");
  ASSERT_TRUE(QTest::qWaitFor([&] { return (source_->callCount()) == (2); }, kWaitMs));
  replaceFile(QStringLiteral("core.db"), "two");
  ASSERT_TRUE(QTest::qWaitFor([&] { return (source_->callCount()) == (3); }, kWaitMs));
}

TEST_F(UpdateMonitorWatchTest, NoEvaluationHappensWithoutATrigger) {
  UpdateMonitor monitor(source_, options(milliseconds{20}));
  monitor.start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return (source_->callCount()) == (1); }, kWaitMs));

  QTest::qWait(500);

  EXPECT_EQ(source_->callCount(), 1);
}

TEST_F(UpdateMonitorWatchTest, MissingDirectoryIsWatchedWhenCreatedAfterStartup) {
  ASSERT_TRUE(QDir(dir_.path()).rmdir("sync"));
  source_->enqueue(UpdateSnapshot{.databasesFound = false});
  source_->enqueue(snapshotWithCount(3));
  UpdateMonitor monitor(source_, options(milliseconds{20}));
  monitor.start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return monitor.status().state == UpdateState::NoDatabases; }, kWaitMs));

  ASSERT_TRUE(QDir(dir_.path()).mkdir("sync"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return monitor.status().updateCount == 3; }, kWaitMs));

  replaceFile(QStringLiteral("core.db"), "one");
  ASSERT_TRUE(QTest::qWaitFor([&] { return source_->callCount() == 3; }, kWaitMs));
}

TEST_F(UpdateMonitorWatchTest, RemovedDirectoryIsWatchedAgainAfterDelayedRecreation) {
  UpdateMonitor monitor(source_, options(milliseconds{20}));
  monitor.start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return source_->callCount() == 1; }, kWaitMs));

  ASSERT_TRUE(QDir(dir_.path()).rmdir("sync"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return source_->callCount() == 2; }, kWaitMs));
  QTest::qWait(100);
  ASSERT_TRUE(QDir(dir_.path()).mkdir("sync"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return source_->callCount() == 3; }, kWaitMs));

  replaceFile(QStringLiteral("core.db"), "one");
  ASSERT_TRUE(QTest::qWaitFor([&] { return source_->callCount() == 4; }, kWaitMs));
}

TEST_F(UpdateMonitorWatchTest, MissingAncestorsAreWatchedAsTheyAreCreated) {
  const QString path = dir_.filePath("db/sync");
  UpdateMonitor monitor(source_, UpdateMonitorOptions{.watchPaths = {path}, .debounce = milliseconds{20}});
  monitor.start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return source_->callCount() == 1; }, kWaitMs));

  ASSERT_TRUE(QDir(dir_.path()).mkdir("db"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return source_->callCount() == 2; }, kWaitMs));
  ASSERT_TRUE(QDir(dir_.filePath("db")).mkdir("sync"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return source_->callCount() == 3; }, kWaitMs));

  QSaveFile file(QDir(path).filePath("core.db"));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write("one"), 3);
  ASSERT_TRUE(file.commit());
  ASSERT_TRUE(QTest::qWaitFor([&] { return source_->callCount() == 4; }, kWaitMs));
}

}  // namespace
}  // namespace holonight_packages_application
