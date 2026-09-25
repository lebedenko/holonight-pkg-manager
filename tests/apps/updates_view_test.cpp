#include "ExploreModel.h"
#include "InstalledPackagesFilterModel.h"
#include "InstalledPackagesModel.h"
#include "UpdatesModel.h"
#include "fake_explore_source.h"
#include "fake_update_source.h"
#include "mock_package_source.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QtQml/qqml.h>

#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace {

using holonight_packages_application::MockPackageSource;
using holonight_packages_application::PackageListUseCase;
using holonight_packages_domain::Package;
using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_domain::UpdateSourceError;
using holonight_packages_domain::UpdateSourceErrorCode;
using holonight_packages_testing::FakeUpdateSource;
using ::testing::Return;

const auto kNow = std::chrono::system_clock::now();

void rejectQmlWarnings(QQmlEngine& engine) {
  QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& errors) {
    for (const auto& error : errors) {
      ADD_FAILURE() << qPrintable(error.toString());
    }
  });
}

QUrl qmlSource(const QString& path) { return QUrl::fromLocalFile(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR) + path); }

PendingUpdate makeUpdate(std::string name, std::uint64_t download_bytes, bool ignored) {
  return PendingUpdate{.name = std::move(name),
                       .installedVersion = "1.0-1",
                       .availableVersion = "2.0-1",
                       .repository = "extra",
                       .downloadSizeBytes = download_bytes,
                       .installedSizeDeltaBytes = -1024,
                       .ignored = ignored};
}

UpdateSnapshot snapshotWith(std::vector<PendingUpdate> updates, std::chrono::system_clock::time_point data_as_of) {
  return UpdateSnapshot{.updates = std::move(updates), .databasesFound = true, .dataAsOf = data_as_of};
}

class UpdatesViewTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    qmlRegisterUncreatableType<InstalledPackagesModel>("HolonightPackages", 1, 0, "InstalledPackagesModel",
                                                       QStringLiteral("Provided by the test"));
    qmlRegisterType<InstalledPackagesFilterModel>("HolonightPackages", 1, 0, "InstalledPackagesFilterModel");
    qmlRegisterUncreatableType<UpdatesModel>("HolonightPackages", 1, 0, "UpdatesModel",
                                             QStringLiteral("Provided by the test"));
    qmlRegisterUncreatableType<ExploreModel>("HolonightPackages", 1, 0, "ExploreModel",
                                             QStringLiteral("Provided by the test"));
  }

  void SetUp() override { rejectQmlWarnings(engine_); }

  // Starts the model; waits for the first load unless the source blocks.
  UpdatesModel& startModel(bool wait = true) {
    model_ = std::make_unique<UpdatesModel>(source_);
    if (wait) {
      EXPECT_TRUE(waitUntilIdle());
    }
    return *model_;
  }

  [[nodiscard]] bool waitUntilIdle() const {
    return QTest::qWaitFor([this] { return !model_->loading(); }, 2000);
  }

  QQuickItem& createView() {
    QQmlComponent component(&engine_, qmlSource(QStringLiteral("/updates/UpdatesView.qml")));
    EXPECT_EQ(component.status(), QQmlComponent::Ready) << component.errorString().toStdString();
    view_.reset(qobject_cast<QQuickItem*>(
        component.createWithInitialProperties({{QStringLiteral("updatesModel"), QVariant::fromValue(model_.get())}})));
    EXPECT_NE(view_, nullptr);
    // Layouts and ListView delegates are only polished inside a window.
    view_->setParentItem(window_.contentItem());
    window_.resize(1200, 800);
    view_->setSize(QSizeF(1200, 800));
    window_.show();
    EXPECT_TRUE(QTest::qWaitForWindowExposed(&window_));
    QCoreApplication::processEvents();
    return *view_;
  }

  QObject* child(const char* name) const {
    auto* object = view_->findChild<QObject*>(QString::fromLatin1(name));
    EXPECT_NE(object, nullptr) << name;
    return object;
  }

  [[nodiscard]] bool isVisible(const char* name) const { return child(name)->property("visible").toBool(); }

  // Delegate items of the updates list, in model order.
  [[nodiscard]] std::vector<QQuickItem*> rows() const {
    auto* list = child("updatesList");
    std::vector<QQuickItem*> result;
    const int count = list->property("count").toInt();
    for (int index = 0; index < count; ++index) {
      QQuickItem* row = nullptr;
      EXPECT_TRUE(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(int, index)));
      result.push_back(row);
    }
    return result;
  }

  // Hosts the workspace (sidebar + both pages) instead of the bare Updates page; not placed in a window.
  void createWorkspace(InstalledPackagesModel& installed) {
    QQmlComponent component(&engine_, qmlSource(QStringLiteral("/workspace/WorkspaceWindow.qml")));
    ASSERT_EQ(component.status(), QQmlComponent::Ready) << component.errorString().toStdString();
    explore_model_ = std::make_unique<ExploreModel>(std::make_shared<holonight_packages_testing::FakeExploreSource>());
    ASSERT_TRUE(QTest::qWaitFor([this] { return !explore_model_->loading(); }, 2000));
    view_.reset(qobject_cast<QQuickItem*>(component.createWithInitialProperties(
        {{QStringLiteral("installedPackagesModel"), QVariant::fromValue(&installed)},
         {QStringLiteral("updatesModel"), QVariant::fromValue(model_.get())},
         {QStringLiteral("exploreModel"), QVariant::fromValue(explore_model_.get())}})));
    ASSERT_NE(view_, nullptr);
  }

  void destroyView() { view_.reset(); }

  [[nodiscard]] FakeUpdateSource& source() const { return *source_; }
  [[nodiscard]] UpdatesModel& model() const { return *model_; }

 private:
  std::shared_ptr<FakeUpdateSource> source_ = std::make_shared<FakeUpdateSource>();
  std::unique_ptr<UpdatesModel> model_;
  std::unique_ptr<ExploreModel> explore_model_;
  QQmlEngine engine_;
  QQuickWindow window_;
  // Declared last so the view is destroyed before the window, engine and model it uses.
  std::unique_ptr<QQuickItem> view_;
};

TEST_F(UpdatesViewTest, RowsExposeAllRolesAndFlagIgnored) {
  source().enqueue(snapshotWith({makeUpdate("gamma", 4096, false), makeUpdate("banana", 2048, true)}, kNow));
  startModel();
  createView();

  ASSERT_TRUE(QTest::qWaitFor([this] {
    const auto current = rows();
    return current.size() == 2 && current[0] != nullptr && current[1] != nullptr;
  }));
  const auto delegates = rows();

  ASSERT_EQ(delegates.size(), 2U);
  ASSERT_NE(delegates[0], nullptr);
  ASSERT_NE(delegates[1], nullptr);
  const QQuickItem& banana = *delegates[0];
  EXPECT_EQ(banana.property("name").toString(), QStringLiteral("banana"));
  EXPECT_EQ(banana.property("installedVersion").toString(), QStringLiteral("1.0-1"));
  EXPECT_EQ(banana.property("availableVersion").toString(), QStringLiteral("2.0-1"));
  EXPECT_EQ(banana.property("repository").toString(), QStringLiteral("extra"));
  EXPECT_EQ(banana.property("downloadSizeLabel").toString(), QStringLiteral("2 KiB"));
  EXPECT_EQ(banana.property("sizeDeltaLabel").toString(), QStringLiteral("-1 KiB"));
  EXPECT_TRUE(banana.property("isIgnored").toBool());
  EXPECT_FALSE(delegates[1]->property("isIgnored").toBool());

  auto* banana_badge = banana.findChild<QObject*>(QStringLiteral("updateRowIgnoredBadge"));
  auto* gamma_badge = delegates[1]->findChild<QObject*>(QStringLiteral("updateRowIgnoredBadge"));
  ASSERT_NE(banana_badge, nullptr);
  ASSERT_NE(gamma_badge, nullptr);
  EXPECT_TRUE(banana_badge->property("visible").toBool());
  EXPECT_FALSE(gamma_badge->property("visible").toBool());
  EXPECT_TRUE(isVisible("updatesTable"));
}

