#include "PackagedConfig.h"
#include "SignalHandler.h"
#include "UpdateStatusService.h"
#include "holonight_packages_application/update_check_runtime.h"
#include "holonight_packages_application/update_check_scheduler.h"
#include "holonight_packages_application/update_check_service.h"
#include "holonight_packages_application/update_monitor.h"
#include "holonight_packages_backends/alpm_update_checker.h"
#include "holonight_packages_backends/alpm_update_source.h"
#include "holonight_packages_backends/backend_capabilities_alpm.h"
#include "holonight_packages_backends/pacman_defaults.h"
#include "holonight_packages_persistence/cache_locations.h"
#include "holonight_packages_persistence/json_update_snapshot_store.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QTextStream>
#include <QtLogging>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

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
  const QCommandLineOption interval_option(
      QStringLiteral("check-interval-minutes"),
      QStringLiteral("Minutes between automatic online update checks (overrides packages.toml)."),
      QStringLiteral("minutes"));
  parser.addOptions({root_option, dbpath_option, conf_option, debounce_option, interval_option});
  parser.process(app);

  bool debounce_ok = false;
  const int debounce_ms = parser.value(debounce_option).toInt(&debounce_ok);
  if (!debounce_ok || debounce_ms < 0) {
    QTextStream(stderr) << "holonight-packaged: invalid --debounce-ms\n";
    return kExitFailure;
  }

  const QString dbpath = parser.value(dbpath_option);
  const auto cache_dir =
      holonight_packages_persistence::appCacheDir(holonight_packages_persistence::processEnvironment());
  auto source = std::make_shared<holonight_packages_backends::AlpmUpdateSource>(
      holonight_packages_backends::AlpmUpdateSourceOptions{
          .databaseRoot = parser.value(root_option).toStdString(),
          .databasePath = dbpath.toStdString(),
          .pacmanConfPath = parser.value(conf_option).toStdString(),
          .snapshotFile = cache_dir.value_or(std::filesystem::path()) /
                          std::string(holonight_packages_persistence::kSnapshotFileName),
      });
  holonight_packages_application::UpdateMonitor monitor(
      std::move(source),
      holonight_packages_application::UpdateMonitorOptions{
          .watchPaths =
              {
                  QDir(dbpath).filePath(QStringLiteral("local")),
                  QDir(dbpath).filePath(QStringLiteral("sync")),
                  parser.value(conf_option),
                  QString::fromStdString((cache_dir.value_or(std::filesystem::path()) /
                                          std::string(holonight_packages_persistence::kSnapshotFileName))
                                             .string()),
              },
          .debounce = std::chrono::milliseconds{debounce_ms},
      });

  // Online update check: only this process checks, schedules and writes the snapshot file.
  const holonight_packages_application::UpdateCheckPolicy policy_defaults;
  const auto settings = packaged_config::load(
      packaged_config::configPath(packaged_config::processEnvironment()),
      parser.isSet(interval_option) ? std::optional<std::string>(parser.value(interval_option).toStdString())
                                    : std::nullopt,
      policy_defaults);
  for (const std::string& warning : settings.warnings) {
    qWarning().noquote() << "holonight-packaged:" << QString::fromStdString(warning);
  }
  holonight_packages_application::UpdateCheckPolicy policy = policy_defaults;
  policy.interval = settings.checkInterval;

  if (!cache_dir.has_value()) {
    qWarning() << "holonight-packaged: neither XDG_CACHE_HOME nor HOME is set; online update checks are disabled";
  }
  const auto checker = std::make_shared<holonight_packages_backends::AlpmUpdateChecker>(
      holonight_packages_backends::AlpmUpdateCheckerOptions{
          .databaseRoot = parser.value(root_option).toStdString(),
          .databasePath = dbpath.toStdString(),
          .pacmanConfPath = parser.value(conf_option).toStdString(),
          .scratchRoot = cache_dir.value_or(std::filesystem::path()) /
                         std::string(holonight_packages_persistence::kScratchDirectoryName),
      });
  auto store = std::make_shared<holonight_packages_persistence::JsonUpdateSnapshotStore>(
      cache_dir.value_or(std::filesystem::path()) / std::string(holonight_packages_persistence::kSnapshotFileName));
  holonight_packages_application::UpdateCheckService check_service(
      {
          .checker = checker,
          .store = store,
          .clock = std::make_shared<holonight_packages_application::SystemClock>(),
          .capabilities = cache_dir.has_value() ? holonight_packages_backends::alpmBackendCapabilities()
                                                : holonight_packages_domain::BackendCapabilities{},
      },
      policy);
  holonight_packages_application::UpdateCheckScheduler scheduler(
      check_service, std::make_unique<holonight_packages_application::QtOneShotTimer>(),
      std::make_shared<holonight_packages_application::MtRandomSource>(), policy);
  QObject::connect(&check_service, &holonight_packages_application::UpdateCheckService::snapshotAdopted, &monitor,
                   &holonight_packages_application::UpdateMonitor::adoptOnline);
  UpdateStatusService service(&monitor, &check_service);

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
  if (cache_dir.has_value()) {
    checker->sweepStale();  // a crash leftover is reduced to the newest run directory before anything runs
    check_service.start();  // adopts a valid stored snapshot; discards an invalid one (this is the single writer)
    scheduler.start();      // first automatic check after the startup delay
  }
  return QCoreApplication::exec();
}
