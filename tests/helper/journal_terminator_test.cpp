#include "HelperSignals.h"
#include "HelperTerminator.h"
#include "JournalLogger.h"

#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>

#include <algorithm>
#include <array>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <unistd.h>

namespace holonight_helper {
namespace {

namespace fs = std::filesystem;
using holonight_packages_transaction::LogLevel;
using ::testing::HasSubstr;
using ::testing::Not;

void pump(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

std::string drain(int descriptor) {
  std::string text;
  std::array<char, 512> buffer{};
  while (true) {
    const ssize_t count = ::read(descriptor, buffer.data(), buffer.size());
    if (count <= 0) {
      break;
    }
    text.append(buffer.data(), static_cast<std::size_t>(count));
  }
  return text;
}

TEST(JournalLoggerTest, LinesCarryTheJournalPriorityPrefix) {
  std::array<int, 2> fds{};
  ASSERT_EQ(::pipe2(fds.data(), O_NONBLOCK), 0);
  const auto sink = journalSinkTo(fds[1]);
  sink(LogLevel::Error, "broken");
  sink(LogLevel::Warning, "careful");
  sink(LogLevel::Info, "fine");
  ::close(fds[1]);
  EXPECT_EQ(drain(fds[0]), "<3>broken\n<4>careful\n<6>fine\n");
  ::close(fds[0]);
}

TEST(JournalLoggerTest, CredentialsInUrlsNeverReachTheJournal) {
  std::array<int, 2> fds{};
  ASSERT_EQ(::pipe2(fds.data(), O_NONBLOCK), 0);
  journalSinkTo(fds[1])(LogLevel::Error,
                        "failed retrieving file 'a' from https://user:secret@mirror.example/core/os : 404");
  ::close(fds[1]);
  const std::string text = drain(fds[0]);
  ::close(fds[0]);
  EXPECT_THAT(text, Not(HasSubstr("secret")));
  EXPECT_THAT(text, Not(HasSubstr("user:")));
  EXPECT_THAT(text, HasSubstr("https://***@mirror.example/core/os"));
}

TEST(JournalLoggerTest, AMessageWithNewlinesIsOneJournalLine) {
  std::array<int, 2> fds{};
  ASSERT_EQ(::pipe2(fds.data(), O_NONBLOCK), 0);
  journalSinkTo(fds[1])(LogLevel::Info, "first\nsecond\r\nthird");
  ::close(fds[1]);
  const std::string text = drain(fds[0]);
  ::close(fds[0]);
  EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 1);
}

TEST(HelperTerminatorTest, RemovesTheLockFileThenExitsAfterTheGracePeriod) {
  const fs::path lock = fs::temp_directory_path() / ("terminator-lock-" + std::to_string(::getpid()));
  {
    std::ofstream(lock) << ::getpid();
  }
  int exits = 0;
  HelperTerminator terminator(lock, [&exits] { ++exits; }, std::chrono::milliseconds(40));
  terminator.terminateAfterReply();
  EXPECT_FALSE(fs::exists(lock));  // the next package manager is not blocked
  EXPECT_EQ(exits, 0);             // the reply still has time to leave
  pump(150);
  EXPECT_EQ(exits, 1);
}

TEST(HelperTerminatorTest, PreservesAForeignLock) {
  const fs::path lock = fs::temp_directory_path() / ("foreign-lock-" + std::to_string(::getpid()));
  std::ofstream(lock) << "0";
  int exits = 0;
  HelperTerminator terminator(lock, [&exits] { ++exits; }, std::chrono::milliseconds(10));
  terminator.terminateAfterReply();
  EXPECT_TRUE(fs::exists(lock));
  pump(80);
  EXPECT_EQ(exits, 1);
  fs::remove(lock);
}

TEST(HelperTerminatorTest, AMissingLockFileIsNotAnError) {
  int exits = 0;
  HelperTerminator terminator("/definitely/not/here/db.lck", [&exits] { ++exits; }, std::chrono::milliseconds(10));
  terminator.terminateAfterReply();
  pump(80);
  EXPECT_EQ(exits, 1);
}

TEST(HelperSignalsTest, SigtermBecomesAQtSignal) {
  HelperSignals bridge;
  QSignalSpy spy(&bridge, &HelperSignals::terminationRequested);
  ::raise(SIGTERM);
  for (int i = 0; i < 50 && spy.isEmpty(); ++i) {
    pump(10);
  }
  EXPECT_EQ(spy.count(), 1);
}

}  // namespace
}  // namespace holonight_helper
