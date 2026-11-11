#include "TransactionRowModel.h"

#include <QSignalSpy>

#include <gtest/gtest.h>

namespace {

using namespace holonight_packages_domain;

ResolvedPlan fakePreview() {
  ResolvedPlan plan;
  plan.action = PlanAction::Remove;
  plan.changes = {
      PlannedChange{
          .name = "app",
          .kind = ChangeKind::Target,
          .old_version = "1-1",
          .new_version = "",
          .repository = "",
      },
      PlannedChange{
          .name = "lib",
          .kind = ChangeKind::Orphan,
          .old_version = "2-1",
          .new_version = "",
          .repository = "",
      },
      PlannedChange{
          .name = "util",
          .kind = ChangeKind::Orphan,
          .old_version = "3-1",
          .new_version = "",
          .repository = "",
      },
  };
  plan.hooks = {
      HookPreview{.name = "ldconfig", .conditional = false},
      HookPreview{.name = "mime", .conditional = true},
  };
  return plan;
}

ResolvedPlan installPreview() {
  ResolvedPlan plan;
  plan.action = PlanAction::Install;
  plan.changes = {
      PlannedChange{
          .name = "app",
          .kind = ChangeKind::Target,
          .old_version = "",
          .new_version = "1-1",
          .repository = "extra",
      },
      PlannedChange{
          .name = "lib",
          .kind = ChangeKind::Dependency,
          .old_version = "",
          .new_version = "2-1",
          .repository = "core",
      },
  };
  return plan;
}

QVariant roleOf(const TransactionRowModel& model, int row, int role) { return model.data(model.index(row), role); }

TEST(TransactionRowModel, TargetDependencyAndOrphanRowsCarryTheirKindRepositoryAndVersionRoles) {
  TransactionRowModel model;
  model.setRows(TransactionRowModel::previewRows(installPreview()));
  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(roleOf(model, 0, TransactionRowModel::KindRole), QStringLiteral("target"));
  EXPECT_EQ(roleOf(model, 0, TransactionRowModel::RepositoryRole), QStringLiteral("extra"));
  EXPECT_EQ(roleOf(model, 0, TransactionRowModel::NewVersionRole), QStringLiteral("1-1"));
  EXPECT_EQ(roleOf(model, 0, TransactionRowModel::ActionRole), QStringLiteral("install"));
  EXPECT_EQ(roleOf(model, 1, TransactionRowModel::KindRole), QStringLiteral("dependency"));
  EXPECT_EQ(roleOf(model, 1, TransactionRowModel::RepositoryRole), QStringLiteral("core"));

  model.setRows(TransactionRowModel::previewRows(fakePreview()));
  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_EQ(roleOf(model, 1, TransactionRowModel::KindRole), QStringLiteral("orphan"));
  EXPECT_EQ(roleOf(model, 1, TransactionRowModel::ActionRole), QStringLiteral("remove"));
  EXPECT_EQ(roleOf(model, 1, TransactionRowModel::OldVersionRole), QStringLiteral("2-1"));
}

TEST(TransactionRowModel, HookRowsCarryTheConditionalMarker) {
  TransactionRowModel model;
  model.setRows(TransactionRowModel::hookRows(fakePreview()));
  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(roleOf(model, 0, TransactionRowModel::KindRole), QStringLiteral("hook"));
  EXPECT_EQ(roleOf(model, 0, TransactionRowModel::ConditionalRole), false);
  EXPECT_EQ(roleOf(model, 1, TransactionRowModel::ConditionalRole), true);
}

TEST(TransactionRowModel, APreviewWithNoHooksHasNoHookRow) {
  TransactionRowModel model;
  model.setRows(TransactionRowModel::hookRows(installPreview()));
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(TransactionRowModel, ChangedRowsCarryNameActionAndVersions) {
  TransactionRowModel model;
  model.setRows(TransactionRowModel::changedRows({
      ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1-1"},
      ChangedPackage{.name = "old", .action = ChangeAction::Remove, .old_version = "9-1", .new_version = ""},
  }));
  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(roleOf(model, 0, TransactionRowModel::KindRole), QStringLiteral("changed"));
  EXPECT_EQ(roleOf(model, 0, TransactionRowModel::ActionRole), QStringLiteral("install"));
  EXPECT_EQ(roleOf(model, 1, TransactionRowModel::ActionRole), QStringLiteral("remove"));
  EXPECT_EQ(roleOf(model, 1, TransactionRowModel::OldVersionRole), QStringLiteral("9-1"));
}

TEST(TransactionRowModel, ANameWithMarkupIsReturnedUnchanged) {
  TransactionRowModel model;
  ResolvedPlan plan = installPreview();
  plan.changes[0].name = "<b>x</b>";
  model.setRows(TransactionRowModel::previewRows(plan));
  EXPECT_EQ(roleOf(model, 0, TransactionRowModel::NameRole).toString(), QStringLiteral("<b>x</b>"));
}

TEST(TransactionRowModel, ReplacingTheContentResetsTheModelAndOutOfRangeRowsAreEmpty) {
  TransactionRowModel model;
  QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
  model.setRows(TransactionRowModel::previewRows(installPreview()));
  EXPECT_EQ(resets.count(), 1);
  model.setRows(TransactionRowModel::previewRows(installPreview()));
  EXPECT_EQ(resets.count(), 1) << "identical content is not a change";
  EXPECT_FALSE(model.data(model.index(5), TransactionRowModel::NameRole).isValid());
  model.clear();
  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_EQ(resets.count(), 2);
}

TEST(TransactionRowModel, ExposesTheRoleNamesTheQmlUses) {
  const TransactionRowModel model;
  const auto names = model.roleNames();
  for (const char* expected : {"name", "kind", "action", "oldVersion", "newVersion", "repository", "conditional"}) {
    EXPECT_TRUE(names.values().contains(expected)) << expected;
  }
}

}  // namespace
