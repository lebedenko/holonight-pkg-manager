#include "ExploreModel.h"

#include "DataFreshness.h"
#include "holonight_packages_application/data_freshness.h"
#include "holonight_packages_application/package_size_formatter.h"
#include "holonight_packages_domain/require_non_null.h"

#include <QtConcurrentRun>

#include <exception>
#include <string>
#include <utility>

namespace {

using holonight_packages_application::ExploreIndex;
using holonight_packages_domain::ExploreSourceError;
using holonight_packages_domain::ExploreSourceErrorCode;
using holonight_packages_domain::SyncPackage;

QString sizeLabel(std::uint64_t bytes) {
  return QString::fromStdString(holonight_packages_application::formatSizeBytes(bytes));
}

QStringList toQStringList(const std::vector<std::string>& values) {
  QStringList list;
  list.reserve(static_cast<qsizetype>(values.size()));
  for (const std::string& value : values) {
    list.append(QString::fromStdString(value));
  }
  return list;
}

QString installedBadgeText(const SyncPackage& package) {
  if (!package.installedVersion.has_value()) {
    return {};
  }
  if (*package.installedVersion == package.version) {
    return ExploreModel::tr("Installed");
  }
  return ExploreModel::tr("Installed %1").arg(QString::fromStdString(*package.installedVersion));
}

}  // namespace

ExploreModel::ExploreModel(std::shared_ptr<holonight_packages_domain::ExploreSource> source, QObject* parent, Clock now,
                           std::chrono::milliseconds debounce)
    : QAbstractListModel(parent),
      source_(holonight_packages_domain::requireNonNull(std::move(source), "ExploreModel requires an explore source")),
      now_(std::move(now)) {
  search_timer_.setSingleShot(true);
  search_timer_.setInterval(debounce);
  connect(&search_timer_, &QTimer::timeout, this, [this] {
    if (index_) {
      runQuery();
    }
  });
  connect(&watcher_, &QFutureWatcher<LoadResult>::finished, this, &ExploreModel::onLoadFinished);
  startLoading();
}

ExploreModel::~ExploreModel() = default;

int ExploreModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(results_.size());
}

QVariant ExploreModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) {
    return {};
  }
  const SyncPackage* package = packageAt(index.row());
  if (package == nullptr) {
    return {};
  }
  switch (role) {
    case NameRole:
      return QString::fromStdString(package->name);
    case AvailableVersionRole:
      return QString::fromStdString(package->version);
    case RepositoryRole:
      return QString::fromStdString(package->repository);
    case DescriptionRole:
      return QString::fromStdString(package->description);
    case DownloadSizeRole:
      return QVariant::fromValue<quint64>(package->downloadSizeBytes);
    case DownloadSizeLabelRole:
      return sizeLabel(package->downloadSizeBytes);
    case InstalledSizeRole:
      return QVariant::fromValue<quint64>(package->installedSizeBytes);
    case InstalledSizeLabelRole:
      return sizeLabel(package->installedSizeBytes);
    case UrlRole:
      return QString::fromStdString(package->url);
    case LicensesRole:
      return toQStringList(package->licenses);
    case DependenciesRole:
      return toQStringList(package->dependencies);
    case OptionalDependenciesRole:
      return toQStringList(package->optionalDependencies);
    case IsInstalledRole:
      return package->installedVersion.has_value();
    case InstalledVersionRole:
      return package->installedVersion.has_value() ? QString::fromStdString(*package->installedVersion) : QString();
    case InstalledBadgeTextRole:
      return installedBadgeText(*package);
    case InstalledVersionDiffersRole:
      return package->installedVersion.has_value() && *package->installedVersion != package->version;
  }
  return {};
}

