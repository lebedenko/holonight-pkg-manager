#include "holonight_packages_backends/alpm_update_checker.h"

#include "file_repo_fixture.h"
#include "holonight_packages_backends/backend_capabilities_alpm.h"
#include "holonight_packages_persistence/cache_locations.h"

#include <QDebug>
#include <QtLogging>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace holonight_packages_backends {
namespace {

namespace fs = std::filesystem;
using holonight_packages_domain::UpdateCheckErrorCode;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_testing::FileRepoFixture;
using holonight_packages_testing::manifestOf;
using ::testing::ElementsAre;

AlpmUpdateCheckerOptions optionsFor(const FileRepoFixture& fixture) {
  return AlpmUpdateCheckerOptions{
      .databaseRoot = fixture.realDbPath(),
      .databasePath = fixture.realDbPath(),
      .pacmanConfPath = fixture.confPath(),
      .scratchRoot = fixture.scratchRoot(),
      .totalBound = std::chrono::seconds{30},
      .hooks = {},
  };
}

std::vector<std::string> namesOf(const UpdateSnapshot& snapshot) {
  std::vector<std::string> names;
  names.reserve(snapshot.updates.size());
  for (const auto& update : snapshot.updates) {
    names.push_back(update.name);
  }
  std::ranges::sort(names);
  return names;
}

std::vector<fs::path> runDirectories(const fs::path& scratchRoot) {
  std::vector<fs::path> runs;
  if (!fs::exists(scratchRoot)) {
    return runs;
  }
  for (const auto& entry : fs::directory_iterator(scratchRoot)) {
    if (entry.path().filename().string().starts_with("run-")) {
      runs.push_back(entry.path());
    }
  }
  return runs;
}

// Captures every log line from any thread while alive.
class LogCapture {
 public:
  LogCapture() : previous_(qInstallMessageHandler(&LogCapture::handle)) { instance_ = this; }
  LogCapture(const LogCapture&) = delete;
  LogCapture& operator=(const LogCapture&) = delete;
  LogCapture(LogCapture&&) = delete;
  LogCapture& operator=(LogCapture&&) = delete;
  ~LogCapture() {
    qInstallMessageHandler(previous_);
    instance_ = nullptr;
  }
  [[nodiscard]] std::vector<std::string> lines() const {
    const std::scoped_lock lock(mutex_);
    return lines_;
  }

 private:
  static void handle(QtMsgType /*type*/, const QMessageLogContext& /*context*/, const QString& message) {
    if (instance_ != nullptr) {
      const std::scoped_lock lock(instance_->mutex_);
      instance_->lines_.push_back(message.toStdString());
    }
  }
  static inline LogCapture* instance_ = nullptr;
  QtMessageHandler previous_ = nullptr;
  mutable std::mutex mutex_;
  std::vector<std::string> lines_;
};

class AlpmUpdateCheckerTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_NE(::geteuid(), 0U) << "the isolation tests must run as a normal user, not root"; }
};

// ---- T-024: happy path --------------------------------------------------------------------------------------

TEST_F(AlpmUpdateCheckerTest, NewerPackagesAreListedWithInstalledAndAvailableVersions) {
  const FileRepoFixture fixture;
  const AlpmUpdateChecker checker(optionsFor(fixture));

  const auto snapshot = checker.checkForUpdates();

  ASSERT_TRUE(snapshot.has_value()) << static_cast<int>(snapshot.error().code);
  EXPECT_TRUE(snapshot->databasesFound);
  EXPECT_THAT(namesOf(*snapshot), ElementsAre("alpha", "banana", "dup", "gamma", "ignoreme"));
  const auto alpha = std::ranges::find(snapshot->updates, "alpha", &holonight_packages_domain::PendingUpdate::name);
  ASSERT_NE(alpha, snapshot->updates.end());
  EXPECT_EQ(alpha->installedVersion, "1.0-1");
  EXPECT_EQ(alpha->availableVersion, "2.0-1");
  EXPECT_EQ(alpha->repository, "core");
  const auto banana = std::ranges::find(snapshot->updates, "banana", &holonight_packages_domain::PendingUpdate::name);
  EXPECT_TRUE(banana->ignored);
}

TEST_F(AlpmUpdateCheckerTest, NothingNewGivesSuccessWithAnEmptyList) {
  const FileRepoFixture fixture(FileRepoFixture::Mirror::NothingNew);
  const AlpmUpdateChecker checker(optionsFor(fixture));
  const auto snapshot = checker.checkForUpdates();
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_TRUE(snapshot->updates.empty());
  EXPECT_TRUE(snapshot->databasesFound);
}

