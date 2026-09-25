#include "DataFreshness.h"
#include "ExploreModel.h"
#include "fake_explore_source.h"

#include <QSignalSpy>
#include <QTest>

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using holonight_packages_domain::ExploreSnapshot;
using holonight_packages_domain::ExploreSourceError;
using holonight_packages_domain::ExploreSourceErrorCode;
using holonight_packages_domain::SyncPackage;
using holonight_packages_testing::FakeExploreSource;
using ViewState = ExploreModel::ViewState;
using std::chrono::days;
using std::chrono::hours;
using std::chrono::milliseconds;
using TimePoint = std::chrono::system_clock::time_point;

// A fixed "now" so staleness does not depend on wall time.
const TimePoint kNow = TimePoint{} + days(20000);
constexpr milliseconds kDebounce{25};

ExploreModel::Clock fixedClock() {
  return [] { return kNow; };
}

SyncPackage makePackage(std::string name, std::string description = "", std::string repository = "extra",
                        std::string version = "1.0-1") {
  return SyncPackage{.name = std::move(name),
                     .version = std::move(version),
                     .repository = std::move(repository),
                     .description = std::move(description),
                     .url = "vim.example.org",
                     .licenses = {"GPL", "MIT"},
                     .dependencies = {"glibc>=2.38"},
                     .optionalDependencies = {"python: Python support"},
                     .downloadSizeBytes = 2048,
                     .installedSizeBytes = 4096,
                     .installedVersion = std::nullopt};
}

std::vector<SyncPackage> vimPackages() {
  std::vector<SyncPackage> packages{
      makePackage("nano", "Pico editor clone, unlike vim", "core"),
      makePackage("neovim", "Fork of Vim"),
      makePackage("gvim", "Vi Improved with GUI"),
      makePackage("vimb", "Browser"),
      makePackage("vim-runtime", "Runtime files", "core", "9.1-1"),
      makePackage("vim", "Vi Improved", "core", "9.1-1"),
  };
  packages[4].installedVersion = "9.1-1";
  packages[5].installedVersion = "9.0-1";
  return packages;
}

ExploreSnapshot snapshotWith(std::vector<SyncPackage> packages, TimePoint data_as_of = kNow - hours(2)) {
  return ExploreSnapshot{.packages = std::move(packages), .databasesFound = true, .dataAsOf = data_as_of};
}

ExploreSourceError failure(std::string message) {
  return ExploreSourceError{.code = ExploreSourceErrorCode::DatabaseOpenFailed, .message = std::move(message)};
}

[[nodiscard]] bool waitUntilIdle(const ExploreModel& model) {
  return QTest::qWaitFor([&model] { return !model.loading(); }, 2000);
}

std::unique_ptr<ExploreModel> loadedModel(const std::shared_ptr<FakeExploreSource>& source) {
  auto model = std::make_unique<ExploreModel>(source, nullptr, fixedClock(), kDebounce);
  EXPECT_TRUE(waitUntilIdle(*model));
  return model;
}

std::unique_ptr<ExploreModel> vimModel(const std::shared_ptr<FakeExploreSource>& source) {
  source->enqueue(snapshotWith(vimPackages()));
  return loadedModel(source);
}

// Types `text` and waits for the debounced search to complete.
void search(ExploreModel& model, const QString& text) {
  const int before = model.searchCount();
  model.setSearchText(text);
  ASSERT_TRUE(QTest::qWaitFor([&] { return model.searchCount() > before || text.trimmed().isEmpty(); }, 2000));
  if (text.trimmed().isEmpty()) {
    ASSERT_TRUE(QTest::qWaitFor([&] { return model.state() == ViewState::Hint; }, 2000));
  }
}

QStringList names(const ExploreModel& model) {
  QStringList out;
  for (int row = 0; row < model.rowCount(); ++row) {
    out << model.data(model.index(row), ExploreModel::NameRole).toString();
  }
  return out;
}

