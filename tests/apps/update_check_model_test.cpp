#include "SnapshotFileReader.h"
#include "UpdateCheckModel.h"
#include "fake_clock.h"
#include "fake_snapshot_store.h"
#include "fake_update_check_client.h"
#include "fake_update_source.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <gtest/gtest.h>
#include <memory>

namespace {

using holonight_packages_domain::CheckedSnapshot;
using holonight_packages_domain::PendingUpdate;
using holonight_packages_testing::FakeClock;
using holonight_packages_testing::FakeSnapshotStore;
using holonight_packages_testing::FakeUpdateCheckClient;
using holonight_packages_testing::FakeUpdateSource;
using std::chrono::hours;
using std::chrono::seconds;

class UpdateCheckModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    file_ = std::filesystem::path(dir_.path().toStdString()) / "update-snapshot.json";
    reader_ = std::make_unique<SnapshotFileReader>(store_, file_, std::chrono::milliseconds{10});
    source_->enqueue(holonight_packages_domain::UpdateSnapshot{});
    updates_ = std::make_unique<UpdatesModel>(source_);
    ASSERT_TRUE(QTest::qWaitFor([this] { return !updates_->loading(); }, 2000));
    model_ = std::make_unique<UpdateCheckModel>(
        &client_, reader_.get(), updates_.get(), [this] { return clock_.now(); }, clock_.makeTimer());
  }

  [[nodiscard]] qint64 secondsAgo(std::chrono::hours age) const {
    return std::chrono::duration_cast<seconds>((clock_.now() - age).time_since_epoch()).count();
  }

  QTemporaryDir dir_;
  std::filesystem::path file_;
  FakeClock clock_;
  FakeUpdateCheckClient client_;
  std::shared_ptr<FakeSnapshotStore> store_ = std::make_shared<FakeSnapshotStore>();
  std::shared_ptr<FakeUpdateSource> source_ = std::make_shared<FakeUpdateSource>();
  std::unique_ptr<SnapshotFileReader> reader_;
  std::unique_ptr<UpdatesModel> updates_;
  std::unique_ptr<UpdateCheckModel> model_;
};

TEST_F(UpdateCheckModelTest, OneActivationMakesExactlyOneClientCall) {
  model_->checkNow();
  EXPECT_EQ(client_.checkNowCalls(), 1);
  model_->checkNow();
  EXPECT_EQ(client_.checkNowCalls(), 2);
}

TEST_F(UpdateCheckModelTest, CheckingDisablesTheControl) {
  EXPECT_TRUE(model_->checkNowEnabled());
  UpdateCheckClientStatus status;
  status.serviceReachable = true;
  status.checking = true;
  client_.setStatus(status);
  EXPECT_TRUE(model_->checking());
  EXPECT_FALSE(model_->checkNowEnabled());
  status.checking = false;
  client_.setStatus(status);
  EXPECT_TRUE(model_->checkNowEnabled());
}

TEST_F(UpdateCheckModelTest, UnreachableServiceStillOffersTheControlBecauseCheckNowStartsIt) {
  EXPECT_FALSE(client_.status().serviceReachable);
  EXPECT_TRUE(model_->available());
  EXPECT_TRUE(model_->checkNowEnabled());
}

TEST_F(UpdateCheckModelTest, BackendWithoutTheCapabilityHidesTheControl) {
  UpdateCheckClientStatus status;
  status.serviceReachable = true;
  status.canCheck = false;
  client_.setStatus(status);
  EXPECT_FALSE(model_->available());
  EXPECT_FALSE(model_->checkNowEnabled());
}

TEST_F(UpdateCheckModelTest, EmptySnapshotMeansNotCheckedYetWithNoAgeText) {
  EXPECT_FALSE(model_->hasSnapshot());
  EXPECT_EQ(model_->statusLineText(), "Not checked yet");
  EXPECT_TRUE(model_->snapshotAgeText().isEmpty());
  EXPECT_FALSE(model_->snapshotFetchedAt().isValid());
}