TEST_F(AlpmUpdateCheckerTest, RetainedRunDirectoryHasLocalSymlinkAndItsOwnSyncCopy) {
  const FileRepoFixture fixture;
  const auto realBefore = manifestOf(fixture.realDbPath());
  const AlpmUpdateChecker checker(optionsFor(fixture));

  ASSERT_TRUE(checker.checkForUpdates().has_value());

  const auto runs = runDirectories(fixture.scratchRoot());
  ASSERT_EQ(runs.size(), 1U);
  const fs::path local = runs.front() / "local";
  ASSERT_TRUE(fs::is_symlink(local));
  EXPECT_EQ(fs::weakly_canonical(local), fs::weakly_canonical(fixture.realDbPath() / "local"));
  EXPECT_NE(manifestOf(runs.front() / "sync").at("core.db").sha256,
            manifestOf(fixture.realDbPath() / "sync").at("core.db").sha256);
  EXPECT_EQ(manifestOf(fixture.realDbPath()), realBefore) << "the real database path must stay byte-identical";
}

TEST_F(AlpmUpdateCheckerTest, ScratchPathFollowsXdgCacheHomeThenHome) {
  using holonight_packages_persistence::appCacheDir;
  const FileRepoFixture fixture;
  auto xdg = appCacheDir([&fixture](std::string_view name) -> std::optional<std::string> {
    if (name == "XDG_CACHE_HOME") {
      return fixture.cacheHome().string();
    }
    return "/does/not/matter";
  });
  ASSERT_TRUE(xdg.has_value());
  auto options = optionsFor(fixture);
  options.scratchRoot = *xdg / "checkdb";
  ASSERT_TRUE(AlpmUpdateChecker(options).checkForUpdates().has_value());
  EXPECT_EQ(runDirectories(fixture.cacheHome() / "holonight-packages" / "checkdb").size(), 1U);

  const auto home = appCacheDir([&fixture](std::string_view name) -> std::optional<std::string> {
    return name == "HOME" ? std::optional<std::string>(fixture.root().string()) : std::nullopt;
  });
  options.scratchRoot = *home / "checkdb";
  ASSERT_TRUE(AlpmUpdateChecker(options).checkForUpdates().has_value());
  EXPECT_EQ(runDirectories(fixture.root() / ".cache" / "holonight-packages" / "checkdb").size(), 1U);
}

TEST_F(AlpmUpdateCheckerTest, TheBackendReportsTheCheckCapability) {
  EXPECT_TRUE(alpmBackendCapabilities().canCheckForUpdates);
}

// ---- T-025: failure paths ---------------------------------------------------------------------------------

TEST_F(AlpmUpdateCheckerTest, PreExistingDatabaseLockGivesBusyQuickly) {
  const FileRepoFixture fixture;
  std::ofstream(fixture.realDbPath() / "db.lck") << "";
  const AlpmUpdateChecker checker(optionsFor(fixture));

  const auto start = std::chrono::steady_clock::now();
  const auto result = checker.checkForUpdates();

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, UpdateCheckErrorCode::Busy);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds{1});
}

TEST_F(AlpmUpdateCheckerTest, DatabaseLockCreatedDuringTheCopyGivesBusy) {
  const FileRepoFixture fixture;
  auto options = optionsFor(fixture);
  options.hooks.afterCopyStarted = [&fixture] { std::ofstream(fixture.realDbPath() / "db.lck") << ""; };
  const AlpmUpdateChecker checker(options);

  const auto result = checker.checkForUpdates();

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, UpdateCheckErrorCode::Busy);
}

TEST_F(AlpmUpdateCheckerTest, ASyncDatabaseChangedDuringTheCopyGivesBusy) {
  const FileRepoFixture fixture;
  auto options = optionsFor(fixture);
  options.hooks.afterCopyStarted = [&fixture] {
    fs::last_write_time(fixture.realDbPath() / "sync" / "core.db", fs::file_time_type::clock::now());
  };
  const auto result = AlpmUpdateChecker(options).checkForUpdates();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, UpdateCheckErrorCode::Busy);
}

TEST_F(AlpmUpdateCheckerTest, MissingLocalMirrorDirectoryIsRepositoryUnreachable) {
  const FileRepoFixture fixture;
  fixture.writeConf({"file://" + (fixture.root() / "missing").string()});
  const AlpmUpdateChecker checker(optionsFor(fixture));

  const auto result = checker.checkForUpdates();

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, UpdateCheckErrorCode::RepositoryUnreachable);
  EXPECT_FALSE(result.error().message().empty());
}

