#include "commit_gate.h"
#include "fixture_root.h"
#include "gpg_keyring.h"
#include "gpgme_guard.h"
#include "holonight_packages_transaction/alpm_transaction_engine.h"
#include "manifest.h"
#include "repo_builder.h"

#include <algorithm>
#include <alpm.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace holonight_packages_transaction {
namespace {

namespace fs = std::filesystem;
using holonight_packages_domain::ChangeAction;
using holonight_packages_domain::ChangeKind;
using holonight_packages_domain::ExecutionContext;
using holonight_packages_domain::ExecutionControl;
using holonight_packages_domain::ExecutionOutcome;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlanRequest;
using holonight_packages_domain::ResolvedPlan;
using holonight_packages_domain::TransactionErrorCode;
using holonight_transaction_testing::FixtureRoot;
using holonight_transaction_testing::GpgKeyring;
using holonight_transaction_testing::InstallReason;
using holonight_transaction_testing::PackageSpec;
using holonight_transaction_testing::RepoBuilder;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

PackageSpec pkg(std::string name, std::string version = "1.0-1", std::vector<std::string> depends = {}) {
  PackageSpec spec;
  spec.files = {{"usr/share/" + name + "/file", name}};
  spec.name = std::move(name);
  spec.version = std::move(version);
  spec.depends = std::move(depends);
  return spec;
}

class ExecutionTest : public ::testing::Test {
 public:
  ExecutionTest() : keyring_(world_.base() / "signing"), core_(world_, "core") {
    keyring_.trustIn(world_.gpgDir());
    core_.setSigner(keyring_.signer());
  }

  void finish(const std::string& sigLevel = "Required DatabaseOptional") const {
    core_.publish();
    world_.writeConf({"core"}, sigLevel);
  }

  AlpmTransactionEngine engine() {
    TransactionSeams seams;
    seams.observe = [this](std::string_view stage) {
      stages_.emplace_back(stage);
      lockAtStage_[std::string(stage)] = fs::exists(world_.lockPath());
      if (onStage) {
        onStage(stage);
      }
    };
    seams.fetch = fetchSeam;
    seams.inject_fault = inject_fault;
    return {world_.config(), [this](LogLevel, const std::string& line) { journal_.push_back(line); }, std::move(seams)};
  }

  struct Approved {
    PlanRequest request;
    ResolvedPlan plan;
  };

  Approved approve(PlanAction action, std::vector<std::string> names) {
    PlanRequest request{.action = action, .names = std::move(names)};
    auto plan = engine().resolve(request);
    EXPECT_TRUE(plan.has_value()) << (plan ? "" : std::string(transactionErrorName(plan.error().code)));
    return Approved{.request = request, .plan = plan ? *plan : ResolvedPlan{}};
  }

  ExecutionResult run(const Approved& approved, const ExecutionContext& context = {}) {
    ExecutionResult result = engine().execute(approved.request, approved.plan, context);
    if (result.outcome == ExecutionOutcome::Failed && std::getenv("EXEC_DEBUG") != nullptr) {
      std::cerr << "execute failed: " << transactionErrorName(result.failure.code) << '\n';
      for (const std::string& line : journal_) {
        std::cerr << "  journal: " << line << '\n';
      }
    }
    return result;
  }

  static std::string transactionErrorName(holonight_packages_domain::TransactionErrorCode code) {
    return std::string(holonight_packages_domain::transactionErrorName(code));
  }

