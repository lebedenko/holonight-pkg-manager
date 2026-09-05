#include "holonight_packages_application/package_size_formatter.h"

#include <gtest/gtest.h>

namespace holonight_packages_application {
namespace {

TEST(PackageSizeFormatter, ZeroBytes) { EXPECT_EQ(formatSizeBytes(0), "0 B"); }

TEST(PackageSizeFormatter, JustBelowOneKibibyte) { EXPECT_EQ(formatSizeBytes(1023), "1023 B"); }

TEST(PackageSizeFormatter, ExactlyOneKibibyte) { EXPECT_EQ(formatSizeBytes(1024), "1 KiB"); }

TEST(PackageSizeFormatter, JustBelowOneMebibyteStaysInKibibytes) {
  EXPECT_EQ(formatSizeBytes((1024ULL * 1024) - 1), "1024 KiB");
}

TEST(PackageSizeFormatter, OneAndAHalfMebibytes) { EXPECT_EQ(formatSizeBytes(1024ULL * 1024 * 3 / 2), "1.5 MiB"); }

TEST(PackageSizeFormatter, ExactMultipleTrimsTrailingZeroDecimal) { EXPECT_EQ(formatSizeBytes(195035136), "186 MiB"); }

TEST(PackageSizeFormatter, GibibyteRange) { EXPECT_EQ(formatSizeBytes(1024ULL * 1024 * 1024 * 5 / 2), "2.5 GiB"); }

}  // namespace
}  // namespace holonight_packages_application
