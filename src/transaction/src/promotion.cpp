#include "promotion.h"

#include "guard_snapshot.h"
#include "holonight_packages_transaction/upgrade_recovery.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <map>
#include <set>
#include <system_error>
#include <unistd.h>

namespace holonight_packages_transaction {
namespace {

namespace fs = std::filesystem;
constexpr std::string_view kTemporaryPrefix = ".hn-promote-";

void report(const LogSink& journal, const std::string& operation, const fs::path& from, const fs::path& target,
            const std::error_code& error, LogLevel level = LogLevel::Error) {
  if (journal) {
    journal(level, redactUrlCredentials(operation + " source=" + from.string() + " destination=" + target.string() +
                                        " error=" + std::to_string(error.value()) +
                                        " category=" + error.category().name() + " message=" + error.message()));
  }
}

std::error_code systemError() { return {errno, std::generic_category()}; }

// Flush failures are diagnostic only; preserve the existing best-effort semantics.
void flush(const fs::path& path, int flags, const LogSink& journal) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) is variadic.
  const int descriptor = ::open(path.c_str(), flags | O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) {
    const auto error = systemError();
    report(journal, "open for flush", path, {}, error);
    return;
  }
  if (::fsync(descriptor) != 0) {
    const auto error = systemError();
    report(journal, "fsync", path, {}, error);
  }
  if (::close(descriptor) != 0) {
    const auto error = systemError();
    report(journal, "close after flush", path, {}, error);
  }
}

void syncDirectory(const fs::path& directory, const LogSink& journal) { flush(directory, O_DIRECTORY, journal); }

bool copyThenRename(const fs::path& from, const fs::path& target, const LogSink& journal) {
  const fs::path temporary =
      target.parent_path() / (std::string(kTemporaryPrefix) + target.filename().string() + ".tmp");
  std::error_code error;
  std::string operation = "copy_file";
  fs::copy_file(from, temporary, fs::copy_options::overwrite_existing, error);
  if (!error) {
    operation = "read last_write_time";
    const auto time = fs::last_write_time(from, error);
    if (!error) {
      operation = "set last_write_time";
      fs::last_write_time(temporary, time, error);
    }
  }
  if (!error) {
    operation = "permissions";
    fs::permissions(temporary,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read,
                    error);
  }
  if (!error) {
    flush(temporary, 0, journal);
    operation = "rename";
    if (std::rename(temporary.c_str(), target.c_str()) != 0) {
      error = systemError();
    }
  }
  if (error) {
    report(journal, operation,
           operation == "rename" || operation == "permissions" || operation == "set last_write_time" ? temporary : from,
           operation == "copy_file" ? temporary : target, error);
    std::error_code cleanup_error;
    fs::remove(temporary, cleanup_error);
    if (cleanup_error) {
      report(journal, "remove temporary", temporary, {}, cleanup_error);
    }
    return false;
  }
  return true;
}

}  // namespace

SyncPromotion::SyncPromotion(fs::path scratch_sync, fs::path live_sync, fs::path rollback,
                             const TransactionSeams& seams, LogSink journal)
    : scratch_(std::move(scratch_sync)),
      live_(std::move(live_sync)),
      rollback_(std::move(rollback)),
      seams_(&seams),
      journal_(std::move(journal)) {}

std::optional<std::vector<SyncPromotion::Step>> SyncPromotion::plan() const {
  std::set<std::string> repositories;
  std::error_code error;
  fs::directory_iterator iterator(scratch_, error);
  if (error) {
    report(journal_, "directory_iterator", scratch_, {}, error);
    return std::nullopt;
  }
  const fs::directory_iterator end;
  for (; iterator != end; iterator.increment(error)) {
    if (error) {
      break;
    }
    const auto& entry = *iterator;
    const std::string name = entry.path().filename().string();
    if (name.ends_with(".db")) {
      repositories.insert(name.substr(0, name.size() - 3));
    }
  }
  if (error) {
    report(journal_, "directory increment", scratch_, {}, error);
    return std::nullopt;
  }
  std::vector<Step> steps;
  for (const std::string& repository : repositories) {
    const std::string signature = repository + ".db.sig";
    const bool scratch_signature = fs::exists(scratch_ / signature, error);
    if (error) {
      report(journal_, "exists signature", scratch_ / signature, {}, error);
      return std::nullopt;
    }
    if (scratch_signature) {
      steps.push_back(Step{.name = signature, .remove = false});
    } else if (fs::exists(live_ / signature, error)) {
      steps.push_back(Step{.name = signature, .remove = true});
    }
    if (error) {
      report(journal_, "exists signature", scratch_ / signature, live_ / signature, error);
      return std::nullopt;
    }
    steps.push_back(Step{.name = repository + ".db", .remove = false});
  }
  return steps;
}

