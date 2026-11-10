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

// GUI-owned cache. Workers read local metadata only; role reads are hash lookups.
class PackageIconResolver : public QObject {
  Q_OBJECT

 public:
  // A non-null catalog directory replaces system locations (isolated fixtures).
  explicit PackageIconResolver(QObject* parent = nullptr, QString catalogDirectory = {});
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
  void reload();
  static Icons load(const std::vector<holonight_packages_domain::Package>& packages, const QString& catalogDirectory);
  QFutureWatcher<Icons> watcher_;
  QString catalog_directory_;
  std::vector<holonight_packages_domain::Package> packages_;
  Icons icons_;
  bool loading_ = false;
  bool pending_ = false;
};
