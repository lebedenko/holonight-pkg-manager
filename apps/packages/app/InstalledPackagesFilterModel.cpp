#include "InstalledPackagesFilterModel.h"

#include "InstalledPackagesModel.h"

#include <QScopedValueRollback>
#include <Qt>

namespace {

QString tabFilterReasonLabel(InstalledPackagesFilterModel::TabFilter filter) {
  return filter == InstalledPackagesFilterModel::TabFilter::Explicit ? QStringLiteral("explicit")
                                                                     : QStringLiteral("dependency");
}

}  // namespace

InstalledPackagesFilterModel::InstalledPackagesFilterModel(QObject* parent) : QSortFilterProxyModel(parent) {
  setDynamicSortFilter(true);
  connect(this, &QAbstractItemModel::modelReset, this, &InstalledPackagesFilterModel::reconcileCurrentRow);
  connect(this, &QAbstractItemModel::layoutChanged, this, &InstalledPackagesFilterModel::reconcileCurrentRow);
  connect(this, &QAbstractItemModel::rowsInserted, this, &InstalledPackagesFilterModel::reconcileCurrentRow);
  connect(this, &QAbstractItemModel::rowsRemoved, this, &InstalledPackagesFilterModel::reconcileCurrentRow);
  connect(this, &QAbstractItemModel::dataChanged, this, &InstalledPackagesFilterModel::reconcileCurrentRow);
  applySortOrder();
}

void InstalledPackagesFilterModel::setSourceModel(QAbstractItemModel* source_model) {
  if (sourceModel() == source_model) {
    return;
  }
  if (sourceModel() != nullptr) {
    disconnect(sourceModel(), &QAbstractItemModel::modelReset, this,
               &InstalledPackagesFilterModel::availableRepositoriesChanged);
  }
  QSortFilterProxyModel::setSourceModel(source_model);
  if (source_model != nullptr) {
    connect(source_model, &QAbstractItemModel::modelReset, this,
            &InstalledPackagesFilterModel::availableRepositoriesChanged);
  }
  emit availableRepositoriesChanged();
  reconcileCurrentRow();
}

InstalledPackagesFilterModel::TabFilter InstalledPackagesFilterModel::tabFilter() const { return tab_filter_; }

void InstalledPackagesFilterModel::setTabFilter(TabFilter filter) {
  if (tab_filter_ == filter) {
    return;
  }
  {
    const QScopedValueRollback<bool> guard(filter_change_in_progress_, true);
    beginFilterChange();
    tab_filter_ = filter;
    endFilterChange(Direction::Rows);
  }
  reconcileCurrentRow();
  emit tabFilterChanged();
}

QString InstalledPackagesFilterModel::searchText() const { return search_text_; }

void InstalledPackagesFilterModel::setSearchText(const QString& text) {
  if (search_text_ == text) {
    return;
  }
  {
    const QScopedValueRollback<bool> guard(filter_change_in_progress_, true);
    beginFilterChange();
    search_text_ = text;
    endFilterChange(Direction::Rows);
  }
  reconcileCurrentRow();
  emit searchTextChanged();
}

QString InstalledPackagesFilterModel::repositoryFilter() const { return repository_filter_; }

void InstalledPackagesFilterModel::setRepositoryFilter(const QString& repository) {
  if (repository_filter_ == repository) {
    return;
  }
  {
    const QScopedValueRollback<bool> guard(filter_change_in_progress_, true);
    beginFilterChange();
    repository_filter_ = repository;
    endFilterChange(Direction::Rows);
  }
  reconcileCurrentRow();
  emit repositoryFilterChanged();
}

InstalledPackagesFilterModel::SortField InstalledPackagesFilterModel::sortField() const { return sort_field_; }

void InstalledPackagesFilterModel::setSortField(SortField field) {
  if (sort_field_ == field) {
    return;
  }
  sort_field_ = field;
  // QSortFilterProxyModel::sort(column, order) is a no-op when column/order are unchanged from last
  // time, so it won't notice that lessThan()'s comparison field changed. invalidate() forces a
  // resort using the current sort order and the now-updated lessThan().
  invalidate();
  emit sortFieldChanged();
}

bool InstalledPackagesFilterModel::sortDescending() const { return sort_descending_; }

void InstalledPackagesFilterModel::setSortDescending(bool descending) {
  if (sort_descending_ == descending) {
    return;
  }
  sort_descending_ = descending;
  applySortOrder();
  emit sortDescendingChanged();
}

