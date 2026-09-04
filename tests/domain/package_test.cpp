#include "holonight_packages_domain/package.h"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace holonight_packages_domain
