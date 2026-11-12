#include "ModelListRefresh.h"

#include "ExploreModel.h"
#include "InstalledPackagesModel.h"
#include "UpdateCheckClient.h"
#include "UpdatesModel.h"

ModelListRefresh::ModelListRefresh(InstalledPackagesModel& installed, ExploreModel& explore, QObject* parent)
    : QObject(parent), installed_(installed), explore_(explore) {
  connect(&installed_, &InstalledPackagesModel::statusChanged, this, &ModelListRefresh::onInstalledStatusChanged);
  connect(&explore_, &ExploreModel::stateChanged, this, &ModelListRefresh::onExploreStateChanged);
}

ModelListRefresh::~ModelListRefresh() = default;

void ModelListRefresh::refreshAfterOutcome() {
  refreshInstalled();
  refreshExplore();
}

void ModelListRefresh::setUpgradeTargets(UpdatesModel* updates, UpdateCheckClient* check) {
  updates_ = updates;
  check_ = check;
  if (updates_ != nullptr) {
    connect(updates_, &UpdatesModel::stateChanged, this, &ModelListRefresh::onUpdatesStateChanged);
  }
}

void ModelListRefresh::refreshAfterUpgrade() {
  refreshAfterOutcome();
  if (updates_ != nullptr) {
    if (updates_->loading()) {
      updates_pending_ = true;
    } else {
      updates_->reload();
    }
  }
  if (check_ != nullptr) {
    check_->checkNow();  // the session update count drops without waiting for the next timer
  }
}

void ModelListRefresh::onUpdatesStateChanged() {
  if (updates_pending_ && updates_ != nullptr && !updates_->loading()) {
    updates_pending_ = false;
    updates_->reload();
  }
}

void ModelListRefresh::refreshInstalled() {
  if (installed_.status() == InstalledPackagesModel::Status::Loading) {
    installed_pending_ = true;
    return;
  }
  installed_.refresh();
}

void ModelListRefresh::refreshExplore() {
  if (explore_.loading()) {
    explore_pending_ = true;
    return;
  }
  explore_.reload();
}

void ModelListRefresh::onInstalledStatusChanged() {
  if (installed_pending_ && installed_.status() != InstalledPackagesModel::Status::Loading) {
    installed_pending_ = false;
    installed_.refresh();
  }
}

void ModelListRefresh::onExploreStateChanged() {
  if (explore_pending_ && !explore_.loading()) {
    explore_pending_ = false;
    explore_.reload();
  }
}
