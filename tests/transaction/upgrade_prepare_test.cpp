#include "guard_snapshot.h"
#include "scratch_dbpath.h"
#include "upgrade_fixture.h"

#include <gmock/gmock.h>
#include <sys/stat.h>

namespace holonight_packages_transaction::upgrade_testing {
namespace {

using holonight_packages_domain::HeldBackEntry;
using holonight_packages_domain::NewDependencyEntry;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlanRequest;
using holonight_packages_domain::ReplacementEntry;
using holonight_packages_domain::UpgradeEntry;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

// The usual starting point: app and lib installed at 1.0, and 2.0 on the mirror.
class UpgradePrepareTest : public UpgradeTest {
 public:
  void twoUpgrades() {
    installed(core_, pkg("app"));
    installed(extra_, pkg("lib"));
    publishLive();
    core_.add(pkg("app", "2.0-1"));
    extra_.add(pkg("lib", "2.0-1"));
    publishServer();
  }
};

// ---- T-015: configuration checks before libalpm (REQ-F-018, REQ-F-075, REQ-F-076) ----

TEST_F(UpgradePrepareTest, XferCommandIsRefusedByNameBeforeLibalpmStarts) {
  twoUpgrades();
  conf({"XferCommand = /usr/bin/curl -o %o %u"});
  const auto before = liveManifest();
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::ConfigurationError);
  EXPECT_THAT(outcome.result.error().names, ElementsAre("XferCommand"));
  EXPECT_EQ(stageCount("alpm-initialize"), 0);
  EXPECT_FALSE(fs::exists(scratchRoot() / "dbpath"));
  EXPECT_EQ(liveManifest(), before);
}

TEST_F(UpgradePrepareTest, AnotherDbPathOrRootDirOrUsageIsRefusedButTheHelpersOwnPathsPass) {
  twoUpgrades();
  conf({"DBPath = /var/lib/elsewhere/", "RootDir = /mnt"});
  auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_THAT(outcome.result.error().names, ElementsAre("DBPath", "RootDir"));
  conf({"DBPath = " + world_.dbPath().string() + "/", "RootDir = " + world_.installRoot().string()});
  EXPECT_TRUE(prepare().result.has_value());
}

TEST_F(UpgradePrepareTest, ParallelDownloadsAndHoldPkgAreAccepted) {
  SKIP_UNLESS_FRESH_GPGME();
  twoUpgrades();
  conf({"ParallelDownloads = 3", "HoldPkg = app"});
  const auto outcome = prepare();
  ASSERT_TRUE(outcome.result.has_value()) << codeName(outcome.result.error().code);
  EXPECT_EQ(outcome.result->plan.upgrade->upgrades.size(), 2U);
}

TEST_F(UpgradePrepareTest, AnInstallWithTheSameUnsupportedOptionStillProceeds) {
  core_.add(pkg("tool"));
  publishLive();
  conf({"XferCommand = /usr/bin/curl -o %o %u"});
  auto plan = engine().resolve(PlanRequest{.action = PlanAction::Install, .names = {"tool"}});
  ASSERT_TRUE(plan.has_value()) << codeName(plan.error().code);
}

TEST_F(UpgradePrepareTest, ARepositoryWithoutServerIsNamedAndNoScratchFileIsCreated) {
  twoUpgrades();
  ConfSpec spec;
  spec.globalSigLevel = "Required DatabaseOptional";
  spec.repos = {RepoConf{.name = "core", .servers = {world_.serverUrl("core")}, .sigLevel = std::nullopt},
                RepoConf{.name = "extra", .servers = {}, .sigLevel = std::nullopt}};
  world_.writeConf(spec);
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::ConfigurationError);
  EXPECT_THAT(outcome.result.error().names, ElementsAre("extra"));
  EXPECT_FALSE(fs::exists(scratchRoot() / "dbpath"));
}

// ---- T-016: signature levels before any network use (REQ-F-031) ----

TEST_F(UpgradePrepareTest, AnOptionalSignatureLevelIsInsecureBeforeAnyDownload) {
  twoUpgrades();
  ConfSpec spec;
  spec.globalSigLevel = "Required DatabaseOptional";
  spec.repos = {RepoConf{.name = "core", .servers = {world_.serverUrl("core")}, .sigLevel = "Optional"},
                RepoConf{.name = "extra", .servers = {world_.serverUrl("extra")}, .sigLevel = std::nullopt}};
  world_.writeConf(spec);
  RecordingFetch fetch;
  fetchSeam = fetch.function();
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::InsecureSigLevel);
  EXPECT_THAT(outcome.result.error().names, ElementsAre("core"));
  EXPECT_EQ(fetch.count(), 0U);
}

