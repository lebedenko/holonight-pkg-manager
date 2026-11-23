#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>

class DBusOwnerTracker : public QObject {
  Q_OBJECT

 public:
  DBusOwnerTracker(QDBusConnection connection, const QString& service);
  [[nodiscard]] const QString& owner() const { return owner_; }
  [[nodiscard]] quint64 generation() const { return generation_; }
  [[nodiscard]] const QDBusConnection& connection() const { return connection_; }

 signals:
  void changed();

 private:
  QDBusConnection connection_;
  QDBusServiceWatcher watcher_;
  QString owner_;
  quint64 generation_ = 0;
};
