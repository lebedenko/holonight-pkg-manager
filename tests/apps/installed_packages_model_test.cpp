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

TEST(InstalledPackagesModel, ExtendedRolesAndAggregatesReflectFixturePackages) {
  auto mock_source = std::make_shared<MockPackageSource>();
  const std::vector<Package> packages{
      Package{.name = "apple",
              .installedVersion = "2.3-4",
              .sourceType = SourceType::Official,
              .repository = "core",
              .installReason = InstallReason::Explicit,
              .sizeBytes = 1024,
              .description = "a tasty fruit",
              .requiredBy = {},
              .optionalDependencies = {"juicer"},
              .configFileCount = 1},
      Package{.name = "zebra",
              .installedVersion = "1.0-1",
              .sourceType = SourceType::Official,
              .repository = "core",
              .installReason = InstallReason::Dependency,
              .sizeBytes = 2048,
              .requiredBy = {"apple"}},
      Package{.name = "orphaned-lib",
              .installedVersion = "0.1-1",
              .sourceType = SourceType::Official,
              .repository = "core",
              .installReason = InstallReason::Dependency,
              .sizeBytes = 4096,
              .requiredBy = {}},
      Package{.name = "foreign-tool",
              .installedVersion = "9.9-1",
              .sourceType = SourceType::Foreign,
              .installReason = InstallReason::Explicit,
              .sizeBytes = 512},
  };
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(packages));

  InstalledPackagesModel model(makeUseCase(mock_source));
  QSignalSpy status_changed(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(status_changed.wait(2000));
  ASSERT_EQ(model.status(), InstalledPackagesModel::Status::Loaded);

  const QModelIndex apple_index = model.index(0, 0);
  EXPECT_EQ(model.data(apple_index, InstalledPackagesModel::SizeRole).toULongLong(), 1024ULL);
  EXPECT_EQ(model.data(apple_index, InstalledPackagesModel::SizeLabelRole).toString(), QStringLiteral("1 KiB"));
  EXPECT_EQ(model.data(apple_index, InstalledPackagesModel::DescriptionRole).toString(),
            QStringLiteral("a tasty fruit"));
  EXPECT_EQ(model.data(apple_index, InstalledPackagesModel::RequiredByCountRole).toInt(), 0);
  EXPECT_EQ(model.data(apple_index, InstalledPackagesModel::OptionalDependenciesRole).toStringList(),
            QStringList{QStringLiteral("juicer")});
  EXPECT_EQ(model.data(apple_index, InstalledPackagesModel::ConfigFileCountRole).toInt(), 1);
  EXPECT_FALSE(model.data(apple_index, InstalledPackagesModel::IsOrphanRole).toBool());

  // Rows are sorted alphabetically by PackageListUseCase: apple, foreign-tool, orphaned-lib, zebra.
  const QModelIndex zebra_index = model.index(3, 0);
  EXPECT_FALSE(model.data(zebra_index, InstalledPackagesModel::IsOrphanRole).toBool());
  EXPECT_EQ(model.data(zebra_index, InstalledPackagesModel::RequiredByListRole).toStringList(),
            QStringList{QStringLiteral("apple")});

  const QModelIndex orphan_index = model.index(2, 0);
  EXPECT_TRUE(model.data(orphan_index, InstalledPackagesModel::IsOrphanRole).toBool());

  EXPECT_EQ(model.totalPackageCount(), 4);
  EXPECT_EQ(model.totalInstalledSizeBytes(), 1024ULL + 2048ULL + 4096ULL + 512ULL);
  EXPECT_EQ(model.explicitPackageCount(), 2);
  EXPECT_EQ(model.dependencyPackageCount(), 2);
  EXPECT_EQ(model.foreignPackageCount(), 1);
  EXPECT_EQ(model.orphanPackageCount(), 1);
  EXPECT_EQ(model.reclaimableSizeBytes(), 4096ULL);
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

TEST(InstalledPackagesModel, RoleNamesExposesExtendedRoles) {
  auto mock_source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{}));
  const InstalledPackagesModel model(makeUseCase(mock_source));

  const QHash<int, QByteArray> roles = model.roleNames();

  EXPECT_EQ(roles.value(InstalledPackagesModel::SizeRole), QByteArrayLiteral("size"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::SizeLabelRole), QByteArrayLiteral("sizeLabel"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::DescriptionRole), QByteArrayLiteral("description"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::InstallDateRole), QByteArrayLiteral("installDate"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::RequiredByCountRole), QByteArrayLiteral("requiredByCount"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::RequiredByListRole), QByteArrayLiteral("requiredByList"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::OptionalDependenciesRole), QByteArrayLiteral("optionalDependencies"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::ConfigFileCountRole), QByteArrayLiteral("configFileCount"));
  EXPECT_EQ(roles.value(InstalledPackagesModel::IsOrphanRole), QByteArrayLiteral("isOrphan"));
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

TEST(InstalledPackagesModel, ReentrantRefreshWithImmediateFutureStartsExactlyOneNewLoad) {
  auto mock_source = std::make_shared<MockPackageSource>();
  {
    ::testing::InSequence sequence;
    EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{}));
    EXPECT_CALL(*mock_source, enumerateInstalledPackages()).Times(1).WillOnce(Return(std::vector<Package>{}));
  }

  InstalledPackagesModel model(makeUseCase(mock_source));
  QSignalSpy initial_status_changed(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(initial_status_changed.wait(2000));
  ASSERT_EQ(model.status(), InstalledPackagesModel::Status::Loaded);

  bool reentered = false;
  QObject::connect(&model, &InstalledPackagesModel::statusChanged, [&model, &reentered] {
    if (!reentered && model.status() == InstalledPackagesModel::Status::Loading) {
      reentered = true;
      model.refresh();
    }
  });

  QSignalSpy status_changed(&model, &InstalledPackagesModel::statusChanged);
  model.refresh();
  ASSERT_TRUE(status_changed.wait(2000));

  EXPECT_TRUE(reentered);
  EXPECT_EQ(model.status(), InstalledPackagesModel::Status::Loaded);
}

}  // namespace
