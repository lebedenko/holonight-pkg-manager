#include "holonight_packages_domain/transaction_plan.h"

#include <gtest/gtest.h>

namespace holonight_packages_domain {
namespace {

PlannedChange install(std::string name, ChangeKind kind, std::string version, std::string repository) {
  return PlannedChange{
      .name = std::move(name),
      .kind = kind,
      .old_version = {},
      .new_version = std::move(version),
      .repository = std::move(repository),
      .download_bytes = 0,
  };
}

PlannedChange removal(std::string name, ChangeKind kind, std::string version) {
  return PlannedChange{
      .name = std::move(name),
      .kind = kind,
      .old_version = std::move(version),
      .new_version = {},
      .repository = {},
      .download_bytes = 0,
  };
}

ResolvedPlan installPlan() {
  ResolvedPlan plan;
  plan.action = PlanAction::Install;
  plan.changes = {
      install("app", ChangeKind::Target, "1.0-1", "core"),
      install("lib", ChangeKind::Dependency, "2.0-1", "core"),
  };
  plan.hooks = {
      HookPreview{.name = "10-first.hook", .conditional = false},
      HookPreview{.name = "20-second.hook", .conditional = true},
  };
  return plan;
}

ResolvedPlan removalPlan() {
  ResolvedPlan plan;
  plan.action = PlanAction::Remove;
  plan.changes = {removal("app", ChangeKind::Target, "1.0-1"), removal("lib", ChangeKind::Orphan, "2.0-1")};
  return plan;
}

TEST(SamePlanTest, IdenticalPlansAreEqual) {
  EXPECT_TRUE(samePlan(installPlan(), installPlan()));
  EXPECT_TRUE(samePlan(removalPlan(), removalPlan()));
}

TEST(SamePlanTest, DownloadSizesAndMirrorHostsDoNotTakePart) {
  ResolvedPlan other = installPlan();
  other.changes[0].download_bytes = 123456;
  other.mirror_hosts = {"mirror.example"};
  EXPECT_TRUE(samePlan(installPlan(), other));
}

TEST(SamePlanTest, OrderOfChangesDoesNotMatter) {
  ResolvedPlan other = installPlan();
  std::swap(other.changes[0], other.changes[1]);
  EXPECT_TRUE(samePlan(installPlan(), other));
}

TEST(SamePlanTest, InstallRoleDoesNotMatterButNameVersionAndRepositoryDo) {
  ResolvedPlan role = installPlan();
  role.changes[1].kind = ChangeKind::Target;
  EXPECT_TRUE(samePlan(installPlan(), role));

  ResolvedPlan version = installPlan();
  version.changes[1].new_version = "2.0-2";
  EXPECT_FALSE(samePlan(installPlan(), version));

  ResolvedPlan repository = installPlan();
  repository.changes[0].repository = "extra";
  EXPECT_FALSE(samePlan(installPlan(), repository));

  ResolvedPlan added = installPlan();
  added.changes.push_back(install("extra-dep", ChangeKind::Dependency, "1-1", "core"));
  EXPECT_FALSE(samePlan(installPlan(), added));
}

TEST(SamePlanTest, AnAddedOrphanOrAChangedInstalledVersionMakesRemovalPlansUnequal) {
  ResolvedPlan orphan = removalPlan();
  orphan.changes.push_back(removal("extra", ChangeKind::Orphan, "1-1"));
  EXPECT_FALSE(samePlan(removalPlan(), orphan));

  ResolvedPlan version = removalPlan();
  version.changes[0].old_version = "1.0-2";
  EXPECT_FALSE(samePlan(removalPlan(), version));
}

TEST(SamePlanTest, ARemovalThatBecomesAnOrphanIsAChange) {
  ResolvedPlan other = removalPlan();
  other.changes[1].kind = ChangeKind::Target;
  EXPECT_FALSE(samePlan(removalPlan(), other));
}

TEST(SamePlanTest, HookOrderAndConditionalityTakePart) {
  ResolvedPlan reordered = installPlan();
  std::swap(reordered.hooks[0], reordered.hooks[1]);
  EXPECT_FALSE(samePlan(installPlan(), reordered));

  ResolvedPlan conditional = installPlan();
  conditional.hooks[0].conditional = true;
  EXPECT_FALSE(samePlan(installPlan(), conditional));
}

TEST(SamePlanTest, DifferentActionsAreUnequal) { EXPECT_FALSE(samePlan(installPlan(), removalPlan())); }

}  // namespace
}  // namespace holonight_packages_domain
