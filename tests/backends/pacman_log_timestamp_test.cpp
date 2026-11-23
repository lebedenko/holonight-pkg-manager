#include "pacman_log_timestamp.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <gtest/gtest.h>
#include <optional>
#include <string>

namespace {

using holonight_packages_backends::hasTimestampShape;
using holonight_packages_backends::parseTimestamp;
using std::chrono::sys_days;
using namespace std::chrono_literals;

// Sets TZ for one test and restores the previous value.
class ScopedTimeZone {
 public:
  explicit ScopedTimeZone(const char* zone) {
    if (const char* previous = std::getenv("TZ")) {
      previous_ = previous;
    }
    setenv("TZ", zone, 1);
    tzset();
  }
  ScopedTimeZone(const ScopedTimeZone&) = delete;
  ScopedTimeZone& operator=(const ScopedTimeZone&) = delete;
  ScopedTimeZone(ScopedTimeZone&&) = delete;
  ScopedTimeZone& operator=(ScopedTimeZone&&) = delete;
  ~ScopedTimeZone() {
    if (previous_) {
      setenv("TZ", previous_->c_str(), 1);
    } else {
      unsetenv("TZ");
    }
    tzset();
  }

 private:
  std::optional<std::string> previous_;
};

std::chrono::system_clock::time_point utc(std::chrono::year_month_day date, std::chrono::seconds time) {
  return sys_days{date} + time;
}

TEST(PacmanLogTimestamp, CurrentFormatAppliesTheOffset) {
  const ScopedTimeZone zone("UTC0");
  EXPECT_EQ(parseTimestamp("2026-11-23T10:05:18+0200"), utc(2026y / 11 / 23, 8h + 5min + 18s));
  EXPECT_EQ(parseTimestamp("2026-11-23T01:00:00-0330"), utc(2026y / 11 / 23, 4h + 30min));
}

TEST(PacmanLogTimestamp, CurrentFormatDoesNotDependOnTheProcessTimeZone) {
  const ScopedTimeZone zone("JST-9");
  EXPECT_EQ(parseTimestamp("2025-02-02T17:11:44+0000"), utc(2025y / 2 / 2, 17h + 11min + 44s));
}

TEST(PacmanLogTimestamp, LegacyFormatIsLocalTime) {
  const ScopedTimeZone zone("CET-1CEST,M3.5.0,M10.5.0/3");                 // Europe/Berlin rules; needs no tzdata
  EXPECT_EQ(parseTimestamp("2019-01-01 12:00"), utc(2019y / 1 / 1, 11h));  // CET, UTC+1
  EXPECT_EQ(parseTimestamp("2019-07-01 12:00"), utc(2019y / 7 / 1, 10h));  // CEST, UTC+2
}

TEST(PacmanLogTimestamp, OtherShapesAreRejected) {
  EXPECT_FALSE(hasTimestampShape("2026-11-2"));
  EXPECT_FALSE(parseTimestamp("2026-11-2").has_value());
  EXPECT_FALSE(hasTimestampShape("2026-11-23 10:05:18"));
  EXPECT_FALSE(hasTimestampShape("2026-11-23T10:05:18Z"));
  EXPECT_FALSE(hasTimestampShape("2026-11-23T10:05:18+02:00"));
  EXPECT_FALSE(hasTimestampShape("abcd-11-23T10:05:18+0200"));
  EXPECT_FALSE(hasTimestampShape(""));
  EXPECT_TRUE(hasTimestampShape("2026-11-23T10:05:18+0200"));
  EXPECT_TRUE(hasTimestampShape("2019-01-01 12:00"));
}

TEST(PacmanLogTimestamp, OutOfRangeFieldsAreRejected) {
  EXPECT_FALSE(parseTimestamp("2026-02-30T10:05:18+0200").has_value());
  EXPECT_FALSE(parseTimestamp("2026-13-01T10:05:18+0200").has_value());
  EXPECT_FALSE(parseTimestamp("2026-11-23T24:00:00+0200").has_value());
  EXPECT_FALSE(parseTimestamp("2026-11-23T10:60:00+0200").has_value());
  EXPECT_FALSE(parseTimestamp("2026-11-23T10:05:18+0260").has_value());
  EXPECT_FALSE(parseTimestamp("2019-00-01 12:00").has_value());
}

TEST(PacmanLogTimestamp, RejectsInstantsOutsideTheClockRangeAfterApplyingOffset) {
  const ScopedTimeZone zone("UTC0");
  EXPECT_FALSE(parseTimestamp("1500-01-01 00:00").has_value());
  EXPECT_FALSE(parseTimestamp("2500-01-01 00:00").has_value());
  EXPECT_FALSE(parseTimestamp("1677-09-21T00:12:43+0000").has_value());
  EXPECT_TRUE(parseTimestamp("1677-09-21T00:12:43-0001").has_value());
  EXPECT_TRUE(parseTimestamp("2262-04-11T23:47:16+0000").has_value());
  EXPECT_FALSE(parseTimestamp("2262-04-11T23:47:16-0001").has_value());
}

}  // namespace
