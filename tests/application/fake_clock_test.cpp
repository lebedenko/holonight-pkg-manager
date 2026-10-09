#include "fake_clock.h"

#include <gtest/gtest.h>
#include <vector>

namespace holonight_packages_testing {
namespace {

using std::chrono::milliseconds;

TEST(FakeClockTest, FiresTimersInDueOrderWithoutRealWaiting) {
  FakeClock clock;
  auto first = clock.makeTimer();
  auto second = clock.makeTimer();
  std::vector<int> order;
  second->start(milliseconds{50}, [&order] { order.push_back(2); });
  first->start(milliseconds{100}, [&order] { order.push_back(1); });

  clock.advance(milliseconds{49});
  EXPECT_TRUE(order.empty());
  clock.advance(milliseconds{1});
  EXPECT_EQ(order, (std::vector<int>{2}));
  clock.advance(milliseconds{1000});
  EXPECT_EQ(order, (std::vector<int>{2, 1}));
  EXPECT_FALSE(first->active());
}

TEST(FakeClockTest, TimerRearmedByCallbackFiresInsideTheSameWindow) {
  FakeClock clock;
  auto timer = clock.makeTimer();
  int fired = 0;
  std::function<void()> tick = [&] {
    ++fired;
    timer->start(milliseconds{10}, tick);
  };
  timer->start(milliseconds{10}, tick);
  clock.advance(milliseconds{100});
  EXPECT_EQ(fired, 10);
}

TEST(FakeClockTest, StoppedTimerDoesNotFire) {
  FakeClock clock;
  auto timer = clock.makeTimer();
  bool fired = false;
  timer->start(milliseconds{10}, [&fired] { fired = true; });
  timer->stop();
  clock.advance(milliseconds{100});
  EXPECT_FALSE(fired);
}

}  // namespace
}  // namespace holonight_packages_testing
