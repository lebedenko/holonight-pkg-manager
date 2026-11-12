#include "guard_snapshot.h"
#include "holonight_packages_transaction/database_lock.h"
#include "promotion.h"
#include "scratch_dbpath.h"
#include "upgrade_fixture.h"

#include <gmock/gmock.h>

namespace holonight_packages_transaction::upgrade_testing {
namespace {

using holonight_packages_domain::CancelDecision;
using holonight_packages_domain::ChangeAction;
using holonight_packages_domain::ChangedPackage;
using holonight_packages_domain::WarningToken;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

class UpgradeExecuteTest : public UpgradeTest {
 public:
  // app, lib and tool at 1.0 installed, 2.0 on the mirror.
  void threeUpgrades() {
    for (const std::string name : {"app", "lib", "tool"}) {
      installed(core_, pkg(name));
    }
    publishLive();
    for (const std::string name : {"app", "lib", "tool"}) {
      core_.add(pkg(name, "2.0-1"));
    }
    publishServer();
    conf();
  }

  // archlinux-keyring and app at 1.0 installed, 2.0 on the mirror.
  void keyringAndApp() {
    installed(core_, pkg("archlinux-keyring"));
    installed(core_, pkg("app"));
    publishLive();
    core_.add(pkg("archlinux-keyring", "2.0-1"));
    core_.add(pkg("app", "2.0-1"));
    publishServer();
    conf();
  }

  [[nodiscard]] std::size_t cachedPackages() const {
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(world_.cacheDir())) {
      count += entry.path().string().ends_with(".pkg.tar.zst") ? 1 : 0;
    }
    return count;
  }
};

TEST_F(UpgradeExecuteTest, KeyringOnlyCompletesWithNoMainChanges) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("archlinux-keyring"));
  publishLive();
  core_.add(pkg("archlinux-keyring", "2.0-1"));
  publishServer();
  conf();
  world_.writeHook("fail.hook",
                   "[Trigger]\nOperation = Upgrade\nType = Package\nTarget = archlinux-keyring\n\n[Action]\n"
                   "When = PostTransaction\nExec = /usr/bin/false\n");
  const auto result = execute(prepared());
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_THAT(result.warnings, ElementsAre(WarningToken::HookFailed));
  ASSERT_EQ(result.changed.size(), 1U);
  EXPECT_EQ(result.changed.front().name, "archlinux-keyring");
  EXPECT_EQ(world_.installedVersions().at("archlinux-keyring"), "2.0-1");
}

TEST_F(UpgradeExecuteTest, KeyringWithDependenciesCompletesWithNoMainChanges) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("archlinux-keyring"));
  publishLive();
  core_.add(pkg("keyring-dependency"));
  core_.add(pkg("archlinux-keyring", "2.0-1", {"keyring-dependency"}));
  publishServer();
  conf();
  const auto result = execute(prepared());
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_EQ(result.changed.size(), 2U);
  EXPECT_EQ(world_.installedVersions(),
            (std::map<std::string, std::string>{{"archlinux-keyring", "2.0-1"}, {"keyring-dependency", "1.0-1"}}));
}

TEST_F(UpgradeExecuteTest, NothingToDoCannotCompleteANonemptyApprovedRemainder) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("archlinux-keyring"));
  publishLive();
  core_.add(pkg("archlinux-keyring", "2.0-1"));
  publishServer();
  conf();
  auto plan = prepared();
  // Introduce a remainder only after the initial comparisons to exercise the main-step mismatch check.
  onStage = [&](std::string_view stage) {
    if (stage == "main-step") {
      plan.plan.upgrade->new_dependencies.push_back(
          holonight_packages_domain::NewDependencyEntry{.name = "missing", .version = "1", .repository = "core"});
    }
  };
  const auto result = execute(plan);
  EXPECT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::UpgradePartiallyApplied);
  EXPECT_THAT(result.failure.names, ElementsAre("main"));
  ASSERT_EQ(result.changed.size(), 1U);
  EXPECT_EQ(result.changed.front().name, "archlinux-keyring");
}

