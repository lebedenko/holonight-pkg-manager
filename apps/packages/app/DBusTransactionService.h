#pragma once

#include "holonight_packages_domain/transaction_service.h"

#include <QDBusConnection>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>
#include <string>

// The QDBus implementation of the TransactionService port: the only code in the GUI that calls the package helper
// (REQ-F-040). Every call is asynchronous (REQ-F-043), the three calls and the Progress subscription share one bus
// connection (a plan belongs to the connection that prepared it), and every reply is decoded by helper_wire so no raw
// helper string reaches the caller.
class DBusTransactionService final : public QObject, public holonight_packages_domain::TransactionService {
  Q_OBJECT

 public:
  static constexpr const char* kServiceName = "org.holonight.PackageHelper1";
  static constexpr const char* kObjectPath = "/org/holonight/PackageHelper1";
  static constexpr const char* kInterfaceName = "org.holonight.PackageHelper1.Transactions";

  // Production defaults (REQ-F-041, REQ-F-042). Tests inject shorter values through the constructor only.
  struct Timeouts {
    int prepare_ms = 30'000;     // helper XML: "above 10 seconds"
    int execute_ms = 3'600'000;  // helper XML: "at least one hour"
    int cancel_ms = -1;          // QtDBus default (25 s); deliberately not the one-hour value
  };
  using ConnectionFactory = std::function<QDBusConnection()>;

  // Production: DBusTransactionService(factory). Tests inject shorter timeouts through the second constructor.
  explicit DBusTransactionService(ConnectionFactory connect, QObject* parent = nullptr);
  DBusTransactionService(ConnectionFactory connect, Timeouts timeouts, QObject* parent = nullptr);
  ~DBusTransactionService() override;

  DBusTransactionService(const DBusTransactionService&) = delete;
  DBusTransactionService& operator=(const DBusTransactionService&) = delete;
  DBusTransactionService(DBusTransactionService&&) = delete;
  DBusTransactionService& operator=(DBusTransactionService&&) = delete;

  void setProgressHandler(ProgressHandler handler) override;
  void preparePlan(const holonight_packages_domain::PlanRequest& request, PrepareDone done) override;
  void execute(const std::string& plan_id, ExecuteDone done) override;
  void cancel(const std::string& plan_id, CancelDone done) override;

  [[nodiscard]] const Timeouts& timeouts() const noexcept { return timeouts_; }
  // The match rule the Progress subscription installs (QtDBus does not expose the installed rule).
  [[nodiscard]] static QString progressMatchRule();

 private slots:
  void onProgress(const QString& plan_id, const QString& phase, uint current, uint total, const QString& item);

 private:
  // Connects lazily on the first use and subscribes to Progress once. Empty when no bus connection exists.
  std::optional<QDBusConnection> ensureConnection();
  [[nodiscard]] static QDBusMessage call(const QString& method);
  template <typename Reply, typename Done, typename Decode>
  void send(const QDBusMessage& message, int timeout_ms, Done done, Decode decode);
  template <typename Reply, typename Done>
  void postUnavailable(Done done);

  ConnectionFactory connect_;
  Timeouts timeouts_;
  ProgressHandler progress_;
  std::optional<QDBusConnection> bus_;
  std::string active_plan_id_;
};