  FixtureRoot world_;
  GpgKeyring keyring_;
  RepoBuilder core_;
  std::vector<std::string> stages_;
  std::map<std::string, bool> lockAtStage_;
  std::vector<std::string> journal_;
  std::function<void(std::string_view)> onStage;
  std::function<int(const std::string&, const std::string&, bool)> fetchSeam;
  std::function<int(std::string_view)> inject_fault;
};

// ---- T-037: the previewed plan runs ----

TEST_F(ExecutionTest, InstallingExtractsExactlyThePreviewedPackagesAndReportsThem) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app", "1.0-1", {"lib"})).add(pkg("lib", "2.0-1")).add(pkg("bystander"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << transactionErrorName(result.failure.code);
  EXPECT_EQ(world_.installedVersions(), (std::map<std::string, std::string>{{"app", "1.0-1"}, {"lib", "2.0-1"}}));
  EXPECT_EQ(world_.readFile("usr/share/app/file"), "app");
  EXPECT_EQ(world_.readFile("usr/share/lib/file"), "lib");
  EXPECT_FALSE(world_.fileExists("usr/share/bystander/file"));
  ASSERT_EQ(result.changed.size(), 2U);
  for (const auto& change : result.changed) {
    EXPECT_EQ(change.action, ChangeAction::Install);
    EXPECT_TRUE(change.old_version.empty());
  }
  const std::vector<std::string> changedNames{result.changed[0].name, result.changed[1].name};
  EXPECT_THAT(changedNames, UnorderedElementsAre("app", "lib"));
  EXPECT_TRUE(result.warnings.empty());
}

TEST_F(ExecutionTest, RemovingDeletesTargetsAndOrphansAndKeepsModifiedConfigurationAsPacsave) {
  PackageSpec app = pkg("app", "1.0-1", {"lib"});
  app.files["etc/app.conf"] = "default";
  app.backup = {"etc/app.conf"};
  core_.add(pkg("unused"));
  finish();
  world_.installPackage(app);
  world_.installPackage(pkg("lib"), InstallReason::Dependency);
  {
    std::ofstream(world_.installRoot() / "etc/app.conf") << "edited by the user";
  }
  const auto approved = approve(PlanAction::Remove, {"app"});
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << transactionErrorName(result.failure.code);
  EXPECT_TRUE(world_.installedVersions().empty());
  EXPECT_FALSE(world_.fileExists("usr/share/app/file"));
  EXPECT_FALSE(world_.fileExists("usr/share/lib/file"));
  EXPECT_FALSE(world_.fileExists("etc/app.conf"));
  ASSERT_TRUE(world_.fileExists("etc/app.conf.pacsave"));
  EXPECT_EQ(world_.readFile("etc/app.conf.pacsave"), "edited by the user");
  ASSERT_EQ(result.changed.size(), 2U);
  for (const auto& change : result.changed) {
    EXPECT_EQ(change.action, ChangeAction::Remove);
    EXPECT_TRUE(change.new_version.empty());
  }
}

TEST_F(ExecutionTest, RemovalWorksWithoutAnyRepository) {
  core_.add(pkg("app"));
  finish();
  world_.installPackage(pkg("app"));
  const auto approved = approve(PlanAction::Remove, {"app"});
  fs::remove_all(world_.base() / "repos");
  fs::remove_all(world_.syncDbPath());
  EXPECT_EQ(run(approved).outcome, ExecutionOutcome::Succeeded);
  EXPECT_TRUE(world_.installedVersions().empty());
}

TEST_F(ExecutionTest, NoDatabaseIsRefreshedDuringAnInstall) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  const auto before = holonight_packages_testing::manifestOf(world_.syncDbPath());
  const auto outcome = run(approved);
  ASSERT_EQ(outcome.outcome, ExecutionOutcome::Succeeded) << transactionErrorName(outcome.failure.code);
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.syncDbPath()), before);
}

// ---- T-032: the database lock ----