TEST_F(UpgradeExecuteTest, AnInstalledUpgradeCanAlsoReplaceAnotherInstalledPackage) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("foo"));
  installed(core_, pkg("bar"));
  publishLive();
  auto replacement = pkg("foo", "2.0-1");
  replacement.replaces = {"bar"};
  core_.add(replacement);
  publishServer();
  conf();
  const auto plan = prepared();
  ASSERT_EQ(plan.plan.upgrade->upgrades.size(), 1U);
  ASSERT_EQ(plan.plan.upgrade->replacements.size(), 1U);
  EXPECT_EQ(plan.plan.upgrade->upgrades.front().name, "foo");
  EXPECT_EQ(plan.plan.upgrade->replacements.front().old_name, "bar");
  EXPECT_THAT(plan.plan.upgrade->new_dependencies, IsEmpty());
  EXPECT_EQ(plan.plan.upgrade->download_bytes, fs::file_size(core_.packagePath("foo")));
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_EQ(result.changed.size(), 2U);
  for (const auto& update : updates()) {
    if (update.phase == ProgressPhase::Installing) {
      EXPECT_EQ(update.total, 1U);
    }
  }
  EXPECT_EQ(world_.installedVersions(), (std::map<std::string, std::string>{{"foo", "2.0-1"}}));
}

// ---- T-028: phases 1 to 3 ----

TEST_F(UpgradeExecuteTest, TooLittleSpaceWithCheckSpaceIsDiskSpaceAndNothingChanges) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  conf({"CheckSpace"});
  const auto plan = prepared();
  int asked = 0;
  freeSpace = [&asked](const fs::path&) -> std::optional<std::uint64_t> {
    ++asked;
    return 0;
  };
  const auto before = liveManifest();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::DiskSpace);
  EXPECT_GT(asked, 0);
  EXPECT_EQ(liveManifest(), before);
  EXPECT_EQ(cachedPackages(), 0U);
}

TEST_F(UpgradeExecuteTest, WithoutCheckSpaceTheEstimateIsNeverAsked) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  int asked = 0;
  freeSpace = [&asked](const fs::path&) -> std::optional<std::uint64_t> {
    ++asked;
    return 0;
  };
  const auto result = execute(plan);
  EXPECT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_EQ(asked, 0);
}

TEST_F(UpgradeExecuteTest, APlanThatNoLongerMatchesBeforeTheFetchIsPlanChangedWithNoDownload) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  auto plan = prepared();
  plan.plan.upgrade->upgrades.front().new_version = "9.9-1";  // as if the mirror moved on
  RecordingFetch fetch;
  fetchSeam = fetch.function();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::PlanChanged);
  EXPECT_EQ(fetch.count(), 0U);
}

TEST_F(UpgradeExecuteTest, AChangedConfigurationIsCheckedAgainAtExecute) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  conf({"XferCommand = /bin/true"});
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::ConfigurationError);
  EXPECT_THAT(result.failure.names, ElementsAre("XferCommand"));
}

// ---- T-029 and T-020: fetch into the cache before any live change ----

TEST_F(UpgradeExecuteTest, ALiveDatabaseWithOnlyANewTimeIsSyncDatabaseChanged) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  fs::last_write_time(world_.syncDbPath() / "core.db", fs::file_time_type::clock::now());
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::SyncDatabaseChanged);
}

