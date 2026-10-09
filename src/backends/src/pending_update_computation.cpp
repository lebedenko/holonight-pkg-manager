#include "pending_update_computation.h"

#include "update_matching.h"

#include <cstdint>
#include <string>
#include <utility>

namespace holonight_packages_backends {

namespace {

using holonight_packages_domain::PendingUpdate;

std::vector<std::string> groupsOf(alpm_pkg_t* pkg) {
  // alpm_pkg_get_groups returns the package's own cached list; borrowed, not freed.
  std::vector<std::string> groups;
  for (alpm_list_t* node = alpm_pkg_get_groups(pkg); node != nullptr; node = alpm_list_next(node)) {
    groups.emplace_back(static_cast<const char*>(node->data));
  }
  return groups;
}

// First registered sync database containing the package wins, as for pacman's repository order.
std::pair<alpm_pkg_t*, alpm_db_t*> findInSyncDatabases(const char* name, const std::vector<alpm_db_t*>& sync_dbs) {
  for (alpm_db_t* sync_db : sync_dbs) {
    if (alpm_pkg_t* found = alpm_db_get_pkg(sync_db, name); found != nullptr) {
      return {found, sync_db};
    }
  }
  return {nullptr, nullptr};
}

std::string stringOrEmpty(const char* text) { return text != nullptr ? text : ""; }

}  // namespace

std::vector<PendingUpdate> computePendingUpdates(alpm_list_t* installed_packages,
                                                 const std::vector<alpm_db_t*>& sync_databases,
                                                 const PacmanConfig& ignore_rules) {
  std::vector<PendingUpdate> updates;
  for (alpm_list_t* node = installed_packages; node != nullptr; node = alpm_list_next(node)) {
    auto* installed = static_cast<alpm_pkg_t*>(node->data);
    const char* name = alpm_pkg_get_name(installed);
    if (name == nullptr) {
      continue;
    }
    const auto [available, repository] = findInSyncDatabases(name, sync_databases);
    if (available == nullptr) {
      continue;  // foreign package
    }
    const char* installed_version = alpm_pkg_get_version(installed);
    const char* available_version = alpm_pkg_get_version(available);
    if (installed_version == nullptr || available_version == nullptr ||
        alpm_pkg_vercmp(available_version, installed_version) <= 0) {
      continue;
    }

    const off_t download_size = alpm_pkg_get_size(available);
    const std::vector<std::string> groups = groupsOf(available);
    updates.push_back(PendingUpdate{
        .name = name,
        .installedVersion = installed_version,
        .availableVersion = available_version,
        .repository = stringOrEmpty(alpm_db_get_name(repository)),
        .downloadSizeBytes = download_size > 0 ? static_cast<std::uint64_t>(download_size) : 0,
        .installedSizeDeltaBytes = static_cast<std::int64_t>(alpm_pkg_get_isize(available)) -
                                   static_cast<std::int64_t>(alpm_pkg_get_isize(installed)),
        .ignored = isIgnored(name, groups, ignore_rules.ignore_pkgs, ignore_rules.ignore_groups),
    });
  }
  return updates;
}

}  // namespace holonight_packages_backends