TEST_F(ExecutionTest, TheLockIsHeldFromTheReResolutionToTheEndAndNeverOtherwise) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  EXPECT_FALSE(fs::exists(world_.lockPath()));
  const auto approved = approve(PlanAction::Install, {"app"});  // PreparePlan
  EXPECT_FALSE(fs::exists(world_.lockPath()));
  stages_.clear();
  ASSERT_EQ(run(approved).outcome, ExecutionOutcome::Succeeded);
  EXPECT_THAT(stages_, ElementsAre("opened", "resolved", "downloading", "commit-point", "committed", "finished"));
  EXPECT_FALSE(lockAtStage_.at("opened"));
  EXPECT_TRUE(lockAtStage_.at("resolved"));
  EXPECT_TRUE(lockAtStage_.at("commit-point"));
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(ExecutionTest, ALockHeldByAnotherProcessIsBusyAndIsNeverRemoved) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  {
    std::ofstream(world_.lockPath()) << "4242";
  }
  const auto before = holonight_packages_testing::manifestOf(world_.dbPath());
  const auto started = std::chrono::steady_clock::now();
  const ExecutionResult busy = run(approved);
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(1));
  ASSERT_EQ(busy.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(busy.failure.code, TransactionErrorCode::Busy);
  EXPECT_TRUE(fs::exists(world_.lockPath()));
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.dbPath()), before);
  EXPECT_TRUE(world_.installedVersions().empty());

  fs::remove(world_.lockPath());
  EXPECT_EQ(run(approved).outcome, ExecutionOutcome::Succeeded);  // the same approval still works
}

// ---- T-034: re-resolution ----

TEST_F(ExecutionTest, ARepositoryThatChangesAfterTheApprovalIsPlanChangedAndNothingStarts) {
  core_.add(pkg("app", "1.0-1"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  onStage = [this](std::string_view stage) {
    if (stage == "opened") {
      core_.add(pkg("app", "1.1-1"));
      core_.publish();
    }
  };
  const auto before = holonight_packages_testing::manifestOf(world_.localDbPath());
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::PlanChanged);
  EXPECT_THAT(stages_, ElementsAre("opened", "resolved", "finished"));  // never reached the commit
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.localDbPath()), before);
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(ExecutionTest, ARefusalDuringReResolutionBecomesPlanChanged) {
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  onStage = [this](std::string_view stage) {
    if (stage == "opened") {
      world_.installPackage(pkg("app"));  // now already installed: NothingToDo at re-resolution
    }
  };
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::PlanChanged);
  EXPECT_TRUE(result.failure.names.empty());
}

TEST_F(ExecutionTest, EnvironmentFailuresKeepTheirOwnCodes) {
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  fs::remove(world_.confPath());
  EXPECT_EQ(run(approved).failure.code, TransactionErrorCode::ConfigurationError);
  world_.writeConf({"core"}, "Never");
  EXPECT_EQ(run(approved).failure.code, TransactionErrorCode::InsecureSigLevel);
  world_.writeConf({"core"}, "Required DatabaseOptional");
  fs::remove(world_.syncDbPath() / "core.db");
  EXPECT_EQ(run(approved).failure.code, TransactionErrorCode::DatabaseUnavailable);
}

// ---- T-035: the commit gate ----

TEST(CommitGateTest, ACancelBeforeTheCommitPointWinsAndAnythingAfterIsRefused) {
  CommitGate gate;
  EXPECT_EQ(gate.state(), CommitGate::State::Pending);
  EXPECT_EQ(gate.requestCancel(), holonight_packages_domain::CancelDecision::Accepted);
  EXPECT_EQ(gate.requestCancel(), holonight_packages_domain::CancelDecision::Accepted);  // idempotent
  EXPECT_TRUE(gate.cancelRequested());
  EXPECT_FALSE(gate.tryCommit());
  EXPECT_EQ(gate.state(), CommitGate::State::CancelRequested);
}

TEST(CommitGateTest, AfterTheCommitPointACancelIsPastThePointOfNoReturnAndChangesNothing) {
  CommitGate gate;
  EXPECT_TRUE(gate.tryCommit());
  EXPECT_EQ(gate.requestCancel(), holonight_packages_domain::CancelDecision::PastPointOfNoReturn);
  EXPECT_EQ(gate.state(), CommitGate::State::Committed);
  EXPECT_TRUE(gate.tryCommit());
}

