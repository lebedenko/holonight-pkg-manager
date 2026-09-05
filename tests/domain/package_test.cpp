#include "holonight_packages_domain/package.h"

#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace holonight_packages_domain {
namespace {

TEST(Package, DefaultConstructionLeavesEmptyStringsAndForeignExplicitDefaults) {
  Package package;

  EXPECT_TRUE(package.identity.empty());
  EXPECT_TRUE(package.name.empty());
  EXPECT_TRUE(package.installedVersion.empty());
  EXPECT_EQ(package.sourceType, SourceType::Foreign);
  EXPECT_TRUE(package.repository.empty());
  EXPECT_EQ(package.installReason, InstallReason::Explicit);
  EXPECT_TRUE(package.backendSpecificId.empty());
  EXPECT_EQ(package.sizeBytes, 0U);
  EXPECT_TRUE(package.description.empty());
  EXPECT_EQ(package.installDate, std::chrono::system_clock::time_point{});
  EXPECT_TRUE(package.requiredBy.empty());
  EXPECT_TRUE(package.optionalDependencies.empty());
  EXPECT_EQ(package.configFileCount, 0U);
}

TEST(Package, FieldAssignmentRoundTripsThroughAllSevenFields) {
  Package package = {
      .identity = "apple",
      .name = "apple",
      .installedVersion = "2.3-4",
      .sourceType = SourceType::Official,
      .repository = "core",
      .installReason = InstallReason::Dependency,
      .backendSpecificId = "apple",
  };

  EXPECT_EQ(package.identity, "apple");
  EXPECT_EQ(package.name, "apple");
  EXPECT_EQ(package.installedVersion, "2.3-4");
  EXPECT_EQ(package.sourceType, SourceType::Official);
  EXPECT_EQ(package.repository, "core");
  EXPECT_EQ(package.installReason, InstallReason::Dependency);
  EXPECT_EQ(package.backendSpecificId, "apple");
}

TEST(Package, FieldAssignmentRoundTripsThroughAllSixExtendedFields) {
  const auto install_date = std::chrono::system_clock::from_time_t(1700000000);
  Package package = {
      .sizeBytes = 123456,
      .description = "a tasty fruit",
      .installDate = install_date,
      .requiredBy = {"pie", "juice"},
      .optionalDependencies = {"sugar: for sweetness"},
      .configFileCount = 2,
  };

  EXPECT_EQ(package.sizeBytes, 123456U);
  EXPECT_EQ(package.description, "a tasty fruit");
  EXPECT_EQ(package.installDate, install_date);
  EXPECT_EQ(package.requiredBy, (std::vector<std::string>{"pie", "juice"}));
  EXPECT_EQ(package.optionalDependencies, (std::vector<std::string>{"sugar: for sweetness"}));
  EXPECT_EQ(package.configFileCount, 2U);
}

TEST(Package, EqualityComparesAllSevenFields) {
  Package base = {
      .identity = "apple",
      .name = "apple",
      .installedVersion = "2.3-4",
      .sourceType = SourceType::Official,
      .repository = "core",
      .installReason = InstallReason::Explicit,
      .backendSpecificId = "apple",
  };
  const Package& same = base;
  Package different_version = base;
  different_version.installedVersion = "2.3-5";

  EXPECT_EQ(base, same);
  EXPECT_NE(base, different_version);
}

TEST(Package, EqualityComparesExtendedFieldsToo) {
  Package base = {
      .sizeBytes = 100,
      .description = "desc",
      .requiredBy = {"a"},
      .optionalDependencies = {"b"},
      .configFileCount = 1,
  };
  Package different_size = base;
  different_size.sizeBytes = 200;

  EXPECT_NE(base, different_size);
}

}  // namespace
}  // namespace holonight_packages_domain
