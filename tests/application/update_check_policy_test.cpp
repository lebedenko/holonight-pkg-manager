#include "holonight_packages_application/update_check_policy.h"

#include "fake_random.h"
#include "holonight_packages_application/update_check_runtime.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <limits>
#include <set>

namespace holonight_packages_application {
namespace {

using std::chrono::milliseconds;
using std::chrono::minutes;

TEST(UpdateCheckPolicyTest, ProductionTimerUsesPreciseDeadlines) {
  const QtOneShotTimer timer;
  EXPECT_EQ(timer.timerType(), Qt::PreciseTimer);
}

TEST(UpdateCheckPolicyTest, OversizedIntervalsAreCappedWithJitterHeadroom) {
  for (const auto* const raw : {"35782", "9223372036854775807", "999999999999999999999999999999"}) {
    const auto resolved = resolveCheckInterval(raw, UpdateCheckPolicy{});
    EXPECT_EQ(resolved.interval, minutes{35781});
    EXPECT_TRUE(resolved.warning);
    UpdateCheckPolicy policy{};
    policy.interval = resolved.interval;
    holonight_packages_testing::ScriptedRandom random;
    random.enqueue(600000);
    EXPECT_EQ(nextAutomaticDelay(policy, 0, random), milliseconds{2147460000});
  }
  EXPECT_FALSE(resolveCheckInterval("35781", UpdateCheckPolicy{}).warning);
}

TEST(UpdateCheckPolicyTest, ExtremePolicyDurationsCannotOverflowDelayGeneration) {
  UpdateCheckPolicy policy{};
  policy.interval = minutes{std::numeric_limits<long long>::max()};
  policy.maxJitter = policy.interval;
  policy.backoff[0] = policy.interval;
  holonight_packages_testing::ScriptedRandom random;
  random.enqueue(std::numeric_limits<long long>::max());
  const auto resolved = resolveCheckInterval("9223372036854775807", policy);
  EXPECT_EQ(resolved.interval, minutes{32538});
  EXPECT_TRUE(resolved.warning);
  EXPECT_LE(nextAutomaticDelay(policy, 0, random).count(), std::numeric_limits<int>::max());
  EXPECT_LE(nextAutomaticDelay(policy, 1, random).count(), std::numeric_limits<int>::max());
}

TEST(UpdateCheckPolicyTest, MissingValueGivesDefaultWithoutWarning) {
  const auto resolved = resolveCheckInterval(std::nullopt, UpdateCheckPolicy{});
  EXPECT_EQ(resolved.interval, minutes{360});
  EXPECT_FALSE(resolved.warning.has_value());
}

TEST(UpdateCheckPolicyTest, UnusableValuesGiveDefaultWithExactlyOneWarning) {
  for (const char* raw : {"", "abc", "0", "-5", "12x", " "}) {
    const auto resolved = resolveCheckInterval(std::string_view(raw), UpdateCheckPolicy{});
    EXPECT_EQ(resolved.interval, minutes{360}) << raw;
    EXPECT_TRUE(resolved.warning.has_value()) << raw;
  }
}

TEST(UpdateCheckPolicyTest, ValueBelowMinimumIsClampedWithOneWarning) {
  const auto resolved = resolveCheckInterval(std::string_view("5"), UpdateCheckPolicy{});
  EXPECT_EQ(resolved.interval, minutes{15});
  EXPECT_TRUE(resolved.warning.has_value());
}

TEST(UpdateCheckPolicyTest, ValidValueIsKeptWithoutWarning) {
  const auto resolved = resolveCheckInterval(std::string_view("120"), UpdateCheckPolicy{});
  EXPECT_EQ(resolved.interval, minutes{120});
  EXPECT_FALSE(resolved.warning.has_value());
  EXPECT_EQ(resolveCheckInterval(std::string_view(" 15 "), UpdateCheckPolicy{}).interval, minutes{15});
}

TEST(UpdateCheckPolicyTest, JitteredIntervalStaysInsideBoundsOver48Hours) {
  const UpdateCheckPolicy policy;
  holonight_packages_testing::SeededRandom random;
  std::set<long long> distinct;
  milliseconds elapsed{0};
  const milliseconds jitter = std::chrono::duration_cast<milliseconds>(policy.maxJitter);
  const milliseconds base = std::chrono::duration_cast<milliseconds>(policy.interval);
  while (elapsed < std::chrono::hours{48}) {
    const milliseconds delay = nextAutomaticDelay(policy, 0, random);
    EXPECT_GE(delay, base - jitter);
    EXPECT_LE(delay, base + jitter);
    distinct.insert(delay.count());
    elapsed += delay;
  }
  EXPECT_GT(distinct.size(), 1U);
}

TEST(UpdateCheckPolicyTest, JitterBoundIsCappedAtTenthOfInterval) {
  UpdateCheckPolicy policy;
  policy.interval = minutes{15};
  holonight_packages_testing::SeededRandom random;
  for (int index = 0; index < 200; ++index) {
    const milliseconds delay = nextAutomaticDelay(policy, 0, random);
    EXPECT_GE(delay, milliseconds{minutes{15}} - milliseconds{90'000});
    EXPECT_LE(delay, milliseconds{minutes{15}} + milliseconds{90'000});
  }
}

TEST(UpdateCheckPolicyTest, FailuresUseExactBackoffThenReturnToInterval) {
  const UpdateCheckPolicy policy;
  holonight_packages_testing::SeededRandom random;
  EXPECT_EQ(nextAutomaticDelay(policy, 1, random), milliseconds{minutes{5}});
  EXPECT_EQ(nextAutomaticDelay(policy, 2, random), milliseconds{minutes{15}});
  EXPECT_EQ(nextAutomaticDelay(policy, 3, random), milliseconds{minutes{60}});
  const milliseconds fourth = nextAutomaticDelay(policy, 4, random);
  EXPECT_GE(fourth, milliseconds{minutes{350}});
  EXPECT_LE(fourth, milliseconds{minutes{370}});
}

}  // namespace
}  // namespace holonight_packages_application
