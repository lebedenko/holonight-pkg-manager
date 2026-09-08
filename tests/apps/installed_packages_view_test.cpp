#include "InstalledPackagesFilterModel.h"
#include "InstalledPackagesModel.h"
#include "mock_package_source.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QtQml/qqml.h>

#include <chrono>
#include <future>
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

void rejectQmlWarnings(QQmlEngine& engine) {
  QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& errors) {
    for (const auto& error : errors) {
      ADD_FAILURE() << qPrintable(error.toString());
    }
  });
}

QUrl qmlSource(const QString& path) {
#ifdef HOLONIGHT_COMPILED_QML_ACCEPTANCE
  return {QStringLiteral("qrc:/HolonightPackages") + path};
#else
  return QUrl::fromLocalFile(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR) + path);
#endif
}

std::unique_ptr<QObject> createView(QQmlEngine& engine, InstalledPackagesModel& model, bool workspace = false) {
  const QUrl source = qmlSource(workspace ? QStringLiteral("/workspace/WorkspaceWindow.qml")
                                          : QStringLiteral("/packages/InstalledPackagesView.qml"));
  QQmlComponent component(&engine, source);
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

QQuickItem* buttonForLabel(QQuickItem& item, const QString& text) {
  for (auto* label = findLabel(item, text); label != nullptr; label = label->parentItem()) {
    if (label->metaObject()->indexOfSignal("clicked()") >= 0) {
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
#ifndef HOLONIGHT_COMPILED_QML_ACCEPTANCE
    qmlRegisterUncreatableType<InstalledPackagesModel>("HolonightPackages", 1, 0, "InstalledPackagesModel",
                                                       QStringLiteral("Provided by the test"));
    qmlRegisterType<InstalledPackagesFilterModel>("HolonightPackages", 1, 0, "InstalledPackagesFilterModel");
#endif
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
  rejectQmlWarnings(engine);

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
  rejectQmlWarnings(engine);

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
  rejectQmlWarnings(engine);

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
  rejectQmlWarnings(engine);

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
  rejectQmlWarnings(engine);
  QQmlComponent component(&engine, qmlSource(QStringLiteral("/packages/PackageDetailPanel.qml")));
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
  const QString long_name = QStringLiteral("package-with-an-extremely-long-name-") + QString(60, QLatin1Char('x'));
  const QString long_version(100, QLatin1Char('9'));
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages())
      .WillOnce(Return(std::vector<Package>{
          Package{.name = long_name.toStdString(),
                  .installedVersion = long_version.toStdString(),
                  .sourceType = holonight_packages_domain::SourceType::Official,
                  .repository = "extra",
                  .description = "A package description with a long unbroken word " + std::string(100, 'd'),
                  .requiredBy = {std::string(100, 'r')},
                  .optionalDependencies = {std::string(100, 'o')}}}));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(loaded.wait(2000));
  QQmlEngine engine;
  rejectQmlWarnings(engine);
  QQuickWindow window;
  auto view = createView(engine, model, true);
  ASSERT_NE(view, nullptr);
  auto* item = qobject_cast<QQuickItem*>(view.get());
  ASSERT_NE(item, nullptr);
  item->setParentItem(window.contentItem());
  window.show();

  for (const QSize window_size :
       {QSize(1360, 890), QSize(1280, 840), QSize(1100, 720), QSize(720, 480), QSize(1343, 890), QSize(1344, 890)}) {
    window.resize(window_size);
    item->setSize(QSizeF(window_size.width(), window_size.height()));
    ASSERT_TRUE(QTest::qWaitFor([&] {
      auto* label = findLabel(*item, long_name);
      return label != nullptr && label->width() > 40;
    }));
    QSignalSpy frame_ready(&window, &QQuickWindow::frameSwapped);
    window.update();
    ASSERT_TRUE(frame_ready.wait(2000));
    auto* list = qobject_cast<QQuickItem*>(stateObject(*view, "packageList"));
    ASSERT_NE(list, nullptr);
    auto* row = list->property("currentItem").value<QQuickItem*>();
    ASSERT_NE(row, nullptr);
    auto* name = findLabel(*row, long_name);
    auto* reason = findLabel(*row, QStringLiteral("Explicit"));
    ASSERT_NE(name, nullptr);
    ASSERT_NE(reason, nullptr);
    EXPECT_GT(name->width(), 40);
    EXPECT_TRUE(name->property("truncated").toBool());
    auto* version = findLabel(*row, long_version);
    ASSERT_NE(version, nullptr);
    EXPECT_TRUE(version->property("truncated").toBool());
    EXPECT_LE(reason->mapToItem(row, QPointF(reason->width(), 0)).x(), row->width());

    auto* table = qobject_cast<QQuickItem*>(stateObject(*view, "installedPackageTable"));
    ASSERT_NE(table, nullptr);
    EXPECT_GE(table->width(), 300);
    auto* flickable = table->property("contentItem").value<QQuickItem*>();
    ASSERT_NE(flickable, nullptr);
    const qreal maximum_x = table->property("contentWidth").toReal() - table->property("availableWidth").toReal();
    if (window_size.width() == 720) {
      EXPECT_GT(maximum_x, 0);
    } else {
      EXPECT_LE(maximum_x, 0);
    }
    auto* detail = qobject_cast<QQuickItem*>(stateObject(*view, "packageDetailPanel"));
    ASSERT_NE(detail, nullptr);
    if (window_size.width() >= 1344) {
      EXPECT_NEAR(table->y(), detail->y(), 1);
      EXPECT_NEAR(detail->width(), 300, 1);
    } else {
      EXPECT_GT(detail->y(), table->y());
    }
    flickable->setProperty("contentX", maximum_x);
    auto* header_reason = findLabel(*table, QStringLiteral("Reason"));
    ASSERT_NE(header_reason, nullptr);
    EXPECT_NEAR(header_reason->mapToItem(table, QPointF()).x(), reason->mapToItem(table, QPointF()).x(), 1);
    EXPECT_GE(reason->mapToItem(table, QPointF()).x(), 0);
    EXPECT_LE(reason->mapToItem(table, QPointF(reason->width(), 0)).x(), table->width());
    flickable->setProperty("contentX", 0);
    for (const auto& labels :
         {std::pair{QStringLiteral("Package"), long_name}, std::pair{QStringLiteral("Origin"), QStringLiteral("extra")},
          std::pair{QStringLiteral("Version"), long_version}}) {
      auto* header = findLabel(*table, labels.first);
      auto* value = findLabel(*row, labels.second);
      ASSERT_NE(header, nullptr);
      ASSERT_NE(value, nullptr);
      const qreal padding = labels.first == QStringLiteral("Origin") ? 6 : 0;
      EXPECT_NEAR(header->mapToItem(table, QPointF()).x() + padding, value->mapToItem(table, QPointF()).x(), 1);
    }
    auto* detail_scroll = qobject_cast<QQuickItem*>(stateObject(*view, "packageDetailScrollView"));
    ASSERT_NE(detail_scroll, nullptr);
    EXPECT_LE(detail_scroll->property("contentWidth").toReal(), detail_scroll->width());
    for (const auto& label :
         {long_name, long_version, QString(100, QLatin1Char('r')), QString(100, QLatin1Char('o'))}) {
      auto* text = findLabel(*detail, label);
      ASSERT_NE(text, nullptr);
      const QRectF bounds = text->mapRectToItem(detail, QRectF(QPointF(), text->size()));
      EXPECT_GE(bounds.left(), 0);
      EXPECT_LE(bounds.right(), detail->width());
    }
    auto* detail_flickable = detail_scroll->property("contentItem").value<QQuickItem*>();
    ASSERT_NE(detail_flickable, nullptr);
    const qreal detail_maximum_y =
        detail_scroll->property("contentHeight").toReal() - detail_scroll->property("availableHeight").toReal();
    EXPECT_GT(detail_maximum_y, 0);
    detail_flickable->setProperty("contentY", detail_maximum_y);
    auto* website = qobject_cast<QQuickItem*>(stateObject(*view, "packageDetailWebsiteLink"));
    ASSERT_NE(website, nullptr);
    EXPECT_TRUE(QRectF(QPointF(), detail_scroll->size())
                    .contains(website->mapRectToItem(detail_scroll, QRectF(QPointF(), website->size()))));
    detail_flickable->setProperty("contentY", 0);
    if (const QString directory = qEnvironmentVariable("HOLONIGHT_TEST_SCREENSHOT_DIR"); !directory.isEmpty()) {
      EXPECT_TRUE(window.grabWindow().save(
          directory + QStringLiteral("/workspace-%1x%2.png").arg(window_size.width()).arg(window_size.height())));
    }
  }
}

TEST_F(InstalledPackagesViewTest, OriginBadgesFitTheirLabelsAndPreserveFullRepositoryNames) {
  using holonight_packages_domain::SourceType;
  const QString long_repository(80, QLatin1Char('r'));
  for (const auto& repository : {QStringLiteral("core"), QStringLiteral("extra"), QStringLiteral("g14"), QString(),
                                 long_repository, QStringLiteral("foreign")}) {
    const bool foreign = repository == QStringLiteral("foreign");
    QString label = repository;
    if (foreign) {
      label = QStringLiteral("Foreign");
    } else if (repository.isEmpty()) {
      label = QStringLiteral("Official");
    }
    auto source = std::make_shared<MockPackageSource>();
    EXPECT_CALL(*source, enumerateInstalledPackages())
        .WillOnce(
            Return(std::vector<Package>{Package{.name = "apple",
                                                .sourceType = foreign ? SourceType::Foreign : SourceType::Official,
                                                .repository = repository.toStdString()}}));
    InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
    QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
    ASSERT_TRUE(loaded.wait(2000));
    QQmlEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& errors) {
      for (const auto& error : errors) {
        ADD_FAILURE() << qPrintable(error.toString());
      }
    });
    QQuickWindow window;
    auto view = createView(engine, model, true);
    ASSERT_NE(view, nullptr);
    auto* item = qobject_cast<QQuickItem*>(view.get());
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(1360, 890));
    window.resize(1360, 890);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    auto* list = stateObject(*view, "packageList");
    ASSERT_TRUE(QTest::qWaitFor([&] { return list->property("currentItem").value<QQuickItem*>() != nullptr; }));
    auto* row = list->property("currentItem").value<QQuickItem*>();
    for (auto* badge : {qobject_cast<QQuickItem*>(stateObject(*row, "packageRowOriginBadge")),
                        qobject_cast<QQuickItem*>(stateObject(*view, "packageDetailOriginBadge"))}) {
      ASSERT_NE(badge, nullptr);
      EXPECT_EQ(badge->property("text").toString(), label);
      EXPECT_EQ(badge->property("toolTipText").toString(),
                !foreign && !repository.isEmpty() ? QStringLiteral("Repository: ") + repository : label);
      auto* accessible = QAccessible::queryAccessibleInterface(badge);
      ASSERT_NE(accessible, nullptr);
      EXPECT_EQ(accessible->text(QAccessible::Name), label);
      auto* text = findLabel(*badge, label);
      ASSERT_NE(text, nullptr);
      if (repository == long_repository) {
        EXPECT_LT(badge->width(), badge->implicitWidth());
        EXPECT_TRUE(text->property("truncated").toBool());
      } else {
        EXPECT_NEAR(badge->width(), badge->implicitWidth(), 1);
        EXPECT_NEAR(badge->width(), text->implicitWidth() + 12, 1);
        EXPECT_FALSE(text->property("truncated").toBool());
      }
      EXPECT_LE(badge->width(), badge->parentItem()->width());
    }
  }
}

