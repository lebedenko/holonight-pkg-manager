#include "upgrade_resolver.h"

#include "dependency_text.h"
#include "error_mapping.h"
#include "protected_set.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::HeldBackEntry;
using holonight_packages_domain::NeedsDecisionChoice;
using holonight_packages_domain::NewDependencyEntry;
using holonight_packages_domain::ReplacementEntry;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;
using holonight_packages_domain::UpgradeDetail;
using holonight_packages_domain::UpgradeEntry;

TransactionFailure refuse(TransactionErrorCode code, std::vector<std::string> names = {}) {
  return TransactionFailure{.code = code, .names = std::move(names), .choices = {}};
}

std::vector<std::string> sortedUnique(std::vector<std::string> names) {
  std::ranges::sort(names);
  names.erase(std::ranges::unique(names).begin(), names.end());
  return names;
}

std::string repositoryOf(alpm_pkg_t* package) {
  alpm_db_t* database = alpm_pkg_get_db(package);
  return database != nullptr ? alpm_db_get_name(database) : std::string();
}

std::int64_t installedSize(alpm_pkg_t* package) { return static_cast<std::int64_t>(alpm_pkg_get_isize(package)); }

TransactionFailure decision(std::vector<NeedsDecisionChoice> choices) {
  TransactionFailure failure = refuse(TransactionErrorCode::NeedsDecision);
  failure.choices = std::move(choices);
  return failure;
}

// alpm_trans_prepare failed with unsatisfied dependencies. An installed package that the upgrade breaks needs a
// decision naming it (REQ-F-027); a dependency that no package satisfies is PackageNotFound naming it (REQ-F-022).
TransactionFailure unsatisfied(alpm_list_t* data) {
  std::vector<NeedsDecisionChoice> broken;
  std::vector<std::string> missing;
  for (const alpm_list_t* item = data; item != nullptr; item = item->next) {
    const auto* entry = static_cast<const alpm_depmissing_t*>(item->data);
    const std::string target = entry->target != nullptr ? entry->target : "";
    if (entry->causingpkg != nullptr) {
      broken.push_back(NeedsDecisionChoice{
          .kind = NeedsDecisionChoice::Kind::Conflict,
          .subject = target,
          .candidates = {target, entry->causingpkg},
      });
    } else if (entry->depend != nullptr && entry->depend->name != nullptr) {
      missing.emplace_back(entry->depend->name);
    }
  }
  if (!broken.empty()) {
    return decision(std::move(broken));
  }
  return refuse(TransactionErrorCode::PackageNotFound, sortedUnique(std::move(missing)));
}

void freePrepareData(alpm_errno_t error, alpm_list_t* data) {
  if (data == nullptr) {
    return;
  }
  if (error == ALPM_ERR_UNSATISFIED_DEPS) {
    for (alpm_list_t* item = data; item != nullptr; item = item->next) {
      alpm_depmissing_free(static_cast<alpm_depmissing_t*>(item->data));
    }
    alpm_list_free(data);
  } else if (error == ALPM_ERR_CONFLICTING_DEPS) {
    for (alpm_list_t* item = data; item != nullptr; item = item->next) {
      alpm_conflict_free(static_cast<alpm_conflict_t*>(item->data));
    }
    alpm_list_free(data);
  }
}

std::vector<NeedsDecisionChoice> conflictChoices(alpm_list_t* data) {
  std::vector<NeedsDecisionChoice> choices;
  for (const alpm_list_t* item = data; item != nullptr; item = item->next) {
    const auto* conflict = static_cast<const alpm_conflict_t*>(item->data);
    const std::string first = conflict->package1 != nullptr ? alpm_pkg_get_name(conflict->package1) : "";
    const std::string second = conflict->package2 != nullptr ? alpm_pkg_get_name(conflict->package2) : "";
    choices.push_back(NeedsDecisionChoice{
        .kind = NeedsDecisionChoice::Kind::Conflict,
        .subject = first,
        .candidates = {first, second},
    });
  }
  return choices;
}

std::map<std::string, alpm_pkg_t*> removedPackages(alpm_handle_t* handle) {
  std::map<std::string, alpm_pkg_t*> removed;
  for (const alpm_list_t* item = alpm_trans_get_remove(handle); item != nullptr; item = item->next) {
    auto* package = static_cast<alpm_pkg_t*>(item->data);
    removed.emplace(alpm_pkg_get_name(package), package);
  }
  return removed;
}

// REQ-F-075: HoldPkg and its friends may be upgraded, never removed (not even by a replacement).
std::optional<TransactionFailure> protectedRemoval(const std::map<std::string, alpm_pkg_t*>& removed,
                                                   const std::vector<std::string>& protectedNames) {
  const std::set<std::string> protectedSet(protectedNames.begin(), protectedNames.end());
  std::vector<std::string> names;
  for (const auto& [name, package] : removed) {
    if (protectedSet.contains(name)) {
      names.push_back(name);
    }
  }
  if (names.empty()) {
    return std::nullopt;
  }
  return refuse(TransactionErrorCode::ProtectedPackage, std::move(names));
}