TEST(CommitGateTest, ExactlyOneOfCancelAndCommitWinsUnderContention) {
  for (int round = 0; round < 200; ++round) {
    CommitGate gate;
    bool committed = false;
    holonight_packages_domain::CancelDecision decision{};
    std::thread committer([&] { committed = gate.tryCommit(); });
    std::thread canceller([&] { decision = gate.requestCancel(); });
    committer.join();
    canceller.join();
    EXPECT_EQ(committed, decision == holonight_packages_domain::CancelDecision::PastPointOfNoReturn);
  }
}

TEST_F(ExecutionTest, TheEnginePublishesItsControlForTheDurationOfTheRun) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  std::vector<ExecutionControl*> published;
  ExecutionContext context;
  context.control_changed = [&published](ExecutionControl* control) { published.push_back(control); };
  ASSERT_EQ(run(approved, context).outcome, ExecutionOutcome::Succeeded);
  ASSERT_EQ(published.size(), 2U);
  EXPECT_NE(published[0], nullptr);
  EXPECT_EQ(published[1], nullptr);
}

TEST_F(ExecutionTest, ACancelRequestedBeforeTheCommitStartsNeverStartsIt) {
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  ExecutionControl* control = nullptr;
  ExecutionContext context;
  context.control_changed = [&control](ExecutionControl* published) {
    if (published != nullptr) {
      control = published;
    }
  };
  onStage = [&control](std::string_view stage) {
    if (stage == "resolved") {
      EXPECT_EQ(control->requestCancel(), holonight_packages_domain::CancelDecision::Accepted);
    }
  };
  const ExecutionResult result = run(approved, context);
  EXPECT_EQ(result.outcome, ExecutionOutcome::Cancelled);
  EXPECT_THAT(stages_, ElementsAre("opened", "resolved", "finished"));
  EXPECT_TRUE(world_.installedVersions().empty());
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(ExecutionTest, ACancelAtTheCommitPointStopsBeforeAnyPackageChanges) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app", "1.0-1", {"lib"})).add(pkg("lib"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  ExecutionControl* control = nullptr;
  ExecutionContext context;
  context.control_changed = [&control](ExecutionControl* published) {
    if (published != nullptr) {
      control = published;
    }
  };
  onStage = [&control](std::string_view stage) {
    if (stage == "commit-point") {
      EXPECT_EQ(control->requestCancel(), holonight_packages_domain::CancelDecision::Accepted);
    }
  };
  const ExecutionResult result = run(approved, context);
  EXPECT_EQ(result.outcome, ExecutionOutcome::Cancelled);
  EXPECT_TRUE(world_.installedVersions().empty());
  EXPECT_FALSE(world_.fileExists("usr/share/app/file"));
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

// ---- T-042 and T-043: failure results ----

TEST_F(ExecutionTest, AnUnownedFileInTheWayIsAFileConflictNamingThePackageAndChangesNothing) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  fs::create_directories(world_.installRoot() / "usr/share/app");
  {
    std::ofstream(world_.installRoot() / "usr/share/app/file") << "someone else's file";
  }
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::FileConflicts);
  EXPECT_THAT(result.failure.names, ElementsAre("app"));
  EXPECT_TRUE(result.changed.empty());
  EXPECT_EQ(world_.readFile("usr/share/app/file"), "someone else's file");
  EXPECT_TRUE(world_.installedVersions().empty());
}

TEST_F(ExecutionTest, RawLibalpmTextReachesTheJournalAndNeverTheResult) {
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  core_.deletePackageFile("app");
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_THAT(journal_, ::testing::Contains(::testing::HasSubstr("failed retrieving file")));
  for (const std::string& name : result.failure.names) {
    EXPECT_EQ(name.find("failed retrieving"), std::string::npos);
  }
}

