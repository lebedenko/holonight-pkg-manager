#include "plan_resolver.h"

#include "dependency_text.h"
#include "error_mapping.h"
#include "hook_preview.h"
#include "protected_set.h"
#include "signature_policy.h"
#include "version_policy.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <span>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::ChangeKind;
using holonight_packages_domain::NeedsDecisionChoice;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlannedChange;
using holonight_packages_domain::PlanRequest;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;

TransactionFailure refuse(TransactionErrorCode code, std::vector<std::string> names = {}) {
  return TransactionFailure{.code = code, .names = std::move(names), .choices = {}};
}

std::vector<std::string> sortedUnique(std::vector<std::string> names) {
  std::ranges::sort(names);
  names.erase(std::ranges::unique(names).begin(), names.end());
  return names;
}

// The result of an exact-name lookup in one database. libalpm reports "not there" and "database unusable" through the
// same null return, so the error code decides.
enum class Lookup : unsigned char { Found, Missing, DatabaseError };

struct Found {
  Lookup result = Lookup::Missing;
  alpm_pkg_t* package = nullptr;
};

Found lookup(alpm_handle_t* handle, alpm_db_t* database, const std::string& name) {
  alpm_pkg_t* package = alpm_db_get_pkg(database, name.c_str());
  if (package != nullptr) {
    return Found{.result = Lookup::Found, .package = package};
  }
  const bool absent = alpm_errno(handle) == ALPM_ERR_PKG_NOT_FOUND;
  return Found{.result = absent ? Lookup::Missing : Lookup::DatabaseError, .package = nullptr};
}

std::string repositoryOf(alpm_pkg_t* package) {
  alpm_db_t* database = alpm_pkg_get_db(package);
  return database != nullptr ? alpm_db_get_name(database) : std::string();
}

void sortChanges(std::vector<PlannedChange>& changes) {
  std::ranges::sort(changes, [](const PlannedChange& left, const PlannedChange& right) {
    return std::tie(left.kind, left.name) < std::tie(right.kind, right.name);
  });
}

// Frees the data list that alpm_trans_prepare leaves behind on failure.
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

class Resolution {
 public:
  Resolution(AlpmSession& session, const LogSink& journal, const PlanRequest& request, ResolveMode mode)
      : session_(&session), journal_(&journal), request_(&request), mode_(mode), handle_(session.handle()) {}

  std::expected<ResolvedPlan, TransactionFailure> run() {
    return request_->action == PlanAction::Install ? install() : remove();
  }

 private:
  [[nodiscard]] std::filesystem::path syncDatabaseFile(const std::string& repository) const {
    return std::filesystem::path(alpm_option_get_dbpath(handle_)) / "sync" / (repository + ".db");
  }

  // ---- install ----

  // A database that is missing or unusable is an environment error, not an absent package.
  std::optional<TransactionFailure> checkDatabases() {
    for (const auto& repository : session_->repositories()) {
      if (!std::filesystem::exists(syncDatabaseFile(repository.name)) || alpm_db_get_valid(repository.db) != 0) {
        return refuse(TransactionErrorCode::DatabaseUnavailable);
      }
      // Loading the package cache is what fails for a file that is not a database archive.
      if (alpm_db_get_pkgcache(repository.db) == nullptr && alpm_errno(handle_) != ALPM_ERR_OK) {
        return refuse(TransactionErrorCode::DatabaseUnavailable);
      }
    }
    return std::nullopt;
  }

  // The first repository, in configuration order, that has `name` exactly.
  std::expected<alpm_pkg_t*, TransactionFailure> findInRepositories(const std::string& name) {
    for (const auto& repository : session_->repositories()) {
      const Found result = lookup(handle_, repository.db, name);
      if (result.result == Lookup::DatabaseError) {
        return std::unexpected(refuse(TransactionErrorCode::DatabaseUnavailable));
      }
      if (result.result == Lookup::Found) {
        return result.package;
      }
    }
    return nullptr;
  }

