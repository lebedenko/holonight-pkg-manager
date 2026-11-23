#pragma once

#include <QDBusMessage>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

class DBusOwnerTracker;

// A property subscription and its initial GetAll for one owner generation.
class DBusUpdateSubscription : public QObject {
  Q_OBJECT

 public:
  using Apply = std::function<void(const QVariantMap&)>;
  DBusUpdateSubscription(const DBusOwnerTracker& tracker, QString path, QString interface, Apply apply,
                         std::function<void()> completed = {});
  ~DBusUpdateSubscription() override;
  DBusUpdateSubscription(const DBusUpdateSubscription&) = delete;
  DBusUpdateSubscription(DBusUpdateSubscription&&) = delete;
  DBusUpdateSubscription& operator=(const DBusUpdateSubscription&) = delete;
  DBusUpdateSubscription& operator=(DBusUpdateSubscription&&) = delete;

 private slots:
  void onPropertiesChanged(const QString& interface, const QVariantMap& changes, const QStringList& invalidated,
                           const QDBusMessage& message);
  void onStatusChanged(const QDBusMessage& message);

 private:
  const DBusOwnerTracker& tracker_;
  const quint64 generation_;
  const QString owner_;
  const QString path_;
  const QString interface_;
  Apply apply_;
  std::function<void()> completed_;
  QVariantMap initial_changes_;
  bool initializing_ = true;
};