TEST_F(UpgradeExecuteTest, ThePackagesAreCachedBeforeAnythingLiveChangesAndTheUpgradeSucceeds) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  const auto before = liveManifest();
  std::optional<holonight_packages_testing::Manifest> atLock;
  std::size_t cachedAtLock = 0;
  bool lockAtGuard = false;
  onStage = [&](std::string_view stage) {
    if (stage == "lock-taken") {
      atLock = holonight_packages_testing::manifestOf(world_.syncDbPath());
      cachedAtLock = cachedPackages();
    }
    if (stage == "guard-checked") {
      lockAtGuard = fs::exists(world_.lockPath());
    }
  };
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_EQ(cachedAtLock, 3U);
  ASSERT_TRUE(atLock.has_value());
  // At the lock the live sync databases are still those from before Execute.
  for (const auto& [path, entry] : *atLock) {
    EXPECT_EQ(before.at(path), entry) << path;
  }
  EXPECT_TRUE(lockAtGuard);
  // The live databases are now the scratch databases, and the local database has the new versions.
  EXPECT_EQ(sha256Of(world_.syncDbPath() / "core.db"), sha256Of(core_.dir() / "core.db"));
  EXPECT_EQ(world_.installedVersions(),
            (std::map<std::string, std::string>{{"app", "2.0-1"}, {"lib", "2.0-1"}, {"tool", "2.0-1"}}));
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(UpgradeExecuteTest, APackageMissingFromTheMirrorIsSyncDatabaseStaleNamingItAndNothingChanges) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  core_.deletePackageFile("lib");
  const auto before = liveManifest();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::SyncDatabaseStale) << codeName(result.failure.code);
  EXPECT_THAT(result.failure.names, ElementsAre("lib"));
  EXPECT_EQ(liveManifest(), before);
  EXPECT_THAT(result.changed, IsEmpty());
}

TEST_F(UpgradeExecuteTest, AnUnreadablePackageFileIsDownloadFailedNotStale) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  core_.makeUnreadable("lib");
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::DownloadFailed) << codeName(result.failure.code);
}

// ---- T-026, T-027: signatures and the verifier ----

TEST_F(UpgradeExecuteTest, APackageSignedByAKeyOutsideTheKeyringIsKeyImportRequiredNamingIt) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("app"));
  publishLive();
  GpgKeyring stranger(world_.base() / "stranger");
  core_.setSigner(stranger.signer());
  core_.add(pkg("app", "2.0-1"));
  publishServer();
  conf();
  const auto plan = prepared();
  const auto keyring = holonight_packages_testing::manifestOf(world_.gpgDir());
  const auto before = liveManifest();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::KeyImportRequired) << codeName(result.failure.code);
  EXPECT_THAT(result.failure.names, ElementsAre("app"));
  EXPECT_EQ(liveManifest(), before);
  for (const auto& [path, entry] : keyring) {
    if (entry.kind == "file" && path.ends_with(".kbx")) {
      EXPECT_EQ(holonight_packages_testing::manifestOf(world_.gpgDir()).at(path).sha256, entry.sha256) << path;
    }
  }
}

TEST_F(UpgradeExecuteTest, AKeyThatTheKeyringStepBringsIsDeferredToTheMainStep) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("archlinux-keyring"));
  installed(core_, pkg("app"));
  publishLive();
  core_.add(pkg("archlinux-keyring", "2.0-1"));
  publishLive();
  GpgKeyring packager(world_.base() / "packager");
  core_.setSigner(packager.signer());
  core_.add(pkg("app", "2.0-1"));
  core_.setSigner(keyring_.signer());
  core_.add(pkg("archlinux-keyring", "2.0-1"));
  publishServer();
  conf();
  const auto plan = prepared();
  // The keyring package's install script would run pacman-key --populate; the seam does the same at its end.
  onStage = [&](std::string_view stage) {
    if (stage == "main-step") {
      packager.trustIn(world_.gpgDir());
    }
  };
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_EQ(world_.installedVersions().at("app"), "2.0-1");
}

TEST_F(UpgradeExecuteTest, AKeyThatTheKeyringStepDoesNotBringFailsTheMainStepAfterTheKeyringWasUpdated) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("archlinux-keyring"));
  installed(core_, pkg("app"));
  publishLive();
  GpgKeyring packager(world_.base() / "packager");
  core_.setSigner(packager.signer());
  core_.add(pkg("app", "2.0-1"));
  core_.setSigner(keyring_.signer());
  core_.add(pkg("archlinux-keyring", "2.0-1"));
  publishServer();
  conf();
  const auto plan = prepared();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::UpgradePartiallyApplied) << codeName(result.failure.code);
  EXPECT_THAT(result.failure.names, ElementsAre("main"));
  ASSERT_FALSE(result.changed.empty());
  EXPECT_EQ(result.changed.front().name, "archlinux-keyring");
  EXPECT_EQ(world_.installedVersions().at("app"), "1.0-1");
}