  // Stage one: every target must exist, none may be ignored, the signature policy must hold.
  std::expected<std::vector<alpm_pkg_t*>, TransactionFailure> findInstallTargets() {
    std::vector<alpm_pkg_t*> targets;
    std::vector<std::string> missing;
    for (const std::string& name : request_->names) {
      auto found = findInRepositories(name);
      if (!found) {
        return std::unexpected(found.error());
      }
      if (*found == nullptr) {
        missing.push_back(name);
      } else {
        targets.push_back(*found);
      }
    }
    if (!missing.empty()) {
      return std::unexpected(refuse(TransactionErrorCode::PackageNotFound, std::move(missing)));
    }
    if (auto ignored = ignoredTargets(targets)) {
      return std::unexpected(std::move(*ignored));
    }
    if (auto weak = signatureRefusal()) {
      return std::unexpected(std::move(*weak));
    }
    return targets;
  }

  // Drops targets that are already current, refuses protected ones that would change, and refuses an empty result.
  std::expected<std::vector<alpm_pkg_t*>, TransactionFailure> pendingTargets(
      const std::vector<alpm_pkg_t*>& targets, const std::set<std::string>& protectedNames) {
    alpm_db_t* local = alpm_get_localdb(handle_);
    std::vector<std::string> protectedTargets;
    std::vector<alpm_pkg_t*> pending;
    for (alpm_pkg_t* target : targets) {
      const std::string name = alpm_pkg_get_name(target);
      alpm_pkg_t* installed = alpm_db_get_pkg(local, name.c_str());
      const bool current = installed != nullptr &&
                           packageVersionOrder(alpm_pkg_get_version(installed), alpm_pkg_get_version(target)) == 0;
      if (installed != nullptr && !current && protectedNames.contains(name)) {
        protectedTargets.push_back(name);
      }
      if (!current) {
        pending.push_back(target);
      }
    }
    if (!protectedTargets.empty()) {
      return std::unexpected(refuse(TransactionErrorCode::ProtectedPackage, sortedUnique(std::move(protectedTargets))));
    }
    if (pending.empty()) {
      return std::unexpected(refuse(TransactionErrorCode::NothingToDo));
    }
    return pending;
  }

  // Stage two: let libalpm resolve the dependencies.
  std::expected<ResolvedPlan, TransactionFailure> resolveInstallTransaction(
      const std::vector<alpm_pkg_t*>& pending, const std::set<std::string>& protectedNames) {
    const int flags = mode_ == ResolveMode::Preview ? ALPM_TRANS_FLAG_NOLOCK : 0;
    if (!session_->beginTransaction(flags)) {
      return std::unexpected(refuse(mapAlpmError(alpm_errno(handle_), FailureContext::Resolve)));
    }
    for (alpm_pkg_t* target : pending) {
      if (alpm_add_pkg(handle_, target) != 0) {
        session_->releaseTransaction();
        return std::unexpected(refuse(mapAlpmError(alpm_errno(handle_), FailureContext::Resolve)));
      }
    }
    if (auto prepared = prepare()) {
      session_->releaseTransaction();
      return std::unexpected(std::move(*prepared));
    }
    if (auto replacement = detectReplacements()) {
      session_->releaseTransaction();
      return std::unexpected(std::move(*replacement));
    }
    auto plan = buildInstallPlan(protectedNames);
    if (mode_ == ResolveMode::Preview || !plan) {
      session_->releaseTransaction();
    }
    return plan;
  }

  std::expected<ResolvedPlan, TransactionFailure> install() {
    if (auto unusable = checkDatabases()) {
      return std::unexpected(std::move(*unusable));
    }
    auto targets = findInstallTargets();
    if (!targets) {
      return std::unexpected(targets.error());
    }
    auto protectedNames = computeProtectedSet(handle_, session_->settings().options.hold_pkgs);
    if (!protectedNames) {
      return std::unexpected(protectedNames.error());
    }
    auto pending = pendingTargets(*targets, *protectedNames);
    if (!pending) {
      return std::unexpected(pending.error());
    }
    return resolveInstallTransaction(*pending, *protectedNames);
  }