TEST_F(AlpmUpdateCheckerTest, UnsignedDatabaseUnderSigLevelRequiredFailsWithoutASnapshot) {
  const FileRepoFixture fixture;
  fixture.writeConf({"file://" + fixture.mirrorDir().string()}, "Required");
  const AlpmUpdateChecker checker(optionsFor(fixture));

  const auto result = checker.checkForUpdates();

  EXPECT_FALSE(result.has_value());
}

TEST_F(AlpmUpdateCheckerTest, IncludedRepeatedRequiredSignaturesRejectUnsignedFixture) {
  const FileRepoFixture fixture;
  fixture.writeConf({"file://" + fixture.mirrorDir().string()}, "Never");
  const auto options = optionsFor(fixture);
  const auto include = fixture.root() / "signature.conf";
  std::ofstream(include) << "SigLevel = DatabaseRequired\nSigLevel = PackageTrustAll\n";
  // Append in the repository section; a later partial directive must retain DatabaseRequired.
  std::ofstream(options.pacmanConfPath, std::ios::app) << "Include = " << include.string() << "\n";
  EXPECT_FALSE(AlpmUpdateChecker(options).checkForUpdates());
}

TEST_F(AlpmUpdateCheckerTest, NoCheckupdatesHelperIsEverRun) {
  const FileRepoFixture fixture;
  const fs::path bin = fixture.root() / "bin";
  fs::create_directories(bin);
  const fs::path marker = fixture.root() / "helper-was-run";
  for (const char* name : {"checkupdates", "pacman", "pacman-conf"}) {
    std::ofstream(bin / name) << "#!/bin/sh\ntouch '" << marker.string() << "'\n";
    fs::permissions(bin / name, fs::perms::owner_all);
  }
  const QByteArray oldPath = qgetenv("PATH");
  qputenv("PATH", (QByteArray::fromStdString(bin.string()) + ":" + oldPath));

  const auto result = AlpmUpdateChecker(optionsFor(fixture)).checkForUpdates();

  qputenv("PATH", oldPath);
  EXPECT_TRUE(result.has_value());
  EXPECT_FALSE(fs::exists(marker));
}

TEST_F(AlpmUpdateCheckerTest, CredentialsInAServerUrlNeverReachTheLogs) {
  const FileRepoFixture fixture;
  const fs::path odd = fixture.root() / "odd?password=hunter2";
  fs::create_directories(odd);
  fixture.writeConf({"file://" + odd.string()});
  const LogCapture logs;

  const auto result = AlpmUpdateChecker(optionsFor(fixture)).checkForUpdates();
  EXPECT_FALSE(result.has_value());

  for (const std::string& line : logs.lines()) {
    EXPECT_EQ(line.find("hunter2"), std::string::npos) << line;
    EXPECT_EQ(line.find("password"), std::string::npos) << line;
    EXPECT_EQ(line.find("file://"), std::string::npos) << line;
  }
}

// ---- T-026: total bound -------------------------------------------------------------------------------------

TEST_F(AlpmUpdateCheckerTest, NeverCompletingFifoDatabaseIsAbandonedWithinTheTotalBound) {
  const FileRepoFixture fixture;
  const fs::path blocked = fixture.mirrorDir() / "core.db";
  fs::remove(blocked);
  ASSERT_EQ(::mkfifo(blocked.c_str(), 0600), 0);
  std::atomic<int> finished{0};
  auto options = optionsFor(fixture);
  options.totalBound = std::chrono::seconds{2};
  options.hooks.onBodyFinished = [&finished] { ++finished; };
  const AlpmUpdateChecker checker(options);

  const auto start = std::chrono::steady_clock::now();
  const auto result = checker.checkForUpdates();
  const auto elapsed = std::chrono::steady_clock::now() - start;

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, UpdateCheckErrorCode::NetworkUnavailable);
  EXPECT_LT(elapsed, std::chrono::seconds{7});
  EXPECT_GE(elapsed, std::chrono::seconds{2});

  // While the wedged worker holds the scratch lock, another check reports Busy instead of piling up.
  const auto second = checker.checkForUpdates();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code, UpdateCheckErrorCode::Busy);

  // Unblock the FIFO (open for writing, then close) so the detached worker can finish before the fixture goes.
  const int writer =
      ::open(blocked.c_str(), O_WRONLY | O_CLOEXEC);  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  if (writer >= 0) {
    ::close(writer);
  }
  // Both workers (the wedged one and the one that found the lock held) must be done before the fixture goes.
  for (int waited = 0; finished.load() < 2 && waited < 2000; ++waited) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  EXPECT_EQ(finished.load(), 2);
}

