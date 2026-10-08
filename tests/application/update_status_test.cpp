#include "holonight_packages_application/update_status.h"

#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_domain::UpdateSourceError;
using holonight_packages_domain::UpdateSourceErrorCode;

PendingUpdate makeUpdate(std::string name, std::uint64_t download_bytes, bool ignored) {
  PendingUpdate update;
  update.name = std::move(name);
  update.downloadSizeBytes = download_bytes;
  update.ignored = ignored;
  return update;
}

UpdateSnapshot makeSnapshot(int normal, int ignored) {
  UpdateSnapshot snapshot;
  for (int index = 0; index < normal; ++index) {
    snapshot.updates.push_back(makeUpdate("normal-" + std::to_string(index), 100, false));
  }
  for (int index = 0; index < ignored; ++index) {
    snapshot.updates.push_back(makeUpdate("ignored-" + std::to_string(index), 5000, true));
  }
  snapshot.dataAsOf = std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}};
  return snapshot;
}

TEST(UpdateStatus, IgnoredRowsAreCountedSeparatelyAndExcludedFromTotals) {
  const UpdateStatus status = buildUpdateStatus(makeSnapshot(7, 3), std::nullopt);

  EXPECT_EQ(status.state, UpdateState::Ready);
  EXPECT_EQ(status.updateCount, 7);
  EXPECT_EQ(status.ignoredCount, 3);
  EXPECT_EQ(status.totalDownloadBytes, 700U);
  EXPECT_EQ(status.dataAsOfEpochSeconds, 1'700'000'000);
  EXPECT_TRUE(status.lastError.empty());
}

TEST(UpdateStatus, UpToDateIsReadyWithZeroCount) {
  const UpdateStatus status = buildUpdateStatus(makeSnapshot(0, 0), std::nullopt);

  EXPECT_EQ(status.state, UpdateState::Ready);
  EXPECT_EQ(status.updateCount, 0);
}

TEST(UpdateStatus, MissingDatabasesGiveNoDatabasesAndClearEarlierValues) {
  const UpdateStatus earlier = buildUpdateStatus(makeSnapshot(4, 0), std::nullopt);
  UpdateSnapshot missing;
  missing.databasesFound = false;

  const UpdateStatus status = buildUpdateStatus(missing, earlier);

  EXPECT_EQ(status.state, UpdateState::NoDatabases);
  EXPECT_EQ(status.updateCount, 0);
  EXPECT_EQ(status.totalDownloadBytes, 0U);
  EXPECT_EQ(status.dataAsOfEpochSeconds, 0);
}

TEST(UpdateStatus, FailureAfterSuccessKeepsEarlierCountsAndReportsError) {
  const UpdateStatus earlier = buildUpdateStatus(makeSnapshot(7, 3), std::nullopt);
  const std::expected<UpdateSnapshot, UpdateSourceError> failure =
      std::unexpected(UpdateSourceError{UpdateSourceErrorCode::DatabaseOpenFailed, "cannot open"});

  const UpdateStatus status = buildUpdateStatus(failure, earlier);

  EXPECT_EQ(status.state, UpdateState::Error);
  EXPECT_EQ(status.lastError, "cannot open");
  EXPECT_EQ(status.updateCount, 7);
  EXPECT_EQ(status.ignoredCount, 3);
  EXPECT_EQ(status.totalDownloadBytes, 700U);
  EXPECT_EQ(status.dataAsOfEpochSeconds, 1'700'000'000);
}

TEST(UpdateStatus, RecoveryAfterFailureClearsTheError) {
  const std::expected<UpdateSnapshot, UpdateSourceError> failure =
      std::unexpected(UpdateSourceError{UpdateSourceErrorCode::Unknown, "boom"});
  const UpdateStatus failed = buildUpdateStatus(failure, std::nullopt);

  const UpdateStatus status = buildUpdateStatus(makeSnapshot(2, 0), failed);

  EXPECT_EQ(status.state, UpdateState::Ready);
  EXPECT_TRUE(status.lastError.empty());
  EXPECT_EQ(status.updateCount, 2);
}

TEST(UpdateStatus, FailureWithoutEarlierStatusHasZeroCounts) {
  const std::expected<UpdateSnapshot, UpdateSourceError> failure =
      std::unexpected(UpdateSourceError{UpdateSourceErrorCode::ConfigurationInvalid, "no conf"});

  const UpdateStatus status = buildUpdateStatus(failure, std::nullopt);

  EXPECT_EQ(status.state, UpdateState::Error);
  EXPECT_EQ(status.updateCount, 0);
}

}  // namespace
}  // namespace holonight_packages_application
