#include "version_policy.h"

#include <gtest/gtest.h>

namespace holonight_packages_transaction {
namespace {

TEST(VersionPolicyTest, OrdersVersionsWithLibalpmRules) {
  EXPECT_LT(packageVersionOrder("1.0-1", "1.0-2"), 0);
  EXPECT_GT(packageVersionOrder("2.0-1", "1.9-9"), 0);
  EXPECT_EQ(packageVersionOrder("1.0-1", "1.0-1"), 0);
  EXPECT_GT(packageVersionOrder("1:0.1-1", "9.9-1"), 0);  // an epoch outranks the version
}

}  // namespace
}  // namespace holonight_packages_transaction
