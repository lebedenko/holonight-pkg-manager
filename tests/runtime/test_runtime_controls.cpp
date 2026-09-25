#include "ExploreModel.h"
#include "InstalledPackagesFilterModel.h"
#include "InstalledPackagesModel.h"
#include "UpdatesModel.h"
#include "fake_explore_source.h"
#include "fake_update_source.h"
#include "mock_package_source.h"

#include <QColor>
#include <QFile>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>
#include <QtQml/private/qqmlcontextdata_p.h>
#include <QtQml/private/qqmldata_p.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {
using holonight_packages_application::MockPackageSource;
using holonight_packages_application::PackageListUseCase;
using holonight_packages_domain::InstallReason;
using holonight_packages_domain::Package;
using holonight_packages_domain::SourceType;
using testing::Return;

std::vector<Package> manyPackages() {
  std::vector<Package> packages;
  for (int index = 0; index < 80; ++index) {
    Package package;
    package.name = QStringLiteral("package-%1").arg(index, 3, 10, QLatin1Char('0')).toStdString();
    package.installedVersion = std::string(100, '9');
    package.repository = QStringLiteral("repository-%1").arg(index % 40, 2, 10, QLatin1Char('0')).toStdString();
    package.sourceType = SourceType::Official;
    package.sizeBytes = static_cast<std::uint64_t>(index + 1) * 1024;
    package.description = std::string(300, 'd');
    package.requiredBy = {std::string(100, 'r')};
    package.optionalDependencies = {"one", "two", "three", "four", "five", "six", "seven", "eight"};
    packages.push_back(std::move(package));
  }
  Package orphan;
  orphan.name = "orphan";
  orphan.installReason = InstallReason::Dependency;
  orphan.sizeBytes = 4096;
  packages.push_back(orphan);
  return packages;
}

QQuickItem* labelForText(QQuickItem& item, const QString& text) {
  if (item.property("rawText").toString() == text) {
    return &item;
  }
  for (auto* child : item.childItems()) {
    if (auto* found = labelForText(*child, text)) {
      return found;
    }
  }
  return nullptr;
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

QObject* buttonForText(QQuickItem& item, const QString& text) {
  for (auto* label = labelForText(item, text); label != nullptr; label = label->parentItem()) {
    if (label->metaObject()->indexOfSignal("clicked()") >= 0) {
      return label;
    }
  }
  return nullptr;
}

bool hasOrigin(QObject* object, const QString& suffix) {
  if (object == nullptr) {
    return false;
  }
  auto* data = QQmlData::get(object);
  for (auto* context = data != nullptr ? data->context : nullptr; context != nullptr;
       context = context->parent().data()) {
    if (context->url().toString().endsWith(suffix)) {
      qInfo().noquote() << "CONTROL_IMPLEMENTATION" << context->url().toString();
      return true;
    }
  }
  return false;
}

class RuntimeControls : public testing::Test {
 protected:
  void SetUp() override {
    source_ = std::make_shared<MockPackageSource>();
    EXPECT_CALL(*source_, enumerateInstalledPackages()).WillOnce(Return(manyPackages()));
    model_ = std::make_unique<InstalledPackagesModel>(std::make_shared<PackageListUseCase>(source_));
    QSignalSpy loaded(model_.get(), &InstalledPackagesModel::statusChanged);
    ASSERT_TRUE(loaded.wait(2000));
    view_.engine()->addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
    QObject::connect(view_.engine(), &QQmlEngine::warnings, view_.engine(), [this](const QList<QQmlError>& errors) {
      for (const auto& error : errors) {
        diagnostics_.append(error.toString());
      }
    });
    view_.setMinimumSize(QSize(720, 480));
    view_.resize(1360, 890);
    view_.setResizeMode(QQuickView::SizeRootObjectToView);
    updates_model_ = std::make_unique<UpdatesModel>(std::make_shared<holonight_packages_testing::FakeUpdateSource>());
    ASSERT_TRUE(QTest::qWaitFor([this] { return !updates_model_->loading(); }, 2000));
    explore_model_ = std::make_unique<ExploreModel>(std::make_shared<holonight_packages_testing::FakeExploreSource>());
    ASSERT_TRUE(QTest::qWaitFor([this] { return !explore_model_->loading(); }, 2000));
    view_.setInitialProperties({{QStringLiteral("installedPackagesModel"), QVariant::fromValue(model_.get())},
                                {QStringLiteral("updatesModel"), QVariant::fromValue(updates_model_.get())},
                                {QStringLiteral("exploreModel"), QVariant::fromValue(explore_model_.get())}});
    view_.setSource(QUrl(QStringLiteral("qrc:/HolonightPackages/workspace/WorkspaceWindow.qml")));
    ASSERT_EQ(view_.status(), QQuickView::Ready);
    view_.show();
    QCoreApplication::processEvents();
    ASSERT_NE(view_.rootObject(), nullptr);
    filter_ = view_.rootObject()->findChild<InstalledPackagesFilterModel*>();
    ASSERT_NE(filter_, nullptr);
  }

  void TearDown() override { EXPECT_TRUE(diagnostics_.isEmpty()) << qPrintable(diagnostics_.join('\n')); }

  QObject* object(const char* name) {
    auto* result = view_.rootObject()->findChild<QObject*>(QString::fromLatin1(name));
    if (result == nullptr) {
      result = visualObject(*view_.rootObject(), QString::fromLatin1(name));
    }
    EXPECT_NE(result, nullptr) << name;
    return result;
  }

  void activate(const char* name, int index) {
    auto* combo = object(name);
    ASSERT_NE(combo, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index)));
    QCoreApplication::processEvents();
  }

  QQuickView* view() { return &view_; }
  InstalledPackagesModel* model() { return model_.get(); }
  InstalledPackagesFilterModel* filter() { return filter_; }

 private:
  // The view must be destroyed before its model and diagnostics callback storage.
  std::shared_ptr<MockPackageSource> source_;
  std::unique_ptr<InstalledPackagesModel> model_;
  std::unique_ptr<UpdatesModel> updates_model_;
  std::unique_ptr<ExploreModel> explore_model_;
  QStringList diagnostics_;
  QQuickView view_;
  InstalledPackagesFilterModel* filter_ = nullptr;
};