QHash<int, QByteArray> ExploreModel::roleNames() const {
  static const QHash<int, QByteArray> role_names{
      {NameRole, QByteArrayLiteral("name")},
      {AvailableVersionRole, QByteArrayLiteral("availableVersion")},
      {RepositoryRole, QByteArrayLiteral("repository")},
      {DescriptionRole, QByteArrayLiteral("description")},
      {DownloadSizeRole, QByteArrayLiteral("downloadSize")},
      {DownloadSizeLabelRole, QByteArrayLiteral("downloadSizeLabel")},
      {InstalledSizeRole, QByteArrayLiteral("installedSize")},
      {InstalledSizeLabelRole, QByteArrayLiteral("installedSizeLabel")},
      {UrlRole, QByteArrayLiteral("url")},
      {LicensesRole, QByteArrayLiteral("licenses")},
      {DependenciesRole, QByteArrayLiteral("dependencies")},
      {OptionalDependenciesRole, QByteArrayLiteral("optionalDependencies")},
      {IsInstalledRole, QByteArrayLiteral("isInstalled")},
      {InstalledVersionRole, QByteArrayLiteral("installedVersion")},
      {InstalledBadgeTextRole, QByteArrayLiteral("installedBadgeText")},
      {InstalledVersionDiffersRole, QByteArrayLiteral("installedVersionDiffers")},
  };
  return role_names;
}

ExploreModel::ViewState ExploreModel::state() const { return state_; }

bool ExploreModel::loading() const { return loading_; }

bool ExploreModel::searchEnabled() const { return index_ != nullptr; }

QString ExploreModel::errorMessage() const { return error_message_; }

QString ExploreModel::reloadErrorMessage() const { return reload_error_message_; }

QString ExploreModel::searchText() const { return search_text_; }

int ExploreModel::matchCount() const { return static_cast<int>(match_count_); }

QString ExploreModel::footerText() const {
  if (match_count_ <= results_.size()) {
    return {};
  }
  return tr("Showing first %1 of %2 matches, refine your search to find what you're looking for.")
      .arg(results_.size())
      .arg(match_count_);
}

QString ExploreModel::noMatchesText() const {
  return tr("No packages match '%1'. Note: Configured sync repositories are searched; AUR and local-only packages "
            "are not covered.")
      .arg(applied_query_);
}

QDateTime ExploreModel::dataAsOf() const { return data_as_of_; }

QString ExploreModel::dataAsOfLabel() const { return data_freshness::dataAsOfLabel(data_as_of_); }

bool ExploreModel::databasesStale() const { return databases_stale_; }

QString ExploreModel::staleHintText() { return data_freshness::staleHintText(); }

QString ExploreModel::repositoryScopeNote() {
  return tr("Configured sync repositories are searched; AUR and local-only packages are not covered.");
}

int ExploreModel::currentRow() const { return current_row_; }

QVariantMap ExploreModel::currentPackage() const { return current_package_; }

int ExploreModel::searchCount() const { return search_count_; }

void ExploreModel::setSearchText(const QString& text) {
  if (text == search_text_) {
    return;
  }
  search_text_ = text;
  emit searchTextChanged();
  search_timer_.start();
}

void ExploreModel::setCurrentRow(int row) { setSelection(packageAt(row) != nullptr ? row : -1); }

void ExploreModel::reload() {
  if (loading_) {
    return;
  }
  startLoading();
}

// Same mechanism as UpdatesModel: QtConcurrent::run keeps the libalpm read and the index build off the GUI thread and
// QFutureWatcher delivers the result back on it, so all model state is only ever touched from the GUI thread.
void ExploreModel::startLoading() {
  loading_ = true;
  if (!index_) {
    state_ = ViewState::Loading;
  }
  watcher_.setFuture(QtConcurrent::run([source = source_] -> LoadResult {
    try {
      auto snapshot = source->loadPackages();
      if (!snapshot.has_value()) {
        return std::unexpected(std::move(snapshot.error()));
      }
      return LoadedIndex{.index = snapshot->databasesFound
                                      ? std::make_shared<const ExploreIndex>(std::move(snapshot->packages))
                                      : nullptr,
                         .databasesFound = snapshot->databasesFound,
                         .dataAsOf = snapshot->dataAsOf};
    } catch (const std::exception& exception) {
      return std::unexpected(ExploreSourceError{.code = ExploreSourceErrorCode::Unknown, .message = exception.what()});
    } catch (...) {
      return std::unexpected(
          ExploreSourceError{.code = ExploreSourceErrorCode::Unknown, .message = "Unknown package load failure"});
    }
  }));
  emit stateChanged();
}

