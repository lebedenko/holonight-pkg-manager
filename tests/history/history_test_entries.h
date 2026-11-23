#pragma once

#include "holonight_packages_domain/history_source.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace holonight_test {

// A history entry started at `day` + `time` UTC that upgrades `names` (1-1 -> 2-1).
inline holonight_packages_domain::HistoryEntry historyEntry(std::chrono::sys_days day, std::chrono::seconds time,
                                                            std::initializer_list<std::string> names = {"foo"}) {
  holonight_packages_domain::HistoryEntry entry;
  entry.started = day + time;
  entry.status = holonight_packages_domain::HistoryStatus::Completed;
  for (const std::string& name : names) {
    entry.actions.push_back({
        .kind = holonight_packages_domain::HistoryActionKind::Upgraded,
        .name = name,
        .old_version = "1-1",
        .new_version = "2-1",
    });
  }
  return entry;
}

inline holonight_packages_domain::HistoryReadResult resetWith(
    std::vector<holonight_packages_domain::HistoryEntry> entries) {
  holonight_packages_domain::HistoryReadResult result;
  result.reset = true;
  result.append = std::move(entries);
  return result;
}

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

}  // namespace holonight_test