int InstalledPackagesFilterModel::currentRow() const { return current_row_; }

QVariantMap InstalledPackagesFilterModel::currentPackage() const { return current_package_; }

void InstalledPackagesFilterModel::setCurrentRow(int row) {
  const QModelIndex proxy_index = index(row, 0);
  const int next_row = proxy_index.isValid() ? row : -1;
  const QVariantMap next_package = get(next_row);
  const bool row_changed = current_row_ != next_row;
  const bool package_changed = current_package_ != next_package;
  current_row_ = next_row;
  current_package_ = next_package;
  active_package_identity_ = current_package_.value(QStringLiteral("name")).toString();
  if (row_changed) {
    emit currentRowChanged();
  }
  if (package_changed) {
    emit currentPackageChanged();
  }
}

QStringList InstalledPackagesFilterModel::availableRepositories() const {
  QStringList repositories;
  const QAbstractItemModel* source = sourceModel();
  if (source == nullptr) {
    return repositories;
  }
  for (int row = 0; row < source->rowCount(); ++row) {
    const QString repository = source->index(row, 0).data(InstalledPackagesModel::RepositoryRole).toString();
    if (repository.isEmpty() || repositories.contains(repository)) {
      continue;
    }
    repositories.append(repository);
  }
  repositories.sort(Qt::CaseInsensitive);
  return repositories;
}

QVariantMap InstalledPackagesFilterModel::get(int row) const {
  QVariantMap result;
  const QModelIndex proxy_index = index(row, 0);
  if (!proxy_index.isValid()) {
    return result;
  }
  const QHash<int, QByteArray> roles = roleNames();
  for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
    result.insert(QString::fromUtf8(it.value()), data(proxy_index, it.key()));
  }
  return result;
}

bool InstalledPackagesFilterModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const {
  const QAbstractItemModel* source = sourceModel();
  if (source == nullptr) {
    return false;
  }
  const QModelIndex source_index = source->index(source_row, 0, source_parent);

  bool tab_matches = false;
  switch (tab_filter_) {
    case TabFilter::Explicit:
    case TabFilter::Dependencies:
      tab_matches =
          source_index.data(InstalledPackagesModel::InstallReasonRole).toString() == tabFilterReasonLabel(tab_filter_);
      break;
    case TabFilter::Foreign:
      tab_matches = source_index.data(InstalledPackagesModel::SourceLabelRole).toString() == QStringLiteral("foreign");
      break;
    case TabFilter::Orphans:
      tab_matches = source_index.data(InstalledPackagesModel::IsOrphanRole).toBool();
      break;
  }
  if (!tab_matches) {
    return false;
  }

  if (!search_text_.isEmpty() &&
      !source_index.data(InstalledPackagesModel::NameRole).toString().contains(search_text_, Qt::CaseInsensitive)) {
    return false;
  }

  if (!repository_filter_.isEmpty() &&
      source_index.data(InstalledPackagesModel::RepositoryRole).toString() != repository_filter_) {
    return false;
  }

  return true;
}

bool InstalledPackagesFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  switch (sort_field_) {
    case SortField::Name: {
      const QString left_name = left.data(InstalledPackagesModel::NameRole).toString();
      const QString right_name = right.data(InstalledPackagesModel::NameRole).toString();
      return QString::compare(left_name, right_name, Qt::CaseInsensitive) < 0;
    }
    case SortField::Size:
      return left.data(InstalledPackagesModel::SizeRole).toULongLong() <
             right.data(InstalledPackagesModel::SizeRole).toULongLong();
  }
  return false;
}

void InstalledPackagesFilterModel::applySortOrder() {
  sort(0, sort_descending_ ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void InstalledPackagesFilterModel::reconcileCurrentRow() {
  // A filter can remove/insert thousands of separate ranges. Preserve the identity until the
  // complete filter operation finishes, then scan and publish the final selection only once.
  if (filter_change_in_progress_) {
    return;
  }
  if (!active_package_identity_.isEmpty()) {
    for (int row = 0; row < rowCount(); ++row) {
      if (index(row, 0).data(InstalledPackagesModel::NameRole).toString() == active_package_identity_) {
        setCurrentRow(row);
        return;
      }
    }
  }
  setCurrentRow(rowCount() > 0 ? 0 : -1);
}
