#include "PackageIconResolver.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>
#include <optional>

namespace {
struct Snapshot {
  std::vector<holonight_packages_domain::Package> packages;
  QHash<QString, bool> official;
};

std::optional<Snapshot> readSnapshot(const QString& path) {
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    return {};
  }
  Snapshot snapshot;
  while (!input.atEnd()) {
    const auto fields = QString::fromUtf8(input.readLine()).trimmed().split('\t');
    if (fields.size() < 2) {
      continue;
    }
    holonight_packages_domain::Package package{.name = fields[0].toStdString()};
    snapshot.official.insert(fields[0], fields[1] == QStringLiteral("1"));
    for (qsizetype i = 2; i < fields.size(); ++i) {
      package.desktopEntryPaths.push_back(fields[i].toStdString());
    }
    snapshot.packages.push_back(std::move(package));
  }
  return snapshot;
}

std::optional<Snapshot> fixtureSnapshot(const QString& directory, const QString& image) {
  QFile desktop(directory + QStringLiteral("/sample.desktop"));
  if (!desktop.open(QIODevice::WriteOnly)) {
    return {};
  }
  desktop.write("[Desktop Entry]\nType=Application\nIcon=" + image.toUtf8() + '\n');
  return Snapshot{
      .packages = {{.name = "sample", .desktopEntryPaths = {desktop.fileName().toStdString()}}},
      .official = {},
  };
}

void reportCoverage(const Snapshot& snapshot, const PackageIconResolver& resolver) {
  int desktop = 0;
  int catalog = 0;
  int unresolved = 0;
  for (const auto& package : snapshot.packages) {
    const auto name = QString::fromStdString(package.name);
    if (!resolver.icon(name, false).source.isEmpty()) {
      ++desktop;
    } else if (!resolver.icon(name, snapshot.official.value(name)).source.isEmpty()) {
      ++catalog;
    } else {
      ++unresolved;
    }
  }
  std::cout << "{\"desktop\":" << desktop << ",\"catalog\":" << catalog << ",\"unresolved\":" << unresolved << "}\n";
}

int checkJxl(const QUrl& source, bool withoutCodec) {
  const bool codec = QImageReader::supportedImageFormats().contains("jxl");
  if (withoutCodec) {
    std::cerr << "codec=" << codec << " source=" << source.scheme().toStdString() << "\n";
    return !codec && source.isEmpty() ? 0 : 1;
  }
  if (!codec) {
    std::cerr << "Supported installation requires the Qt JPEG XL image plugin\n";
  }
  const auto image = QImage::fromData(QByteArray::fromBase64(source.toString().section(',', 1).toLatin1()));
  return codec && source.scheme() == QStringLiteral("data") && !image.isNull() && image.width() <= 64 &&
                 image.height() <= 64
             ? 0
             : 1;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const auto args = QCoreApplication::arguments();
  if (args.size() < 3) {
    return 2;
  }
  QTemporaryDir isolated;
  const bool without_codec = args[1] == QStringLiteral("--without-jxl");
  if (without_codec) {
    QCoreApplication::setLibraryPaths({isolated.path()});
  }
  const bool coverage = args[1] == QStringLiteral("--coverage");
  const auto snapshot = coverage ? readSnapshot(args[2]) : fixtureSnapshot(isolated.path(), args[2]);
  if (!snapshot) {
    return 2;
  }
  PackageIconResolver resolver(nullptr, coverage ? QString() : isolated.path());
  QObject::connect(&resolver, &PackageIconResolver::iconsChanged, &app, [&] {
    if (coverage) {
      reportCoverage(*snapshot, resolver);
      QCoreApplication::exit(0);
    } else {
      QCoreApplication::exit(checkJxl(resolver.icon(QStringLiteral("sample")).source, without_codec));
    }
  });
  resolver.setInstalledPackages(snapshot->packages);
  QTimer::singleShot(120000, &app, [] { QCoreApplication::exit(3); });
  return QGuiApplication::exec();
}