TEST_F(ExecutionTest, AFailureAfterTheFirstPackageListsOnlyWhatCompleted) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app", "1.0-1", {"lib"})).add(pkg("lib"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  // `lib` installs first (app depends on it); libalpm then "fails" before `app`.
  inject_fault = [](std::string_view stage) { return stage == "package-done" ? static_cast<int>(ALPM_ERR_SYSTEM) : 0; };
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::TransactionFailed);
  ASSERT_EQ(result.changed.size(), 1U);
  EXPECT_EQ(result.changed[0].name, "lib");
  EXPECT_EQ(result.changed[0].action, ChangeAction::Install);
  EXPECT_EQ(world_.installedVersions(), (std::map<std::string, std::string>{{"lib", "1.0-1"}}));  // matches the list
  EXPECT_FALSE(world_.fileExists("usr/share/app/file"));
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(ExecutionTest, AnUnexpectedFaultBeforeTheCommitPointRollsBackAsAnInternalError) {
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  inject_fault = [](std::string_view stage) { return stage == "resolved" ? static_cast<int>(ALPM_ERR_MEMORY) : 0; };
  const auto before = holonight_packages_testing::manifestOf(world_.localDbPath());
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::InternalError);
  EXPECT_TRUE(result.changed.empty());
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.localDbPath()), before);
  EXPECT_THAT(stages_, ElementsAre("opened", "resolved", "finished"));
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(ExecutionTest, AResultCarriesOnlyEnumeratedFields) {
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  inject_fault = [](std::string_view stage) { return stage == "resolved" ? static_cast<int>(ALPM_ERR_SYSTEM) : 0; };
  const ExecutionResult result = run(approved);
  // The failure is a code plus package names; there is no free-text member to fill (REQ-F-051).
  EXPECT_FALSE(holonight_packages_domain::transactionErrorName(result.failure.code).empty());
  EXPECT_TRUE(result.failure.names.empty());
  EXPECT_TRUE(result.warnings.empty());
}

// ---- T-058: no external process ----

TEST_F(ExecutionTest, AFullInstallNeverStartsAPacmanFromThePath) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  const fs::path trap = world_.base() / "trap";
  fs::create_directories(trap);
  const fs::path marker = world_.base() / "pacman-was-run";
  {
    std::ofstream script(trap / "pacman");
    script << "#!/bin/sh\necho ran > '" << marker.string() << "'\n";
  }
  fs::permissions(trap / "pacman", fs::perms::owner_all);
  const std::string oldPath = std::getenv("PATH") != nullptr ? std::getenv("PATH") : "";
  ::setenv("PATH", (trap.string() + ":" + oldPath).c_str(), 1);
  const ExecutionResult result = run(approved);
  ::setenv("PATH", oldPath.c_str(), 1);
  EXPECT_EQ(result.outcome, ExecutionOutcome::Succeeded);
  EXPECT_TRUE(world_.isInstalled("app"));
  EXPECT_FALSE(fs::exists(marker)) << "the helper started pacman";
}

// ---- T-041: the corrupted-package retry ----

// A downloader that copies like libalpm's own, and damages the first `corrupt` copies of a package file.
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
      const bool isPackage = source.filename().string().ends_with(".pkg.tar.zst");
      if (isPackage && ++packageFetches_ <= corrupt_) {
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

TEST_F(ExecutionTest, ACorruptPackageIsDownloadedAgainOnceAndTheRetryUsesAFreshLock) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  CorruptingFetch fetch(1);
  fetchSeam = fetch.function();
  std::vector<bool> lockAtResolved;
  onStage = [&](std::string_view stage) {
    if (stage == "resolved") {
      lockAtResolved.push_back(fs::exists(world_.lockPath()));
    }
  };
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << transactionErrorName(result.failure.code);
  EXPECT_EQ(fetch.packageFetches(), 2);
  EXPECT_EQ(lockAtResolved, (std::vector<bool>{true, true}));  // each attempt re-resolves under the lock
  EXPECT_TRUE(world_.isInstalled("app"));
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(ExecutionTest, ASecondCorruptionFailsWithAnEmptyChangedListAndIsNotRetriedAgain) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  CorruptingFetch fetch(2);
  fetchSeam = fetch.function();
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::TransactionFailed);
  EXPECT_TRUE(result.changed.empty());
  EXPECT_EQ(fetch.packageFetches(), 2);  // never a third attempt
  EXPECT_TRUE(world_.installedVersions().empty());
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(ExecutionTest, ASignatureThatDoesNotVerifyIsNeverRetried) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  const fs::path signature = core_.packagePath("app").string() + ".sig";
  {
    std::fstream file(signature, std::ios::in | std::ios::out | std::ios::binary);
    file.seekp(-5, std::ios::end);
    file.put('X');
  }
  CorruptingFetch fetch(0);
  fetchSeam = fetch.function();
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::SignatureInvalid);
  EXPECT_EQ(fetch.packageFetches(), 1);
  EXPECT_TRUE(world_.installedVersions().empty());
}

