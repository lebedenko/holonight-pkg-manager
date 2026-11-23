// transaction-history REQ-NF-001..003: parse budgets and event-loop latency. Registered as a test only in Release
// builds (`task bench:history`); timings in a Debug build mean nothing.
#include "history_fixture.h"
#include "history_log_generator.h"
#include "holonight_packages_backends/pacman_log_history_source.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

using holonight_packages_backends::PacmanLogHistorySource;
using holonight_packages_backends::PacmanLogHistorySourceOptions;
using holonight_packages_domain::HistoryReadMode;
using holonight_test::GeneratedHistoryLog;
using holonight_test::generatedTransaction;
using holonight_test::generateHistoryLog;
using holonight_test::HistoryFixture;
using holonight_test::HistoryIoRecord;
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::duration<double, std::milli>;

constexpr double kFullParseBudgetMs = 500.0;
constexpr double kRefreshBudgetMs = 20.0;
constexpr double kEventDelayBudgetMs = 50.0;
constexpr std::size_t kMiB = std::size_t{1} << 20U;

double median(std::vector<double> values) {
  std::ranges::sort(values);
  return values.at(values.size() / 2);
}

double timeMs(const std::function<void()>& work) {
  const auto start = Clock::now();
  work();
  return Milliseconds(Clock::now() - start).count();
}

// The generated REQ-NF-001 log, written once per test into a temporary fixture.
class HistoryBench : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { log_ = std::make_unique<GeneratedHistoryLog>(generateHistoryLog()); }
  static void TearDownTestSuite() { log_.reset(); }

  void SetUp() override { fixture_.append(log_->text); }

  [[nodiscard]] std::unique_ptr<PacmanLogHistorySource> makeSource() const {
    return std::make_unique<PacmanLogHistorySource>(fixture_.options(record_));
  }

  [[nodiscard]] static const GeneratedHistoryLog& log() { return *log_; }
  [[nodiscard]] const HistoryFixture& fixture() const { return fixture_; }

 private:
  static std::unique_ptr<GeneratedHistoryLog> log_;
  HistoryFixture fixture_;
  std::shared_ptr<HistoryIoRecord> record_ = std::make_shared<HistoryIoRecord>();
};

std::unique_ptr<GeneratedHistoryLog> HistoryBench::log_;

TEST_F(HistoryBench, GeneratedLogHasTheReferenceShape) {
  EXPECT_EQ(log().lines, 60000U);
  EXPECT_EQ(static_cast<std::size_t>(std::ranges::count(log().text, '\n')), 60000U);
  EXPECT_GE(log().text.size(), 48 * kMiB / 10);
  EXPECT_LE(log().text.size(), 55 * kMiB / 10);
  EXPECT_EQ(makeSource()->read(HistoryReadMode::Full).append.size(), 1178U);
  // Deterministic: the same seed gives the same bytes.
  EXPECT_EQ(generateHistoryLog().text, log().text);
}

// REQ-NF-001: the median of five full parses, each on a fresh source, after one warm-up read.
TEST_F(HistoryBench, FullParseMedianIsUnderBudget) {
  ASSERT_EQ(makeSource()->read(HistoryReadMode::Full).append.size(), 1178U);
  std::vector<double> times;
  times.reserve(5);
  for (int run = 0; run < 5; ++run) {
    const auto source = makeSource();
    times.push_back(timeMs([&source] { static_cast<void>(source->read(HistoryReadMode::Full)); }));
  }
  const double result = median(times);
  std::cout << "[bench] full parse of " << log().text.size() << " bytes, " << log().lines << " lines: median " << result
            << " ms\n";
  EXPECT_LT(result, kFullParseBudgetMs);
}

// REQ-NF-002: one appended transaction of about 1 KiB, then an incremental refresh; and refreshes with no new bytes.
TEST_F(HistoryBench, IncrementalRefreshMedianIsUnderBudget) {
  const auto source = makeSource();
  ASSERT_EQ(source->read(HistoryReadMode::Full).append.size(), 1178U);
  std::mt19937_64 random(7);
  std::vector<double> appended;
  appended.reserve(20);
  for (std::size_t iteration = 0; iteration < 20; ++iteration) {
    const std::string transaction = generatedTransaction(random, iteration);
    EXPECT_GT(transaction.size(), 600U);
    EXPECT_LT(transaction.size(), 1600U);
    fixture().append(transaction);
    std::size_t added = 0;
    appended.push_back(timeMs([&] { added = source->read(HistoryReadMode::Incremental).append.size(); }));
    EXPECT_GE(added, 1U);
  }
  std::vector<double> idle;
  idle.reserve(20);
  for (int iteration = 0; iteration < 20; ++iteration) {
    idle.push_back(timeMs([&source] { static_cast<void>(source->read(HistoryReadMode::Incremental)); }));
  }
  std::cout << "[bench] append and refresh: median " << median(appended) << " ms; refresh with no new bytes: median "
            << median(idle) << " ms\n";
  EXPECT_LT(median(appended), kRefreshBudgetMs);
  EXPECT_LT(median(idle), kRefreshBudgetMs);
}

