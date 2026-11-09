#include "protected_set.h"

#include "dependency_text.h"

#include <deque>

namespace holonight_packages_transaction {

std::expected<std::set<std::string>, holonight_packages_domain::TransactionFailure> computeProtectedSet(
    alpm_handle_t* handle, const std::vector<std::string>& holdPkgs) {
  std::set<std::string> protectedNames(holdPkgs.begin(), holdPkgs.end());
  alpm_db_t* localDb = alpm_get_localdb(handle);
  alpm_list_t* syncDbs = alpm_get_syncdbs(handle);

  alpm_pkg_t* base = alpm_db_get_pkg(localDb, "base");
  bool fromLocal = base != nullptr;
  if (base == nullptr) {
    for (const alpm_list_t* db = syncDbs; db != nullptr && base == nullptr; db = db->next) {
      base = alpm_db_get_pkg(static_cast<alpm_db_t*>(db->data), "base");
    }
  }
  if (base == nullptr) {
    return protectedNames;  // no `base`: only HoldPkg is protected
  }

  std::set<std::string> visited;
  std::deque<alpm_pkg_t*> queue{base};
  visited.insert(alpm_pkg_get_name(base));
  protectedNames.insert(alpm_pkg_get_name(base));
  while (!queue.empty()) {
    alpm_pkg_t* package = queue.front();
    queue.pop_front();
    for (const alpm_list_t* dependency = alpm_pkg_get_depends(package); dependency != nullptr;
         dependency = dependency->next) {
      const std::string text = dependencyText(static_cast<const alpm_depend_t*>(dependency->data));
      alpm_pkg_t* satisfier = fromLocal ? alpm_find_satisfier(alpm_db_get_pkgcache(localDb), text.c_str())
                                        : alpm_find_dbs_satisfier(handle, syncDbs, text.c_str());
      if (satisfier == nullptr) {
        continue;
      }
      const std::string name = alpm_pkg_get_name(satisfier);
      if (!visited.insert(name).second) {
        continue;
      }
      protectedNames.insert(name);
      if (visited.size() > kMaxProtectedSetSize) {
        return std::unexpected(holonight_packages_domain::TransactionFailure{
            .code = holonight_packages_domain::TransactionErrorCode::InternalError,
            .names = {},
            .choices = {},
        });
      }
      queue.push_back(satisfier);
    }
  }
  return protectedNames;
}

}  // namespace holonight_packages_transaction
