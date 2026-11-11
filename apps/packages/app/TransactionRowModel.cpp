#include "TransactionRowModel.h"

#include <QVariant>

namespace {

using holonight_packages_domain::ChangeKind;

QString text(const std::string& value) { return QString::fromStdString(value); }

}  // namespace

TransactionRowModel::TransactionRowModel(QObject* parent) : QAbstractListModel(parent) {}

int TransactionRowModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant TransactionRowModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
    return {};
  }
  const Row& row = rows_.at(index.row());
  switch (role) {
    case NameRole:
      return row.name;
    case KindRole:
      return row.kind;
    case ActionRole:
      return row.action;
    case OldVersionRole:
      return row.old_version;
    case NewVersionRole:
      return row.new_version;
    case RepositoryRole:
      return row.repository;
    case ConditionalRole:
      return row.conditional;
    default:
      return {};
  }
}

QHash<int, QByteArray> TransactionRowModel::roleNames() const {
  return {
      {NameRole, "name"},
      {KindRole, "kind"},
      {ActionRole, "action"},
      {OldVersionRole, "oldVersion"},
      {NewVersionRole, "newVersion"},
      {RepositoryRole, "repository"},
      {ConditionalRole, "conditional"},
  };
}

void TransactionRowModel::setRows(QList<Row> rows) {
  if (rows == rows_) {
    return;
  }
  const bool count_changes = rows.size() != rows_.size();
  beginResetModel();
  rows_ = std::move(rows);
  endResetModel();
  if (count_changes) {
    emit countChanged();
  }
}

void TransactionRowModel::clear() { setRows({}); }

QList<TransactionRowModel::Row> TransactionRowModel::previewRows(const holonight_packages_domain::ResolvedPlan& plan) {
  QList<Row> rows;
  for (const auto& change : plan.changes) {
    QString kind = QStringLiteral("target");
    if (change.kind == ChangeKind::Dependency) {
      kind = QStringLiteral("dependency");
    } else if (change.kind == ChangeKind::Orphan) {
      kind = QStringLiteral("orphan");
    }
    // Removals (and orphans) remove; the rest install.
    const bool removes = plan.isRemoval() || change.kind == ChangeKind::Orphan;
    rows.push_back(Row{
        .name = text(change.name),
        .kind = kind,
        .action = removes ? QStringLiteral("remove") : QStringLiteral("install"),
        .old_version = text(change.old_version),
        .new_version = text(change.new_version),
        .repository = text(change.repository),
        .conditional = false,
    });
  }
  return rows;
}

QList<TransactionRowModel::Row> TransactionRowModel::hookRows(const holonight_packages_domain::ResolvedPlan& plan) {
  QList<Row> rows;
  for (const auto& hook : plan.hooks) {
    rows.push_back(Row{
        .name = text(hook.name),
        .kind = QStringLiteral("hook"),
        .action = {},
        .old_version = {},
        .new_version = {},
        .repository = {},
        .conditional = hook.conditional,
    });
  }
  return rows;
}

QList<TransactionRowModel::Row> TransactionRowModel::changedRows(
    const std::vector<holonight_packages_domain::ChangedPackage>& changed) {
  QList<Row> rows;
  for (const auto& change : changed) {
    rows.push_back(Row{
        .name = text(change.name),
        .kind = QStringLiteral("changed"),
        .action = QString::fromLatin1(holonight_packages_domain::changeActionName(change.action).data()),
        .old_version = text(change.old_version),
        .new_version = text(change.new_version),
        .repository = {},
        .conditional = false,
    });
  }
  return rows;
}