// A downloader that damages the first `corrupt` copies of package files.
class CorruptingFetch {
 public:
  explicit CorruptingFetch(int corrupt) : corrupt_(corrupt) {}
  std::function<int(const std::string&, const std::string&, bool)> function() {
    return [this](const std::string& url, const std::string& destination, bool) {
      const fs::path source = url.starts_with("file://") ? fs::path(url.substr(7)) : fs::path(url);
      const fs::path target = fs::path(destination) / source.filename();
      std::error_code error;
      fs::copy_file(source, target, fs::copy_options::overwrite_existing, error);
      if (error) {
        return -1;
      }
      if (source.filename().string().ends_with(".pkg.tar.zst") && ++packageFetches_ <= corrupt_) {
        std::fstream file(target, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(10);
        file.put('X');
      }
      return 0;
    };
  }
  [[nodiscard]] int packageFetches() const { return packageFetches_; }

 private:
  int corrupt_;
  int packageFetches_ = 0;
};

TEST_F(UpgradeExecuteTest, ACorruptDownloadIsFetchedOnceMoreAndThenSucceeds) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("app"));
  publishLive();
  core_.add(pkg("app", "2.0-1"));
  publishServer();
  conf();
  const auto plan = prepared();
  CorruptingFetch fetch(1);
  fetchSeam = fetch.function();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_EQ(fetch.packageFetches(), 2);
}

TEST_F(UpgradeExecuteTest, ADownloadCorruptTwiceIsDownloadFailedAfterTheSecondRequest) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("app"));
  publishLive();
  core_.add(pkg("app", "2.0-1"));
  publishServer();
  conf();
  const auto plan = prepared();
  CorruptingFetch fetch(2);
  fetchSeam = fetch.function();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::DownloadFailed) << codeName(result.failure.code);
  EXPECT_EQ(fetch.packageFetches(), 2);
  EXPECT_EQ(world_.installedVersions().at("app"), "1.0-1");
}

TEST_F(UpgradeExecuteTest, ACorruptSignatureIsSignatureInvalidAndNotRetried) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("app"));
  publishLive();
  core_.add(pkg("app", "2.0-1"));
  publishServer();
  std::ofstream(core_.packagePath("app").string() + ".sig", std::ios::binary | std::ios::trunc) << "garbage";
  conf();
  const auto plan = prepared();
  RecordingFetch fetch;
  fetchSeam = fetch.function();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::SignatureInvalid) << codeName(result.failure.code);
  int packageRequests = 0;
  for (const std::string& url : fetch.urls()) {
    packageRequests += url.ends_with(".pkg.tar.zst") ? 1 : 0;
  }
  EXPECT_EQ(packageRequests, 1);
}

// ---- T-030, T-031: lock, guard and re-resolution ----

TEST_F(UpgradeExecuteTest, AForeignLiveLockIsBusyAndStays) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  std::ofstream(world_.lockPath()) << "";
  const auto started = std::chrono::steady_clock::now();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::Busy) << codeName(result.failure.code);
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
  EXPECT_TRUE(fs::exists(world_.lockPath()));
}

TEST_F(UpgradeExecuteTest, ALiveDatabaseSyncedAfterPreparationIsSyncDatabaseChanged) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  publishLive();  // an external database sync between the preview and the confirmation
  const auto before = liveManifest();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::SyncDatabaseChanged);
  EXPECT_EQ(liveManifest(), before);
  EXPECT_FALSE(fs::exists(world_.lockPath()));
  // A new preparation works.
  fs::remove_all(scratchRoot());
  EXPECT_TRUE(prepare().result.has_value());
}

TEST_F(UpgradeExecuteTest, ALocalChangeBeforeTheLockIsPlanChanged) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  onStage = [&](std::string_view stage) {
    if (stage == "fetched") {
      world_.installPackage(pkg("tool", "2.0-1"));  // an external install of tool, between fetch and lock
      fs::remove_all(world_.localDbPath() / "tool-1.0-1");
    }
  };
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::PlanChanged) << codeName(result.failure.code);
  EXPECT_EQ(stageCount("promotion-starting"), 0);
}