TEST_F(RuntimeControls, ControlsUseSelectedImplementationsAndPreserveCoreComposites) {
  const auto style = qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE");
  const auto prefix =
      style == QStringLiteral("Fusion") ? QStringLiteral("/QtQuick/Controls/Fusion/") : QStringLiteral("/Holonight/");
  for (const auto& control :
       {std::pair{"installedSortComboBox", "ComboBox"}, std::pair{"orphanFooterReviewButton", "Button"},
        std::pair{"packageRowCheckBox", "CheckBox"}, std::pair{"installedPackageTable", "ScrollView"}}) {
    EXPECT_TRUE(hasOrigin(object(control.first), prefix + QString::fromLatin1(control.second) + ".qml"));
  }
  EXPECT_TRUE(hasOrigin(object("installedSearchField"), QStringLiteral("/Holonight/Controls/HnSearchField.qml")));
  EXPECT_TRUE(hasOrigin(object("orphanFooterSummaryLabel"), QStringLiteral("/Holonight/Core/HnLabel.qml")));
  QQmlExpression palette(qmlContext(view()->rootObject()), view()->rootObject(),
                         QStringLiteral("HoloniightPalette.background"));
  EXPECT_EQ(view()->rootObject()->property("color").value<QColor>(), palette.evaluate().value<QColor>());
  EXPECT_FALSE(palette.hasError());
  EXPECT_TRUE(hasOrigin(object("installedSearchField"), prefix + "TextField.qml"));
  int scroll_bars = 0;
  for (auto* child : object("installedPackageTable")->findChildren<QObject*>()) {
    if (child->property("orientation").isValid() && child->property("policy").isValid() &&
        child->property("size").isValid()) {
      EXPECT_TRUE(hasOrigin(child, prefix + "ScrollBar.qml"));
      ++scroll_bars;
    }
  }
  EXPECT_GT(scroll_bars, 0);
  QFile maps(QStringLiteral("/proc/self/maps"));
  ASSERT_TRUE(maps.open(QIODevice::ReadOnly));
  const auto libraries = maps.readAll();
  const auto qml = QByteArray(HOLONIGHT_QML_IMPORT_PATH) + "/Holonight/";
  for (const auto* module : {"Core/libholonight_core_qml.so", "Controls/libholonight_controls_qml.so"}) {
    EXPECT_TRUE(libraries.contains(qml + module));
  }
  EXPECT_EQ(libraries.contains(qml + "libholonight_qml.so"), style == QStringLiteral("Holonight"));
  qInfo() << "PLUGIN_ROOT" << HOLONIGHT_QML_IMPORT_PATH;
}