TEST_F(UpdateCheckModelTest, SnapshotThreeHoursOldShowsThreeHours) {
  UpdateCheckClientStatus status;
  status.serviceReachable = true;
  status.snapshotFetchedAt = secondsAgo(hours{3});
  store_->setStored(CheckedSnapshot{.snapshot = {.dataAsOf = clock_.now()}, .fetchedAt = clock_.now() - hours{3}});
  client_.setStatus(status);
  EXPECT_TRUE(model_->hasSnapshot());
  EXPECT_TRUE(model_->snapshotAgeText().contains("3 h")) << model_->snapshotAgeText().toStdString();
  EXPECT_EQ(model_->statusLineText(), model_->snapshotAgeText());
}

TEST_F(UpdateCheckModelTest, AgeTextRefreshesWhenTheMinuteTimerFires) {
  UpdateCheckClientStatus status;
  status.snapshotFetchedAt = secondsAgo(hours{3});
  store_->setStored(CheckedSnapshot{.snapshot = {.dataAsOf = clock_.now()}, .fetchedAt = clock_.now() - hours{3}});
  client_.setStatus(status);
  QSignalSpy changed(model_.get(), &UpdateCheckModel::changed);
  clock_.advance(std::chrono::minutes{61});
  EXPECT_GE(changed.count(), 1);
  EXPECT_TRUE(model_->snapshotAgeText().contains("4 h")) << model_->snapshotAgeText().toStdString();
}

TEST_F(UpdateCheckModelTest, EachOfTheFourCodesShowsItsMessageAndKeepsTheAgeText) {
  const std::array<std::pair<const char*, const char*>, 4> cases{
      {
          {"network-unavailable", "No network connection to the package servers."},
          {"repository-unreachable", "A package repository could not be reached."},
          {"busy", "Another package operation is using the package database."},
          {"unknown", "The check failed for an unexpected reason."},
      },
  };
  store_->setStored(CheckedSnapshot{.snapshot = {.dataAsOf = clock_.now()}, .fetchedAt = clock_.now() - hours{3}});
  for (const auto& [token, message] : cases) {
    UpdateCheckClientStatus status;
    status.serviceReachable = true;
    status.lastCheckTime = secondsAgo(hours{1});
    status.lastCheckSucceeded = false;
    status.lastCheckError = QString::fromLatin1(token);
    status.snapshotFetchedAt = secondsAgo(hours{3});
    client_.setStatus(status);
    EXPECT_TRUE(model_->failed()) << token;
    EXPECT_EQ(model_->failureText(), QString("Last check failed: ") + message) << token;
    EXPECT_EQ(model_->lastErrorCode(), token);
    EXPECT_TRUE(model_->snapshotAgeText().contains("3 h")) << token;
  }
}

TEST_F(UpdateCheckModelTest, SuccessHasNoFailureText) {
  UpdateCheckClientStatus status;
  status.serviceReachable = true;
  status.lastCheckTime = secondsAgo(hours{1});
  status.lastCheckSucceeded = true;
  client_.setStatus(status);
  EXPECT_FALSE(model_->failed());
  EXPECT_TRUE(model_->lastCheckSucceeded());
  EXPECT_TRUE(model_->failureText().isEmpty());
}

TEST_F(UpdateCheckModelTest, CheckNowFailedShowsServiceUnavailableAndKeepsTheControlEnabled) {
  model_->checkNow();
  client_.failCheckNow();
  EXPECT_TRUE(model_->serviceUnavailable());
  EXPECT_EQ(model_->failureText(), "Update service unavailable");
  EXPECT_TRUE(model_->checkNowEnabled());

  UpdateCheckClientStatus status;
  status.serviceReachable = true;
  client_.setStatus(status);
  EXPECT_FALSE(model_->serviceUnavailable());
  EXPECT_TRUE(model_->failureText().isEmpty());
}

TEST_F(UpdateCheckModelTest, ASnapshotReadFromTheFileUpdatesTheUpdatesModelAndTheStatusLine) {
  CheckedSnapshot online;
  online.fetchedAt = clock_.now() - hours{2};
  online.snapshot.dataAsOf = clock_.now();  // fresher than the (empty) local databases
  online.snapshot.updates.push_back(PendingUpdate{.name = "from-file"});
  store_->setStored(online);
  QSignalSpy changed(model_.get(), &UpdateCheckModel::changed);

  reader_->readNow();

  EXPECT_EQ(updates_->updateCount(), 1);
  EXPECT_TRUE(model_->hasSnapshot());
  EXPECT_TRUE(model_->snapshotAgeText().contains("2 h"));
  EXPECT_GE(changed.count(), 1);
}

