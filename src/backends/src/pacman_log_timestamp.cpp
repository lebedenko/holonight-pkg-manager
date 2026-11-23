#include "pacman_log_timestamp.h"

#include <cerrno>
#include <cstddef>
#include <ctime>

namespace holonight_packages_backends {
namespace {

constexpr std::size_t kCurrentLength = 24;  // 2026-11-23T10:05:18+0200
constexpr std::size_t kLegacyLength = 16;   // 2019-01-01 12:00

constexpr bool isDigit(char character) noexcept { return character >= '0' && character <= '9'; }

bool digitsAt(std::string_view text, std::size_t first, std::size_t count) noexcept {
  for (std::size_t i = first; i < first + count; ++i) {
    if (!isDigit(text[i])) {
      return false;
    }
  }
  return true;
}

int number(std::string_view text, std::size_t first, std::size_t count) noexcept {
  int value = 0;
  for (std::size_t i = first; i < first + count; ++i) {
    value = (value * 10) + (text[i] - '0');
  }
  return value;
}

// YYYY-MM-DD at offset 0, shared by both shapes.
bool hasDateShape(std::string_view text) noexcept {
  return digitsAt(text, 0, 4) && text[4] == '-' && digitsAt(text, 5, 2) && text[7] == '-' && digitsAt(text, 8, 2);
}

bool hasCurrentShape(std::string_view text) noexcept {
  return text.size() == kCurrentLength && hasDateShape(text) && text[10] == 'T' && digitsAt(text, 11, 2) &&
         text[13] == ':' && digitsAt(text, 14, 2) && text[16] == ':' && digitsAt(text, 17, 2) &&
         (text[19] == '+' || text[19] == '-') && digitsAt(text, 20, 4);
}

bool hasLegacyShape(std::string_view text) noexcept {
  return text.size() == kLegacyLength && hasDateShape(text) && text[10] == ' ' && digitsAt(text, 11, 2) &&
         text[13] == ':' && digitsAt(text, 14, 2);
}

struct Fields {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
};

bool inRange(const Fields& fields) {
  const std::chrono::year_month_day date{std::chrono::year{fields.year},
                                         std::chrono::month{static_cast<unsigned>(fields.month)},
                                         std::chrono::day{static_cast<unsigned>(fields.day)}};
  return date.ok() && fields.hour < 24 && fields.minute < 60 && fields.second < 60;
}

Fields dateFields(std::string_view text) {
  return Fields{
      .year = number(text, 0, 4),
      .month = number(text, 5, 2),
      .day = number(text, 8, 2),
      .hour = number(text, 11, 2),
      .minute = number(text, 14, 2),
      .second = 0,
  };
}

std::optional<std::chrono::system_clock::time_point> checkedTimePoint(std::chrono::sys_seconds instant) {
  using TimePoint = std::chrono::system_clock::time_point;
  using std::chrono::seconds;
  if (instant < std::chrono::ceil<seconds>(TimePoint::min()) ||
      instant > std::chrono::floor<seconds>(TimePoint::max())) {
    return std::nullopt;
  }
  return TimePoint{instant};
}

std::optional<std::chrono::system_clock::time_point> parseCurrent(std::string_view text) {
  Fields fields = dateFields(text);
  fields.second = number(text, 17, 2);
  const int offset_hours = number(text, 20, 2);
  const int offset_minutes = number(text, 22, 2);
  if (!inRange(fields) || offset_hours > 23 || offset_minutes > 59) {
    return std::nullopt;
  }
  const std::chrono::sys_days days{std::chrono::year{fields.year} / fields.month / fields.day};
  const std::chrono::minutes offset{((text[19] == '-') ? -1 : 1) * ((offset_hours * 60) + offset_minutes)};
  // Local wall time = UTC + offset, so UTC = local - offset.
  return checkedTimePoint(days + std::chrono::hours{fields.hour} + std::chrono::minutes{fields.minute} +
                          std::chrono::seconds{fields.second} - offset);
}

std::optional<std::chrono::system_clock::time_point> parseLegacy(std::string_view text) {
  const Fields fields = dateFields(text);
  if (!inRange(fields)) {
    return std::nullopt;
  }
  std::tm local{};
  local.tm_year = fields.year - 1900;
  local.tm_mon = fields.month - 1;
  local.tm_mday = fields.day;
  local.tm_hour = fields.hour;
  local.tm_min = fields.minute;
  local.tm_isdst = -1;  // let the process TZ decide (REQ-F-015)
  errno = 0;
  const std::time_t seconds = std::mktime(&local);
  if (seconds == static_cast<std::time_t>(-1) && errno != 0) {
    return std::nullopt;
  }
  return checkedTimePoint(std::chrono::sys_seconds{std::chrono::seconds{seconds}});
}

}  // namespace

bool hasTimestampShape(std::string_view inside_brackets) noexcept {
  return hasCurrentShape(inside_brackets) || hasLegacyShape(inside_brackets);
}

std::optional<std::chrono::system_clock::time_point> parseTimestamp(std::string_view inside_brackets) {
  if (hasCurrentShape(inside_brackets)) {
    return parseCurrent(inside_brackets);
  }
  if (hasLegacyShape(inside_brackets)) {
    return parseLegacy(inside_brackets);
  }
  return std::nullopt;
}

}  // namespace holonight_packages_backends
