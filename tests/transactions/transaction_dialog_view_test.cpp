// Source-based QML test of qml/transactions/*.qml over a real TransactionController and the fake service. It drives
// the controller only: no pointer, touch or focus input is simulated (REQ-NF-004).
#include "TransactionController.h"
#include "fake_package_list_refresh.h"
#include "fake_transaction_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

using namespace holonight_packages_domain;
using holonight_packages_testing::FakePackageListRefresh;
using holonight_packages_testing::FakeTransactionService;

QUrl qmlSource(const QString& path) { return QUrl::fromLocalFile(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR) + path); }

ResolvedPlan installPlan() {
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
  plan.hooks = {HookPreview{.name = "ldconfig", .conditional = true}};
  return plan;
}

class TransactionDialogView : public testing::Test {
 public:
  void SetUp() override {
    QObject::connect(&engine_, &QQmlEngine::warnings, &engine_, [](const QList<QQmlError>& errors) {
      for (const auto& error : errors) {
        ADD_FAILURE() << qPrintable(error.toString());
      }
    });
    controller_ = std::make_unique<TransactionController>(service_, refresh_);
    QQmlComponent component(&engine_);
    component.setData(R"(
      import QtQuick
      Item {
        id: host
        property var controller
        width: 900; height: 700
        TransactionDialog { controller: host.controller }
      }
    )",
                      qmlSource(QStringLiteral("/transactions/")));
    ASSERT_EQ(component.status(), QQmlComponent::Ready) << component.errorString().toStdString();
    host_.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
        {{QStringLiteral("controller"), QVariant::fromValue(controller_.get())}})));
    ASSERT_NE(host_, nullptr);
    host_->setParentItem(window_.contentItem());
    window_.resize(900, 700);
    window_.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window_));
  }

  void TearDown() override { host_.reset(); }

  void toPreviewed() {
    ASSERT_TRUE(controller_->requestInstall(QStringLiteral("app")));
    service_.completePrepare(FakeTransactionService::previewOf("plan-1", installPlan()));
    ASSERT_TRUE(QTest::qWaitFor([this] { return popup()->property("opened").toBool(); }, 3000));
    QCoreApplication::processEvents();
  }
  void toExecuting(ProgressPhase phase) {
    toPreviewed();
    controller_->confirm();
    service_.emitProgress("plan-1", ProgressUpdate{.phase = phase, .current = 3, .total = 10, .item = "foo"});
    QCoreApplication::processEvents();
  }

  [[nodiscard]] QObject* popup() const { return host_->findChild<QObject*>(QStringLiteral("transactionDialog")); }

  // Every item below the window content, popups included (the overlay is a sibling subtree of the host).
  [[nodiscard]] std::vector<QQuickItem*> allItems() const {
    std::vector<QQuickItem*> out;
    std::vector<QQuickItem*> pending{window_.contentItem()};
    while (!pending.empty()) {
      QQuickItem* item = pending.back();
      pending.pop_back();
      out.push_back(item);
      for (QQuickItem* child : item->childItems()) {
        pending.push_back(child);
      }
    }
    return out;
  }
  [[nodiscard]] QQuickItem* find(const char* name) const {
    const QString wanted = QString::fromLatin1(name);
    for (QQuickItem* item : allItems()) {
      if (item->objectName() == wanted) {
        return item;
      }
    }
    return nullptr;
  }
  [[nodiscard]] bool visible(const char* name) const {
    const QQuickItem* item = find(name);
    return item != nullptr && item->isVisible();
  }

  QQmlEngine engine_;
  FakeTransactionService service_;
  FakePackageListRefresh refresh_;
  std::unique_ptr<TransactionController> controller_;
  QQuickWindow window_;
  std::unique_ptr<QQuickItem> host_;
};

