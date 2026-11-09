#include "fixture_root.h"
#include "holonight_packages_transaction/alpm_transaction_engine.h"
#include "manifest.h"
#include "repo_builder.h"

#include <chrono>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::ChangeKind;
using holonight_packages_domain::NeedsDecisionChoice;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlanRequest;
using holonight_packages_domain::TransactionErrorCode;
using holonight_transaction_testing::FixtureRoot;
using holonight_transaction_testing::InstallReason;
using holonight_transaction_testing::PackageSpec;
using holonight_transaction_testing::RepoBuilder;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

PackageSpec pkg(std::string name, std::string version = "1.0-1", std::vector<std::string> depends = {}) {
  PackageSpec spec;
  spec.files = {{"usr/share/" + name + "/file", name}};
  spec.name = std::move(name);
  spec.version = std::move(version);
  spec.depends = std::move(depends);
  return spec;
}

// A world with one "core" repository and the usual Required + DatabaseOptional configuration.
class ResolverTest : public ::testing::Test {
 public:
  ResolverTest() : core_(world_, "core"), signatureLevel_("Required DatabaseOptional") {}

  void finish() const {
    core_.publish();
    world_.writeConf({"core"}, signatureLevel_);
  }

  AlpmTransactionEngine engine() {
    return {world_.config(), [this](LogLevel, const std::string& line) { journal_.push_back(line); }};
  }

  auto install(std::vector<std::string> names) {
    return engine().resolve(PlanRequest{.action = PlanAction::Install, .names = std::move(names)});
  }
  auto remove(std::vector<std::string> names) {
    return engine().resolve(PlanRequest{.action = PlanAction::Remove, .names = std::move(names)});
  }

  static std::vector<std::string> namesOf(const holonight_packages_domain::ResolvedPlan& plan, ChangeKind kind) {
    std::vector<std::string> names;
    for (const auto& change : plan.changes) {
      if (change.kind == kind) {
        names.push_back(change.name);
      }
    }
    return names;
  }

  FixtureRoot world_;
  RepoBuilder core_;
  std::string signatureLevel_;
  std::vector<std::string> journal_;
};

// ---- T-018: configuration and handle ----

TEST_F(ResolverTest, AnUnreadableConfigurationIsAConfigurationErrorForEveryAction) {
  core_.add(pkg("app"));
  core_.publish();  // pacman.conf is never written
  EXPECT_EQ(install({"app"}).error().code, TransactionErrorCode::ConfigurationError);
  EXPECT_EQ(remove({"app"}).error().code, TransactionErrorCode::ConfigurationError);
}

TEST_F(ResolverTest, DatabasePathAndRootFromPacmanConfAreIgnored) {
  core_.add(pkg("app"));
  finish();
  std::ifstream input(world_.confPath());
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  text.replace(text.find("[options]\n") + 10, 0, "DBPath = /nonexistent/db/\nRootDir = /nonexistent/root/\n");
  world_.writeConfText(text);
  const auto plan = install({"app"});
  ASSERT_TRUE(plan.has_value()) << transactionErrorName(plan.error().code);
  EXPECT_THAT(namesOf(*plan, ChangeKind::Target), ElementsAre("app"));
}

// ---- T-019: install happy path ----

TEST_F(ResolverTest, InstallPreviewsTargetsAndDependenciesWithRepositories) {
  core_.add(pkg("app", "1.0-1", {"lib"})).add(pkg("lib", "2.0-1")).add(pkg("unrelated"));
  finish();
  const auto plan = install({"app"});
  ASSERT_TRUE(plan.has_value()) << transactionErrorName(plan.error().code);
  EXPECT_EQ(plan->action, PlanAction::Install);
  ASSERT_EQ(plan->changes.size(), 2U);
  EXPECT_EQ(plan->changes[0].name, "app");
  EXPECT_EQ(plan->changes[0].kind, ChangeKind::Target);
  EXPECT_EQ(plan->changes[0].repository, "core");
  EXPECT_EQ(plan->changes[0].new_version, "1.0-1");
  EXPECT_TRUE(plan->changes[0].old_version.empty());
  EXPECT_GT(plan->changes[0].download_bytes, 0U);
  EXPECT_EQ(plan->changes[1].name, "lib");
  EXPECT_EQ(plan->changes[1].kind, ChangeKind::Dependency);
  EXPECT_EQ(plan->changes[1].new_version, "2.0-1");
}

