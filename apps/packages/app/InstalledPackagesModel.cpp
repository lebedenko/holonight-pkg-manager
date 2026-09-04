#include "InstalledPackagesModel.h"

#include "holonight_packages_domain/require_non_null.h"

#include <QtConcurrentRun>

#include <exception>

namespace {

QString sourceLabel(holonight_packages_domain::SourceType source_type) {
  return source_type == holonight_packages_domain::SourceType::Official ? QStringLiteral("official")
                                                                        : QStringLiteral("foreign");
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
  const holonight_packages_domain::Package& package = packages_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case NameRole:
      return QString::fromStdString(package.name);
    case InstalledVersionRole:
      return QString::fromStdString(package.installedVersion);
    case SourceLabelRole:
      return sourceLabel(package.sourceType);
    case RepositoryRole:
      return QString::fromStdString(package.repository);
  }
  return {};
}

QHash<int, QByteArray> InstalledPackagesModel::roleNames() const {
  static const QHash<int, QByteArray> role_names{
      {NameRole, QByteArrayLiteral("name")},
      {InstalledVersionRole, QByteArrayLiteral("installedVersion")},
      {SourceLabelRole, QByteArrayLiteral("sourceLabel")},
      {RepositoryRole, QByteArrayLiteral("repository")},
  };
  return role_names;
}

InstalledPackagesModel::Status InstalledPackagesModel::status() const { return status_; }

QString InstalledPackagesModel::errorMessage() const { return error_message_; }

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
          .code = holonight_packages_domain::PackageSourceErrorCode::Unknown, .message = exception.what()});
    } catch (...) {
      return std::unexpected(holonight_packages_domain::PackageSourceError{
          .code = holonight_packages_domain::PackageSourceErrorCode::Unknown,
          .message = "Unknown package enumeration failure"});
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
  } else {
    beginResetModel();
    packages_.clear();
    endResetModel();
    status_ = Status::Error;
    error_message_ = QString::fromStdString(result.error().message);
  }
  load_in_progress_ = false;
  emit statusChanged();
}
