#include "PackageIconResolver.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
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
#include <holonight_images/image.h>
#include <holonight_images/svg.h>

namespace {

QUrl imageSource(const QImage& image) {
  if (image.isNull()) {
    return {};
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    return {};
  }
  return {QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64())};
}

QUrl readableImage(const QString& path) {
  if (!QDir::isAbsolutePath(path) || !QFileInfo(path).isFile()) {
    return {};
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.isSequential()) {
    return {};
  }
  static constexpr HolonightImages::Limits limits{
      .inputBytes = 16 * 1024 * 1024,
      .sourcePixels = 16000000,
      .sourceExtent = 8192,
      .decodedBytes = 64 * 1024 * 1024,
      .metadataBytes = 1024 * 1024,
      .tiffMetadataBytes = 4 * 1024 * 1024,
      .metadataRecords = 1024,
  };
  const std::atomic_bool cancelled{false};
  if (QFileInfo(path).suffix().compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0) {
    const auto source = HolonightImages::loadSvg(file, limits.inputBytes, cancelled);
    if (source.outcome != HolonightImages::Outcome::Success) {
      return {};
    }
    return imageSource(HolonightImages::rasterizeSvg(
                           source.bytes, {.bound = QSize(64, 64), .outputBytes = limits.decodedBytes}, cancelled)
                           .image);
  }
  return imageSource(HolonightImages::decode(file, {.limits = limits, .bound = QSize(64, 64)}, cancelled).image);
}

using Candidates = QHash<QString, QList<PackageIcon>>;

PackageIcon desktopIcon(const QString& path) {
  QSettings entry(path, QSettings::IniFormat);
  entry.beginGroup(QStringLiteral("Desktop Entry"));
  if (entry.value(QStringLiteral("Type")).toString() != QStringLiteral("Application") ||
      entry.value(QStringLiteral("Hidden"), false).toBool()) {
    return {};
  }
  const QString value = entry.value(QStringLiteral("Icon")).toString();
  return QDir::isAbsolutePath(value) ? PackageIcon{.name = {}, .source = readableImage(value)}
                                     : PackageIcon{.name = value, .source = {}};
}

QHash<QString, PackageIcon> resolveCandidates(const Candidates& candidates) {
  QHash<QString, PackageIcon> result;
  for (auto it = candidates.cbegin(); it != candidates.cend(); ++it) {
    for (auto icon : it.value()) {
      if (!icon.name.isEmpty()) {
        // Theme machinery and pixmaps belong to the GUI thread.
        icon.source = imageSource(QIcon::fromTheme(icon.name).pixmap(64, 64).toImage());
      }
      if (!icon.source.isEmpty()) {
        result.insert(it.key(), icon);
        break;
      }
    }
  }
  return result;
}

Candidates installedIcons(const std::vector<holonight_packages_domain::Package>& packages) {
  Candidates result;
  for (const auto& package : packages) {
    auto paths = package.desktopEntryPaths;
    const QString name = QString::fromStdString(package.name);
    std::ranges::sort(paths, [&name](const auto& left, const auto& right) {
      const auto noDisplay = [](const auto& path) {
        QSettings entry(QString::fromStdString(path), QSettings::IniFormat);
        return entry.value(QStringLiteral("Desktop Entry/NoDisplay"), false).toBool();
      };
      if (noDisplay(left) != noDisplay(right)) {
        return !noDisplay(left);
      }
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
        result[name].append(icon);
      }
    }
  }

  return result;
}

Candidates catalogIcons(const QString& catalogDirectory) {
  Candidates result;
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
    QList<PackageIcon> icons;
    for (const auto& candidate : component.icons()) {
      if (candidate.kind() == AppStream::Icon::KindStock) {
        icons.append({.name = candidate.name(), .source = {}});
      }
    }
    for (const auto& candidate : component.icons()) {
      if (candidate.kind() == AppStream::Icon::KindCached || candidate.kind() == AppStream::Icon::KindLocal) {
        icons.append({.name = {}, .source = readableImage(candidate.filename())});
      }
    }
    for (const auto& name : component.packageNames()) {
      result[name].append(icons);
    }
  }
  return result;
}

}  // namespace

PackageIconResolver::PackageIconResolver(QObject* parent, QString catalogDirectory)
    : QObject(parent), catalog_directory_(std::move(catalogDirectory)) {
  QImageReader::setAllocationLimit(64);
  connect(&watcher_, &QFutureWatcher<LoadedIcons>::finished, this, [this] {
    loading_ = false;
    LoadedIcons loaded = watcher_.future().takeResult();
    if (pending_) {
      pending_ = false;
      reload();
      return;
    }
    icons_ = {.installed = resolveCandidates(loaded.installed), .catalog = resolveCandidates(loaded.catalog)};
    emit iconsChanged();
  });
  reload();
}

PackageIconResolver::~PackageIconResolver() { watcher_.waitForFinished(); }

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
      return LoadedIcons{};
    }
  }));
}

PackageIconResolver::LoadedIcons PackageIconResolver::load(
    const std::vector<holonight_packages_domain::Package>& packages, const QString& catalogDirectory) {
  return {.installed = installedIcons(packages), .catalog = catalogIcons(catalogDirectory)};
}

bool PackageIconResolver::isOfficialRepository(std::string_view repository) {
  static constexpr std::array<std::string_view, 9> repositories{
      "core",         "extra",         "multilib",         "core-testing", "extra-testing", "multilib-testing",
      "core-staging", "extra-staging", "multilib-staging",
  };
  return std::ranges::find(repositories, repository) != repositories.end();
}
