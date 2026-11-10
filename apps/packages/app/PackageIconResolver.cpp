#include "PackageIconResolver.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QImageReader>
#include <QSettings>
#include <QtConcurrentRun>

#include <AppStreamQt/component.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/pool.h>
#include <algorithm>
#include <array>
#include <exception>

namespace {

QUrl readableImage(const QString& path) {
  if (!QDir::isAbsolutePath(path)) {
    return {};
  }
  QImageReader reader(path);
  reader.setScaledSize(QSize(64, 64));
  return reader.read().isNull() ? QUrl() : QUrl::fromLocalFile(path);
}

PackageIcon desktopIcon(const QString& path) {
  QSettings entry(path, QSettings::IniFormat);
  entry.beginGroup(QStringLiteral("Desktop Entry"));
  if (entry.value(QStringLiteral("Type")).toString() != QStringLiteral("Application") ||
      entry.value(QStringLiteral("Hidden"), false).toBool() ||
      entry.value(QStringLiteral("NoDisplay"), false).toBool()) {
    return {};
  }
  const QString value = entry.value(QStringLiteral("Icon")).toString();
  return QDir::isAbsolutePath(value) ? PackageIcon{.name = {}, .source = readableImage(value)}
                                     : PackageIcon{.name = value, .source = {}};
}

void resolveThemes(QHash<QString, PackageIcon>& icons) {
  for (auto& icon : icons) {
    if (!icon.name.isEmpty()) {
      // QIcon's theme machinery and pixmaps stay on the GUI thread. Publish a bounded,
      // original-color image once, rather than resolving the theme in each delegate.
      const QIcon themed = QIcon::fromTheme(icon.name);
      const QPixmap pixmap = themed.pixmap(64, 64);
      if (!pixmap.isNull()) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (pixmap.save(&buffer, "PNG")) {
          icon.source = QUrl(QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64()));
        }
      } else {
        icon.name.clear();
      }
    }
  }
}

QHash<QString, PackageIcon> installedIcons(const std::vector<holonight_packages_domain::Package>& packages) {
  QHash<QString, PackageIcon> result;
  for (const auto& package : packages) {
    auto paths = package.desktopEntryPaths;
    const QString name = QString::fromStdString(package.name);
    std::ranges::sort(paths, [&name](const auto& left, const auto& right) {
      const auto preferred = [&name](const auto& path) {
        return QFileInfo(QString::fromStdString(path)).completeBaseName() == name;
      };
      if (preferred(left) != preferred(right)) {
        return preferred(left);
      }
      return left < right;
    });
    for (const auto& path : paths) {
      const auto icon = desktopIcon(QString::fromStdString(path));
      if (!icon.name.isEmpty() || !icon.source.isEmpty()) {
        result.insert(name, icon);
        break;
      }
    }
  }

  return result;
}

QHash<QString, PackageIcon> catalogIcons(const QString& catalogDirectory) {
  QHash<QString, PackageIcon> result;
  AppStream::Pool pool;
  pool.setFlags(AppStream::Pool::FlagLoadOsCatalog);
  if (!catalogDirectory.isNull()) {
    pool.setLoadStdDataLocations(false);
    pool.addExtraDataLocation(catalogDirectory, AppStream::Metadata::FormatStyleCatalog);
    pool.overrideCacheLocations(catalogDirectory + QStringLiteral("/cache"),
                                catalogDirectory + QStringLiteral("/cache"));
  }
  // A partially readable catalog is still useful; absent/broken data is a silent fallback.
  pool.load();
  auto components = pool.components().toList();
  std::ranges::sort(components, [](const auto& left, const auto& right) {
    return left.id() == right.id() ? left.dataId() < right.dataId() : left.id() < right.id();
  });
  for (const auto& component : components) {
    if (component.kind() != AppStream::Component::KindDesktopApp) {
      continue;
    }
    PackageIcon icon;
    for (const auto& candidate : component.icons()) {
      if (candidate.kind() == AppStream::Icon::KindStock && icon.name.isEmpty()) {
        icon.name = candidate.name();
      } else if ((candidate.kind() == AppStream::Icon::KindCached || candidate.kind() == AppStream::Icon::KindLocal) &&
                 icon.source.isEmpty()) {
        icon.source = readableImage(candidate.filename());
      }
    }
    for (const auto& name : component.packageNames()) {
      if (!result.contains(name)) {
        result.insert(name, icon);
      }
    }
  }
  return result;
}

}  // namespace

PackageIconResolver::PackageIconResolver(QObject* parent, QString catalogDirectory)
    : QObject(parent), catalog_directory_(std::move(catalogDirectory)) {
  connect(&watcher_, &QFutureWatcher<Icons>::finished, this, [this] {
    loading_ = false;
    Icons loaded = watcher_.future().takeResult();
    if (pending_) {
      pending_ = false;
      reload();
      return;
    }
    resolveThemes(loaded.installed);
    resolveThemes(loaded.catalog);
    icons_ = std::move(loaded);
    emit iconsChanged();
  });
  reload();
}

void PackageIconResolver::setInstalledPackages(const std::vector<holonight_packages_domain::Package>& packages) {
  packages_ = packages;
  reload();
}

PackageIcon PackageIconResolver::icon(const QString& package, bool allowCatalog) const {
  auto installed = icons_.installed.value(package);
  if (!installed.source.isEmpty()) {
    return installed;
  }
  return allowCatalog ? icons_.catalog.value(package) : PackageIcon{};
}

void PackageIconResolver::reload() {
  if (loading_) {
    pending_ = true;
    return;
  }
  loading_ = true;
  watcher_.setFuture(QtConcurrent::run([packages = packages_, directory = catalog_directory_] {
    try {
      return load(packages, directory);
    } catch (const std::exception&) {
      return Icons{};
    }
  }));
}

PackageIconResolver::Icons PackageIconResolver::load(const std::vector<holonight_packages_domain::Package>& packages,
                                                     const QString& catalogDirectory) {
  return {.installed = installedIcons(packages), .catalog = catalogIcons(catalogDirectory)};
}

bool PackageIconResolver::isOfficialRepository(std::string_view repository) {
  static constexpr std::array<std::string_view, 9> repositories{
      "core",         "extra",         "multilib",         "core-testing", "extra-testing", "multilib-testing",
      "core-staging", "extra-staging", "multilib-staging",
  };
  return std::ranges::find(repositories, repository) != repositories.end();
}
