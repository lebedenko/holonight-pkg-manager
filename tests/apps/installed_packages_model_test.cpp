#include "InstalledPackagesModel.h"
#include "mock_package_source.h"

#include <QSignalSpy>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using holonight_packages_application::MockPackageSource;
using holonight_packages_application::PackageListUseCase;
using holonight_packages_domain::InstallReason;
using holonight_packages_domain::Package;
using holonight_packages_domain::PackageSourceError;
using holonight_packages_domain::PackageSourceErrorCode;
using holonight_packages_domain::SourceType;
using ::testing::Return;
using LoadResult = std::expected<std::vector<Package>, PackageSourceError>;

std::shared_ptr<PackageListUseCase> makeUseCase(const std::shared_ptr<MockPackageSource>& mock_source) {
  return std::make_shared<PackageListUseCase>(mock_source);
}

TEST(InstalledPackagesModel, RejectsNullUseCase) {
  EXPECT_THROW(InstalledPackagesModel(nullptr), std::invalid_argument);
}

TEST(InstalledPackagesModel, ConstructorStartsInLoadingState) {
  auto mock_source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{}));

  const InstalledPackagesModel model(makeUseCase(mock_source));

  EXPECT_EQ(model.status(), InstalledPackagesModel::Status::Loading);
}

TEST(InstalledPackagesModel, SuccessfulLoadTransitionsToLoadedWithRoleData) {
  auto mock_source = std::make_shared<MockPackageSource>();
  const std::vector<Package> packages{
      Package{.name = "apple",
              .installedVersion = "2.3-4",
              .sourceType = SourceType::Official,
              .repository = "core",
              .installReason = InstallReason::Explicit},
  };
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(packages));

  InstalledPackagesModel model(makeUseCase(mock_source));
  QSignalSpy status_changed(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(status_changed.wait(2000));

  EXPECT_EQ(model.status(), InstalledPackagesModel::Status::Loaded);
  ASSERT_EQ(model.rowCount(), 1);
  const QModelIndex index = model.index(0, 0);
  EXPECT_EQ(model.data(index, InstalledPackagesModel::NameRole).toString(), QStringLiteral("apple"));
  EXPECT_EQ(model.data(index, InstalledPackagesModel::InstalledVersionRole).toString(), QStringLiteral("2.3-4"));
  EXPECT_EQ(model.data(index, InstalledPackagesModel::SourceLabelRole).toString(), QStringLiteral("official"));
  EXPECT_EQ(model.data(index, InstalledPackagesModel::RepositoryRole).toString(), QStringLiteral("core"));
}

TEST(InstalledPackagesModel, BackendErrorTransitionsToErrorStateWithMessage) {
  auto mock_source = std::make_shared<MockPackageSource>();
  const PackageSourceError error{.code = PackageSourceErrorCode::DatabaseOpenFailed, .message = "boom"};
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(std::unexpected(error)));

  InstalledPackagesModel model(makeUseCase(mock_source));
  QSignalSpy status_changed(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(status_changed.wait(2000));

  EXPECT_EQ(model.status(), InstalledPackagesModel::Status::Error);
  EXPECT_EQ(model.errorMessage(), QStringLiteral("boom"));
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(InstalledPackagesModel, SourceExceptionTransitionsToErrorState) {
  auto mock_source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce([]() -> LoadResult {
    throw std::runtime_error("unexpected backend exception");
  });

  InstalledPackagesModel model(makeUseCase(mock_source));
  QSignalSpy status_changed(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(status_changed.wait(2000));

  EXPECT_EQ(model.status(), InstalledPackagesModel::Status::Error);
  EXPECT_EQ(model.errorMessage(), QStringLiteral("unexpected backend exception"));
}

TEST(InstalledPackagesModel, DelayedLoadKeepsEventLoopResponsiveAndUpdatesPromptly) {
  auto mock_source = std::make_shared<MockPackageSource>();
  std::atomic<std::int64_t> completed_at_nanoseconds{0};
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce([&completed_at_nanoseconds] {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    completed_at_nanoseconds.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                                   std::memory_order_release);
    return LoadResult(std::vector<Package>{});
  });

  QTimer heartbeat;
  heartbeat.setInterval(10);
  int heartbeat_count = 0;
  QObject::connect(&heartbeat, &QTimer::timeout, [&heartbeat_count] { ++heartbeat_count; });
  heartbeat.start();

  InstalledPackagesModel model(makeUseCase(mock_source));
  QSignalSpy status_changed(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(status_changed.wait(2500));

  const auto observed_at = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto completed_at = completed_at_nanoseconds.load(std::memory_order_acquire);
  EXPECT_GT(heartbeat_count, 20);
  EXPECT_EQ(model.status(), InstalledPackagesModel::Status::Loaded);
  EXPECT_LT(std::chrono::steady_clock::duration(observed_at - completed_at), std::chrono::milliseconds(100));
}

TEST(InstalledPackagesModel, RoleNamesExposesFourRoles) {
  auto mock_source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{}));
  const InstalledPackagesModel model(makeUseCase(mock_source));

  const QHash<int, QByteArray> roles = model.roleNames();

  EXPECT_EQ(roles.value(InstalledPackagesModel::NameRole), QByteArrayLiteral("name"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::InstalledVersionRole), QByteArrayLiteral("installedVersion"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::SourceLabelRole), QByteArrayLiteral("sourceLabel"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::RepositoryRole), QByteArrayLiteral("repository"));
}

TEST(InstalledPackagesModel, RefreshWhileRunningIsANoOp) {
  auto mock_source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).Times(1).WillOnce([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return std::expected<std::vector<Package>, PackageSourceError>(std::vector<Package>{});
  });

  InstalledPackagesModel model(makeUseCase(mock_source));
  model.refresh();

  QSignalSpy status_changed(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(status_changed.wait(2000));
  EXPECT_EQ(model.status(), InstalledPackagesModel::Status::Loaded);
}

}  // namespace
