#pragma once

#include "holonight_packages_domain/update_source.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>

namespace holonight_packages_persistence {
class AlpmConnectionCache;
}  // namespace holonight_packages_persistence

namespace holonight_packages_backends {

// Every path the adapter touches comes from here; it holds no default pacman locations.
struct AlpmUpdateSourceOptions {
  // libalpm root directory.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::filesystem::path databaseRoot;
  // libalpm database path, containing local/ and sync/.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::filesystem::path databasePath;
  // Repository identities, priority and ignore rules; RootDir and DBPath in it are not consulted.
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  std::filesystem::path pacmanConfPath;
  // NOLINTNEXTLINE(readability-identifier-naming): matches established options naming.
  std::filesystem::path snapshotFile;
};

// Compares current installed packages with selected local and published checked repositories. Never downloads or
// mutates package databases. A private temporary layout supplies the selected catalog to libalpm.
// Without a snapshot path, the legacy local-only path retains its AlpmConnectionCache.
class AlpmUpdateSource : public holonight_packages_domain::UpdateSource {
 public:
  explicit AlpmUpdateSource(AlpmUpdateSourceOptions options);
  ~AlpmUpdateSource() override;
  [[nodiscard]] std::vector<std::filesystem::path> watchPaths() const override;

  AlpmUpdateSource(const AlpmUpdateSource&) = delete;
  AlpmUpdateSource& operator=(const AlpmUpdateSource&) = delete;
  AlpmUpdateSource(AlpmUpdateSource&&) = delete;
  AlpmUpdateSource& operator=(AlpmUpdateSource&&) = delete;

  [[nodiscard]] std::expected<holonight_packages_domain::UpdateSnapshot, holonight_packages_domain::UpdateSourceError>
  loadUpdates() const override;

 private:
  [[nodiscard]] std::expected<holonight_packages_domain::UpdateSnapshot, holonight_packages_domain::UpdateSourceError>
  loadLocalUpdates() const;
  mutable std::mutex watch_mutex_;
  mutable std::vector<std::filesystem::path> configuration_watch_paths_;
  AlpmUpdateSourceOptions options_;
  std::unique_ptr<holonight_packages_persistence::AlpmConnectionCache> connection_cache_;
};

}  // namespace holonight_packages_backends