TEST_F(UpgradePrepareTest, RequiredTrustedOnlyEverywhereIsNotInsecure) {
  SKIP_UNLESS_FRESH_GPGME();
  twoUpgrades();
  conf({}, "Required TrustedOnly DatabaseOptional");
  const auto outcome = prepare();
  EXPECT_TRUE(outcome.result.has_value()) << codeName(outcome.result.error().code);
}

// ---- T-017: the scratch layout (REQ-F-007) ----

TEST_F(UpgradePrepareTest, TheScratchLayoutLinksLocalSeedsTheDatabasesAndChangesNothingLive) {
  twoUpgrades();
  const auto before = liveManifest();
  const ScratchDbpath scratch{.root = scratchRoot()};
  const auto seeded = prepareScratchDbpath(world_.dbPath(), scratch, {"core", "extra"}, {});
  ASSERT_TRUE(seeded.has_value()) << codeName(seeded.error().code);
  EXPECT_EQ(liveManifest(), before);
  EXPECT_TRUE(fs::is_symlink(scratch.dbpath() / "local"));
  EXPECT_EQ(fs::read_symlink(scratch.dbpath() / "local"), world_.localDbPath());
  for (const std::string name : {"core.db", "extra.db"}) {
    EXPECT_EQ(fs::last_write_time(scratch.sync() / name), fs::last_write_time(world_.syncDbPath() / name)) << name;
    EXPECT_EQ(sha256Of(scratch.sync() / name), sha256Of(world_.syncDbPath() / name)) << name;
  }
  ASSERT_EQ(seeded->guard.size(), 2U);
  EXPECT_EQ(seeded->seeded_bytes,
            fs::file_size(world_.syncDbPath() / "core.db") + fs::file_size(world_.syncDbPath() / "extra.db"));
  for (const fs::path& directory : {scratch.root, scratch.dbpath(), scratch.sync()}) {
    struct stat status{};
    ASSERT_EQ(::stat(directory.c_str(), &status), 0);
    EXPECT_EQ(status.st_mode & 0777, 0711U) << directory;  // REQ-F-008, SPIKES S-1 f
  }
}

TEST_F(UpgradePrepareTest, ALiveLockIsBusyAndCreatesNothing) {
  twoUpgrades();
  std::ofstream(world_.lockPath()) << "";
  const auto seeded =
      prepareScratchDbpath(world_.dbPath(), ScratchDbpath{.root = scratchRoot()}, {"core", "extra"}, {});
  ASSERT_FALSE(seeded.has_value());
  EXPECT_EQ(seeded.error().code, TransactionErrorCode::Busy);
  EXPECT_FALSE(fs::exists(scratchRoot()));
  EXPECT_TRUE(fs::exists(world_.lockPath()));
}

// ---- T-018: the guard snapshot (REQ-F-051) ----

TEST_F(UpgradePrepareTest, TheGuardSeesContentTimeSizeAndAddedOrRemovedSignaturesButNotOtherFiles) {
  twoUpgrades();
  const auto base = snapshotSyncFiles(world_.syncDbPath());
  ASSERT_TRUE(base.has_value());
  const fs::path core = world_.syncDbPath() / "core.db";
  // Same content, new time.
  fs::last_write_time(core, fs::file_time_type::clock::now());
  EXPECT_NE(*snapshotSyncFiles(world_.syncDbPath()), *base);
  // Another file type is outside the guard.
  const auto touched = snapshotSyncFiles(world_.syncDbPath());
  std::ofstream(world_.syncDbPath() / "core.files") << "files";
  EXPECT_EQ(*snapshotSyncFiles(world_.syncDbPath()), *touched);
  // An added and then removed signature.
  std::ofstream(world_.syncDbPath() / "core.db.sig") << "sig";
  const auto signedOne = snapshotSyncFiles(world_.syncDbPath());
  EXPECT_NE(*signedOne, *touched);
  fs::remove(world_.syncDbPath() / "core.db.sig");
  EXPECT_EQ(*snapshotSyncFiles(world_.syncDbPath()), *touched);
  // A size change.
  std::ofstream(core, std::ios::app) << "x";
  EXPECT_NE(*snapshotSyncFiles(world_.syncDbPath()), *touched);
}

