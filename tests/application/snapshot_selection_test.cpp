#include "holonight_packages_application/snapshot_selection.h"

#include <gtest/gtest.h>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::UpdateSnapshot;
using std::chrono::system_clock;

UpdateSnapshot withDataAsOf(long long seconds, bool found = true) {
  UpdateSnapshot snapshot;
  snapshot.databasesFound = found;
  snapshot.dataAsOf = system_clock::time_point{std::chrono::seconds{seconds}};
  return snapshot;
}

TEST(SnapshotSelectionTest, OnlineWinsWhenLocalIsOlder) {
  EXPECT_EQ(selectFresherSnapshot(withDataAsOf(100), withDataAsOf(200)), SnapshotChoice::Online);
}

TEST(SnapshotSelectionTest, LocalWinsWhenEqualOrNewer) {
  EXPECT_EQ(selectFresherSnapshot(withDataAsOf(200), withDataAsOf(200)), SnapshotChoice::Local);
  EXPECT_EQ(selectFresherSnapshot(withDataAsOf(300), withDataAsOf(200)), SnapshotChoice::Local);
}

TEST(SnapshotSelectionTest, EvaluationOutranksNewerLegacyRows) {
  auto evaluated = withDataAsOf(100);
  evaluated.evaluated = true;
  EXPECT_EQ(selectFresherSnapshot(evaluated, withDataAsOf(1000)), SnapshotChoice::Local);
}

TEST(SnapshotSelectionTest, MissingSidesForceTheChoice) {
  EXPECT_EQ(selectFresherSnapshot(withDataAsOf(100), std::nullopt), SnapshotChoice::Local);
  EXPECT_EQ(selectFresherSnapshot(std::nullopt, withDataAsOf(100)), SnapshotChoice::Online);
  EXPECT_EQ(selectFresherSnapshot(std::nullopt, std::nullopt), SnapshotChoice::Local);
  EXPECT_EQ(selectFresherSnapshot(withDataAsOf(500, false), withDataAsOf(100)), SnapshotChoice::Online);
}

TEST(UpdateSnapshotStateTest, CheckedRowsAreHistoricalUntilAnOfflineEvaluationSucceeds) {
  UpdateSnapshotState state;
  auto checked = withDataAsOf(200);
  checked.evaluated = true;
  state.adoptChecked({.snapshot = checked, .fetchedAt = checked.dataAsOf});

  EXPECT_TRUE(state.selected().previously_loaded);
  EXPECT_FALSE(state.selected().evaluated);
  EXPECT_EQ(state.selected().dataAsOf, checked.dataAsOf);

  auto evaluated = withDataAsOf(100);
  evaluated.evaluated = true;
  state.recordEvaluation(evaluated);

  EXPECT_EQ(state.selected(), evaluated);
}

TEST(UpdateSnapshotStateTest, InvalidationIsConsumedOnceForAnInFlightEvaluation) {
  UpdateSnapshotState state;
  state.invalidateInFlightEvaluation(false);
  EXPECT_FALSE(state.takeInvalidation());

  state.invalidateInFlightEvaluation(true);
  state.invalidateInFlightEvaluation(true);
  EXPECT_TRUE(state.takeInvalidation());
  EXPECT_FALSE(state.takeInvalidation());
}

}  // namespace
}  // namespace holonight_packages_application
