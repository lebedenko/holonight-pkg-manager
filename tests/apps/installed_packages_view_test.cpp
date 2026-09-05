#include "InstalledPackagesFilterModel.h"
#include "InstalledPackagesModel.h"
#include "mock_package_source.h"

#include <QCoreApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QtQml/qqml.h>

#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

namespace {

using holonight_packages_application::MockPackageSource;
using holonight_packages_application::PackageListUseCase;
using holonight_packages_domain::Package;
using holonight_packages_domain::PackageSourceError;
using holonight_packages_domain::PackageSourceErrorCode;
using ::testing::Return;

std::unique_ptr<QObject> createView(QQmlEngine& engine, InstalledPackagesModel& model) {
  QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR) +
                                                       QStringLiteral("/packages/InstalledPackagesView.qml")));
  EXPECT_EQ(component.status(), QQmlComponent::Ready) << component.errorString().toStdString();
  return std::unique_ptr<QObject>(
      component.createWithInitialProperties({{QStringLiteral("installedPackagesModel"), QVariant::fromValue(&model)}}));
}

QObject* stateObject(QObject& view, const char* name) {
  auto* object = view.findChild<QObject*>(QString::fromLatin1(name));
  EXPECT_NE(object, nullptr);
  return object;
}

bool isVisible(QObject& view, const char* name) { return stateObject(view, name)->property("visible").toBool(); }

QQuickItem* findLabel(QQuickItem& item, const QString& text) {
  if (item.property("rawText").toString() == text) {
    return &item;
  }
  for (auto* child : item.childItems()) {
    if (auto* label = findLabel(*child, text)) {
      return label;
    }
  }
  return nullptr;
}

QVariantMap packageDetails(const QObject& panel) {
  const QVariant value = panel.property("currentPackage");
  return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toMap()
                                                             : value.toMap();
}

class InstalledPackagesViewTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    qmlRegisterUncreatableType<InstalledPackagesModel>("HolonightPackages", 1, 0, "InstalledPackagesModel",
                                                       QStringLiteral("Provided by the test"));
    qmlRegisterType<InstalledPackagesFilterModel>("HolonightPackages", 1, 0, "InstalledPackagesFilterModel");
  }
};

TEST_F(InstalledPackagesViewTest, ShowsLoadingState) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages()).WillOnce([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return MockPackageSource::Result(std::vector<Package>{});
  });
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QQmlEngine engine;

  auto view = createView(engine, model);

  ASSERT_NE(view, nullptr);
  EXPECT_TRUE(isVisible(*view, "loadingState"));
}

TEST_F(InstalledPackagesViewTest, ShowsLoadedEmptyStateAtNarrowWidth) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{}));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(loaded.wait(2000));
  QQmlEngine engine;

  auto view = createView(engine, model);
  ASSERT_NE(view, nullptr);
  view->setProperty("width", 20);
  QCoreApplication::processEvents();

  EXPECT_TRUE(isVisible(*view, "emptyState"));
  EXPECT_GE(stateObject(*view, "emptyState")->property("width").toReal(), 0.0);
}

TEST_F(InstalledPackagesViewTest, ShowsLoadedPackageList) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{Package{.name = "apple"}}));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(loaded.wait(2000));
  QQmlEngine engine;

  auto view = createView(engine, model);
  ASSERT_NE(view, nullptr);
  QCoreApplication::processEvents();

  EXPECT_TRUE(isVisible(*view, "packageList"));
  EXPECT_FALSE(isVisible(*view, "emptyState"));
}

TEST_F(InstalledPackagesViewTest, ShowsErrorState) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages())
      .WillOnce(Return(std::unexpected(
          PackageSourceError{.code = PackageSourceErrorCode::DatabaseOpenFailed, .message = "failed"})));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy failed(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(failed.wait(2000));
  QQmlEngine engine;

  auto view = createView(engine, model);
  ASSERT_NE(view, nullptr);
  QCoreApplication::processEvents();

  EXPECT_TRUE(isVisible(*view, "errorState"));
}