// Runs `start` (which begins the parse and returns when it is finished or handed to a worker) while a 10 ms precise
// timer on the main thread records how late each tick arrives. Sampling stops when `done()` turns true.
double largestEventDelayMs(const std::function<void()>& start, const std::function<bool()>& done) {
  constexpr auto kPeriod = std::chrono::milliseconds(10);
  double largest = 0.0;
  auto expected = Clock::now() + kPeriod;
  QEventLoop loop;
  QTimer timer;
  timer.setTimerType(Qt::PreciseTimer);
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
    const auto now = Clock::now();
    largest = std::max(largest, Milliseconds(now - expected).count());
    if (done()) {
      loop.quit();
      return;
    }
    // Re-arm from the expected time, so a late tick does not shift the schedule.
    while (expected <= now) {
      expected += kPeriod;
    }
    timer.start(std::chrono::duration_cast<std::chrono::milliseconds>(expected - now));
  });
  timer.start(kPeriod);
  QTimer::singleShot(0, &loop, start);
  loop.exec();
  return largest;
}

// REQ-NF-003: a full parse on a QtConcurrent worker leaves the main event loop responsive.
TEST_F(HistoryBench, ParseOnTheWorkerKeepsEventsUnder50Ms) {
  if (std::thread::hardware_concurrency() < 2) {
    GTEST_SKIP() << "needs at least two hardware threads";
  }
  const auto source = makeSource();
  double best = 0.0;
  for (int attempt = 0; attempt < 3; ++attempt) {
    QFuture<void> future;
    const double delay = largestEventDelayMs(
        [&] { future = QtConcurrent::run([&source] { static_cast<void>(source->read(HistoryReadMode::Full)); }); },
        [&future] { return future.isStarted() && future.isFinished(); });
    future.waitForFinished();
    std::cout << "[bench] worker parse, attempt " << attempt + 1 << ": largest event delay " << delay << " ms\n";
    best = attempt == 0 ? delay : std::min(best, delay);
    if (best < kEventDelayBudgetMs) {
      break;
    }
  }
  EXPECT_LT(best, kEventDelayBudgetMs);
}

// Sensitivity control for the probe: parsing on the main thread must show up as a late event. One parse may be
// shorter than the budget on a fast machine, so the control parses repeatedly for at least 80 ms in one go.
TEST_F(HistoryBench, ProbeSeesAParseOnTheMainThread) {
  const auto source = makeSource();
  bool finished = false;
  int parses = 0;
  const double delay = largestEventDelayMs(
      [&] {
        const auto start = Clock::now();
        while (Milliseconds(Clock::now() - start).count() < 80.0) {
          static_cast<void>(source->read(HistoryReadMode::Full));
          ++parses;
        }
        finished = true;
      },
      [&finished] { return finished; });
  std::cout << "[bench] main-thread parse (" << parses << " parses): largest event delay " << delay << " ms\n";
  EXPECT_GE(delay, kEventDelayBudgetMs);
}

// The manual NF-001 measurement on a real log, named only through this explicit environment variable.
TEST(HistoryBenchReference, ReferenceLog) {
  const char* path = std::getenv("HOLONIGHT_HISTORY_BENCH_LOG");
  if (path == nullptr || *path == '\0') {
    GTEST_SKIP() << "set HOLONIGHT_HISTORY_BENCH_LOG to a pacman log to measure it";
  }
  HistoryFixture fixture;
  fixture.writeConf(path, fixture.root() / "db");
  PacmanLogHistorySourceOptions options;
  options.pacman_conf_path = fixture.conf();
  std::vector<double> times;
  times.reserve(5);
  std::size_t entries = 0;
  for (int run = 0; run < 5; ++run) {
    PacmanLogHistorySource source(options);
    times.push_back(timeMs([&] { entries = source.read(HistoryReadMode::Full).append.size(); }));
  }
  std::cout << "[bench] reference log " << path << ": " << entries << " transactions, median " << median(times)
            << " ms\n";
  EXPECT_LT(median(times), kFullParseBudgetMs);
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
