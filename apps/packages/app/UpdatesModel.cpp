#include "UpdatesModel.h"

#include "DataFreshness.h"
#include "holonight_packages_application/data_freshness.h"
#include "holonight_packages_application/package_size_formatter.h"
#include "holonight_packages_application/snapshot_selection.h"
#include "holonight_packages_domain/require_non_null.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace {

using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::UpdateSnapshot;
using holonight_packages_domain::UpdateSourceError;
using holonight_packages_domain::UpdateSourceErrorCode;

QString sizeLabel(std::uint64_t bytes) {
  return QString::fromStdString(holonight_packages_application::formatSizeBytes(bytes));
}

}  // namespace

UpdatesModel::UpdatesModel(std::shared_ptr<holonight_packages_domain::UpdateSource> source, QObject* parent, Clock now)
    : QAbstractListModel(parent),
      source_(holonight_packages_domain::requireNonNull(std::move(source), "UpdatesModel requires an update source")),
      now_(std::move(now)) {
  connect(&watcher_, &QFutureWatcher<LoadResult>::finished, this, &UpdatesModel::onLoadFinished);
  debounce_timer_.setSingleShot(true);
  debounce_timer_.setInterval(2000);
  connect(&debounce_timer_, &QTimer::timeout, this, &UpdatesModel::reload);
  const auto changed = [this] {
    armWatcher();
    if (loading_) {
      rerun_ = true;
    }
    debounce_timer_.start();
  };
  connect(&filesystem_watcher_, &QFileSystemWatcher::fileChanged, this, changed);
  connect(&filesystem_watcher_, &QFileSystemWatcher::directoryChanged, this, changed);
  armWatcher();
  startLoading();
}

UpdatesModel::~UpdatesModel() = default;

int UpdatesModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(updates_.size());
}

QVariant UpdatesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= updates_.size()) {
    return {};
  }
  const PendingUpdate& update = updates_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case NameRole:
      return QString::fromStdString(update.name);
    case InstalledVersionRole:
      return QString::fromStdString(update.installedVersion);
    case AvailableVersionRole:
      return QString::fromStdString(update.availableVersion);
    case RepositoryRole:
      return QString::fromStdString(update.repository);
    case DownloadSizeRole:
      return QVariant::fromValue<quint64>(update.downloadSizeBytes);
    case DownloadSizeLabelRole:
      return sizeLabel(update.downloadSizeBytes);
    case SizeDeltaRole:
      return QVariant::fromValue<qint64>(update.installedSizeDeltaBytes);
    case SizeDeltaLabelRole:
      return QString::fromStdString(
          holonight_packages_application::formatSignedSizeBytes(update.installedSizeDeltaBytes));
    case IsIgnoredRole:
      return update.ignored;
  }
  return {};
}

QHash<int, QByteArray> UpdatesModel::roleNames() const {
  static const QHash<int, QByteArray> role_names{
      {NameRole, QByteArrayLiteral("name")},
      {InstalledVersionRole, QByteArrayLiteral("installedVersion")},
      {AvailableVersionRole, QByteArrayLiteral("availableVersion")},
      {RepositoryRole, QByteArrayLiteral("repository")},
      {DownloadSizeRole, QByteArrayLiteral("downloadSize")},
      {DownloadSizeLabelRole, QByteArrayLiteral("downloadSizeLabel")},
      {SizeDeltaRole, QByteArrayLiteral("sizeDelta")},
      {SizeDeltaLabelRole, QByteArrayLiteral("sizeDeltaLabel")},
      {IsIgnoredRole, QByteArrayLiteral("isIgnored")},
  };
  return role_names;
}

UpdatesModel::ViewState UpdatesModel::state() const { return state_; }

QString UpdatesModel::emptyStateText() const {
  if (previously_loaded_) {
    return tr("No current update evaluation is available.");
  }
  return checked_data_ ? tr("All official-repository packages are up to date.")
                       : tr("No updates found in local package data");
}

QString UpdatesModel::errorMessage() const { return error_message_; }

bool UpdatesModel::loading() const { return loading_; }

int UpdatesModel::updateCount() const { return summary_.updateCount; }

int UpdatesModel::ignoredCount() const { return summary_.ignoredCount; }

quint64 UpdatesModel::totalDownloadBytes() const { return summary_.totalDownloadBytes; }

QString UpdatesModel::totalDownloadLabel() const { return sizeLabel(summary_.totalDownloadBytes); }

QDateTime UpdatesModel::dataAsOf() const { return data_as_of_; }

QString UpdatesModel::dataAsOfLabel() const { return data_freshness::dataAsOfLabel(data_as_of_); }

bool UpdatesModel::databasesStale() const { return databases_stale_; }

QString UpdatesModel::reloadErrorMessage() const { return reload_error_message_; }

QString UpdatesModel::staleHintText() { return data_freshness::staleHintText(); }

QString UpdatesModel::officialOnlyNote() { return tr("AUR and foreign packages are not covered."); }

void UpdatesModel::reload() {
  if (loading_) {
    return;
  }
  startLoading();
}

bool UpdatesModel::hasResult() const {
  return state_ == ViewState::Updates || state_ == ViewState::UpToDate || state_ == ViewState::NoDatabases;
}