TEST_F(UpdateCheckModelTest, CheckCompletedAsksTheReaderToReadAgain) {
  CheckedSnapshot online;
  online.fetchedAt = clock_.now();
  online.snapshot.dataAsOf = clock_.now();
  online.snapshot.updates.push_back(PendingUpdate{.name = "after-check"});
  client_.completeCheck();  // nothing stored yet: nothing changes
  EXPECT_EQ(updates_->updateCount(), 0);

  store_->setStored(online);
  client_.completeCheck();

  EXPECT_EQ(updates_->updateCount(), 1);
}

}  // namespace

TEST_F(UpdateCheckModelTest, SuccessfulNoOpAcknowledgementClearsUnavailable) {
  client_.failCheckNow();
  QSignalSpy completed(&client_, &UpdateCheckClient::checkCompleted);
  emit client_.checkNowSucceeded();
  EXPECT_FALSE(model_->serviceUnavailable());
  EXPECT_EQ(completed.count(), 0);
}

TEST_F(UpdateCheckModelTest, InvalidatedSnapshotKeepsRowsAndClearsProvenance) {
  CheckedSnapshot online{};
  online.fetchedAt = clock_.now();
  online.snapshot.dataAsOf = clock_.now();
  online.snapshot.updates.push_back(PendingUpdate{.name = "retained"});
  store_->setStored(online);
  reader_->readNow();
  QSignalSpy invalidated(reader_.get(), &SnapshotFileReader::snapshotInvalidated);
  store_->setInvalid();
  reader_->readNow();
  reader_->readNow();
  EXPECT_EQ(invalidated.count(), 1);
  EXPECT_FALSE(reader_->latest().has_value());
  EXPECT_EQ(updates_->rowCount(), 1);
  EXPECT_EQ(model_->statusLineText(), "Not checked yet");
}

TEST_F(UpdateCheckModelTest, NewerServiceMetadataDoesNotReplaceDisplayedSnapshotAge) {
  store_->setStored(CheckedSnapshot{.snapshot = {.dataAsOf = clock_.now()}, .fetchedAt = clock_.now() - hours{3}});
  UpdateCheckClientStatus status{};
  status.snapshotFetchedAt = secondsAgo(hours{1});
  client_.setStatus(status);
  EXPECT_TRUE(model_->snapshotAgeText().contains("3 h"));
  EXPECT_EQ(model_->snapshotFetchedAt().toSecsSinceEpoch(), status.snapshotFetchedAt);
}

TEST_F(UpdateCheckModelTest, AsynchronousLocalReloadChangesDisplayedProvenance) {
  CheckedSnapshot online{};
  online.fetchedAt = clock_.now() - hours{3};
  online.snapshot.dataAsOf = clock_.now() - hours{2};
  store_->setStored(online);
  reader_->readNow();
  ASSERT_TRUE(model_->snapshotAgeText().contains("3 h"));
  source_->enqueue(holonight_packages_domain::UpdateSnapshot{.databasesFound = true, .dataAsOf = clock_.now()});
  QSignalSpy changed(model_.get(), &UpdateCheckModel::changed);
  updates_->reload();
  ASSERT_TRUE(QTest::qWaitFor([this] { return !updates_->loading(); }, 2000));
  EXPECT_EQ(model_->statusLineText(), "Showing local package data");
  EXPECT_TRUE(model_->snapshotAgeText().isEmpty());
  EXPECT_GE(changed.count(), 1);
  store_->setInvalid();
  reader_->readNow();
  source_->enqueue(holonight_packages_domain::UpdateSnapshot{});
  updates_->reload();
  ASSERT_TRUE(QTest::qWaitFor([this] { return !updates_->loading(); }, 2000));
  EXPECT_EQ(model_->statusLineText(), "Not checked yet");
}