TEST_F(TransactionDialogView, TheConfirmPageShowsTwoRowsTheExpiryAndTheInstallLabel) {
  toPreviewed();
  auto* list = find("transactionPreviewList");
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(list->property("count").toInt(), 2);
  auto* expires = find("transactionExpires");
  ASSERT_NE(expires, nullptr);
  EXPECT_FALSE(expires->property("rawText").toString().isEmpty());
  auto* confirm = find("transactionConfirmButton");
  ASSERT_NE(confirm, nullptr);
  EXPECT_EQ(confirm->property("text").toString(), QStringLiteral("Install"));
  EXPECT_TRUE(confirm->property("enabled").toBool());
  EXPECT_TRUE(visible("transactionCancelButton"));
  EXPECT_FALSE(visible("transactionCloseButton"));
  auto* hooks = find("transactionHookList");
  ASSERT_NE(hooks, nullptr);
  EXPECT_EQ(hooks->property("count").toInt(), 1);
}

TEST_F(TransactionDialogView, TheClosePolicyExcludesPressOutsideAndFollowsClosable) {
  toPreviewed();
  constexpr int kCloseOnEscape = 0x10;        // QQuickPopup::CloseOnEscape
  constexpr int kCloseOnPressOutside = 0x0F;  // any of the press/release-outside policies
  EXPECT_TRUE(controller_->closable());
  EXPECT_EQ(popup()->property("closePolicy").toInt() & kCloseOnPressOutside, 0);
  EXPECT_EQ(popup()->property("closePolicy").toInt() & kCloseOnEscape, kCloseOnEscape);

  controller_->confirm();
  ASSERT_FALSE(controller_->closable());
  EXPECT_EQ(popup()->property("closePolicy").toInt(), 0) << "NoAutoClose while the transaction runs";
}

TEST_F(TransactionDialogView, TheDialogOpensWithTheControllerAndClosesWhenTheControllerReturnsToIdle) {
  EXPECT_FALSE(popup()->property("opened").toBool());
  toPreviewed();
  EXPECT_TRUE(popup()->property("opened").toBool());
  controller_->dismiss();
  ASSERT_TRUE(QTest::qWaitFor([this] { return !popup()->property("opened").toBool(); }, 3000));
}

TEST_F(TransactionDialogView, ThePackageNameAppearsAsPlainText) {
  ASSERT_TRUE(controller_->requestInstall(QStringLiteral("<b>x</b>")));
  service_.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::PackageNotFound, {"<b>x</b>"}));
  ASSERT_TRUE(QTest::qWaitFor([this] { return popup()->property("opened").toBool(); }, 3000));
  QCoreApplication::processEvents();
  auto* message = find("transactionMessage");
  ASSERT_NE(message, nullptr);
  EXPECT_TRUE(message->property("rawText").toString().contains(QStringLiteral("<b>x</b>")));
}

// ---- Progress and result pages (T-019) ----------------------------------------------------------------------------

TEST_F(TransactionDialogView, TheProgressPageHoldsOnlyPhaseItemBarAndTheCancelControl) {
  toExecuting(ProgressPhase::Downloading);
  EXPECT_TRUE(visible("transactionPhase"));
  EXPECT_EQ(find("transactionPhase")->property("rawText").toString(), QStringLiteral("Downloading"));
  EXPECT_EQ(find("transactionItem")->property("rawText").toString(), QStringLiteral("foo"));
  auto* bar = find("transactionProgressBar");
  ASSERT_NE(bar, nullptr);
  EXPECT_FALSE(bar->property("indeterminate").toBool());
  EXPECT_DOUBLE_EQ(bar->property("value").toDouble(), 0.3);
  EXPECT_TRUE(find("transactionRunCancelButton")->property("enabled").toBool());
  for (QQuickItem* item : allItems()) {
    const QString type = QString::fromLatin1(item->metaObject()->className());
    EXPECT_FALSE(type.contains(QStringLiteral("TextArea")) || type.contains(QStringLiteral("TextEdit")) ||
                 type.contains(QStringLiteral("ListView")) || type.contains(QStringLiteral("Repeater")))
        << qPrintable(type);
  }
}

TEST_F(TransactionDialogView, TheCancelControlIsDisabledAfterAHooksEvent) {
  toExecuting(ProgressPhase::Downloading);
  ASSERT_TRUE(find("transactionRunCancelButton")->property("enabled").toBool());
  service_.emitProgress("plan-1", ProgressUpdate{.phase = ProgressPhase::Hooks, .current = 0, .total = 0, .item = ""});
  QCoreApplication::processEvents();
  EXPECT_FALSE(find("transactionRunCancelButton")->property("enabled").toBool());
  EXPECT_TRUE(find("transactionProgressBar")->property("indeterminate").toBool());
}

