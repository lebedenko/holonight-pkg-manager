#include "history_tag.h"

#include "fake_authorizer.h"
#include "fake_caller_identity.h"
#include "fake_clock.h"
#include "fake_plan_id_generator.h"
#include "fixture_root.h"
#include "gpg_keyring.h"
#include "gpgme_guard.h"
#include "holonight_packages_application/plan_service.h"
#include "holonight_packages_backends/pacman_log_history_source.h"
#include "holonight_packages_transaction/alpm_transaction_engine.h"
#include "manual_work_runner.h"
#include "recording_terminator.h"
#include "repo_builder.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace holonight_packages_transaction {
namespace {

namespace fs = std::filesystem;
using holonight_packages_application::AuthorizationDecision;
using holonight_packages_application::PlanService;
using holonight_packages_domain::ExecutionOutcome;
using holonight_packages_domain::ExecutionResult;
using holonight_packages_domain::HistoryOriginKind;
using holonight_packages_domain::HistoryReadMode;
using holonight_packages_domain::PlanAction;
using holonight_transaction_testing::FixtureRoot;
using holonight_transaction_testing::PackageSpec;
using holonight_transaction_testing::RepoBuilder;
using ::testing::HasSubstr;

constexpr std::string_view kCaller = ":1.10";
constexpr std::uint32_t kUid = 1000;

PackageSpec pkg(std::string name, std::string version = "1.0-1") {
  PackageSpec spec;
  spec.files = {{"usr/share/" + name + "/file", name}};
  spec.name = std::move(name);
  spec.version = std::move(version);
  return spec;
}

// The whole file, or nullopt when it does not exist.
std::optional<std::string> readAll(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::vector<std::string> linesOf(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  for (std::string line; std::getline(stream, line);) {
    lines.push_back(line);
  }
  return lines;
}

// True when `wanted` occurs in `events` in order, other events allowed in between.
bool isSubsequence(const std::vector<std::string>& events, const std::vector<std::string>& wanted) {
  auto position = events.begin();
  for (const std::string& event : wanted) {
    position = std::find(position, events.end(), event);
    if (position == events.end()) {
      return false;
    }
    ++position;
  }
  return true;
}

TEST(FormatHistoryTag, FormatTag) {
  const std::vector<std::string> two{"foo", "bar"};
  const std::vector<std::string> one{"foo"};
  EXPECT_EQ(formatHistoryTag(PlanAction::Install, two, kUid), "Running 'install foo bar' (uid 1000)");
  EXPECT_EQ(formatHistoryTag(PlanAction::Remove, one, kUid), "Running 'remove foo' (uid 1000)");
  EXPECT_EQ(formatHistoryTag(PlanAction::Upgrade, {}, kUid), "Running 'upgrade' (uid 1000)");
}

TEST(FormatHistoryTag, RejectsInvalidInput) {
  const std::vector<std::string> one{"foo"};
  const std::vector<std::string> quoted{"foo'bar"};
  const std::vector<std::string> spaced{"foo bar"};
  const std::vector<std::string> newline{"foo\nbar"};
  EXPECT_EQ(formatHistoryTag(PlanAction::Upgrade, one, kUid), std::nullopt);
  EXPECT_EQ(formatHistoryTag(PlanAction::Install, {}, kUid), std::nullopt);
  EXPECT_EQ(formatHistoryTag(PlanAction::Remove, {}, kUid), std::nullopt);
  EXPECT_EQ(formatHistoryTag(PlanAction::Install, quoted, kUid), std::nullopt);
  EXPECT_EQ(formatHistoryTag(PlanAction::Install, spaced, kUid), std::nullopt);
  EXPECT_EQ(formatHistoryTag(PlanAction::Install, newline, kUid), std::nullopt);
}

// An install through the real PlanService and the real engine on a temporary fixture root with a file:// repository.
class HistoryTagTest : public ::testing::Test {
 public:
  HistoryTagTest() : keyring_(world_.base() / "signing"), core_(world_, "core") {
    keyring_.trustIn(world_.gpgDir());
    core_.setSigner(keyring_.signer());
    identities_.set(std::string(kCaller), kUid);
    authorizer_.beforeAnswer = [this](const auto& /*request*/) {
      events_.emplace_back("authorize");
      if (onAuthorize) {
        onAuthorize();
      }
    };
  }

  void SetUp() override {
    // Package changes only inside the temporary fixture root (CLAUDE.md carve-out).
    const fs::path temp = fs::weakly_canonical(fs::temp_directory_path());
    const fs::path base = fs::weakly_canonical(world_.base());
    ASSERT_TRUE(base.string().starts_with(temp.string() + "/")) << base;
    core_.add(pkg("app"));
    core_.publish();
    world_.writeConf(std::vector<std::string>{"core"}, "Required DatabaseOptional");
  }

  [[nodiscard]] fs::path logPath() const { return world_.base() / "pacman.log"; }

  // Prepares and executes `names` as the caller; returns the result, or nullopt when preparing failed.
  std::optional<ExecutionResult> run(PlanAction action, const std::vector<std::string>& names) {
    std::optional<holonight_packages_application::PlanId> plan_id;
    std::string error;
    service_.prepare(std::string(kCaller), action == PlanAction::Install ? "install" : "remove", names, {},
                     [&plan_id, &error](auto prepared) {
                       if (prepared) {
                         plan_id = prepared->id;
                       } else {
                         error = holonight_packages_domain::transactionErrorName(prepared.error().code);
                       }
                     });
    if (!plan_id) {
      ADD_FAILURE() << "prepare failed: " << error;
      return std::nullopt;
    }
    std::optional<ExecutionResult> result;
    service_.execute(std::string(kCaller), *plan_id,
                     [&result](ExecutionResult outcome) { result = std::move(outcome); });
    return result;
  }

  [[nodiscard]] std::vector<std::string> holonightLines() const {
    std::vector<std::string> found;
    for (const std::string& line : linesOf(readAll(logPath()).value_or(""))) {
      if (line.contains("[HOLONIGHT]")) {
        found.push_back(line);
      }
    }
    return found;
  }

  // The origin the History adapter reads for the last transaction of the fixture log.
  [[nodiscard]] holonight_packages_domain::HistoryOrigin lastOrigin() const {
    holonight_packages_backends::PacmanLogHistorySource source(
        holonight_packages_backends::PacmanLogHistorySourceOptions{.pacman_conf_path = world_.confPath()});
    const auto result = source.read(HistoryReadMode::Full);
    EXPECT_FALSE(result.append.empty()) << result.reason;
    return result.append.empty() ? holonight_packages_domain::HistoryOrigin{} : result.append.back().origin;
  }

  std::function<void()> onAuthorize;
  std::function<int(std::string_view)> faults;

  FixtureRoot world_;
  holonight_transaction_testing::GpgKeyring keyring_;
  RepoBuilder core_;
  std::vector<std::string> events_;
  std::vector<std::string> journal_;
  holonight_packages_testing::FakeAuthorizer authorizer_;

 private:
  AlpmTransactionEngine engine_{
      world_.config(),
      [this](LogLevel /*level*/, const std::string& line) { journal_.push_back(line); },
      TransactionSeams{
          .observe = [this](std::string_view stage) { events_.emplace_back(stage); },
          .inject_fault = [this](std::string_view stage) { return faults ? faults(stage) : 0; },
          .fetch = {},
      },
  };
  holonight_packages_testing::FakeCallerIdentity identities_;
  holonight_packages_testing::FakePlanIdGenerator ids_;
  holonight_packages_testing::FakeClock clock_;
  holonight_packages_testing::ManualWorkRunner runner_;
  holonight_packages_testing::RecordingTerminator terminator_;
  PlanService service_{PlanService::Dependencies{
      .engine = engine_,
      .authorizer = authorizer_,
      .identities = identities_,
      .ids = ids_,
      .clock = clock_,
      .runner = runner_,
      .terminator = terminator_,
  }};
};

TEST_F(HistoryTagTest, TagOrder) {
  SKIP_UNLESS_FRESH_GPGME();
  const auto result = run(PlanAction::Install, {"app"});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->outcome, ExecutionOutcome::Succeeded);
  EXPECT_TRUE(isSubsequence(events_, {"authorize", "opened", "resolved", "tag-writing", "tag-written", "commit-point",
                                      "committed", "finished"}))
      << ::testing::PrintToString(events_);

  const std::vector<std::string> tags = holonightLines();
  ASSERT_EQ(tags.size(), 1U);
  EXPECT_TRUE(tags.front().ends_with("[HOLONIGHT] Running 'install app' (uid 1000)")) << tags.front();
  const std::vector<std::string> lines = linesOf(*readAll(logPath()));
  const auto tag = std::ranges::find_if(lines, [](const std::string& line) { return line.contains("[HOLONIGHT]"); });
  const auto started =
      std::ranges::find_if(lines, [](const std::string& line) { return line.contains("[ALPM] transaction started"); });
  ASSERT_NE(started, lines.end());
  EXPECT_LT(tag, started);

  // The History adapter reads the run as a Packages run of uid 1000 (T-027).
  const auto origin = lastOrigin();
  EXPECT_EQ(origin.kind, HistoryOriginKind::Packages);
  EXPECT_EQ(origin.uid, std::optional<std::uint32_t>(kUid));
}

TEST_F(HistoryTagTest, TagWriteFails) {
  SKIP_UNLESS_FRESH_GPGME();
  faults = [](std::string_view stage) { return stage == "tag-write" ? 1 : 0; };
  const auto result = run(PlanAction::Install, {"app"});
  ASSERT_TRUE(result);
  EXPECT_EQ(result->outcome, ExecutionOutcome::Succeeded);
  // The same change as a run whose tag was written.
  EXPECT_EQ(world_.installedVersions(), (std::map<std::string, std::string>{{"app", "1.0-1"}}));
  EXPECT_THAT(journal_, ::testing::Contains(HasSubstr("the history tag was not written")));
  EXPECT_TRUE(holonightLines().empty());
  EXPECT_TRUE(isSubsequence(events_, {"tag-writing", "tag-failed", "commit-point", "committed"}));

  // Without a tag and without a [PACMAN] Running line the adapter reads origin unknown (REQ-F-004, T-027).
  EXPECT_EQ(lastOrigin().kind, HistoryOriginKind::Unknown);
}

TEST_F(HistoryTagTest, RefusedWritesNothing) {
  SKIP_UNLESS_FRESH_GPGME();
  world_.installPackage(pkg("app"));
  const std::optional<std::string> before = readAll(logPath());

  // Authorization denied: nothing runs.
  authorizer_.answer = AuthorizationDecision::NotAuthorized;
  auto denied = run(PlanAction::Remove, {"app"});
  ASSERT_TRUE(denied);
  EXPECT_EQ(denied->failure.code, holonight_packages_domain::TransactionErrorCode::NotAuthorized);
  EXPECT_EQ(readAll(logPath()), before);

  // The repository changes between the approval and the run: the plan no longer matches and is refused.
  authorizer_.answer = AuthorizationDecision::Authorized;
  core_.add(pkg("other"));
  core_.publish();
  onAuthorize = [this] {
    core_.add(pkg("other", "2.0-1"));
    core_.publish();
  };
  auto changed = run(PlanAction::Install, {"other"});
  ASSERT_TRUE(changed);
  EXPECT_NE(changed->outcome, ExecutionOutcome::Succeeded);
  EXPECT_EQ(changed->failure.code, holonight_packages_domain::TransactionErrorCode::PlanChanged);
  EXPECT_EQ(readAll(logPath()), before);
  EXPECT_EQ(std::ranges::count(events_, std::string("tag-writing")), 0);
}

}  // namespace
}  // namespace holonight_packages_transaction
