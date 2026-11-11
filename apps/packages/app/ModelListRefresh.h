#pragma once

#include "holonight_packages_application/package_list_refresh.h"

#include <QObject>

class InstalledPackagesModel;
class ExploreModel;

// Re-reads the Installed list and the Explore index after a transaction outcome (REQ-F-019, DESIGN section 9).
// Both models are single-flight: a refresh requested while a load runs would be dropped and the list could keep
// showing pre-transaction content. So a request that arrives during a load is repeated once the load ends.
class ModelListRefresh final : public QObject, public holonight_packages_application::PackageListRefresh {
  Q_OBJECT

 public:
  ModelListRefresh(InstalledPackagesModel& installed, ExploreModel& explore, QObject* parent = nullptr);
  ~ModelListRefresh() override;

  ModelListRefresh(const ModelListRefresh&) = delete;
  ModelListRefresh& operator=(const ModelListRefresh&) = delete;
  ModelListRefresh(ModelListRefresh&&) = delete;
  ModelListRefresh& operator=(ModelListRefresh&&) = delete;

  void refreshAfterOutcome() override;

 private:
  void refreshInstalled();
  void refreshExplore();
  void onInstalledStatusChanged();
  void onExploreStateChanged();

  InstalledPackagesModel& installed_;
  ExploreModel& explore_;
  bool installed_pending_ = false;
  bool explore_pending_ = false;
};