// ---- T-032, T-033: promotion ----

class SyncPromotionTest : public ::testing::Test {
 public:
  SyncPromotionTest() {
    for (const fs::path& directory : {scratch(), live(), rollback()}) {
      fs::create_directories(directory);
    }
    for (const std::string name : {"core", "extra", "multilib"}) {
      std::ofstream(live() / (name + ".db")) << "old " << name;
      std::ofstream(scratch() / (name + ".db")) << "new " << name;
    }
  }
  [[nodiscard]] fs::path scratch() const { return world_.base() / "s" / "sync"; }
  [[nodiscard]] fs::path live() const { return world_.syncDbPath(); }
  [[nodiscard]] fs::path rollback() const { return world_.base() / "s" / "rollback"; }

  FixtureRoot world_;
  TransactionSeams seams_;
};

TEST_F(SyncPromotionTest, AFaultAtTheSecondFileRollsTheFirstBack) {
  const auto before = holonight_packages_testing::manifestOf(live());
  seams_.inject_fault = [](std::string_view stage) { return stage == "promotion-rename-2" ? 1 : 0; };
  SyncPromotion promotion(scratch(), live(), rollback(), seams_, {});
  EXPECT_FALSE(promotion.promote());
  const auto after = holonight_packages_testing::manifestOf(live());
  ASSERT_EQ(after.size(), before.size());
  for (const auto& [path, entry] : before) {
    EXPECT_EQ(after.at(path).sha256, entry.sha256) << path;
  }
}

TEST_F(SyncPromotionTest, ASuccessfulPromotionLeavesTheScratchContentAndNoTemporaryFile) {
  std::map<std::string, std::string> expected;
  for (const auto& entry : fs::directory_iterator(scratch())) {
    expected[entry.path().filename().string()] = *sha256Of(entry.path());
  }
  SyncPromotion promotion(scratch(), live(), rollback(), seams_, {});
  ASSERT_TRUE(promotion.promote());
  for (const auto& [name, digest] : expected) {
    EXPECT_EQ(sha256Of(live() / name), digest) << name;
  }
  for (const auto& entry : fs::directory_iterator(live())) {
    EXPECT_FALSE(entry.path().filename().string().starts_with(".hn-promote-")) << entry.path();
  }
}

TEST_F(SyncPromotionTest, ByCopyTheScratchFileStaysAndTheLiveFileIsReplacedWhole) {
  seams_.promote_by_copy = true;
  SyncPromotion promotion(scratch(), live(), rollback(), seams_, {});
  ASSERT_TRUE(promotion.promote());
  EXPECT_TRUE(fs::exists(scratch() / "core.db"));  // copied, not renamed
  EXPECT_EQ(sha256Of(live() / "core.db"), sha256Of(scratch() / "core.db"));
}

TEST_F(SyncPromotionTest, ALeftoverTemporaryFileIsSweptAndTheLiveFileIsUntouched) {
  std::ofstream(live() / ".hn-promote-core.db.tmp") << "half";
  const auto core = sha256Of(live() / "core.db");
  removePromotionLeftovers(live());
  EXPECT_FALSE(fs::exists(live() / ".hn-promote-core.db.tmp"));
  EXPECT_EQ(sha256Of(live() / "core.db"), core);
}

TEST_F(UpgradeExecuteTest, AFailedPromotionRestoresTheLiveDatabasesAndInstallsNothing) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  installed(extra_, pkg("other"));
  publishLive();
  extra_.add(pkg("other", "2.0-1"));
  publishServer();
  conf();
  const auto plan = prepared();
  const auto before = liveManifest();
  injectFault = [](std::string_view stage) { return stage == "promotion-rename-2" ? 1 : 0; };
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::InternalError);
  const auto after = liveManifest();
  for (const auto& [path, entry] : before) {
    EXPECT_EQ(after.at(path).sha256, entry.sha256) << path;
  }
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

