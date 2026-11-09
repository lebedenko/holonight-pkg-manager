#include "hook_preview.h"

#include <algorithm>
#include <cstdint>
#include <fnmatch.h>
#include <fstream>
#include <map>
#include <set>
#include <span>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::HookPreview;

struct Trigger {
  std::set<std::string> operations;  // Install, Upgrade, Remove
  bool path_type = false;
  std::vector<std::string> targets;
};

struct Hook {
  std::string file_name;
  bool pre_transaction = false;
  bool has_when = false;
  bool has_exec = false;
  std::vector<Trigger> triggers;
};

std::string_view trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

enum class Section : std::uint8_t { None, Trigger, Action, Other };

Section sectionOf(std::string_view name) {
  if (name == "Trigger") {
    return Section::Trigger;
  }
  return name == "Action" ? Section::Action : Section::Other;
}

void applyTriggerKey(Trigger& trigger, std::string_view key, std::string_view value) {
  if (key == "Operation") {
    trigger.operations.insert(std::string(value));
  } else if (key == "Type") {
    trigger.path_type = value == "Path" || value == "File";
  } else if (key == "Target") {
    trigger.targets.emplace_back(value);
  }
}

void applyActionKey(Hook& hook, std::string_view key, std::string_view value) {
  if (key == "When") {
    hook.has_when = value == "PreTransaction" || value == "PostTransaction";
    hook.pre_transaction = value == "PreTransaction";
  } else if (key == "Exec") {
    hook.has_exec = !value.empty();
  }
}

std::optional<Hook> parseHook(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return std::nullopt;
  }
  Hook hook;
  hook.file_name = path.filename().string();
  Section section = Section::None;
  for (std::string raw; std::getline(stream, raw);) {
    const std::string_view line = trim(raw);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      section = sectionOf(trim(line.substr(1, line.size() - 2)));
      if (section == Section::Trigger) {
        hook.triggers.emplace_back();
      }
      continue;
    }
    const auto equals = line.find('=');
    const std::string_view key = trim(line.substr(0, equals));
    std::string_view value;
    if (equals != std::string_view::npos) {
      value = trim(line.substr(equals + 1));
    }
    if (section == Section::Trigger && !hook.triggers.empty()) {
      applyTriggerKey(hook.triggers.back(), key, value);
    } else if (section == Section::Action) {
      applyActionKey(hook, key, value);
    }
  }
  if (!hook.has_when || !hook.has_exec || hook.triggers.empty()) {
    return std::nullopt;
  }
  return hook;
}

// "Subsequent matches override previous ones; a leading '!' inverts a match."
bool targetsMatch(const std::vector<std::string>& targets, const std::string& subject) {
  bool matched = false;
  for (const std::string& target : targets) {
    const bool inverted = target.starts_with('!');
    const std::string pattern = inverted ? target.substr(1) : target;
    if (fnmatch(pattern.c_str(), subject.c_str(), 0) == 0) {
      matched = !inverted;
    }
  }
  return matched;
}

enum class Match : std::uint8_t { No, Conditional, Yes };

bool packageTriggerMatches(const Trigger& trigger, std::span<const HookInstallFact> installs,
                           std::span<const HookRemovalFact> removals) {
  const bool on_install = trigger.operations.contains("Install");
  const bool on_upgrade = trigger.operations.contains("Upgrade");
  const bool on_remove = trigger.operations.contains("Remove");
  const bool installed = std::ranges::any_of(installs, [&](const HookInstallFact& fact) {
    return (fact.upgrade ? on_upgrade : on_install) && targetsMatch(trigger.targets, fact.name);
  });
  const bool removed = std::ranges::any_of(
      removals, [&](const HookRemovalFact& fact) { return on_remove && targetsMatch(trigger.targets, fact.name); });
  return installed || removed;
}

bool pathTriggerMatchesRemoval(const Trigger& trigger, std::span<const HookRemovalFact> removals) {
  if (!trigger.operations.contains("Remove")) {
    return false;
  }
  return std::ranges::any_of(removals, [&](const HookRemovalFact& fact) {
    return std::ranges::any_of(fact.files,
                               [&](const std::string& file) { return targetsMatch(trigger.targets, file); });
  });
}

// The file list of a package that is still to be downloaded is unknown, so no path pattern can be excluded.
bool pathTriggerMayMatchInstall(const Trigger& trigger, std::span<const HookInstallFact> installs) {
  const bool on_install = trigger.operations.contains("Install");
  const bool on_upgrade = trigger.operations.contains("Upgrade");
  return std::ranges::any_of(installs,
                             [&](const HookInstallFact& fact) { return fact.upgrade ? on_upgrade : on_install; });
}

Match evaluate(const Hook& hook, std::span<const HookInstallFact> installs, std::span<const HookRemovalFact> removals) {
  Match result = Match::No;
  for (const Trigger& trigger : hook.triggers) {
    if (!trigger.path_type) {
      if (packageTriggerMatches(trigger, installs, removals)) {
        return Match::Yes;
      }
    } else if (pathTriggerMatchesRemoval(trigger, removals)) {
      return Match::Yes;
    } else if (pathTriggerMayMatchInstall(trigger, installs)) {
      result = Match::Conditional;
    }
  }
  return result;
}

std::string stem(const std::string& file_name) {
  constexpr std::string_view kSuffix = ".hook";
  return file_name.size() > kSuffix.size() ? file_name.substr(0, file_name.size() - kSuffix.size()) : file_name;
}

}  // namespace

std::vector<HookPreview> previewHooks(std::span<const std::filesystem::path> hookDirs,
                                      std::span<const HookInstallFact> installs,
                                      std::span<const HookRemovalFact> removals, const LogSink& journal) {
  // File name -> path; a later directory overrides an earlier one.
  std::map<std::string, std::filesystem::path> files;
  for (const std::filesystem::path& directory : hookDirs) {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
      if (entry.path().extension() == ".hook" && entry.is_regular_file(error)) {
        files[entry.path().filename().string()] = entry.path();
      }
    }
  }
  std::vector<Hook> hooks;
  for (const auto& [name, path] : files) {
    if (auto hook = parseHook(path)) {
      hooks.push_back(std::move(*hook));
    } else if (journal) {
      journal(LogLevel::Warning, "skipping unreadable or incomplete hook " + name);
    }
  }
  std::ranges::stable_sort(hooks, [](const Hook& left, const Hook& right) {
    if (left.pre_transaction != right.pre_transaction) {
      return left.pre_transaction;
    }
    return stem(left.file_name) < stem(right.file_name);
  });
  std::vector<HookPreview> preview;
  for (const Hook& hook : hooks) {
    switch (evaluate(hook, installs, removals)) {
      case Match::Yes:
        preview.push_back(HookPreview{.name = hook.file_name, .conditional = false});
        break;
      case Match::Conditional:
        preview.push_back(HookPreview{.name = hook.file_name, .conditional = true});
        break;
      case Match::No:
        break;
    }
  }
  return preview;
}

}  // namespace holonight_packages_transaction
