#include "BuildIdentity.h"
#include "DBusTransactionService.h"
#include "PackagesApplication.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QTextStream>

#include <cstdlib>
#include <span>
#include <string_view>

int main(int argc, char* argv[]) {
  const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
  const std::string_view command = argc == 2 ? arguments[1] : "";
  if (command == "--build-info" || command == "--diagnostics") {
    QCoreApplication core(argc, argv);
    QTextStream output(stdout);
    output << "holonight-packages: " << HOLONIGHT_BUILD_ID << "\n";
    if (command == "--diagnostics") {
      const QString identity = DBusTransactionService::runningBuildId(QDBusConnection::systemBus());
      if (identity.isEmpty()) {
        output << "Running helper: unavailable (inactive, unreachable, or lacks build identity)\n";
        return EXIT_FAILURE;
      }
      output << "Running helper: " << identity << "\n";
      const bool matches =
          !identity.contains(QStringLiteral("unknown")) && identity == QStringLiteral(HOLONIGHT_BUILD_ID);
      output << (matches ? "Builds match\n" : "Builds differ; check the installation and running services\n");
      return matches ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }
  PackagesApplication app(argc, argv);
  if (!app.isReady()) {
    return EXIT_FAILURE;
  }
  return QGuiApplication::exec();
}
