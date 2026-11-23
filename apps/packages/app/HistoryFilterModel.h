#pragma once

#include <QModelIndex>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

// The package-name filter and the selection of the History page (transaction-history DESIGN 5.7). The selection
// identity is the entry's `sequence`, so new rows inserted at the top keep the selected entry selected.
class HistoryFilterModel : public QSortFilterProxyModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
  Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)
  Q_PROPERTY(QVariantMap currentEntry READ currentEntry NOTIFY currentEntryChanged)
  Q_PROPERTY(int count READ count NOTIFY countChanged)

 public:
  explicit HistoryFilterModel(QObject* parent = nullptr);

  void setSourceModel(QAbstractItemModel* source_model) override;

  [[nodiscard]] QString searchText() const;
  void setSearchText(const QString& text);
  [[nodiscard]] int currentRow() const;
  void setCurrentRow(int row);
  [[nodiscard]] QVariantMap currentEntry() const;
  // The number of rows that pass the filter.
  [[nodiscard]] int count() const;

  Q_INVOKABLE [[nodiscard]] QVariantMap get(int row) const;

 signals:
  void searchTextChanged();
  void currentRowChanged();
  void currentEntryChanged();
  void countChanged();

 protected:
  [[nodiscard]] bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

 private:
  void reconcileCurrentRow();

  QString search_text_;
  std::optional<quint32> active_sequence_;
  int current_row_ = -1;
  int count_ = 0;
  QVariantMap current_entry_;
  bool filter_change_in_progress_ = false;
};
