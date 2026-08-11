#include "InstalledPackagesModel.h"
#include "mock_package_source.h"

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
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

class InstalledPackagesViewTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    qmlRegisterUncreatableType<InstalledPackagesModel>("HolonightPackages", 1, 0, "InstalledPackagesModel",
                                                       QStringLiteral("Provided by the test"));
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

}  // namespace
