#include "UpdatesModel.h"
#include "fake_update_source.h"

#include <QTest>

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace {

using holonight_packages_domain::CheckedSnapshot;
using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_testing::FakeUpdateSource;
using ViewState = UpdatesModel::ViewState;
using std::chrono::hours;
using TimePoint = std::chrono::system_clock::time_point;

const TimePoint kNow = TimePoint{} + std::chrono::days(20000);

UpdatesModel::Clock fixedClock() {
  return [] { return kNow; };
}

UpdateSnapshot snapshot(int count, TimePoint dataAsOf, const std::string& prefix) {
  UpdateSnapshot result;
  result.databasesFound = true;
  result.dataAsOf = dataAsOf;
  for (int index = 0; index < count; ++index) {
    result.updates.push_back(PendingUpdate{
        .name = prefix + std::to_string(index),
        .installedVersion = "1-1",
        .availableVersion = "2-1",
        .repository = "core",
        .downloadSizeBytes = 10,
    });
  }
  return result;
}

CheckedSnapshot checked(int count, TimePoint dataAsOf) {
  return CheckedSnapshot{.snapshot = snapshot(count, dataAsOf, "online"), .fetchedAt = dataAsOf + hours(1)};
}

std::unique_ptr<UpdatesModel> loadedModel(const std::shared_ptr<FakeUpdateSource>& source) {
  auto model = std::make_unique<UpdatesModel>(source, nullptr, fixedClock());
  EXPECT_TRUE(QTest::qWaitFor([&model] { return !model->loading(); }, 2000));
  return model;
}

QString nameAt(const UpdatesModel& model, int row) {
  return model.data(model.index(row), UpdatesModel::NameRole).toString();
}

TEST(UpdatesModelCheckedSnapshot, FresherOnlineSnapshotReplacesTheLocalList) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshot(1, kNow - hours(48), "local"));
  auto model = loadedModel(source);
  ASSERT_EQ(model->updateCount(), 1);

  model->applyCheckedSnapshot(checked(4, kNow - hours(3)));

  EXPECT_EQ(model->updateCount(), 4);
  EXPECT_EQ(model->state(), ViewState::Updates);
  EXPECT_EQ(nameAt(*model, 0), "online0");
}

TEST(UpdatesModelCheckedSnapshot, OlderOnlineSnapshotLeavesTheLocalList) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshot(1, kNow - hours(1), "local"));
  auto model = loadedModel(source);

  model->applyCheckedSnapshot(checked(4, kNow - hours(24)));

  EXPECT_EQ(model->updateCount(), 1);
  EXPECT_EQ(nameAt(*model, 0), "local0");
}

TEST(UpdatesModelCheckedSnapshot, EqualDataAsOfPrefersTheLocalList) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshot(1, kNow - hours(5), "local"));
  auto model = loadedModel(source);
  model->applyCheckedSnapshot(checked(4, kNow - hours(5)));
  EXPECT_EQ(model->updateCount(), 1);
}

TEST(UpdatesModelCheckedSnapshot, ReloadAfterOnlineSnapshotStillFollowsTheFreshnessRule) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshot(1, kNow - hours(48), "local"));
  auto model = loadedModel(source);
  model->applyCheckedSnapshot(checked(4, kNow - hours(3)));
  ASSERT_EQ(model->updateCount(), 4);

  // The user syncs outside the app: the local databases become newer than the online snapshot.
  source->enqueue(snapshot(0, kNow - hours(1), "local"));
  model->reload();
  ASSERT_TRUE(QTest::qWaitFor([&model] { return !model->loading(); }, 2000));

  EXPECT_EQ(model->updateCount(), 0);
  EXPECT_EQ(model->state(), ViewState::UpToDate);
}

TEST(UpdatesModelCheckedSnapshot, OnlineSnapshotArrivingDuringTheFirstLoadIsShownAndThenMerged) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->setBlocking(true);
  source->enqueue(snapshot(1, kNow - hours(48), "local"));
  UpdatesModel model(source, nullptr, fixedClock());
  ASSERT_EQ(model.state(), ViewState::Loading);

  model.applyCheckedSnapshot(checked(4, kNow - hours(3)));
  EXPECT_EQ(model.updateCount(), 4);

  source->release();
  ASSERT_TRUE(QTest::qWaitFor([&model] { return !model.loading(); }, 2000));
  EXPECT_EQ(model.updateCount(), 4) << "the online snapshot is fresher than the local databases";
}

TEST(UpdatesModelCheckedSnapshot, NoOnlineSnapshotLeavesTheBehaviourExactlyAsBefore) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshot(2, kNow - hours(1), "local"));
  auto model = loadedModel(source);
  EXPECT_EQ(model->updateCount(), 2);
  EXPECT_EQ(model->state(), ViewState::Updates);
}

}  // namespace
