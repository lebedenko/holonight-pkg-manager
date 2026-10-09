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

TEST(SnapshotSelectionTest, MissingSidesForceTheChoice) {
  EXPECT_EQ(selectFresherSnapshot(withDataAsOf(100), std::nullopt), SnapshotChoice::Local);
  EXPECT_EQ(selectFresherSnapshot(std::nullopt, withDataAsOf(100)), SnapshotChoice::Online);
  EXPECT_EQ(selectFresherSnapshot(std::nullopt, std::nullopt), SnapshotChoice::Local);
  EXPECT_EQ(selectFresherSnapshot(withDataAsOf(500, false), withDataAsOf(100)), SnapshotChoice::Online);
}

}  // namespace
}  // namespace holonight_packages_application
