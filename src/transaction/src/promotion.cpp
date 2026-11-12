#include "promotion.h"

#include "guard_snapshot.h"
#include "holonight_packages_transaction/upgrade_recovery.h"

#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace holonight_packages_transaction {
namespace {

namespace fs = std::filesystem;
constexpr std::string_view kTemporaryPrefix = ".hn-promote-";

bool sameFilesystem(const fs::path& left, const fs::path& right) {
  struct stat first{};
  struct stat second{};
  return ::stat(left.c_str(), &first) == 0 && ::stat(right.c_str(), &second) == 0 && first.st_dev == second.st_dev;
}

// Flushes a file or directory to disk; failures are ignored (the rename that follows is what makes the change atomic).
void flush(const fs::path& path, int flags) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) is variadic.
  const int descriptor = ::open(path.c_str(), flags | O_RDONLY | O_CLOEXEC);
  if (descriptor >= 0) {
    ::fsync(descriptor);
    ::close(descriptor);
  }
}

void syncDirectory(const fs::path& directory) { flush(directory, O_DIRECTORY); }

// Copies `from` to a temporary file next to `target`, keeps the mtime, flushes it and renames it over `target`.
bool copyThenRename(const fs::path& from, const fs::path& target) {
  const fs::path temporary =
      target.parent_path() / (std::string(kTemporaryPrefix) + target.filename().string() + ".tmp");
  std::error_code error;
  fs::copy_file(from, temporary, fs::copy_options::overwrite_existing, error);
  if (!error) {
    fs::last_write_time(temporary, fs::last_write_time(from, error), error);
  }
  if (!error) {
    fs::permissions(temporary,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read,
                    error);
  }
  if (!error) {
    flush(temporary, 0);
  }
  if (error || std::rename(temporary.c_str(), target.c_str()) != 0) {
    fs::remove(temporary, error);
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

std::vector<SyncPromotion::Step> SyncPromotion::plan() const {
  std::set<std::string> repositories;
  std::error_code error;
  for (const auto& entry : fs::directory_iterator(scratch_, error)) {
    const std::string name = entry.path().filename().string();
    if (name.ends_with(".db")) {
      repositories.insert(name.substr(0, name.size() - 3));
    }
  }
  std::vector<Step> steps;
  for (const std::string& repository : repositories) {
    const std::string signature = repository + ".db.sig";
    if (fs::exists(scratch_ / signature, error)) {
      steps.push_back(Step{.name = signature, .remove = false});
    } else if (fs::exists(live_ / signature, error)) {
      steps.push_back(Step{.name = signature, .remove = true});
    }
    steps.push_back(Step{.name = repository + ".db", .remove = false});
  }
  return steps;
}

bool SyncPromotion::replace(const std::string& name) const {
  const fs::path from = scratch_ / name;
  const fs::path target = live_ / name;
  if (!seams_->promote_by_copy && sameFilesystem(scratch_, live_)) {
    return std::rename(from.c_str(), target.c_str()) == 0;
  }
  return copyThenRename(from, target);
}

bool SyncPromotion::promote() {
  const auto log = [this](const std::string& text) {
    if (journal_) {
      journal_(LogLevel::Error, text);
    }
  };
  const std::vector<Step> steps = plan();
  std::map<std::string, std::string> expected;
  for (const Step& step : steps) {
    if (!step.remove) {
      auto digest = sha256Of(scratch_ / step.name);
      if (!digest) {
        log("cannot hash the scratch database " + step.name);
        return false;
      }
      expected[step.name] = std::move(*digest);
    }
  }
  std::vector<std::string> replaced;
  std::vector<std::string> added;
  std::error_code error;
  int number = 0;
  for (const Step& step : steps) {
    ++number;
    const fs::path live = live_ / step.name;
    const bool existed = fs::exists(live, error);
    if (existed && !copyThenRename(live, rollback_ / step.name)) {
      log("cannot back up " + step.name);
      rollBack(replaced, added);
      return false;
    }
    const int fault = seams_->inject_fault ? seams_->inject_fault("promotion-rename-" + std::to_string(number)) : 0;
    const bool done = fault == 0 && (step.remove ? fs::remove(live, error) : replace(step.name));
    if (!done) {
      log("cannot promote " + step.name);
      rollBack(replaced, added);
      return false;
    }
    (existed ? replaced : added).push_back(step.name);
  }
  syncDirectory(live_);
  const bool matches = std::ranges::all_of(
      expected, [this](const auto& entry) { return sha256Of(live_ / entry.first) == entry.second; });
  if (!matches) {
    log("a promoted database does not match its scratch copy");
    rollBack(replaced, added);
    return false;
  }
  return true;
}

void SyncPromotion::rollBack(const std::vector<std::string>& replaced, const std::vector<std::string>& added) const {
  std::error_code error;
  for (const std::string& name : replaced) {
    if (!copyThenRename(rollback_ / name, live_ / name) && journal_) {
      journal_(LogLevel::Error, "cannot restore " + name + " after a failed promotion");
    }
  }
  for (const std::string& name : added) {
    fs::remove(live_ / name, error);
  }
  syncDirectory(live_);
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
