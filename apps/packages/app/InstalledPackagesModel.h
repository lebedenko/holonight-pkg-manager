#pragma once

#include "holonight_packages_application/package_list_use_case.h"
#include "holonight_packages_domain/package.h"
#include "holonight_packages_domain/package_source.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QFutureWatcher>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQmlIntegration/qqmlintegration.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

class InstalledPackagesModel : public QAbstractListModel {
  Q_OBJECT
  QML_NAMED_ELEMENT(InstalledPackagesModel)
  QML_UNCREATABLE("InstalledPackagesModel is provided by the application")
  Q_PROPERTY(Status status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)
  Q_PROPERTY(int totalPackageCount READ totalPackageCount NOTIFY statusChanged)
  Q_PROPERTY(quint64 totalInstalledSizeBytes READ totalInstalledSizeBytes NOTIFY statusChanged)
  Q_PROPERTY(int explicitPackageCount READ explicitPackageCount NOTIFY statusChanged)
  Q_PROPERTY(int dependencyPackageCount READ dependencyPackageCount NOTIFY statusChanged)
  Q_PROPERTY(int foreignPackageCount READ foreignPackageCount NOTIFY statusChanged)
  Q_PROPERTY(int orphanPackageCount READ orphanPackageCount NOTIFY statusChanged)
  Q_PROPERTY(quint64 reclaimableSizeBytes READ reclaimableSizeBytes NOTIFY statusChanged)

 public:
  enum class Status : std::uint8_t { Loading, Loaded, Error };
  Q_ENUM(Status)

  enum Role : std::uint16_t {
    NameRole = Qt::UserRole + 1,
    InstalledVersionRole,
    SourceLabelRole,
    RepositoryRole,
    InstallReasonRole,
    SizeRole,
    SizeLabelRole,
    DescriptionRole,
    InstallDateRole,
    RequiredByCountRole,
    RequiredByListRole,
    OptionalDependenciesRole,
    ConfigFileCountRole,
    IsOrphanRole,
  };

  explicit InstalledPackagesModel(std::shared_ptr<holonight_packages_application::PackageListUseCase> use_case,
                                  QObject* parent = nullptr);
  ~InstalledPackagesModel() override;

  InstalledPackagesModel(const InstalledPackagesModel&) = delete;
  InstalledPackagesModel& operator=(const InstalledPackagesModel&) = delete;
  InstalledPackagesModel(InstalledPackagesModel&&) = delete;
  InstalledPackagesModel& operator=(InstalledPackagesModel&&) = delete;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] Status status() const;
  [[nodiscard]] QString errorMessage() const;
  [[nodiscard]] int totalPackageCount() const;
  [[nodiscard]] quint64 totalInstalledSizeBytes() const;
  [[nodiscard]] int explicitPackageCount() const;
  [[nodiscard]] int dependencyPackageCount() const;
  [[nodiscard]] int foreignPackageCount() const;
  [[nodiscard]] int orphanPackageCount() const;
  [[nodiscard]] quint64 reclaimableSizeBytes() const;

  Q_INVOKABLE void refresh();
  Q_INVOKABLE static QString formatSize(quint64 bytes);

 signals:
  void statusChanged();

 private:
  using LoadResult =
      std::expected<std::vector<holonight_packages_domain::Package>, holonight_packages_domain::PackageSourceError>;

  struct Aggregates {
    int totalPackageCount = 0;
    quint64 totalInstalledSizeBytes = 0;
    int explicitPackageCount = 0;
    int dependencyPackageCount = 0;
    int foreignPackageCount = 0;
    int orphanPackageCount = 0;
    quint64 reclaimableSizeBytes = 0;
  };

  void startLoading();
  void onEnumerationFinished();
  void recomputeAggregates();

  std::shared_ptr<holonight_packages_application::PackageListUseCase> use_case_;
  QFutureWatcher<LoadResult> watcher_;
  std::vector<holonight_packages_domain::Package> packages_;
  bool load_in_progress_ = false;
  Status status_ = Status::Loading;
  QString error_message_;
  Aggregates aggregates_;
};