// --- T-008: async load and single-flight reload ---

TEST(ExploreModel, RejectsNullSource) { EXPECT_THROW(ExploreModel(nullptr), std::invalid_argument); }

TEST(ExploreModel, ConstructorReturnsBeforeSourceCompletes) {
  auto source = std::make_shared<FakeExploreSource>();
  source->setBlocking(true);
  source->enqueue(snapshotWith(vimPackages()));

  ExploreModel model(source, nullptr, fixedClock(), kDebounce);

  EXPECT_TRUE(model.loading());
  EXPECT_EQ(model.state(), ViewState::Loading);
  EXPECT_FALSE(model.searchEnabled());
  EXPECT_EQ(model.rowCount(), 0);

  source->release();
  ASSERT_TRUE(waitUntilIdle(model));
  EXPECT_EQ(model.state(), ViewState::Hint);
  EXPECT_TRUE(model.searchEnabled());
}

TEST(ExploreModel, SecondReloadWhileLoadingIsIgnored) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = loadedModel(source);
  ASSERT_EQ(source->callCount(), 1);
  source->setBlocking(true);

  model->reload();
  model->reload();
  source->release();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_EQ(source->callCount(), 2);
}

TEST(ExploreModel, ReloadDuringInitialLoadCallsSourceOnce) {
  auto source = std::make_shared<FakeExploreSource>();
  source->setBlocking(true);
  ExploreModel model(source, nullptr, fixedClock(), kDebounce);

  model.reload();
  source->release();
  ASSERT_TRUE(waitUntilIdle(model));

  EXPECT_EQ(source->callCount(), 1);
}

TEST(ExploreModel, ReloadKeepsIndexSearchableAndStateWhileInFlight) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  source->setBlocking(true);

  model->reload();

  EXPECT_TRUE(model->loading());
  EXPECT_EQ(model->state(), ViewState::Results);
  EXPECT_TRUE(model->searchEnabled());
  EXPECT_EQ(model->rowCount(), 6);
  source->release();
  ASSERT_TRUE(waitUntilIdle(*model));
}

TEST(ExploreModel, ReloadKeepsTheQueryAndReappliesItToNewData) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  source->enqueue(snapshotWith({makePackage("vim-new")}));

  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_EQ(model->searchText(), "vim");
  EXPECT_EQ(names(*model), QStringList{"vim-new"});
}

TEST(ExploreModel, ReloadKeepsOldRowsValidUntilModelReset) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  ASSERT_EQ(model->rowCount(), 6);
  const QString old_last_name = model->data(model->index(5), ExploreModel::NameRole).toString();
  source->enqueue(snapshotWith({makePackage("vim-new")}));

  bool observed_reset = false;
  QObject::connect(model.get(), &QAbstractItemModel::modelAboutToBeReset, model.get(), [&] {
    observed_reset = true;
    EXPECT_EQ(model->rowCount(), 6);
    EXPECT_EQ(model->data(model->index(5), ExploreModel::NameRole).toString(), old_last_name);
  });
  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_TRUE(observed_reset);
  EXPECT_EQ(names(*model), QStringList{"vim-new"});
}

// --- T-009: debounce and search states ---

TEST(ExploreModel, RapidKeystrokesRunOneSearchWithTheFinalQuery) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);

  model->setSearchText("v");
  model->setSearchText("vi");
  model->setSearchText("vim");
  ASSERT_TRUE(QTest::qWaitFor([&] { return model->searchCount() > 0; }, 2000));
  QTest::qWait(3 * kDebounce.count());

  EXPECT_EQ(model->searchCount(), 1);
  EXPECT_EQ(model->state(), ViewState::Results);
  EXPECT_EQ(model->rowCount(), 6);
}

TEST(ExploreModel, SeparatedKeystrokesRunTwoSearches) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);

  search(*model, "vi");
  search(*model, "vim");

  EXPECT_EQ(model->searchCount(), 2);
}

