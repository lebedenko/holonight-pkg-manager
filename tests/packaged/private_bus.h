#pragma once

#include <QByteArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

namespace holonight_packages_testing {

// A throwaway dbus-daemon on a private socket, so tests never touch the real session bus.
class PrivateBus {
 public:
  PrivateBus() = default;
  PrivateBus(const PrivateBus&) = delete;
  PrivateBus& operator=(const PrivateBus&) = delete;
  PrivateBus(PrivateBus&&) = delete;
  PrivateBus& operator=(PrivateBus&&) = delete;

  ~PrivateBus() {
    daemon_.terminate();
    if (!daemon_.waitForFinished(3000)) {
      daemon_.kill();
      daemon_.waitForFinished(3000);
    }
  }

  // Environment for the daemon (and so for services it activates). Call before start(); the default is the caller's.
  void setProcessEnvironment(const QProcessEnvironment& environment) { daemon_.setProcessEnvironment(environment); }

  // Returns false (with a remediation message in error()) when dbus-daemon cannot be started.
  [[nodiscard]] bool start() {
    daemon_.setProgram(QStringLiteral("dbus-daemon"));
    daemon_.setArguments(
        {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
    daemon_.start();
    if (!daemon_.waitForStarted(5000)) {
      error_ = QStringLiteral("cannot start dbus-daemon; install the dbus package and make it available on PATH");
      return false;
    }
    if (!daemon_.waitForReadyRead(5000)) {
      error_ = QStringLiteral("dbus-daemon did not print an address");
      return false;
    }
    address_ = QString::fromUtf8(daemon_.readLine()).trimmed();
    return !address_.isEmpty();
  }

  [[nodiscard]] const QString& address() const { return address_; }
  [[nodiscard]] const QString& error() const { return error_; }

 private:
  QProcess daemon_;
  QString address_;
  QString error_;
};

}  // namespace holonight_packages_testing
