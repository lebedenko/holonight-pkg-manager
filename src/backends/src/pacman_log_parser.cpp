#include "pacman_log_parser.h"

#include "pacman_log_timestamp.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <memory>
#include <utility>

namespace holonight_packages_backends {
namespace {

using holonight_packages_domain::HistoryAction;
using holonight_packages_domain::HistoryActionKind;
using holonight_packages_domain::HistoryEntry;
using holonight_packages_domain::HistoryMessage;
using holonight_packages_domain::HistoryOrigin;
using holonight_packages_domain::HistoryOriginKind;
using holonight_packages_domain::HistorySeverity;
using holonight_packages_domain::HistoryStatus;

enum class Tag : std::uint8_t { Alpm, Pacman, Holonight, Other };

struct Line {
  std::chrono::system_clock::time_point timestamp;
  Tag tag = Tag::Other;
  std::string_view text;  // after "] ", without the line end
};

Tag tagOf(std::string_view name) noexcept {
  if (name == "ALPM") {
    return Tag::Alpm;
  }
  if (name == "PACMAN") {
    return Tag::Pacman;
  }
  if (name == "HOLONIGHT") {
    return Tag::Holonight;
  }
  return Tag::Other;  // ALPM-SCRIPTLET and anything unknown (REQ-F-011)
}

std::size_t utf8ContinuationCount(unsigned char lead) {
  if (lead < 0xE0) {
    return 1;
  }
  return lead < 0xF0 ? 2 : 3;
}

bool isValidUtf8(std::string_view text) {
  while (!text.empty()) {
    const auto lead = static_cast<unsigned char>(text.front());
    text.remove_prefix(1);
    if (lead < 0x80) {
      continue;
    }
    if (lead < 0xC2 || lead > 0xF4) {
      return false;
    }
    const std::size_t count = utf8ContinuationCount(lead);
    if (text.size() < count) {
      return false;
    }
    // The first continuation byte rules out overlong encodings, surrogates and values above U+10FFFF.
    const auto first = static_cast<unsigned char>(text.front());
    if ((lead == 0xE0 && first < 0xA0) || (lead == 0xED && first > 0x9F) || (lead == 0xF0 && first < 0x90) ||
        (lead == 0xF4 && first > 0x8F)) {
      return false;
    }
    if (!std::ranges::all_of(text.substr(0, count), [](unsigned char byte) { return byte >= 0x80 && byte <= 0xBF; })) {
      return false;
    }
    text.remove_prefix(count);
  }
  return true;
}

// `[<ts>] [<tag>] <text>`; nullopt for anything else.
std::optional<Line> splitLine(std::string_view raw) {
  if (raw.ends_with('\r')) {
    raw.remove_suffix(1);
  }
  if (!raw.starts_with('[')) {
    return std::nullopt;
  }
  const std::size_t ts_end = raw.find(']');
  if (ts_end == std::string_view::npos) {
    return std::nullopt;
  }
  if (raw.substr(ts_end + 1, 2) != " [") {
    return std::nullopt;
  }
  const std::size_t tag_begin = ts_end + 3;
  const std::size_t tag_end = raw.find(']', tag_begin);
  if (tag_end == std::string_view::npos || raw.substr(tag_end + 1, 1) != " ") {
    return std::nullopt;
  }
  const Tag tag = tagOf(raw.substr(tag_begin, tag_end - tag_begin));
  if (tag == Tag::Other || !isValidUtf8(raw.substr(tag_end + 2))) {
    return std::nullopt;
  }
  const auto timestamp = parseTimestamp(raw.substr(1, ts_end - 1));
  if (!timestamp) {
    return std::nullopt;
  }
  return Line{
      .timestamp = *timestamp,
      .tag = tag,
      .text = raw.substr(tag_end + 2),
  };
}

bool isToken(std::string_view text) noexcept {
  return !text.empty() && text.find_first_of(" ()") == std::string_view::npos;
}

// "<name> (<version>)" or "<name> (<old> -> <new>)".
std::optional<HistoryAction> parseAction(HistoryActionKind kind, std::string_view rest, bool two_versions) {
  const std::size_t space = rest.find(' ');
  if (space == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view name = rest.substr(0, space);
  std::string_view versions = rest.substr(space + 1);
  if (!isToken(name) || !versions.starts_with('(') || !versions.ends_with(')')) {
    return std::nullopt;
  }
  versions = versions.substr(1, versions.size() - 2);
  HistoryAction action{.kind = kind, .name = std::string(name), .old_version = {}, .new_version = {}};
  if (two_versions) {
    const std::size_t arrow = versions.find(" -> ");
    if (arrow == std::string_view::npos) {
      return std::nullopt;
    }
    const std::string_view old_version = versions.substr(0, arrow);
    const std::string_view new_version = versions.substr(arrow + 4);
    if (!isToken(old_version) || !isToken(new_version)) {
      return std::nullopt;
    }
    action.old_version = old_version;
    action.new_version = new_version;
    return action;
  }
  if (!isToken(versions)) {
    return std::nullopt;
  }
  if (kind == HistoryActionKind::Removed) {
    action.old_version = versions;
  } else {
    action.new_version = versions;  // installed, reinstalled
  }
  return action;
}

std::optional<HistoryAction> actionOf(std::string_view text) {
  struct Verb {
    std::string_view prefix;
    HistoryActionKind kind;
    bool two_versions;
  };
  static constexpr std::array kVerbs = {
      Verb{.prefix = "installed ", .kind = HistoryActionKind::Installed, .two_versions = false},
      Verb{.prefix = "upgraded ", .kind = HistoryActionKind::Upgraded, .two_versions = true},
      Verb{.prefix = "downgraded ", .kind = HistoryActionKind::Downgraded, .two_versions = true},
      Verb{.prefix = "reinstalled ", .kind = HistoryActionKind::Reinstalled, .two_versions = false},
      Verb{.prefix = "removed ", .kind = HistoryActionKind::Removed, .two_versions = false},
  };
  for (const Verb& verb : kVerbs) {
    if (text.starts_with(verb.prefix)) {
      return parseAction(verb.kind, text.substr(verb.prefix.size()), verb.two_versions);
    }
  }
  return std::nullopt;
}

// "running '<name>'..."
std::optional<std::string_view> hookOf(std::string_view text) {
  constexpr std::string_view kPrefix = "running '";
  constexpr std::string_view kSuffix = "'...";
  if (!text.starts_with(kPrefix) || !text.ends_with(kSuffix) || text.size() <= kPrefix.size() + kSuffix.size()) {
    return std::nullopt;
  }
  const std::string_view name = text.substr(kPrefix.size(), text.size() - kPrefix.size() - kSuffix.size());
  if (name.contains('\'')) {
    return std::nullopt;
  }
  return name;
}

std::optional<HistoryMessage> messageOf(std::string_view text) {
  constexpr std::string_view kWarning = "warning: ";
  constexpr std::string_view kError = "error: ";
  if (text.starts_with(kWarning)) {
    return HistoryMessage{.severity = HistorySeverity::Warning, .text = std::string(text.substr(kWarning.size()))};
  }
  if (text.starts_with(kError)) {
    return HistoryMessage{.severity = HistorySeverity::Error, .text = std::string(text.substr(kError.size()))};
  }
  return std::nullopt;
}

// [PACMAN] Running '<command>'
std::optional<HistoryOrigin> pacmanOriginOf(std::string_view text) {
  constexpr std::string_view kPrefix = "Running '";
  if (!text.starts_with(kPrefix) || !text.ends_with('\'') || text.size() <= kPrefix.size()) {
    return std::nullopt;
  }
  return HistoryOrigin{
      .kind = HistoryOriginKind::Pacman,
      .uid = std::nullopt,
      .command = std::string(text.substr(kPrefix.size(), text.size() - kPrefix.size() - 1)),
  };
}

bool isPackageNameChar(char character) noexcept {
  constexpr std::string_view kPunctuation = "@._+- ";
  return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
         kPunctuation.contains(character);
}

// [HOLONIGHT] Running '<install|remove|upgrade>[ <names…>]' (uid N)   (REQ-F-002)
std::optional<HistoryOrigin> holonightOriginOf(std::string_view text) {
  constexpr std::string_view kPrefix = "Running '";
  constexpr std::string_view kUid = "' (uid ";
  if (!text.starts_with(kPrefix) || !text.ends_with(')')) {
    return std::nullopt;
  }
  const std::size_t uid_at = text.rfind(kUid);
  if (uid_at == std::string_view::npos || uid_at < kPrefix.size()) {
    return std::nullopt;
  }
  const std::string_view quoted = text.substr(kPrefix.size(), uid_at - kPrefix.size());
  const std::size_t space = quoted.find(' ');
  const std::string_view action = quoted.substr(0, space);
  if (action != "install" && action != "remove" && action != "upgrade") {
    return std::nullopt;
  }
  for (const char character : quoted.substr(action.size())) {
    if (!isPackageNameChar(character)) {
      return std::nullopt;
    }
  }
  const std::string_view digits = text.substr(uid_at + kUid.size(), text.size() - uid_at - kUid.size() - 1);
  std::uint32_t uid = 0;
  const char* const digits_end = std::to_address(digits.end());
  const auto [end, error] = std::from_chars(std::to_address(digits.begin()), digits_end, uid);
  if (digits.empty() || error != std::errc{} || end != digits_end) {
    return std::nullopt;
  }
  return HistoryOrigin{.kind = HistoryOriginKind::Packages, .uid = uid, .command = {}};
}

void finishLast(LogParseState& state, std::vector<HistoryEntry>& finished) {
  if (!state.last) {
    return;
  }
  if (state.last_open) {
    state.last->status = HistoryStatus::Interrupted;  // a start line follows without a completed line (REQ-F-014)
  }
  finished.push_back(std::move(*state.last));
  state.last.reset();
  state.last_open = false;
  ++state.entries_finished;
}

void onRunning(LogParseState& state, HistoryOrigin origin) {
  state.pending_origin = std::move(origin);
  state.pending_hooks.clear();  // an orphan Running line's hooks and messages are dropped
  state.pending_messages.clear();
  state.attach_open = false;
}

void onStart(LogParseState& state, std::vector<HistoryEntry>& finished, std::chrono::system_clock::time_point started) {
  finishLast(state, finished);
  HistoryEntry entry;
  entry.started = started;
  entry.origin = state.pending_origin.value_or(HistoryOrigin{});
  entry.pre_hooks = std::exchange(state.pending_hooks, {});
  entry.messages = std::exchange(state.pending_messages, {});
  entry.status = HistoryStatus::Interrupted;  // until the completed line, or the lock probe of the adapter
  state.pending_origin.reset();
  state.last = std::move(entry);
  state.last_open = true;
  state.attach_open = true;
}

void onAlpmText(LogParseState& state, std::vector<HistoryEntry>& finished, const Line& line) {
  if (line.text == "transaction started") {
    onStart(state, finished, line.timestamp);
    return;
  }
  if (line.text == "transaction completed") {
    if (state.last && state.last_open) {
      state.last->status = HistoryStatus::Completed;
      state.last_open = false;
    }
    return;
  }
  if (auto action = actionOf(line.text)) {
    if (state.last && state.last_open) {
      state.last->actions.push_back(std::move(*action));
    }
    return;
  }
  if (const auto hook = hookOf(line.text)) {
    if (state.pending_origin) {
      state.pending_hooks.emplace_back(*hook);  // pre-transaction (REQ-F-009)
    } else if (state.attach_open && state.last && !state.last_open) {
      state.last->hooks.emplace_back(*hook);  // post-transaction
    }
  }
}

void onMessage(LogParseState& state, HistoryMessage message) {
  if (state.pending_origin) {
    state.pending_messages.push_back(std::move(message));
  } else if (state.attach_open && state.last) {
    state.last->messages.push_back(std::move(message));
  }
}

void parseLine(std::string_view raw, LogParseState& state, std::vector<HistoryEntry>& finished) {
  const auto line = splitLine(raw);
  if (!line) {
    return;
  }
  if (line->tag == Tag::Alpm || line->tag == Tag::Pacman) {
    if (auto message = messageOf(line->text)) {
      onMessage(state, std::move(*message));
      return;
    }
  }
  switch (line->tag) {
    case Tag::Alpm:
      onAlpmText(state, finished, *line);
      break;
    case Tag::Pacman:
      if (auto origin = pacmanOriginOf(line->text)) {
        onRunning(state, std::move(*origin));
      }
      break;
    case Tag::Holonight:
      if (auto origin = holonightOriginOf(line->text)) {
        onRunning(state, std::move(*origin));
      }
      break;
    case Tag::Other:
      break;
  }
}

}  // namespace

void parseLines(std::string_view lines, LogParseState& state, std::vector<HistoryEntry>& finished) {
  std::size_t begin = 0;
  while (begin < lines.size()) {
    const std::size_t end = lines.find('\n', begin);
    if (end == std::string_view::npos) {
      return;  // a trailing partial line is not part of the input contract (REQ-F-020)
    }
    if (end - begin <= kMaxLogLineLength) {
      parseLine(lines.substr(begin, end - begin), state, finished);
    }
    begin = end + 1;
  }
}

}  // namespace holonight_packages_backends