TEST_F(InstalledPackagesViewTest, ListAndGridChoicesRemainExclusiveAndDoNotChangeTheTable) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{Package{.name = "apple"}}));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(loaded.wait(2000));
  QQmlEngine engine;
  rejectQmlWarnings(engine);
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

TEST_F(InstalledPackagesViewTest, ToolbarAndCategoryTabsRemainReachableAtMinimumWindowSize) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{Package{.name = "apple"}}));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(loaded.wait(2000));
  QQmlEngine engine;
  rejectQmlWarnings(engine);
  QQuickWindow window;
  auto view = createView(engine, model, true);
  ASSERT_NE(view, nullptr);
  auto* item = qobject_cast<QQuickItem*>(view.get());
  ASSERT_NE(item, nullptr);
  item->setParentItem(window.contentItem());
  window.show();

  for (const QSize window_size : {QSize(720, 480), QSize(1100, 720), QSize(1280, 840), QSize(1360, 890)}) {
    window.resize(window_size);
    item->setSize(QSizeF(window_size.width(), window_size.height()));
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(QTest::qWaitFor([&] { return findLabel(*item, QStringLiteral("Orphans")) != nullptr; }));
    QSignalSpy frame(&window, &QQuickWindow::frameSwapped);
    window.update();
    ASSERT_TRUE(frame.wait(2000));
    const QRectF page_bounds(QPointF(), item->size());
    QSignalSpy frame_ready(&window, &QQuickWindow::frameSwapped);
    window.update();
    ASSERT_TRUE(frame_ready.wait(2000));
    auto* list = qobject_cast<QQuickItem*>(stateObject(*view, "packageList"));
    ASSERT_NE(list, nullptr);
    EXPECT_GE(list->height(), 64);
    for (const char* name :
         {"installedSearchField", "installedSortComboBox", "installedListViewButton", "installedGridViewButton",
          "installedOverflowButton", "installedRepositoryComboBox", "installedAllStatesComboBox"}) {
      auto* control = qobject_cast<QQuickItem*>(stateObject(*view, name));
      ASSERT_NE(control, nullptr);
      EXPECT_TRUE(page_bounds.contains(control->mapRectToItem(item, QRectF(QPointF(), control->size())))) << name;
    }
    auto* search = qobject_cast<QQuickItem*>(stateObject(*view, "installedSearchField"));
    ASSERT_NE(search, nullptr);
    if (window_size.width() == 720) {
      EXPECT_NEAR(search->width(), window_size.width() - 220 - 32, 1);
    }
    for (const char* label : {"Explicit", "Dependencies", "AUR / Foreign", "Orphans"}) {
      auto* control = buttonForLabel(*item, QString::fromUtf8(label));
      ASSERT_NE(control, nullptr) << label;
      EXPECT_TRUE(page_bounds.contains(control->mapRectToItem(item, QRectF(QPointF(), control->size())))) << label;
    }
    auto* page_scroll = qobject_cast<QQuickItem*>(stateObject(*view, "installedPageScrollView"));
    ASSERT_NE(page_scroll, nullptr);
    auto* flickable = page_scroll->property("contentItem").value<QQuickItem*>();
    ASSERT_NE(flickable, nullptr);
    const qreal maximum_y =
        page_scroll->property("contentHeight").toReal() - page_scroll->property("availableHeight").toReal();
    if (window_size.height() == 480) {
      EXPECT_GT(maximum_y, 0);
    }
    flickable->setProperty("contentY", maximum_y);
    auto* review_button = qobject_cast<QQuickItem*>(stateObject(*view, "orphanFooterReviewButton"));
    ASSERT_NE(review_button, nullptr);
    EXPECT_TRUE(page_bounds.contains(review_button->mapRectToItem(item, QRectF(QPointF(), review_button->size()))));
    flickable->setProperty("contentY", 0);
  }
}