// ---- T-034: the point of no return ----

TEST_F(UpgradeExecuteTest, ACancelAtPromotionIsRefusedAndTheUpgradeRunsToItsEnd) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  std::optional<CancelDecision> decision;
  onStage = [&](std::string_view stage) {
    if (stage == "promotion-starting") {
      decision = control_.load()->requestCancel();
    }
  };
  const auto result = execute(plan);
  ASSERT_EQ(decision, CancelDecision::PastPointOfNoReturn);
  EXPECT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
}

TEST_F(UpgradeExecuteTest, ACancelBeforeTheLockStopsWithNothingChanged) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  const auto before = liveManifest();
  onStage = [&](std::string_view stage) {
    if (stage == "fetched") {
      EXPECT_EQ(control_.load()->requestCancel(), CancelDecision::Accepted);
    }
  };
  const auto result = execute(plan);
  EXPECT_EQ(result.outcome, ExecutionOutcome::Cancelled);
  EXPECT_EQ(liveManifest(), before);
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

// ---- T-035 to T-037: the steps and their markers ----

TEST_F(UpgradeExecuteTest, TheKeyringIsCommittedAloneFirstAndTheMarkersSayStepOneThenTwo) {
  SKIP_UNLESS_FRESH_GPGME();
  keyringAndApp();
  const auto plan = prepared();
  std::string keyringAtMain;
  onStage = [&](std::string_view stage) {
    if (stage == "main-step") {
      keyringAtMain = world_.installedVersions().at("archlinux-keyring");
    }
  };
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_EQ(keyringAtMain, "2.0-1");
  ASSERT_EQ(result.changed.size(), 2U);
  EXPECT_EQ(result.changed.front().name, "archlinux-keyring");
  std::vector<ProgressUpdate> markers;
  for (const auto& update : updates()) {
    if (update.phase == ProgressPhase::KeyringStep || update.phase == ProgressPhase::MainStep) {
      markers.push_back(update);
    }
  }
  ASSERT_EQ(markers.size(), 2U);
  EXPECT_EQ(markers[0].phase, ProgressPhase::KeyringStep);
  EXPECT_EQ(std::pair(markers[0].current, markers[0].total), std::pair(1U, 2U));
  EXPECT_EQ(markers[1].phase, ProgressPhase::MainStep);
  EXPECT_EQ(std::pair(markers[1].current, markers[1].total), std::pair(2U, 2U));
  const auto all = phases();
  EXPECT_EQ(all.back(), ProgressPhase::Done);
  EXPECT_EQ(std::count(all.begin(), all.end(), ProgressPhase::Done), 1);
  const auto promoting = std::ranges::find(all, ProgressPhase::Promoting);
  const auto keyringMarker = std::ranges::find(all, ProgressPhase::KeyringStep);
  ASSERT_NE(promoting, all.end());
  EXPECT_LT(promoting, keyringMarker);
  EXPECT_NE(std::find(all.begin(), promoting, ProgressPhase::Downloading), promoting);
}

TEST_F(UpgradeExecuteTest, WithoutKeyringThereIsOneMainStepMarkerOneOfOne) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  int mainMarkers = 0;
  for (const auto& update : updates()) {
    EXPECT_NE(update.phase, ProgressPhase::KeyringStep);
    if (update.phase == ProgressPhase::MainStep) {
      ++mainMarkers;
      EXPECT_EQ(std::pair(update.current, update.total), std::pair(1U, 1U));
    }
  }
  EXPECT_EQ(mainMarkers, 1);
}

TEST_F(UpgradeExecuteTest, AKeyringCommitFailureIsPartialAtTheKeyringStepWithTheNewDatabases) {
  SKIP_UNLESS_FRESH_GPGME();
  keyringAndApp();
  const auto plan = prepared();
  injectFault = [](std::string_view stage) { return stage == "keyring-commit" ? 1 : 0; };
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::UpgradePartiallyApplied);
  EXPECT_THAT(result.failure.names, ElementsAre("keyring"));
  EXPECT_THAT(result.changed, IsEmpty());
  EXPECT_EQ(world_.installedVersions().at("app"), "1.0-1");
  EXPECT_EQ(sha256Of(world_.syncDbPath() / "core.db"), sha256Of(core_.dir() / "core.db"));
  EXPECT_FALSE(fs::exists(world_.lockPath()));
  const auto all = phases();
  EXPECT_EQ(std::count(all.begin(), all.end(), ProgressPhase::Done), 0);
}

