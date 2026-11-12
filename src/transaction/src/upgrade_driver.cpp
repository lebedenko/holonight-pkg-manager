#include "upgrade_driver.h"

#include "fetch_seam.h"
#include "guard_snapshot.h"
#include "preparation_control.h"
#include "promotion.h"
#include "upgrade_resolver.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sys/statvfs.h>

namespace holonight_packages_transaction {
namespace {

namespace fs = std::filesystem;
using holonight_packages_domain::ChangeAction;
using holonight_packages_domain::ChangedPackage;
using holonight_packages_domain::ExecutionContext;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::ProgressUpdate;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;
using holonight_packages_domain::UpgradeDetail;
using holonight_packages_domain::UpgradeWorkspace;
using holonight_packages_domain::WarningToken;

constexpr std::string_view kKeyMissing = "required key missing from keyring";

ExecutionResult failed(TransactionErrorCode code, std::vector<std::string> names = {}) {
  return ExecutionResult::failed(TransactionFailure{.code = code, .names = std::move(names), .choices = {}});
}

// The environment errors keep their own code at a re-resolution; any other refusal means the plan changed (helper
// REQ-F-045, system-upgrade REQ-F-052).
TransactionErrorCode reresolutionCode(TransactionErrorCode code) {
  switch (code) {
    case TransactionErrorCode::Busy:
    case TransactionErrorCode::InternalError:
    case TransactionErrorCode::ConfigurationError:
    case TransactionErrorCode::DatabaseUnavailable:
    case TransactionErrorCode::InsecureSigLevel:
    case TransactionErrorCode::NeedsDecision:
    case TransactionErrorCode::KeyImportRequired:
      return code;
    default:
      return TransactionErrorCode::PlanChanged;
  }
}

std::vector<std::string> plannedNames(const UpgradeDetail& detail) {
  std::vector<std::string> names;
  names.reserve(detail.upgrades.size() + detail.replacements.size() + detail.new_dependencies.size());
  for (const auto& entry : detail.upgrades) {
    names.push_back(entry.name);
  }
  for (const auto& entry : detail.replacements) {
    names.push_back(entry.new_name);
  }
  for (const auto& entry : detail.new_dependencies) {
    names.push_back(entry.name);
  }
  std::ranges::sort(names);
  names.erase(std::ranges::unique(names).begin(), names.end());
  return names;
}

// The preview without the packages of the keyring step: what the main step must still change.
UpgradeDetail withoutKeyring(const UpgradeDetail& detail) {
  const std::set<std::string> keyring(detail.keyring_packages.begin(), detail.keyring_packages.end());
  UpgradeDetail rest = detail;
  std::erase_if(rest.upgrades, [&keyring](const auto& entry) { return keyring.contains(entry.name); });
  std::erase_if(rest.replacements, [&keyring](const auto& entry) { return keyring.contains(entry.new_name); });
  std::erase_if(rest.new_dependencies, [&keyring](const auto& entry) { return keyring.contains(entry.name); });
  rest.keyring_packages.clear();
  return rest;
}

bool sameChanges(const UpgradeDetail& left, const UpgradeDetail& right) {
  return left.upgrades == right.upgrades && left.replacements == right.replacements &&
         left.new_dependencies == right.new_dependencies && left.held_back == right.held_back;
}

std::optional<fs::path> cachedFile(alpm_handle_t* handle, const char* file_name) {
  if (file_name == nullptr) {
    return std::nullopt;
  }
  for (const alpm_list_t* item = alpm_option_get_cachedirs(handle); item != nullptr; item = item->next) {
    fs::path candidate = fs::path(static_cast<const char*>(item->data)) / file_name;
    std::error_code error;
    if (fs::is_regular_file(candidate, error)) {
      return candidate;
    }
  }
  return std::nullopt;
}

}  // namespace

UpgradeDriver::UpgradeDriver(const TransactionConfig& config, LogSink journal, const TransactionSeams& seams,
                             const ResolvedPlan& approved, const UpgradeWorkspace& workspace,
                             const ExecutionContext& context)
    : config_(&config),
      journal_(std::move(journal)),
      seams_(&seams),
      approved_(&approved),
      workspace_(&workspace),
      context_(&context),
      scratch_{.root = workspace.scratch_root},
      reporter_(approved.upgrade ? plannedNames(*approved.upgrade) : std::vector<std::string>{}) {}

void UpgradeDriver::observe(std::string_view stage) const {
  if (seams_->observe) {
    seams_->observe(stage);
  }
}

int UpgradeDriver::fault(std::string_view stage) const {
  return seams_->inject_fault ? seams_->inject_fault(stage) : 0;
}

void UpgradeDriver::emit(ProgressPhase phase, std::uint32_t current, std::uint32_t total) const {
  if (context_->progress) {
    context_->progress(ProgressUpdate{.phase = phase, .current = current, .total = total, .item = {}});
  }
}

ExecutionResult UpgradeDriver::partial(const char* step) const {
  ExecutionResult result = failed(TransactionErrorCode::UpgradePartiallyApplied, {step});
  result.changed = changed_;
  result.warnings = warnings_;
  return result;
}

ExecutionResult UpgradeDriver::run() {
  if (!approved_->upgrade) {
    return failed(TransactionErrorCode::InternalError);
  }
  const ControlPublication publication(*context_, &gate_);

  // Phases 1 and 2: the configuration checks run again inside openUpgrade, and the plan is resolved again before
  // anything is downloaded.
  {
    auto scratch =
        AlpmSession::openUpgrade(*config_, AlpmSession::UpgradeRole::Scratch, scratch_.dbpath(), seams_, journal_);
    if (!scratch) {
      return ExecutionResult::failed(std::move(scratch.error()));
    }
    installFetchSeam((*scratch)->handle(), *seams_);
    observe("opened");
    if (auto stop = recheckAndResolveBeforeFetch(**scratch)) {
      return std::move(*stop);
    }
    if (auto stop = estimateSpace(**scratch)) {  // phase 3
      return std::move(*stop);
    }
    if (auto stop = fetch(**scratch)) {  // phases 4 and 5
      return std::move(*stop);
    }
  }  // phase 6: the scratch handle is released
  observe("fetched");
  if (gate_.cancelRequested()) {
    return ExecutionResult::cancelled();
  }

  // Phase 7: the live handle has only the sentinel server, so nothing after this point can download (DESIGN D-7).
  auto live =
      AlpmSession::openUpgrade(*config_, AlpmSession::UpgradeRole::Live, config_->database_path, seams_, journal_);
  if (!live) {
    return ExecutionResult::failed(std::move(live.error()));
  }
  // A fresh scratch handle for phase 9: the first one read the local database before the fetch (SPIKES S-3).
  auto scratch =
      AlpmSession::openUpgrade(*config_, AlpmSession::UpgradeRole::Scratch, scratch_.dbpath(), seams_, journal_);
  if (!scratch) {
    return ExecutionResult::failed(std::move(scratch.error()));
  }
  if (auto stop = lockGuardAndCompare(**live, **scratch)) {  // phases 7 to 9
    return std::move(*stop);
  }
  scratch->reset();

  // Phase 10: the point of no return.
  if (!gate_.tryCommit()) {
    (*live)->releaseTransaction();
    return ExecutionResult::cancelled();
  }
  emit(ProgressPhase::Promoting, 0, 0);
  observe("promotion-starting");
  SyncPromotion promotion(scratch_.sync(), config_->database_path / "sync", scratch_.rollback(), *seams_, journal_);
  if (!promotion.promote()) {  // phase 11; the live files are rolled back, nothing was installed
    (*live)->releaseTransaction();
    return failed(TransactionErrorCode::InternalError);
  }
  observe("promotion-done");

  if (approved_->upgrade->keyring()) {  // phase 12
    if (auto stop = keyringStep(**live)) {
      return std::move(*stop);
    }
  }
  if (auto stop = mainStep(**live)) {  // phase 13
    return std::move(*stop);
  }
  ExecutionResult result = ExecutionResult::succeeded(changed_);  // phase 14
  result.warnings = warnings_;
  observe("finished");
  return result;
}

std::optional<ExecutionResult> UpgradeDriver::recheckAndResolveBeforeFetch(AlpmSession& scratch) {
  if (gate_.cancelRequested()) {
    return ExecutionResult::cancelled();
  }
  UpgradeResolver resolver(scratch, journal_);
  auto detail = resolver.resolve(ResolveMode::Preview);
  if (!detail) {
    TransactionFailure failure = std::move(detail.error());
    failure.code = reresolutionCode(failure.code);
    if (failure.code == TransactionErrorCode::PlanChanged) {
      failure.names.clear();
      failure.choices.clear();
    }
    return ExecutionResult::failed(std::move(failure));
  }
  ResolvedPlan resolved;
  resolved.action = holonight_packages_domain::PlanAction::Upgrade;
  resolved.upgrade = std::move(*detail);
  if (!holonight_packages_domain::samePlan(*approved_, resolved)) {
    return failed(TransactionErrorCode::PlanChanged);
  }
  return std::nullopt;
}

std::optional<ExecutionResult> UpgradeDriver::estimateSpace(const AlpmSession& scratch) const {
  if (!scratch.settings().options.check_space) {
    return std::nullopt;  // REQ-F-048: no estimate without CheckSpace
  }
  if (gate_.cancelRequested()) {
    return ExecutionResult::cancelled();
  }
  const auto freeBytes = [this](const fs::path& path) -> std::optional<std::uint64_t> {
    if (seams_->free_space) {
      return seams_->free_space(path);
    }
    struct statvfs status{};
    if (::statvfs(path.c_str(), &status) != 0) {
      return std::nullopt;
    }
    return static_cast<std::uint64_t>(status.f_bavail) * status.f_frsize;
  };
  const UpgradeDetail& detail = *approved_->upgrade;
  const alpm_list_t* cacheDirs = alpm_option_get_cachedirs(scratch.handle());
  const fs::path cache =
      cacheDirs != nullptr ? fs::path(static_cast<const char*>(cacheDirs->data)) : config_->cache_directory;
  const std::uint64_t growth = detail.size_change_bytes > 0 ? static_cast<std::uint64_t>(detail.size_change_bytes) : 0;
  const auto cacheFree = freeBytes(cache);
  const auto rootFree = freeBytes(config_->root);
  if ((cacheFree && *cacheFree < detail.download_bytes) || (rootFree && *rootFree < growth + (growth / 20))) {
    return failed(TransactionErrorCode::DiskSpace);
  }
  return std::nullopt;
}

std::optional<ExecutionResult> UpgradeDriver::fetch(AlpmSession& scratch) {
  // A corrupt download is deleted and fetched once more (REQ-F-046); a second corruption is a failure.
  for (int attempt = 1;; ++attempt) {
    bool retry = false;
    auto stop = fetchOnce(scratch, retry);
    scratch.releaseTransaction();
    if (stop || !retry) {
      return stop;
    }
    if (attempt == 2) {
      return failed(TransactionErrorCode::DownloadFailed);
    }
    if (journal_) {
      journal_(LogLevel::Info, "a corrupt package file was deleted; fetching it once more");
    }
  }
}

std::optional<ExecutionResult> UpgradeDriver::fetchOnce(AlpmSession& scratch, bool& retry) {
  if (gate_.cancelRequested()) {
    return ExecutionResult::cancelled();
  }
  observe("upgrade-fetch");
  alpm_handle_t* handle = scratch.handle();
  scratch.questions().clear();
  scratch.questions().setUpgradeMode(true);
  scratch.questions().setCorruptedAnswer(true);
  mapper_ = std::make_unique<EventMapper>(context_->progress, plannedNames(*approved_->upgrade).size(), 0);
  scratch.setEventHandler([this](const alpm_event_t& event) { mapper_->onEvent(event); });
  scratch.setDownloadHandler([this](const std::string& file, alpm_download_event_type_t type, void* data) {
    mapper_->onDownload(file, type, data);
  });
  scratch.setProgressHandler([this](alpm_progress_t progress, const std::string& item, int, std::size_t total,
                                    std::size_t current) { mapper_->onProgress(progress, item, total, current); });
  if (!scratch.beginTransaction(ALPM_TRANS_FLAG_DOWNLOADONLY)) {
    return failed(mapAlpmError(alpm_errno(handle), FailureContext::Resolve));
  }
  alpm_list_t* data = nullptr;
  if (alpm_sync_sysupgrade(handle, 0) != 0 || alpm_trans_prepare(handle, &data) != 0) {
    return failed(TransactionErrorCode::PlanChanged);  // phase 2 compared it a moment ago
  }
  scratch.clearLog();
  data = nullptr;
  if (alpm_trans_commit(handle, &data) == 0) {
    return std::nullopt;
  }
  const alpm_errno_t error = alpm_errno(handle);
  const auto& questions = scratch.questions();
  // A missing signing key stops libalpm's keyring check after every file was downloaded (SPIKES S-1 c).
  if (logMentions(scratch.logLines(), kKeyMissing) || questions.keyImportRefused()) {
    return verifyCached(scratch, retry);
  }
  const alpm_errno_t reason = questions.lastCorruptedReason();
  if (!questions.corruptedFiles().empty() &&
      (reason == ALPM_ERR_PKG_INVALID_CHECKSUM || reason == ALPM_ERR_PKG_INVALID || reason == ALPM_ERR_PKG_OPEN)) {
    retry = true;
    return std::nullopt;
  }
  ExecutionResult result = reporter_.failureResult(scratch, error, data, CommitStage::BeforeCommitPoint, {});
  if (result.failure.code == TransactionErrorCode::TransactionFailed ||
      result.failure.code == TransactionErrorCode::InternalError) {
    result.failure.code = TransactionErrorCode::DownloadFailed;
  }
  return result;
}

// Phase 5: libalpm's keyring check stopped the fetch, so its integrity step did not run. Every file is checked here:
// checksum against the database entry, signature at the repository's level. A key that only the updated keyring may
// have is deferred to the main step for a package outside the keyring step (DESIGN D-6).
std::optional<ExecutionResult> UpgradeDriver::verifyCached(AlpmSession& scratch, bool& retry) {
  alpm_handle_t* handle = scratch.handle();
  const UpgradeDetail& detail = *approved_->upgrade;
  const std::set<std::string> keyring(detail.keyring_packages.begin(), detail.keyring_packages.end());
  std::vector<std::string> keyMissing;
  for (const alpm_list_t* item = alpm_trans_get_add(handle); item != nullptr; item = item->next) {
    auto* package = static_cast<alpm_pkg_t*>(item->data);
    const std::string name = alpm_pkg_get_name(package);
    const auto file = cachedFile(handle, alpm_pkg_get_filename(package));
    if (!file) {
      return failed(TransactionErrorCode::DownloadFailed, {name});
    }
    const char* expected = alpm_pkg_get_sha256sum(package);
    if (expected != nullptr && sha256Of(*file) != std::string(expected)) {
      std::error_code ignored;
      fs::remove(*file, ignored);
      retry = true;
      return std::nullopt;
    }
    alpm_db_t* database = alpm_pkg_get_db(package);
    const int level = database != nullptr ? alpm_db_get_siglevel(database) : alpm_option_get_default_siglevel(handle);
    scratch.clearLog();
    alpm_pkg_t* loaded = nullptr;
    const int status = alpm_pkg_load(handle, file->c_str(), 1, level, &loaded);
    if (loaded != nullptr) {
      alpm_pkg_free(loaded);
    }
    if (status == 0) {
      continue;
    }
    if (logMentions(scratch.logLines(), kKeyMissing)) {
      if (detail.keyring() && !keyring.contains(name)) {
        keyDeferred_ = true;
        continue;
      }
      keyMissing.push_back(name);
      continue;
    }
    return failed(TransactionErrorCode::SignatureInvalid, {name});
  }
  if (!keyMissing.empty()) {
    return failed(TransactionErrorCode::KeyImportRequired, std::move(keyMissing));
  }
  if (journal_ && keyDeferred_) {
    journal_(LogLevel::Info, "a signing key is missing; the check is deferred to the main step");
  }
  return std::nullopt;
}

std::optional<ExecutionResult> UpgradeDriver::lockGuardAndCompare(AlpmSession& live, AlpmSession& scratch) {
  if (gate_.cancelRequested()) {
    return ExecutionResult::cancelled();
  }
  if (!live.beginTransaction(0)) {
    const alpm_errno_t error = alpm_errno(live.handle());
    return failed(error == ALPM_ERR_HANDLE_LOCK ? TransactionErrorCode::Busy
                                                : mapAlpmError(error, FailureContext::Resolve));
  }
  observe("lock-taken");
  const auto release = [&live](ExecutionResult result) {
    live.releaseTransaction();
    return std::optional<ExecutionResult>(std::move(result));
  };
  const auto current = snapshotSyncFiles(config_->database_path / "sync");
  if (!current || *current != workspace_->guard) {
    if (journal_) {
      journal_(LogLevel::Info, "the live sync databases changed after the preparation");
    }
    return release(failed(TransactionErrorCode::SyncDatabaseChanged));
  }
  observe("guard-checked");
  if (gate_.cancelRequested()) {
    return release(ExecutionResult::cancelled());
  }
  // Phase 9: the scratch databases with the live local database, which no other process can change now.
  UpgradeResolver resolver(scratch, journal_);
  auto detail = resolver.resolve(ResolveMode::Preview);
  if (!detail) {
    TransactionFailure failure = std::move(detail.error());
    failure.code = reresolutionCode(failure.code);
    if (failure.code == TransactionErrorCode::PlanChanged) {
      failure.names.clear();
      failure.choices.clear();
    }
    return release(ExecutionResult::failed(std::move(failure)));
  }
  ResolvedPlan resolved;
  resolved.action = holonight_packages_domain::PlanAction::Upgrade;
  resolved.upgrade = std::move(*detail);
  if (!holonight_packages_domain::samePlan(*approved_, resolved)) {
    return release(failed(TransactionErrorCode::PlanChanged));
  }
  return std::nullopt;
}

std::optional<ExecutionResult> UpgradeDriver::keyringStep(AlpmSession& live) {
  const UpgradeDetail& detail = *approved_->upgrade;
  emit(ProgressPhase::KeyringStep, 1, 2);
  observe("keyring-step");
  alpm_handle_t* handle = live.handle();
  live.questions().clear();
  live.questions().setUpgradeMode(true);
  // The live handle reads the sync databases for the first time here, after promotion (SPIKES S-3, item 7).
  for (const std::string& name : detail.keyring_packages) {
    alpm_pkg_t* package = nullptr;
    for (const alpm_list_t* db = alpm_get_syncdbs(handle); db != nullptr && package == nullptr; db = db->next) {
      package = alpm_db_get_pkg(static_cast<alpm_db_t*>(db->data), name.c_str());
    }
    if (package == nullptr || alpm_add_pkg(handle, package) != 0) {
      live.releaseTransaction();
      return partial("keyring");
    }
  }
  alpm_list_t* data = nullptr;
  if (alpm_trans_prepare(handle, &data) != 0) {
    live.releaseTransaction();
    return partial("keyring");
  }
  ExecutionResult result = commitStep(live, "keyring", detail.keyring_packages.size(), 0, false);
  live.releaseTransaction();
  if (result.outcome != holonight_packages_domain::ExecutionOutcome::Succeeded) {
    return result;
  }
  // The lock is free between the two transactions (DESIGN 12.3, R-4); another package manager taking it ends here.
  if (!live.beginTransaction(0)) {
    return partial("main");
  }
  return std::nullopt;
}

std::optional<ExecutionResult> UpgradeDriver::mainStep(AlpmSession& live) {
  const UpgradeDetail& detail = *approved_->upgrade;
  const std::uint8_t steps = detail.stepCount();
  emit(ProgressPhase::MainStep, steps, steps);
  observe("main-step");
  UpgradeResolver resolver(live, journal_);
  auto resolved = resolver.resolveInOpenTransaction();
  const UpgradeDetail expected = withoutKeyring(detail);
  if (!resolved && resolved.error().code == TransactionErrorCode::NothingToDo && expected.empty()) {
    live.releaseTransaction();
    return std::nullopt;
  }
  if (!resolved || !sameChanges(*resolved, expected)) {
    live.releaseTransaction();
    if (journal_) {
      journal_(LogLevel::Error, "the main step does not match the approved plan after promotion");
    }
    return partial("main");
  }
  std::size_t removals = 0;
  for (const alpm_list_t* item = alpm_trans_get_remove(live.handle()); item != nullptr; item = item->next) {
    ++removals;
  }
  ExecutionResult result = commitStep(live, "main", plannedNames(expected).size(), removals, true);
  live.releaseTransaction();
  if (result.outcome != holonight_packages_domain::ExecutionOutcome::Succeeded) {
    return result;
  }
  return std::nullopt;
}

ExecutionResult UpgradeDriver::commitStep(AlpmSession& live, const char* step, std::size_t installs,
                                          std::size_t removals, bool last) {
  alpm_handle_t* handle = live.handle();
  mapper_ = std::make_unique<EventMapper>(context_->progress, installs, removals);
  live.setEventHandler([this, handle](const alpm_event_t& event) {
    onStepEvent(event, handle);
    mapper_->onEvent(event);
  });
  live.setDownloadHandler([this](const std::string& file, alpm_download_event_type_t type, void* data) {
    mapper_->onDownload(file, type, data);
  });
  live.setProgressHandler([this](alpm_progress_t progress, const std::string& item, int, std::size_t total,
                                 std::size_t current) { mapper_->onProgress(progress, item, total, current); });
  if (fault(std::string(step) + "-commit") != 0) {
    return partial(step);
  }
  live.clearLog();
  alpm_list_t* data = nullptr;
  const int status = alpm_trans_commit(handle, &data);
  if (status != 0 || injectedFault_ != 0) {
    if (journal_ && status != 0) {
      journal_(LogLevel::Error, std::string("the ") + step + " step failed: " + alpm_strerror(alpm_errno(handle)));
    }
    return partial(step);
  }
  if (logMentions(live.logLines(), "command failed to execute correctly") &&
      std::ranges::find(warnings_, WarningToken::HookFailed) == warnings_.end()) {
    warnings_.push_back(WarningToken::HookFailed);
  }
  if (last) {
    mapper_->finish();
  }
  return ExecutionResult::succeeded(changed_);
}

void UpgradeDriver::onStepEvent(const alpm_event_t& event, alpm_handle_t* handle) {
  if (event.type != ALPM_EVENT_PACKAGE_OPERATION_DONE) {
    return;
  }
  const auto& operation = event.package_operation;
  if (operation.operation == ALPM_PACKAGE_REMOVE) {
    changed_.push_back(ChangedPackage{
        .name = alpm_pkg_get_name(operation.oldpkg),
        .action = ChangeAction::Remove,
        .old_version = alpm_pkg_get_version(operation.oldpkg),
        .new_version = {},
    });
  } else {
    changed_.push_back(ChangedPackage{
        .name = alpm_pkg_get_name(operation.newpkg),
        .action = ChangeAction::Install,
        .old_version = operation.oldpkg != nullptr ? alpm_pkg_get_version(operation.oldpkg) : "",
        .new_version = alpm_pkg_get_version(operation.newpkg),
    });
  }
  if (const int injected = fault("package-done"); injected != 0 && injectedFault_ == 0) {
    injectedFault_ = injected;
    static_cast<void>(alpm_trans_interrupt(handle));  // the remaining packages are not changed
  }
}

}  // namespace holonight_packages_transaction
