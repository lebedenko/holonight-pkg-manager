#include "holonight_packages_application/update_summary.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::PendingUpdate;

PendingUpdate makeUpdate(std::string name, std::uint64_t download_bytes, bool ignored) {
  PendingUpdate update;
  update.name = std::move(name);
  update.downloadSizeBytes = download_bytes;
  update.ignored = ignored;
  return update;
}

TEST(UpdateSummary, EmptyListIsAllZero) { EXPECT_EQ(summarizeUpdates({}), UpdateSummary{}); }

TEST(UpdateSummary, IgnoredRowsAreExcludedFromCountAndTotal) {
  std::vector<PendingUpdate> updates;
  updates.reserve(10);
  for (int index = 0; index < 7; ++index) {
    updates.push_back(makeUpdate("normal-" + std::to_string(index), 100, false));
  }
  for (int index = 0; index < 3; ++index) {
    updates.push_back(makeUpdate("ignored-" + std::to_string(index), 5000, true));
  }

  const UpdateSummary summary = summarizeUpdates(updates);

  EXPECT_EQ(summary.updateCount, 7);
  EXPECT_EQ(summary.ignoredCount, 3);
  EXPECT_EQ(summary.totalDownloadBytes, 700U);
}

TEST(UpdateSummary, AllIgnoredGivesZeroCount) {
  const std::vector<PendingUpdate> updates = {makeUpdate("a", 10, true), makeUpdate("b", 20, true)};

  const UpdateSummary summary = summarizeUpdates(updates);

  EXPECT_EQ(summary.updateCount, 0);
  EXPECT_EQ(summary.ignoredCount, 2);
  EXPECT_EQ(summary.totalDownloadBytes, 0U);
}

TEST(UpdateSummary, SortByNameOrdersAscending) {
  std::vector<PendingUpdate> updates = {makeUpdate("zeta", 1, false), makeUpdate("alpha", 2, false),
                                        makeUpdate("mid", 3, true)};

  sortUpdatesByName(updates);

  ASSERT_EQ(updates.size(), 3U);
  EXPECT_EQ(updates[0].name, "alpha");
  EXPECT_EQ(updates[1].name, "mid");
  EXPECT_EQ(updates[2].name, "zeta");
}

}  // namespace
}  // namespace holonight_packages_application
