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
  result.evaluated = true;
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

TEST(UpdatesModelCheckedSnapshot, LegacyArrivalReevaluatesRatherThanReplacingCurrentRows) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshot(1, kNow - hours(48), "local"));
  auto model = loadedModel(source);
  source->enqueue(snapshot(1, kNow - hours(48), "current"));
  model->applyCheckedSnapshot(checked(5, kNow));
  EXPECT_EQ(model->updateCount(), 1);
  ASSERT_TRUE(QTest::qWaitFor([&model] { return !model->loading(); }, 2000));
  EXPECT_EQ(nameAt(*model, 0), "current0");
  EXPECT_FALSE(model->previouslyLoaded());
}

TEST(UpdatesModelCheckedSnapshot, ArrivalDuringLoadIsHistoricalUntilReevaluationCompletes) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->setBlocking(true);
  source->enqueue(snapshot(5, kNow, "obsolete"));
  source->enqueue(snapshot(1, kNow - hours(48), "current"));
  UpdatesModel model(source, nullptr, fixedClock());
  model.applyCheckedSnapshot(checked(5, kNow));
  EXPECT_TRUE(model.previouslyLoaded());
  EXPECT_EQ(model.updateCount(), 5);
  EXPECT_EQ(model.displayedFetchedAt(), kNow + hours(1));
  source->release();
  ASSERT_TRUE(QTest::qWaitFor([&source] { return source->callCount() == 2; }, 2000));
  source->release();
  ASSERT_TRUE(QTest::qWaitFor([&model] { return !model.loading(); }, 2000));
  EXPECT_EQ(model.updateCount(), 1);
  EXPECT_EQ(nameAt(model, 0), "current0");
  EXPECT_FALSE(model.previouslyLoaded());
}

TEST(UpdatesModelCheckedSnapshot, HistoricalEmptyFallbackExposesFailureAndRecovers) {
  auto source = std::make_shared<FakeUpdateSource>();
  auto historical = snapshot(0, kNow, "saved");
  historical.evaluated = false;
  historical.previously_loaded = true;
  historical.evaluation_error = "cannot read installed packages";
  source->enqueue(historical);
  auto model = loadedModel(source);
  EXPECT_TRUE(model->previouslyLoaded());
  EXPECT_EQ(model->emptyStateText(), "No current update evaluation is available.");
  EXPECT_EQ(model->reloadErrorMessage(), "cannot read installed packages");
  source->enqueue(snapshot(0, kNow, "current"));
  model->reload();
  ASSERT_TRUE(QTest::qWaitFor([&model] { return !model->loading(); }, 2000));
  EXPECT_FALSE(model->previouslyLoaded());
  EXPECT_TRUE(model->reloadErrorMessage().isEmpty());
}

}  // namespace
