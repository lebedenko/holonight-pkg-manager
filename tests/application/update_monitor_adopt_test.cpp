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
  result.evaluated = true;
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

TEST_F(UpdateMonitorAdoptTest, LegacyArrivalTriggersEvaluationAndCannotReplaceCurrentCount) {
  source_->enqueue(snapshot(1, 100));
  UpdateMonitor monitor(source_, {});
  monitor.refresh();
  QTRY_COMPARE_WITH_TIMEOUT(monitor.status().updateCount, 1, kWaitMs);
  source_->enqueue(snapshot(2, 100));
  monitor.adoptOnline({.snapshot = snapshot(5, 200), .fetchedAt = system_clock::time_point{seconds{201}}});
  EXPECT_EQ(monitor.status().updateCount, 1);
  QTRY_COMPARE_WITH_TIMEOUT(monitor.status().updateCount, 2, kWaitMs);
  EXPECT_EQ(monitor.status().state, UpdateState::Ready);
}

TEST_F(UpdateMonitorAdoptTest, HistoricalAdoptionHidesPendingStatusAndFailureRetainsTotals) {
  source_->setBlocking(true);
  source_->enqueue(std::unexpected(holonight_packages_domain::UpdateSourceError{
      .code = holonight_packages_domain::UpdateSourceErrorCode::DatabaseOpenFailed,
      .message = "boom",
  }));
  UpdateMonitor monitor(source_, {});
  monitor.adoptOnline({.snapshot = snapshot(5, 200), .fetchedAt = system_clock::time_point{seconds{201}}});
  EXPECT_EQ(monitor.status().state, UpdateState::Error);
  EXPECT_EQ(monitor.status().updateCount, 5);
  source_->release();
  QTRY_COMPARE_WITH_TIMEOUT(monitor.status().lastError, std::string("boom"), kWaitMs);
  EXPECT_EQ(monitor.status().updateCount, 5);
  source_->setBlocking(false);
  source_->enqueue(snapshot(1, 100));
  monitor.refresh();
  QTRY_COMPARE_WITH_TIMEOUT(monitor.status().state, UpdateState::Ready, kWaitMs);
  EXPECT_EQ(monitor.status().updateCount, 1);
  EXPECT_TRUE(monitor.status().lastError.empty());
}

}  // namespace
}  // namespace holonight_packages_application
