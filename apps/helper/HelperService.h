#pragma once

#include "holonight_packages_application/plan_service.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class QDBusServiceWatcher;

namespace holonight_helper {

class HelperAdaptor;

// Glue between the system bus and PlanService (DESIGN 4, 5): takes the calls of the adaptor, answers them with delayed
// replies, sends Progress only to the plan owner, and watches each owner so a disconnect becomes a cancel. It holds no
// package logic of its own.
class HelperService : public QObject {
  Q_OBJECT

 public:
  HelperService(QDBusConnection connection, holonight_packages_application::PlanService& plans,
                QObject* parent = nullptr);
  ~HelperService() override;
  HelperService(const HelperService&) = delete;
  HelperService& operator=(const HelperService&) = delete;
  HelperService(HelperService&&) = delete;
  HelperService& operator=(HelperService&&) = delete;

  // Claims the well-known name and exports the object. False when the name is taken or the export fails.
  [[nodiscard]] bool start();

  [[nodiscard]] bool busy() const;

  // Called by the adaptor. PreparePlan and Execute answer later through `message`; Cancel answers at once.
  void preparePlan(const QDBusMessage& message, const QString& action, const QStringList& names,
                   const QVariantMap& options);
  void execute(const QDBusMessage& message, const QString& planId);
  void cancel(const QDBusMessage& message, const QString& planId);

 private:
  void watch(const QString& unique_name);
  void sendProgress(const QString& owner, const QString& planId, const QString& phase, uint current, uint total,
                    const QString& item);
  void reply(const QDBusMessage& message, const QVariantList& values);

  QDBusConnection connection_;
  holonight_packages_application::PlanService* plans_;
  HelperAdaptor* adaptor_;
  QHash<QString, QDBusServiceWatcher*> watchers_;
};

}  // namespace holonight_helper