TEST_F(UpdatesViewTest, HeadlineExcludesIgnoredRows) {
  std::vector<PendingUpdate> updates;
  updates.reserve(13);
  for (int index = 0; index < 10; ++index) {
    updates.push_back(makeUpdate("normal-" + std::to_string(index), 1024, false));
  }
  for (int index = 0; index < 3; ++index) {
    updates.push_back(makeUpdate("ignored-" + std::to_string(index), 1024 * 1024, true));
  }
  source().enqueue(snapshotWith(std::move(updates), kNow));
  startModel();
  createView();

  EXPECT_EQ(child("updatesList")->property("count").toInt(), 13);
  EXPECT_EQ(child("updatesHeadlineCount")->property("rawText").toString(), QStringLiteral("10 updates"));
  EXPECT_EQ(child("updatesHeadlineDownload")->property("rawText").toString(), QStringLiteral("10 KiB"));
  EXPECT_TRUE(isVisible("updatesHeadlineIgnored"));
  EXPECT_TRUE(child("updatesHeadlineIgnored")->property("rawText").toString().startsWith(QStringLiteral("3 ")));
  EXPECT_EQ(child("updatesHeadlineDataAsOf")->property("rawText").toString(), model().dataAsOfLabel());
}

TEST_F(UpdatesViewTest, ZeroUpdatesShowsUpToDateStateNotError) {
  source().enqueue(snapshotWith({}, kNow));
  startModel();
  createView();

  EXPECT_EQ(child("updatesList")->property("count").toInt(), 0);
  EXPECT_TRUE(isVisible("upToDateState"));
  EXPECT_EQ(child("upToDateState")->property("descriptionText").toString(), UpdatesModel::officialOnlyNote());
  EXPECT_FALSE(isVisible("errorState"));
  EXPECT_FALSE(isVisible("noDatabasesState"));
  EXPECT_FALSE(isVisible("updatesTable"));
  EXPECT_TRUE(isVisible("updatesHeadline"));
}

TEST_F(UpdatesViewTest, NoDatabasesStateIsDistinct) {
  source().enqueue(UpdateSnapshot{.updates = {}, .databasesFound = false, .dataAsOf = {}});
  startModel();
  createView();

  EXPECT_TRUE(isVisible("noDatabasesState"));
  EXPECT_FALSE(isVisible("upToDateState"));
  EXPECT_FALSE(isVisible("errorState"));
  EXPECT_FALSE(isVisible("updatesHeadline"));
}

TEST_F(UpdatesViewTest, InitialFailureShowsErrorStateWithReloadAvailable) {
  source().enqueue(std::unexpected(
      UpdateSourceError{.code = UpdateSourceErrorCode::DatabaseOpenFailed, .message = "could not open"}));
  startModel();
  createView();

  EXPECT_TRUE(isVisible("errorState"));
  EXPECT_EQ(child("errorState")->property("descriptionText").toString(), QStringLiteral("could not open"));
  EXPECT_TRUE(child("updatesReloadButton")->property("enabled").toBool());
}

TEST_F(UpdatesViewTest, LoadingDisablesReloadAndShowsProgress) {
  source().setBlocking(true);
  startModel(false);
  createView();

  EXPECT_TRUE(isVisible("loadingState"));
  EXPECT_TRUE(isVisible("updatesProgressBar"));
  EXPECT_FALSE(child("updatesReloadButton")->property("enabled").toBool());

  source().release();
  ASSERT_TRUE(waitUntilIdle());
  EXPECT_TRUE(child("updatesReloadButton")->property("enabled").toBool());
  EXPECT_FALSE(isVisible("updatesProgressBar"));
}