TEST_F(InstalledPackagesViewTest, PageLayoutRemainsFreeOfBindingLoopsDuringLoadAndResize) {
  std::promise<std::vector<Package>> enumeration;
  const auto result = enumeration.get_future().share();
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages()).WillOnce([result] {
    return MockPackageSource::Result(result.get());
  });
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  QStringList warnings;
  QQmlEngine engine;
  rejectQmlWarnings(engine);
  QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&warnings](const QList<QQmlError>& errors) {
    for (const auto& error : errors) {
      warnings.append(error.toString());
    }
  });
  QQuickWindow window;
  auto view = createView(engine, model, true);
  ASSERT_NE(view, nullptr);
  auto* item = qobject_cast<QQuickItem*>(view.get());
  ASSERT_NE(item, nullptr);
  item->setParentItem(window.contentItem());
  item->setSize(QSizeF(848, 720));
  window.resize(1100, 720);
  window.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
  enumeration.set_value({Package{.name = "apple"}});
  ASSERT_TRUE(loaded.wait(2000));

  for (const QSize window_size : {QSize(1100, 720), QSize(720, 480), QSize(1050, 600), QSize(1100, 720)}) {
    window.resize(window_size);
    item->setSize(QSizeF(window_size.width(), window_size.height()));
    QSignalSpy frame(&window, &QQuickWindow::frameSwapped);
    window.update();
    ASSERT_TRUE(frame.wait(2000));
  }
  EXPECT_TRUE(warnings.isEmpty()) << warnings.join(QLatin1Char('\n')).toStdString();
}