// ---- T-040: questions during Execute ----

TEST_F(ExecutionTest, ADecisionThatAppearsAfterTheApprovalFailsTheRunAsADecisionWithNothingChanged) {
  PackageSpec replacement = pkg("newpkg");
  replacement.conflicts = {"oldpkg"};
  core_.add(replacement);
  finish();
  const auto approved = approve(PlanAction::Install, {"newpkg"});
  onStage = [this](std::string_view stage) {
    if (stage == "opened") {
      world_.installPackage(pkg("oldpkg"));  // now newpkg conflicts with something installed
    }
  };
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::NeedsDecision);
  ASSERT_EQ(result.failure.choices.size(), 1U);
  EXPECT_EQ(result.failure.choices[0].kind, holonight_packages_domain::NeedsDecisionChoice::Kind::Conflict);
  EXPECT_EQ(world_.installedVersions(),
            (std::map<std::string, std::string>{{"oldpkg", "1.0-1"}}));  // newpkg never started
  EXPECT_FALSE(world_.fileExists("usr/share/newpkg/file"));
  EXPECT_THAT(journal_, ::testing::Contains(::testing::HasSubstr("conflicting-package")));
}

TEST_F(ExecutionTest, APackageSignedByAKeyOutsideTheKeyringIsRefusedAndTheKeyringIsLeftAlone) {
  SKIP_UNLESS_FRESH_GPGME();
  GpgKeyring stranger(world_.base() / "stranger");
  core_.setSigner(stranger.signer());
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  const auto readKeyring = [this] {
    std::ifstream input(world_.gpgDir() / "pubring.kbx", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  };
  const std::string keyringBefore = readKeyring();
  ASSERT_FALSE(keyringBefore.empty());
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::SignatureInvalid);
  EXPECT_TRUE(result.changed.empty());
  EXPECT_TRUE(world_.installedVersions().empty());
  EXPECT_EQ(readKeyring(), keyringBefore);  // the stranger's key was never imported
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

// ---- T-039: progress ----

std::vector<holonight_packages_domain::ProgressPhase> collapse(
    const std::vector<holonight_packages_domain::ProgressUpdate>& updates) {
  std::vector<holonight_packages_domain::ProgressPhase> phases;
  for (const auto& update : updates) {
    if (phases.empty() || phases.back() != update.phase) {
      phases.push_back(update.phase);
    }
  }
  return phases;
}

TEST_F(ExecutionTest, AnInstallReportsItsPhasesInOrderWithHooksBeforeAndAfterTheInstalling) {
  SKIP_UNLESS_FRESH_GPGME();
  using holonight_packages_domain::ProgressPhase;
  core_.add(pkg("app"));
  finish();
  const std::string trigger = "[Trigger]\nOperation = Install\nType = Package\nTarget = app\n[Action]\nWhen = ";
  world_.writeHook("10-pre.hook", trigger + "PreTransaction\nExec = /usr/bin/true\n");
  world_.writeHook("20-post.hook", trigger + "PostTransaction\nExec = /usr/bin/true\n");
  const auto approved = approve(PlanAction::Install, {"app"});
  std::vector<holonight_packages_domain::ProgressUpdate> updates;
  ExecutionContext context;
  context.progress = [&updates](const holonight_packages_domain::ProgressUpdate& update) { updates.push_back(update); };
  const ExecutionResult result = run(approved, context);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Succeeded) << transactionErrorName(result.failure.code);
  EXPECT_THAT(collapse(updates),
              ElementsAre(ProgressPhase::Resolving, ProgressPhase::Downloading, ProgressPhase::Verifying,
                          ProgressPhase::Hooks, ProgressPhase::Installing, ProgressPhase::Hooks, ProgressPhase::Done));
  ASSERT_FALSE(updates.empty());
  EXPECT_EQ(updates.back(), (holonight_packages_domain::ProgressUpdate{
                                .phase = ProgressPhase::Done, .current = 1, .total = 1, .item = {}}));
  // `current` never decreases inside one occurrence of a phase.
  for (std::size_t i = 1; i < updates.size(); ++i) {
    if (updates[i].phase == updates[i - 1].phase) {
      EXPECT_GE(updates[i].current, updates[i - 1].current) << i;
    }
  }
  const auto downloading =
      std::ranges::find_if(updates, [](const auto& update) { return update.phase == ProgressPhase::Downloading; });
  ASSERT_NE(downloading, updates.end());
  EXPECT_EQ(downloading->total, 1U);
}

