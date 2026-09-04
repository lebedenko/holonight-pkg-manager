#include "holonight_packages_application/package_list_use_case.h"

#include "mock_package_source.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <vector>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::Package;
using holonight_packages_domain::PackageSourceError;
using holonight_packages_domain::PackageSourceErrorCode;
using ::testing::Return;

TEST(PackageListUseCase, RejectsNullPackageSource) { EXPECT_THROW(PackageListUseCase(nullptr), std::invalid_argument); }

TEST(PackageListUseCase, ReturnsPackagesSortedAlphabeticallyByName) {
  auto mock_source = std::make_shared<MockPackageSource>();
  const std::vector<Package> unsorted{
      Package{.name = "zebra"},
      Package{.name = "apple"},
      Package{.name = "banana"},
  };
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(unsorted));

  const PackageListUseCase use_case(mock_source);
  const auto result = use_case.enumerateInstalledPackages();

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3U);
  EXPECT_EQ((*result)[0].name, "apple");
  EXPECT_EQ((*result)[1].name, "banana");
  EXPECT_EQ((*result)[2].name, "zebra");
}

TEST(PackageListUseCase, SortIsCaseInsensitive) {
  auto mock_source = std::make_shared<MockPackageSource>();
  const std::vector<Package> unsorted{
      Package{.name = "Zebra"},
      Package{.name = "apple"},
      Package{.name = "Banana"},
  };
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(unsorted));

  const PackageListUseCase use_case(mock_source);
  const auto result = use_case.enumerateInstalledPackages();

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3U);
  EXPECT_EQ((*result)[0].name, "apple");
  EXPECT_EQ((*result)[1].name, "Banana");
  EXPECT_EQ((*result)[2].name, "Zebra");
}

TEST(PackageListUseCase, CaseOnlyTiesUseDeterministicBytewiseOrder) {
  auto mock_source = std::make_shared<MockPackageSource>();
  const std::vector<Package> unsorted{
      Package{.name = "apple"},
      Package{.name = "APPLE"},
      Package{.name = "Apple"},
  };
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(unsorted));

  const PackageListUseCase use_case(mock_source);
  const auto result = use_case.enumerateInstalledPackages();

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3U);
  EXPECT_EQ((*result)[0].name, "APPLE");
  EXPECT_EQ((*result)[1].name, "Apple");
  EXPECT_EQ((*result)[2].name, "apple");
}

TEST(PackageListUseCase, PassesThroughBackendErrorUnchanged) {
  auto mock_source = std::make_shared<MockPackageSource>();
  const PackageSourceError expected_error{.code = PackageSourceErrorCode::DatabaseOpenFailed,
                                          .message = "fixture failure"};
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(std::unexpected(expected_error)));

  const PackageListUseCase use_case(mock_source);
  const auto result = use_case.enumerateInstalledPackages();

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, PackageSourceErrorCode::DatabaseOpenFailed);
  EXPECT_EQ(result.error().message, "fixture failure");
}

TEST(PackageListUseCase, EmptyBackendResultReturnsEmptyListWithoutError) {
  auto mock_source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*mock_source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{}));

  const PackageListUseCase use_case(mock_source);
  const auto result = use_case.enumerateInstalledPackages();

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());
}

}  // namespace
}  // namespace holonight_packages_application
