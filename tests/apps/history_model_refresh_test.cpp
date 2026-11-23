#include "ExploreModel.h"
#include "HistoryModel.h"
#include "InstalledPackagesModel.h"
#include "ModelListRefresh.h"
#include "UpdatesModel.h"
#include "counting_history_source.h"
#include "fake_explore_source.h"
#include "fake_update_check_client.h"
#include "fake_update_source.h"
#include "mock_package_source.h"

#include <QTest>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

using holonight_packages_application::MockPackageSource;
using holonight_packages_application::PackageListUseCase;
using holonight_packages_domain::HistoryReadMode;
using holonight_packages_domain::Package;
using holonight_packages_testing::FakeExploreSource;
using holonight_test::CountingHistorySource;
using ::testing::ElementsAre;

constexpr int kWaitMs = 3000;

// transaction-history REQ-F-031: every own transaction outcome refreshes the opened History page incrementally.
class HistoryModelRefreshTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EXPECT_CALL(*package_source_, enumerateInstalledPackages()).WillRepeatedly([] {
      return std::expected<std::vector<Package>, holonight_packages_domain::PackageSourceError>(std::vector<Package>{});
    });
    ASSERT_TRUE(QTest::qWaitFor(
        [this] { return !explore_.loading() && installed_.status() != InstalledPackagesModel::Status::Loading; },
        kWaitMs));
    refresh_.setHistoryTarget(&history_);
  }

  [[nodiscard]] bool waitForReads(std::size_t count) {
    return QTest::qWaitFor([this, count] { return history_source_->calls() == count && !history_.loading(); }, kWaitMs);
  }

  [[nodiscard]] ModelListRefresh& refresh() { return refresh_; }
  [[nodiscard]] HistoryModel& history() { return history_; }
  [[nodiscard]] CountingHistorySource& historySource() { return *history_source_; }

 private:
  std::shared_ptr<MockPackageSource> package_source_ = std::make_shared<MockPackageSource>();
  InstalledPackagesModel installed_{std::make_shared<PackageListUseCase>(package_source_)};
  ExploreModel explore_{std::make_shared<FakeExploreSource>()};
  std::shared_ptr<CountingHistorySource> history_source_ = std::make_shared<CountingHistorySource>();
  HistoryModel history_{history_source_, HistoryModelOptions{.current_uid = 1000, .resolve_user = {}}};
  ModelListRefresh refresh_{installed_, explore_};
};

TEST_F(HistoryModelRefreshTest, AnOutcomeBeforeTheFirstOpenReadsNothing) {
  refresh().refreshAfterOutcome();
  refresh().refreshAfterUpgrade();
  QTest::qWait(100);
  EXPECT_EQ(historySource().calls(), 0U);
}

TEST_F(HistoryModelRefreshTest, AnInstallOrRemoveOutcomeIsOneIncrementalRead) {
  history().ensureLoaded();
  ASSERT_TRUE(waitForReads(1));
  refresh().refreshAfterOutcome();
  ASSERT_TRUE(waitForReads(2));
  QTest::qWait(100);
  EXPECT_THAT(historySource().modes(), ElementsAre(HistoryReadMode::Full, HistoryReadMode::Incremental));
}

TEST_F(HistoryModelRefreshTest, AnUpgradeOutcomeIsOneIncrementalRead) {
  history().ensureLoaded();
  ASSERT_TRUE(waitForReads(1));
  refresh().refreshAfterUpgrade();
  ASSERT_TRUE(waitForReads(2));
  QTest::qWait(100);
  EXPECT_THAT(historySource().modes(), ElementsAre(HistoryReadMode::Full, HistoryReadMode::Incremental));
}

}  // namespace
