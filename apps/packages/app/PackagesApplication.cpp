#include "PackagesApplication.h"

#include "DBusUpdateCheckClient.h"
#include "ExploreModel.h"
#include "InstalledPackagesModel.h"
#include "SnapshotFileReader.h"
#include "UpdateCheckModel.h"
#include "UpdateStatusClient.h"
#include "UpdatesModel.h"
#include "holonight_packages_application/package_list_use_case.h"
#include "holonight_packages_application/update_check_runtime.h"
#include "holonight_packages_backends/alpm_explore_source.h"
#include "holonight_packages_backends/alpm_package_source.h"
#include "holonight_packages_backends/alpm_update_source.h"
#include "holonight_packages_backends/pacman_defaults.h"
#include "holonight_packages_persistence/cache_locations.h"
#include "holonight_packages_persistence/json_update_snapshot_store.h"

#include <QDBusConnection>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickView>
#include <QScreen>
#include <QVariant>

#include <chrono>
#include <filesystem>
#include <string>

PackagesApplication::PackagesApplication(int& argc, char** argv) : QGuiApplication(argc, argv) {
  setApplicationName(QStringLiteral("holonight-packages"));
  setApplicationDisplayName(tr("HoloNight Packages"));
  setApplicationVersion(QStringLiteral(HOLONIGHT_PACKAGES_VERSION));
  setWindowIcon(QIcon(QStringLiteral(":/HolonightPackages/assets/holonight-pkg-manager.svg")));

  auto package_source = std::make_shared<holonight_packages_backends::AlpmPackageSource>(
      holonight_packages_backends::kDefaultPacmanRoot, holonight_packages_backends::kDefaultPacmanDatabasePath);
  auto use_case = std::make_shared<holonight_packages_application::PackageListUseCase>(std::move(package_source));
  installed_packages_model_ = std::make_unique<InstalledPackagesModel>(std::move(use_case));

  const auto cache_dir =
      holonight_packages_persistence::appCacheDir(holonight_packages_persistence::processEnvironment());
  auto update_source = std::make_shared<holonight_packages_backends::AlpmUpdateSource>(
      holonight_packages_backends::AlpmUpdateSourceOptions{
          .databaseRoot = holonight_packages_backends::kDefaultPacmanRoot,
          .databasePath = holonight_packages_backends::kDefaultPacmanDatabasePath,
          .pacmanConfPath = holonight_packages_backends::kDefaultPacmanConfPath,
          .snapshotFile = cache_dir.value_or(std::filesystem::path()) /
                          std::string(holonight_packages_persistence::kSnapshotFileName),
      });
  updates_model_ = std::make_unique<UpdatesModel>(std::move(update_source));

  auto explore_source = std::make_shared<holonight_packages_backends::AlpmExploreSource>(
      holonight_packages_backends::AlpmExploreSourceOptions{
          .databaseRoot = holonight_packages_backends::kDefaultPacmanRoot,
          .databasePath = holonight_packages_backends::kDefaultPacmanDatabasePath,
      });
  explore_model_ = std::make_unique<ExploreModel>(std::move(explore_source));

  update_status_client_ = std::make_unique<UpdateStatusClient>(QDBusConnection::sessionBus());

  // Online update check: the GUI neither checks nor writes anything. "Check now" asks holonight-packaged over D-Bus,
  // and the resulting list is read back from the snapshot file it writes.
  update_check_client_ = std::make_unique<DBusUpdateCheckClient>(QDBusConnection::sessionBus());

  snapshot_reader_ = std::make_unique<SnapshotFileReader>(
      std::make_shared<holonight_packages_persistence::JsonUpdateSnapshotStore>(
          cache_dir.value_or(std::filesystem::path()) / std::string(holonight_packages_persistence::kSnapshotFileName)),
      cache_dir.value_or(std::filesystem::path()) / std::string(holonight_packages_persistence::kSnapshotFileName));
  update_check_model_ = std::make_unique<UpdateCheckModel>(
      update_check_client_.get(), snapshot_reader_.get(), updates_model_.get(), &std::chrono::system_clock::now,
      std::make_unique<holonight_packages_application::QtOneShotTimer>());
  if (cache_dir.has_value()) {
    snapshot_reader_->start();
  }

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
  view_->setInitialProperties({
      {QStringLiteral("installedPackagesModel"), QVariant::fromValue(installed_packages_model_.get())},
      {QStringLiteral("updatesModel"), QVariant::fromValue(updates_model_.get())},
      {QStringLiteral("exploreModel"), QVariant::fromValue(explore_model_.get())},
      {QStringLiteral("updateStatusClient"), QVariant::fromValue(update_status_client_.get())},
      {QStringLiteral("updateCheckModel"), QVariant::fromValue(update_check_model_.get())},
  });
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
  update_check_model_.reset();
  snapshot_reader_.reset();
  update_check_client_.reset();
  update_status_client_.reset();
  explore_model_.reset();
  updates_model_.reset();
  installed_packages_model_.reset();
}

bool PackagesApplication::isReady() const { return ready_; }
