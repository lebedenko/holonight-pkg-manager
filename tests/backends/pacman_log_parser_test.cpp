#include "pacman_log_parser.h"

#include <chrono>
#include <cstddef>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <random>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace {

using holonight_packages_backends::LogParseState;
using holonight_packages_backends::parseLines;
using holonight_packages_domain::HistoryAction;
using holonight_packages_domain::HistoryActionKind;
using holonight_packages_domain::HistoryEntry;
using holonight_packages_domain::HistoryMessage;
using holonight_packages_domain::HistoryOrigin;
using holonight_packages_domain::HistoryOriginKind;
using holonight_packages_domain::HistorySeverity;
using holonight_packages_domain::HistoryStatus;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using namespace std::chrono_literals;

// Parses `text` in one call and returns every entry, the still-changeable last one included.
std::vector<HistoryEntry> parseAll(std::string_view text) {
  LogParseState state;
  std::vector<HistoryEntry> entries;
  parseLines(text, state, entries);
  if (state.last) {
    entries.push_back(*state.last);
  }
  return entries;
}

constexpr std::string_view kStart = "[2026-11-23T10:00:00+0000] [ALPM] transaction started\n";
constexpr std::string_view kDone = "[2026-11-23T10:00:05+0000] [ALPM] transaction completed\n";

std::string line(std::string_view tag, std::string_view text) {
  return "[2026-11-23T10:00:01+0000] [" + std::string(tag) + "] " + std::string(text) + "\n";
}

TEST(PacmanLogParser, BlocksInFileOrder) {
  const std::string log = "[2026-11-21T08:00:00+0000] [ALPM] transaction started\n" + std::string(kDone) +
                          "[2026-11-22T08:00:00+0000] [ALPM] transaction started\n" + std::string(kDone) +
                          "[2026-11-23T08:00:00+0000] [ALPM] transaction started\n" + std::string(kDone);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 3U);
  EXPECT_LT(entries[0].started, entries[1].started);
  EXPECT_LT(entries[1].started, entries[2].started);
  EXPECT_EQ(entries[0].started, std::chrono::sys_days{2026y / 11 / 21} + 8h);
  for (const auto& entry : entries) {
    EXPECT_EQ(entry.status, HistoryStatus::Completed);
  }
}

TEST(PacmanLogParser, StartStartCompleted) {
  const std::string log =
      std::string(kStart) + std::string(kDone) + std::string(kStart) + std::string(kStart) + std::string(kDone);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 3U);
  EXPECT_EQ(entries[0].status, HistoryStatus::Completed);
  EXPECT_EQ(entries[1].status, HistoryStatus::Interrupted);
  EXPECT_EQ(entries[2].status, HistoryStatus::Completed);
}

TEST(PacmanLogParser, OpenBlockAtEndStaysInTheState) {
  LogParseState state;
  std::vector<HistoryEntry> finished;
  parseLines(std::string(kStart) + line("ALPM", "installed foo (1-1)"), state, finished);
  EXPECT_THAT(finished, IsEmpty());
  ASSERT_TRUE(state.last.has_value());
  EXPECT_TRUE(state.last_open);
  EXPECT_EQ(state.entries_finished, 0U);
}

TEST(PacmanLogParser, OriginFromRunningLines) {
  const std::string log = line("HOLONIGHT", "Running 'remove foo' (uid 1000)") + std::string(kStart) +
                          std::string(kDone) + line("PACMAN", "Running 'pacman --upgrade /tmp/x.pkg.tar.zst'") +
                          std::string(kStart) + std::string(kDone) + std::string(kStart) + std::string(kDone);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 3U);
  EXPECT_EQ(entries[0].origin, (HistoryOrigin{.kind = HistoryOriginKind::Packages, .uid = 1000, .command = {}}));
  EXPECT_EQ(entries[1].origin, (HistoryOrigin{.kind = HistoryOriginKind::Pacman,
                                              .uid = std::nullopt,
                                              .command = "pacman --upgrade /tmp/x.pkg.tar.zst"}));
  EXPECT_EQ(entries[2].origin.kind, HistoryOriginKind::Unknown);
}

