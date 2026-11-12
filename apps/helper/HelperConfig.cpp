#include "HelperConfig.h"

#include "holonight_packages_backends/pacman_defaults.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QStringList>

#include <array>
#include <filesystem>

namespace holonight_helper {
namespace {

constexpr const char* kDefaultCacheDirectory = "/var/cache/pacman/pkg";
constexpr const char* kDefaultStateDirectory = "/var/lib/holonight-package-helper";

}  // namespace

std::expected<HelperConfig, std::string> parseHelperConfig(const QStringList& arguments) {
  using namespace holonight_packages_backends;
  QCommandLineParser parser;
  const QCommandLineOption root(QStringLiteral("root"), QStringLiteral("Installation root."), QStringLiteral("dir"),
                                QString::fromUtf8(kDefaultPacmanRoot));
  const QCommandLineOption dbpath(QStringLiteral("dbpath"), QStringLiteral("libalpm database directory."),
                                  QStringLiteral("dir"), QString::fromUtf8(kDefaultPacmanDatabasePath));
  const QCommandLineOption conf(QStringLiteral("pacman-conf"), QStringLiteral("pacman.conf to read."),
                                QStringLiteral("file"), QString::fromUtf8(kDefaultPacmanConfPath));
  const QCommandLineOption cache(QStringLiteral("cache-dir"), QStringLiteral("Fallback package cache."),
                                 QStringLiteral("dir"), QString::fromUtf8(kDefaultCacheDirectory));
  const QCommandLineOption state(QStringLiteral("state-dir"), QStringLiteral("Directory for upgrade scratch roots."),
                                 QStringLiteral("dir"), QString::fromUtf8(kDefaultStateDirectory));
  parser.addOptions({root, dbpath, conf, cache, state});
  if (!parser.parse(arguments)) {
    return std::unexpected(parser.errorText().toStdString());
  }
  if (!parser.positionalArguments().isEmpty()) {
    return std::unexpected("unexpected argument " + parser.positionalArguments().first().toStdString());
  }
  HelperConfig config;
  const std::array<std::pair<const QCommandLineOption*, std::filesystem::path*>, 5> targets{
      {
          {&root, &config.transaction.root},
          {&dbpath, &config.transaction.database_path},
          {&conf, &config.transaction.pacman_conf_path},
          {&cache, &config.transaction.cache_directory},
          {&state, &config.state_directory},
      },
  };
  for (const auto& [option, target] : targets) {
    const std::filesystem::path path = parser.value(*option).toStdString();
    if (path.empty() || !path.is_absolute()) {
      return std::unexpected("--" + option->names().first().toStdString() + " must be an absolute path");
    }
    *target = path;
  }
  return config;
}

}  // namespace holonight_helper