TEST(ExploreModel, SearchTextIsPublishedImmediately) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  QSignalSpy spy(model.get(), &ExploreModel::searchTextChanged);

  model->setSearchText("vim");

  EXPECT_EQ(model->searchText(), "vim");
  EXPECT_EQ(spy.count(), 1);
  EXPECT_EQ(model->searchCount(), 0);
}

TEST(ExploreModel, EmptyAndWhitespaceQueriesShowHintWithoutSearching) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  ASSERT_EQ(model->searchCount(), 1);

  search(*model, "");
  EXPECT_EQ(model->state(), ViewState::Hint);
  EXPECT_EQ(model->rowCount(), 0);

  model->setSearchText("   ");
  QTest::qWait(3 * kDebounce.count());
  EXPECT_EQ(model->state(), ViewState::Hint);
  EXPECT_EQ(model->searchCount(), 1);
}

TEST(ExploreModel, ResultsAreRankedByRelevance) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);

  search(*model, "vim");

  EXPECT_EQ(names(*model), (QStringList{"vim", "vim-runtime", "vimb", "gvim", "neovim", "nano"}));
  EXPECT_EQ(model->matchCount(), 6);
}

TEST(ExploreModel, NoMatchesStateNamesTheQueryAndRepositoryScope) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);

  search(*model, "  zzzz ");

  EXPECT_EQ(model->state(), ViewState::NoMatches);
  EXPECT_TRUE(model->noMatchesText().contains("'zzzz'"));
  EXPECT_TRUE(model->noMatchesText().contains("Configured sync repositories"));
  EXPECT_EQ(model->rowCount(), 0);
}

TEST(ExploreModel, CapFooterShownOnlyAboveFiveHundredMatches) {
  auto source = std::make_shared<FakeExploreSource>();
  std::vector<SyncPackage> packages;
  packages.reserve(600);
  for (int i = 0; i < 600; ++i) {
    packages.push_back(makePackage("pkg" + std::to_string(1000 + i)));
  }
  source->enqueue(snapshotWith(std::move(packages)));
  auto model = loadedModel(source);

  search(*model, "pkg");

  EXPECT_EQ(model->rowCount(), 500);
  EXPECT_EQ(model->matchCount(), 600);
  EXPECT_TRUE(model->footerText().contains("500"));
  EXPECT_TRUE(model->footerText().contains("600"));

  search(*model, "pkg1000");
  EXPECT_EQ(model->matchCount(), 1);
  EXPECT_TRUE(model->footerText().isEmpty());
}

TEST(ExploreModel, ExactlyFiveHundredMatchesHaveNoFooter) {
  auto source = std::make_shared<FakeExploreSource>();
  std::vector<SyncPackage> packages;
  packages.reserve(500);
  for (int i = 0; i < 500; ++i) {
    packages.push_back(makePackage("pkg" + std::to_string(1000 + i)));
  }
  source->enqueue(snapshotWith(std::move(packages)));
  auto model = loadedModel(source);

  search(*model, "pkg");

  EXPECT_EQ(model->rowCount(), 500);
  EXPECT_TRUE(model->footerText().isEmpty());
}

struct StaleCase {
  hours age;
  bool stale;
};

class ExploreModelStaleness : public ::testing::TestWithParam<StaleCase> {};

TEST_P(ExploreModelStaleness, HintOnlyWhenStrictlyOlderThanSevenDays) {
  auto source = std::make_shared<FakeExploreSource>();
  source->enqueue(snapshotWith({makePackage("a")}, kNow - GetParam().age));

  const auto model = loadedModel(source);

  EXPECT_EQ(model->databasesStale(), GetParam().stale);
}

