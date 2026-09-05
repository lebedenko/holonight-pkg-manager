#include "InstalledPackagesFilterModel.h"
#include "InstalledPackagesModel.h"
#include "mock_package_source.h"

#include <QElapsedTimer>
#include <QSignalSpy>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace {

using holonight_packages_application::MockPackageSource;
using holonight_packages_application::PackageListUseCase;
using holonight_packages_domain::InstallReason;
using holonight_packages_domain::Package;
using holonight_packages_domain::SourceType;
using ::testing::Return;

std::shared_ptr<PackageListUseCase> makeUseCase(const std::shared_ptr<MockPackageSource>& mock_source) {
  return std::make_shared<PackageListUseCase>(mock_source);
}

std::unique_ptr<InstalledPackagesModel> makeLoadedModel(const std::vector<Package>& packages) {
  auto mock_source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(packages));
  auto model = std::make_unique<InstalledPackagesModel>(makeUseCase(mock_source));
  QSignalSpy status_changed(model.get(), &InstalledPackagesModel::statusChanged);
  status_changed.wait(2000);
  return model;
}

QStringList namesInOrder(const InstalledPackagesFilterModel& filter_model) {
  QStringList names;
  for (int row = 0; row < filter_model.rowCount(); ++row) {
    names.append(filter_model.data(filter_model.index(row, 0), InstalledPackagesModel::NameRole).toString());
  }
  return names;
}

const std::vector<Package> kFixturePackages{
    Package{.name = "apple",
            .sourceType = SourceType::Official,
            .repository = "core",
            .installReason = InstallReason::Explicit,
            .sizeBytes = 300},
    Package{.name = "libapple",
            .sourceType = SourceType::Official,
            .repository = "community",
            .installReason = InstallReason::Explicit,
            .sizeBytes = 100},
    Package{.name = "zebra",
            .sourceType = SourceType::Official,
            .repository = "core",
            .installReason = InstallReason::Dependency,
            .sizeBytes = 200,
            .requiredBy = {"apple"}},
    Package{.name = "orphaned-lib",
            .sourceType = SourceType::Official,
            .repository = "community",
            .installReason = InstallReason::Dependency,
            .sizeBytes = 500,
            .requiredBy = {}},
    Package{.name = "foreign-tool", .sourceType = SourceType::Foreign, .installReason = InstallReason::Explicit},
};

TEST(InstalledPackagesFilterModel, DefaultsToExplicitTabSortedByNameAscending) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());

  // "foreign-tool" is Explicit (the fixture's default install reason) as well as Foreign, so it
  // belongs to both the Explicit and Foreign tabs -- the four tabs are independent categorizations,
  // not a mutually exclusive partition of the data (only tab *selection* in the UI is exclusive).
  EXPECT_EQ(filter_model.tabFilter(), InstalledPackagesFilterModel::TabFilter::Explicit);
  EXPECT_EQ(namesInOrder(filter_model), (QStringList{"apple", "foreign-tool", "libapple"}));
}

TEST(InstalledPackagesFilterModel, DependenciesTabShowsOnlyDependencyPackages) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());

  filter_model.setTabFilter(InstalledPackagesFilterModel::TabFilter::Dependencies);

  EXPECT_EQ(namesInOrder(filter_model), (QStringList{"orphaned-lib", "zebra"}));
}

TEST(InstalledPackagesFilterModel, ForeignTabShowsOnlyForeignPackages) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());

  filter_model.setTabFilter(InstalledPackagesFilterModel::TabFilter::Foreign);

  EXPECT_EQ(namesInOrder(filter_model), (QStringList{"foreign-tool"}));
}

TEST(InstalledPackagesFilterModel, OrphansTabShowsOnlyOrphanPackages) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());

  filter_model.setTabFilter(InstalledPackagesFilterModel::TabFilter::Orphans);

  EXPECT_EQ(namesInOrder(filter_model), (QStringList{"orphaned-lib"}));
}

TEST(InstalledPackagesFilterModel, SearchTextIsCaseInsensitiveAndComposesWithTab) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());

  filter_model.setSearchText("APPLE");

  EXPECT_EQ(namesInOrder(filter_model), (QStringList{"apple", "libapple"}));

  filter_model.setSearchText("");
  EXPECT_EQ(namesInOrder(filter_model), (QStringList{"apple", "foreign-tool", "libapple"}));
}

TEST(InstalledPackagesFilterModel, RepositoryFilterComposesWithTabAndSearch) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());
  filter_model.setTabFilter(InstalledPackagesFilterModel::TabFilter::Dependencies);

  filter_model.setRepositoryFilter("community");

  EXPECT_EQ(namesInOrder(filter_model), (QStringList{"orphaned-lib"}));
}

TEST(InstalledPackagesFilterModel, AvailableRepositoriesExcludesEmptyAndDuplicates) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());

  const QStringList repositories = filter_model.availableRepositories();

  EXPECT_EQ(repositories, (QStringList{"community", "core"}));
}

TEST(InstalledPackagesFilterModel, SortByNameDescendingReversesOrder) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());
  filter_model.setTabFilter(InstalledPackagesFilterModel::TabFilter::Dependencies);

  filter_model.setSortDescending(true);

  EXPECT_EQ(namesInOrder(filter_model), (QStringList{"zebra", "orphaned-lib"}));
}