TEST(PacmanLogParser, RunningLineIsUsedByOneStartOnly) {
  const std::string log =
      line("PACMAN", "Running 'pacman --sync --refresh --sysupgrade'") + std::string(kStart) + std::string(kStart);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_EQ(entries[0].origin.kind, HistoryOriginKind::Pacman);
  EXPECT_EQ(entries[1].origin.kind, HistoryOriginKind::Unknown);
}

TEST(PacmanLogParser, HolonightTagForms) {
  const auto originOf = [](std::string_view text) {
    return parseAll(line("HOLONIGHT", text) + std::string(kStart)).front().origin;
  };
  EXPECT_EQ(originOf("Running 'upgrade' (uid 0)").uid, 0U);
  EXPECT_EQ(originOf("Running 'install foo bar' (uid 1000)").kind, HistoryOriginKind::Packages);
  EXPECT_EQ(originOf("Running 'install lib32-gcc-libs gtk+ qt6-base' (uid 4294967295)").uid, 4294967295U);
  EXPECT_EQ(originOf("Running 'install foo'").kind, HistoryOriginKind::Unknown);
  EXPECT_EQ(originOf("Running 'install foo' (uid )").kind, HistoryOriginKind::Unknown);
  EXPECT_EQ(originOf("Running 'install foo' (uid 4294967296)").kind, HistoryOriginKind::Unknown);
  EXPECT_EQ(originOf("Running 'erase foo' (uid 1000)").kind, HistoryOriginKind::Unknown);
  EXPECT_EQ(originOf("Running 'install Foo' (uid 1000)").kind, HistoryOriginKind::Unknown);
  EXPECT_EQ(originOf("Running 'install foo' (uid 1000) extra").kind, HistoryOriginKind::Unknown);
}

// The real lines of DESIGN 5.3 (reference log).
TEST(PacmanLogParser, ActionKinds) {
  const std::string log = std::string(kStart) +
                          "[2025-02-02T17:11:44+0000] [ALPM] installed iana-etc (20241206-1)\n"
                          "[2025-02-03T00:16:54+0200] [ALPM] upgraded noto-fonts-cjk (20230817-2 -> 20240730-1)\n"
                          "[2025-08-12T15:54:32+0300] [ALPM] downgraded uwsm (0.23.1-1 -> 0.23.0-1)\n"
                          "[2025-02-02T17:19:20+0000] [ALPM] reinstalled systemd (257.2-2)\n"
                          "[2025-02-02T21:50:14+0200] [ALPM] removed nvidia (565.77-12)\n" +
                          std::string(kDone);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_THAT(
      entries[0].actions,
      ElementsAre(
          HistoryAction{
              .kind = HistoryActionKind::Installed, .name = "iana-etc", .old_version = "", .new_version = "20241206-1"},
          HistoryAction{.kind = HistoryActionKind::Upgraded,
                        .name = "noto-fonts-cjk",
                        .old_version = "20230817-2",
                        .new_version = "20240730-1"},
          HistoryAction{.kind = HistoryActionKind::Downgraded,
                        .name = "uwsm",
                        .old_version = "0.23.1-1",
                        .new_version = "0.23.0-1"},
          HistoryAction{
              .kind = HistoryActionKind::Reinstalled, .name = "systemd", .old_version = "", .new_version = "257.2-2"},
          HistoryAction{
              .kind = HistoryActionKind::Removed, .name = "nvidia", .old_version = "565.77-12", .new_version = ""}));
}

TEST(PacmanLogParser, ActionsOutsideAnOpenBlockAreIgnored) {
  const std::string log = line("ALPM", "installed early (1-1)") + std::string(kStart) + std::string(kDone) +
                          line("ALPM", "installed late (1-1)");
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_THAT(entries[0].actions, IsEmpty());
}