INSTANTIATE_TEST_SUITE_P(Boundaries, ExploreModelStaleness,
                         ::testing::Values(StaleCase{.age = hours(2), .stale = false},
                                           StaleCase{.age = hours(6 * 24), .stale = false},
                                           StaleCase{.age = hours(7 * 24), .stale = false},
                                           StaleCase{.age = hours(8 * 24), .stale = true}));

TEST(ExploreModel, ShowsTheSameStaleWordingAsTheUpdatesPage) {
  EXPECT_EQ(ExploreModel::staleHintText(), data_freshness::staleHintText());
}

TEST(ExploreModel, DataAsOfLabelUsesTheSharedFormatting) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);

  EXPECT_EQ(model->dataAsOf(), data_freshness::toQDateTime(kNow - hours(2)));
  EXPECT_EQ(model->dataAsOfLabel(), data_freshness::dataAsOfLabel(model->dataAsOf()));
}

// --- T-010: installed badge and selection ---

TEST(ExploreModel, InstalledBadgeReflectsInstalledVersion) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");

  const auto role = [&](const QString& name, int role_id) {
    for (int row = 0; row < model->rowCount(); ++row) {
      if (model->data(model->index(row), ExploreModel::NameRole).toString() == name) {
        return model->data(model->index(row), role_id);
      }
    }
    return QVariant();
  };

  EXPECT_FALSE(role("vimb", ExploreModel::IsInstalledRole).toBool());
  EXPECT_EQ(role("vimb", ExploreModel::InstalledBadgeTextRole).toString(), "");
  EXPECT_FALSE(role("vimb", ExploreModel::InstalledVersionDiffersRole).toBool());

  EXPECT_TRUE(role("vim-runtime", ExploreModel::IsInstalledRole).toBool());
  EXPECT_EQ(role("vim-runtime", ExploreModel::InstalledBadgeTextRole).toString(), "Installed");
  EXPECT_FALSE(role("vim-runtime", ExploreModel::InstalledVersionDiffersRole).toBool());

  EXPECT_EQ(role("vim", ExploreModel::InstalledBadgeTextRole).toString(), "Installed 9.0-1");
  EXPECT_TRUE(role("vim", ExploreModel::InstalledVersionDiffersRole).toBool());
  EXPECT_EQ(role("vim", ExploreModel::InstalledVersionRole).toString(), "9.0-1");
}

TEST(ExploreModel, CurrentRowSelectsPackage) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  ASSERT_EQ(model->currentRow(), -1);
  ASSERT_TRUE(model->currentPackage().isEmpty());

  model->setCurrentRow(1);

  EXPECT_EQ(model->currentRow(), 1);
  EXPECT_EQ(model->currentPackage().value("name").toString(), "vim-runtime");
  EXPECT_EQ(model->currentPackage().value("repository").toString(), "core");
}

TEST(ExploreModel, OutOfRangeRowClearsSelection) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  model->setCurrentRow(0);

  model->setCurrentRow(99);

  EXPECT_EQ(model->currentRow(), -1);
  EXPECT_TRUE(model->currentPackage().isEmpty());
}

TEST(ExploreModel, SelectionFollowsThePackageWhenResultsChange) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  model->setCurrentRow(2);  // vimb
  ASSERT_EQ(model->currentPackage().value("name").toString(), "vimb");

  search(*model, "vimb");

  EXPECT_EQ(model->currentRow(), 0);
  EXPECT_EQ(model->currentPackage().value("name").toString(), "vimb");
}

TEST(ExploreModel, SelectionIsClearedWhenPackageLeavesTheResults) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  model->setCurrentRow(0);

  search(*model, "neovim");

  EXPECT_EQ(model->currentRow(), -1);
  EXPECT_TRUE(model->currentPackage().isEmpty());
}

TEST(ExploreModel, SelectionSurvivesReload) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  model->setCurrentRow(2);
  source->enqueue(snapshotWith(vimPackages()));

  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_EQ(model->currentPackage().value("name").toString(), "vimb");
}