TEST_F(UpdatesViewTest, ReloadButtonReloadsAndFailureShowsBannerOverPreviousList) {
  source().enqueue(snapshotWith({makeUpdate("alpha", 10, false)}, kNow));
  source().enqueue(std::unexpected(
      UpdateSourceError{.code = UpdateSourceErrorCode::ConfigurationInvalid, .message = "pacman.conf unreadable"}));
  startModel();
  createView();
  EXPECT_FALSE(isVisible("updatesReloadBanner"));

  ASSERT_TRUE(QMetaObject::invokeMethod(child("updatesReloadButton"), "clicked"));
  ASSERT_TRUE(waitUntilIdle());
  QCoreApplication::processEvents();

  EXPECT_EQ(source().callCount(), 2);
  EXPECT_TRUE(isVisible("updatesReloadBanner"));
  EXPECT_TRUE(
      child("updatesReloadBanner")->property("text").toString().contains(QStringLiteral("pacman.conf unreadable")));
  EXPECT_EQ(child("updatesList")->property("count").toInt(), 1);
}

TEST_F(UpdatesViewTest, StaleHintNamesNoCommand) {
  source().enqueue(snapshotWith({makeUpdate("alpha", 10, false)}, kNow - std::chrono::days(8)));
  startModel();
  createView();

  EXPECT_TRUE(isVisible("updatesStaleHint"));
  const QString hint = child("updatesStaleHint")->property("text").toString();
  EXPECT_FALSE(hint.contains(QStringLiteral("pacman")));
  EXPECT_FALSE(hint.contains(QStringLiteral("-S")));
}

TEST_F(UpdatesViewTest, FreshDataShowsNoStaleHint) {
  source().enqueue(snapshotWith({makeUpdate("alpha", 10, false)}, kNow - std::chrono::hours(2)));
  startModel();
  createView();

  EXPECT_FALSE(isVisible("updatesStaleHint"));
}

TEST_F(UpdatesViewTest, SidebarSwitchesBetweenInstalledUpdatesAndExplorePages) {
  auto package_source = std::make_shared<MockPackageSource>();
  EXPECT_CALL(*package_source, enumerateInstalledPackages()).WillOnce(Return(std::vector<Package>{}));
  InstalledPackagesModel installed(std::make_shared<PackageListUseCase>(package_source));
  ASSERT_TRUE(
      QTest::qWaitFor([&installed] { return installed.status() != InstalledPackagesModel::Status::Loading; }, 2000));
  startModel();
  createWorkspace(installed);
  auto* pages = child("workspacePages");

  EXPECT_EQ(pages->property("currentIndex").toInt(), 0);
  EXPECT_TRUE(child("sidebarInstalledNav")->property("checked").toBool());

  ASSERT_TRUE(QMetaObject::invokeMethod(child("sidebarUpdatesNav"), "clicked"));
  EXPECT_EQ(pages->property("currentIndex").toInt(), 1);
  EXPECT_TRUE(child("sidebarUpdatesNav")->property("checked").toBool());
  EXPECT_FALSE(child("sidebarInstalledNav")->property("checked").toBool());

  ASSERT_TRUE(QMetaObject::invokeMethod(child("sidebarExploreNav"), "clicked"));
  EXPECT_EQ(pages->property("currentIndex").toInt(), 2);
  EXPECT_TRUE(child("sidebarExploreNav")->property("checked").toBool());
  EXPECT_FALSE(child("sidebarUpdatesNav")->property("checked").toBool());

  ASSERT_TRUE(QMetaObject::invokeMethod(child("sidebarInstalledNav"), "clicked"));
  EXPECT_EQ(pages->property("currentIndex").toInt(), 0);
  destroyView();
}

}  // namespace