TEST(InstalledPackagesFilterModel, SortBySizeOrdersNumerically) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());
  filter_model.setTabFilter(InstalledPackagesFilterModel::TabFilter::Dependencies);

  filter_model.setSortField(InstalledPackagesFilterModel::SortField::Size);

  EXPECT_EQ(namesInOrder(filter_model), (QStringList{"zebra", "orphaned-lib"}));

  filter_model.setSortDescending(true);
  EXPECT_EQ(namesInOrder(filter_model), (QStringList{"orphaned-lib", "zebra"}));
}

TEST(InstalledPackagesFilterModel, CurrentRowDefaultsToFirstVisibleRow) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());

  EXPECT_EQ(filter_model.currentRow(), 0);
  EXPECT_EQ(filter_model.data(filter_model.index(0, 0), InstalledPackagesModel::NameRole).toString(),
            QStringLiteral("apple"));
}

TEST(InstalledPackagesFilterModel, CurrentRowSurvivesReorderingBySearch) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());
  // Default Explicit tab order: apple, foreign-tool, libapple.
  filter_model.setCurrentRow(2);
  ASSERT_EQ(filter_model.data(filter_model.index(2, 0), InstalledPackagesModel::NameRole).toString(),
            QStringLiteral("libapple"));

  filter_model.setSortDescending(true);

  EXPECT_EQ(
      filter_model.data(filter_model.index(filter_model.currentRow(), 0), InstalledPackagesModel::NameRole).toString(),
      QStringLiteral("libapple"));
}

TEST(InstalledPackagesFilterModel, CurrentRowResetsToFirstRemainingRowWhenFilteredOut) {
  auto model = makeLoadedModel(kFixturePackages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());
  // Default Explicit tab order: apple, foreign-tool, libapple.
  filter_model.setCurrentRow(2);
  ASSERT_EQ(filter_model.data(filter_model.index(2, 0), InstalledPackagesModel::NameRole).toString(),
            QStringLiteral("libapple"));

  filter_model.setSearchText("apple");
  filter_model.setSearchText("zzz-not-present");

  EXPECT_EQ(filter_model.currentRow(), -1);
}

TEST(InstalledPackagesFilterModel, FilterSortAndSearchStayUnder100MillisecondsAtFiveThousandPackages) {
  std::vector<Package> packages;
  packages.reserve(5000);
  for (int i = 0; i < 5000; ++i) {
    const std::string repository = i % 2 == 0 ? "core" : "extra";
    packages.push_back(Package{
        .name = "package-" + std::to_string(i),
        .sourceType = i % 5 == 0 ? SourceType::Foreign : SourceType::Official,
        .repository = i % 5 == 0 ? "" : repository,
        .installReason = i % 3 == 0 ? InstallReason::Dependency : InstallReason::Explicit,
        .sizeBytes = static_cast<std::uint64_t>(i) * 1024,
        .requiredBy = i % 3 == 0 && i % 2 == 0 ? std::vector<std::string>{"package-0"} : std::vector<std::string>{},
    });
  }
  auto model = makeLoadedModel(packages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());

  QElapsedTimer timer;

  timer.start();
  filter_model.setTabFilter(InstalledPackagesFilterModel::TabFilter::Dependencies);
  EXPECT_LT(timer.elapsed(), 100);

  timer.start();
  filter_model.setSearchText("package-4");
  EXPECT_LT(timer.elapsed(), 100);

  timer.start();
  filter_model.setSortField(InstalledPackagesFilterModel::SortField::Size);
  EXPECT_LT(timer.elapsed(), 100);

  timer.start();
  filter_model.setSortDescending(true);
  EXPECT_LT(timer.elapsed(), 100);

  timer.start();
  filter_model.setRepositoryFilter("core");
  EXPECT_LT(timer.elapsed(), 100);
}

TEST(InstalledPackagesFilterModel, RepositoryFilteringPreservesLateSelectionWithinResponsivenessBudget) {
  std::vector<Package> packages;
  packages.reserve(5000);
  for (int i = 0; i < 5000; ++i) {
    packages.push_back(Package{.name = QStringLiteral("package-%1").arg(i, 4, 10, QLatin1Char('0')).toStdString(),
                               .repository = i % 2 == 0 ? "core" : "extra"});
  }
  auto model = makeLoadedModel(packages);
  InstalledPackagesFilterModel filter_model;
  filter_model.setSourceModel(model.get());
  filter_model.setCurrentRow(4999);

  QElapsedTimer timer;
  timer.start();
  filter_model.setRepositoryFilter(QStringLiteral("extra"));

  EXPECT_LT(timer.elapsed(), 100);
  EXPECT_EQ(filter_model.rowCount(), 2500);
  EXPECT_EQ(filter_model.get(filter_model.currentRow()).value(QStringLiteral("name")).toString(),
            QStringLiteral("package-4999"));

  timer.restart();
  filter_model.setRepositoryFilter(QString());
  EXPECT_LT(timer.elapsed(), 100);
  EXPECT_EQ(filter_model.currentRow(), 4999);
}

}  // namespace
