#include "UpdatesModel.h"
#include "fake_update_source.h"

#include <QTest>

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_domain::UpdateSourceError;
using holonight_packages_domain::UpdateSourceErrorCode;
using holonight_packages_testing::FakeUpdateSource;
using ViewState = UpdatesModel::ViewState;
using std::chrono::days;
using std::chrono::hours;
using TimePoint = std::chrono::system_clock::time_point;

// A fixed "now" so staleness does not depend on wall time.
const TimePoint kNow = TimePoint{} + days(20000);

UpdatesModel::Clock fixedClock() {
  return [] { return kNow; };
}

PendingUpdate makeUpdate(std::string name, std::uint64_t download_bytes = 1024, bool ignored = false) {
  return PendingUpdate{.name = std::move(name),
                       .installedVersion = "1.0-1",
                       .availableVersion = "2.0-1",
                       .repository = "core",
                       .downloadSizeBytes = download_bytes,
                       .installedSizeDeltaBytes = 0,
                       .ignored = ignored};
}

UpdateSnapshot snapshotWith(std::vector<PendingUpdate> updates, TimePoint data_as_of = kNow - hours(2)) {
  return UpdateSnapshot{.updates = std::move(updates), .databasesFound = true, .dataAsOf = data_as_of};
}

UpdateSourceError failure(std::string message) {
  return UpdateSourceError{.code = UpdateSourceErrorCode::DatabaseOpenFailed, .message = std::move(message)};
}

[[nodiscard]] bool waitUntilIdle(const UpdatesModel& model) {
  return QTest::qWaitFor([&model] { return !model.loading(); }, 2000);
}

std::unique_ptr<UpdatesModel> loadedModel(const std::shared_ptr<FakeUpdateSource>& source) {
  auto model = std::make_unique<UpdatesModel>(source, nullptr, fixedClock());
  EXPECT_TRUE(waitUntilIdle(*model));
  return model;
}

TEST(UpdatesModel, RejectsNullSource) { EXPECT_THROW(UpdatesModel(nullptr), std::invalid_argument); }

TEST(UpdatesModel, ConstructorReturnsBeforeSourceCompletes) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->setBlocking(true);
  source->enqueue(snapshotWith({makeUpdate("alpha")}));

  const UpdatesModel model(source, nullptr, fixedClock());

  EXPECT_TRUE(model.loading());
  EXPECT_EQ(model.state(), ViewState::Loading);
  EXPECT_EQ(model.rowCount(), 0);

  source->release();
  ASSERT_TRUE(waitUntilIdle(model));
  EXPECT_EQ(model.state(), ViewState::Updates);
  EXPECT_EQ(model.rowCount(), 1);
}

TEST(UpdatesModel, ReloadReturnsBeforeSourceCompletesAndKeepsPreviousRows) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({makeUpdate("alpha")}));
  source->enqueue(snapshotWith({makeUpdate("alpha"), makeUpdate("beta")}));
  auto model = loadedModel(source);
  source->setBlocking(true);

  model->reload();

  EXPECT_TRUE(model->loading());
  EXPECT_EQ(model->state(), ViewState::Updates);
  EXPECT_EQ(model->rowCount(), 1);

  source->release();
  ASSERT_TRUE(waitUntilIdle(*model));
  EXPECT_EQ(model->rowCount(), 2);
}

TEST(UpdatesModel, SecondReloadWhileLoadingIsIgnored) {
  auto source = std::make_shared<FakeUpdateSource>();
  auto model = loadedModel(source);
  ASSERT_EQ(source->callCount(), 1);
  source->setBlocking(true);

  model->reload();
  model->reload();
  source->release();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_EQ(source->callCount(), 2);
}

TEST(UpdatesModel, ReloadDuringInitialLoadIsIgnored) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->setBlocking(true);
  UpdatesModel model(source, nullptr, fixedClock());

  model.reload();
  source->release();
  ASSERT_TRUE(waitUntilIdle(model));

  EXPECT_EQ(source->callCount(), 1);
}