TEST_F(ExecutionTest, ARemovalWithoutHooksReportsNoHooksPhaseAndEndsWithDone) {
  using holonight_packages_domain::ProgressPhase;
  core_.add(pkg("unused"));
  finish();
  world_.installPackage(pkg("app"));
  const auto approved = approve(PlanAction::Remove, {"app"});
  std::vector<holonight_packages_domain::ProgressUpdate> updates;
  ExecutionContext context;
  context.progress = [&updates](const holonight_packages_domain::ProgressUpdate& update) { updates.push_back(update); };
  ASSERT_EQ(run(approved, context).outcome, ExecutionOutcome::Succeeded);
  const auto phases = collapse(updates);
  EXPECT_THAT(phases, ::testing::Contains(ProgressPhase::Removing));
  EXPECT_THAT(phases, ::testing::Not(::testing::Contains(ProgressPhase::Hooks)));
  EXPECT_EQ(phases.back(), ProgressPhase::Done);
}

TEST_F(ExecutionTest, AFailedRunNeverReportsDone) {
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  core_.deletePackageFile("app");
  std::vector<holonight_packages_domain::ProgressUpdate> updates;
  ExecutionContext context;
  context.progress = [&updates](const holonight_packages_domain::ProgressUpdate& update) { updates.push_back(update); };
  EXPECT_EQ(run(approved, context).outcome, ExecutionOutcome::Failed);
  EXPECT_THAT(collapse(updates), ::testing::Not(::testing::Contains(holonight_packages_domain::ProgressPhase::Done)));
}

// ---- T-038: download failures ----

TEST_F(ExecutionTest, ADeletedPackageFileIsAStaleMirrorAndNothingChanges) {
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  core_.deletePackageFile("app");
  const auto before = holonight_packages_testing::manifestOf(world_.localDbPath());
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::SyncDatabaseStale);
  EXPECT_THAT(result.failure.names, ElementsAre("app"));
  EXPECT_TRUE(result.changed.empty());
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.localDbPath()), before);
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(ExecutionTest, AnUnreadablePackageFileIsADownloadFailureNotAStaleMirror) {
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  core_.makeUnreadable("app");
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::DownloadFailed);
}

TEST_F(ExecutionTest, APackageWithoutItsSignatureFileIsASignatureFailureNotAStaleMirror) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  core_.deleteSignature("app");
  const ExecutionResult result = run(approved);
  ASSERT_EQ(result.outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(result.failure.code, TransactionErrorCode::SignatureInvalid);
  EXPECT_TRUE(world_.installedVersions().empty());
}