TEST_F(InstalledPackagesViewTest, ArrowKeysKeepHighlightedRowAndPackageDetailsInSync) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages())
      .WillOnce(Return(std::vector<Package>{Package{.name = "apple"}, Package{.name = "banana"}}));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(loaded.wait(2000));
  QQmlEngine engine;
  rejectQmlWarnings(engine);
  QQuickWindow window;
  auto view = createView(engine, model);
  ASSERT_NE(view, nullptr);
  auto* item = qobject_cast<QQuickItem*>(view.get());
  ASSERT_NE(item, nullptr);
  item->setParentItem(window.contentItem());
  item->setSize(QSizeF(848, 720));
  window.resize(1100, 720);
  window.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
  auto* list = qobject_cast<QQuickItem*>(stateObject(*view, "packageList"));
  ASSERT_NE(list, nullptr);
  auto* filter = view->findChild<InstalledPackagesFilterModel*>();
  ASSERT_NE(filter, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return list->property("currentItem").value<QQuickItem*>() != nullptr; }));
  list->property("currentItem").value<QQuickItem*>()->forceActiveFocus();

  QTest::keyClick(&window, Qt::Key_Down);
  EXPECT_EQ(list->property("currentIndex").toInt(), 1);
  EXPECT_EQ(filter->currentPackage().value(QStringLiteral("name")).toString(), QStringLiteral("banana"));
  QTest::keyClick(&window, Qt::Key_Down);
  EXPECT_EQ(filter->currentRow(), 1);
  QTest::keyClick(&window, Qt::Key_Up);
  EXPECT_EQ(list->property("currentIndex").toInt(), 0);
  EXPECT_EQ(filter->currentPackage().value(QStringLiteral("name")).toString(), QStringLiteral("apple"));
  QTest::keyClick(&window, Qt::Key_Up);
  EXPECT_EQ(filter->currentRow(), 0);

  filter->setSearchText(QStringLiteral("banana"));
  EXPECT_EQ(list->property("currentIndex").toInt(), 0);
  EXPECT_EQ(filter->currentPackage().value(QStringLiteral("name")).toString(), QStringLiteral("banana"));
  filter->setSearchText(QStringLiteral("missing"));
  QTest::keyClick(&window, Qt::Key_Down);
  EXPECT_EQ(filter->currentRow(), -1);
}

