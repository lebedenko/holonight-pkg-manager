#pragma once

#include "fixture_root.h"
#include "gpg_keyring.h"
#include "gpgme_guard.h"
#include "holonight_packages_transaction/alpm_transaction_engine.h"
#include "manifest.h"
#include "repo_builder.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace holonight_packages_transaction::upgrade_testing {

namespace fs = std::filesystem;
using holonight_packages_domain::ExecutionContext;
using holonight_packages_domain::ExecutionControl;
using holonight_packages_domain::ExecutionOutcome;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::ProgressPhase;
using holonight_packages_domain::ProgressUpdate;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::UpgradeDetail;
using holonight_packages_domain::UpgradePrepareOutcome;
using holonight_packages_domain::UpgradeWorkspace;
using holonight_transaction_testing::ConfSpec;
using holonight_transaction_testing::FixtureRoot;
using holonight_transaction_testing::GpgKeyring;
using holonight_transaction_testing::PackageSpec;
using holonight_transaction_testing::RepoBuilder;
using holonight_transaction_testing::RepoConf;

inline PackageSpec pkg(std::string name, std::string version = "1.0-1", std::vector<std::string> depends = {}) {
  PackageSpec spec;
  spec.files = {{"usr/share/" + name + "/" + version, name + " " + version}};
  spec.name = std::move(name);
  spec.version = std::move(version);
  spec.depends = std::move(depends);
  return spec;
}

inline std::string codeName(TransactionErrorCode code) {
  return std::string(holonight_packages_domain::transactionErrorName(code));
}

// Copies a file:// URL into the destination like libalpm's own downloader, counting every request by its directory.
// Optionally holds every request until release(), so a test can stall the sync or the fetch deterministically.
class RecordingFetch {
 public:
  explicit RecordingFetch(bool hold = false) : held_(hold) {}

  std::function<int(const std::string&, const std::string&, bool)> function() {
    return [this](const std::string& url, const std::string& destination, bool) {
      {
        std::unique_lock lock(mutex_);
        urls_.push_back(url);
        waiting_ += 1;
        changed_.notify_all();
        changed_.wait(lock, [this] { return !held_; });
        waiting_ -= 1;
      }
      const fs::path source = url.starts_with("file://") ? fs::path(url.substr(7)) : fs::path(url);
      std::error_code error;
      if (!fs::exists(source, error)) {
        finish();
        return -1;
      }
      fs::copy_file(source, fs::path(destination) / source.filename(), fs::copy_options::overwrite_existing, error);
      finish();
      return error ? -1 : 0;
    };
  }

  void release() {
    const std::scoped_lock lock(mutex_);
    held_ = false;
    changed_.notify_all();
  }
  bool waitUntilHeld(std::chrono::seconds limit) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, limit, [this] { return waiting_ > 0; });
  }
  // After release(): waits until no request is in flight, so a detached worker is done with the world.
  bool waitUntilIdle(std::chrono::seconds limit) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, limit, [this] { return waiting_ == 0 && active_ == 0; });
  }
  [[nodiscard]] std::vector<std::string> urls() const {
    const std::scoped_lock lock(mutex_);
    return urls_;
  }
  [[nodiscard]] std::size_t count() const {
    const std::scoped_lock lock(mutex_);
    return urls_.size();
  }

 private:
  void finish() {
    const std::scoped_lock lock(mutex_);
    changed_.notify_all();
  }

  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<std::string> urls_;
  bool held_;
  int waiting_ = 0;
  int active_ = 0;
};

// A world with the repositories `core` and `extra`, signed packages, and live databases that lag behind the servers.
class UpgradeTest : public ::testing::Test {
 public:
  UpgradeTest() : keyring_(world_.base() / "signing"), core_(world_, "core"), extra_(world_, "extra") {
    keyring_.trustIn(world_.gpgDir());
    core_.setSigner(keyring_.signer());
    extra_.setSigner(keyring_.signer());
    fs::create_directories(stateDir());
    fs::permissions(stateDir(), fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);
  }

  [[nodiscard]] fs::path stateDir() const { return world_.base() / "state"; }
  [[nodiscard]] fs::path scratchRoot() const { return stateDir() / "upgrade-test"; }

  // Installs `spec` and publishes it in `repo` (live and server alike, as after a sync).
  void installed(RepoBuilder& repo, const PackageSpec& spec) const {
    repo.add(spec);
    world_.installPackage(spec);
  }

  // Writes the databases of every repository to the servers and the live sync directory.
  void publishLive() const {
    core_.publish();
    extra_.publish();
  }

  // Writes the databases to the servers only: the live sync directory keeps its content and becomes a day old, so the
  // live databases lag behind the mirror (the state an upgrade starts from).
  void publishServer() const {
    const fs::path saved = world_.base() / "saved-sync";
    fs::remove_all(saved);
    fs::create_directories(saved);
    for (const auto& entry : fs::directory_iterator(world_.syncDbPath())) {
      fs::copy_file(entry.path(), saved / entry.path().filename());
    }
    publishLive();
    for (const auto& entry : fs::directory_iterator(world_.syncDbPath())) {
      fs::remove(entry.path());
    }
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(24);
    for (const auto& entry : fs::directory_iterator(saved)) {
      const fs::path target = world_.syncDbPath() / entry.path().filename();
      fs::copy_file(entry.path(), target);
      fs::last_write_time(target, old);
    }
  }

