#include "holonight_packages_application/update_check_policy.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace holonight_packages_application {

namespace {

// Search in minutes, so no multiplication can overflow even for hostile policy/input values.
long long maximumInterval(const UpdateCheckPolicy& policy) {
  constexpr long long limit = std::numeric_limits<int>::max() / 60000;
  long long low = 0;
  long long high = limit;
  while (low < high) {
    const auto middle = low + ((high - low + 1) / 2);
    const auto jitter = (std::min)((std::max)(0LL, static_cast<long long>(policy.maxJitter.count())), middle / 10);
    if (middle + jitter <= limit) {
      low = middle;
    } else {
      high = middle - 1;
    }
  }
  return low;
}

std::chrono::milliseconds intervalWithJitter(const UpdateCheckPolicy& policy, RandomSource& random) {
  const auto minutes = std::clamp(static_cast<long long>(policy.interval.count()), 0LL, maximumInterval(policy));
  const auto jitter = (std::min)((std::max)(0LL, static_cast<long long>(policy.maxJitter.count())), minutes / 10);
  const auto bound = jitter * 60000;
  const auto offset = bound > 0 ? random.uniform(-bound, bound) : 0;
  return std::chrono::milliseconds{(minutes * 60000) + std::clamp(static_cast<long long>(offset), -bound, bound)};
}

std::string_view trimmed(std::string_view text) {
  constexpr std::string_view kSpace = " \t\r\n";
  const std::size_t begin = text.find_first_not_of(kSpace);
  if (begin == std::string_view::npos) {
    return {};
  }
  const std::size_t end = text.find_last_not_of(kSpace);
  return text.substr(begin, end - begin + 1);
}

}  // namespace

std::chrono::milliseconds nextAutomaticDelay(const UpdateCheckPolicy& policy, int consecutiveFailures,
                                             RandomSource& random) {
  if (consecutiveFailures >= 1 && std::cmp_less_equal(consecutiveFailures, policy.backoff.size())) {
    const auto minutes =
        std::clamp(static_cast<long long>(policy.backoff.at(static_cast<std::size_t>(consecutiveFailures) - 1).count()),
                   0LL, static_cast<long long>(std::numeric_limits<int>::max() / 60000));
    return std::chrono::milliseconds{minutes * 60000};
  }
  return intervalWithJitter(policy, random);
}

IntervalResolution resolveCheckInterval(std::optional<std::string_view> raw, const UpdateCheckPolicy& policy) {
  if (!raw.has_value()) {
    return {.interval = policy.interval, .warning = std::nullopt};
  }
  const std::string_view text = trimmed(*raw);
  long long minutes = 0;
  std::size_t used = 0;
  try {
    minutes = std::stoll(std::string(text), &used);
  } catch (const std::out_of_range&) {
    auto digits = text;
    if (digits.starts_with('+')) {
      digits.remove_prefix(1);
    }
    if (!digits.empty() && std::ranges::all_of(digits, [](char digit) { return digit >= '0' && digit <= '9'; })) {
      minutes = std::numeric_limits<long long>::max();
      used = text.size();
    }
  } catch (const std::invalid_argument&) {
    used = 0;
  }
  if (text.empty() || used != text.size() || minutes <= 0) {
    return {
        .interval = policy.interval,
        .warning = "Invalid check_interval_minutes value '" + std::string(text) + "'; using the default of " +
                   std::to_string(policy.interval.count()) + " minutes.",
    };
  }
  const auto maximum = maximumInterval(policy);
  if (minutes > maximum) {
    return {
        .interval = std::chrono::minutes{maximum},
        .warning = "check_interval_minutes exceeds the maximum; using " + std::to_string(maximum) + " minutes.",
    };
  }
  if (minutes < policy.minInterval.count()) {
    return {
        .interval = std::chrono::minutes{(std::min)(static_cast<long long>(policy.minInterval.count()), maximum)},
        .warning = "check_interval_minutes " + std::to_string(minutes) + " is below the minimum; using " +
                   std::to_string(policy.minInterval.count()) + " minutes.",
    };
  }
  return {.interval = std::chrono::minutes{minutes}, .warning = std::nullopt};
}

}  // namespace holonight_packages_application
