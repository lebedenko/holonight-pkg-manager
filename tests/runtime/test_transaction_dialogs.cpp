// Compiled-QML acceptance of the transaction dialogs and the Install / Remove controls, run under both styles by the
// runtime_controls_Holonight and runtime_controls_Fusion CTests. Every case names the controller call or fake-service
// reply that drives it and simulates no pointer or focus input (REQ-NF-004).
#include "ExploreModel.h"
#include "InstalledPackagesFilterModel.h"
#include "InstalledPackagesModel.h"
#include "TransactionController.h"
#include "UpdatesModel.h"
#include "fake_explore_source.h"
#include "fake_package_list_refresh.h"
#include "fake_transaction_service.h"
#include "fake_update_source.h"
#include "mock_package_source.h"

#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

using namespace holonight_packages_domain;
using holonight_packages_application::MockPackageSource;
using holonight_packages_application::PackageListUseCase;
using holonight_packages_testing::FakeExploreSource;
using holonight_packages_testing::FakePackageListRefresh;
using holonight_packages_testing::FakeTransactionService;
using holonight_packages_testing::FakeUpdateSource;
using State = TransactionController::State;

ResolvedPlan installPlan() {
  ResolvedPlan plan;
  plan.action = PlanAction::Install;
  plan.changes = {
      PlannedChange{
          .name = "vim",
          .kind = ChangeKind::Target,
          .old_version = "",
          .new_version = "9.1-1",
          .repository = "extra",
      },
      PlannedChange{
          .name = "gpm",
          .kind = ChangeKind::Dependency,
          .old_version = "",
          .new_version = "1.20-1",
          .repository = "core",
      },
  };
  plan.hooks = {HookPreview{.name = "ldconfig", .conditional = true}};
  return plan;
}

