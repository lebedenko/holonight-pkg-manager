#include "SnapshotFileReader.h"
#include "holonight_packages_persistence/json_update_snapshot_store.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace {

namespace fs = std::filesystem;
using holonight_packages_domain::CheckedSnapshot;
using holonight_packages_domain::PendingUpdate;
using holonight_packages_persistence::JsonUpdateSnapshotStore;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::system_clock;

constexpr int kWaitMs = 5000;

// Counts every writer-side call so a test can prove the reader never makes one.
class WriterCallCountingStore final : public holonight_packages_domain::UpdateSnapshotStore {
 public:
  explicit WriterCallCountingStore(fs::path file) : inner_(std::move(file)) {}
  [[nodiscard]] std::expected<holonight_packages_domain::SnapshotLoad, holonight_packages_domain::SnapshotStoreError>
  load() const override {
    return inner_.load();
  }
  [[nodiscard]] std::expected<void, holonight_packages_domain::SnapshotStoreError> save(
      const CheckedSnapshot& snapshot) override {
    ++writes_;
    return inner_.save(snapshot);
  }
  [[nodiscard]] std::expected<void, holonight_packages_domain::SnapshotStoreError> discardInvalid() override {
    ++writes_;
    return inner_.discardInvalid();
  }
  [[nodiscard]] int writeCount() const { return writes_.load(); }

 private:
  std::atomic<int> writes_{0};
  JsonUpdateSnapshotStore inner_;
};

CheckedSnapshot sample(int count, long long fetched) {
  CheckedSnapshot result;
  result.fetchedAt = system_clock::time_point{seconds{fetched}};
  result.snapshot.dataAsOf = system_clock::time_point{seconds{fetched - 100}};
  for (int index = 0; index < count; ++index) {
    result.snapshot.updates.push_back(PendingUpdate{.name = "p" + std::to_string(index)});
  }
  return result;
}

std::string slurp(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

class SnapshotFileReaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    file_ = fs::path(dir_.path().toStdString()) / "cache" / "holonight-packages" / "update-snapshot.json";
    store_ = std::make_shared<WriterCallCountingStore>(file_);
    reader_ = std::make_unique<SnapshotFileReader>(store_, file_, milliseconds{30});
  }
  QTemporaryDir dir_;
  fs::path file_;
  std::shared_ptr<WriterCallCountingStore> store_;
  std::unique_ptr<SnapshotFileReader> reader_;
};

TEST_F(SnapshotFileReaderTest, AtomicRenameOfAValidSnapshotIsPickedUpWithoutAnyDBusSignal) {
  reader_->start();
  QSignalSpy spy(reader_.get(), &SnapshotFileReader::snapshotRead);
  EXPECT_EQ(spy.count(), 0);

  JsonUpdateSnapshotStore writer(file_);  // the writer: temp file + rename, creating the directories
  ASSERT_TRUE(writer.save(sample(3, 1'760'000'000)).has_value());

  ASSERT_TRUE(spy.wait(kWaitMs));
  const auto snapshot = spy.takeFirst().at(0).value<CheckedSnapshot>();
  EXPECT_EQ(snapshot.snapshot.updates.size(), 3U);
  EXPECT_EQ(snapshot.fetchedAt, system_clock::time_point{seconds{1'760'000'000}});
}

TEST_F(SnapshotFileReaderTest, ASecondRewriteIsPickedUpAndAnIdenticalOneIsNot) {
  JsonUpdateSnapshotStore writer(file_);
  ASSERT_TRUE(writer.save(sample(1, 1'760'000'000)).has_value());
  reader_->start();
  QSignalSpy spy(reader_.get(), &SnapshotFileReader::snapshotRead);
  EXPECT_EQ(spy.count(), 0) << "start() already emitted before the spy existed";

  ASSERT_TRUE(writer.save(sample(2, 1'760'000'500)).has_value());
  ASSERT_TRUE(spy.wait(kWaitMs));
  EXPECT_EQ(reader_->latest()->snapshot.updates.size(), 2U);

  ASSERT_TRUE(writer.save(sample(2, 1'760'000'500)).has_value());
  QTest::qWait(250);
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(SnapshotFileReaderTest, AbsentFileEmitsNothingAndDoesNotCrash) {
  QSignalSpy spy(reader_.get(), &SnapshotFileReader::snapshotRead);
  reader_->start();
  reader_->readNow();
  QTest::qWait(100);
  EXPECT_EQ(spy.count(), 0);
  EXPECT_FALSE(reader_->latest().has_value());
  EXPECT_FALSE(fs::exists(file_));
}

TEST_F(SnapshotFileReaderTest, CorruptFileEmitsNothingAndStaysByteIdentical) {
  fs::create_directories(file_.parent_path());
  {
    std::ofstream(file_) << "{ \"schemaVersion\": 1, truncated";
  }
  const std::string before = slurp(file_);
  QSignalSpy spy(reader_.get(), &SnapshotFileReader::snapshotRead);

  reader_->start();
  QTest::qWait(150);

  EXPECT_EQ(spy.count(), 0);
  EXPECT_EQ(slurp(file_), before);
  EXPECT_TRUE(fs::exists(file_));
}

TEST_F(SnapshotFileReaderTest, TheReaderNeverCallsSaveOrDiscardInvalid) {
  fs::create_directories(file_.parent_path());
  std::ofstream(file_) << "garbage";
  reader_->start();
  JsonUpdateSnapshotStore writer(file_);
  ASSERT_TRUE(writer.save(sample(1, 1'760'000'000)).has_value());
  QTest::qWait(250);
  reader_->readNow();
  EXPECT_EQ(store_->writeCount(), 0);
}

TEST_F(SnapshotFileReaderTest, DeletionCorruptionAndReadFailureInvalidateOnce) {
  JsonUpdateSnapshotStore writer(file_);
  for (int mode = 0; mode < 3; ++mode) {
    ASSERT_TRUE(writer.save(sample(1, 1760000000)));
    reader_->readNow();
    ASSERT_TRUE(reader_->latest());
    QSignalSpy invalidated(reader_.get(), &SnapshotFileReader::snapshotInvalidated);
    if (mode == 0) {
      fs::remove(file_);
    } else if (mode == 1) {
      std::ofstream(file_) << "corrupt";
    } else {
      fs::remove(file_);
      fs::create_directory(file_);
    }
    reader_->readNow();
    reader_->readNow();
    EXPECT_EQ(invalidated.count(), 1);
    EXPECT_FALSE(reader_->latest());
    if (mode == 2) {
      fs::remove(file_);
    }
  }
}

}  // namespace
