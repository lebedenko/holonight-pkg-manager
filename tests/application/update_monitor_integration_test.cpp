#include "holonight_packages_application/update_monitor.h"
#include "holonight_packages_backends/alpm_update_source.h"

#include <QCryptographicHash>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <map>
#include <memory>

namespace holonight_packages_application {
namespace {

namespace fs = std::filesystem;

fs::path updatesFixture() { return fs::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman" / "updates"; }

struct FileState {
  QByteArray sha256;
  std::uintmax_t size = 0;
  fs::file_time_type modified;

  bool operator==(const FileState&) const = default;
};

std::map<fs::path, FileState> syncFileStates(const fs::path& sync_dir) {
  std::map<fs::path, FileState> states;
  for (const auto& entry : fs::directory_iterator(sync_dir)) {
    QFile file(QString::fromStdString(entry.path().string()));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    states[entry.path()] = FileState{
        .sha256 = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256),
        .size = fs::file_size(entry.path()),
        .modified = fs::last_write_time(entry.path()),
    };
  }
  return states;
}

TEST(UpdateMonitorIntegration, FixtureStatusMatchesAndSyncDatabasesAreNeverModified) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const fs::path root = fs::path(directory.path().toStdString()) / "db";
  fs::create_directories(root);
  fs::copy(updatesFixture() / "local", root / "local", fs::copy_options::recursive);
  fs::copy(updatesFixture() / "sync", root / "sync", fs::copy_options::recursive);
  const auto before = syncFileStates(root / "sync");

  const holonight_packages_backends::AlpmUpdateSourceOptions source_options{
      .databaseRoot = root,
      .databasePath = root,
      .pacmanConfPath = updatesFixture() / "pacman.conf",
  };
  auto source = std::make_shared<holonight_packages_backends::AlpmUpdateSource>(source_options);
  UpdateMonitor monitor(source, UpdateMonitorOptions{
                                    .watchPaths =
                                        {
                                            QString::fromStdString((root / "local").string()),
                                            QString::fromStdString((root / "sync").string()),
                                        },
                                    .debounce = std::chrono::milliseconds{20},
                                });
  QSignalSpy spy(&monitor, &UpdateMonitor::statusChanged);

  monitor.start();
  ASSERT_TRUE(spy.wait(5000));
  for (int round = 0; round < 3; ++round) {
    monitor.refresh();
    QTest::qWait(300);
  }

  const UpdateStatus& status = monitor.status();
  EXPECT_EQ(status.state, UpdateState::Ready);
  // alpha, dup and gamma are pending; ignoreme (IgnorePkg) and banana (IgnoreGroup) are ignored.
  EXPECT_EQ(status.updateCount, 3);
  EXPECT_EQ(status.ignoredCount, 2);
  EXPECT_GT(status.totalDownloadBytes, 0U);
  EXPECT_GT(status.dataAsOfEpochSeconds, 0);
  EXPECT_EQ(syncFileStates(root / "sync"), before);
}

}  // namespace
}  // namespace holonight_packages_application