// ---- T-019 to T-021: the sync (REQ-F-006, REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-017, REQ-F-034) ----

TEST_F(UpgradePrepareTest, TheSyncFillsTheScratchDatabasesFromTheServersAndLeavesLiveAlone) {
  SKIP_UNLESS_FRESH_GPGME();
  twoUpgrades();
  conf();
  const auto before = liveManifest();
  const auto plan = prepared();
  EXPECT_EQ(liveManifest(), before);
  const ScratchDbpath scratch{.root = scratchRoot()};
  EXPECT_EQ(sha256Of(scratch.sync() / "core.db"), sha256Of(core_.dir() / "core.db"));
  EXPECT_EQ(sha256Of(scratch.sync() / "extra.db"), sha256Of(extra_.dir() / "extra.db"));
  ASSERT_TRUE(plan.plan.upgrade.has_value());
  EXPECT_THAT(
      plan.plan.upgrade->upgrades,
      ElementsAre(UpgradeEntry{.name = "app", .old_version = "1.0-1", .new_version = "2.0-1", .repository = "core"},
                  UpgradeEntry{.name = "lib", .old_version = "1.0-1", .new_version = "2.0-1", .repository = "extra"}));
  // One Syncing update per repository, in order (REQ-F-034).
  std::vector<std::pair<std::uint32_t, std::uint32_t>> syncing;
  for (const auto& update : updates()) {
    if (update.phase == ProgressPhase::Syncing) {
      syncing.emplace_back(update.current, update.total);
    }
  }
  EXPECT_THAT(syncing, ElementsAre(std::pair{1U, 2U}, std::pair{2U, 2U}));
  // The guard is the live state at the start.
  EXPECT_EQ(plan.guard, *snapshotSyncFiles(world_.syncDbPath()));
}

TEST_F(UpgradePrepareTest, OnlyTheConfiguredServersAreAskedAndTheFirstThatAnswersWins) {
  SKIP_UNLESS_FRESH_GPGME();
  twoUpgrades();
  RepoBuilder other(world_, "other");
  other.add(pkg("app", "9.0-1")).publish();
  ConfSpec spec;
  spec.globalSigLevel = "Required DatabaseOptional";
  spec.repos = {RepoConf{.name = "core", .servers = {world_.serverUrl("core"), other.serverUrl()}, .sigLevel = {}},
                RepoConf{.name = "extra", .servers = {world_.serverUrl("extra")}, .sigLevel = {}}};
  world_.writeConf(spec);
  RecordingFetch fetch;
  fetchSeam = fetch.function();
  const auto plan = prepared();
  for (const std::string& url : fetch.urls()) {
    EXPECT_FALSE(url.starts_with(other.serverUrl())) << url;
  }
  EXPECT_EQ(plan.plan.upgrade->upgrades.front().new_version, "2.0-1");
}

TEST_F(UpgradePrepareTest, AnUnreachableMirrorIsSyncFailedWithNothingLiveChangedAndNoLock) {
  twoUpgrades();
  conf();
  fs::remove_all(extra_.dir());
  const auto before = liveManifest();
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::SyncFailed);
  EXPECT_FALSE(outcome.worker_stuck);
  EXPECT_EQ(liveManifest(), before);
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(UpgradePrepareTest, AHeldSyncHitsTheInternalTimeoutAndReportsAStuckWorker) {
  twoUpgrades();
  conf();
  limits_.sync_timeout = std::chrono::seconds(1);
  RecordingFetch fetch(true);
  fetchSeam = fetch.function();
  const auto started = std::chrono::steady_clock::now();
  const auto outcome = prepare();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::SyncFailed);
  EXPECT_TRUE(outcome.worker_stuck);
  EXPECT_LT(elapsed, std::chrono::seconds(3));
  fetch.release();
  EXPECT_TRUE(fetch.waitUntilIdle(std::chrono::seconds(5)));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));  // let the detached worker leave libalpm
}

TEST_F(UpgradePrepareTest, ADatabaseBeyondTheSizeCapIsSyncFailed) {
  twoUpgrades();
  for (int index = 0; index < 60; ++index) {
    core_.add(pkg("filler-" + std::to_string(index), "1.0-1"));
  }
  publishServer();
  conf();
  ASSERT_GT(fs::file_size(core_.dir() / "core.db"), 4096U);
  limits_.scratch_cap_bytes = 1024;
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::SyncFailed);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

