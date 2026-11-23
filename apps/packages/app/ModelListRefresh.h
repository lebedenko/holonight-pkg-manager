#pragma once

#include "holonight_packages_application/package_list_refresh.h"

#include <QObject>

class InstalledPackagesModel;
class ExploreModel;
class UpdatesModel;
class UpdateCheckClient;
class HistoryModel;

// Re-reads the Installed list, the Explore index and the opened History page after a transaction outcome (REQ-F-019,
// DESIGN section 9; transaction-history REQ-F-031).
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
  // system-upgrade REQ-F-088: also the Updates list, and one CheckNow() of holonight-packaged.
  void refreshAfterUpgrade() override;

  // The Updates list and the update-check client that a system upgrade refreshes. Either may stay null.
  void setUpgradeTargets(UpdatesModel* updates, UpdateCheckClient* check);
  // The History page model; may stay null. It queues its own repeat and reads nothing before the page was opened.
  void setHistoryTarget(HistoryModel* history);

 private:
  void refreshInstalled();
  void refreshExplore();
  void refreshHistory();
  void onInstalledStatusChanged();
  void onExploreStateChanged();
  void onUpdatesStateChanged();

  InstalledPackagesModel& installed_;
  ExploreModel& explore_;
  bool installed_pending_ = false;
  bool explore_pending_ = false;
  UpdatesModel* updates_ = nullptr;
  UpdateCheckClient* check_ = nullptr;
  bool updates_pending_ = false;
  HistoryModel* history_ = nullptr;
};
