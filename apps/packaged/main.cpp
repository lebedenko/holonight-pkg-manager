#include "SignalHandler.h"
#include "UpdateStatusService.h"
#include "holonight_packages_application/update_monitor.h"
#include "holonight_packages_backends/alpm_update_source.h"
#include "holonight_packages_backends/pacman_defaults.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QTextStream>

#include <chrono>
#include <memory>

namespace {

constexpr int kExitNameTaken = 0;
constexpr int kExitFailure = 1;

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("holonight-packaged"));
  QCoreApplication::setApplicationVersion(QStringLiteral(HOLONIGHT_PACKAGED_VERSION));

  const QString default_root = QString::fromUtf8(holonight_packages_backends::kDefaultPacmanRoot);
  const QString default_dbpath = QString::fromUtf8(holonight_packages_backends::kDefaultPacmanDatabasePath);
  const QString default_conf = QString::fromUtf8(holonight_packages_backends::kDefaultPacmanConfPath);

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("HoloNight Packages user service"));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption root_option(QStringLiteral("root"), QStringLiteral("libalpm root directory."),
                                       QStringLiteral("dir"), default_root);
  const QCommandLineOption dbpath_option(QStringLiteral("dbpath"), QStringLiteral("libalpm database path."),
                                         QStringLiteral("dir"), default_dbpath);
  const QCommandLineOption conf_option(QStringLiteral("pacman-conf"), QStringLiteral("pacman.conf to read."),
                                       QStringLiteral("file"), default_conf);
  const QCommandLineOption debounce_option(QStringLiteral("debounce-ms"),
                                           QStringLiteral("Quiet period before re-evaluating."), QStringLiteral("ms"),
                                           QStringLiteral("2000"));
  parser.addOptions({root_option, dbpath_option, conf_option, debounce_option});
  parser.process(app);

  bool debounce_ok = false;
  const int debounce_ms = parser.value(debounce_option).toInt(&debounce_ok);
  if (!debounce_ok || debounce_ms < 0) {
    QTextStream(stderr) << "holonight-packaged: invalid --debounce-ms\n";
    return kExitFailure;
  }

  const QString dbpath = parser.value(dbpath_option);
  auto source = std::make_shared<holonight_packages_backends::AlpmUpdateSource>(
      holonight_packages_backends::AlpmUpdateSourceOptions{
          .databaseRoot = parser.value(root_option).toStdString(),
          .databasePath = dbpath.toStdString(),
          .pacmanConfPath = parser.value(conf_option).toStdString(),
      });
  holonight_packages_application::UpdateMonitor monitor(
      std::move(source),
      holonight_packages_application::UpdateMonitorOptions{
          .watchPaths = {QDir(dbpath).filePath(QStringLiteral("local")), QDir(dbpath).filePath(QStringLiteral("sync"))},
          .debounce = std::chrono::milliseconds{debounce_ms}});
  UpdateStatusService service(&monitor);

  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    QTextStream(stderr) << "holonight-packaged: no session bus\n";
    return kExitFailure;
  }
  if (!service.registerOn(bus)) {
    QTextStream(stderr) << "holonight-packaged: " << service.error() << '\n';
    // Another instance already serves the name; that is the expected outcome of racing activations.
    return bus.interface() != nullptr && bus.interface()->isServiceRegistered(UpdateStatusService::kServiceName)
               ? kExitNameTaken
               : kExitFailure;
  }

  SignalHandler termination;
  QObject::connect(&termination, &SignalHandler::terminationRequested, &app, &QCoreApplication::quit);
  monitor.start();
  return QCoreApplication::exec();
}