TEST_F(UpgradePrepareTest, ADatabaseSignedByAnUnknownKeyIsDatabaseUnavailable) {
  SKIP_UNLESS_FRESH_GPGME();
  twoUpgrades();
  GpgKeyring stranger(world_.base() / "stranger");
  for (RepoBuilder* repo : {&core_, &extra_}) {
    keyring_.signFile(world_.syncDbPath() / (repo->name() + ".db"));
    stranger.signFile(repo->dir() / (repo->name() + ".db"));
  }
  conf({}, "Required DatabaseRequired");
  const auto before = liveManifest();
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::DatabaseUnavailable);
  EXPECT_EQ(liveManifest(), before);
}

// ---- T-022 to T-025: resolution and the question policy (REQ-F-019 to REQ-F-028) ----

TEST_F(UpgradePrepareTest, ThePreviewSortsUpgradesReplacementsNewDependenciesAndHeldBackPackages) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("app"));
  installed(core_, pkg("lib"));
  installed(core_, pkg("oldpkg"));
  installed(extra_, pkg("kept"));
  publishLive();
  core_.add(pkg("app", "2.0-1", {"newdep"}));
  core_.add(pkg("lib", "2.0-1"));
  core_.add(pkg("newdep"));
  PackageSpec replacing = pkg("newpkg");
  replacing.replaces = {"oldpkg"};
  core_.add(replacing);
  extra_.add(pkg("kept", "2.0-1"));
  publishServer();
  conf({"IgnorePkg = kept"});
  const auto plan = prepared();
  ASSERT_TRUE(plan.plan.upgrade.has_value());
  const UpgradeDetail& detail = *plan.plan.upgrade;
  EXPECT_THAT(
      detail.upgrades,
      ElementsAre(UpgradeEntry{.name = "app", .old_version = "1.0-1", .new_version = "2.0-1", .repository = "core"},
                  UpgradeEntry{.name = "lib", .old_version = "1.0-1", .new_version = "2.0-1", .repository = "core"}));
  EXPECT_THAT(detail.replacements, ElementsAre(ReplacementEntry{.old_name = "oldpkg",
                                                                .old_version = "1.0-1",
                                                                .new_name = "newpkg",
                                                                .new_version = "1.0-1",
                                                                .repository = "core"}));
  EXPECT_THAT(detail.new_dependencies,
              ElementsAre(NewDependencyEntry{.name = "newdep", .version = "1.0-1", .repository = "core"}));
  EXPECT_THAT(detail.held_back, ElementsAre(HeldBackEntry{.name = "kept", .installed_version = "1.0-1"}));
  EXPECT_FALSE(detail.keyring());
  std::uint64_t download = 0;
  for (const std::string name : {"app", "lib", "newdep", "newpkg"}) {
    download += fs::file_size(core_.packagePath(name));
  }
  EXPECT_EQ(detail.download_bytes, download);
}

TEST_F(UpgradePrepareTest, ALiveChangeAfterPreparationDoesNotChangeTheScratchResult) {
  SKIP_UNLESS_FRESH_GPGME();
  twoUpgrades();
  conf();
  const auto first = prepared();
  // The live databases catch up, as an external database sync would make them.
  publishLive();
  EXPECT_EQ(sha256Of(ScratchDbpath{.root = scratchRoot()}.sync() / "core.db"), sha256Of(core_.dir() / "core.db"));
  EXPECT_EQ(first.plan.upgrade->upgrades.size(), 2U);
}

TEST_F(UpgradePrepareTest, TheKeyringPackageAndItsNewDependenciesFormTheKeyringStep) {
  SKIP_UNLESS_FRESH_GPGME();
  installed(core_, pkg("archlinux-keyring", "1.0-1"));
  installed(core_, pkg("app"));
  publishLive();
  core_.add(pkg("archlinux-keyring", "2.0-1", {"keyring-helper"}));
  core_.add(pkg("keyring-helper"));
  core_.add(pkg("app", "2.0-1"));
  publishServer();
  conf();
  const auto plan = prepared();
  ASSERT_TRUE(plan.plan.upgrade.has_value());
  EXPECT_THAT(plan.plan.upgrade->keyring_packages, ElementsAre("archlinux-keyring", "keyring-helper"));
  EXPECT_EQ(plan.plan.upgrade->stepCount(), 2);
}