  void conf(std::vector<std::string> extraOptions = {}, const std::string& sigLevel = "Required DatabaseOptional",
            const std::vector<std::string>& repositories = {"core", "extra"}) const {
    ConfSpec spec;
    spec.globalSigLevel = sigLevel;
    spec.extraOptions = std::move(extraOptions);
    for (const std::string& name : repositories) {
      spec.repos.push_back(RepoConf{.name = name, .servers = {world_.serverUrl(name)}, .sigLevel = std::nullopt});
    }
    world_.writeConf(spec);
  }

  [[nodiscard]] TransactionConfig config() const {
    TransactionConfig config = world_.config();
    config.upgrade_limits = limits_;
    return config;
  }

  // Each call builds an engine with the current seams. It stays alive until the test ends, because a stuck sync worker
  // may still use its seams after the call that started it returned (in production the engine lives as long as the
  // process).
  [[nodiscard]] AlpmTransactionEngine& engine() {
    TransactionSeams seams;
    seams.observe = [this](std::string_view stage) {
      {
        const std::scoped_lock lock(stagesMutex_);
        stages_.emplace_back(stage);
      }
      if (onStage) {
        onStage(stage);
      }
    };
    seams.inject_fault = injectFault;
    seams.fetch = fetchSeam;
    seams.free_space = freeSpace;
    seams.promote_by_copy = promoteByCopy;
    engines_.push_back(std::make_unique<AlpmTransactionEngine>(
        config(),
        [this](LogLevel, const std::string& line) {
          const std::scoped_lock lock(stagesMutex_);
          journal_.push_back(line);
        },
        std::move(seams)));
    return *engines_.back();
  }

  // A context that records progress and keeps the published control.
  [[nodiscard]] ExecutionContext recordingContext() {
    ExecutionContext context;
    context.progress = [this](const ProgressUpdate& update) {
      const std::scoped_lock lock(stagesMutex_);
      progress_.push_back(update);
    };
    context.control_changed = [this](ExecutionControl* control) {
      if (control != nullptr) {
        control_ = control;
      }
    };
    return context;
  }

  UpgradePrepareOutcome prepare() {
    auto& engine = this->engine();
    UpgradePrepareOutcome outcome = engine.prepareUpgrade(scratchRoot(), recordingContext());
    if (!outcome.result && std::getenv("UPGRADE_DEBUG") != nullptr) {
      dumpJournal("prepare failed: " + codeName(outcome.result.error().code));
    }
    return outcome;
  }

  // Prepares and expects a plan.
  holonight_packages_domain::UpgradePrepared prepared() {
    auto outcome = prepare();
    EXPECT_TRUE(outcome.result.has_value()) << (outcome.result ? "" : codeName(outcome.result.error().code));
    return outcome.result ? *outcome.result : holonight_packages_domain::UpgradePrepared{};
  }

  ExecutionResult execute(const holonight_packages_domain::UpgradePrepared& plan) {
    const UpgradeWorkspace workspace{.scratch_root = scratchRoot(), .guard = plan.guard};
    auto& engine = this->engine();
    ExecutionResult result = engine.executeUpgrade(plan.plan, workspace, recordingContext());
    if (result.outcome == ExecutionOutcome::Failed && std::getenv("UPGRADE_DEBUG") != nullptr) {
      dumpJournal("execute failed: " + codeName(result.failure.code));
    }
    return result;
  }

  void dumpJournal(const std::string& title) {
    const std::scoped_lock lock(stagesMutex_);
    std::cerr << title << '\n';
    for (const std::string& line : journal_) {
      std::cerr << "  journal: " << line << '\n';
    }
  }

  [[nodiscard]] holonight_packages_testing::Manifest liveManifest() const {
    auto sync = holonight_packages_testing::manifestOf(world_.syncDbPath());
    for (auto& [path, entry] : holonight_packages_testing::manifestOf(world_.localDbPath())) {
      sync["local/" + path] = entry;
    }
    return sync;
  }

  [[nodiscard]] std::vector<ProgressPhase> phases() const {
    const std::scoped_lock lock(stagesMutex_);
    std::vector<ProgressPhase> result;
    result.reserve(progress_.size());
    for (const auto& update : progress_) {
      result.push_back(update.phase);
    }
    return result;
  }
  [[nodiscard]] std::vector<ProgressUpdate> updates() const {
    const std::scoped_lock lock(stagesMutex_);
    return progress_;
  }
  [[nodiscard]] std::vector<std::string> stages() const {
    const std::scoped_lock lock(stagesMutex_);
    return stages_;
  }
  [[nodiscard]] int stageCount(std::string_view stage) const {
    const auto all = stages();
    return static_cast<int>(std::count(all.begin(), all.end(), std::string(stage)));
  }

  // First member: destroyed last, after the world and every recording member a stuck worker may reach.
  std::vector<std::unique_ptr<AlpmTransactionEngine>> engines_;
  FixtureRoot world_;
  GpgKeyring keyring_;
  RepoBuilder core_;
  RepoBuilder extra_;
  UpgradeLimits limits_;
  std::function<void(std::string_view)> onStage;
  std::function<int(std::string_view)> injectFault;
  std::function<int(const std::string&, const std::string&, bool)> fetchSeam;
  std::function<std::optional<std::uint64_t>(const fs::path&)> freeSpace;
  bool promoteByCopy = false;
  std::atomic<ExecutionControl*> control_{nullptr};
  mutable std::mutex stagesMutex_;
  std::vector<std::string> stages_;
  std::vector<std::string> journal_;
  std::vector<ProgressUpdate> progress_;
};

}  // namespace holonight_packages_transaction::upgrade_testing
