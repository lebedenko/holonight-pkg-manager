#include "DBusCallerIdentity.h"
#include "HelperConfig.h"
#include "HelperIdentity.h"
#include "HelperService.h"
#include "HelperSignals.h"
#include "HelperTerminator.h"
#include "JournalLogger.h"
#include "PolkitAuthorizer.h"
#include "QtWorkRunner.h"
#include "holonight_packages_application/idle_monitor.h"
#include "holonight_packages_application/plan_id_generator.h"
#include "holonight_packages_application/plan_service.h"
#include "holonight_packages_application/update_check_runtime.h"
#include "holonight_packages_transaction/alpm_transaction_engine.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QTextStream>

#include <memory>

namespace {

constexpr int kExitFailure = 1;

}  // namespace

// The composition root: the only place that names the libalpm engine, the polkit authorizer and the real clock.
int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("holonight-package-helper"));
  QCoreApplication::setApplicationVersion(QStringLiteral(HOLONIGHT_PACKAGE_HELPER_VERSION));

  auto config = holonight_helper::parseHelperConfig(QCoreApplication::arguments());
  if (!config) {
    QTextStream(stderr) << "holonight-package-helper: " << QString::fromStdString(config.error()) << "\n";
    return kExitFailure;
  }

  QDBusConnection bus = QDBusConnection::systemBus();
  if (!bus.isConnected()) {
    QTextStream(stderr) << "holonight-package-helper: cannot connect to the system bus\n";
    return kExitFailure;
  }

  holonight_packages_transaction::AlpmTransactionEngine engine(config->transaction, holonight_helper::journalSink());
  holonight_helper::PolkitAuthorizer authorizer(bus);
  holonight_helper::DBusCallerIdentity identities(bus);
  holonight_packages_application::SystemPlanIdGenerator ids;
  holonight_packages_application::SystemClock clock;
  holonight_helper::QtWorkRunner runner;
  holonight_helper::HelperTerminator terminator(config->lockFile());

  holonight_packages_application::PlanService plans(holonight_packages_application::PlanService::Dependencies{
      .engine = engine,
      .authorizer = authorizer,
      .identities = identities,
      .ids = ids,
      .clock = clock,
      .runner = runner,
      .terminator = terminator,
  });
  holonight_helper::HelperService service(bus, plans);

  holonight_packages_application::QtOneShotTimer idleTimer;
  holonight_packages_application::QtOneShotTimer expiryTimer;
  holonight_packages_application::IdleMonitor idle(clock, idleTimer, expiryTimer,
                                                   {
                                                       .has_activity = [&plans] { return plans.hasActivity(); },
                                                       .next_expiry = [&plans] { return plans.nextExpiry(); },
                                                       .sweep_expired = [&plans] { plans.sweepExpired(); },
                                                       .on_idle = [] { QCoreApplication::quit(); },
                                                   });
  bool quitWhenIdle = false;
  plans.setActivityObserver([&idle, &plans, &quitWhenIdle] {
    idle.evaluate();
    if (quitWhenIdle && !plans.slotBusy()) {
      QCoreApplication::quit();
    }
  });

  holonight_helper::HelperSignals signalBridge;
  QObject::connect(&signalBridge, &holonight_helper::HelperSignals::terminationRequested, &app,
                   [&plans, &quitWhenIdle] {
                     // Before the commit point this cancels the run; after it the helper finishes the transaction
                     // first.
                     if (plans.shutdownRequested()) {
                       QCoreApplication::quit();
                     } else {
                       quitWhenIdle = true;
                     }
                   });

  if (!service.start()) {
    QTextStream(stderr) << "holonight-package-helper: cannot claim " << QString::fromLatin1(holonight_helper::kBusName)
                        << " on the system bus\n";
    return kExitFailure;
  }
  idle.evaluate();
  return QCoreApplication::exec();
}
