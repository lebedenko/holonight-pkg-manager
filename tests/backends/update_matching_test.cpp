#include "update_matching.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace holonight_packages_backends {
namespace {

const std::vector<std::string> kNoGroups;

TEST(UpdateMatching, IgnorePkgGlobMatchesName) {
  const std::vector<std::string> ignore_pkgs = {"ign*"};

  EXPECT_TRUE(isIgnored("ignoreme", kNoGroups, ignore_pkgs, {}));
  EXPECT_FALSE(isIgnored("alpha", kNoGroups, ignore_pkgs, {}));
}

TEST(UpdateMatching, IgnorePkgExactNameMatches) {
  const std::vector<std::string> ignore_pkgs = {"linux", "mesa"};

  EXPECT_TRUE(isIgnored("mesa", kNoGroups, ignore_pkgs, {}));
  EXPECT_FALSE(isIgnored("linux-headers", kNoGroups, ignore_pkgs, {}));
}

TEST(UpdateMatching, IgnoreGroupMatchesAnyGroupOfThePackage) {
  const std::vector<std::string> groups = {"desserts", "fruits"};
  const std::vector<std::string> ignore_groups = {"fruits"};

  EXPECT_TRUE(isIgnored("banana", groups, {}, ignore_groups));
}

TEST(UpdateMatching, IgnoreGroupGlobMatches) {
  const std::vector<std::string> groups = {"kde-applications"};
  const std::vector<std::string> ignore_groups = {"kde-*"};

  EXPECT_TRUE(isIgnored("dolphin", groups, {}, ignore_groups));
}

TEST(UpdateMatching, NonMatchingPackageIsNotIgnored) {
  const std::vector<std::string> groups = {"vegetables"};
  const std::vector<std::string> ignore_pkgs = {"ign*"};
  const std::vector<std::string> ignore_groups = {"fruits"};

  EXPECT_FALSE(isIgnored("carrot", groups, ignore_pkgs, ignore_groups));
}

TEST(UpdateMatching, EmptyListsIgnoreNothing) { EXPECT_FALSE(isIgnored("anything", kNoGroups, {}, {})); }

}  // namespace
}  // namespace holonight_packages_backends