TEST_F(UpgradeExecuteTest, AFaultAfterTheFirstOfThreeIsPartialAtTheMainStepWithOnlyThatPackage) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  injectFault = [](std::string_view stage) { return stage == "package-done" ? 1 : 0; };
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::UpgradePartiallyApplied);
  EXPECT_THAT(result.failure.names, ElementsAre("main"));
  ASSERT_EQ(result.changed.size(), 1U);
  int upgraded = 0;
  for (const auto& [name, version] : world_.installedVersions()) {
    upgraded += version == "2.0-1" ? 1 : 0;
  }
  EXPECT_EQ(upgraded, 1);

  // T-040: the next upgrade completes it (REQ-F-063).
  injectFault = nullptr;
  fs::remove_all(scratchRoot());
  const auto rest = prepared();
  EXPECT_EQ(rest.plan.upgrade->upgrades.size(), 2U);
  const auto second = execute(rest);
  ASSERT_EQ(second.outcome, ExecutionOutcome::Succeeded) << codeName(second.failure.code);
  fs::remove_all(scratchRoot());
  const auto done = prepare();
  ASSERT_FALSE(done.result.has_value());
  EXPECT_EQ(done.result.error().code, TransactionErrorCode::NothingToDo);
}

TEST_F(UpgradeExecuteTest, AMainStepNeverDownloads) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  RecordingFetch fetch;
  fetchSeam = fetch.function();
  std::size_t requestsAtMain = 0;
  onStage = [&](std::string_view stage) {
    if (stage == "main-step") {
      requestsAtMain = fetch.count();
      fs::remove(world_.cacheDir() / pkg("lib", "2.0-1").fileName());
    }
  };
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::UpgradePartiallyApplied);
  EXPECT_THAT(result.failure.names, ElementsAre("main"));
  EXPECT_EQ(fetch.count(), requestsAtMain);
}

// ---- T-038: the result ----

TEST_F(UpgradeExecuteTest, AReplacementIsOneRemovalAndOneInstallInTheChangedList) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("oldpkg"));
  publishLive();
  PackageSpec replacing = pkg("newpkg");
  replacing.replaces = {"oldpkg"};
  core_.add(replacing);
  publishServer();
  conf();
  const auto plan = prepared();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_FALSE(world_.isInstalled("oldpkg"));
  EXPECT_TRUE(world_.isInstalled("newpkg"));
  std::vector<std::pair<std::string, ChangeAction>> changes;
  changes.reserve(result.changed.size());
  for (const ChangedPackage& change : result.changed) {
    changes.emplace_back(change.name, change.action);
  }
  EXPECT_THAT(changes, UnorderedElementsAre(std::pair{std::string("oldpkg"), ChangeAction::Remove},
                                            std::pair{std::string("newpkg"), ChangeAction::Install}));
}

TEST_F(UpgradeExecuteTest, AFailingPostTransactionHookIsSuccessWithTheHookWarning) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  world_.writeHook("fail.hook",
                   "[Trigger]\nOperation = Upgrade\nType = Package\nTarget = app\n\n[Action]\nWhen = PostTransaction\n"
                   "Exec = /usr/bin/false\n");
  const auto plan = prepared();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_THAT(result.warnings, ElementsAre(WarningToken::HookFailed));
  EXPECT_EQ(result.changed.size(), 3U);
}

TEST_F(UpgradeExecuteTest, HeldBackPackagesStayAtTheirVersion) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("app"));
  installed(core_, pkg("foo"));
  publishLive();
  core_.add(pkg("app", "2.0-1"));
  core_.add(pkg("foo", "2.0-1"));
  publishServer();
  conf({"IgnorePkg = foo"});
  const auto plan = prepared();
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
  EXPECT_EQ(world_.installedVersions().at("foo"), "1.0-1");
  EXPECT_EQ(world_.installedVersions().at("app"), "2.0-1");
}