// GUI REQ-F-022 as amended by system-upgrade REQ-F-089: Close and "Update system…", never a retry of the install.
TEST_F(TransactionDialogView, APartialUpgradeRefusalOffersCloseAndUpdateSystem) {
  ASSERT_TRUE(controller_->requestInstall(QStringLiteral("app")));
  service_.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::PartialUpgradeRequired, {"glibc"}));
  ASSERT_TRUE(QTest::qWaitFor([this] { return popup()->property("opened").toBool(); }, 3000));
  QCoreApplication::processEvents();
  EXPECT_TRUE(visible("transactionCloseButton"));
  EXPECT_TRUE(visible("transactionUpdateSystemButton"));
  EXPECT_FALSE(visible("transactionConfirmButton"));
  EXPECT_FALSE(visible("transactionTryAgainButton"));
  EXPECT_FALSE(visible("transactionCancelButton"));
  EXPECT_TRUE(find("transactionMessage")->property("rawText").toString().contains(QStringLiteral("Update system")));
}

TEST_F(TransactionDialogView, ABusyRefusalOffersTryAgainAndClose) {
  ASSERT_TRUE(controller_->requestInstall(QStringLiteral("app")));
  service_.completePrepare(FakeTransactionService::refusal(TransactionErrorCode::Busy));
  ASSERT_TRUE(QTest::qWaitFor([this] { return popup()->property("opened").toBool(); }, 3000));
  QCoreApplication::processEvents();
  EXPECT_TRUE(visible("transactionTryAgainButton"));
  EXPECT_TRUE(visible("transactionCloseButton"));
}

TEST_F(TransactionDialogView, ASuccessResultListsTheChangedPackagesAndTheHookNote) {
  toPreviewed();
  controller_->confirm();
  ExecutionResult result = ExecutionResult::succeeded(
      {ChangedPackage{.name = "app", .action = ChangeAction::Install, .old_version = "", .new_version = "1-1"}});
  result.warnings = {WarningToken::HookFailed};
  service_.completeExecute(result);
  QCoreApplication::processEvents();
  EXPECT_EQ(find("transactionResultList")->property("count").toInt(), 1);
  EXPECT_TRUE(visible("transactionNotice"));
  EXPECT_TRUE(find("transactionNotice")->property("rawText").toString().contains(QStringLiteral("hook")));
  EXPECT_TRUE(visible("transactionCloseButton"));
}

TEST_F(TransactionDialogView, EveryLabelRendersAsPlainText) {
  toPreviewed();
  int labels = 0;
  const auto check = [&] {
    for (QQuickItem* item : allItems()) {
      if (item->metaObject()->indexOfProperty("rawText") >= 0) {
        ++labels;
        EXPECT_EQ(item->property("textFormat").toInt(), static_cast<int>(Qt::PlainText))
            << qPrintable(item->objectName());
      }
    }
  };
  check();
  controller_->confirm();
  service_.completeExecute(FakeTransactionService::failed(TransactionErrorCode::DiskSpace));
  QCoreApplication::processEvents();
  check();
  EXPECT_GT(labels, 4);
}

TEST_F(TransactionDialogView, TheQmlSourcesUseNoRichTextAndTheTestsSimulateNoInput) {
  // qml/transactions: no rich-text formats and no tag strings.
  QDir dir(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR) + QStringLiteral("/transactions"));
  const auto files = dir.entryList({QStringLiteral("*.qml")});
  EXPECT_FALSE(files.isEmpty());
  for (const QString& file : files) {
    QFile qml(dir.filePath(file));
    ASSERT_TRUE(qml.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(qml.readAll());
    for (const char* forbidden : {"RichText", "StyledText", "AutoText", "MarkdownText", "textFormat"}) {
      EXPECT_FALSE(text.contains(QLatin1String(forbidden))) << qPrintable(file) << " " << forbidden;
    }
  }
}

// ---- system-upgrade T-071 to T-073: the upgrade pages ----

