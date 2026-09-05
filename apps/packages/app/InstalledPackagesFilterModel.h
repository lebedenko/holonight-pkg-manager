#pragma once

#include <QModelIndex>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <cstdint>

class InstalledPackagesFilterModel : public QSortFilterProxyModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(TabFilter tabFilter READ tabFilter WRITE setTabFilter NOTIFY tabFilterChanged)
  Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
  Q_PROPERTY(QString repositoryFilter READ repositoryFilter WRITE setRepositoryFilter NOTIFY repositoryFilterChanged)
  Q_PROPERTY(SortField sortField READ sortField WRITE setSortField NOTIFY sortFieldChanged)
  Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortDescendingChanged)
  Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)
  Q_PROPERTY(QVariantMap currentPackage READ currentPackage NOTIFY currentPackageChanged)
  Q_PROPERTY(QStringList availableRepositories READ availableRepositories NOTIFY availableRepositoriesChanged)

 public:
  enum class TabFilter : std::uint8_t { Explicit, Dependencies, Foreign, Orphans };
  Q_ENUM(TabFilter)
  enum class SortField : std::uint8_t { Name, Size };
  Q_ENUM(SortField)

  explicit InstalledPackagesFilterModel(QObject* parent = nullptr);

  void setSourceModel(QAbstractItemModel* source_model) override;

  [[nodiscard]] TabFilter tabFilter() const;
  void setTabFilter(TabFilter filter);
  [[nodiscard]] QString searchText() const;
  void setSearchText(const QString& text);
  [[nodiscard]] QString repositoryFilter() const;
  void setRepositoryFilter(const QString& repository);
  [[nodiscard]] SortField sortField() const;
  void setSortField(SortField field);
  [[nodiscard]] bool sortDescending() const;
  void setSortDescending(bool descending);
  [[nodiscard]] int currentRow() const;
  void setCurrentRow(int row);
  [[nodiscard]] QVariantMap currentPackage() const;
  [[nodiscard]] QStringList availableRepositories() const;

  Q_INVOKABLE [[nodiscard]] QVariantMap get(int row) const;

 signals:
  void tabFilterChanged();
  void searchTextChanged();
  void repositoryFilterChanged();
  void sortFieldChanged();
  void sortDescendingChanged();
  void currentRowChanged();
  void currentPackageChanged();
  void availableRepositoriesChanged();

 protected:
  [[nodiscard]] bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
  [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

 private:
  void applySortOrder();
  void reconcileCurrentRow();

  TabFilter tab_filter_ = TabFilter::Explicit;
  QString search_text_;
  QString repository_filter_;
  SortField sort_field_ = SortField::Name;
  bool sort_descending_ = false;
  QString active_package_identity_;
  int current_row_ = -1;
  QVariantMap current_package_;
  bool filter_change_in_progress_ = false;
};