// Sorts every added package into an upgrade, a replacement (of the installed packages libalpm asked about and the
// policy accepted; they leave `removed`) or a new package, and sums the download and the installed-size change.
void classifyAdded(const std::vector<alpm_pkg_t*>& added, alpm_db_t* local,
                   const std::vector<std::pair<std::string, std::string>>& accepted,
                   std::map<std::string, alpm_pkg_t*>& removed, UpgradeDetail& detail) {
  std::int64_t sizeChange = 0;
  std::uint64_t download = 0;
  for (alpm_pkg_t* package : added) {
    const std::string name = alpm_pkg_get_name(package);
    download += static_cast<std::uint64_t>(std::max<off_t>(alpm_pkg_download_size(package), 0));
    alpm_pkg_t* installed = alpm_db_get_pkg(local, name.c_str());
    if (installed != nullptr) {
      detail.upgrades.push_back(UpgradeEntry{
          .name = name,
          .old_version = alpm_pkg_get_version(installed),
          .new_version = alpm_pkg_get_version(package),
          .repository = repositoryOf(package),
      });
      sizeChange -= installedSize(installed);
    }
    bool replaces = false;
    for (const auto& [oldName, newName] : accepted) {
      const auto old = removed.find(oldName);
      if (newName != name || old == removed.end()) {
        continue;
      }
      detail.replacements.push_back(ReplacementEntry{
          .old_name = oldName,
          .old_version = alpm_pkg_get_version(old->second),
          .new_name = name,
          .new_version = alpm_pkg_get_version(package),
          .repository = repositoryOf(package),
      });
      sizeChange -= installedSize(old->second);
      removed.erase(old);
      replaces = true;
    }
    sizeChange += installedSize(package);
    if (!replaces && installed == nullptr) {
      detail.new_dependencies.push_back(NewDependencyEntry{
          .name = name,
          .version = alpm_pkg_get_version(package),
          .repository = repositoryOf(package),
      });
    }
  }
  detail.download_bytes = download;
  detail.size_change_bytes = sizeChange;
}

// Installed packages with a newer version that IgnorePkg or IgnoreGroup keeps back (REQ-F-021).
std::vector<HeldBackEntry> heldBack(alpm_handle_t* handle, alpm_db_t* local) {
  std::vector<HeldBackEntry> held;
  alpm_list_t* syncdbs = alpm_get_syncdbs(handle);
  for (const alpm_list_t* item = alpm_db_get_pkgcache(local); item != nullptr; item = item->next) {
    auto* installed = static_cast<alpm_pkg_t*>(item->data);
    if (alpm_pkg_should_ignore(handle, installed) != 0 && alpm_sync_get_new_version(installed, syncdbs) != nullptr) {
      held.push_back(HeldBackEntry{
          .name = alpm_pkg_get_name(installed),
          .installed_version = alpm_pkg_get_version(installed),
      });
    }
  }
  return held;
}

// The keyring step: archlinux-keyring and the packages of the plan it depends on, transitively (REQ-F-023).
std::vector<std::string> keyringStep(const std::vector<alpm_pkg_t*>& added) {
  const auto keyring = std::ranges::find_if(added, [](alpm_pkg_t* package) {
    return std::string(alpm_pkg_get_name(package)) == UpgradeResolver::kKeyringPackage;
  });
  if (keyring == added.end()) {
    return {};
  }
  alpm_list_t* candidates = nullptr;
  for (alpm_pkg_t* package : added) {
    candidates = alpm_list_add(candidates, package);
  }
  std::set<std::string> step{UpgradeResolver::kKeyringPackage};
  std::vector<alpm_pkg_t*> pending{*keyring};
  while (!pending.empty()) {
    alpm_pkg_t* current = pending.back();
    pending.pop_back();
    for (const alpm_list_t* dep = alpm_pkg_get_depends(current); dep != nullptr; dep = dep->next) {
      const std::string text = dependencyText(static_cast<const alpm_depend_t*>(dep->data));
      alpm_pkg_t* satisfier = alpm_find_satisfier(candidates, text.c_str());
      if (satisfier != nullptr && step.insert(alpm_pkg_get_name(satisfier)).second) {
        pending.push_back(satisfier);
      }
    }
  }
  alpm_list_free(candidates);
  return {step.begin(), step.end()};
}

}  // namespace