// Reference oracle for the action grammar of DESIGN 5.3.
std::optional<HistoryAction> oracle(const std::string& text) {
  static const std::regex kOne(R"(^(installed|reinstalled|removed) ([^ ()]+) \(([^ ()]+)\)$)");
  static const std::regex kTwo(R"(^(upgraded|downgraded) ([^ ()]+) \(([^ ()]+) -> ([^ ()]+)\)$)");
  std::smatch match;
  if (std::regex_match(text, match, kOne)) {
    const std::string verb = match[1];
    HistoryAction action{.kind = HistoryActionKind::Installed, .name = match[2], .old_version = {}, .new_version = {}};
    if (verb == "removed") {
      action.kind = HistoryActionKind::Removed;
      action.old_version = match[3];
    } else {
      action.kind = verb == "installed" ? HistoryActionKind::Installed : HistoryActionKind::Reinstalled;
      action.new_version = match[3];
    }
    return action;
  }
  if (std::regex_match(text, match, kTwo)) {
    return HistoryAction{
        .kind = match[1] == "upgraded" ? HistoryActionKind::Upgraded : HistoryActionKind::Downgraded,
        .name = match[2],
        .old_version = match[3],
        .new_version = match[4],
    };
  }
  return std::nullopt;
}

TEST(PacmanLogParser, AgreesWithTheRegexOracleOnASeededSample) {
  std::mt19937_64 random(0x484953);
  const std::vector<std::string> pieces = {
      "installed", "upgraded", "downgraded", "reinstalled", "removed", "foo", "lib32-a+b", "1:2.3-4", "(",
      ")",         " -> ",     " ",          "->",          "x_y.z@1", "",    "((",        "a b",
  };
  std::uniform_int_distribution<std::size_t> pick(0, pieces.size() - 1);
  std::uniform_int_distribution<int> length(1, 7);
  std::size_t matched = 0;
  for (int i = 0; i < 4000; ++i) {
    std::string text;
    // Bias toward well-formed lines so both branches are exercised.
    if (i % 2 == 0) {
      const std::string& verb = pieces[i % 5];
      text = verb + " " + pieces[5 + (pick(random) % 2)] + " (" + pieces[7];
      text += (verb == "upgraded" || verb == "downgraded") ? " -> 2-1)" : ")";
      if (pick(random) % 4 == 0) {
        text.insert(pick(random) % text.size(), pieces[pick(random)]);
      }
    } else {
      for (int count = length(random); count > 0; --count) {
        text += pieces[pick(random)];
      }
    }
    const auto expected = oracle(text);
    const auto entries = parseAll(std::string(kStart) + line("ALPM", text));
    ASSERT_EQ(entries.size(), 1U);
    if (expected) {
      ++matched;
      ASSERT_THAT(entries[0].actions, ElementsAre(*expected)) << text;
    } else {
      ASSERT_THAT(entries[0].actions, IsEmpty()) << text;
    }
  }
  EXPECT_GT(matched, 1000U);
}

TEST(PacmanLogParser, MessagesAttachToTheirTransaction) {
  const std::string log = std::string(kStart) + line("ALPM", "warning: foo: local is newer") + std::string(kDone) +
                          line("ALPM", "running '30-systemd-update.hook'...") +
                          line("ALPM", "error: bar: command failed to execute correctly") +
                          line("PACMAN", "Running 'pacman --sync baz'") + line("ALPM", "warning: after the boundary") +
                          std::string(kStart);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_THAT(entries[0].messages,
              ElementsAre(HistoryMessage{.severity = HistorySeverity::Warning, .text = "foo: local is newer"},
                          HistoryMessage{.severity = HistorySeverity::Error,
                                         .text = "bar: command failed to execute correctly"}));
  // A message between a Running line and its start line belongs to that next transaction, like its hooks.
  EXPECT_THAT(entries[1].messages,
              ElementsAre(HistoryMessage{.severity = HistorySeverity::Warning, .text = "after the boundary"}));
}

