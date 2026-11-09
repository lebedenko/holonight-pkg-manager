#include "hook_preview.h"

#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace holonight_packages_transaction {
namespace {

namespace fs = std::filesystem;
using holonight_packages_domain::HookPreview;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

class HookPreviewTest : public ::testing::Test {
 public:
  HookPreviewTest() {
    base_ =
        fs::temp_directory_path() / ("hook-preview-" + std::to_string(::getpid()) + "-" + std::to_string(counter_++));
    fs::create_directories(base_ / "system");
    fs::create_directories(base_ / "user");
  }
  ~HookPreviewTest() override { fs::remove_all(base_); }
  HookPreviewTest(const HookPreviewTest&) = delete;
  HookPreviewTest& operator=(const HookPreviewTest&) = delete;
  HookPreviewTest(HookPreviewTest&&) = delete;
  HookPreviewTest& operator=(HookPreviewTest&&) = delete;

  void hook(const std::string& dir, const std::string& name, const std::string& body) const {
    std::ofstream(base_ / dir / name) << body;
  }
  static std::string text(const std::string& when, const std::string& triggers) {
    return triggers + "[Action]\nWhen = " + when + "\nExec = /usr/bin/true\n";
  }
  static std::string packageTrigger(const std::string& operation, const std::string& target) {
    return "[Trigger]\nOperation = " + operation + "\nType = Package\nTarget = " + target + "\n";
  }
  [[nodiscard]] std::vector<fs::path> dirs() const { return {base_ / "system", base_ / "user"}; }

  fs::path base_;
  static inline int counter_ = 0;
};

TEST_F(HookPreviewTest, PackageTriggersMatchByOperationAndGlob) {
  hook("system", "10-install.hook", text("PostTransaction", packageTrigger("Install", "app*")));
  hook("system", "20-upgrade.hook", text("PostTransaction", packageTrigger("Upgrade", "app")));
  hook("system", "30-remove.hook", text("PostTransaction", packageTrigger("Remove", "app")));
  hook("system", "40-other.hook", text("PostTransaction", packageTrigger("Install", "zzz")));
  const std::vector<HookInstallFact> installs{HookInstallFact{.name = "app-extra", .upgrade = false}};
  const auto preview = previewHooks(dirs(), installs, {}, nullptr);
  ASSERT_EQ(preview.size(), 1U);
  EXPECT_EQ(preview[0], (HookPreview{.name = "10-install.hook", .conditional = false}));

  const std::vector<HookInstallFact> upgrades{HookInstallFact{.name = "app", .upgrade = true}};
  EXPECT_THAT(previewHooks(dirs(), upgrades, {}, nullptr),
              ElementsAre(HookPreview{.name = "20-upgrade.hook", .conditional = false}));
  const std::vector<HookRemovalFact> removals{HookRemovalFact{.name = "app", .files = {}}};
  EXPECT_THAT(previewHooks(dirs(), {}, removals, nullptr),
              ElementsAre(HookPreview{.name = "30-remove.hook", .conditional = false}));
}

TEST_F(HookPreviewTest, HooksRunSortedByNameIgnoringTheSuffixPreBeforePost) {
  hook("system", "b.hook", text("PostTransaction", packageTrigger("Install", "*")));
  hook("system", "a.hook", text("PostTransaction", packageTrigger("Install", "*")));
  hook("system", "z-first.hook", text("PreTransaction", packageTrigger("Install", "*")));
  const std::vector<HookInstallFact> installs{HookInstallFact{.name = "app", .upgrade = false}};
  const auto preview = previewHooks(dirs(), installs, {}, nullptr);
  ASSERT_EQ(preview.size(), 3U);
  EXPECT_EQ(preview[0].name, "z-first.hook");
  EXPECT_EQ(preview[1].name, "a.hook");
  EXPECT_EQ(preview[2].name, "b.hook");
}

TEST_F(HookPreviewTest, ALaterDirectoryOverridesTheSameFileName) {
  hook("system", "x.hook", text("PostTransaction", packageTrigger("Install", "nothing-matches")));
  hook("user", "x.hook", text("PostTransaction", packageTrigger("Install", "app")));
  const std::vector<HookInstallFact> installs{HookInstallFact{.name = "app", .upgrade = false}};
  EXPECT_THAT(previewHooks(dirs(), installs, {}, nullptr),
              ElementsAre(HookPreview{.name = "x.hook", .conditional = false}));
}

TEST_F(HookPreviewTest, InvertedTargetsAndLaterMatchesOverrideEarlierOnes) {
  hook("system", "x.hook", text("PostTransaction", packageTrigger("Install", "*") + "Target = !app\n"));
  const std::vector<HookInstallFact> app{HookInstallFact{.name = "app", .upgrade = false}};
  const std::vector<HookInstallFact> other{HookInstallFact{.name = "other", .upgrade = false}};
  EXPECT_THAT(previewHooks(dirs(), app, {}, nullptr), IsEmpty());
  EXPECT_EQ(previewHooks(dirs(), other, {}, nullptr).size(), 1U);
}

TEST_F(HookPreviewTest, PathTriggersAreEvaluatedForRemovalsAndConditionalForInstalls) {
  const std::string pathTrigger =
      "[Trigger]\nOperation = Install\nOperation = Upgrade\nOperation = Remove\nType = Path\nTarget = "
      "usr/lib/modules/*\n";
  hook("system", "depmod.hook", text("PostTransaction", pathTrigger));
  const std::vector<HookInstallFact> installs{HookInstallFact{.name = "kernel", .upgrade = false}};
  EXPECT_THAT(previewHooks(dirs(), installs, {}, nullptr),
              ElementsAre(HookPreview{.name = "depmod.hook", .conditional = true}));
  const std::vector<HookRemovalFact> hit{HookRemovalFact{.name = "kernel", .files = {"usr/lib/modules/6.1/vmlinuz"}}};
  EXPECT_THAT(previewHooks(dirs(), {}, hit, nullptr),
              ElementsAre(HookPreview{.name = "depmod.hook", .conditional = false}));
  const std::vector<HookRemovalFact> miss{HookRemovalFact{.name = "kernel", .files = {"usr/share/doc/readme"}}};
  EXPECT_THAT(previewHooks(dirs(), {}, miss, nullptr), IsEmpty());
}

TEST_F(HookPreviewTest, IncompleteHooksAreSkippedAndJournaledWithoutExecutingAnything) {
  hook("system", "broken.hook", "[Trigger]\nOperation = Install\nType = Package\nTarget = *\n");  // no [Action]
  hook("system", "ignored.txt", text("PostTransaction", packageTrigger("Install", "*")));
  std::vector<std::string> journal;
  const std::vector<HookInstallFact> installs{HookInstallFact{.name = "app", .upgrade = false}};
  EXPECT_THAT(
      previewHooks(dirs(), installs, {}, [&journal](LogLevel, const std::string& line) { journal.push_back(line); }),
      IsEmpty());
  EXPECT_THAT(journal, ElementsAre(::testing::HasSubstr("broken.hook")));
}

TEST_F(HookPreviewTest, MissingDirectoriesYieldNoHooks) {
  const std::vector<fs::path> missing{base_ / "absent"};
  const std::vector<HookInstallFact> installs{HookInstallFact{.name = "app", .upgrade = false}};
  EXPECT_THAT(previewHooks(missing, installs, {}, nullptr), IsEmpty());
}

}  // namespace
}  // namespace holonight_packages_transaction