std::expected<UpgradeDetail, TransactionFailure> UpgradeResolver::resolve(ResolveMode mode) {
  alpm_handle_t* handle = session_->handle();
  const int flags = mode == ResolveMode::Preview ? ALPM_TRANS_FLAG_NOLOCK : 0;
  if (!session_->beginTransaction(flags)) {
    return std::unexpected(refuse(mapAlpmError(alpm_errno(handle), FailureContext::Resolve)));
  }
  auto detail = resolveInOpenTransaction();
  if (mode == ResolveMode::Preview || !detail) {
    session_->releaseTransaction();
  }
  return detail;
}

std::expected<UpgradeDetail, TransactionFailure> UpgradeResolver::resolveInOpenTransaction() {
  alpm_handle_t* handle = session_->handle();
  for (const auto& repository : session_->repositories()) {
    const auto file = std::filesystem::path(alpm_option_get_dbpath(handle)) / "sync" / (repository.name + ".db");
    if (!std::filesystem::exists(file) || alpm_db_get_valid(repository.db) != 0) {
      return std::unexpected(refuse(TransactionErrorCode::DatabaseUnavailable));
    }
  }
  auto protectedSet = computeProtectedSet(handle, session_->settings().options.hold_pkgs);
  if (!protectedSet) {
    return std::unexpected(protectedSet.error());
  }
  const std::vector<std::string> protectedNames(protectedSet->begin(), protectedSet->end());

  session_->questions().clear();
  session_->questions().setUpgradeMode(true);
  if (alpm_sync_sysupgrade(handle, 0) != 0) {
    if (!session_->questions().decisions().empty()) {
      return std::unexpected(decision(session_->questions().decisions()));
    }
    return std::unexpected(refuse(mapAlpmError(alpm_errno(handle), FailureContext::Resolve)));
  }
  alpm_list_t* data = nullptr;
  if (alpm_trans_prepare(handle, &data) != 0) {
    const alpm_errno_t error = alpm_errno(handle);
    TransactionFailure failure = refuse(mapAlpmError(error, FailureContext::Resolve));
    if (session_->questions().keyImportRefused()) {
      failure = refuse(TransactionErrorCode::KeyImportRequired);
    } else if (!session_->questions().decisions().empty()) {
      failure = decision(session_->questions().decisions());
    } else if (error == ALPM_ERR_UNSATISFIED_DEPS) {
      failure = unsatisfied(data);
    } else if (error == ALPM_ERR_CONFLICTING_DEPS) {
      failure = decision(conflictChoices(data));
    }
    freePrepareData(error, data);
    return std::unexpected(std::move(failure));
  }
  // A package that cannot be upgraded because its dependencies cannot be met (libalpm offered to skip it).
  if (!session_->questions().unresolvable().empty()) {
    return std::unexpected(
        refuse(TransactionErrorCode::PackageNotFound, sortedUnique(session_->questions().unresolvable())));
  }
  if (!session_->questions().decisions().empty()) {
    return std::unexpected(decision(session_->questions().decisions()));
  }
  return classify(protectedNames);
}

std::expected<UpgradeDetail, TransactionFailure> UpgradeResolver::classify(
    const std::vector<std::string>& protectedNames) {
  alpm_handle_t* handle = session_->handle();
  alpm_db_t* local = alpm_get_localdb(handle);

  // Packages that leave the system: replaced ones. Anything else would be a conflict removal, which is refused.
  std::map<std::string, alpm_pkg_t*> removed = removedPackages(handle);
  if (auto refusal = protectedRemoval(removed, protectedNames)) {
    return std::unexpected(std::move(*refusal));
  }
  std::vector<alpm_pkg_t*> added;
  added.reserve(alpm_list_count(alpm_trans_get_add(handle)));
  for (const alpm_list_t* item = alpm_trans_get_add(handle); item != nullptr; item = item->next) {
    added.push_back(static_cast<alpm_pkg_t*>(item->data));
  }
  UpgradeDetail detail;
  classifyAdded(added, local, session_->questions().acceptedReplacements(), removed, detail);
  if (!removed.empty()) {
    std::vector<NeedsDecisionChoice> choices;
    choices.reserve(removed.size());
    for (const auto& [name, package] : removed) {
      choices.push_back(
          NeedsDecisionChoice{.kind = NeedsDecisionChoice::Kind::Conflict, .subject = name, .candidates = {name}});
    }
    return std::unexpected(decision(std::move(choices)));
  }
  detail.held_back = heldBack(handle, local);
  detail.keyring_packages = keyringStep(added);

  std::ranges::sort(detail.upgrades, {}, &UpgradeEntry::name);
  std::ranges::sort(detail.replacements, {}, &ReplacementEntry::new_name);
  std::ranges::sort(detail.new_dependencies, {}, &NewDependencyEntry::name);
  std::ranges::sort(detail.held_back, {}, &HeldBackEntry::name);
  if (detail.empty()) {
    return std::unexpected(refuse(TransactionErrorCode::NothingToDo));
  }
  return detail;
}

}  // namespace holonight_packages_transaction
