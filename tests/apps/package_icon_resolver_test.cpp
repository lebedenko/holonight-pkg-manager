#include "ExploreModel.h"
#include "InstalledPackagesFilterModel.h"
#include "InstalledPackagesModel.h"
#include "PackageIconResolver.h"
#include "UpdatesModel.h"
#include "fake_explore_source.h"
#include "fake_update_source.h"
#include "mock_package_source.h"

#include <QDir>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using holonight_packages_domain::Package;

void writeFile(const QString& path, const QByteArray& contents) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(contents), contents.size());
}

QString imageFile(const QString& directory, const QString& name, QRgb color) {
  const QString path = directory + '/' + name + QStringLiteral(".png");
  QImage image(24, 24, QImage::Format_RGB32);
  image.fill(color);
  EXPECT_TRUE(image.save(path));
  return path;
}

QString desktopFile(const QString& directory, const QString& name, const QString& icon, bool hidden = false) {
  const QString path = directory + '/' + name + QStringLiteral(".desktop");
  writeFile(path, (QStringLiteral("[Desktop Entry]\nType=Application\nName=Fixture\nIcon=") + icon +
                   (hidden ? QStringLiteral("\nNoDisplay=true\n") : QStringLiteral("\n")))
                      .toUtf8());
  return path;
}

TEST(PackageIconResolver, JpegXlUsesDiscoveredPluginOrFallsBack) {
  QTemporaryDir directory;
  const QString image = QStringLiteral(HOLONIGHT_TEST_FIXTURES_DIR "/icons/icon.jxl");
  const auto desktop = desktopFile(directory.path(), QStringLiteral("sample"), image);
  PackageIconResolver resolver(nullptr, directory.path());
  QSignalSpy changed(&resolver, &PackageIconResolver::iconsChanged);
  resolver.setInstalledPackages({Package{.name = "sample", .desktopEntryPaths = {desktop.toStdString()}}});
  ASSERT_TRUE(changed.wait(5000));
  const bool supported = QImageReader::supportedImageFormats().contains("jxl");
  if (supported) {
    EXPECT_EQ(resolver.icon(QStringLiteral("sample")).source, QUrl::fromLocalFile(image));
  } else {
    EXPECT_TRUE(resolver.icon(QStringLiteral("sample")).source.isEmpty());
  }
  RecordProperty("jpeg_xl_decoder_available", supported);
}

TEST(PackageIconResolver, SharedQmlIconShowsOriginalImageAndFallsBack) {
  QTemporaryDir directory;
  const auto image = imageFile(directory.path(), QStringLiteral("icon"), qRgb(4, 8, 16));
  QQmlEngine engine;
  QQmlComponent component(&engine,
                          QUrl::fromLocalFile(QStringLiteral(HOLONIGHT_QML_SOURCE_DIR "/packages/PackageIcon.qml")));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> object(
      component.createWithInitialProperties({{"packageName", "sample"}, {"iconSource", QUrl()}}));
  ASSERT_NE(object, nullptr);
  const auto children = object->findChildren<QQuickItem*>();
  const auto image_item =
      std::ranges::find_if(children, [](const auto* child) { return child->property("source").isValid(); });
  ASSERT_NE(image_item, children.end());
  EXPECT_FALSE((*image_item)->isVisible());
  object->setProperty("iconSource", QUrl::fromLocalFile(image));
  QTRY_VERIFY((*image_item)->isVisible());
  object->setProperty("iconSource", QUrl());
  QTRY_VERIFY(!(*image_item)->isVisible());
}

TEST(PackageIconResolver, DesktopPriorityVisibilityInvalidImagesAndRefresh) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto first = imageFile(directory.path(), QStringLiteral("first"), qRgb(255, 0, 0));
  const auto preferred = imageFile(directory.path(), QStringLiteral("preferred"), qRgb(0, 255, 0));
  const auto first_entry = desktopFile(directory.path(), QStringLiteral("a"), first);
  const auto match = desktopFile(directory.path(), QStringLiteral("sample"), preferred);
  const auto hidden = desktopFile(directory.path(), QStringLiteral("hidden"), preferred, true);
  const auto invalid = desktopFile(directory.path(), QStringLiteral("invalid"), directory.path() + "/broken.png");
  writeFile(directory.path() + "/broken.png", "not an image");
  PackageIconResolver resolver(nullptr, directory.path());
  QSignalSpy changed(&resolver, &PackageIconResolver::iconsChanged);
  resolver.setInstalledPackages({
      Package{.name = "sample", .desktopEntryPaths = {first_entry.toStdString(), match.toStdString()}},
      Package{.name = "hidden", .desktopEntryPaths = {hidden.toStdString()}},
      Package{.name = "invalid", .desktopEntryPaths = {invalid.toStdString()}},
  });
  ASSERT_TRUE(changed.wait(5000));
  EXPECT_EQ(resolver.icon(QStringLiteral("sample")).source, QUrl::fromLocalFile(preferred));
  EXPECT_TRUE(resolver.icon(QStringLiteral("hidden")).source.isEmpty());
  EXPECT_TRUE(resolver.icon(QStringLiteral("invalid")).source.isEmpty());
  EXPECT_TRUE(resolver.icon(QStringLiteral("sample-git")).source.isEmpty());
  resolver.setInstalledPackages({Package{.name = "sample", .desktopEntryPaths = {first_entry.toStdString()}}});
  ASSERT_TRUE(changed.wait(5000));
  EXPECT_EQ(resolver.icon(QStringLiteral("sample")).source, QUrl::fromLocalFile(first));
  resolver.setInstalledPackages({});
  ASSERT_TRUE(changed.wait(5000));
  EXPECT_TRUE(resolver.icon(QStringLiteral("sample")).source.isEmpty());
}

