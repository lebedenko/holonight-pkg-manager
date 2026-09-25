#include "PackagesApplication.h"

#include "ExploreModel.h"
#include "InstalledPackagesModel.h"
#include "UpdatesModel.h"
#include "holonight_packages_application/package_list_use_case.h"
#include "holonight_packages_backends/alpm_explore_source.h"
#include "holonight_packages_backends/alpm_package_source.h"
#include "holonight_packages_backends/alpm_update_source.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickView>
#include <QScreen>
#include <QVariant>

PackagesApplication::PackagesApplication(int& argc, char** argv) : QGuiApplication(argc, argv) {
  setApplicationName(QStringLiteral("holonight-packages"));
  setApplicationDisplayName(tr("HoloNight Packages"));
  setApplicationVersion(QStringLiteral(HOLONIGHT_PACKAGES_VERSION));
  setWindowIcon(QIcon(QStringLiteral(":/HolonightPackages/assets/holonight-pkg-manager.svg")));

  auto package_source = std::make_shared<holonight_packages_backends::AlpmPackageSource>("/", "/var/lib/pacman");
  auto use_case = std::make_shared<holonight_packages_application::PackageListUseCase>(std::move(package_source));
  installed_packages_model_ = std::make_unique<InstalledPackagesModel>(std::move(use_case));

  auto update_source = std::make_shared<holonight_packages_backends::AlpmUpdateSource>(
      holonight_packages_backends::AlpmUpdateSourceOptions{
          .databaseRoot = "/", .databasePath = "/var/lib/pacman", .pacmanConfPath = "/etc/pacman.conf"});
  updates_model_ = std::make_unique<UpdatesModel>(std::move(update_source));

  auto explore_source = std::make_shared<holonight_packages_backends::AlpmExploreSource>(
      holonight_packages_backends::AlpmExploreSourceOptions{.databaseRoot = "/", .databasePath = "/var/lib/pacman"});
  explore_model_ = std::make_unique<ExploreModel>(std::move(explore_source));

  view_ = std::make_unique<QQuickView>();
  if (QFileInfo{applicationFilePath()}.canonicalFilePath() ==
      QFileInfo{QStringLiteral(HOLONIGHT_BUILD_EXECUTABLE)}.canonicalFilePath()) {
    view_->engine()->addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  } else {
    view_->engine()->addImportPath(
        QDir{applicationDirPath()}.absoluteFilePath(QStringLiteral(HOLONIGHT_INSTALL_QML_PATH)));
  }
  view_->setMinimumSize(QSize(720, 480));
  QSize initial_size(1360, 890);
  if (const auto* screen = view_->screen()) {
    initial_size.scale(initial_size.boundedTo(screen->availableGeometry().size()), Qt::KeepAspectRatio);
  }
  view_->resize(initial_size.expandedTo(view_->minimumSize()));
  view_->setResizeMode(QQuickView::SizeRootObjectToView);
  view_->setInitialProperties(
      {{QStringLiteral("installedPackagesModel"), QVariant::fromValue(installed_packages_model_.get())},
       {QStringLiteral("updatesModel"), QVariant::fromValue(updates_model_.get())},
       {QStringLiteral("exploreModel"), QVariant::fromValue(explore_model_.get())}});
  view_->setSource(QUrl(QStringLiteral("qrc:/HolonightPackages/workspace/WorkspaceWindow.qml")));
  if (view_->status() == QQuickView::Error) {
    for (const QQmlError& error : view_->errors()) {
      qCritical() << error;
    }
    return;
  }
  view_->setTitle(applicationDisplayName());
  view_->show();
  ready_ = true;
}

PackagesApplication::~PackagesApplication() {
  view_.reset();
  explore_model_.reset();
  updates_model_.reset();
  installed_packages_model_.reset();
}

bool PackagesApplication::isReady() const { return ready_; }
