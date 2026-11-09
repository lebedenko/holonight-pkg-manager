#pragma once

#include <QCryptographicHash>
#include <QFile>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace holonight_packages_testing {

// Content and metadata of a regular file, or the target of a symlink, or a directory marker.
struct ManifestEntry {
  std::string kind;
  std::string sha256;
  std::uintmax_t size = 0;
  std::filesystem::file_time_type modified;
  bool operator==(const ManifestEntry&) const = default;
};
using Manifest = std::map<std::string, ManifestEntry>;

// Path, kind, size, SHA-256 and mtime of everything under `root`, without following symlinks.
inline Manifest manifestOf(const std::filesystem::path& root) {
  namespace fs = std::filesystem;
  Manifest manifest;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    const std::string relative = fs::relative(entry.path(), root).string();
    if (entry.is_symlink()) {
      manifest[relative] = ManifestEntry{
          .kind = "symlink:" + fs::read_symlink(entry.path()).string(),
          .sha256 = {},
          .size = 0,
          .modified = {},
      };
    } else if (entry.is_directory()) {
      manifest[relative] = ManifestEntry{
          .kind = "dir",
          .sha256 = {},
          .size = 0,
          .modified = {},
      };
    } else {
      QFile file(QString::fromStdString(entry.path().string()));
      if (!file.open(QIODevice::ReadOnly)) {
        manifest[relative] =
            ManifestEntry{.kind = "unreadable", .sha256 = {}, .size = 0, .modified = fs::last_write_time(entry.path())};
        continue;
      }
      manifest[relative] = ManifestEntry{
          .kind = "file",
          .sha256 = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex().toStdString(),
          .size = fs::file_size(entry.path()),
          .modified = fs::last_write_time(entry.path()),
      };
    }
  }
  return manifest;
}

}  // namespace holonight_packages_testing
