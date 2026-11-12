#pragma once

#include "holonight_packages_domain/transaction_plan.h"
#include "holonight_packages_domain/transaction_result.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

// Rows of the transaction dialogs: the plan preview (targets, dependency installs, orphans), the hook list and the
// changed list of a result. Names and versions are plain text; the model never interprets them (REQ-F-049). The
// content is replaced as a whole (a model reset), never edited in place.
class TransactionRowModel : public QAbstractListModel {
  Q_OBJECT
  QML_NAMED_ELEMENT(TransactionRowModel)
  QML_UNCREATABLE("TransactionRowModel is provided by TransactionController")
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

 public:
  // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): preserve Qt model role constants.
  enum Role : std::uint16_t {
    NameRole = Qt::UserRole + 1,
    // "target" | "dependency" | "orphan" | "hook" | "changed", and for an upgrade (system-upgrade REQ-F-082) "upgrade"
    // | "replacement" | "newDependency" | "heldBack", in that order
    KindRole,
    ActionRole,  // "install" | "remove" ("" for hooks)
    OldVersionRole,
    NewVersionRole,
    RepositoryRole,
    ConditionalRole,
    ReplacesRole,  // a replacement: the name of the installed package it replaces
  };

  struct Row {
    QString name;
    QString kind;
    QString action;
    QString old_version;
    QString new_version;
    QString repository;
    bool conditional = false;
    QString replaces;

    bool operator==(const Row&) const = default;
  };

  explicit TransactionRowModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  void setRows(QList<Row> rows);
  void clear();
  [[nodiscard]] const QList<Row>& rows() const { return rows_; }

  // Builders from the domain values.
  [[nodiscard]] static QList<Row> previewRows(const holonight_packages_domain::ResolvedPlan& plan);
  [[nodiscard]] static QList<Row> upgradeRows(const holonight_packages_domain::UpgradeDetail& detail);
  [[nodiscard]] static QList<Row> hookRows(const holonight_packages_domain::ResolvedPlan& plan);
  [[nodiscard]] static QList<Row> changedRows(const std::vector<holonight_packages_domain::ChangedPackage>& changed);

 signals:
  void countChanged();

 private:
  QList<Row> rows_;
};