// ---- T-027: isolation -----------------------------------------------------------------------------------------

TEST_F(AlpmUpdateCheckerTest, RealDatabasePathIsByteIdenticalAfterSuccessAndAfterFailure) {
  const FileRepoFixture fixture;
  const auto before = manifestOf(fixture.realDbPath());

  ASSERT_TRUE(AlpmUpdateChecker(optionsFor(fixture)).checkForUpdates().has_value());
  EXPECT_EQ(manifestOf(fixture.realDbPath()), before);

  fixture.writeConf({"file://" + fixture.mirrorDir().string()}, "Required");
  EXPECT_FALSE(AlpmUpdateChecker(optionsFor(fixture)).checkForUpdates().has_value());
  EXPECT_EQ(manifestOf(fixture.realDbPath()), before);
}

TEST_F(AlpmUpdateCheckerTest, ReadOnlyRealDatabasePathIsEnoughForACheck) {
  const FileRepoFixture fixture;
  for (const auto& entry : fs::recursive_directory_iterator(fixture.realDbPath())) {
    if (!entry.is_symlink()) {
      fs::permissions(entry.path(),
                      entry.is_directory() ? fs::perms::owner_read | fs::perms::owner_exec : fs::perms::owner_read,
                      fs::perm_options::replace);
    }
  }
  fs::permissions(fixture.realDbPath(), fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
  const auto before = manifestOf(fixture.realDbPath());

  const auto result = AlpmUpdateChecker(optionsFor(fixture)).checkForUpdates();

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(manifestOf(fixture.realDbPath()), before);
  // Restore write permission so the temporary directory can be removed.
  fs::permissions(fixture.realDbPath(), fs::perms::owner_all, fs::perm_options::replace);
  for (const auto& entry : fs::recursive_directory_iterator(fixture.realDbPath())) {
    if (!entry.is_symlink()) {
      fs::permissions(entry.path(), fs::perms::owner_all, fs::perm_options::replace);
    }
  }
}

TEST_F(AlpmUpdateCheckerTest, PlantedStaleCorruptAndStrayEntriesAreNeverRead) {
  const FileRepoFixture clean;
  const auto baseline = AlpmUpdateChecker(optionsFor(clean)).checkForUpdates();
  ASSERT_TRUE(baseline.has_value());

  const FileRepoFixture planted;
  const fs::path root = planted.scratchRoot();
  fs::create_directories(root);
  fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace);
  // A corrupt run directory that claims to be newer, a stray file, and a retained directory that disagrees with the
  // real databases (an old-looking sync database containing garbage).
  fs::create_directories(root / "run-corrupt" / "sync");
  std::ofstream(root / "run-corrupt" / "sync" / "core.db") << "not a database";
  fs::create_directories(root / "run-retained" / "sync");
  fs::copy_file(clean.mirrorDir() / "core.db", root / "run-retained" / "sync" / "core.db");
  std::ofstream(root / "stray.txt") << "stray";

  const auto result = AlpmUpdateChecker(optionsFor(planted)).checkForUpdates();

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(namesOf(*result), namesOf(*baseline));
  EXPECT_EQ(runDirectories(root).size(), 1U);
  EXPECT_FALSE(fs::exists(root / "stray.txt"));
}

TEST_F(AlpmUpdateCheckerTest, ThreeChecksOneFailingLeaveExactlyOneRunDirectory) {
  const FileRepoFixture fixture;
  const AlpmUpdateChecker checker(optionsFor(fixture));
  ASSERT_TRUE(checker.checkForUpdates().has_value());
  fixture.writeConf({"file://" + (fixture.root() / "missing").string()});
  EXPECT_FALSE(checker.checkForUpdates().has_value());
  fixture.writeConf({"file://" + fixture.mirrorDir().string()});
  ASSERT_TRUE(checker.checkForUpdates().has_value());
  EXPECT_EQ(runDirectories(fixture.scratchRoot()).size(), 1U);
}

TEST_F(AlpmUpdateCheckerTest, SweepStaleReducesLeftoversToTheNewestRunDirectory) {
  const FileRepoFixture fixture;
  const fs::path root = fixture.scratchRoot();
  fs::create_directories(root / "run-aaaaaa");
  fs::create_directories(root / "run-bbbbbb");
  fs::last_write_time(root / "run-aaaaaa", fs::file_time_type::clock::now() - std::chrono::hours{10});
  fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace);

  AlpmUpdateChecker(optionsFor(fixture)).sweepStale();

  EXPECT_THAT(runDirectories(root), ElementsAre(root / "run-bbbbbb"));
}

}  // namespace
}  // namespace holonight_packages_backends
