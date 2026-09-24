#pragma once

#include "holonight_packages_domain/update_source.h"

#include <expected>
#include <filesystem>
#include <memory>

namespace holonight_packages_persistence {
class AlpmConnectionCache;
}  // namespace holonight_packages_persistence

namespace holonight_packages_backends {

// Every path the adapter touches comes from here; it holds no default pacman locations.
struct AlpmUpdateSourceOptions {
  // libalpm root directory.
  std::filesystem::path databaseRoot;
  // libalpm database path, containing local/ and sync/.
  std::filesystem::path databasePath;
  // Read for IgnorePkg / IgnoreGroup only; RootDir and DBPath in it are not consulted.
  std::filesystem::path pacmanConfPath;
};

// Compares the local database against the sync databases as they are on disk. Read-only: never syncs, downloads or
// writes. Owns its own AlpmConnectionCache, so unchanged sync databases are parsed once per instance.
class AlpmUpdateSource : public holonight_packages_domain::UpdateSource {
 public:
  explicit AlpmUpdateSource(AlpmUpdateSourceOptions options);
  ~AlpmUpdateSource() override;

  AlpmUpdateSource(const AlpmUpdateSource&) = delete;
  AlpmUpdateSource& operator=(const AlpmUpdateSource&) = delete;
  AlpmUpdateSource(AlpmUpdateSource&&) = delete;
  AlpmUpdateSource& operator=(AlpmUpdateSource&&) = delete;

  [[nodiscard]] std::expected<holonight_packages_domain::UpdateSnapshot, holonight_packages_domain::UpdateSourceError>
  loadUpdates() const override;

 private:
  AlpmUpdateSourceOptions options_;
  std::unique_ptr<holonight_packages_persistence::AlpmConnectionCache> connection_cache_;
};

}  // namespace holonight_packages_backends
