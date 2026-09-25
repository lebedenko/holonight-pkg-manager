#include "holonight_packages_application/data_freshness.h"

#include <chrono>
#include <gtest/gtest.h>

namespace holonight_packages_application {
namespace {

using std::chrono::days;
using std::chrono::hours;

const std::chrono::system_clock::time_point kNow{std::chrono::sys_days{std::chrono::year{2026} / 9 / 22}};

TEST(DataFreshness, StaleAfterIsSevenDays) { EXPECT_EQ(kStaleAfter, days{7}); }

TEST(DataFreshness, RecentDataIsFresh) {
  EXPECT_FALSE(isStale(kNow - hours{2}, kNow));
  EXPECT_FALSE(isStale(kNow - days{6}, kNow));
}

TEST(DataFreshness, ExactlySevenDaysIsNotStale) { EXPECT_FALSE(isStale(kNow - days{7}, kNow)); }

TEST(DataFreshness, EightDaysIsStale) { EXPECT_TRUE(isStale(kNow - days{8}, kNow)); }

}  // namespace
}  // namespace holonight_packages_application