  std::optional<TransactionFailure> ignoredTargets(const std::vector<alpm_pkg_t*>& targets) {
    std::vector<std::string> ignored;
    for (alpm_pkg_t* target : targets) {
      if (alpm_pkg_should_ignore(handle_, target) != 0) {
        ignored.emplace_back(alpm_pkg_get_name(target));
      }
    }
    if (ignored.empty()) {
      return std::nullopt;
    }
    return refuse(TransactionErrorCode::PackageIgnored, sortedUnique(std::move(ignored)));
  }

  std::optional<TransactionFailure> signatureRefusal() {
    std::vector<RepositoryLevel> levels;
    for (const auto& repository : session_->repositories()) {
      levels.push_back(RepositoryLevel{
          .name = repository.name,
          .level = repository.sig_level,
          .explicit_level = repository.sig_level_explicit,
      });
    }
    return checkInstallSignatureLevels(levels, *journal_);
  }

  // alpm_trans_prepare, mapped to the stage-two refusals of REQ-F-030.
  std::optional<TransactionFailure> prepare() {
    alpm_list_t* data = nullptr;
    const int result = alpm_trans_prepare(handle_, &data);
    const alpm_errno_t error = alpm_errno(handle_);
    std::optional<TransactionFailure> failure;
    if (result != 0) {
      failure = mapPrepareFailure(error, data);
      freePrepareData(error, data);
    }
    if (!failure) {
      if (!session_->questions().decisions().empty()) {
        TransactionFailure decision = refuse(TransactionErrorCode::NeedsDecision);
        decision.choices = session_->questions().decisions();
        failure = std::move(decision);
      } else if (!session_->questions().unresolvable().empty()) {
        failure = refuse(TransactionErrorCode::PackageNotFound, sortedUnique(session_->questions().unresolvable()));
      }
    }
    return failure;
  }

  // libalpm applies `replaces` only during a system upgrade, so an install that would replace an installed package is
  // found here and handed back as a decision (REQ-F-024).
  std::optional<TransactionFailure> detectReplacements() {
    alpm_db_t* local = alpm_get_localdb(handle_);
    std::vector<NeedsDecisionChoice> choices;
    for (const alpm_list_t* item = alpm_trans_get_add(handle_); item != nullptr; item = item->next) {
      auto* added = static_cast<alpm_pkg_t*>(item->data);
      for (const alpm_list_t* replace = alpm_pkg_get_replaces(added); replace != nullptr; replace = replace->next) {
        const std::string text = dependencyText(static_cast<const alpm_depend_t*>(replace->data));
        alpm_pkg_t* installed = alpm_find_satisfier(alpm_db_get_pkgcache(local), text.c_str());
        if (installed != nullptr && std::string(alpm_pkg_get_name(installed)) != alpm_pkg_get_name(added)) {
          choices.push_back(NeedsDecisionChoice{
              .kind = NeedsDecisionChoice::Kind::Replace,
              .subject = alpm_pkg_get_name(installed),
              .candidates = {alpm_pkg_get_name(installed), alpm_pkg_get_name(added)},
          });
        }
      }
    }
    if (choices.empty()) {
      return std::nullopt;
    }
    TransactionFailure decision = refuse(TransactionErrorCode::NeedsDecision);
    decision.choices = std::move(choices);
    return decision;
  }

