#include "fake_update_source.h"
#include "holonight_packages_application/update_monitor.h"

#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::CheckedSnapshot;
using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_testing::FakeUpdateSource;
using std::chrono::seconds;
using std::chrono::system_clock;

constexpr int kWaitMs = 5000;

UpdateSnapshot snapshot(int count, long long dataAsOfSeconds) {
  UpdateSnapshot result;
  for (int index = 0; index < count; ++index) {
    result.updates.push_back(PendingUpdate{.name = "pkg-" + std::to_string(index)});
  }
  result.dataAsOf = system_clock::time_point{seconds{dataAsOfSeconds}};
  return result;
}

class UpdateMonitorAdoptTest : public ::testing::Test {
 protected:
  std::shared_ptr<FakeUpdateSource> source_ = std::make_shared<FakeUpdateSource>();
};

TEST_F(UpdateMonitorAdoptTest, FresherOnlineSnapshotReplacesTheMonitorCount) {
  source_->enqueue(snapshot(1, 100));
  UpdateMonitor monitor(source_, UpdateMonitorOptions{});
  QSignalSpy spy(&monitor, &UpdateMonitor::statusChanged);
  monitor.refresh();
  ASSERT_TRUE(spy.wait(kWaitMs));
  ASSERT_EQ(monitor.status().updateCount, 1);

  monitor.adoptOnline(
      CheckedSnapshot{.snapshot = snapshot(4, 200), .fetchedAt = system_clock::time_point{seconds{201}}});

  EXPECT_EQ(monitor.status().updateCount, 4);
  EXPECT_EQ(monitor.status().state, UpdateState::Ready);
  EXPECT_EQ(monitor.status().dataAsOfEpochSeconds, 200);
}

TEST_F(UpdateMonitorAdoptTest, FresherLocalResultKeepsTheLocalCount) {
  UpdateMonitor monitor(source_, UpdateMonitorOptions{});
  monitor.adoptOnline(
      CheckedSnapshot{.snapshot = snapshot(4, 200), .fetchedAt = system_clock::time_point{seconds{201}}});
  EXPECT_EQ(monitor.status().updateCount, 4);

  source_->enqueue(snapshot(1, 300));
  QSignalSpy spy(&monitor, &UpdateMonitor::statusChanged);
  monitor.refresh();
  ASSERT_TRUE(spy.wait(kWaitMs));

  EXPECT_EQ(monitor.status().updateCount, 1);
  EXPECT_EQ(monitor.status().dataAsOfEpochSeconds, 300);
}

TEST_F(UpdateMonitorAdoptTest, OlderLocalResultDoesNotOverrideTheOnlineSnapshot) {
  UpdateMonitor monitor(source_, UpdateMonitorOptions{});
  monitor.adoptOnline(
      CheckedSnapshot{.snapshot = snapshot(4, 200), .fetchedAt = system_clock::time_point{seconds{201}}});

  source_->enqueue(snapshot(1, 100));
  QSignalSpy spy(&monitor, &UpdateMonitor::statusChanged);
  monitor.refresh();
  QTest::qWait(100);

  EXPECT_EQ(monitor.status().updateCount, 4);
}

TEST_F(UpdateMonitorAdoptTest, LocalFailureAfterAdoptionKeepsTheOnlineCounts) {
  UpdateMonitor monitor(source_, UpdateMonitorOptions{});
  monitor.adoptOnline(
      CheckedSnapshot{.snapshot = snapshot(4, 200), .fetchedAt = system_clock::time_point{seconds{201}}});
  source_->enqueue(std::unexpected(holonight_packages_domain::UpdateSourceError{
      .code = holonight_packages_domain::UpdateSourceErrorCode::DatabaseOpenFailed,
      .message = "boom",
  }));
  QSignalSpy spy(&monitor, &UpdateMonitor::statusChanged);
  monitor.refresh();
  ASSERT_TRUE(spy.wait(kWaitMs));

  EXPECT_EQ(monitor.status().state, UpdateState::Error);
  EXPECT_EQ(monitor.status().updateCount, 4);
}

}  // namespace
}  // namespace holonight_packages_application