TEST_F(RuntimeControls, FiltersSortingSelectionAndOrphanSummaryFollowUiSignals) {
  ASSERT_EQ(filter()->rowCount(), 80);
  auto* search = object("installedSearchField");
  ASSERT_NE(search, nullptr);
  search->setProperty("text", "package-003");
  QTRY_COMPARE(filter()->rowCount(), 1);
  EXPECT_EQ(filter()->currentPackage().value("name").toString(), QStringLiteral("package-003"));
  search->setProperty("text", "");
  QTRY_COMPARE(filter()->rowCount(), 80);
  activate("installedSortComboBox", 2);
  EXPECT_EQ(filter()->sortField(), InstalledPackagesFilterModel::SortField::Size);
  EXPECT_TRUE(filter()->sortDescending());
  EXPECT_EQ(filter()->get(0).value("name").toString(), QStringLiteral("package-079"));
  EXPECT_EQ(filter()->currentPackage().value("name").toString(), QStringLiteral("package-003"));
  activate("installedRepositoryComboBox", 1);
  EXPECT_EQ(filter()->repositoryFilter(), QStringLiteral("repository-00"));
  EXPECT_EQ(filter()->rowCount(), 2);
  EXPECT_EQ(filter()->currentPackage().value("name").toString(), QStringLiteral("package-040"));
  activate("installedRepositoryComboBox", 0);
  for (const auto& category :
       {QStringLiteral("Dependencies"), QStringLiteral("AUR / Foreign"), QStringLiteral("Orphans")}) {
    auto* button = buttonForText(*view()->rootObject(), category);
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
    QTRY_COMPARE(filter()->rowCount(), 1);
    EXPECT_EQ(filter()->currentPackage().value("name").toString(), QStringLiteral("orphan"));
  }
  EXPECT_EQ(model()->orphanPackageCount(), 1);
  EXPECT_EQ(model()->reclaimableSizeBytes(), 4096);
  EXPECT_TRUE(object("orphanFooterSummaryLabel")->property("rawText").toString().contains("1 orphaned packages"));
  search->setProperty("text", "missing");
  QTRY_COMPARE(filter()->currentRow(), -1);
  EXPECT_TRUE(object("packageDetailEmptyState")->property("visible").toBool());
}

TEST_F(RuntimeControls, RepositoryPopupOverflowsAndSelectionRemainsReachable) {
  view()->resize(720, 480);
  auto* combo = object("installedRepositoryComboBox");
  ASSERT_NE(combo, nullptr);
  auto* popup = combo->property("popup").value<QObject*>();
  ASSERT_NE(popup, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(popup, "open"));
  QTRY_VERIFY(popup->property("visible").toBool());
  auto* list = popup->property("contentItem").value<QQuickItem*>();
  ASSERT_NE(list, nullptr);
  QTRY_VERIFY(list->property("contentHeight").toReal() > list->height());
  EXPECT_GT(list->height(), 0);
  EXPECT_LE(popup->property("height").toReal(), view()->height());
  activate("installedRepositoryComboBox", 40);
  QTRY_COMPARE(combo->property("currentIndex").toInt(), 40);
  EXPECT_EQ(filter()->repositoryFilter(), QStringLiteral("repository-39"));
  ASSERT_TRUE(QMetaObject::invokeMethod(popup, "close"));
  ASSERT_TRUE(QMetaObject::invokeMethod(popup, "open"));
  QTRY_VERIFY(list->property("contentY").toReal() > 0);
  ASSERT_TRUE(QMetaObject::invokeMethod(popup, "close"));
}

TEST_F(RuntimeControls, ResponsiveBreakpointsKeepToolbarAndDetailComposition) {
  QObject* toolbar = object("installedSortComboBox");
  while (toolbar != nullptr && !toolbar->property("sortOptions").isValid()) {
    toolbar = toolbar->parent();
  }
  ASSERT_NE(toolbar, nullptr);
  for (const int width : {851, 852, 1251, 1252, 1343, 1344}) {
    view()->resize(width, 890);
    QCoreApplication::processEvents();
    QTRY_COMPARE(toolbar->property("narrow").toBool(), width < 852);
    QTRY_COMPARE(toolbar->property("compact").toBool(), width < 1252);
    QTRY_COMPARE(object("installedPackageLayout")->property("columns").toInt(), width < 1344 ? 1 : 2);
  }
}

