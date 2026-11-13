#pragma once

#include "holonight_packages_domain/package.h"

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

#include <string_view>
#include <vector>

struct PackageIcon {
  QString name;
  QUrl source;
};

// GUI-owned cache. Workers parse metadata and decode local files; role reads are hash lookups.
class PackageIconResolver : public QObject {
  Q_OBJECT

 public:
  // A non-null catalog directory replaces system locations (isolated fixtures).
  explicit PackageIconResolver(QObject* parent = nullptr, QString catalogDirectory = {});
  ~PackageIconResolver() override;

  PackageIconResolver(const PackageIconResolver&) = delete;
  PackageIconResolver& operator=(const PackageIconResolver&) = delete;
  PackageIconResolver(PackageIconResolver&&) = delete;
  PackageIconResolver& operator=(PackageIconResolver&&) = delete;

  void setInstalledPackages(const std::vector<holonight_packages_domain::Package>& packages);
  [[nodiscard]] PackageIcon icon(const QString& package, bool allowCatalog = true) const;

  [[nodiscard]] static bool isOfficialRepository(std::string_view repository);

 signals:
  void iconsChanged();

 private:
  struct Icons {
    QHash<QString, PackageIcon> installed;
    QHash<QString, PackageIcon> catalog;
  };
  struct LoadedIcons {
    QHash<QString, QList<PackageIcon>> installed;
    QHash<QString, QList<PackageIcon>> catalog;
  };
  void reload();
  static LoadedIcons load(const std::vector<holonight_packages_domain::Package>& packages,
                          const QString& catalogDirectory);
  QFutureWatcher<LoadedIcons> watcher_;
  QString catalog_directory_;
  std::vector<holonight_packages_domain::Package> packages_;
  Icons icons_;
  bool loading_ = false;
  bool pending_ = false;
};