TEST_F(InstalledPackagesViewTest, DetailPanelFollowsReplacementAtSameRowAndMetadataRefresh) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages())
      .WillOnce(Return(
          std::vector<Package>{Package{.name = "apple"}, Package{.name = "foreign-tool", .installedVersion = "1"}}))
      .WillOnce(Return(std::vector<Package>{Package{.name = "foreign-tool", .installedVersion = "2"}}));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(loaded.wait(2000));
  InstalledPackagesFilterModel filter;
  filter.setSourceModel(&model);
  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR) +
                                                       QStringLiteral("/packages/PackageDetailPanel.qml")));
  std::unique_ptr<QObject> panel(
      component.createWithInitialProperties({{QStringLiteral("filterModel"), QVariant::fromValue(&filter)}}));
  ASSERT_NE(panel, nullptr) << component.errorString().toStdString();
  ASSERT_EQ(packageDetails(*panel).value(QStringLiteral("name")).toString(), QStringLiteral("apple"));

  filter.setSearchText(QStringLiteral("foreign"));
  ASSERT_EQ(filter.currentRow(), 0);
  EXPECT_EQ(packageDetails(*panel).value(QStringLiteral("name")).toString(), QStringLiteral("foreign-tool"));

  model.refresh();
  ASSERT_TRUE(loaded.wait(2000));
  EXPECT_EQ(packageDetails(*panel).value(QStringLiteral("installedVersion")).toString(), QStringLiteral("2"));

  filter.setSearchText(QStringLiteral("missing"));
  EXPECT_TRUE(isVisible(*panel, "packageDetailEmptyState"));
  EXPECT_TRUE(packageDetails(*panel).value(QStringLiteral("name")).toString().isEmpty());
}

TEST_F(InstalledPackagesViewTest, TableKeepsReadableColumnsAtDefaultAndMinimumWindowWidths) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{Package{.name = "apple"}}));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(loaded.wait(2000));
  QQmlEngine engine;
  QQuickWindow window;
  auto view = createView(engine, model);
  ASSERT_NE(view, nullptr);
  auto* item = qobject_cast<QQuickItem*>(view.get());
  ASSERT_NE(item, nullptr);
  item->setParentItem(window.contentItem());
  window.show();

  for (const QSize window_size : {QSize(1100, 720), QSize(720, 480)}) {
    window.resize(window_size);
    item->setSize(QSizeF(window_size.width() - 252, window_size.height()));
    ASSERT_TRUE(QTest::qWaitFor([&] {
      auto* label = findLabel(*item, QStringLiteral("apple"));
      return label != nullptr && label->width() > 40;
    }));
    auto* list = qobject_cast<QQuickItem*>(stateObject(*view, "packageList"));
    ASSERT_NE(list, nullptr);
    auto* row = list->property("currentItem").value<QQuickItem*>();
    ASSERT_NE(row, nullptr);
    auto* name = findLabel(*row, QStringLiteral("apple"));
    auto* reason = findLabel(*row, QStringLiteral("Explicit"));
    ASSERT_NE(name, nullptr);
    ASSERT_NE(reason, nullptr);
    EXPECT_GT(name->width(), 40);
    EXPECT_LE(reason->mapToItem(row, QPointF(reason->width(), 0)).x(), row->width());

    auto* table = qobject_cast<QQuickItem*>(stateObject(*view, "installedPackageTable"));
    ASSERT_NE(table, nullptr);
    EXPECT_GE(table->width(), 300);
    auto* flickable = table->property("contentItem").value<QQuickItem*>();
    ASSERT_NE(flickable, nullptr);
    const qreal maximum_x = table->property("contentWidth").toReal() - table->property("availableWidth").toReal();
    ASSERT_GT(maximum_x, 0);
    flickable->setProperty("contentX", maximum_x);
    auto* header_reason = findLabel(*table, QStringLiteral("Reason"));
    ASSERT_NE(header_reason, nullptr);
    EXPECT_NEAR(header_reason->mapToItem(table, QPointF()).x(), reason->mapToItem(table, QPointF()).x(), 1);
    EXPECT_GE(reason->mapToItem(table, QPointF()).x(), 0);
    EXPECT_LE(reason->mapToItem(table, QPointF(reason->width(), 0)).x(), table->width());
    flickable->setProperty("contentX", 0);
  }
}

TEST_F(InstalledPackagesViewTest, ListAndGridChoicesRemainExclusiveAndDoNotChangeTheTable) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{Package{.name = "apple"}}));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(loaded.wait(2000));
  QQmlEngine engine;
  auto view = createView(engine, model);
  ASSERT_NE(view, nullptr);
  auto* list_button = stateObject(*view, "installedListViewButton");
  auto* grid_button = stateObject(*view, "installedGridViewButton");

  ASSERT_TRUE(QMetaObject::invokeMethod(grid_button, "click"));
  EXPECT_TRUE(grid_button->property("checked").toBool());
  EXPECT_FALSE(list_button->property("checked").toBool());
  EXPECT_TRUE(isVisible(*view, "packageList"));

  ASSERT_TRUE(QMetaObject::invokeMethod(list_button, "click"));
  ASSERT_TRUE(QMetaObject::invokeMethod(list_button, "click"));
  EXPECT_TRUE(list_button->property("checked").toBool());
  EXPECT_FALSE(grid_button->property("checked").toBool());
  EXPECT_EQ(stateObject(*view, "packageList")->property("count").toInt(), 1);
}

}  // namespace