ResolvedPlan upgradePlanOf(int upgrades, bool keyring) {
  ResolvedPlan plan;
  plan.action = PlanAction::Upgrade;
  UpgradeDetail detail;
  for (int index = 0; index < upgrades; ++index) {
    detail.upgrades.push_back(UpgradeEntry{
        .name = "pkg-" + std::to_string(index), .old_version = "1", .new_version = "2", .repository = "core"});
  }
  detail.held_back = {HeldBackEntry{.name = "kept", .installed_version = "1"}};
  if (keyring) {
    detail.keyring_packages = {"archlinux-keyring"};
  }
  detail.download_bytes = 4096;
  plan.upgrade = detail;
  return plan;
}

class TransactionDialogUpgradeView : public TransactionDialogView {
 public:
  void startUpgrade() {
    service_.setVersion(2);
    ASSERT_TRUE(controller_->requestUpgrade());
    service_.completeProbes();
    ASSERT_TRUE(QTest::qWaitFor([this] { return popup()->property("opened").toBool(); }, 3000));
    QCoreApplication::processEvents();
  }
  void toUpgradePreview(int upgrades = 3, bool keyring = false) {
    startUpgrade();
    service_.completePrepare(FakeTransactionService::previewOf("plan-u", upgradePlanOf(upgrades, keyring)));
    QCoreApplication::processEvents();
  }
};

TEST_F(TransactionDialogUpgradeView, ThePreparingPageShowsTheSyncAndOffersCancel) {
  startUpgrade();
  service_.emitProgress("", ProgressUpdate{.phase = ProgressPhase::Syncing, .current = 1, .total = 2, .item = "core"});
  QCoreApplication::processEvents();
  EXPECT_TRUE(find("transactionMessage")->property("rawText").toString().contains(QStringLiteral("1 of 2")));
  service_.emitProgress("", ProgressUpdate{.phase = ProgressPhase::Syncing, .current = 2, .total = 2, .item = "extra"});
  QCoreApplication::processEvents();
  EXPECT_TRUE(find("transactionMessage")->property("rawText").toString().contains(QStringLiteral("2 of 2")));
  ASSERT_TRUE(visible("transactionPrepareCancelButton"));
  EXPECT_TRUE(find("transactionPrepareCancelButton")->property("enabled").toBool());
}

TEST_F(TransactionDialogUpgradeView, ThePreviewListsEveryRowInSectionsWithoutSelectionAndTheWiderDialog) {
  toUpgradePreview(500);
  auto* list = find("upgradePreviewList");
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(list->property("count").toInt(), 501);
  EXPECT_NE(find("upgradePreviewScrollBar"), nullptr);
  EXPECT_FALSE(visible("upgradeKeyringNotice"));
  EXPECT_FALSE(find("upgradeDownloadSize")->property("rawText").toString().isEmpty());
  EXPECT_FALSE(find("transactionExpires")->property("rawText").toString().isEmpty());
  EXPECT_EQ(popup()->property("preferredWidth").toInt(), 640);
  for (QQuickItem* item : allItems()) {
    EXPECT_STRNE(item->metaObject()->className(), "QQuickCheckBox");
  }
  EXPECT_TRUE(visible("transactionConfirmButton"));
}

TEST_F(TransactionDialogUpgradeView, TheKeyringNoticeShowsWhenTheFlagIsSet) {
  toUpgradePreview(1, true);
  ASSERT_TRUE(visible("upgradeKeyringNotice"));
  EXPECT_TRUE(
      find("upgradeKeyringNotice")->property("rawText").toString().contains(QStringLiteral("archlinux-keyring")));
}