TEST(PacmanLogParser, ScriptletOutputIsExcluded) {
  const std::string log = std::string(kStart) + line("ALPM-SCRIPTLET", "warning: scriptlet text") +
                          line("ALPM-SCRIPTLET", "running 'fake.hook'...") + std::string(kDone);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_THAT(entries[0].messages, IsEmpty());
  EXPECT_THAT(entries[0].hooks, IsEmpty());
}

TEST(PacmanLogParser, MalformedLinesAreSkipped) {
  const std::string log =
      "no timestamp at all\n"
      "[2026-11-2\n"
      "[2026-11-23T10:00:00+0000] [FOO] bar\n"
      "[2026-11-23T10:00:00+0000] [ALPM] \xff\xfe\x80 invalid utf-8\n"
      "[2026-11-23T10:00:00+0000] [ALPM]transaction started\n"
      "[2026-02-30T10:00:00+0000] [ALPM] transaction started\n"
      "[2026-11-23T10:00:00+0000] [ALPM] installed broken (1-1\n"
      "\n" +
      std::string(kStart) + line("ALPM", "installed ok (1-1)") + std::string(kDone);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].status, HistoryStatus::Completed);
  EXPECT_EQ(entries[0].actions.size(), 1U);
}

TEST(PacmanLogParser, InterleavedRunningKeepsActionsWithTheOpenTransaction) {
  const auto entries = parseAll(line("PACMAN", "Running 'pacman --sync foo'") + std::string(kStart) +
                                line("PACMAN", "Running 'pacman --sync bar'") + line("ALPM", "installed foo (1-1)") +
                                std::string(kDone) + line("ALPM", "running 'pre.hook'...") + std::string(kStart) +
                                line("ALPM", "installed bar (1-1)") + std::string(kDone));
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_EQ(entries[0].origin.command, "pacman --sync foo");
  ASSERT_EQ(entries[0].actions.size(), 1U);
  EXPECT_EQ(entries[0].actions[0].name, "foo");
  EXPECT_THAT(entries[0].hooks, IsEmpty());
  EXPECT_EQ(entries[1].origin.command, "pacman --sync bar");
  EXPECT_THAT(entries[1].pre_hooks, ElementsAre("pre.hook"));
  ASSERT_EQ(entries[1].actions.size(), 1U);
  EXPECT_EQ(entries[1].actions[0].name, "bar");
}

TEST(PacmanLogParser, InvalidRecognizedRecordsCannotChangeState) {
  const auto entries =
      parseAll(line("PACMAN", "Running 'pacman --sync foo'") +
               "[2026-02-30T10:00:00+0000] [HOLONIGHT] Running 'install other' (uid 1000)\n" +
               line("PACMAN", "Running 'pacman \xff'") + std::string(kStart) +
               "[2500-01-01T00:00:00+0000] [ALPM] transaction completed\n"
               "[2026-02-30 10:00] [ALPM] installed bad-date (1-1)\n" +
               line("ALPM", "installed bad-utf8 (\xc0\xaf)") + line("ALPM", "warning: \xed\xa0\x80") +
               line("ALPM", "installed foo (1-1)") + std::string(kDone) + line("ALPM", "running 'bad\xff.hook'..."));
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].origin.command, "pacman --sync foo");
  EXPECT_EQ(entries[0].status, HistoryStatus::Completed);
  ASSERT_EQ(entries[0].actions.size(), 1U);
  EXPECT_EQ(entries[0].actions[0].name, "foo");
  EXPECT_THAT(entries[0].messages, IsEmpty());
  EXPECT_THAT(entries[0].hooks, IsEmpty());
}