// ---- T-039, T-040: cancel during the fetch; refusals before promotion are idempotent ----

TEST_F(UpgradeExecuteTest, ACancelWithTheFetchHeldIsCancelledAndLeavesNoLock) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  const auto before = liveManifest();
  RecordingFetch fetch(true);
  fetchSeam = fetch.function();
  std::thread canceller([&] {
    ASSERT_TRUE(fetch.waitUntilHeld(std::chrono::seconds(5)));
    while (control_.load() == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(control_.load()->requestCancel(), CancelDecision::Accepted);
    fetch.release();
  });
  const auto result = execute(plan);
  canceller.join();
  EXPECT_EQ(result.outcome, ExecutionOutcome::Cancelled);
  EXPECT_EQ(liveManifest(), before);
  EXPECT_FALSE(fs::exists(world_.lockPath()));
  EXPECT_FALSE(DatabaseLockGuard{}.ownsLock());
}

TEST_F(UpgradeExecuteTest, AfterARefusalBeforePromotionTheSamePreviewComesBack) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto before = liveManifest();
  const auto first = prepared();
  core_.deletePackageFile("lib");
  ASSERT_EQ(execute(first).failure.code, TransactionErrorCode::SyncDatabaseStale);
  EXPECT_EQ(liveManifest(), before);
  fs::remove_all(scratchRoot());
  const auto second = prepared();
  EXPECT_TRUE(holonight_packages_domain::samePlan(first.plan, second.plan));
}

TEST_F(UpgradeExecuteTest, RawLibalpmTextStaysInTheJournal) {
  SKIP_UNLESS_FRESH_GPGME();
  threeUpgrades();
  const auto plan = prepared();
  core_.deletePackageFile("lib");
  const auto result = execute(plan);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  bool journalHasLibalpmText = false;
  {
    const std::scoped_lock lock(stagesMutex_);
    for (const std::string& line : journal_) {
      journalHasLibalpmText = journalHasLibalpmText || line.contains("failed retrieving file");
    }
  }
  EXPECT_TRUE(journalHasLibalpmText);
  for (const std::string& name : result.failure.names) {
    EXPECT_FALSE(name.contains(' ')) << name;  // names only, never a sentence
  }
  for (const auto& update : updates()) {
    EXPECT_FALSE(update.item.contains("failed")) << update.item;
  }
}

// ---- T-047: the internal sync timeout; T-055: install and remove never touch the sync databases ----

TEST(UpgradeLimitsTest, TheInternalSyncTimeoutIsBelowTheClientTimeout) {
  EXPECT_EQ(UpgradeLimits{}.sync_timeout, std::chrono::seconds(480));
  EXPECT_LT(UpgradeLimits{}.sync_timeout, std::chrono::seconds(600));
  EXPECT_EQ(UpgradeLimits{}.scratch_cap_bytes, std::uint64_t{256} << 20U);
}

TEST_F(UpgradeExecuteTest, AFullInstallAndAFullRemoveLeaveTheLiveSyncDatabasesAlone) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("tool"));
  publishLive();
  conf();
  const auto sync = holonight_packages_testing::manifestOf(world_.syncDbPath());
  using holonight_packages_domain::PlanAction;
  using holonight_packages_domain::PlanRequest;
  for (const PlanAction action : {PlanAction::Install, PlanAction::Remove}) {
    const PlanRequest request{.action = action, .names = {"tool"}};
    auto plan = engine().resolve(request);
    ASSERT_TRUE(plan.has_value()) << codeName(plan.error().code);
    const auto result = engine().execute(request, *plan, {});
    ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << codeName(result.failure.code);
    EXPECT_EQ(holonight_packages_testing::manifestOf(world_.syncDbPath()), sync);
  }
}

}  // namespace
}  // namespace holonight_packages_transaction::upgrade_testing