TEST(ExploreModel, SelectionIdentityIsRepositoryAndName) {
  auto source = std::make_shared<FakeExploreSource>();
  source->enqueue(snapshotWith({makePackage("dup", "", "core", "1.0-1"), makePackage("dup", "", "extra", "2.0-1")}));
  auto model = loadedModel(source);
  search(*model, "dup");
  model->setCurrentRow(1);
  ASSERT_EQ(model->currentPackage().value("repository").toString(), "extra");

  search(*model, "du");

  EXPECT_EQ(model->currentRow(), 1);
  EXPECT_EQ(model->currentPackage().value("availableVersion").toString(), "2.0-1");
}

// --- T-011: roles and state machine ---

TEST(ExploreModel, RolesReturnFixtureValues) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  const QModelIndex first = model->index(0);  // vim

  const auto value = [&](ExploreModel::Role role) { return model->data(first, role); };
  EXPECT_EQ(value(ExploreModel::NameRole), "vim");
  EXPECT_EQ(value(ExploreModel::AvailableVersionRole), "9.1-1");
  EXPECT_EQ(value(ExploreModel::RepositoryRole), "core");
  EXPECT_EQ(value(ExploreModel::DescriptionRole), "Vi Improved");
  EXPECT_EQ(value(ExploreModel::DownloadSizeRole).toULongLong(), 2048U);
  EXPECT_EQ(value(ExploreModel::DownloadSizeLabelRole).toString(), "2 KiB");
  EXPECT_EQ(value(ExploreModel::InstalledSizeRole).toULongLong(), 4096U);
  EXPECT_EQ(value(ExploreModel::InstalledSizeLabelRole).toString(), "4 KiB");
  EXPECT_EQ(value(ExploreModel::UrlRole), "vim.example.org");
  EXPECT_EQ(value(ExploreModel::LicensesRole).toStringList(), (QStringList{"GPL", "MIT"}));
  EXPECT_EQ(value(ExploreModel::DependenciesRole).toStringList(), QStringList{"glibc>=2.38"});
  EXPECT_EQ(value(ExploreModel::OptionalDependenciesRole).toStringList(), QStringList{"python: Python support"});
  EXPECT_TRUE(value(ExploreModel::IsInstalledRole).toBool());
  EXPECT_EQ(value(ExploreModel::InstalledVersionRole), "9.0-1");
  EXPECT_EQ(value(ExploreModel::InstalledBadgeTextRole), "Installed 9.0-1");
  EXPECT_TRUE(value(ExploreModel::InstalledVersionDiffersRole).toBool());
}

TEST(ExploreModel, EveryRoleHasAName) {
  auto source = std::make_shared<FakeExploreSource>();
  const auto model = loadedModel(source);

  const auto names = model->roleNames().values();

  for (const char* expected :
       {"name", "availableVersion", "repository", "description", "downloadSize", "downloadSizeLabel", "installedSize",
        "installedSizeLabel", "url", "licenses", "dependencies", "optionalDependencies", "isInstalled",
        "installedVersion", "installedBadgeText", "installedVersionDiffers"}) {
    EXPECT_TRUE(names.contains(expected)) << expected;
  }
}

TEST(ExploreModel, FreshModelStartsLoadingWithEmptyQueryAndNoRows) {
  auto source = std::make_shared<FakeExploreSource>();
  source->setBlocking(true);

  ExploreModel model(source, nullptr, fixedClock(), kDebounce);

  EXPECT_EQ(model.state(), ViewState::Loading);
  EXPECT_TRUE(model.searchText().isEmpty());
  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_EQ(model.currentRow(), -1);
  EXPECT_TRUE(model.currentPackage().isEmpty());
  source->release();
  ASSERT_TRUE(waitUntilIdle(model));
}

