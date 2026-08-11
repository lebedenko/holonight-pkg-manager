#include "PackagesApplication.h"

#include "InstalledPackagesModel.h"
#include "holonight_packages_application/package_list_use_case.h"
#include "holonight_packages_backends/alpm_package_source.h"

#include <QQuickView>
#include <QVariant>

PackagesApplication::PackagesApplication(int& argc, char** argv) : QGuiApplication(argc, argv) {
  setApplicationName(QStringLiteral("holonight-packages"));
  setApplicationDisplayName(tr("HoloNight Packages"));
  setApplicationVersion(QStringLiteral(HOLONIGHT_PACKAGES_VERSION));
  setWindowIcon(QIcon(QStringLiteral(":/HolonightPackages/assets/holonight-pkg-manager.svg")));

  auto package_source = std::make_shared<holonight_packages_backends::AlpmPackageSource>("/", "/var/lib/pacman");
  auto use_case = std::make_shared<holonight_packages_application::PackageListUseCase>(std::move(package_source));
  installed_packages_model_ = std::make_unique<InstalledPackagesModel>(std::move(use_case));

  view_ = std::make_unique<QQuickView>();
  view_->setMinimumSize(QSize(720, 480));
  view_->resize(1100, 720);
  view_->setResizeMode(QQuickView::SizeRootObjectToView);
  view_->setInitialProperties(
      {{QStringLiteral("installedPackagesModel"), QVariant::fromValue(installed_packages_model_.get())}});
  view_->setSource(QUrl(QStringLiteral("qrc:/HolonightPackages/workspace/WorkspaceWindow.qml")));
  view_->setTitle(applicationDisplayName());
  view_->show();
}

PackagesApplication::~PackagesApplication() = default;