void ExploreModel::onLoadFinished() {
  LoadResult result = watcher_.future().takeResult();
  loading_ = false;
  if (result.has_value()) {
    applyLoaded(std::move(*result));
  } else {
    applyFailure(result.error());
  }
  emit stateChanged();
}

void ExploreModel::applyLoaded(LoadedIndex loaded) {
  error_message_.clear();
  reload_error_message_.clear();
  search_timer_.stop();
  if (!loaded.databasesFound) {
    setResults(nullptr, {});
    match_count_ = 0;
    applied_query_.clear();
    state_ = ViewState::NoDatabases;
    data_as_of_ = QDateTime();
    databases_stale_ = false;
    return;
  }
  data_as_of_ = data_freshness::toQDateTime(loaded.dataAsOf);
  databases_stale_ = holonight_packages_application::isStale(loaded.dataAsOf, now_());
  runQuery(std::move(loaded.index));
}

void ExploreModel::applyFailure(const ExploreSourceError& error) {
  const QString reason = QString::fromStdString(error.message);
  if (index_) {
    // An index exists: keep rows, timestamp and state; only report the failed reload.
    reload_error_message_ = reason;
    return;
  }
  search_timer_.stop();
  setResults(nullptr, {});
  match_count_ = 0;
  applied_query_.clear();
  state_ = ViewState::Error;
  error_message_ = reason;
  reload_error_message_.clear();
  data_as_of_ = QDateTime();
  databases_stale_ = false;
}

void ExploreModel::runQuery(std::shared_ptr<const ExploreIndex> replacement) {
  search_timer_.stop();
  const auto active_index = replacement ? std::move(replacement) : index_;
  const QString trimmed = search_text_.trimmed();
  applied_query_ = trimmed;
  if (trimmed.isEmpty()) {
    match_count_ = 0;
    setResults(active_index, {});
    state_ = ViewState::Hint;
  } else {
    ++search_count_;
    auto found = holonight_packages_application::searchPackages(*active_index, trimmed.toStdString(),
                                                                holonight_packages_application::kMaxSearchResults);
    match_count_ = found.totalMatches;
    setResults(active_index, std::move(found.rows));
    state_ = match_count_ > 0 ? ViewState::Results : ViewState::NoMatches;
  }
  emit stateChanged();
}

void ExploreModel::setResults(std::shared_ptr<const ExploreIndex> index, std::vector<std::uint32_t> rows) {
  beginResetModel();
  index_ = std::move(index);
  results_ = std::move(rows);
  endResetModel();
  reconcileSelection();
}

void ExploreModel::reconcileSelection() {
  if (selected_identity_.isEmpty()) {
    return;
  }
  for (int row = 0; std::cmp_less(row, results_.size()); ++row) {
    if (identityAt(row) == selected_identity_) {
      const bool moved = row != current_row_;
      current_row_ = row;
      current_package_ = packageMap(row);
      if (moved) {
        emit currentRowChanged();
      }
      emit currentPackageChanged();
      return;
    }
  }
  setSelection(-1);
}

void ExploreModel::setSelection(int row) {
  const QString identity = row >= 0 ? identityAt(row) : QString();
  const bool row_changed = row != current_row_;
  const bool package_changed = identity != selected_identity_;
  current_row_ = row;
  selected_identity_ = identity;
  current_package_ = row >= 0 ? packageMap(row) : QVariantMap();
  if (row_changed) {
    emit currentRowChanged();
  }
  if (package_changed || row >= 0) {
    emit currentPackageChanged();
  }
}

const SyncPackage* ExploreModel::packageAt(int row) const {
  if (!index_ || row < 0 || static_cast<std::size_t>(row) >= results_.size()) {
    return nullptr;
  }
  return &index_->packages()[results_[static_cast<std::size_t>(row)]];
}

QVariantMap ExploreModel::packageMap(int row) const {
  QVariantMap map;
  const QModelIndex model_index = index(row);
  const auto names = roleNames();
  for (auto it = names.cbegin(); it != names.cend(); ++it) {
    map.insert(QString::fromUtf8(it.value()), data(model_index, it.key()));
  }
  return map;
}

QString ExploreModel::identityAt(int row) const {
  const SyncPackage* package = packageAt(row);
  if (package == nullptr) {
    return {};
  }
  return QString::fromStdString(package->repository + "/" + package->name);
}