TEST_F(ResolverTest, PreviewNeverChangesTheDatabasesTheRootOrTheCache) {
  core_.add(pkg("app", "1.0-1", {"lib"})).add(pkg("lib"));
  finish();
  world_.installPackage(pkg("present"));
  const auto before = holonight_packages_testing::manifestOf(world_.base() / "db");
  const auto rootBefore = holonight_packages_testing::manifestOf(world_.installRoot());
  const auto cacheBefore = holonight_packages_testing::manifestOf(world_.cacheDir());
  ASSERT_TRUE(install({"app"}).has_value());
  ASSERT_TRUE(remove({"present"}).has_value());
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.base() / "db"), before);
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.installRoot()), rootBefore);
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.cacheDir()), cacheBefore);
  EXPECT_FALSE(std::filesystem::exists(world_.lockPath()));
}

TEST_F(ResolverTest, InstallingACurrentPackageIsNothingToDoButAMixedRequestOmitsIt) {
  core_.add(pkg("current")).add(pkg("fresh"));
  finish();
  world_.installPackage(pkg("current"));
  EXPECT_EQ(install({"current"}).error().code, TransactionErrorCode::NothingToDo);
  const auto plan = install({"current", "fresh"});
  ASSERT_TRUE(plan.has_value());
  EXPECT_THAT(namesOf(*plan, ChangeKind::Target), ElementsAre("fresh"));
  EXPECT_EQ(plan->changes.size(), 1U);
}

TEST_F(ResolverTest, TheHooksThatWillRunAppearByNameInThePreview) {
  core_.add(pkg("app"));
  finish();
  world_.writeHook("10-notify.hook",
                   "[Trigger]\nOperation = Install\nType = Package\nTarget = app\n[Action]\nWhen = "
                   "PostTransaction\nExec = /usr/bin/true\n");
  world_.writeHook("20-unrelated.hook",
                   "[Trigger]\nOperation = Install\nType = Package\nTarget = other\n[Action]\nWhen = "
                   "PostTransaction\nExec = /usr/bin/true\n");
  const auto plan = install({"app"});
  ASSERT_TRUE(plan.has_value());
  ASSERT_EQ(plan->hooks.size(), 1U);
  EXPECT_EQ(plan->hooks[0].name, "10-notify.hook");
  EXPECT_FALSE(plan->hooks[0].conditional);
}

TEST_F(ResolverTest, FiftyPackageClosureInAFiveHundredPackageRepositoryResolvesWithinTenSeconds) {
  std::vector<std::string> chain;
  for (int i = 0; i < 500; ++i) {
    const std::string name = "p" + std::to_string(i);
    std::vector<std::string> depends;
    if (i < 49) {
      depends = {"p" + std::to_string(i + 1)};
    }
    core_.add(pkg(name, "1.0-1", depends));
  }
  finish();
  const auto started = std::chrono::steady_clock::now();
  const auto plan = install({"p0"});
  ASSERT_TRUE(plan.has_value());
  EXPECT_EQ(plan->changes.size(), 50U);
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(10));
}

TEST_F(ResolverTest, AMissingOrCorruptSyncDatabaseIsDatabaseUnavailableNotPackageNotFound) {
  core_.add(pkg("app"));
  finish();
  std::filesystem::remove(world_.syncDbPath() / "core.db");
  EXPECT_EQ(install({"app"}).error().code, TransactionErrorCode::DatabaseUnavailable);
  std::ofstream(world_.syncDbPath() / "core.db") << "this is not an archive";
  EXPECT_EQ(install({"app"}).error().code, TransactionErrorCode::DatabaseUnavailable);
}

// ---- T-020: stage-one refusals ----

