#include "InstalledPackagesModel.h"

#include "holonight_packages_application/orphan_package_filter.h"
#include "holonight_packages_application/package_size_formatter.h"
#include "holonight_packages_domain/require_non_null.h"

#include <QDateTime>
#include <QStringList>
#include <QtConcurrentRun>

#include <chrono>
#include <exception>

namespace {

using holonight_packages_domain::InstallReason;
using holonight_packages_domain::Package;
using holonight_packages_domain::SourceType;

QString sourceLabel(SourceType source_type) {
  return source_type == SourceType::Official ? QStringLiteral("official") : QStringLiteral("foreign");
}

QString installReasonLabel(InstallReason install_reason) {
  return install_reason == InstallReason::Explicit ? QStringLiteral("explicit") : QStringLiteral("dependency");
}

QDateTime toQDateTime(const std::chrono::system_clock::time_point& time_point) {
  return QDateTime::fromSecsSinceEpoch(
      std::chrono::duration_cast<std::chrono::seconds>(time_point.time_since_epoch()).count());
}

QStringList toQStringList(const std::vector<std::string>& values) {
  QStringList result;
  result.reserve(static_cast<qsizetype>(values.size()));
  for (const std::string& value : values) {
    result.append(QString::fromStdString(value));
  }
  return result;
}

}  // namespace

InstalledPackagesModel::InstalledPackagesModel(
    std::shared_ptr<holonight_packages_application::PackageListUseCase> use_case, QObject* parent)
    : QAbstractListModel(parent),
      use_case_(holonight_packages_domain::requireNonNull(std::move(use_case),
                                                          "InstalledPackagesModel requires a package list use case")) {
  connect(&watcher_, &QFutureWatcher<LoadResult>::finished, this, &InstalledPackagesModel::onEnumerationFinished);
  startLoading();
}

InstalledPackagesModel::~InstalledPackagesModel() = default;

int InstalledPackagesModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(packages_.size());
}

QVariant InstalledPackagesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= packages_.size()) {
    return {};
  }
  const Package& package = packages_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case NameRole:
      return QString::fromStdString(package.name);
    case InstalledVersionRole:
      return QString::fromStdString(package.installedVersion);
    case SourceLabelRole:
      return sourceLabel(package.sourceType);
    case RepositoryRole:
      return QString::fromStdString(package.repository);
    case InstallReasonRole:
      return installReasonLabel(package.installReason);
    case SizeRole:
      return QVariant::fromValue<quint64>(package.sizeBytes);
    case SizeLabelRole:
      return QString::fromStdString(holonight_packages_application::formatSizeBytes(package.sizeBytes));
    case DescriptionRole:
      return QString::fromStdString(package.description);
    case InstallDateRole:
      return toQDateTime(package.installDate);
    case RequiredByCountRole:
      return static_cast<int>(package.requiredBy.size());
    case RequiredByListRole:
      return toQStringList(package.requiredBy);
    case OptionalDependenciesRole:
      return toQStringList(package.optionalDependencies);
    case ConfigFileCountRole:
      return static_cast<int>(package.configFileCount);
    case IsOrphanRole:
      return holonight_packages_application::isOrphan(package);
  }
  return {};
}

QHash<int, QByteArray> InstalledPackagesModel::roleNames() const {
  static const QHash<int, QByteArray> role_names{
      {NameRole, QByteArrayLiteral("name")},
      {InstalledVersionRole, QByteArrayLiteral("installedVersion")},
      {SourceLabelRole, QByteArrayLiteral("sourceLabel")},
      {RepositoryRole, QByteArrayLiteral("repository")},
      {InstallReasonRole, QByteArrayLiteral("installReason")},
      {SizeRole, QByteArrayLiteral("size")},
      {SizeLabelRole, QByteArrayLiteral("sizeLabel")},
      {DescriptionRole, QByteArrayLiteral("description")},
      {InstallDateRole, QByteArrayLiteral("installDate")},
      {RequiredByCountRole, QByteArrayLiteral("requiredByCount")},
      {RequiredByListRole, QByteArrayLiteral("requiredByList")},
      {OptionalDependenciesRole, QByteArrayLiteral("optionalDependencies")},
      {ConfigFileCountRole, QByteArrayLiteral("configFileCount")},
      {IsOrphanRole, QByteArrayLiteral("isOrphan")},
  };
  return role_names;
}

