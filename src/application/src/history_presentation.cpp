#include "holonight_packages_application/history_presentation.h"

#include <array>
#include <cstddef>
#include <ctime>
#include <format>
#include <pwd.h>
#include <unistd.h>
#include <vector>

namespace holonight_packages_application {

using holonight_packages_domain::HistoryAction;
using holonight_packages_domain::HistoryActionKind;
using holonight_packages_domain::HistoryOrigin;
using holonight_packages_domain::HistoryOriginKind;
using holonight_packages_domain::HistoryStatus;

namespace {

constexpr std::string_view kSeparator = " · ";

std::tm localTime(std::chrono::system_clock::time_point time) {
  const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
  std::tm local{};
  localtime_r(&seconds, &local);
  return local;
}

}  // namespace

std::optional<std::string> systemUserName(std::uint32_t uid) {
  const long suggested = sysconf(_SC_GETPW_R_SIZE_MAX);
  std::vector<char> buffer(suggested > 0 ? static_cast<std::size_t>(suggested) : std::size_t{16384});
  passwd entry{};
  passwd* found = nullptr;
  if (getpwuid_r(static_cast<uid_t>(uid), &entry, buffer.data(), buffer.size(), &found) != 0 || found == nullptr ||
      found->pw_name == nullptr) {
    return std::nullopt;
  }
  return std::string(found->pw_name);
}

std::string originLabel(const HistoryOrigin& origin, std::uint32_t current_uid, const UserNameResolver& resolve_user) {
  switch (origin.kind) {
    case HistoryOriginKind::Packages: {
      if (!origin.uid || *origin.uid == current_uid) {
        return "Packages";
      }
      const std::optional<std::string> name = resolve_user ? resolve_user(*origin.uid) : std::nullopt;
      if (name && !name->empty()) {
        return std::format("Packages{}{}", kSeparator, *name);
      }
      return std::format("Packages{}uid {}", kSeparator, *origin.uid);
    }
    case HistoryOriginKind::Pacman:
      return "pacman";
    case HistoryOriginKind::Unknown:
      break;
  }
  return "unknown";
}

std::string summaryText(std::span<const HistoryAction> actions) {
  static constexpr std::array kOrder = {
      HistoryActionKind::Upgraded,    HistoryActionKind::Installed, HistoryActionKind::Downgraded,
      HistoryActionKind::Reinstalled, HistoryActionKind::Removed,
  };
  std::string text;
  for (const HistoryActionKind kind : kOrder) {
    std::size_t count = 0;
    for (const HistoryAction& action : actions) {
      count += action.kind == kind ? 1U : 0U;
    }
    if (count == 0) {
      continue;
    }
    if (!text.empty()) {
      text += kSeparator;
    }
    text += std::format("{} {}", count, actionKindLabel(kind));
  }
  return text;
}

std::string_view statusLabel(HistoryStatus status) noexcept {
  switch (status) {
    case HistoryStatus::Completed:
      return "Completed";
    case HistoryStatus::InProgress:
      return "In progress";
    case HistoryStatus::Interrupted:
      break;
  }
  return "Interrupted";
}

std::string_view actionKindLabel(HistoryActionKind kind) noexcept {
  switch (kind) {
    case HistoryActionKind::Installed:
      return "installed";
    case HistoryActionKind::Upgraded:
      return "upgraded";
    case HistoryActionKind::Downgraded:
      return "downgraded";
    case HistoryActionKind::Reinstalled:
      return "reinstalled";
    case HistoryActionKind::Removed:
      break;
  }
  return "removed";
}

LocalDay localDay(std::chrono::system_clock::time_point time) {
  const std::tm local = localTime(time);
  return LocalDay{.year = local.tm_year + 1900, .month = local.tm_mon + 1, .day = local.tm_mday};
}

std::string dayKey(const LocalDay& day) { return std::format("{:04}-{:02}-{:02}", day.year, day.month, day.day); }

std::string timeLabel(std::chrono::system_clock::time_point time) {
  const std::tm local = localTime(time);
  return std::format("{:02}:{:02}", local.tm_hour, local.tm_min);
}

}  // namespace holonight_packages_application