TEST(ExploreModel, LoadedWithoutQueryShowsHint) {
  auto source = std::make_shared<FakeExploreSource>();
  const auto model = vimModel(source);

  EXPECT_EQ(model->state(), ViewState::Hint);
  EXPECT_EQ(model->searchCount(), 0);
  EXPECT_TRUE(model->errorMessage().isEmpty());
}

TEST(ExploreModel, NoDatabasesState) {
  auto source = std::make_shared<FakeExploreSource>();
  source->enqueue(ExploreSnapshot{.packages = {}, .databasesFound = false, .dataAsOf = {}});

  const auto model = loadedModel(source);

  EXPECT_EQ(model->state(), ViewState::NoDatabases);
  EXPECT_FALSE(model->searchEnabled());
  EXPECT_FALSE(model->dataAsOf().isValid());
  EXPECT_FALSE(model->databasesStale());
}

TEST(ExploreModel, InitialLoadFailureIsErrorState) {
  auto source = std::make_shared<FakeExploreSource>();
  source->enqueue(std::unexpected(failure("db is gone")));

  const auto model = loadedModel(source);

  EXPECT_EQ(model->state(), ViewState::Error);
  EXPECT_EQ(model->errorMessage(), "db is gone");
  EXPECT_FALSE(model->searchEnabled());
  EXPECT_EQ(model->rowCount(), 0);
}

TEST(ExploreModel, ErrorThenReloadThenSuccessClearsTheError) {
  auto source = std::make_shared<FakeExploreSource>();
  source->enqueue(std::unexpected(failure("db is gone")));
  auto model = loadedModel(source);
  source->setBlocking(true);
  source->enqueue(snapshotWith(vimPackages()));

  model->reload();
  EXPECT_EQ(model->state(), ViewState::Loading);
  EXPECT_TRUE(model->loading());
  source->release();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_EQ(model->state(), ViewState::Hint);
  EXPECT_TRUE(model->errorMessage().isEmpty());
}

TEST(ExploreModel, FailedReloadWithIndexKeepsRowsAndReportsBanner) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  const auto as_of = model->dataAsOf();
  source->enqueue(std::unexpected(failure("boom")));

  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_EQ(model->state(), ViewState::Results);
  EXPECT_EQ(model->rowCount(), 6);
  EXPECT_EQ(model->reloadErrorMessage(), "boom");
  EXPECT_TRUE(model->errorMessage().isEmpty());
  EXPECT_EQ(model->dataAsOf(), as_of);
}

TEST(ExploreModel, LaterSuccessfulReloadClearsTheBanner) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  source->enqueue(std::unexpected(failure("boom")));
  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));
  ASSERT_FALSE(model->reloadErrorMessage().isEmpty());

  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_TRUE(model->reloadErrorMessage().isEmpty());
}

TEST(ExploreModel, ReloadThatFindsNoDatabasesDropsTheIndex) {
  auto source = std::make_shared<FakeExploreSource>();
  auto model = vimModel(source);
  search(*model, "vim");
  source->enqueue(ExploreSnapshot{.packages = {}, .databasesFound = false, .dataAsOf = {}});

  model->reload();
  ASSERT_TRUE(waitUntilIdle(*model));

  EXPECT_EQ(model->state(), ViewState::NoDatabases);
  EXPECT_EQ(model->rowCount(), 0);
  EXPECT_FALSE(model->searchEnabled());
}

TEST(ExploreModel, SourceExceptionBecomesError) {
  class ThrowingSource : public holonight_packages_domain::ExploreSource {
   public:
    [[nodiscard]] std::expected<ExploreSnapshot, ExploreSourceError> loadPackages() const override {
      throw std::runtime_error("kaboom");
    }
  };
  ExploreModel model(std::make_shared<ThrowingSource>(), nullptr, fixedClock(), kDebounce);
  ASSERT_TRUE(waitUntilIdle(model));

  EXPECT_EQ(model.state(), ViewState::Error);
  EXPECT_EQ(model.errorMessage(), "kaboom");
}

}  // namespace
