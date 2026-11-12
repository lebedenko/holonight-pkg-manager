#include "ExploreModel.h"
#include "InstalledPackagesModel.h"
#include "ModelListRefresh.h"
#include "UpdatesModel.h"
#include "fake_explore_source.h"
#include "fake_update_check_client.h"
#include "fake_update_source.h"
#include "mock_package_source.h"

#include <QTest>

#include <atomic>
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>

namespace {

using holonight_packages_application::MockPackageSource;
using holonight_packages_application::PackageListUseCase;
using holonight_packages_domain::Package;
using holonight_packages_testing::FakeExploreSource;
using ::testing::_;

TEST(ModelListRefresh, ARefreshDuringAnInstalledLoadIsRepeatedExactlyOnceAfterTheLoad) {
  auto mock_source = std::make_shared<MockPackageSource>();
  std::atomic<int> calls{0};
  std::promise<void> gate;
  std::shared_future<void> gate_future = gate.get_future().share();
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillRepeatedly([&] {
    if (calls.fetch_add(1) == 0) {
      gate_future.wait();  // hold the first load so the request lands while it is running
    }
    return std::expected<std::vector<Package>, holonight_packages_domain::PackageSourceError>(std::vector<Package>{});
  });
  InstalledPackagesModel installed(std::make_shared<PackageListUseCase>(mock_source));
  auto explore_source = std::make_shared<FakeExploreSource>();
  ExploreModel explore(explore_source);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !explore.loading(); }, 2000));
  ModelListRefresh refresh(installed, explore);

  ASSERT_EQ(installed.status(), InstalledPackagesModel::Status::Loading);
  refresh.refreshAfterOutcome();
  EXPECT_EQ(calls.load(), 1) << "the load is still running; nothing was started yet";

  gate.set_value();
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return calls.load() == 2 && installed.status() != InstalledPackagesModel::Status::Loading; }, 3000));
  QTest::qWait(150);
  EXPECT_EQ(calls.load(), 2);
}

TEST(ModelListRefresh, ExploreIsReloadedOncePerOutcome) {
  auto mock_source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillRepeatedly([] {
    return std::expected<std::vector<Package>, holonight_packages_domain::PackageSourceError>(std::vector<Package>{});
  });
  InstalledPackagesModel installed(std::make_shared<PackageListUseCase>(mock_source));
  auto explore_source = std::make_shared<FakeExploreSource>();
  ExploreModel explore(explore_source);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return !explore.loading() && installed.status() != InstalledPackagesModel::Status::Loading; }, 2000));
  ASSERT_EQ(explore_source->callCount(), 1);
  ModelListRefresh refresh(installed, explore);

  refresh.refreshAfterOutcome();
  ASSERT_TRUE(QTest::qWaitFor([&] { return explore_source->callCount() == 2 && !explore.loading(); }, 2000));
  QTest::qWait(100);
  EXPECT_EQ(explore_source->callCount(), 2);
}

TEST(ModelListRefresh, ARefreshDuringAnExploreLoadIsRepeatedAfterIt) {
  auto mock_source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillRepeatedly([] {
    return std::expected<std::vector<Package>, holonight_packages_domain::PackageSourceError>(std::vector<Package>{});
  });
  InstalledPackagesModel installed(std::make_shared<PackageListUseCase>(mock_source));
  auto explore_source = std::make_shared<FakeExploreSource>();
  explore_source->setBlocking(true);
  ExploreModel explore(explore_source);
  ModelListRefresh refresh(installed, explore);
  ASSERT_TRUE(explore.loading());

  refresh.refreshAfterOutcome();
  explore_source->release();  // finish the initial load
  explore_source->release();  // allow the repeated load
  ASSERT_TRUE(QTest::qWaitFor([&] { return explore_source->callCount() == 2 && !explore.loading(); }, 3000));
  QTest::qWait(100);
  EXPECT_EQ(explore_source->callCount(), 2);
}

// system-upgrade T-069, REQ-F-088: an upgrade outcome re-reads every list and asks for a new update count once.
TEST(ModelListRefresh, AnUpgradeOutcomeReReadsTheThreeListsAndCallsCheckNowOnce) {
  auto mock_source = std::make_shared<MockPackageSource>();
  std::atomic<int> installedCalls{0};
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillRepeatedly([&installedCalls] {
    ++installedCalls;
    return std::expected<std::vector<Package>, holonight_packages_domain::PackageSourceError>(std::vector<Package>{});
  });
  InstalledPackagesModel installed(std::make_shared<PackageListUseCase>(mock_source));
  auto explore_source = std::make_shared<FakeExploreSource>();
  ExploreModel explore(explore_source);
  auto update_source = std::make_shared<holonight_packages_testing::FakeUpdateSource>();
  UpdatesModel updates(update_source);
  holonight_packages_testing::FakeUpdateCheckClient check;
  ASSERT_TRUE(QTest::qWaitFor(
      [&] {
        return !explore.loading() && !updates.loading() &&
               installed.status() != InstalledPackagesModel::Status::Loading;
      },
      2000));
  const int installedBefore = installedCalls.load();
  const int exploreBefore = explore_source->callCount();
  const int updatesBefore = update_source->callCount();
  ModelListRefresh refresh(installed, explore);
  refresh.setUpgradeTargets(&updates, &check);

  refresh.refreshAfterUpgrade();
  ASSERT_TRUE(QTest::qWaitFor(
      [&] {
        return installedCalls.load() == installedBefore + 1 && explore_source->callCount() == exploreBefore + 1 &&
               update_source->callCount() == updatesBefore + 1 && !updates.loading() && !explore.loading();
      },
      3000));
  QTest::qWait(100);
  EXPECT_EQ(installedCalls.load(), installedBefore + 1);
  EXPECT_EQ(explore_source->callCount(), exploreBefore + 1);
  EXPECT_EQ(update_source->callCount(), updatesBefore + 1);
  EXPECT_EQ(check.checkNowCalls(), 1);

  // The install and remove refresh leaves the Updates list and the count alone.
  refresh.refreshAfterOutcome();
  QTest::qWait(150);
  EXPECT_EQ(update_source->callCount(), updatesBefore + 1);
  EXPECT_EQ(check.checkNowCalls(), 1);
}

}  // namespace
