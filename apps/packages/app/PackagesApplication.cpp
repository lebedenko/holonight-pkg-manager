#include "PackagesApplication.h"

#include <QQuickView>

PackagesApplication::PackagesApplication(int& argc, char** argv) : QGuiApplication(argc, argv) {
  setApplicationName(QStringLiteral("holonight-packages"));
  setApplicationDisplayName(tr("HoloNight Packages"));
  setApplicationVersion(QStringLiteral(HOLONIGHT_PACKAGES_VERSION));
  setWindowIcon(QIcon(QStringLiteral(":/HolonightPackages/assets/holonight-pkg-manager.svg")));

  view_ = std::make_unique<QQuickView>();
  view_->setMinimumSize(QSize(720, 480));
  view_->resize(1100, 720);
  view_->setResizeMode(QQuickView::SizeRootObjectToView);
  view_->setSource(QUrl(QStringLiteral("qrc:/HolonightPackages/workspace/WorkspaceWindow.qml")));
  view_->setTitle(applicationDisplayName());
  view_->show();
}

PackagesApplication::~PackagesApplication() = default;