TEST_F(ResolverTest, UnknownTargetsAreReportedByNameAndGroupsOrProvidesAreNotAccepted) {
  PackageSpec grouped = pkg("grouped");
  grouped.groups = {"tools"};
  PackageSpec provider = pkg("vi");
  provider.provides = {"editor"};
  core_.add(grouped).add(provider);
  finish();
  const auto missing = install({"no-such-pkg", "grouped"});
  EXPECT_EQ(missing.error().code, TransactionErrorCode::PackageNotFound);
  EXPECT_THAT(missing.error().names, ElementsAre("no-such-pkg"));
  EXPECT_EQ(install({"tools"}).error().code, TransactionErrorCode::PackageNotFound);
  EXPECT_EQ(install({"editor"}).error().code, TransactionErrorCode::PackageNotFound);
}

TEST_F(ResolverTest, IgnoredPackagesAndIgnoredGroupMembersAreRefusedForBothActions) {
  PackageSpec grouped = pkg("member");
  grouped.groups = {"fruits"};
  core_.add(pkg("foo")).add(grouped);
  finish();
  world_.installPackage(pkg("foo"));
  world_.installPackage(grouped);
  std::ifstream input(world_.confPath());
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  text.replace(text.find("[options]\n") + 10, 0, "IgnorePkg = foo\nIgnoreGroup = fruits\n");
  world_.writeConfText(text);
  EXPECT_EQ(install({"foo"}).error().code, TransactionErrorCode::PackageIgnored);
  EXPECT_THAT(install({"foo"}).error().names, ElementsAre("foo"));
  EXPECT_EQ(remove({"foo"}).error().code, TransactionErrorCode::PackageIgnored);
  const auto group = remove({"member"});
  EXPECT_EQ(group.error().code, TransactionErrorCode::PackageIgnored);
  EXPECT_THAT(group.error().names, ElementsAre("member"));
}

// ---- T-021: signature policy ----

TEST_F(ResolverTest, WeakOrMissingSignatureLevelsRefuseEveryInstall) {
  core_.add(pkg("app"));
  core_.publish();
  for (const char* level : {"Never", "Optional", "Required TrustAll DatabaseOptional", "Optional DatabaseOptional"}) {
    world_.writeConf({"core"}, level);
    const auto plan = install({"app"});
    ASSERT_FALSE(plan.has_value()) << level;
    EXPECT_EQ(plan.error().code, TransactionErrorCode::InsecureSigLevel) << level;
    EXPECT_THAT(plan.error().names, ElementsAre("core")) << level;
  }
  holonight_transaction_testing::ConfSpec none;
  none.globalSigLevel = std::nullopt;
  none.repos = {{.name = "core", .servers = {world_.serverUrl("core")}, .sigLevel = std::nullopt}};
  world_.writeConf(none);
  EXPECT_EQ(install({"app"}).error().code, TransactionErrorCode::InsecureSigLevel);
}

TEST_F(ResolverTest, RequiredTrustedOnlyIsAcceptedAndOneWeakRepositoryBlocksTargetsFromAStrongOne) {
  core_.add(pkg("app"));
  finish();
  EXPECT_TRUE(install({"app"}).has_value());

  RepoBuilder extra(world_, "extra");
  extra.add(pkg("other"));
  extra.publish();
  holonight_transaction_testing::ConfSpec mixed;
  mixed.globalSigLevel = "Required DatabaseOptional";
  mixed.repos = {
      {.name = "core", .servers = {world_.serverUrl("core")}, .sigLevel = std::nullopt},
      {.name = "extra", .servers = {world_.serverUrl("extra")}, .sigLevel = "Optional DatabaseOptional"},
  };
  world_.writeConf(mixed);
  const auto plan = install({"app"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::InsecureSigLevel);
  EXPECT_THAT(plan.error().names, ElementsAre("extra"));
}

TEST_F(ResolverTest, RefusingAWeakLevelNeverRewritesPacmanConf) {
  core_.add(pkg("app"));
  core_.publish();
  world_.writeConf({"core"}, "Never");
  const auto before = holonight_packages_testing::manifestOf(world_.base());
  EXPECT_EQ(install({"app"}).error().code, TransactionErrorCode::InsecureSigLevel);
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.base()).at("pacman.conf"), before.at("pacman.conf"));
}

TEST_F(ResolverTest, RemoveIgnoresTheSignatureLevel) {
  core_.add(pkg("app"));
  core_.publish();
  world_.writeConf({"core"}, "Never");
  world_.installPackage(pkg("app"));
  EXPECT_TRUE(remove({"app"}).has_value());
}