// Same mechanism as InstalledPackagesModel: QtConcurrent::run keeps libalpm work off the GUI thread and
// QFutureWatcher delivers the result back on it, so all model state is only ever touched from the GUI thread.
void UpdatesModel::armWatcher() {
  QStringList desired;
  for (const auto& path : source_->watchPaths()) {
    if (path.empty()) {
      continue;
    }
    const QFileInfo info(QString::fromStdString(path.string()));
    if (info.exists()) {
      desired.append(info.absoluteFilePath());
    }
    auto parent = info.absoluteDir();
    while (!parent.exists() && parent.cdUp()) {
    }
    if (parent.exists()) {
      desired.append(parent.absolutePath());
    }
    if (info.isDir()) {
      QDirIterator entries(info.absoluteFilePath(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);
      while (entries.hasNext()) {
        desired.append(entries.next());
      }
    }
  }
  desired.removeDuplicates();
  const auto existing = filesystem_watcher_.files() + filesystem_watcher_.directories();
  for (const auto& path : existing) {
    if (!desired.contains(path)) {
      filesystem_watcher_.removePath(path);
    }
  }
  for (const auto& path : desired) {
    if (!existing.contains(path)) {
      filesystem_watcher_.addPath(path);
    }
  }
}

void UpdatesModel::startLoading() {
  loading_ = true;
  if (!hasResult()) {
    state_ = ViewState::Loading;
  }
  watcher_.setFuture(QtConcurrent::run([source = source_] -> LoadResult {
    try {
      return source->loadUpdates();
    } catch (const std::exception& exception) {
      return std::unexpected(UpdateSourceError{.code = UpdateSourceErrorCode::Unknown, .message = exception.what()});
    } catch (...) {
      return std::unexpected(
          UpdateSourceError{.code = UpdateSourceErrorCode::Unknown, .message = "Unknown update check failure"});
    }
  }));
  emit stateChanged();
}

void UpdatesModel::onLoadFinished() {
  LoadResult result = watcher_.future().takeResult();
  if (std::exchange(rerun_, false)) {
    startLoading();
    return;
  }
  if (result.has_value()) {
    local_ = *result;
    applyFreshest();
  } else {
    applyFailure(result.error());
  }
  loading_ = false;
  emit stateChanged();
  if (std::exchange(rerun_, false)) {
    startLoading();
  }
}

void UpdatesModel::applyFreshest() {
  const bool online = holonight_packages_application::selectFresherSnapshot(local_, online_) ==
                      holonight_packages_application::SnapshotChoice::Online;
  const bool checked =
      !online && local_ && std::ranges::any_of(local_->repositories, [](const auto& repo) { return repo.checked; });
  displayed_fetched_at_ =
      online || checked || (local_ && local_->previously_loaded) ? online_fetched_at_ : std::nullopt;
  applySnapshot(online ? *online_ : *local_);
}

void UpdatesModel::applyCheckedSnapshot(const holonight_packages_domain::CheckedSnapshot& snapshot) {
  online_ = snapshot.snapshot;
  online_fetched_at_ = snapshot.fetchedAt;
  // Without a local result yet the online snapshot is shown at once; the local result is merged in by the same rule
  // when it arrives.
  if (snapshot.snapshot.repositories.empty()) {
    applyFreshest();
  } else {
    if (loading_) {
      rerun_ = true;
    }
    reload();
  }
  emit stateChanged();
}

void UpdatesModel::invalidateCheckedSnapshot() {
  online_.reset();
  online_fetched_at_.reset();
  displayed_fetched_at_.reset();
  previously_loaded_ = true;
  source_text_ = tr("Previously loaded data");
  checked_data_ = false;
  reload();
  emit stateChanged();
}

void UpdatesModel::applySnapshot(UpdateSnapshot snapshot) {
  const bool checked = std::ranges::any_of(snapshot.repositories, [](const auto& repo) { return repo.checked; });
  const bool local = std::ranges::any_of(snapshot.repositories, [](const auto& repo) { return !repo.checked; });
  checked_data_ = checked;
  previously_loaded_ = snapshot.previously_loaded || (!snapshot.evaluated && displayed_fetched_at_.has_value());
  if (previously_loaded_) {
    source_text_ = tr("Previously loaded data");
  } else if (checked && local) {
    source_text_ = tr("Mixed local and checked data");
  } else if (checked) {
    source_text_ = tr("Checked repository data");
  } else {
    source_text_ = tr("Local package data");
  }
  holonight_packages_application::sortUpdatesByName(snapshot.updates);
  beginResetModel();
  updates_ = std::move(snapshot.updates);
  endResetModel();
  summary_ = holonight_packages_application::summarizeUpdates(updates_);

  if (!snapshot.databasesFound) {
    state_ = ViewState::NoDatabases;
    data_as_of_ = QDateTime();
    databases_stale_ = false;
  } else {
    state_ = updates_.empty() ? ViewState::UpToDate : ViewState::Updates;
    data_as_of_ = data_freshness::toQDateTime(snapshot.dataAsOf);
    databases_stale_ = holonight_packages_application::isStale(snapshot.dataAsOf, now_());
  }
  error_message_.clear();
  reload_error_message_.clear();
}

void UpdatesModel::applyFailure(const UpdateSourceError& error) {
  const QString reason = QString::fromStdString(error.message);
  if (hasResult()) {
    // Keep the usable rows and timestamp, but qualify them when current evaluation is unavailable.
    previously_loaded_ = true;
    checked_data_ = false;
    source_text_ = tr("Previously loaded data");
    reload_error_message_ = reason;
    return;
  }
  beginResetModel();
  updates_.clear();
  endResetModel();
  summary_ = {};
  state_ = ViewState::Error;
  error_message_ = reason;
  reload_error_message_.clear();
  data_as_of_ = QDateTime();
  databases_stale_ = false;
}
