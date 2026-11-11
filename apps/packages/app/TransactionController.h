#pragma once

#include "TransactionRowModel.h"
#include "holonight_packages_application/package_list_refresh.h"
#include "holonight_packages_application/transaction_flow.h"
#include "holonight_packages_domain/transaction_service.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

// The transaction view-model (SPEC glossary): it owns a TransactionFlow, turns its state into QML properties and
// texts, and owns the row models of the dialogs. It holds no D-Bus type: everything goes through the port.
class TransactionController : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(TransactionController)
  QML_UNCREATABLE("TransactionController is provided by the application")

  Q_PROPERTY(State state READ state NOTIFY changed)
  Q_PROPERTY(Page page READ page NOTIFY changed)
  Q_PROPERTY(bool dialogOpen READ dialogOpen NOTIFY changed)
  Q_PROPERTY(int openDialogCount READ openDialogCount NOTIFY changed)
  Q_PROPERTY(bool allowAction READ allowAction NOTIFY changed)
  Q_PROPERTY(bool closable READ closable NOTIFY changed)
  Q_PROPERTY(QString actionKind READ actionKind NOTIFY changed)
  Q_PROPERTY(QString packageName READ packageName NOTIFY changed)
  Q_PROPERTY(QString title READ title NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(QString noticeText READ noticeText NOTIFY changed)
  Q_PROPERTY(QStringList actions READ actions NOTIFY changed)
  Q_PROPERTY(QString confirmLabel READ confirmLabel NOTIFY changed)
  Q_PROPERTY(bool confirmEnabled READ confirmEnabled NOTIFY changed)
  Q_PROPERTY(QDateTime expiresAt READ expiresAt NOTIFY changed)
  Q_PROPERTY(QString expiresText READ expiresText NOTIFY changed)
  Q_PROPERTY(TransactionRowModel* previewRows READ previewRows CONSTANT)
  Q_PROPERTY(TransactionRowModel* hookRows READ hookRows CONSTANT)
  Q_PROPERTY(TransactionRowModel* resultRows READ resultRows CONSTANT)
  Q_PROPERTY(bool hasHookWarning READ hasHookWarning NOTIFY changed)
  Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY changed)
  Q_PROPERTY(QString currentItem READ currentItem NOTIFY changed)
  Q_PROPERTY(qreal progressValue READ progressValue NOTIFY changed)
  Q_PROPERTY(bool progressIndeterminate READ progressIndeterminate NOTIFY changed)
  Q_PROPERTY(bool cancelEnabled READ cancelEnabled NOTIFY changed)
  Q_PROPERTY(QString cancelLabel READ cancelLabel NOTIFY changed)

 public:
  // Mirrors holonight_packages_application::FlowState one to one (REQ-F-007).
  enum class State : quint8 {
    Idle,
    Preparing,
    Previewed,
    Refused,
    Authorizing,
    Executing,
    Succeeded,
    Failed,
    Cancelled,
    Unknown,
    HelperUnavailable,
  };
  Q_ENUM(State)

  enum class Page : quint8 { None, Preparing, Confirm, Progress, Result };
  Q_ENUM(Page)

  TransactionController(holonight_packages_domain::TransactionService& service,
                        holonight_packages_application::PackageListRefresh& refresh, QObject* parent = nullptr);
  ~TransactionController() override;

  TransactionController(const TransactionController&) = delete;
  TransactionController& operator=(const TransactionController&) = delete;
  TransactionController(TransactionController&&) = delete;
  TransactionController& operator=(TransactionController&&) = delete;

  [[nodiscard]] State state() const;
  [[nodiscard]] Page page() const;
  [[nodiscard]] bool dialogOpen() const;
  [[nodiscard]] int openDialogCount() const;
  [[nodiscard]] bool allowAction() const;
  [[nodiscard]] bool closable() const;
  [[nodiscard]] QString actionKind() const;
  [[nodiscard]] QString packageName() const;
  [[nodiscard]] QString title() const;
  [[nodiscard]] QString message() const;
  [[nodiscard]] QString noticeText() const;
  [[nodiscard]] QStringList actions() const;
  [[nodiscard]] QString confirmLabel() const;
  [[nodiscard]] bool confirmEnabled() const;
  [[nodiscard]] QDateTime expiresAt() const;
  [[nodiscard]] QString expiresText() const;
  [[nodiscard]] TransactionRowModel* previewRows() const { return preview_rows_; }
  [[nodiscard]] TransactionRowModel* hookRows() const { return hook_rows_; }
  [[nodiscard]] TransactionRowModel* resultRows() const { return result_rows_; }
  [[nodiscard]] bool hasHookWarning() const;
  [[nodiscard]] QString phaseLabel() const;
  [[nodiscard]] QString currentItem() const;
  [[nodiscard]] qreal progressValue() const;
  [[nodiscard]] bool progressIndeterminate() const;
  [[nodiscard]] bool cancelEnabled() const;
  [[nodiscard]] QString cancelLabel() const;

  Q_INVOKABLE bool requestInstall(const QString& name);
  Q_INVOKABLE bool requestRemove(const QString& name);
  Q_INVOKABLE void confirm();
  Q_INVOKABLE void dismiss();
  Q_INVOKABLE void cancelRun();
  Q_INVOKABLE void tryAgain();

  // For WindowCloseGuard (not invokable from QML).
  [[nodiscard]] holonight_packages_application::CloseDecision handleCloseRequest();

 signals:
  void changed();
  void closeReady();

 private:
  void onFlowChanged();
  void syncRows();

  holonight_packages_domain::TransactionService& service_;
  holonight_packages_application::TransactionFlow flow_;
  TransactionRowModel* preview_rows_;
  TransactionRowModel* hook_rows_;
  TransactionRowModel* result_rows_;
};
