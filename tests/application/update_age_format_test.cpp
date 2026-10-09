#include "holonight_packages_application/update_age_format.h"

#include <gtest/gtest.h>

namespace holonight_packages_application {
namespace {

using std::chrono::days;
using std::chrono::hours;
using std::chrono::minutes;
using std::chrono::seconds;
using std::chrono::system_clock;

const system_clock::time_point kNow{seconds{1'760'000'000}};

TEST(UpdateAgeFormatTest, UnderOneMinuteIsJustNow) {
  EXPECT_EQ(formatSnapshotAge(kNow, kNow), "just now");
  EXPECT_EQ(formatSnapshotAge(kNow, kNow - seconds{59}), "just now");
  EXPECT_EQ(formatSnapshotAge(kNow, kNow + hours{1}), "just now");
}

TEST(UpdateAgeFormatTest, MinutesBelowAnHour) {
  EXPECT_EQ(formatSnapshotAge(kNow, kNow - minutes{1}), "1 min");
  EXPECT_EQ(formatSnapshotAge(kNow, kNow - minutes{59}), "59 min");
}

TEST(UpdateAgeFormatTest, HoursBelowFortyEight) {
  EXPECT_EQ(formatSnapshotAge(kNow, kNow - hours{1}), "1 h");
  EXPECT_EQ(formatSnapshotAge(kNow, kNow - hours{3}), "3 h");
  EXPECT_EQ(formatSnapshotAge(kNow, kNow - hours{47}), "47 h");
}

TEST(UpdateAgeFormatTest, DaysFromFortyEightHours) {
  EXPECT_EQ(formatSnapshotAge(kNow, kNow - hours{48}), "2 d");
  EXPECT_EQ(formatSnapshotAge(kNow, kNow - days{10}), "10 d");
}

}  // namespace
}  // namespace holonight_packages_application