  TransactionFailure mapPrepareFailure(alpm_errno_t error, alpm_list_t* data) {
    if (error == ALPM_ERR_UNSATISFIED_DEPS) {
      std::vector<std::string> names;
      for (const alpm_list_t* item = data; item != nullptr; item = item->next) {
        const auto* missing = static_cast<const alpm_depmissing_t*>(item->data);
        if (request_->action == PlanAction::Install) {
          names.emplace_back(missing->depend != nullptr && missing->depend->name != nullptr ? missing->depend->name
                                                                                            : "");
        } else {
          names.emplace_back(missing->target != nullptr ? missing->target : "");
        }
      }
      return refuse(request_->action == PlanAction::Install ? TransactionErrorCode::PackageNotFound
                                                            : TransactionErrorCode::BreaksDependents,
                    sortedUnique(std::move(names)));
    }
    if (!session_->questions().decisions().empty()) {
      TransactionFailure decision = refuse(TransactionErrorCode::NeedsDecision);
      decision.choices = session_->questions().decisions();
      return decision;
    }
    if (error == ALPM_ERR_CONFLICTING_DEPS) {
      return refuse(TransactionErrorCode::NeedsDecision);
    }
    return refuse(mapAlpmError(error, FailureContext::Resolve));
  }

  std::expected<ResolvedPlan, TransactionFailure> buildInstallPlan(const std::set<std::string>& protectedNames) {
    alpm_db_t* local = alpm_get_localdb(handle_);
    const std::set<std::string> requested(request_->names.begin(), request_->names.end());
    std::vector<std::string> protectedChanges;
    std::vector<std::string> partial;
    ResolvedPlan plan;
    plan.action = PlanAction::Install;
    for (const alpm_list_t* item = alpm_trans_get_add(handle_); item != nullptr; item = item->next) {
      auto* package = static_cast<alpm_pkg_t*>(item->data);
      const std::string name = alpm_pkg_get_name(package);
      PlannedChange change{
          .name = name,
          .kind = requested.contains(name) ? ChangeKind::Target : ChangeKind::Dependency,
          .old_version = {},
          .new_version = alpm_pkg_get_version(package),
          .repository = repositoryOf(package),
          .download_bytes = static_cast<std::uint64_t>(std::max<off_t>(alpm_pkg_download_size(package), 0)),
      };
      if (alpm_pkg_t* installed = alpm_db_get_pkg(local, name.c_str())) {
        change.old_version = alpm_pkg_get_version(installed);
        if (packageVersionOrder(change.old_version, change.new_version) != 0) {
          if (protectedNames.contains(name)) {
            protectedChanges.push_back(name);
          }
          partial.push_back(name);
        }
      }
      plan.changes.push_back(std::move(change));
    }
    if (!protectedChanges.empty()) {
      return std::unexpected(refuse(TransactionErrorCode::ProtectedPackage, sortedUnique(std::move(protectedChanges))));
    }
    if (!partial.empty()) {
      return std::unexpected(refuse(TransactionErrorCode::PartialUpgradeRequired, sortedUnique(std::move(partial))));
    }
    if (plan.changes.empty()) {
      return std::unexpected(refuse(TransactionErrorCode::NothingToDo));
    }
    sortChanges(plan.changes);
    std::vector<HookInstallFact> facts;
    facts.reserve(plan.changes.size());
    for (const PlannedChange& change : plan.changes) {
      facts.push_back(HookInstallFact{.name = change.name, .upgrade = !change.old_version.empty()});
    }
    plan.hooks = previewHooks(session_->hookDirs(), facts, {}, *journal_);
    return plan;
  }