TEST(PacmanLogParser, ValidUtf8MessagesArePreserved) {
  const std::string message = "café / ☃ / 🐈";
  const auto entries = parseAll(std::string(kStart) + line("ALPM", "warning: " + message) + std::string(kDone));
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_THAT(entries[0].messages, ElementsAre(HistoryMessage{.severity = HistorySeverity::Warning, .text = message}));
}

TEST(PacmanLogParser, CarriageReturnLineEndsAreAccepted) {
  const auto entries = parseAll(
      "[2026-11-23T10:00:00+0000] [ALPM] transaction started\r\n[2026-11-23T10:00:00+0000] [ALPM] transaction "
      "completed\r\n");
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].status, HistoryStatus::Completed);
}

TEST(PacmanLogParser, HookAttachment) {
  // REQ-F-009, first criterion.
  const std::string log = std::string(kStart) + std::string(kDone) + line("ALPM", "running 'a.hook'...") +
                          line("ALPM", "running 'b.hook'...") + line("PACMAN", "Running 'pacman --remove x'") +
                          line("ALPM", "running '71-dkms-remove.hook'...") + std::string(kStart);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_THAT(entries[0].pre_hooks, IsEmpty());
  EXPECT_THAT(entries[0].hooks, ElementsAre("a.hook", "b.hook"));
  EXPECT_THAT(entries[1].pre_hooks, ElementsAre("71-dkms-remove.hook"));
  EXPECT_THAT(entries[1].hooks, IsEmpty());
}

TEST(PacmanLogParser, HookWithoutRunningLineBeforeTheNextStartIsPostTransaction) {
  const std::string log =
      std::string(kStart) + std::string(kDone) + line("ALPM", "running 'a.hook'...") + std::string(kStart);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_THAT(entries[0].hooks, ElementsAre("a.hook"));
  EXPECT_THAT(entries[1].pre_hooks, IsEmpty());
}

TEST(PacmanLogParser, OrphanRunningLineDropsItsHooks) {
  const std::string log = line("HOLONIGHT", "Running 'install foo' (uid 1000)") +
                          line("ALPM", "running 'orphan.hook'...") + line("PACMAN", "Running 'pacman --sync bar'") +
                          line("ALPM", "running 'kept.hook'...") + std::string(kStart);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].origin.kind, HistoryOriginKind::Pacman);
  EXPECT_THAT(entries[0].pre_hooks, ElementsAre("kept.hook"));
}

TEST(PacmanLogParser, HookInsideAnOpenBlockIsNotListed) {
  const std::string log = std::string(kStart) + line("ALPM", "running 'inside.hook'...") + std::string(kDone);
  const auto entries = parseAll(log);
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_THAT(entries[0].hooks, IsEmpty());
  EXPECT_THAT(entries[0].pre_hooks, IsEmpty());
}

TEST(PacmanLogParser, SplittingTheInputAnywhereGivesTheSameEntries) {
  const std::string log =
      line("PACMAN", "Running 'pacman --sync --refresh --sysupgrade'") + line("ALPM", "running 'pre.hook'...") +
      std::string(kStart) + line("ALPM", "upgraded a (1-1 -> 2-1)") + std::string(kDone) +
      line("ALPM", "running 'post.hook'...") + line("ALPM", "warning: w") +
      line("HOLONIGHT", "Running 'remove b' (uid 1000)") + std::string(kStart) + line("ALPM", "removed b (1-1)");
  const auto whole = parseAll(log);
  for (std::size_t cut = 0; cut <= log.size(); ++cut) {
    if (cut != 0 && log[cut - 1] != '\n') {
      continue;  // the adapter only ever passes complete lines
    }
    LogParseState state;
    std::vector<HistoryEntry> entries;
    parseLines(std::string_view(log).substr(0, cut), state, entries);
    parseLines(std::string_view(log).substr(cut), state, entries);
    if (state.last) {
      entries.push_back(*state.last);
    }
    ASSERT_EQ(entries, whole) << "cut at " << cut;
  }
}

}  // namespace
