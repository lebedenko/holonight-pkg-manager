#include "HistoryFilterModel.h"

#include "HistoryModel.h"

#include <QByteArray>
#include <QHash>
#include <QScopedValueRollback>
#include <QStringList>
#include <Qt>

#include <algorithm>

HistoryFilterModel::HistoryFilterModel(QObject* parent) : QSortFilterProxyModel(parent) {
  setDynamicSortFilter(true);
  setFilterRole(HistoryModel::PackageNamesRole);
  search_debounce_.setSingleShot(true);
  search_debounce_.setTimerType(Qt::PreciseTimer);
  search_debounce_.setInterval(150);
  connect(&search_debounce_, &QTimer::timeout, this, [this] {
    {
      const QScopedValueRollback<bool> guard(filter_change_in_progress_, true);
      applied_search_ = search_text_;
      // A search can change thousands of disjoint rows. Publish one layout change for the new mapping.
      invalidate();
    }
    reconcileCurrentRow();
  });
  connect(this, &QAbstractItemModel::modelReset, this, [this] {
    active_sequence_.reset();
    reconcileCurrentRow();
  });
  connect(this, &QAbstractItemModel::layoutChanged, this, &HistoryFilterModel::reconcileCurrentRow);
  connect(this, &QAbstractItemModel::rowsInserted, this, &HistoryFilterModel::reconcileCurrentRow);
  connect(this, &QAbstractItemModel::rowsRemoved, this, &HistoryFilterModel::reconcileCurrentRow);
  connect(this, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex& first, const QModelIndex& last) {
    if (current_row_ >= first.row() && current_row_ <= last.row()) {
      setCurrentRow(current_row_);
    }
  });
}

void HistoryFilterModel::setSourceModel(QAbstractItemModel* source_model) {
  if (sourceModel() == source_model) {
    return;
  }
  QSortFilterProxyModel::setSourceModel(source_model);
  reconcileCurrentRow();
}

QString HistoryFilterModel::searchText() const { return search_text_; }

void HistoryFilterModel::setSearchText(const QString& text) {
  if (search_text_ == text) {
    return;
  }
  search_text_ = text;
  if (search_text_ == applied_search_) {
    search_debounce_.stop();
  } else {
    search_debounce_.start();
  }
  emit searchTextChanged();
}

int HistoryFilterModel::currentRow() const { return current_row_; }

QVariantMap HistoryFilterModel::currentEntry() const { return current_entry_; }

int HistoryFilterModel::count() const { return count_; }

void HistoryFilterModel::setCurrentRow(int row) {
  const QModelIndex proxy_index = index(row, 0);
  const int next_row = proxy_index.isValid() ? row : -1;
  const QVariantMap next_entry = get(next_row);
  const bool row_changed = current_row_ != next_row;
  const bool entry_changed = current_entry_ != next_entry;
  current_row_ = next_row;
  current_entry_ = next_entry;
  active_sequence_ = proxy_index.isValid()
                         ? std::optional<quint32>(proxy_index.data(HistoryModel::SequenceRole).value<quint32>())
                         : std::nullopt;
  if (row_changed) {
    emit currentRowChanged();
  }
  if (entry_changed) {
    emit currentEntryChanged();
  }
}

QVariantMap HistoryFilterModel::get(int row) const {
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

bool HistoryFilterModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const {
  if (applied_search_.isEmpty()) {
    return true;
  }
  const QAbstractItemModel* source = sourceModel();
  if (source == nullptr) {
    return false;
  }
  const QStringList names =
      source->index(source_row, 0, source_parent).data(HistoryModel::PackageNamesRole).toStringList();
  // A case-insensitive substring of any package name (REQ-F-029, OQ-H4).
  return std::ranges::any_of(
      names, [this](const QString& name) { return name.contains(applied_search_, Qt::CaseInsensitive); });
}

void HistoryFilterModel::reconcileCurrentRow() {
  // Publish the selection once, after a complete filter change.
  if (filter_change_in_progress_) {
    return;
  }
  if (rowCount() != count_) {
    count_ = rowCount();
    emit countChanged();
  }
  if (active_sequence_ && sourceModel() != nullptr) {
    const auto source_row = sourceModel()->rowCount() - 1 - static_cast<qint64>(*active_sequence_);
    if (source_row >= 0) {
      const auto selected = mapFromSource(sourceModel()->index(static_cast<int>(source_row), 0));
      if (selected.isValid()) {
        setCurrentRow(selected.row());
        return;
      }
    }
  }
  setCurrentRow(rowCount() > 0 ? 0 : -1);
}