TEST(UpdatesModel, ReloadAfterCompletionShowsNewRowsAndTimestamp) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({makeUpdate("alpha")}, kNow - days(3)));
  source->enqueue(snapshotWith({makeUpdate("beta"), makeUpdate("gamma")}, kNow - hours(1)));
  auto model = loadedModel(source);
  const QDateTime first_as_of = model->dataAsOf();
  const QString first_label = model->dataAsOfLabel();

  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_EQ(source->callCount(), 2);
  ASSERT_EQ(model->rowCount(), 2);
  EXPECT_EQ(model->data(model->index(0, 0), UpdatesModel::NameRole).toString(), QStringLiteral("beta"));
  EXPECT_GT(model->dataAsOf(), first_as_of);
  EXPECT_NE(model->dataAsOfLabel(), first_label);
}

TEST(UpdatesModel, FailedReloadKeepsRowsAndTimestampAndSetsBanner) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({makeUpdate("alpha"), makeUpdate("beta")}));
  source->enqueue(std::unexpected(failure("database is locked")));
  auto model = loadedModel(source);
  const QDateTime as_of = model->dataAsOf();

  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_FALSE(model->loading());
  EXPECT_EQ(model->state(), ViewState::Updates);
  EXPECT_EQ(model->rowCount(), 2);
  EXPECT_EQ(model->dataAsOf(), as_of);
  EXPECT_EQ(model->reloadErrorMessage(), QStringLiteral("database is locked"));
  EXPECT_TRUE(model->errorMessage().isEmpty());
}

TEST(UpdatesModel, ConfigurationFailureOnReloadKeepsList) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({makeUpdate("alpha")}));
  source->enqueue(std::unexpected(
      UpdateSourceError{.code = UpdateSourceErrorCode::ConfigurationInvalid, .message = "pacman.conf unreadable"}));
  auto model = loadedModel(source);

  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_EQ(model->rowCount(), 1);
  EXPECT_EQ(model->reloadErrorMessage(), QStringLiteral("pacman.conf unreadable"));
}

TEST(UpdatesModel, SuccessfulReloadClearsBanner) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({makeUpdate("alpha")}));
  source->enqueue(std::unexpected(failure("boom")));
  source->enqueue(snapshotWith({makeUpdate("alpha")}));
  auto model = loadedModel(source);
  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));
  ASSERT_FALSE(model->reloadErrorMessage().isEmpty());

  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_TRUE(model->reloadErrorMessage().isEmpty());
}

TEST(UpdatesModel, FailedInitialLoadGivesErrorStateAndAcceptsReload) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(std::unexpected(failure("cannot open database")));
  source->enqueue(snapshotWith({makeUpdate("alpha")}));
  auto model = loadedModel(source);

  EXPECT_EQ(model->state(), ViewState::Error);
  EXPECT_EQ(model->errorMessage(), QStringLiteral("cannot open database"));
  EXPECT_EQ(model->rowCount(), 0);
  EXPECT_TRUE(model->reloadErrorMessage().isEmpty());

  model->reload();
  EXPECT_EQ(model->state(), ViewState::Loading);
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_EQ(source->callCount(), 2);
  EXPECT_EQ(model->state(), ViewState::Updates);
  EXPECT_TRUE(model->errorMessage().isEmpty());
}

TEST(UpdatesModel, ExceptionFromSourceBecomesError) {
  class ThrowingSource : public holonight_packages_domain::UpdateSource {
   public:
    [[nodiscard]] std::expected<UpdateSnapshot, UpdateSourceError> loadUpdates() const override {
      throw std::runtime_error("unexpected libalpm failure");
    }
  };
  const UpdatesModel model(std::make_shared<ThrowingSource>(), nullptr, fixedClock());
  ASSERT_TRUE(waitUntilIdle(model));

  EXPECT_EQ(model.state(), ViewState::Error);
  EXPECT_EQ(model.errorMessage(), QStringLiteral("unexpected libalpm failure"));
}