bool SyncPromotion::replace(const std::string& name) const {
  const fs::path from = scratch_ / name;
  const fs::path target = live_ / name;
  if (seams_->promote_by_copy) {
    return copyThenRename(from, target, journal_);
  }
  if (std::rename(from.c_str(), target.c_str()) == 0) {
    return true;
  }
  const auto error = systemError();
  if (error.value() != EXDEV) {
    report(journal_, "rename", from, target, error);
    return false;
  }
  report(journal_, "rename fallback=copy-then-rename", from, target, error, LogLevel::Info);
  return copyThenRename(from, target, journal_);
}

void SyncPromotion::logError(const std::string& text) const {
  if (journal_) {
    journal_(LogLevel::Error, redactUrlCredentials(text));
  }
}

std::optional<SyncPromotion::Digests> SyncPromotion::scratchDigests(const std::vector<Step>& steps) const {
  Digests expected;
  for (const Step& step : steps) {
    if (step.remove) {
      continue;
    }
    auto digest = sha256Of(scratch_ / step.name);
    if (!digest) {
      logError("cannot hash scratch database source=" + (scratch_ / step.name).string());
      return std::nullopt;
    }
    expected[step.name] = std::move(*digest);
  }
  return expected;
}

// Backs up the live file if it exists, then replaces or removes it. Records the change in `applied` only on success.
bool SyncPromotion::applyStep(const Step& step, int number, Applied& applied) const {
  const fs::path live = live_ / step.name;
  std::error_code error;
  const bool existed = fs::exists(live, error);
  if (error) {
    report(journal_, "exists live", live, {}, error);
    return false;
  }
  if (existed && !copyThenRename(live, rollback_ / step.name, journal_)) {
    logError("cannot back up " + step.name);
    return false;
  }
  const int fault = seams_->inject_fault ? seams_->inject_fault("promotion-rename-" + std::to_string(number)) : 0;
  if (fault != 0) {
    logError("injected promotion failure source=" + (scratch_ / step.name).string() + " destination=" + live.string() +
             " fault=" + std::to_string(fault));
  }
  const bool done = fault == 0 && (step.remove ? fs::remove(live, error) : replace(step.name));
  if (step.remove && error) {
    report(journal_, "remove signature", live, {}, error);
  }
  if (!done) {
    logError("cannot promote " + step.name);
    return false;
  }
  (existed ? applied.replaced : applied.added).push_back(step.name);
  return true;
}

bool SyncPromotion::matchesScratch(const Digests& expected) const {
  return std::ranges::all_of(expected, [this](const auto& entry) {
    const auto digest = sha256Of(live_ / entry.first);
    if (digest != entry.second) {
      logError("verify promoted database source=" + (scratch_ / entry.first).string() + " destination=" +
               (live_ / entry.first).string() + (digest ? " hash mismatch" : " cannot hash destination"));
      return false;
    }
    return true;
  });
}

bool SyncPromotion::promote() {
  const auto steps = plan();
  if (!steps) {
    return false;
  }
  const auto expected = scratchDigests(*steps);
  if (!expected) {
    return false;
  }
  Applied applied;
  int number = 0;
  for (const Step& step : *steps) {
    if (!applyStep(step, ++number, applied)) {
      rollBack(applied);
      return false;
    }
  }
  syncDirectory(live_, journal_);
  if (!matchesScratch(*expected)) {
    logError("a promoted database does not match its scratch copy");
    rollBack(applied);
    return false;
  }
  return true;
}

void SyncPromotion::rollBack(const Applied& applied) const {
  std::error_code error;
  for (const std::string& name : applied.replaced) {
    if (!copyThenRename(rollback_ / name, live_ / name, journal_)) {
      logError("cannot restore " + name + " after a failed promotion");
    }
  }
  for (const std::string& name : applied.added) {
    fs::remove(live_ / name, error);
    if (error) {
      report(journal_, "rollback remove", live_ / name, {}, error);
    }
  }
  syncDirectory(live_, journal_);
}

void sweepUpgradeLeftovers(const TransactionConfig& config) noexcept {
  removePromotionLeftovers(config.database_path / "sync");
}

void removePromotionLeftovers(const fs::path& live_sync) noexcept {
  std::error_code error;
  for (const auto& entry : fs::directory_iterator(live_sync, error)) {
    if (entry.path().filename().string().starts_with(kTemporaryPrefix)) {
      fs::remove(entry.path(), error);
    }
  }
}

}  // namespace holonight_packages_transaction
