#include "holonight_packages_persistence/json_update_snapshot_store.h"

#include "fake_clock.h"
#include "fake_update_checker.h"
#include "holonight_packages_application/update_check_service.h"
#include "holonight_packages_persistence/cache_locations.h"
#include "update_check_test_support.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace holonight_packages_persistence {
namespace {

namespace fs = std::filesystem;
using holonight_packages_application::UpdateCheckService;
using holonight_packages_domain::CheckedSnapshot;
using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::SnapshotFileState;
using holonight_packages_domain::UpdateSnapshot;
using std::chrono::seconds;
using std::chrono::system_clock;

std::string slurp(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void spit(const fs::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

CheckedSnapshot sample(int updates = 2, long long fetched = 1'760'000'000) {
  CheckedSnapshot result;
  result.fetchedAt = system_clock::time_point{seconds{fetched}};
  result.snapshot.dataAsOf = system_clock::time_point{seconds{fetched - 500}};
  for (int index = 0; index < updates; ++index) {
    result.snapshot.updates.push_back(PendingUpdate{
        .name = "pkg" + std::to_string(index),
        .installedVersion = "1.0-1",
        .availableVersion = "2.0-1",
        .repository = "core",
        .downloadSizeBytes = 2048,
        .installedSizeDeltaBytes = -500,
        .ignored = index == 1,
    });
  }
  return result;
}

class JsonUpdateSnapshotStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    root_ = fs::path(dir_.path().toStdString());
    file_ = root_ / "cache" / "update-snapshot.json";
  }
  void TearDown() override {
    std::error_code ignored;
    fs::permissions(root_ / "cache", fs::perms::owner_all, fs::perm_options::replace, ignored);
  }

  QTemporaryDir dir_;
  fs::path root_;
  fs::path file_;
};

TEST_F(JsonUpdateSnapshotStoreTest, TimestampConversionRejectsValuesBeyondClockRange) {
  JsonUpdateSnapshotStore store(file_);
  ASSERT_TRUE(store.save(sample()));
  const auto original = QJsonDocument::fromJson(QByteArray::fromStdString(slurp(file_))).object();
  const auto maximum = std::chrono::duration_cast<seconds>(system_clock::duration::max()).count();
  for (const auto* const key : {"fetchedAt", "dataAsOf"}) {
    auto root = original;
    root[key] = static_cast<double>(maximum);
    spit(file_, QJsonDocument(root).toJson().toStdString());
    ASSERT_TRUE(store.load());
    EXPECT_EQ(store.load()->state, SnapshotFileState::Valid);
    root[key] = static_cast<double>(maximum + 1);
    spit(file_, QJsonDocument(root).toJson().toStdString());
    EXPECT_EQ(store.load()->state, SnapshotFileState::Invalid);
  }
}

// ---- T-016 ------------------------------------------------------------------------------------------------------

TEST_F(JsonUpdateSnapshotStoreTest, SavedFileHoldsSchemaVersionFetchedAtCountAndUpdates) {
  JsonUpdateSnapshotStore store(file_);
  ASSERT_TRUE(store.save(sample()).has_value());

  const QJsonObject root = QJsonDocument::fromJson(QByteArray::fromStdString(slurp(file_))).object();
  EXPECT_EQ(root["schemaVersion"].toInt(), 1);
  EXPECT_EQ(root["fetchedAt"].toDouble(), 1'760'000'000);
  EXPECT_EQ(root["count"].toInt(), 1);  // one of the two rows is ignored
  EXPECT_EQ(root["updates"].toArray().size(), 2);
  struct stat info{};
  ASSERT_EQ(::stat(file_.c_str(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0600U);
  ASSERT_EQ(::stat(file_.parent_path().c_str(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0700U);
}

TEST_F(JsonUpdateSnapshotStoreTest, SaveThenLoadRoundTripsEverything) {
  JsonUpdateSnapshotStore store(file_);
  const CheckedSnapshot original = sample(3);
  ASSERT_TRUE(store.save(original).has_value());
  const auto loaded = store.load();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->state, SnapshotFileState::Valid);
  ASSERT_TRUE(loaded->snapshot.has_value());
  EXPECT_EQ(*loaded->snapshot, original);
}

TEST_F(JsonUpdateSnapshotStoreTest, FaultBeforeRenameLeavesPreviousFileByteIdenticalAndNoPartialTarget) {
  JsonUpdateSnapshotStore good(file_);
  ASSERT_TRUE(good.save(sample(1)).has_value());
  const std::string before = slurp(file_);

  JsonUpdateSnapshotStore faulty(file_, {.beforeRename = [] { return false; }});
  EXPECT_FALSE(faulty.save(sample(5, 1'760'000'999)).has_value());

  EXPECT_EQ(slurp(file_), before);
  int entries = 0;
  for (const auto& entry : fs::directory_iterator(file_.parent_path())) {
    ++entries;
    EXPECT_EQ(entry.path(), file_) << "stray temporary file left behind";
  }
  EXPECT_EQ(entries, 1);
}

TEST_F(JsonUpdateSnapshotStoreTest, FaultBeforeFirstWriteLeavesNoTarget) {
  JsonUpdateSnapshotStore faulty(file_, {.beforeRename = [] { return false; }});
  EXPECT_FALSE(faulty.save(sample()).has_value());
  EXPECT_FALSE(fs::exists(file_));
}

TEST_F(JsonUpdateSnapshotStoreTest, AppCacheDirPrefersXdgThenHome) {
  const auto xdg = appCacheDir([](std::string_view name) -> std::optional<std::string> {
    if (name == "XDG_CACHE_HOME") {
      return "/x/cache";
    }
    return "/home/u";
  });
  EXPECT_EQ(xdg, fs::path("/x/cache/holonight-packages"));
  const auto home = appCacheDir([](std::string_view name) -> std::optional<std::string> {
    if (name == "HOME") {
      return "/home/u";
    }
    return std::nullopt;
  });
  EXPECT_EQ(home, fs::path("/home/u/.cache/holonight-packages"));
  const auto emptyXdg = appCacheDir([](std::string_view name) -> std::optional<std::string> {
    return name == "XDG_CACHE_HOME" ? std::optional<std::string>("") : std::optional<std::string>("/h");
  });
  EXPECT_EQ(emptyXdg, fs::path("/h/.cache/holonight-packages"));
  EXPECT_FALSE(appCacheDir([](std::string_view) { return std::optional<std::string>(); }).has_value());
}

TEST_F(JsonUpdateSnapshotStoreTest, ReadOnlyCacheDirectoryPreservesThePublishedResult) {
  ASSERT_NE(::geteuid(), 0U) << "root bypasses directory permissions; run this test as a normal user";
  JsonUpdateSnapshotStore store(file_);
  ASSERT_TRUE(store.save(sample(1)).has_value());
  const std::string before = slurp(file_);
  fs::permissions(file_.parent_path(), fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);

  auto checker = std::make_shared<holonight_packages_testing::FakeUpdateChecker>();
  checker->enqueue(sample(4).snapshot);
  auto clock = std::make_shared<holonight_packages_testing::FakeClock>();
  UpdateCheckService service(
      {
          .checker = checker,
          .store = std::make_shared<JsonUpdateSnapshotStore>(file_),
          .clock = clock,
          .capabilities = {.canCheckForUpdates = true},
      },
      holonight_packages_application::UpdateCheckPolicy{});
  service.start();
  const auto previousCount = service.status().count;
  service.requestCheck(holonight_packages_application::CheckOrigin::Automatic);
  ASSERT_TRUE(holonight_packages_testing::waitIdle(service));

  EXPECT_EQ(slurp(file_), before);
  EXPECT_EQ(service.status().count, previousCount);
  EXPECT_FALSE(service.status().lastCheckSucceeded);
}

// ---- T-017 ------------------------------------------------------------------------------------------------------

TEST_F(JsonUpdateSnapshotStoreTest, MissingFileLoadsAsAbsentAndStaysMissing) {
  JsonUpdateSnapshotStore store(file_);
  const auto loaded = store.load();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->state, SnapshotFileState::Absent);
  EXPECT_FALSE(loaded->snapshot.has_value());
  EXPECT_FALSE(fs::exists(file_));
  EXPECT_FALSE(fs::exists(file_.parent_path()));
}

TEST_F(JsonUpdateSnapshotStoreTest, InvalidFixturesLoadAsInvalidAndAreLeftByteIdentical) {
  JsonUpdateSnapshotStore writer(file_);
  ASSERT_TRUE(writer.save(sample()).has_value());
  const std::string valid = slurp(file_);

  const std::map<std::string, std::string> fixtures{
      {"truncated", valid.substr(0, valid.size() / 2)},
      {"empty", ""},
      {"not-an-object", "[1,2,3]"},
      {
          "unknown-schema",
          [&valid] {
            std::string copy = valid;
            copy.replace(copy.find("\"schemaVersion\": 1"), 18, "\"schemaVersion\": 2");
            return copy;
          }(),
      },
      {
          "count-mismatch",
          [&valid] {
            std::string copy = valid;
            copy.replace(copy.find("\"count\": 1"), 10, "\"count\": 9");
            return copy;
          }(),
      },
      {
          "wrong-type",
          [&valid] {
            std::string copy = valid;
            copy.replace(copy.find("\"fetchedAt\""), 11, R"("fetchedAt": "x", "y")");
            return copy;
          }(),
      },
  };
  for (const auto& [name, content] : fixtures) {
    spit(file_, content);
    JsonUpdateSnapshotStore reader(file_);
    const auto loaded = reader.load();
    ASSERT_TRUE(loaded.has_value()) << name;
    EXPECT_EQ(loaded->state, SnapshotFileState::Invalid) << name;
    EXPECT_FALSE(loaded->snapshot.has_value()) << name;
    EXPECT_EQ(slurp(file_), content) << name << " must stay untouched";
  }
}

TEST_F(JsonUpdateSnapshotStoreTest, OversizedFileIsInvalid) {
  fs::create_directories(file_.parent_path());
  spit(file_, std::string(JsonUpdateSnapshotStore::kMaxFileBytes + 1, ' '));
  JsonUpdateSnapshotStore store(file_);
  const auto loaded = store.load();
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->state, SnapshotFileState::Invalid);
}

TEST_F(JsonUpdateSnapshotStoreTest, DiscardInvalidRemovesOnlyAnInvalidFile) {
  JsonUpdateSnapshotStore store(file_);
  ASSERT_TRUE(store.save(sample()).has_value());
  const std::string valid = slurp(file_);
  ASSERT_TRUE(store.discardInvalid().has_value());
  EXPECT_EQ(slurp(file_), valid);

  spit(file_, "{ nope");
  ASSERT_TRUE(store.discardInvalid().has_value());
  EXPECT_FALSE(fs::exists(file_));
  EXPECT_TRUE(store.discardInvalid().has_value());  // nothing to do when absent
}

TEST_F(JsonUpdateSnapshotStoreTest, RestartMakesTheCountAvailableBeforeTheFirstCheckerCall) {
  const CheckedSnapshot original = sample(3, 1'760'000'123);
  {
    JsonUpdateSnapshotStore writer(file_);
    ASSERT_TRUE(writer.save(original).has_value());
  }

  auto checker = std::make_shared<holonight_packages_testing::FakeUpdateChecker>();
  UpdateCheckService service(
      {
          .checker = checker,
          .store = std::make_shared<JsonUpdateSnapshotStore>(file_),
          .clock = std::make_shared<holonight_packages_testing::FakeClock>(),
          .capabilities = {.canCheckForUpdates = true},
      },
      holonight_packages_application::UpdateCheckPolicy{});
  QSignalSpy adopted(&service, &UpdateCheckService::snapshotAdopted);
  service.start();

  EXPECT_EQ(adopted.count(), 1);
  EXPECT_EQ(checker->callCount(), 0);
  EXPECT_EQ(service.status().snapshotFetchedAt, original.fetchedAt);
  EXPECT_EQ(*service.status().count, 2);
}

}  // namespace
}  // namespace holonight_packages_persistence