TEST_F(InstalledPackagesViewTest, CategoryTabsExposeTheirVisibleAccessibleNames) {
  auto source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{Package{.name = "apple"}}));
  InstalledPackagesModel model(std::make_shared<PackageListUseCase>(source));
  QSignalSpy loaded(&model, &InstalledPackagesModel::statusChanged);
  ASSERT_TRUE(loaded.wait(2000));
  QQmlEngine engine;
  rejectQmlWarnings(engine);
  auto view = createView(engine, model);
  ASSERT_NE(view, nullptr);
  auto* item = qobject_cast<QQuickItem*>(view.get());
  ASSERT_NE(item, nullptr);

  for (const char* label : {"Explicit", "Dependencies", "AUR / Foreign", "Orphans"}) {
    auto* control = buttonForLabel(*item, QString::fromUtf8(label));
    ASSERT_NE(control, nullptr) << label;
    auto* accessible = QAccessible::queryAccessibleInterface(control);
    ASSERT_NE(accessible, nullptr);
    EXPECT_EQ(accessible->text(QAccessible::Name), QString::fromUtf8(label));
  }
}

TEST_F(InstalledPackagesViewTest, OptionalDependenciesCanBeExpandedFromTheKeyboard) {
  QQmlEngine engine;
  rejectQmlWarnings(engine);
  QQmlComponent component(&engine, qmlSource(QStringLiteral("/packages/PackageDetailDependencySections.qml")));
  std::unique_ptr<QObject> section(component.createWithInitialProperties(
      {{QStringLiteral("description"), QString()},
       {QStringLiteral("requiredByCount"), 0},
       {QStringLiteral("requiredByList"), QStringList()},
       {QStringLiteral("optionalDependencies"), QStringList{"one", "two", "three", "four", "five", "six"}},
       {QStringLiteral("configFileCount"), 0}}));
  ASSERT_NE(section, nullptr) << component.errorString().toStdString();
  QQuickWindow window;
  auto* item = qobject_cast<QQuickItem*>(section.get());
  ASSERT_NE(item, nullptr);
  item->setParentItem(window.contentItem());
  item->setWidth(380);
  window.resize(380, 600);
  window.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
  EXPECT_EQ(findLabel(*item, QStringLiteral("six")), nullptr);
  auto* button = buttonForLabel(*item, QStringLiteral("+1 more"));
  ASSERT_NE(button, nullptr);
  EXPECT_TRUE(button->activeFocusOnTab());
  auto* accessible = QAccessible::queryAccessibleInterface(button);
  ASSERT_NE(accessible, nullptr);
  EXPECT_EQ(accessible->text(QAccessible::Name), QStringLiteral("+1 more"));
  item->forceActiveFocus();
  QTest::keyClick(&window, Qt::Key_Tab);
  EXPECT_TRUE(button->hasActiveFocus());
  QTest::keyClick(&window, Qt::Key_Space);
  EXPECT_NE(findLabel(*item, QStringLiteral("six")), nullptr);
  EXPECT_FALSE(button->isVisible());
}

}  // namespace