// ---- T-022: decisions ----

TEST_F(ResolverTest, TwoProvidersOfADependencyNeedADecisionListingBoth) {
  PackageSpec vi_package = pkg("vi");
  vi_package.provides = {"editor"};
  PackageSpec nano = pkg("nano");
  nano.provides = {"editor"};
  core_.add(pkg("tool", "1.0-1", {"editor"})).add(vi_package).add(nano);
  finish();
  const auto plan = install({"tool"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::NeedsDecision);
  ASSERT_EQ(plan.error().choices.size(), 1U);
  EXPECT_EQ(plan.error().choices[0].kind, NeedsDecisionChoice::Kind::Provider);
  EXPECT_EQ(plan.error().choices[0].subject, "editor");
  EXPECT_THAT(plan.error().choices[0].candidates, UnorderedElementsAre("vi", "nano"));
}

TEST_F(ResolverTest, AConflictWithAnInstalledPackageNeedsADecision) {
  PackageSpec replacement = pkg("newpkg");
  replacement.conflicts = {"oldpkg"};
  core_.add(replacement);
  finish();
  world_.installPackage(pkg("oldpkg"));
  const auto plan = install({"newpkg"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::NeedsDecision);
  ASSERT_EQ(plan.error().choices.size(), 1U);
  EXPECT_EQ(plan.error().choices[0].kind, NeedsDecisionChoice::Kind::Conflict);
  EXPECT_THAT(plan.error().choices[0].candidates, UnorderedElementsAre("newpkg", "oldpkg"));
}

TEST_F(ResolverTest, ASyncPackageThatReplacesAnInstalledOneNeedsADecision) {
  PackageSpec replacement = pkg("newname");
  replacement.replaces = {"oldname"};
  core_.add(replacement);
  finish();
  world_.installPackage(pkg("oldname"));
  const auto plan = install({"newname"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::NeedsDecision);
  ASSERT_EQ(plan.error().choices.size(), 1U);
  EXPECT_EQ(plan.error().choices[0].kind, NeedsDecisionChoice::Kind::Replace);
  EXPECT_THAT(plan.error().choices[0].candidates, UnorderedElementsAre("oldname", "newname"));
}

TEST_F(ResolverTest, DecisionQuestionsAreJournaledByTypeOnly) {
  PackageSpec vi_package = pkg("vi");
  vi_package.provides = {"editor"};
  PackageSpec nano = pkg("nano");
  nano.provides = {"editor"};
  core_.add(pkg("tool", "1.0-1", {"editor"})).add(vi_package).add(nano);
  finish();
  ASSERT_FALSE(install({"tool"}).has_value());
  EXPECT_THAT(journal_, ::testing::Contains(::testing::HasSubstr("select-provider")));
}

// ---- T-023: dependencies and partial upgrades ----

TEST_F(ResolverTest, AMissingDependencyIsNamed) {
  core_.add(pkg("app", "1.0-1", {"missing-lib"}));
  finish();
  const auto plan = install({"app"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::PackageNotFound);
  EXPECT_THAT(plan.error().names, ElementsAre("missing-lib"));
}

TEST_F(ResolverTest, AnInstalledPackageWithANewerSyncVersionIsAPartialUpgrade) {
  core_.add(pkg("foo", "2.0-1"));
  finish();
  world_.installPackage(pkg("foo", "1.0-1"));
  const auto plan = install({"foo"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::PartialUpgradeRequired);
  EXPECT_THAT(plan.error().names, ElementsAre("foo"));
}

TEST_F(ResolverTest, ADependencyThatNeedsANewerInstalledVersionIsAPartialUpgrade) {
  core_.add(pkg("app", "1.0-1", {"lib>=2"})).add(pkg("lib", "2.0-1"));
  finish();
  world_.installPackage(pkg("lib", "1.0-1"));
  const auto plan = install({"app"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::PartialUpgradeRequired);
  EXPECT_THAT(plan.error().names, ElementsAre("lib"));
}

TEST_F(ResolverTest, ADependencySatisfiedByAnInstalledProviderIsNotAnUpgrade) {
  PackageSpec vi_package = pkg("vi-impl");
  vi_package.provides = {"editor"};
  core_.add(pkg("tool", "1.0-1", {"editor"}));
  finish();
  world_.installPackage(vi_package);
  const auto plan = install({"tool"});
  ASSERT_TRUE(plan.has_value()) << transactionErrorName(plan.error().code);
  EXPECT_THAT(namesOf(*plan, ChangeKind::Target), ElementsAre("tool"));
  EXPECT_THAT(namesOf(*plan, ChangeKind::Dependency), IsEmpty());
}

TEST_F(ResolverTest, ANamedPackageAtAnOlderVersionIsAPartialUpgradeEvenWhenAProviderExists) {
  core_.add(pkg("tool", "1.0-1", {"editor>=2"})).add(pkg("editor", "2.0-1"));
  finish();
  world_.installPackage(pkg("editor", "1.0-1"));
  const auto plan = install({"tool"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::PartialUpgradeRequired);
  EXPECT_THAT(plan.error().names, ElementsAre("editor"));
}

// ---- T-024 and T-025: protected set and check order ----

TEST_F(ResolverTest, UpgradingAnInstalledMemberOfTheBaseClosureIsRefused) {
  core_.add(pkg("core-a", "2.0-1"));
  finish();
  world_.installPackage(pkg("base", "1-1", {"core-a"}));
  world_.installPackage(pkg("core-a", "1.0-1"));
  const auto plan = install({"core-a"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::ProtectedPackage);
  EXPECT_THAT(plan.error().names, ElementsAre("core-a"));
}

TEST_F(ResolverTest, HoldPkgNamesAreProtectedWithoutBeingGlobbed) {
  core_.add(pkg("foo", "2.0-1"));
  finish();
  world_.installPackage(pkg("foo", "1.0-1"));
  std::ifstream input(world_.confPath());
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  text.replace(text.find("[options]\n") + 10, 0, "HoldPkg = foo\n");
  world_.writeConfText(text);
  EXPECT_EQ(install({"foo"}).error().code, TransactionErrorCode::ProtectedPackage);
}

TEST_F(ResolverTest, ProtectedPackageOutranksPartialUpgradeWhenBothApply) {
  core_.add(pkg("tool", "1.0-1", {"held>=2"})).add(pkg("held", "2.0-1"));
  finish();
  world_.installPackage(pkg("held", "1.0-1"));
  std::ifstream input(world_.confPath());
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  text.replace(text.find("[options]\n") + 10, 0, "HoldPkg = held\n");
  world_.writeConfText(text);
  const auto plan = install({"tool"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::ProtectedPackage);
  EXPECT_THAT(plan.error().names, ElementsAre("held"));
}

TEST_F(ResolverTest, AnIgnoredTargetIsReportedBeforeAProviderDecision) {
  PackageSpec vi_package = pkg("vi");
  vi_package.provides = {"editor"};
  PackageSpec nano = pkg("nano");
  nano.provides = {"editor"};
  core_.add(pkg("tool", "1.0-1", {"editor"})).add(vi_package).add(nano);
  finish();
  std::ifstream input(world_.confPath());
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  text.replace(text.find("[options]\n") + 10, 0, "IgnorePkg = tool\n");
  world_.writeConfText(text);
  EXPECT_EQ(install({"tool"}).error().code, TransactionErrorCode::PackageIgnored);
}

TEST_F(ResolverTest, ACyclicBaseClosureTerminates) {
  core_.add(pkg("app"));
  finish();
  world_.installPackage(pkg("base", "1-1", {"a"}));
  world_.installPackage(pkg("a", "1-1", {"b"}));
  world_.installPackage(pkg("b", "1-1", {"c"}));
  world_.installPackage(pkg("c", "1-1", {"a"}));
  const auto started = std::chrono::steady_clock::now();
  EXPECT_TRUE(install({"app"}).has_value());
  EXPECT_EQ(remove({"a"}).error().code, TransactionErrorCode::ProtectedPackage);
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
}

// ---- T-026 and T-027: removal ----

TEST_F(ResolverTest, RemovalListsTargetsAndOrphansSeparately) {
  core_.add(pkg("unused"));
  finish();
  world_.installPackage(pkg("app", "1.0-1", {"lib"}));
  world_.installPackage(pkg("lib", "1.0-1"), InstallReason::Dependency);
  const auto plan = remove({"app"});
  ASSERT_TRUE(plan.has_value()) << transactionErrorName(plan.error().code);
  EXPECT_EQ(plan->action, PlanAction::Remove);
  EXPECT_THAT(namesOf(*plan, ChangeKind::Target), ElementsAre("app"));
  EXPECT_THAT(namesOf(*plan, ChangeKind::Orphan), ElementsAre("lib"));
  for (const auto& change : plan->changes) {
    EXPECT_TRUE(change.repository.empty());
    EXPECT_FALSE(change.old_version.empty());
  }
}

TEST_F(ResolverTest, ExplicitlyInstalledOrSharedDependenciesAreNotOrphans) {
  core_.add(pkg("unused"));
  finish();
  world_.installPackage(pkg("app", "1.0-1", {"explicit-lib", "shared-lib"}));
  world_.installPackage(pkg("explicit-lib"), InstallReason::Explicit);
  world_.installPackage(pkg("shared-lib"), InstallReason::Dependency);
  world_.installPackage(pkg("other", "1.0-1", {"shared-lib"}));
  const auto plan = remove({"app"});
  ASSERT_TRUE(plan.has_value());
  EXPECT_THAT(namesOf(*plan, ChangeKind::Orphan), IsEmpty());
  EXPECT_EQ(plan->changes.size(), 1U);
}

TEST_F(ResolverTest, InstalledDependentsBlockRemovalAndAreListed) {
  core_.add(pkg("unused"));
  finish();
  world_.installPackage(pkg("lib"));
  world_.installPackage(pkg("app", "1.0-1", {"lib"}));
  const auto before = holonight_packages_testing::manifestOf(world_.base() / "db");
  const auto plan = remove({"lib"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::BreaksDependents);
  EXPECT_THAT(plan.error().names, ElementsAre("app"));
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.base() / "db"), before);
}

TEST_F(ResolverTest, AbsentTargetsAreNotFoundAndProtectedOnesAreRefused) {
  core_.add(pkg("unused"));
  finish();
  world_.installPackage(pkg("base", "1-1", {"core-a"}));
  world_.installPackage(pkg("core-a"));
  world_.installPackage(pkg("app", "1.0-1", {"orphan-of-core"}));
  world_.installPackage(pkg("orphan-of-core"), InstallReason::Dependency);
  const auto absent = remove({"not-installed-pkg"});
  EXPECT_EQ(absent.error().code, TransactionErrorCode::PackageNotFound);
  EXPECT_THAT(absent.error().names, ElementsAre("not-installed-pkg"));
  const auto protectedTarget = remove({"core-a"});
  EXPECT_EQ(protectedTarget.error().code, TransactionErrorCode::ProtectedPackage);
  EXPECT_THAT(protectedTarget.error().names, ElementsAre("core-a"));
}

TEST_F(ResolverTest, AnOrphanListedInHoldPkgIsRefusedByName) {
  core_.add(pkg("unused"));
  finish();
  world_.installPackage(pkg("app", "1.0-1", {"shared"}));
  world_.installPackage(pkg("shared"), InstallReason::Dependency);
  std::ifstream input(world_.confPath());
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  text.replace(text.find("[options]\n") + 10, 0, "HoldPkg = shared\n");
  world_.writeConfText(text);
  const auto plan = remove({"app"});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::ProtectedPackage);
  EXPECT_THAT(plan.error().names, ElementsAre("shared"));
}

TEST_F(ResolverTest, RemovalDoesNotNeedTheRepositories) {
  core_.add(pkg("app"));
  finish();
  world_.installPackage(pkg("app"));
  std::filesystem::remove_all(world_.base() / "repos");
  std::filesystem::remove_all(world_.syncDbPath());
  const auto plan = remove({"app"});
  ASSERT_TRUE(plan.has_value()) << transactionErrorName(plan.error().code);
  EXPECT_THAT(namesOf(*plan, ChangeKind::Target), ElementsAre("app"));
}

}  // namespace
}  // namespace holonight_packages_transaction
