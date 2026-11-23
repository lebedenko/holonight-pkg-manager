#include "UpdatesModel.h"

#include "DataFreshness.h"
#include "holonight_packages_application/data_freshness.h"
#include "holonight_packages_application/package_size_formatter.h"
#include "holonight_packages_domain/require_non_null.h"

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
      now_(std::move(now)),
      filesystem_watcher_(
          [source = source_] {
            QStringList desired;
            for (const auto& path : source->watchPaths()) {
              if (!path.empty()) {
                desired.append(QString::fromStdString(path.string()));
              }
            }
            return desired;
          },
          std::chrono::milliseconds{2000}) {
  connect(&filesystem_watcher_, &holonight_packages_application::UpdatePathWatcher::invalidated, this,
          [this] { snapshots_.invalidateInFlightEvaluation(loading_); });
  connect(&filesystem_watcher_, &holonight_packages_application::UpdatePathWatcher::reconciled, this, [this] {
    initial_watch_ready_ = true;
    if (loading_) {
      snapshots_.invalidateInFlightEvaluation(true);
    } else {
      reload();
    }
  });
  connect(&watcher_, &QFutureWatcher<LoadResult>::finished, this, &UpdatesModel::onLoadFinished);
  filesystem_watcher_.start();
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
    case IconNameRole:
      return icon_resolver_ ? icon_resolver_->icon(QString::fromStdString(update.name), true).name : QString();
    case IconSourceRole:
      return icon_resolver_ ? icon_resolver_->icon(QString::fromStdString(update.name), true).source : QUrl();
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
      {IconNameRole, QByteArrayLiteral("iconName")},
      {IconSourceRole, QByteArrayLiteral("iconSource")},
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

bool UpdatesModel::loading() const { return loading_ || !initial_watch_ready_; }

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
  if (loading()) {
    return;
  }
  startLoading();
}

bool UpdatesModel::hasResult() const {
  return state_ == ViewState::Updates || state_ == ViewState::UpToDate || state_ == ViewState::NoDatabases;
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
  if (snapshots_.takeInvalidation()) {
    loading_ = false;
    if (!filesystem_watcher_.pending()) {
      startLoading();
    }
    return;
  }
  if (result.has_value()) {
    snapshots_.recordEvaluation(std::move(*result));
    applyFreshest();
  } else {
    applyFailure(result.error());
  }
  loading_ = false;
  emit stateChanged();
  if (snapshots_.takeInvalidation()) {
    startLoading();
  }
}

void UpdatesModel::applyFreshest() {
  const auto& selected = snapshots_.selected();
  const auto& checked_snapshot = snapshots_.checked();
  const bool checked = std::ranges::any_of(selected.repositories, [](const auto& repo) { return repo.checked; });
  if (checked_snapshot && (checked || selected.previously_loaded)) {
    displayed_fetched_at_ = checked_snapshot->fetchedAt;
  } else if (checked || !selected.previously_loaded) {
    displayed_fetched_at_.reset();
  }
  applySnapshot(selected);
}

void UpdatesModel::applyCheckedSnapshot(const holonight_packages_domain::CheckedSnapshot& snapshot) {
  snapshots_.adoptChecked(snapshot);
  if (!hasResult()) {
    displayed_fetched_at_ = snapshot.fetchedAt;
    applySnapshot(snapshots_.checked()->snapshot);
  }
  snapshots_.invalidateInFlightEvaluation(loading_);
  reload();
  emit stateChanged();
}

void UpdatesModel::invalidateCheckedSnapshot() {
  snapshots_.clearChecked();
  previously_loaded_ = true;
  source_text_ = tr("Previously loaded data");
  checked_data_ = false;
  snapshots_.invalidateInFlightEvaluation(loading_);
  reload();
  emit stateChanged();
}

void UpdatesModel::applySnapshot(UpdateSnapshot snapshot) {
  const bool checked = std::ranges::any_of(snapshot.repositories, [](const auto& repo) { return repo.checked; });
  const bool local = std::ranges::any_of(snapshot.repositories, [](const auto& repo) { return !repo.checked; });
  previously_loaded_ = snapshot.previously_loaded || (!snapshot.evaluated && displayed_fetched_at_.has_value());
  checked_data_ = checked && !previously_loaded_;
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

  if (!snapshot.databasesFound && !previously_loaded_) {
    state_ = ViewState::NoDatabases;
    data_as_of_ = QDateTime();
    databases_stale_ = false;
  } else {
    state_ = updates_.empty() ? ViewState::UpToDate : ViewState::Updates;
    data_as_of_ = data_freshness::toQDateTime(snapshot.dataAsOf);
    databases_stale_ = holonight_packages_application::isStale(snapshot.dataAsOf, now_());
  }
  error_message_.clear();
  reload_error_message_ = QString::fromStdString(snapshot.evaluation_error);
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

void UpdatesModel::setIconResolver(PackageIconResolver* resolver) {
  if (icon_resolver_) {
    disconnect(icon_resolver_, nullptr, this, nullptr);
  }
  icon_resolver_ = resolver;
  if (resolver != nullptr) {
    connect(resolver, &PackageIconResolver::iconsChanged, this, [this] {
      if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), {IconNameRole, IconSourceRole});
      }
    });
  }
}