ResolvedPlan removePlan() {
  ResolvedPlan plan;
  plan.action = PlanAction::Remove;
  plan.changes = {
      PlannedChange{
          .name = "vim",
          .kind = ChangeKind::Target,
          .old_version = "9.1-1",
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
  return plan;
}

QQuickItem* visualObject(QQuickItem& item, const QString& name) {
  if (item.objectName() == name) {
    return &item;
  }
  for (auto* child : item.childItems()) {
    if (auto* found = visualObject(*child, name)) {
      return found;
    }
  }
  return nullptr;
}

int countObjects(QQuickItem& item, const QString& name) {
  int count = item.objectName() == name ? 1 : 0;
  for (auto* child : item.childItems()) {
    count += countObjects(*child, name);
  }
  return count;
}

class RuntimeTransactions : public testing::Test {
 public:
  void SetUp() override {
    source_ = std::make_shared<MockPackageSource>();
    Package vim;
    vim.name = "vim";
    vim.installedVersion = "9.1-1";
    vim.repository = "extra";
    vim.sourceType = SourceType::Official;
    vim.installReason = InstallReason::Explicit;
    EXPECT_CALL(*source_, enumerateInstalledPackages()).WillRepeatedly(testing::Return(std::vector<Package>{vim}));
    installed_ = std::make_unique<InstalledPackagesModel>(std::make_shared<PackageListUseCase>(source_));
    ASSERT_TRUE(
        QTest::qWaitFor([this] { return installed_->status() != InstalledPackagesModel::Status::Loading; }, 3000));
    updates_ = std::make_unique<UpdatesModel>(std::make_shared<FakeUpdateSource>());
    ASSERT_TRUE(QTest::qWaitFor([this] { return !updates_->loading(); }, 3000));
    explore_source_ = std::make_shared<FakeExploreSource>();
    ExploreSnapshot snapshot;
    snapshot.databasesFound = true;
    snapshot.dataAsOf = std::chrono::system_clock::now();
    snapshot.packages.push_back(SyncPackage{
        .name = "vim",
        .version = "9.1-1",
        .repository = "extra",
        .description = "Vi Improved",
        .url = "https://vim.example",
        .licenses = {"Vim"},
        .dependencies = {},
        .optionalDependencies = {},
        .downloadSizeBytes = 1,
        .installedSizeBytes = 2,
        .installedVersion = std::nullopt,
    });
    explore_snapshot_ = snapshot;
    explore_source_->enqueue(snapshot);
    explore_ = std::make_unique<ExploreModel>(explore_source_);
    ASSERT_TRUE(QTest::qWaitFor([this] { return !explore_->loading(); }, 3000));
    controller_ = std::make_unique<TransactionController>(service_, refresh_);

    view_.engine()->addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
    QObject::connect(view_.engine(), &QQmlEngine::warnings, view_.engine(), [this](const QList<QQmlError>& errors) {
      for (const auto& error : errors) {
        diagnostics_.append(error.toString());
      }
    });
    view_.resize(1360, 890);
    view_.setResizeMode(QQuickView::SizeRootObjectToView);
    view_.setInitialProperties({
        {QStringLiteral("installedPackagesModel"), QVariant::fromValue(installed_.get())},
        {QStringLiteral("updatesModel"), QVariant::fromValue(updates_.get())},
        {QStringLiteral("exploreModel"), QVariant::fromValue(explore_.get())},
        {QStringLiteral("transactionController"), QVariant::fromValue(controller_.get())},
    });
    view_.setSource(QUrl(QStringLiteral("qrc:/HolonightPackages/workspace/WorkspaceWindow.qml")));
    ASSERT_EQ(view_.status(), QQuickView::Ready);
    view_.show();
    QCoreApplication::processEvents();
    ASSERT_NE(view_.rootObject(), nullptr);
    filter_ = view_.rootObject()->findChild<InstalledPackagesFilterModel*>();
    ASSERT_NE(filter_, nullptr);
    ASSERT_TRUE(QTest::qWaitFor([this] { return filter_->rowCount() == 1; }, 3000));
    filter_->setCurrentRow(0);
    explore_->setSearchText(QStringLiteral("vim"));
    ASSERT_TRUE(QTest::qWaitFor([this] { return explore_->matchCount() == 1; }, 3000));
    explore_->setCurrentRow(0);
    QCoreApplication::processEvents();
  }

  void TearDown() override { EXPECT_TRUE(diagnostics_.isEmpty()) << qPrintable(diagnostics_.join('\n')); }

  QQuickItem* find(const char* name) { return visualObject(*view_.contentItem(), QString::fromLatin1(name)); }
  bool enabled(const char* name) {
    auto* item = find(name);
    return item != nullptr && item->property("enabled").toBool();
  }
  bool visible(const char* name) {
    auto* item = find(name);
    return item != nullptr && item->isVisible();
  }
  QString text(const char* name) {
    auto* item = find(name);
    return item == nullptr ? QString() : item->property("rawText").toString();
  }
  static void settle() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
  }
  void openPreview(const ResolvedPlan& plan, bool remove) {
    ASSERT_TRUE(remove ? controller_->requestRemove(QStringLiteral("vim"))
                       : controller_->requestInstall(QStringLiteral("vim")));
    ASSERT_TRUE(service_.completePrepare(FakeTransactionService::previewOf("plan-1", plan)));
    settle();
    ASSERT_NE(find("transactionPages"), nullptr);
  }
  void execute() const {
    controller_->confirm();
    settle();
  }
  void progress(ProgressPhase phase, std::uint32_t current, std::uint32_t total, const char* item) const {
    service_.emitProgress("plan-1", ProgressUpdate{.phase = phase, .current = current, .total = total, .item = item});
    settle();
  }

  FakeTransactionService service_;
  FakePackageListRefresh refresh_;
  std::shared_ptr<MockPackageSource> source_;
  std::unique_ptr<InstalledPackagesModel> installed_;
  std::unique_ptr<UpdatesModel> updates_;
  std::shared_ptr<FakeExploreSource> explore_source_;
  ExploreSnapshot explore_snapshot_;
  std::unique_ptr<ExploreModel> explore_;
  std::unique_ptr<TransactionController> controller_;
  QStringList diagnostics_;
  QQuickView view_;
  InstalledPackagesFilterModel* filter_ = nullptr;
};

class RuntimeTransactionDialogs : public RuntimeTransactions {};
class RuntimeTransactionScope : public RuntimeTransactions {};

// ---- Dialogs (T-022) ----------------------------------------------------------------------------------------------

TEST_F(RuntimeTransactionDialogs, ConfirmationAndResultPagesHaveActiveFocus) {
  openPreview(installPlan(), false);
  auto* dialog = view_.rootObject()->findChild<QObject*>(QStringLiteral("transactionDialog"));
  ASSERT_NE(dialog, nullptr);
  EXPECT_TRUE(QTest::qWaitFor([dialog] { return dialog->property("activeFocus").toBool(); }));

  execute();
  service_.completeExecute(ExecutionResult::succeeded({}));
  settle();
  ASSERT_TRUE(visible("transactionCloseButton"));
  EXPECT_TRUE(QTest::qWaitFor([dialog] { return dialog->property("activeFocus").toBool(); }));
}

TEST_F(RuntimeTransactionDialogs,
       ConfirmInstallShowsTwoRowsTheHookAndTheInstallLabel) {  // requestInstall + completePrepare
  openPreview(installPlan(), false);
  EXPECT_EQ(find("transactionPreviewList")->property("count").toInt(), 2);
  EXPECT_EQ(find("transactionHookList")->property("count").toInt(), 1);
  EXPECT_FALSE(text("transactionExpires").isEmpty());
  EXPECT_EQ(find("transactionConfirmButton")->property("text").toString(), QStringLiteral("Install"));
}

TEST_F(RuntimeTransactionDialogs,
       ConfirmRemoveLabelsTheButtonWithTheCount) {  // requestRemove + completePrepare(orphans)
  openPreview(removePlan(), true);
  EXPECT_EQ(find("transactionConfirmButton")->property("text").toString(), QStringLiteral("Remove 3 packages"));
}

TEST_F(RuntimeTransactionDialogs, ProgressIsDeterminateThenIndeterminate) {  // confirm + emitProgress
  openPreview(installPlan(), false);
  execute();
  EXPECT_TRUE(find("transactionProgressBar")->property("indeterminate").toBool()) << "authorizing";
  progress(ProgressPhase::Downloading, 3, 10, "foo");
  EXPECT_FALSE(find("transactionProgressBar")->property("indeterminate").toBool());
  EXPECT_DOUBLE_EQ(find("transactionProgressBar")->property("value").toDouble(), 0.3);
  EXPECT_EQ(text("transactionPhase"), QStringLiteral("Downloading"));
  EXPECT_EQ(text("transactionItem"), QStringLiteral("foo"));
  progress(ProgressPhase::Resolving, 0, 0, "");
  EXPECT_TRUE(find("transactionProgressBar")->property("indeterminate").toBool());
}

TEST_F(RuntimeTransactionDialogs, HooksDisableTheCancelControl) {  // emitProgress(Hooks)
  openPreview(installPlan(), false);
  execute();
  progress(ProgressPhase::Downloading, 1, 2, "foo");
  EXPECT_TRUE(enabled("transactionRunCancelButton"));
  progress(ProgressPhase::Hooks, 0, 0, "");
  EXPECT_FALSE(enabled("transactionRunCancelButton"));
}

TEST_F(RuntimeTransactionDialogs,
       SuccessWithAHookFailureShowsTheRowsAndTheNote) {  // completeExecute(Succeeded + HookFailed)
  openPreview(installPlan(), false);
  execute();
  ExecutionResult result = ExecutionResult::succeeded(
      {ChangedPackage{.name = "vim", .action = ChangeAction::Install, .old_version = "", .new_version = "9.1-1"}});
  result.warnings = {WarningToken::HookFailed};
  service_.completeExecute(result);
  settle();
  EXPECT_EQ(find("transactionResultList")->property("count").toInt(), 1);
  EXPECT_TRUE(text("transactionNotice").contains(QStringLiteral("hook")));
  EXPECT_TRUE(visible("transactionCloseButton"));
}

TEST_F(RuntimeTransactionDialogs, FailureCancelledAndUnknownShowTheirMessagesWithCloseOnly) {  // completeExecute(...)
  struct Case {
    ExecuteReply reply;
    const char* expected;
  };
  const std::vector<Case> cases = [] {
    std::vector<Case> list;
    list.push_back(
        {.reply = FakeTransactionService::failed(TransactionErrorCode::DiskSpace), .expected = "disk space"});
    list.push_back({.reply = ExecutionResult::cancelled(), .expected = "Nothing was changed"});
    list.push_back({
        .reply = std::unexpected(ServiceError{.kind = ServiceError::Kind::NoReply, .failure = {}}),
        .expected = "Installed list",
    });
    return list;
  }();
  for (const Case& test_case : cases) {
    openPreview(installPlan(), false);
    execute();
    service_.completeExecute(test_case.reply);
    settle();
    EXPECT_TRUE(text("transactionMessage").contains(QString::fromLatin1(test_case.expected))) << test_case.expected;
    EXPECT_TRUE(visible("transactionCloseButton"));
    EXPECT_FALSE(visible("transactionTryAgainButton"));
    controller_->dismiss();
    settle();
  }
}

TEST_F(RuntimeTransactionDialogs, HelperUnavailableOffersCloseOnly) {  // completePrepare(HelperUnavailable)
  ASSERT_TRUE(controller_->requestInstall(QStringLiteral("vim")));
  service_.completePrepare(std::unexpected(ServiceError{.kind = ServiceError::Kind::HelperUnavailable, .failure = {}}));
  settle();
  EXPECT_TRUE(text("transactionMessage").contains(QStringLiteral("not available")));
  EXPECT_TRUE(visible("transactionCloseButton"));
}

TEST_F(RuntimeTransactionDialogs, EachRefusalOffersTheRightActions) {  // completePrepare(refusal)
  for (std::size_t index = 0; index < kTransactionErrorCodeCount; ++index) {
    const auto code = static_cast<TransactionErrorCode>(index);
    ASSERT_TRUE(controller_->requestInstall(QStringLiteral("vim"))) << index;
    service_.completePrepare(FakeTransactionService::refusal(code, {"vim"}));
    settle();
    const bool retry = code == TransactionErrorCode::Busy || code == TransactionErrorCode::PlanNotFound ||
                       code == TransactionErrorCode::PlanExpired || code == TransactionErrorCode::SyncDatabaseChanged;
    EXPECT_TRUE(visible("transactionCloseButton")) << index;
    EXPECT_EQ(visible("transactionTryAgainButton"), retry) << index;
    // system-upgrade REQ-F-089: only a partial upgrade offers the system upgrade.
    EXPECT_EQ(visible("transactionUpdateSystemButton"), code == TransactionErrorCode::PartialUpgradeRequired) << index;
    EXPECT_FALSE(visible("transactionConfirmButton")) << index;
    EXPECT_FALSE(text("transactionMessage").isEmpty()) << index;
    controller_->dismiss();
    settle();
  }
}

// ---- system-upgrade T-074: the upgrade dialogs, in both styles -----------------------------------------------------

ResolvedPlan upgradePlan(int upgrades, bool keyring) {
  ResolvedPlan plan;
  plan.action = PlanAction::Upgrade;
  UpgradeDetail detail;
  for (int index = 0; index < upgrades; ++index) {
    detail.upgrades.push_back(UpgradeEntry{.name = "package-" + std::to_string(index),
                                           .old_version = "1.0-1",
                                           .new_version = "1.1-1",
                                           .repository = "extra"});
  }
  detail.replacements = {ReplacementEntry{
      .old_name = "oldpkg", .old_version = "1-1", .new_name = "newpkg", .new_version = "2-1", .repository = "extra"}};
  detail.new_dependencies = {NewDependencyEntry{.name = "dep", .version = "1-1", .repository = "core"}};
  detail.held_back = {HeldBackEntry{.name = "kept", .installed_version = "1-1"}};
  if (keyring) {
    detail.keyring_packages = {"archlinux-keyring"};
  }
  detail.download_bytes = 123456789;
  detail.size_change_bytes = 4567890;
  plan.upgrade = detail;
  return plan;
}

TEST_F(RuntimeTransactionDialogs, TheUpdatesPageOffersUpdateSystemForAnEmptyList) {  // probeHelper + completeProbes
  view_.rootObject()->setProperty("currentPage", QStringLiteral("updates"));
  settle();
  service_.setVersion(2);
  service_.completeProbes();
  settle();
  ASSERT_TRUE(visible("updateSystemButton"));
  EXPECT_TRUE(enabled("updateSystemButton"));  // the Updates list is empty, the button is not (REQ-F-078)
}

TEST_F(RuntimeTransactionDialogs, ThePreviewOfFiveHundredUpgradesAndTheStepCounterRender) {  // requestUpgrade
  service_.setVersion(2);
  ASSERT_TRUE(controller_->requestUpgrade());
  service_.completeProbes();
  settle();
  service_.emitProgress("", ProgressUpdate{.phase = ProgressPhase::Syncing, .current = 1, .total = 2, .item = "core"});
  settle();
  EXPECT_TRUE(text("transactionMessage").contains(QStringLiteral("1 of 2")));
  EXPECT_TRUE(visible("transactionPrepareCancelButton"));
  ASSERT_TRUE(service_.completePrepare(FakeTransactionService::previewOf("plan-u", upgradePlan(500, true))));
  settle();
  auto* list = find("upgradePreviewList");
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(list->property("count").toInt(), 503);
  EXPECT_TRUE(visible("upgradeKeyringNotice"));
  EXPECT_FALSE(text("upgradeDownloadSize").isEmpty());
  EXPECT_EQ(find("transactionConfirmButton")->property("text").toString(), QStringLiteral("Update"));

  controller_->confirm();
  settle();
  service_.emitProgress("plan-u", ProgressUpdate{.phase = ProgressPhase::Promoting});
  service_.emitProgress("plan-u", ProgressUpdate{.phase = ProgressPhase::KeyringStep, .current = 1, .total = 2});
  settle();
  EXPECT_TRUE(text("transactionStep").contains(QStringLiteral("1 of 2")));
  EXPECT_FALSE(enabled("transactionRunCancelButton"));
  service_.emitProgress("plan-u", ProgressUpdate{.phase = ProgressPhase::MainStep, .current = 2, .total = 2});
  service_.emitProgress(
      "plan-u", ProgressUpdate{.phase = ProgressPhase::Installing, .current = 3, .total = 500, .item = "package-3"});
  settle();
  EXPECT_TRUE(text("transactionStep").contains(QStringLiteral("2 of 2")));
  service_.completeExecute(ExecutionResult::succeeded({ChangedPackage{
      .name = "archlinux-keyring", .action = ChangeAction::Install, .old_version = "1", .new_version = "2"}}));
  settle();
  EXPECT_TRUE(text("transactionMessage").contains(QStringLiteral("keyring")));
  EXPECT_TRUE(visible("transactionCloseButton"));
}

TEST_F(RuntimeTransactionDialogs, LongUpgradePreviewsKeepActionsInsideASmallWindow) {
  view_.resize(720, 480);
  for (const bool keyring : {false, true}) {
    service_.setVersion(2);
    ASSERT_TRUE(controller_->requestUpgrade());
    service_.completeProbes();
    ASSERT_TRUE(service_.completePrepare(FakeTransactionService::previewOf("plan-u", upgradePlan(500, keyring))));
    settle();
    auto* list = qobject_cast<QQuickItem*>(find("upgradePreviewList"));
    ASSERT_NE(list, nullptr);
    EXPECT_GT(list->height(), 0);
    EXPECT_GT(list->property("contentHeight").toReal(), list->height());
    for (const auto* name :
         {"transactionConfirmButton", "transactionCancelButton", "upgradeDownloadSize", "transactionExpires"}) {
      auto* item = qobject_cast<QQuickItem*>(find(name));
      ASSERT_NE(item, nullptr);
      const auto bounds = item->mapRectToScene(item->boundingRect());
      EXPECT_GE(bounds.top(), 0) << name;
      EXPECT_LE(bounds.bottom(), 480) << name;
    }
    controller_->dismiss();
    settle();
  }
}

// ---- Busy state, scope, close and idle behaviour (T-023) ----------------------------------------------------------

TEST_F(RuntimeTransactionScope, InstallAndRemoveAreDisabledInTheFourBusyStatesAndEnabledOtherwise) {
  // Idle: enabled.
  EXPECT_TRUE(enabled("packageDetailRemoveButton"));
  EXPECT_TRUE(enabled("packageDetailInstallButton"));

  ASSERT_TRUE(controller_->requestInstall(QStringLiteral("other")));  // preparing
  settle();
  EXPECT_EQ(controller_->state(), State::Preparing);
  EXPECT_FALSE(enabled("packageDetailRemoveButton"));
  EXPECT_FALSE(enabled("packageDetailInstallButton"));

  service_.completePrepare(FakeTransactionService::previewOf("plan-1", installPlan()));  // previewed
  settle();
  EXPECT_EQ(controller_->state(), State::Previewed);
  EXPECT_FALSE(enabled("packageDetailRemoveButton"));
  EXPECT_FALSE(enabled("packageDetailInstallButton"));

  controller_->confirm();  // authorizing
  settle();
  EXPECT_EQ(controller_->state(), State::Authorizing);
  EXPECT_FALSE(enabled("packageDetailRemoveButton"));
  EXPECT_FALSE(enabled("packageDetailInstallButton"));

  progress(ProgressPhase::Downloading, 1, 2, "x");  // executing
  EXPECT_EQ(controller_->state(), State::Executing);
  EXPECT_FALSE(enabled("packageDetailRemoveButton"));
  EXPECT_FALSE(enabled("packageDetailInstallButton"));

  service_.completeExecute(ExecutionResult::succeeded());  // terminal: enabled again
  settle();
  EXPECT_EQ(controller_->state(), State::Succeeded);
  EXPECT_TRUE(enabled("packageDetailRemoveButton"));
  EXPECT_TRUE(enabled("packageDetailInstallButton"));
}

TEST_F(RuntimeTransactionScope, OnlyTheInstalledPanelHasRemoveAndOnlyTheExplorePanelHasInstall) {
  EXPECT_EQ(countObjects(*view_.contentItem(), QStringLiteral("packageDetailRemoveButton")), 1);
  EXPECT_EQ(countObjects(*view_.contentItem(), QStringLiteral("packageDetailInstallButton")), 1);
  auto* remove = find("packageDetailRemoveButton");
  auto* install = find("packageDetailInstallButton");
  ASSERT_NE(remove, nullptr);
  ASSERT_NE(install, nullptr);
  auto isInside = [](QQuickItem* item, const QString& panel_name) {
    for (QQuickItem* parent = item; parent != nullptr; parent = parent->parentItem()) {
      if (parent->objectName() == panel_name) {
        return true;
      }
    }
    return false;
  };
  EXPECT_TRUE(isInside(remove, QStringLiteral("installedPage")));
  EXPECT_TRUE(isInside(install, QStringLiteral("explorePage")));
  EXPECT_FALSE(isInside(remove, QStringLiteral("updatesPage")));
  EXPECT_FALSE(isInside(install, QStringLiteral("updatesPage")));
}

TEST_F(RuntimeTransactionScope,
       TheCloseUnavailableNoticeShowsAfterTheCommitPoint) {  // emitProgress(Hooks) + handleCloseRequest
  openPreview(installPlan(), false);
  execute();
  progress(ProgressPhase::Hooks, 0, 0, "");
  EXPECT_EQ(controller_->handleCloseRequest(), holonight_packages_application::CloseDecision::Ignore);
  settle();
  EXPECT_TRUE(text("transactionNotice").contains(QStringLiteral("Closing is unavailable")));
  EXPECT_EQ(service_.cancelCalls(), 0U);
}

TEST_F(RuntimeTransactionScope, OpeningThePagesMakesNoTransactionCall) {
  for (const char* page : {"installed", "updates", "explore", "installed"}) {
    view_.rootObject()->setProperty("currentPage", QString::fromLatin1(page));
    settle();
  }
  EXPECT_TRUE(service_.calls().empty());
  EXPECT_EQ(controller_->state(), State::Idle);
  EXPECT_EQ(refresh_.count(), 0);
}

TEST_F(RuntimeTransactionScope, ActivatingInstallAndRemoveRequestsPlansForTheSelectedPackages) {
  ASSERT_TRUE(QMetaObject::invokeMethod(find("packageDetailRemoveButton"), "clicked"));
  settle();
  ASSERT_EQ(service_.prepareCalls(), 1U);
  EXPECT_EQ(service_.calls().front().request.action, PlanAction::Remove);
  EXPECT_EQ(service_.calls().front().request.names, std::vector<std::string>{"vim"});
}

TEST_F(RuntimeTransactionScope, ExploreDetailsFollowInstalledStateAfterReload) {
  view_.rootObject()->setProperty("currentPage", QStringLiteral("explore"));
  settle();
  auto* panel = find("exploreDetailPanel");
  ASSERT_NE(panel, nullptr);
  auto* indicator = visualObject(*panel, QStringLiteral("packageDetailInstalledIndicator"));
  ASSERT_NE(indicator, nullptr);
  EXPECT_FALSE(indicator->isVisible());

  explore_snapshot_.packages.front().installedVersion = "9.1-1";
  explore_source_->enqueue(explore_snapshot_);
  explore_->reload();
  ASSERT_TRUE(QTest::qWaitFor([this] { return !explore_->loading(); }, 3000));
  settle();
  EXPECT_TRUE(indicator->isVisible());

  explore_snapshot_.packages.front().installedVersion.reset();
  explore_source_->enqueue(explore_snapshot_);
  explore_->reload();
  ASSERT_TRUE(QTest::qWaitFor([this] { return !explore_->loading(); }, 3000));
  settle();
  EXPECT_FALSE(indicator->isVisible());
}

}  // namespace
