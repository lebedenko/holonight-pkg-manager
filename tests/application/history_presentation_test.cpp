#include "holonight_packages_application/history_presentation.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

namespace {

using holonight_packages_application::actionKindLabel;
using holonight_packages_application::dayKey;
using holonight_packages_application::LocalDay;
using holonight_packages_application::localDay;
using holonight_packages_application::originLabel;
using holonight_packages_application::statusLabel;
using holonight_packages_application::summaryText;
using holonight_packages_application::systemUserName;
using holonight_packages_application::timeLabel;
using holonight_packages_application::UserNameResolver;
using holonight_packages_domain::HistoryAction;
using holonight_packages_domain::HistoryActionKind;
using holonight_packages_domain::HistoryOrigin;
using holonight_packages_domain::HistoryOriginKind;
using holonight_packages_domain::HistoryStatus;
using std::chrono::sys_days;
using namespace std::chrono_literals;

// Sets TZ (a POSIX rule string, so no tzdata is needed) for one test and restores the previous value.
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

constexpr const char* kBerlin = "CET-1CEST,M3.5.0,M10.5.0/3";

HistoryOrigin packagesBy(std::uint32_t uid) {
  return HistoryOrigin{.kind = HistoryOriginKind::Packages, .uid = uid, .command = {}};
}

std::vector<HistoryAction> actionsOf(const std::vector<std::pair<HistoryActionKind, int>>& counts) {
  std::vector<HistoryAction> actions;
  for (const auto& [kind, count] : counts) {
    for (int i = 0; i < count; ++i) {
      actions.push_back(HistoryAction{
          .kind = kind,
          .name = "p" + std::to_string(actions.size()),
          .old_version = {},
          .new_version = {},
      });
    }
  }
  return actions;
}

TEST(HistoryPresentation, OriginOfTheCurrentUserIsPackages) {
  int resolved = 0;
  const UserNameResolver resolver = [&resolved](std::uint32_t /*uid*/) {
    ++resolved;
    return std::optional<std::string>("someone");
  };
  EXPECT_EQ(originLabel(packagesBy(1000), 1000, resolver), "Packages");
  EXPECT_EQ(resolved, 0);
}

TEST(HistoryPresentation, OriginOfAnotherUserNamesThem) {
  const UserNameResolver resolver = [](std::uint32_t uid) {
    return uid == 1001 ? std::optional<std::string>("alice") : std::nullopt;
  };
  EXPECT_EQ(originLabel(packagesBy(1001), 1000, resolver), "Packages · alice");
  EXPECT_EQ(originLabel(packagesBy(1002), 1000, resolver), "Packages · uid 1002");
  EXPECT_EQ(originLabel(packagesBy(1002), 1000, UserNameResolver{}), "Packages · uid 1002");
}

TEST(HistoryPresentation, PackagesOriginWithoutUidIsPackages) {
  EXPECT_EQ(originLabel(HistoryOrigin{.kind = HistoryOriginKind::Packages, .uid = std::nullopt, .command = {}}, 1000,
                        UserNameResolver{}),
            "Packages");
}

TEST(HistoryPresentation, PacmanAndUnknownOrigins) {
  EXPECT_EQ(originLabel(HistoryOrigin{.kind = HistoryOriginKind::Pacman,
                                      .uid = std::nullopt,
                                      .command = "pacman --sync --refresh --sysupgrade"},
                        1000, UserNameResolver{}),
            "pacman");
  EXPECT_EQ(originLabel(HistoryOrigin{}, 1000, UserNameResolver{}), "unknown");
}

TEST(HistoryPresentation, SummaryCountsInTheFixedOrder) {
  EXPECT_EQ(
      summaryText(actionsOf(
          {{HistoryActionKind::Removed, 2}, {HistoryActionKind::Installed, 1}, {HistoryActionKind::Upgraded, 12}})),
      "12 upgraded · 1 installed · 2 removed");
  EXPECT_EQ(summaryText(actionsOf({{HistoryActionKind::Removed, 1}})), "1 removed");
  EXPECT_EQ(summaryText(actionsOf({{HistoryActionKind::Reinstalled, 1},
                                   {HistoryActionKind::Removed, 1},
                                   {HistoryActionKind::Downgraded, 1},
                                   {HistoryActionKind::Installed, 1},
                                   {HistoryActionKind::Upgraded, 1}})),
            "1 upgraded · 1 installed · 1 downgraded · 1 reinstalled · 1 removed");
  EXPECT_EQ(summaryText({}), "");
}

TEST(HistoryPresentation, StatusAndActionLabels) {
  EXPECT_EQ(statusLabel(HistoryStatus::Completed), "Completed");
  EXPECT_EQ(statusLabel(HistoryStatus::Interrupted), "Interrupted");
  EXPECT_EQ(statusLabel(HistoryStatus::InProgress), "In progress");
  EXPECT_EQ(actionKindLabel(HistoryActionKind::Installed), "installed");
  EXPECT_EQ(actionKindLabel(HistoryActionKind::Upgraded), "upgraded");
  EXPECT_EQ(actionKindLabel(HistoryActionKind::Downgraded), "downgraded");
  EXPECT_EQ(actionKindLabel(HistoryActionKind::Reinstalled), "reinstalled");
  EXPECT_EQ(actionKindLabel(HistoryActionKind::Removed), "removed");
}

TEST(HistoryPresentation, TimeLabelLegacy) {
  const ScopedTimeZone zone(kBerlin);
  // The legacy log line [2019-01-01 12:00] is 11:00 UTC under Europe/Berlin rules.
  EXPECT_EQ(timeLabel(sys_days{2019y / 1 / 1} + 11h), "12:00");
}

TEST(HistoryPresentation, TimeLabelIsLocal) {
  const ScopedTimeZone zone("UTC0");
  EXPECT_EQ(timeLabel(sys_days{2026y / 11 / 23} + 8h + 5min + 18s), "08:05");
}

TEST(HistoryPresentation, LocalDayFollowsTheTimeZone) {
  const auto late_evening_utc = sys_days{2026y / 11 / 22} + 23h + 30min;
  {
    const ScopedTimeZone zone("UTC0");
    EXPECT_EQ(localDay(late_evening_utc), (LocalDay{.year = 2026, .month = 11, .day = 22}));
  }
  {
    const ScopedTimeZone zone(kBerlin);
    EXPECT_EQ(localDay(late_evening_utc), (LocalDay{.year = 2026, .month = 11, .day = 23}));
    EXPECT_EQ(dayKey(localDay(late_evening_utc)), "2026-11-23");
  }
  EXPECT_LT((LocalDay{.year = 2026, .month = 1, .day = 31}), (LocalDay{.year = 2026, .month = 2, .day = 1}));
}

TEST(HistoryPresentation, SystemUserNameOfRootIsRoot) { EXPECT_EQ(systemUserName(0), "root"); }

}  // namespace