TEST(PackageIconResolver, CatalogUsesExactPackageAssociationsAndStableComponentOrder) {
  QTemporaryDir directory;
  const auto first = imageFile(directory.path(), QStringLiteral("first"), qRgb(255, 0, 0));
  const auto second = imageFile(directory.path(), QStringLiteral("second"), qRgb(0, 255, 0));
  const auto component = [](const QString& identifier, const QString& icon) {
    return QStringLiteral(
               "<component type='desktop-application'><id>%1</id><name>Fixture</name>"
               "<summary>Fixture app</summary><pkgname>sample</pkgname>"
               "<icon type='local'>%2</icon></component>")
        .arg(identifier, icon);
  };
  writeFile(directory.path() + "/catalog.xml",
            (QStringLiteral("<?xml version='1.0'?><components version='1.0' origin='fixture'>") +
             component(QStringLiteral("z.desktop"), second) + component(QStringLiteral("a.desktop"), first) +
             QStringLiteral("</components>"))
                .toUtf8());
  PackageIconResolver resolver(nullptr, directory.path());
  QSignalSpy changed(&resolver, &PackageIconResolver::iconsChanged);
  ASSERT_TRUE(changed.wait(5000));
  EXPECT_EQ(resolver.icon(QStringLiteral("sample")).source, QUrl::fromLocalFile(first));
  EXPECT_TRUE(resolver.icon(QStringLiteral("sample"), false).source.isEmpty());
  EXPECT_TRUE(resolver.icon(QStringLiteral("sample-git")).source.isEmpty());
}

TEST(PackageIconResolver, CatalogFindsCachedIconsInOriginDirectory) {
  QTemporaryDir directory;
  ASSERT_TRUE(QDir().mkpath(directory.path() + "/icons/fixture/64x64"));
  const auto image = imageFile(directory.path() + "/icons/fixture/64x64", QStringLiteral("sample"), qRgb(8, 16, 32));
  writeFile(directory.path() + "/catalog.xml",
            "<components version='1.0' origin='fixture'><component type='desktop-application'>"
            "<id>sample.desktop</id><name>Sample</name><summary>Sample</summary><pkgname>sample</pkgname>"
            "<icon type='cached' width='64' height='64'>sample.png</icon></component></components>");
  PackageIconResolver resolver(nullptr, directory.path());
  QSignalSpy changed(&resolver, &PackageIconResolver::iconsChanged);
  ASSERT_TRUE(changed.wait(5000));
  EXPECT_EQ(resolver.icon(QStringLiteral("sample")).source, QUrl::fromLocalFile(image));
}

TEST(PackageIconResolver, AvailableThemePrecedesCachedImageAndKeepsOriginalColors) {
  QTemporaryDir directory;
  const auto paths = QIcon::themeSearchPaths();
  const auto theme = QIcon::themeName();
  const auto restore = qScopeGuard([&] {
    QIcon::setThemeSearchPaths(paths);
    QIcon::setThemeName(theme);
  });
  ASSERT_TRUE(QDir().mkpath(directory.path() + "/fixture/64x64/apps"));
  writeFile(
      directory.path() + "/fixture/index.theme",
      "[Icon Theme]\nName=Fixture\nDirectories=64x64/apps\n[64x64/apps]\nSize=64\nType=Fixed\nContext=Applications\n");
  imageFile(directory.path() + "/fixture/64x64/apps", QStringLiteral("fixture-icon"), qRgb(18, 52, 86));
  QIcon::setThemeSearchPaths({directory.path()});
  QIcon::setThemeName(QStringLiteral("fixture"));
  const auto cached = imageFile(directory.path(), QStringLiteral("cached"), qRgb(255, 0, 0));
  writeFile(
      directory.path() + "/catalog.xml",
      (QStringLiteral("<components version='1.0' origin='fixture'><component type='desktop-application'>"
                      "<id>sample.desktop</id><name>Sample</name><summary>Sample</summary><pkgname>sample</pkgname>"
                      "<icon type='stock'>fixture-icon</icon><icon type='local'>%1</icon>"
                      "</component></components>")
           .arg(cached))
          .toUtf8());
  PackageIconResolver resolver(nullptr, directory.path());
  QSignalSpy changed(&resolver, &PackageIconResolver::iconsChanged);
  ASSERT_TRUE(changed.wait(5000));
  const auto icon = resolver.icon(QStringLiteral("sample"));
  EXPECT_EQ(icon.name, QStringLiteral("fixture-icon"));
  EXPECT_EQ(icon.source.scheme(), QStringLiteral("data"));
  const auto data = QByteArray::fromBase64(icon.source.toString().section(',', 1).toLatin1());
  const auto decoded = QImage::fromData(data);
  ASSERT_FALSE(decoded.isNull());
  EXPECT_EQ(decoded.pixelColor(12, 12), QColor(18, 52, 86));
}