struct StaleCase {
  std::chrono::system_clock::duration age;
  bool stale;
};

class UpdatesModelStaleness : public ::testing::TestWithParam<StaleCase> {};

TEST_P(UpdatesModelStaleness, HintOnlyWhenStrictlyOlderThanSevenDays) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({makeUpdate("alpha")}, kNow - GetParam().age));

  const auto model = loadedModel(source);

  EXPECT_EQ(model->databasesStale(), GetParam().stale);
  EXPECT_FALSE(model->dataAsOfLabel().isEmpty());
}

INSTANTIATE_TEST_SUITE_P(Boundaries, UpdatesModelStaleness,
                         ::testing::Values(StaleCase{.age = hours(2), .stale = false},
                                           StaleCase{.age = days(6), .stale = false},
                                           StaleCase{.age = days(7), .stale = false},
                                           StaleCase{.age = days(8), .stale = true}));

TEST(UpdatesModel, ReloadWithFreshTimestampClearsStaleHint) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({makeUpdate("alpha")}, kNow - days(10)));
  source->enqueue(snapshotWith({makeUpdate("alpha")}, kNow - hours(1)));
  auto model = loadedModel(source);
  ASSERT_TRUE(model->databasesStale());

  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_FALSE(model->databasesStale());
}

TEST(UpdatesModel, UpToDateStateStillShowsStaleHint) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({}, kNow - days(9)));

  const auto model = loadedModel(source);

  EXPECT_EQ(model->state(), ViewState::UpToDate);
  EXPECT_TRUE(model->databasesStale());
}

TEST(UpdatesModel, HeadlineExcludesIgnoredRows) {
  auto source = std::make_shared<FakeUpdateSource>();
  std::vector<PendingUpdate> updates;
  updates.reserve(8);
  for (const char* name : {"a", "b", "c", "d", "e"}) {
    updates.push_back(makeUpdate(name, 1000));
  }
  for (const char* name : {"x", "y", "z"}) {
    updates.push_back(makeUpdate(name, 50000, true));
  }
  source->enqueue(snapshotWith(std::move(updates)));

  const auto model = loadedModel(source);

  EXPECT_EQ(model->rowCount(), 8);
  EXPECT_EQ(model->updateCount(), 5);
  EXPECT_EQ(model->ignoredCount(), 3);
  EXPECT_EQ(model->totalDownloadBytes(), 5000U);
  EXPECT_EQ(model->totalDownloadLabel(), QStringLiteral("4.9 KiB"));
}

TEST(UpdatesModel, AllRowsIgnoredStillListsThem) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({makeUpdate("x", 10, true)}));

  const auto model = loadedModel(source);

  EXPECT_EQ(model->state(), ViewState::Updates);
  EXPECT_EQ(model->rowCount(), 1);
  EXPECT_EQ(model->updateCount(), 0);
}

TEST(UpdatesModel, FreshModelCarriesNoPreviousState) {
  auto first_source = std::make_shared<FakeUpdateSource>();
  first_source->enqueue(snapshotWith({makeUpdate("alpha")}, kNow - days(10)));
  const auto first = loadedModel(first_source);
  ASSERT_EQ(first->rowCount(), 1);

  auto second_source = std::make_shared<FakeUpdateSource>();
  second_source->setBlocking(true);
  const UpdatesModel second(second_source, nullptr, fixedClock());

  EXPECT_EQ(second.state(), ViewState::Loading);
  EXPECT_TRUE(second.loading());
  EXPECT_EQ(second.rowCount(), 0);
  EXPECT_EQ(second.updateCount(), 0);
  EXPECT_FALSE(second.dataAsOf().isValid());
  EXPECT_TRUE(second.dataAsOfLabel().isEmpty());
  EXPECT_FALSE(second.databasesStale());
  EXPECT_TRUE(second.reloadErrorMessage().isEmpty());

  second_source->release();
  ASSERT_TRUE(waitUntilIdle(second));
}

