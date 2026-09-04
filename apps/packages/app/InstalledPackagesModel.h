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

 public:
  enum class Status : std::uint8_t { Loading, Loaded, Error };
  Q_ENUM(Status)

  enum Role : std::uint16_t { NameRole = Qt::UserRole + 1, InstalledVersionRole, SourceLabelRole, RepositoryRole };

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

  Q_INVOKABLE void refresh();

 signals:
  void statusChanged();

 private:
  using LoadResult =
      std::expected<std::vector<holonight_packages_domain::Package>, holonight_packages_domain::PackageSourceError>;

  void startLoading();
  void onEnumerationFinished();

  std::shared_ptr<holonight_packages_application::PackageListUseCase> use_case_;
  QFutureWatcher<LoadResult> watcher_;
  std::vector<holonight_packages_domain::Package> packages_;
  bool load_in_progress_ = false;
  Status status_ = Status::Loading;
  QString error_message_;
};
