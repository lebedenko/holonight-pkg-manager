// The Install and Remove controls of the shared detail header (qml/packages/PackageDetailHeader.qml). The tests drive
// the controller and emit the button's clicked signal; they simulate no pointer or focus input (REQ-NF-004).
#include "TransactionController.h"
#include "fake_package_list_refresh.h"
#include "fake_transaction_service.h"

#include <QCoreApplication>
#include <QMetaObject>
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

using holonight_packages_testing::FakePackageListRefresh;
using holonight_packages_testing::FakeTransactionService;

QUrl qmlSource(const QString& path) { return QUrl::fromLocalFile(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR) + path); }

class TransactionButtons : public testing::Test {
 public:
  void SetUp() override {
    QObject::connect(&engine_, &QQmlEngine::warnings, &engine_, [](const QList<QQmlError>& errors) {
      for (const auto& error : errors) {
        ADD_FAILURE() << qPrintable(error.toString());
      }
    });
    controller_ = std::make_unique<TransactionController>(service_, refresh_);
  }
  void TearDown() override { header_.reset(); }

  void createHeader(const QString& kind, bool with_controller, bool more_options) {
    QQmlComponent component(&engine_, qmlSource(QStringLiteral("/packages/PackageDetailHeader.qml")));
    ASSERT_EQ(component.status(), QQmlComponent::Ready) << component.errorString().toStdString();
    QVariantMap properties{
        {QStringLiteral("name"), QStringLiteral("vim")},
        {QStringLiteral("sourceLabel"), QStringLiteral("repository")},
        {QStringLiteral("repository"), QStringLiteral("extra")},
        {QStringLiteral("actionKind"), kind},
        {QStringLiteral("showMoreOptions"), more_options},
    };
    if (with_controller) {
      properties.insert(QStringLiteral("transactionController"), QVariant::fromValue(controller_.get()));
    }
    header_.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties(properties)));
    ASSERT_NE(header_, nullptr);
    header_->setParentItem(window_.contentItem());
    window_.resize(400, 300);
    header_->setSize(QSizeF(360, 200));
    window_.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window_));
    QCoreApplication::processEvents();
  }

  [[nodiscard]] QObject* find(const char* name) const {
    std::vector<QQuickItem*> pending{header_.get()};
    while (!pending.empty()) {
      QQuickItem* item = pending.back();
      pending.pop_back();
      if (item->objectName() == QLatin1String(name)) {
        return item;
      }
      for (QQuickItem* child : item->childItems()) {
        pending.push_back(child);
      }
    }
    return nullptr;
  }
  [[nodiscard]] bool enabled(const char* name) const {
    const QObject* button = find(name);
    return button != nullptr && button->property("enabled").toBool();
  }

  QQmlEngine engine_;
  FakeTransactionService service_;
  FakePackageListRefresh refresh_;
  std::unique_ptr<TransactionController> controller_;
  QQuickWindow window_;
  std::unique_ptr<QQuickItem> header_;
};

TEST_F(TransactionButtons, TheInstalledPageHasOnlyAnEnabledRemoveControl) {
  createHeader(QStringLiteral("remove"), true, true);
  EXPECT_TRUE(enabled("packageDetailRemoveButton"));
  EXPECT_EQ(find("packageDetailInstallButton"), nullptr);
  EXPECT_NE(find("packageDetailMoreOptionsButton"), nullptr);
}

TEST_F(TransactionButtons, TheExplorePageHasOnlyAnEnabledInstallControl) {
  createHeader(QStringLiteral("install"), true, false);
  EXPECT_TRUE(enabled("packageDetailInstallButton"));
  EXPECT_EQ(find("packageDetailRemoveButton"), nullptr);
  EXPECT_EQ(find("packageDetailMoreOptionsButton"), nullptr);
}

TEST_F(TransactionButtons, WithoutAControllerTheControlIsDisabledWithTheNotImplementedTooltip) {
  createHeader(QStringLiteral("remove"), false, true);
  EXPECT_FALSE(enabled("packageDetailRemoveButton"));
  EXPECT_EQ(header_->property("actionToolTip").toString(), QStringLiteral("Not implemented yet"));
  header_.reset();
  createHeader(QStringLiteral("install"), false, false);
  EXPECT_FALSE(enabled("packageDetailInstallButton"));
}

TEST_F(TransactionButtons, ActivatingRemoveRequestsARemovePlanForTheShownPackage) {
  createHeader(QStringLiteral("remove"), true, true);
  QMetaObject::invokeMethod(find("packageDetailRemoveButton"), "clicked");
  ASSERT_EQ(service_.prepareCalls(), 1U);
  EXPECT_EQ(service_.calls().front().request.action, holonight_packages_domain::PlanAction::Remove);
  EXPECT_EQ(service_.calls().front().request.names, std::vector<std::string>{"vim"});
}

TEST_F(TransactionButtons, ActivatingInstallRequestsAnInstallPlanForTheShownPackage) {
  createHeader(QStringLiteral("install"), true, false);
  QMetaObject::invokeMethod(find("packageDetailInstallButton"), "clicked");
  ASSERT_EQ(service_.prepareCalls(), 1U);
  EXPECT_EQ(service_.calls().front().request.action, holonight_packages_domain::PlanAction::Install);
}

TEST_F(TransactionButtons, TheControlIsDisabledWhileAnotherRequestIsInFlight) {
  createHeader(QStringLiteral("install"), true, false);
  ASSERT_TRUE(controller_->requestInstall(QStringLiteral("other")));
  QCoreApplication::processEvents();
  EXPECT_FALSE(enabled("packageDetailInstallButton"));
  EXPECT_EQ(header_->property("actionToolTip").toString(), QStringLiteral("Another package operation is in progress"));
  service_.completePrepare(
      FakeTransactionService::refusal(holonight_packages_domain::TransactionErrorCode::NothingToDo));
  controller_->dismiss();
  QCoreApplication::processEvents();
  EXPECT_TRUE(enabled("packageDetailInstallButton"));
}

}  // namespace
