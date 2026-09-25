#pragma once

#include "holonight_packages_domain/explore_source.h"

#include <expected>
#include <filesystem>
#include <memory>

namespace holonight_packages_persistence {
class AlpmConnectionCache;
}  // namespace holonight_packages_persistence

namespace holonight_packages_backends {

// Every path the adapter touches comes from here; it holds no default pacman locations.
struct AlpmExploreSourceOptions {
  // libalpm root directory.
  std::filesystem::path databaseRoot;
  // libalpm database path, containing local/ and sync/.
  std::filesystem::path databasePath;
};

// Enumerates every package in the sync databases as they are on disk, marking those installed locally. Read-only:
// never syncs, downloads or writes. Owns its own AlpmConnectionCache, so unchanged sync databases are parsed once per
// instance.
class AlpmExploreSource : public holonight_packages_domain::ExploreSource {
 public:
  explicit AlpmExploreSource(AlpmExploreSourceOptions options);
  ~AlpmExploreSource() override;

  AlpmExploreSource(const AlpmExploreSource&) = delete;
  AlpmExploreSource& operator=(const AlpmExploreSource&) = delete;
  AlpmExploreSource(AlpmExploreSource&&) = delete;
  AlpmExploreSource& operator=(AlpmExploreSource&&) = delete;

  [[nodiscard]] std::expected<holonight_packages_domain::ExploreSnapshot, holonight_packages_domain::ExploreSourceError>
  loadPackages() const override;

 private:
  AlpmExploreSourceOptions options_;
  std::unique_ptr<holonight_packages_persistence::AlpmConnectionCache> connection_cache_;
};

}  // namespace holonight_packages_backends