TEST_F(RuntimeControls, TableListDetailAndPageScrollIndependentlyToTheirEnds) {
  EXPECT_EQ(view()->size(), QSize(1360, 890));
  EXPECT_EQ(view()->minimumSize(), QSize(720, 480));
  view()->resize(720, 480);
  QCoreApplication::processEvents();
  auto* table = object("installedPackageTable");
  auto* list = object("packageList");
  auto* detail = object("packageDetailScrollView");
  auto* page = object("installedPageScrollView");
  ASSERT_NE(table, nullptr);
  ASSERT_NE(list, nullptr);
  ASSERT_NE(detail, nullptr);
  ASSERT_NE(page, nullptr);
  auto* table_flick = table->property("contentItem").value<QQuickItem*>();
  auto* detail_flick = detail->property("contentItem").value<QQuickItem*>();
  auto* page_flick = page->property("contentItem").value<QQuickItem*>();
  ASSERT_NE(table_flick, nullptr);
  ASSERT_NE(detail_flick, nullptr);
  ASSERT_NE(page_flick, nullptr);
  QTRY_VERIFY(table->property("contentWidth").toReal() > table->property("availableWidth").toReal());
  const qreal maximum_x = table->property("contentWidth").toReal() - table->property("availableWidth").toReal();
  table_flick->setProperty("contentX", maximum_x);
  EXPECT_DOUBLE_EQ(table_flick->property("contentX").toReal(), maximum_x);
  EXPECT_EQ(list->property("contentY").toReal(), 0);
  EXPECT_EQ(detail_flick->property("contentY").toReal(), 0);
  EXPECT_EQ(page_flick->property("contentY").toReal(), 0);
  ASSERT_TRUE(QMetaObject::invokeMethod(list, "positionViewAtEnd"));
  QTRY_VERIFY(list->property("contentY").toReal() > 0);
  QTRY_VERIFY(list->property("atYEnd").toBool());
  QTRY_VERIFY(labelForText(*view()->rootObject(), QStringLiteral("package-079")) != nullptr);
  const qreal list_y = list->property("contentY").toReal();
  EXPECT_DOUBLE_EQ(table_flick->property("contentX").toReal(), maximum_x);
  EXPECT_EQ(detail_flick->property("contentY").toReal(), 0);
  auto* more = buttonForText(*view()->rootObject(), QStringLiteral("+3 more"));
  ASSERT_NE(more, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(more, "clicked"));
  QTRY_VERIFY(labelForText(*view()->rootObject(), QStringLiteral("eight")) != nullptr);
  QTRY_VERIFY(detail->property("contentHeight").toReal() > detail->property("availableHeight").toReal());
  detail_flick->setProperty("contentY",
                            detail->property("contentHeight").toReal() - detail->property("availableHeight").toReal());
  EXPECT_DOUBLE_EQ(list->property("contentY").toReal(), list_y);
  EXPECT_EQ(page_flick->property("contentY").toReal(), 0);
  const qreal detail_y = detail_flick->property("contentY").toReal();
  EXPECT_TRUE(detail_flick->property("atYEnd").toBool());
  QTRY_VERIFY(page->property("contentHeight").toReal() > page->property("availableHeight").toReal());
  page_flick->setProperty("contentY",
                          page->property("contentHeight").toReal() - page->property("availableHeight").toReal());
  EXPECT_DOUBLE_EQ(list->property("contentY").toReal(), list_y);
  EXPECT_DOUBLE_EQ(table_flick->property("contentX").toReal(), maximum_x);
  EXPECT_DOUBLE_EQ(detail_flick->property("contentY").toReal(), detail_y);
  EXPECT_TRUE(page_flick->property("atYEnd").toBool());
  auto* footer = qobject_cast<QQuickItem*>(object("orphanFooterReviewButton"));
  ASSERT_NE(footer, nullptr);
  const auto bounds = footer->mapRectToItem(page_flick, QRectF(QPointF(), footer->size()));
  EXPECT_LE(bounds.bottom(), page_flick->height() + 1);
  EXPECT_GE(bounds.top(), 0);
}
}  // namespace
