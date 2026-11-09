#include "holonight_packages_domain/transaction_request.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace holonight_packages_domain {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(PackageNameTest, AcceptsTheArchGrammar) {
  for (const char* name : {"lib32-foo", "g++", "python3.12", "a@b", "gtk3", "x_y", "a", "0ad"}) {
    EXPECT_TRUE(isValidPackageName(name)) << name;
  }
}

TEST(PackageNameTest, RejectsWhatTheGrammarForbids) {
  for (const char* name : {"-foo", ".foo", "Foo", "foo bar", "foo!", "", "fo\\o", "föö"}) {
    EXPECT_FALSE(isValidPackageName(name)) << name;
  }
  EXPECT_FALSE(isValidPackageName(std::string(256, 'a')));
  EXPECT_TRUE(isValidPackageName(std::string(255, 'a')));
}

TEST(PackageNameTest, NamesCannotBeReadAsFilesOrUrls) {
  for (const char* name : {
           "/tmp/x.pkg.tar.zst",
           "https://example.org/a",
           "file:///x",
           "../foo",
           "foo/bar",
           "foo.pkg.tar.zst",
           "foo.pkg.tar",
       }) {
    EXPECT_FALSE(isValidPackageName(name)) << name;
  }
}

TEST(OffendingValueTest, IsTruncatedAndStripsControlCharacters) {
  EXPECT_EQ(sanitizeOffendingValue("a\nb\tc\x01"), "a?b?c?");
  EXPECT_EQ(sanitizeOffendingValue(std::string(1000, 'x')).size(), 256U);
  EXPECT_EQ(sanitizeOffendingValue("plain"), "plain");
}

TEST(ValidateRequestTest, BuildsARequestFromValidInput) {
  const auto request = validateRequest("install", {"app", "lib"}, {});
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->action, PlanAction::Install);
  EXPECT_THAT(request->names, ElementsAre("app", "lib"));
  const auto removal = validateRequest("remove", {"app"}, {});
  ASSERT_TRUE(removal.has_value());
  EXPECT_EQ(removal->action, PlanAction::Remove);
}

TEST(ValidateRequestTest, UnknownActionsAreInvalid) {
  const auto upgrade = validateRequest("upgrade", {"app"}, {});
  ASSERT_FALSE(upgrade.has_value());
  EXPECT_EQ(upgrade.error().code, TransactionErrorCode::InvalidArgument);
  EXPECT_THAT(upgrade.error().names, ElementsAre("upgrade"));
  EXPECT_FALSE(validateRequest("", {"app"}, {}).has_value());
  EXPECT_FALSE(validateRequest("Install", {"app"}, {}).has_value());
}

TEST(ValidateRequestTest, AnyOptionKeyIsInvalid) {
  for (const char* key : {"noconfirm", "SigLevel", "force"}) {
    const auto request = validateRequest("install", {"app"}, {key});
    ASSERT_FALSE(request.has_value()) << key;
    EXPECT_EQ(request.error().code, TransactionErrorCode::InvalidArgument);
    EXPECT_THAT(request.error().names, ElementsAre(key));
  }
}

TEST(ValidateRequestTest, ZeroTooManyOrDuplicateNamesAreInvalid) {
  EXPECT_FALSE(validateRequest("install", {}, {}).has_value());
  EXPECT_THAT(validateRequest("install", {}, {}).error().names, IsEmpty());
  EXPECT_FALSE(validateRequest("install", std::vector<std::string>(65, "a"), {}).has_value());
  std::vector<std::string> sixtyFour;
  sixtyFour.reserve(64);
  for (int i = 0; i < 64; ++i) {
    sixtyFour.push_back("pkg" + std::to_string(i));
  }
  EXPECT_TRUE(validateRequest("install", sixtyFour, {}).has_value());
  const auto duplicate = validateRequest("install", {"app", "lib", "app"}, {});
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_THAT(duplicate.error().names, ElementsAre("app"));
}

TEST(ValidateRequestTest, ReportsEveryOffendingName) {
  const auto request =
      validateRequest("install", {"ok", "/tmp/x.pkg.tar.zst", "https://example.org/a", "file:///x", "../foo"}, {});
  ASSERT_FALSE(request.has_value());
  EXPECT_EQ(request.error().code, TransactionErrorCode::InvalidArgument);
  EXPECT_THAT(request.error().names, ElementsAre("/tmp/x.pkg.tar.zst", "https://example.org/a", "file:///x", "../foo"));
}

TEST(ValidateRequestTest, OffendingValuesAreSanitized) {
  const auto request = validateRequest("install", {"bad\nname"}, {});
  ASSERT_FALSE(request.has_value());
  EXPECT_THAT(request.error().names, ElementsAre("bad?name"));
}

}  // namespace
}  // namespace holonight_packages_domain