InstalledPackagesModel::Status InstalledPackagesModel::status() const { return status_; }

QString InstalledPackagesModel::errorMessage() const { return error_message_; }

int InstalledPackagesModel::totalPackageCount() const { return aggregates_.total_package_count; }

quint64 InstalledPackagesModel::totalInstalledSizeBytes() const { return aggregates_.total_installed_size_bytes; }

int InstalledPackagesModel::explicitPackageCount() const { return aggregates_.explicit_package_count; }

int InstalledPackagesModel::dependencyPackageCount() const { return aggregates_.dependency_package_count; }

int InstalledPackagesModel::foreignPackageCount() const { return aggregates_.foreign_package_count; }

int InstalledPackagesModel::orphanPackageCount() const { return aggregates_.orphan_package_count; }

quint64 InstalledPackagesModel::reclaimableSizeBytes() const { return aggregates_.reclaimable_size_bytes; }

QString InstalledPackagesModel::formatSize(quint64 bytes) {
  return QString::fromStdString(holonight_packages_application::formatSizeBytes(bytes));
}

void InstalledPackagesModel::refresh() {
  if (load_in_progress_) {
    return;
  }
  startLoading();
}

void InstalledPackagesModel::startLoading() {
  load_in_progress_ = true;
  status_ = Status::Loading;
  watcher_.setFuture(QtConcurrent::run([use_case = use_case_] -> LoadResult {
    try {
      return use_case->enumerateInstalledPackages();
    } catch (const std::exception& exception) {
      return std::unexpected(holonight_packages_domain::PackageSourceError{
          .code = holonight_packages_domain::PackageSourceErrorCode::Unknown,
          .message = exception.what(),
      });
    } catch (...) {
      return std::unexpected(holonight_packages_domain::PackageSourceError{
          .code = holonight_packages_domain::PackageSourceErrorCode::Unknown,
          .message = "Unknown package enumeration failure",
      });
    }
  }));
  emit statusChanged();
}

void InstalledPackagesModel::onEnumerationFinished() {
  LoadResult result = watcher_.future().takeResult();
  if (result.has_value()) {
    beginResetModel();
    packages_ = std::move(*result);
    endResetModel();
    status_ = Status::Loaded;
    error_message_.clear();
    recomputeAggregates();
  } else {
    beginResetModel();
    packages_.clear();
    endResetModel();
    status_ = Status::Error;
    error_message_ = QString::fromStdString(result.error().message);
    aggregates_ = Aggregates{};
  }
  load_in_progress_ = false;
  emit statusChanged();
}

void InstalledPackagesModel::recomputeAggregates() {
  Aggregates aggregates;
  aggregates.total_package_count = static_cast<int>(packages_.size());
  for (const Package& package : packages_) {
    aggregates.total_installed_size_bytes += package.sizeBytes;
    if (package.installReason == InstallReason::Explicit) {
      ++aggregates.explicit_package_count;
    } else {
      ++aggregates.dependency_package_count;
    }
    if (package.sourceType == SourceType::Foreign) {
      ++aggregates.foreign_package_count;
    }
  }
  const holonight_packages_application::OrphanStatistics orphan_statistics =
      holonight_packages_application::computeOrphanStatistics(packages_);
  aggregates.orphan_package_count = orphan_statistics.orphanPackageCount;
  aggregates.reclaimable_size_bytes = orphan_statistics.reclaimableSizeBytes;
  aggregates_ = aggregates;
}
