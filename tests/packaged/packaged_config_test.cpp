#include "PackagedConfig.h"

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>

namespace {

namespace fs = std::filesystem;
using std::chrono::minutes;

class PackagedConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    root_ = fs::path(dir_.path().toStdString());
  }
  [[nodiscard]] fs::path write(const std::string& contents) const {
    const fs::path path = root_ / "packages.toml";
    std::ofstream(path) << contents;
    return path;
  }
  QTemporaryDir dir_;
  fs::path root_;
  holonight_packages_application::UpdateCheckPolicy policy_;
};

TEST_F(PackagedConfigTest, NoFileGivesSixHoursAndNoWarning) {
  const auto settings = packaged_config::load(root_ / "absent.toml", std::nullopt, policy_);
  EXPECT_EQ(settings.checkInterval, minutes{360});
  EXPECT_TRUE(settings.warnings.empty());
  EXPECT_EQ(packaged_config::load(std::nullopt, std::nullopt, policy_).checkInterval, minutes{360});
}

TEST_F(PackagedConfigTest, FileWithoutTheKeyGivesSixHoursAndNoWarning) {
  const auto settings = packaged_config::load(write("[updates]\nother = 1\n"), std::nullopt, policy_);
  EXPECT_EQ(settings.checkInterval, minutes{360});
  EXPECT_TRUE(settings.warnings.empty());
}

TEST_F(PackagedConfigTest, FileValueIsUsed) {
  const auto settings =
      packaged_config::load(write("[updates]\ncheck_interval_minutes = 120\n"), std::nullopt, policy_);
  EXPECT_EQ(settings.checkInterval, minutes{120});
  EXPECT_TRUE(settings.warnings.empty());
}

TEST_F(PackagedConfigTest, CommandLineOptionOverridesTheFile) {
  const auto settings =
      packaged_config::load(write("[updates]\ncheck_interval_minutes = 120\n"), std::string("30"), policy_);
  EXPECT_EQ(settings.checkInterval, minutes{30});
  EXPECT_TRUE(settings.warnings.empty());
}

TEST_F(PackagedConfigTest, UnusableValuesWarnExactlyOnceAndUseTheDefault) {
  for (const char* bad : {"\"abc\"", "0", "-3", "true", "1.5"}) {
    const auto settings = packaged_config::load(write(std::string("[updates]\ncheck_interval_minutes = ") + bad + "\n"),
                                                std::nullopt, policy_);
    EXPECT_EQ(settings.checkInterval, minutes{360}) << bad;
    EXPECT_EQ(settings.warnings.size(), 1U) << bad;
  }
  const auto clamped = packaged_config::load(write("[updates]\ncheck_interval_minutes = 5\n"), std::nullopt, policy_);
  EXPECT_EQ(clamped.checkInterval, minutes{15});
  EXPECT_EQ(clamped.warnings.size(), 1U);
}

TEST_F(PackagedConfigTest, InvalidTomlWarnsAndUsesTheDefault) {
  const auto settings = packaged_config::load(write("[updates\nnot toml"), std::nullopt, policy_);
  EXPECT_EQ(settings.checkInterval, minutes{360});
  EXPECT_EQ(settings.warnings.size(), 1U);
}

TEST_F(PackagedConfigTest, DefaultPathFollowsXdgThenHomeAndTheOverride) {
  const auto env = [](std::map<std::string, std::string> values) {
    return [values = std::move(values)](std::string_view name) -> std::optional<std::string> {
      const auto found = values.find(std::string(name));
      return found == values.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
  };
  EXPECT_EQ(packaged_config::configPath(env({{"XDG_CONFIG_HOME", "/x"}, {"HOME", "/h"}})),
            fs::path("/x/holonight/packages.toml"));
  EXPECT_EQ(packaged_config::configPath(env({{"HOME", "/h"}})), fs::path("/h/.config/holonight/packages.toml"));
  EXPECT_EQ(packaged_config::configPath(env({{"XDG_CONFIG_HOME", ""}, {"HOME", "/h"}})),
            fs::path("/h/.config/holonight/packages.toml"));
  EXPECT_EQ(packaged_config::configPath(env({{"HOLONIGHT_PACKAGES_FILE", "/custom.toml"}, {"HOME", "/h"}})),
            fs::path("/custom.toml"));
  EXPECT_FALSE(packaged_config::configPath(env({})).has_value());
}

}  // namespace