// ---- T-036: cancel while the work runs ----

// Holds the download until release(), then copies the file like libalpm's own downloader would. Installed through the
// fetch seam, so the engine stalls in its download phase deterministically with no network.
class StalledFetch {
 public:
  std::function<int(const std::string&, const std::string&, bool)> function() {
    return [this](const std::string& url, const std::string& destination, bool) {
      {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return released_; });
      }
      const fs::path source = url.starts_with("file://") ? fs::path(url.substr(7)) : fs::path(url);
      std::error_code error;
      fs::copy_file(source, fs::path(destination) / source.filename(), fs::copy_options::overwrite_existing, error);
      return error ? -1 : 0;
    };
  }
  void release() {
    {
      const std::scoped_lock lock(mutex_);
      released_ = true;
    }
    condition_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool released_ = false;
};

struct Signal {
  std::mutex mutex;
  std::condition_variable condition;
  bool raised = false;
  void raise() {
    {
      const std::scoped_lock lock(mutex);
      raised = true;
    }
    condition.notify_all();
  }
  bool wait(std::chrono::seconds limit) {
    std::unique_lock lock(mutex);
    return condition.wait_for(lock, limit, [this] { return raised; });
  }
};

TEST_F(ExecutionTest, CancelDuringTheDownloadAcceptsThenNothingIsInstalled) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  StalledFetch stalled;
  fetchSeam = stalled.function();
  const auto localBefore = holonight_packages_testing::manifestOf(world_.localDbPath());
  const auto rootBefore = holonight_packages_testing::manifestOf(world_.installRoot());

  Signal downloading;
  std::atomic<ExecutionControl*> control{nullptr};
  ExecutionContext context;
  context.control_changed = [&control](ExecutionControl* published) {
    if (published != nullptr) {
      control = published;
    }
  };
  onStage = [&downloading](std::string_view stage) {
    if (stage == "downloading") {
      downloading.raise();
    }
  };
  ExecutionResult result;
  std::thread worker([&] { result = run(approved, context); });
  ASSERT_TRUE(downloading.wait(std::chrono::seconds(10)));
  ASSERT_NE(control.load(), nullptr);
  EXPECT_TRUE(fs::exists(world_.lockPath()));  // the lock is held while the download is stalled
  EXPECT_EQ(control.load()->requestCancel(), holonight_packages_domain::CancelDecision::Accepted);
  stalled.release();  // in production the process ends here; the test lets the worker finish instead
  worker.join();

  EXPECT_EQ(result.outcome, ExecutionOutcome::Cancelled);
  EXPECT_TRUE(world_.installedVersions().empty());
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.localDbPath()), localBefore);
  EXPECT_EQ(holonight_packages_testing::manifestOf(world_.installRoot()), rootBefore);
  EXPECT_FALSE(fs::exists(world_.lockPath()));
}

TEST_F(ExecutionTest, CancelAfterTheCommitPointIsRefusedAndTheInstallCompletes) {
  SKIP_UNLESS_FRESH_GPGME();
  core_.add(pkg("app"));
  finish();
  const auto approved = approve(PlanAction::Install, {"app"});
  std::atomic<ExecutionControl*> control{nullptr};
  ExecutionContext context;
  context.control_changed = [&control](ExecutionControl* published) {
    if (published != nullptr) {
      control = published;
    }
  };
  std::optional<holonight_packages_domain::CancelDecision> decision;
  onStage = [&](std::string_view stage) {
    if (stage == "committed") {
      decision = control.load()->requestCancel();
    }
  };
  const ExecutionResult result = run(approved, context);
  ASSERT_TRUE(decision.has_value());
  EXPECT_EQ(*decision, holonight_packages_domain::CancelDecision::PastPointOfNoReturn);
  EXPECT_EQ(result.outcome, ExecutionOutcome::Succeeded);
  EXPECT_TRUE(world_.isInstalled("app"));
}

}  // namespace
}  // namespace holonight_packages_transaction