  // ---- remove ----
  std::expected<ResolvedPlan, TransactionFailure> remove() {
    alpm_db_t* local = alpm_get_localdb(handle_);
    std::vector<alpm_pkg_t*> targets;
    std::vector<std::string> missing;
    for (const std::string& name : request_->names) {
      const Found result = lookup(handle_, local, name);
      if (result.result == Lookup::DatabaseError) {
        return std::unexpected(refuse(TransactionErrorCode::DatabaseUnavailable));
      }
      if (result.result == Lookup::Missing) {
        missing.push_back(name);
      } else {
        targets.push_back(result.package);
      }
    }
    if (!missing.empty()) {
      return std::unexpected(refuse(TransactionErrorCode::PackageNotFound, std::move(missing)));
    }
    if (auto ignored = ignoredTargets(targets)) {
      return std::unexpected(std::move(*ignored));
    }
    auto protectedNames = computeProtectedSet(handle_, session_->settings().options.hold_pkgs);
    if (!protectedNames) {
      return std::unexpected(protectedNames.error());
    }
    std::vector<std::string> protectedTargets;
    for (alpm_pkg_t* target : targets) {
      if (protectedNames->contains(alpm_pkg_get_name(target))) {
        protectedTargets.emplace_back(alpm_pkg_get_name(target));
      }
    }
    if (!protectedTargets.empty()) {
      return std::unexpected(refuse(TransactionErrorCode::ProtectedPackage, sortedUnique(std::move(protectedTargets))));
    }

    const int flags = ALPM_TRANS_FLAG_RECURSE | (mode_ == ResolveMode::Preview ? ALPM_TRANS_FLAG_NOLOCK : 0);
    if (!session_->beginTransaction(flags)) {
      return std::unexpected(refuse(mapAlpmError(alpm_errno(handle_), FailureContext::Resolve)));
    }
    for (alpm_pkg_t* target : targets) {
      if (alpm_remove_pkg(handle_, target) != 0) {
        session_->releaseTransaction();
        return std::unexpected(refuse(mapAlpmError(alpm_errno(handle_), FailureContext::Resolve)));
      }
    }
    if (auto prepared = prepare()) {
      session_->releaseTransaction();
      return std::unexpected(std::move(*prepared));
    }
    auto plan = buildRemovalPlan(*protectedNames);
    if (mode_ == ResolveMode::Preview || !plan) {
      session_->releaseTransaction();
    }
    return plan;
  }

  std::expected<ResolvedPlan, TransactionFailure> buildRemovalPlan(const std::set<std::string>& protectedNames) {
    const std::set<std::string> requested(request_->names.begin(), request_->names.end());
    std::vector<std::string> protectedOrphans;
    ResolvedPlan plan;
    plan.action = PlanAction::Remove;
    for (const alpm_list_t* item = alpm_trans_get_remove(handle_); item != nullptr; item = item->next) {
      auto* package = static_cast<alpm_pkg_t*>(item->data);
      const std::string name = alpm_pkg_get_name(package);
      const bool target = requested.contains(name);
      if (!target && protectedNames.contains(name)) {
        protectedOrphans.push_back(name);
      }
      plan.changes.push_back(PlannedChange{
          .name = name,
          .kind = target ? ChangeKind::Target : ChangeKind::Orphan,
          .old_version = alpm_pkg_get_version(package),
          .new_version = {},
          .repository = {},
          .download_bytes = 0,
      });
    }
    if (!protectedOrphans.empty()) {
      return std::unexpected(refuse(TransactionErrorCode::ProtectedPackage, sortedUnique(std::move(protectedOrphans))));
    }
    sortChanges(plan.changes);
    std::vector<HookRemovalFact> facts;
    facts.reserve(alpm_list_count(alpm_trans_get_remove(handle_)));
    for (const alpm_list_t* item = alpm_trans_get_remove(handle_); item != nullptr; item = item->next) {
      auto* package = static_cast<alpm_pkg_t*>(item->data);
      HookRemovalFact fact{.name = alpm_pkg_get_name(package), .files = {}};
      if (const alpm_filelist_t* list = alpm_pkg_get_files(package)) {
        for (const alpm_file_t& file : std::span<const alpm_file_t>(list->files, list->count)) {
          fact.files.emplace_back(file.name);
        }
      }
      facts.push_back(std::move(fact));
    }
    plan.hooks = previewHooks(session_->hookDirs(), {}, facts, *journal_);
    return plan;
  }

  AlpmSession* session_;
  const LogSink* journal_;
  const PlanRequest* request_;
  ResolveMode mode_;
  alpm_handle_t* handle_;
};

}  // namespace

std::expected<ResolvedPlan, TransactionFailure> PlanResolver::resolve(const PlanRequest& request, ResolveMode mode) {
  return Resolution(*session_, journal_, request, mode).run();
}

}  // namespace holonight_packages_transaction