TEST_F(UpgradePrepareTest, AnAppOnlyUpgradeHasNoKeyringStep) {
  SKIP_UNLESS_FRESH_GPGME();
  twoUpgrades();
  conf();
  const auto plan = prepared();
  EXPECT_THAT(plan.plan.upgrade->keyring_packages, IsEmpty());
}

TEST_F(UpgradePrepareTest, ACurrentSystemIsNothingToDoAndCreatesNoPlan) {
  installed(core_, pkg("app"));
  publishLive();
  conf();
  const auto before = liveManifest();
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::NothingToDo);
  EXPECT_EQ(liveManifest(), before);
}

TEST_F(UpgradePrepareTest, AHeldBackPackageThatBreaksADependencyIsPackageNotFoundNamingIt) {
  installed(core_, pkg("foo"));
  installed(core_, pkg("app"));
  publishLive();
  core_.add(pkg("foo", "2.0-1"));
  core_.add(pkg("app", "2.0-1", {"foo>=2"}));
  publishServer();
  conf({"IgnorePkg = foo"});
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::PackageNotFound)
      << codeName(outcome.result.error().code);
  EXPECT_THAT(outcome.result.error().names, ElementsAre("foo"));
}

TEST_F(UpgradePrepareTest, AReplacementOfAHoldPkgPackageIsProtected) {
  installed(core_, pkg("base-thing"));
  publishLive();
  PackageSpec replacing = pkg("base-thing-ng");
  replacing.replaces = {"base-thing"};
  core_.add(replacing);
  publishServer();
  conf({"HoldPkg = base-thing"});
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::ProtectedPackage);
  EXPECT_THAT(outcome.result.error().names, ElementsAre("base-thing"));
}

TEST_F(UpgradePrepareTest, AConflictWithoutReplacementNeedsADecisionNamingBoth) {
  installed(core_, pkg("old-x"));
  installed(core_, pkg("app"));
  publishLive();
  PackageSpec conflicting = pkg("app", "2.0-1", {"new-y"});
  core_.add(conflicting);
  PackageSpec newY = pkg("new-y");
  newY.conflicts = {"old-x"};
  core_.add(newY);
  publishServer();
  conf();
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::NeedsDecision) << codeName(outcome.result.error().code);
  ASSERT_FALSE(outcome.result.error().choices.empty());
  EXPECT_THAT(outcome.result.error().choices.front().candidates, UnorderedElementsAre("new-y", "old-x"));
}

TEST_F(UpgradePrepareTest, AnInstalledPackageThatTheUpgradeBreaksNeedsADecisionNamingIt) {
  installed(core_, pkg("lib"));
  installed(core_, pkg("ext-tool", "1.0-1", {"lib<2"}));
  publishLive();
  core_.add(pkg("lib", "2.0-1"));
  publishServer();
  conf();
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::NeedsDecision) << codeName(outcome.result.error().code);
  ASSERT_FALSE(outcome.result.error().choices.empty());
  EXPECT_EQ(outcome.result.error().choices.front().subject, "ext-tool");
}

TEST_F(UpgradePrepareTest, SeveralProvidersOfANewDependencyNeedADecision) {
  installed(core_, pkg("app"));
  publishLive();
  core_.add(pkg("app", "2.0-1", {"virt"}));
  PackageSpec first = pkg("prov-a");
  first.provides = {"virt"};
  PackageSpec second = pkg("prov-b");
  second.provides = {"virt"};
  core_.add(first);
  core_.add(second);
  publishServer();
  conf();
  const auto outcome = prepare();
  ASSERT_FALSE(outcome.result.has_value());
  EXPECT_EQ(outcome.result.error().code, TransactionErrorCode::NeedsDecision) << codeName(outcome.result.error().code);
  ASSERT_FALSE(outcome.result.error().choices.empty());
  EXPECT_THAT(outcome.result.error().choices.front().candidates, UnorderedElementsAre("prov-a", "prov-b"));
}

TEST_F(UpgradePrepareTest, TheSameReplacementIsADecisionForAnInstall) {
  installed(core_, pkg("oldpkg"));
  PackageSpec replacing = pkg("newpkg");
  replacing.replaces = {"oldpkg"};
  core_.add(replacing);
  publishLive();
  conf();
  auto plan = engine().resolve(PlanRequest{.action = PlanAction::Install, .names = {"newpkg"}});
  ASSERT_FALSE(plan.has_value());
  EXPECT_EQ(plan.error().code, TransactionErrorCode::NeedsDecision);  // REQ-F-030
}

}  // namespace
}  // namespace holonight_packages_transaction::upgrade_testing