TEST(PackageIconResolver, BrokenCatalogAndRemoteIconsRetainFallback) {
  QTemporaryDir directory;
  writeFile(directory.path() + "/catalog.xml", "broken xml");
  PackageIconResolver resolver(nullptr, directory.path());
  QSignalSpy changed(&resolver, &PackageIconResolver::iconsChanged);
  ASSERT_TRUE(changed.wait(5000));
  EXPECT_TRUE(resolver.icon(QStringLiteral("sample")).source.isEmpty());
  writeFile(directory.path() + "/catalog.xml",
            "<components version='1.0' origin='fixture'><component type='desktop-application'>"
            "<id>sample.desktop</id><name>Sample</name><summary>Sample</summary><pkgname>sample</pkgname>"
            "<icon type='remote'>https://invalid.example/icon.png</icon></component></components>");
  resolver.setInstalledPackages({});
  ASSERT_TRUE(changed.wait(5000));
  EXPECT_TRUE(resolver.icon(QStringLiteral("sample")).source.isEmpty());
}

TEST(PackageIconResolver, SharedModelRolesSurviveFilteringSelectionAndInstalledRefresh) {
  QTemporaryDir directory;
  const auto image = imageFile(directory.path(), QStringLiteral("icon"), qRgb(4, 8, 16));
  const auto desktop = desktopFile(directory.path(), QStringLiteral("sample"), image);
  const Package package{.name = "sample", .desktopEntryPaths = {desktop.toStdString()}};
  auto source = std::make_shared<holonight_packages_application::MockPackageSource>();
  EXPECT_CALL(*source, enumerateInstalledPackages())
      .WillOnce(testing::Return(std::vector<Package>{package}))
      .WillOnce(testing::Return(std::vector<Package>{Package{.name = "sample"}}));
  InstalledPackagesModel installed(std::make_shared<holonight_packages_application::PackageListUseCase>(source));
  auto update_source = std::make_shared<holonight_packages_testing::FakeUpdateSource>();
  holonight_packages_domain::UpdateSnapshot updates;
  updates.updates.push_back({.name = "sample"});
  update_source->enqueue(updates);
  UpdatesModel update(update_source);
  auto explore_source = std::make_shared<holonight_packages_testing::FakeExploreSource>();
  holonight_packages_domain::ExploreSnapshot sync;
  sync.packages.push_back({.name = "sample", .repository = "extra"});
  explore_source->enqueue(sync);
  ExploreModel explore(explore_source, nullptr, &std::chrono::system_clock::now, std::chrono::milliseconds(0));
  PackageIconResolver resolver(nullptr, directory.path());
  installed.setIconResolver(&resolver);
  update.setIconResolver(&resolver);
  explore.setIconResolver(&resolver);
  QSignalSpy changed(&resolver, &PackageIconResolver::iconsChanged);
  ASSERT_TRUE(changed.wait(5000));
  QTRY_VERIFY_WITH_TIMEOUT(!resolver.icon(QStringLiteral("sample")).source.isEmpty(), 5000);
  QTRY_COMPARE(update.rowCount(), 1);
  explore.setSearchText(QStringLiteral("sample"));
  QTRY_COMPARE(explore.rowCount(), 1);
  explore.setCurrentRow(0);
  EXPECT_EQ(installed.data(installed.index(0), InstalledPackagesModel::IconSourceRole).toUrl(),
            QUrl::fromLocalFile(image));
  EXPECT_EQ(update.data(update.index(0), UpdatesModel::IconSourceRole).toUrl(), QUrl::fromLocalFile(image));
  EXPECT_EQ(explore.data(explore.index(0), ExploreModel::IconSourceRole).toUrl(), QUrl::fromLocalFile(image));
  InstalledPackagesFilterModel filter;
  filter.setSourceModel(&installed);
  filter.setSearchText(QStringLiteral("sample"));
  ASSERT_EQ(filter.rowCount(), 1);
  EXPECT_EQ(filter.data(filter.index(0, 0), InstalledPackagesModel::IconSourceRole).toUrl(),
            QUrl::fromLocalFile(image));
  filter.setCurrentRow(0);
  installed.refresh();
  ASSERT_TRUE(changed.wait(5000));
  EXPECT_TRUE(update.data(update.index(0), UpdatesModel::IconSourceRole).toUrl().isEmpty());
  EXPECT_EQ(explore.currentRow(), 0);
  EXPECT_EQ(filter.currentRow(), 0);
  EXPECT_TRUE(explore.data(explore.index(0), ExploreModel::IconSourceRole).toUrl().isEmpty());
}

}  // namespace