TEST_F(TransactionDialogUpgradeView, TheProgressPageCountsTheStepsAndCancelEndsAtPromotion) {
  toUpgradePreview(1, true);
  controller_->confirm();
  service_.emitProgress("plan-u", ProgressUpdate{.phase = ProgressPhase::Downloading, .current = 0, .total = 2});
  QCoreApplication::processEvents();
  EXPECT_TRUE(find("transactionRunCancelButton")->property("enabled").toBool());
  service_.emitProgress("plan-u", ProgressUpdate{.phase = ProgressPhase::Promoting});
  QCoreApplication::processEvents();
  EXPECT_FALSE(find("transactionRunCancelButton")->property("enabled").toBool());
  EXPECT_EQ(controller_->handleCloseRequest(), holonight_packages_application::CloseDecision::Ignore);
  QCoreApplication::processEvents();
  EXPECT_TRUE(popup()->property("opened").toBool());
  EXPECT_TRUE(visible("transactionNotice"));
  service_.emitProgress("plan-u", ProgressUpdate{.phase = ProgressPhase::KeyringStep, .current = 1, .total = 2});
  QCoreApplication::processEvents();
  EXPECT_TRUE(find("transactionStep")->property("rawText").toString().contains(QStringLiteral("1 of 2")));
  service_.emitProgress("plan-u", ProgressUpdate{.phase = ProgressPhase::MainStep, .current = 2, .total = 2});
  QCoreApplication::processEvents();
  EXPECT_TRUE(find("transactionStep")->property("rawText").toString().contains(QStringLiteral("2 of 2")));
}

TEST_F(TransactionDialogUpgradeView, APartialUpgradeResultOffersCloseAndNamesTheStep) {
  toUpgradePreview();
  controller_->confirm();
  auto failed = FakeTransactionService::failed(TransactionErrorCode::UpgradePartiallyApplied);
  failed->failure.names = {"main"};
  service_.completeExecute(std::move(failed));
  QCoreApplication::processEvents();
  EXPECT_TRUE(visible("transactionCloseButton"));
  EXPECT_FALSE(visible("transactionTryAgainButton"));
  EXPECT_TRUE(find("transactionMessage")->property("rawText").toString().contains(QStringLiteral("Update system")));
}

// ---- system-upgrade T-070: the Updates page button, by its source ----

QString readQml(const QString& relative) {
  QFile file(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR) + relative);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text)) << relative.toStdString();
  return QString::fromUtf8(file.readAll());
}

TEST(UpdatesViewSource, UpdateSystemFollowsTheControllerAndNeverTheList) {
  const QString source = readQml(QStringLiteral("/updates/UpdatesView.qml"));
  const qsizetype start = source.indexOf(QStringLiteral("objectName: \"updateSystemButton\""));
  ASSERT_GE(start, 0);
  const qsizetype end = source.indexOf(QStringLiteral("onClicked"), start);
  const QString button = source.mid(start, end - start);
  EXPECT_TRUE(button.contains(QStringLiteral("root.transactionController.upgradeEnabled")));
  EXPECT_TRUE(button.contains(QStringLiteral("upgradeDisabledReason")));
  EXPECT_FALSE(button.contains(QStringLiteral("updatesModel")));  // enabled whatever the list shows (REQ-F-078)
  EXPECT_TRUE(source.contains(QStringLiteral("root.transactionController.requestUpgrade()")));
  EXPECT_TRUE(source.contains(QStringLiteral("root.transactionController.probeHelper()")));
}

TEST(UpdatesViewSource, TheUpgradePreviewHasSectionsAScrollBarAndNoSelectionControl) {
  const QString source = readQml(QStringLiteral("/transactions/UpgradePreviewPage.qml"));
  EXPECT_TRUE(source.contains(QStringLiteral("section.property: \"kind\"")));
  EXPECT_TRUE(source.contains(QStringLiteral("Controls.ScrollBar.vertical")));
  EXPECT_FALSE(source.contains(QStringLiteral("CheckBox")));
  EXPECT_FALSE(source.contains(QStringLiteral("ItemSelectionModel")));
  EXPECT_FALSE(source.contains(QStringLiteral("Switch")));
}

TEST(UpdatesViewSource, TheRefusalButtonStartsTheUpgradeAndNothingRetriesTheInstall) {
  const QString source = readQml(QStringLiteral("/transactions/TransactionActionRow.qml"));
  const qsizetype start = source.indexOf(QStringLiteral("transactionUpdateSystemButton"));
  ASSERT_GE(start, 0);
  const QString button = source.mid(start, source.indexOf(QLatin1Char('}'), start) - start);
  EXPECT_TRUE(button.contains(QStringLiteral("root.controller.updateSystem()")));
  EXPECT_FALSE(source.contains(QStringLiteral("requestInstall")));
}

}  // namespace