TEST(UpdatesModel, RolesExposeAllFields) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({PendingUpdate{.name = "gamma",
                                              .installedVersion = "1.0-1",
                                              .availableVersion = "1.1-1",
                                              .repository = "extra",
                                              .downloadSizeBytes = 4096,
                                              .installedSizeDeltaBytes = -2048,
                                              .ignored = true}}));

  const auto model = loadedModel(source);

  ASSERT_EQ(model->rowCount(), 1);
  const QModelIndex index = model->index(0, 0);
  EXPECT_EQ(model->data(index, UpdatesModel::NameRole).toString(), QStringLiteral("gamma"));
  EXPECT_EQ(model->data(index, UpdatesModel::InstalledVersionRole).toString(), QStringLiteral("1.0-1"));
  EXPECT_EQ(model->data(index, UpdatesModel::AvailableVersionRole).toString(), QStringLiteral("1.1-1"));
  EXPECT_EQ(model->data(index, UpdatesModel::RepositoryRole).toString(), QStringLiteral("extra"));
  EXPECT_EQ(model->data(index, UpdatesModel::DownloadSizeRole).toULongLong(), 4096ULL);
  EXPECT_EQ(model->data(index, UpdatesModel::DownloadSizeLabelRole).toString(), QStringLiteral("4 KiB"));
  EXPECT_EQ(model->data(index, UpdatesModel::SizeDeltaRole).toLongLong(), -2048LL);
  EXPECT_EQ(model->data(index, UpdatesModel::SizeDeltaLabelRole).toString(), QStringLiteral("-2 KiB"));
  EXPECT_TRUE(model->data(index, UpdatesModel::IsIgnoredRole).toBool());

  const QHash<int, QByteArray> names = model->roleNames();
  for (const char* role : {"name", "installedVersion", "availableVersion", "repository", "downloadSize",
                           "downloadSizeLabel", "sizeDelta", "sizeDeltaLabel", "isIgnored"}) {
    EXPECT_TRUE(names.values().contains(QByteArray(role))) << role;
  }
}

TEST(UpdatesModel, RowsAreSortedByName) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({makeUpdate("zeta"), makeUpdate("alpha"), makeUpdate("mid")}));

  const auto model = loadedModel(source);

  ASSERT_EQ(model->rowCount(), 3);
  EXPECT_EQ(model->data(model->index(0, 0), UpdatesModel::NameRole).toString(), QStringLiteral("alpha"));
  EXPECT_EQ(model->data(model->index(2, 0), UpdatesModel::NameRole).toString(), QStringLiteral("zeta"));
}

TEST(UpdatesModel, ZeroUpdatesIsUpToDateWithOfficialOnlyNote) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(snapshotWith({}));

  const auto model = loadedModel(source);

  EXPECT_EQ(model->state(), ViewState::UpToDate);
  EXPECT_EQ(model->rowCount(), 0);
  EXPECT_TRUE(model->errorMessage().isEmpty());
  EXPECT_TRUE(model->dataAsOf().isValid());
  EXPECT_FALSE(UpdatesModel::officialOnlyNote().isEmpty());
}

TEST(UpdatesModel, MissingDatabasesGiveNoDatabasesState) {
  auto source = std::make_shared<FakeUpdateSource>();
  source->enqueue(UpdateSnapshot{.updates = {}, .databasesFound = false, .dataAsOf = {}});

  const auto model = loadedModel(source);

  EXPECT_EQ(model->state(), ViewState::NoDatabases);
  EXPECT_TRUE(model->errorMessage().isEmpty());
  EXPECT_FALSE(model->dataAsOf().isValid());
  EXPECT_FALSE(model->databasesStale());
}

}  // namespace
