#include "fixture_root.h"
#include "gpg_keyring.h"
#include "gpgme_guard.h"
#include "holonight_packages_transaction/alpm_transaction_engine.h"
#include "manifest.h"
#include "repo_builder.h"

#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace holonight_packages_transaction {
namespace {

namespace fs = std::filesystem;
using holonight_packages_domain::ExecutionContext;
using holonight_packages_domain::ExecutionOutcome;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlanRequest;
using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::WarningToken;
using holonight_transaction_testing::FixtureRoot;
using holonight_transaction_testing::GpgKeyring;
using holonight_transaction_testing::PackageSpec;
using holonight_transaction_testing::RepoBuilder;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

PackageSpec pkg(std::string name, std::string version = "1.0-1") {
  PackageSpec spec;
  spec.files = {{"usr/share/" + name + "/file", name}};
  spec.name = std::move(name);
  spec.version = std::move(version);
  return spec;
}

// Fixture worlds whose hooks really run: the static marker tool is copied into the root, and the whole test runs inside
// a user namespace (tests/transaction/userns_main.cpp).
class UsernsHookTest : public ::testing::Test {
 public:
  UsernsHookTest() : core_(world_, "core") {
    fs::create_directories(world_.installRoot() / "usr/bin");
    fs::copy_file(HOLONIGHT_TEST_MARKER_TOOL, world_.installRoot() / "usr/bin/marker");
    fs::permissions(world_.installRoot() / "usr/bin/marker", fs::perms::owner_all);
  }

  void hook(const std::string& name, const std::string& when, const std::string& operation, const std::string& target,
            const std::string& command, bool abortOnFail = false) const {
    world_.writeHook(name, "[Trigger]\nOperation = " + operation + "\nType = Package\nTarget = " + target +
                               "\n[Action]\nWhen = " + when + "\nExec = " + command + "\n" +
                               (abortOnFail ? "AbortOnFail\n" : ""));
  }

  AlpmTransactionEngine engine() {
    return {world_.config(), [this](LogLevel, const std::string& line) { journal_.push_back(line); }};
  }
  struct Approved {
    PlanRequest request;
    holonight_packages_domain::ResolvedPlan plan;
  };
  Approved approve(PlanAction action, const std::string& name) {
    PlanRequest request{.action = action, .names = {name}};
    auto plan = engine().resolve(request);
    EXPECT_TRUE(plan.has_value());
    return Approved{.request = request, .plan = plan ? *plan : holonight_packages_domain::ResolvedPlan{}};
  }
  ExecutionResult run(const Approved& approved, std::vector<ProgressPhase>* phases = nullptr) {
    ExecutionContext context;
    if (phases != nullptr) {
      context.progress = [phases](const holonight_packages_domain::ProgressUpdate& update) {
        if (phases->empty() || phases->back() != update.phase) {
          phases->push_back(update.phase);
        }
      };
    }
    return engine().execute(approved.request, approved.plan, context);
  }

  FixtureRoot world_;
  RepoBuilder core_;
  std::vector<std::string> journal_;
};

TEST_F(UsernsHookTest, ThisRunIsInsideAUserNamespaceAndNotRealRoot) {
  std::ifstream map("/proc/self/uid_map");
  unsigned long inside = 99;
  unsigned long outside = 0;
  map >> inside >> outside;
  EXPECT_EQ(inside, 0UL);
  EXPECT_NE(outside, 0UL);  // the host user behind uid 0 is an ordinary one (REQ-C-010)
}

TEST_F(UsernsHookTest, APostTransactionHookRunsForARemovalAndLeavesItsMarker) {
  core_.add(pkg("unused"));
  core_.publish();
  world_.writeConf({"core"}, "Never");
  world_.installPackage(pkg("app"));
  hook("20-post.hook", "PostTransaction", "Remove", "app", "/usr/bin/marker /marker-post");
  const auto approved = approve(PlanAction::Remove, "app");
  ASSERT_THAT(approved.plan.hooks.size(), 1U);
  std::vector<ProgressPhase> phases;
  const ExecutionResult result = run(approved, &phases);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded);
  EXPECT_TRUE(world_.fileExists("marker-post"));
  EXPECT_EQ(world_.readFile("marker-post"), "ran\n");
  EXPECT_THAT(result.warnings, IsEmpty());
  EXPECT_THAT(phases, Contains(ProgressPhase::Hooks));
  EXPECT_EQ(phases.back(), ProgressPhase::Done);
}

TEST_F(UsernsHookTest, AFailingPostTransactionHookIsAWarningAndTheChangedListIsComplete) {
  core_.add(pkg("unused"));
  core_.publish();
  world_.writeConf({"core"}, "Never");
  world_.installPackage(pkg("app"));
  hook("20-post.hook", "PostTransaction", "Remove", "app", "/usr/bin/marker /marker-post fail");
  const ExecutionResult result = run(approve(PlanAction::Remove, "app"));
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded);
  EXPECT_THAT(result.warnings, ElementsAre(WarningToken::HookFailed));
  ASSERT_EQ(result.changed.size(), 1U);
  EXPECT_EQ(result.changed[0].name, "app");
  EXPECT_TRUE(world_.fileExists("marker-post"));  // it did run, and failed
  EXPECT_FALSE(world_.isInstalled("app"));
  EXPECT_THAT(journal_, Contains(HasSubstr("command failed to execute correctly")));
}

TEST_F(UsernsHookTest, AnAbortingPreTransactionHookFailsBeforeAnyChangeAndLeavesTheManifestUnchanged) {
  core_.add(pkg("unused"));
  core_.publish();
  world_.writeConf({"core"}, "Never");
  world_.installPackage(pkg("app"));
  hook("10-pre.hook", "PreTransaction", "Remove", "app", "/usr/bin/marker /marker-pre fail", true);
  const auto approved = approve(PlanAction::Remove, "app");
  const auto before = holonight_packages_testing::manifestOf(world_.localDbPath());
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::TransactionFailed);
  EXPECT_TRUE(result.changed.empty());
  EXPECT_TRUE(world_.fileExists("marker-pre"));
  EXPECT_TRUE(world_.isInstalled("app"));
  EXPECT_TRUE(world_.fileExists("usr/share/app/file"));
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.localDbPath()), before);
}

TEST_F(UsernsHookTest, AnInstallRunsItsPreAndPostHooksAroundTheInstalling) {
  SKIP_UNLESS_FRESH_GPGME();
  GpgKeyring keyring(world_.base() / "signing");
  keyring.trustIn(world_.gpgDir());
  core_.setSigner(keyring.signer());
  core_.add(pkg("app"));
  core_.publish();
  world_.writeConf({"core"}, "Required DatabaseOptional");
  hook("10-pre.hook", "PreTransaction", "Install", "app", "/usr/bin/marker /marker-pre");
  hook("20-post.hook", "PostTransaction", "Install", "app", "/usr/bin/marker /marker-post");
  std::vector<ProgressPhase> phases;
  const ExecutionResult result = run(approve(PlanAction::Install, "app"), &phases);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded)
      << holonight_packages_domain::transactionErrorName(result.failure.code);
  EXPECT_TRUE(world_.fileExists("marker-pre"));
  EXPECT_TRUE(world_.fileExists("marker-post"));
  EXPECT_THAT(phases,
              ElementsAre(ProgressPhase::Resolving, ProgressPhase::Downloading, ProgressPhase::Verifying,
                          ProgressPhase::Hooks, ProgressPhase::Installing, ProgressPhase::Hooks, ProgressPhase::Done));
}

}  // namespace
}  // namespace holonight_packages_transaction
