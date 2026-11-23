#include "pacman_config.h"

#include <cerrno>
#include <fstream>
#include <glob.h>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace holonight_packages_backends {

namespace {

constexpr std::string_view kWhitespace = " \t\r";

std::string_view trim(std::string_view text) {
  const auto first = text.find_first_not_of(kWhitespace);
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(kWhitespace);
  return text.substr(first, last - first + 1);
}

void appendWords(std::string_view value, std::vector<std::string>& target) {
  std::istringstream words{std::string(value)};
  std::string word;
  while (words >> word) {
    target.push_back(word);
  }
}

void appendPaths(std::string_view value, std::vector<std::filesystem::path>& target) {
  std::vector<std::string> paths;
  appendWords(value, paths);
  for (std::string& path : paths) {
    target.emplace_back(std::move(path));
  }
}

int parseCount(std::string_view value) {
  const std::string text(trim(value));
  std::size_t used = 0;
  try {
    const int count = std::stoi(text, &used);
    return used == text.size() && count > 0 ? count : 0;
  } catch (const std::logic_error&) {
    return 0;  // not a number, or out of range
  }
}

void applyFlag(std::string_view key, PacmanConfig& config) {
  if (key == "CheckSpace") {
    config.check_space = true;
  } else if (key == "DisableSandbox") {
    config.disable_sandbox_filesystem = true;
    config.disable_sandbox_syscalls = true;
  } else if (key == "DisableSandboxFilesystem") {
    config.disable_sandbox_filesystem = true;
  } else if (key == "DisableSandboxSyscalls") {
    config.disable_sandbox_syscalls = true;
  }
}

// `key = value` lines of [options] that the helper and the update check care about.
void applyOption(std::string_view key, std::string_view value, PacmanConfig& config) {
  if (key == "IgnorePkg") {
    appendWords(value, config.ignore_pkgs);
  } else if (key == "IgnoreGroup") {
    appendWords(value, config.ignore_groups);
  } else if (key == "HoldPkg") {
    appendWords(value, config.hold_pkgs);
  } else if (key == "NoUpgrade") {
    appendWords(value, config.no_upgrade);
  } else if (key == "NoExtract") {
    appendWords(value, config.no_extract);
  } else if (key == "CacheDir") {
    appendPaths(value, config.cache_dirs);
  } else if (key == "HookDir") {
    appendPaths(value, config.hook_dirs);
  } else if (key == "LogFile") {
    config.log_file = std::string(trim(value));
  } else if (key == "DownloadUser") {
    config.download_user = std::string(trim(value));
  } else if (key == "ParallelDownloads") {
    config.parallel_downloads = parseCount(value);
  }
}

std::string readError(const std::filesystem::path& path, const std::string& reason) {
  return "Cannot read " + path.string() + ": " + reason;
}

template <typename OnInclude>
std::expected<void, std::string> scanIncludes(const std::filesystem::path& pattern, OnInclude on_include) {
  // glob's default order is pacman's order. Relative patterns use the process working directory.
  glob_t matches{};
  const int status = ::glob(pattern.c_str(), GLOB_ERR, nullptr, &matches);
  std::expected<void, std::string> result;
  if (status == 0) {
    for (const char* match : std::span(matches.gl_pathv, matches.gl_pathc)) {
      result = on_include(match);
      if (!result) {
        break;
      }
    }
  } else {
    result = std::unexpected(readError(pattern, "cannot expand Include pattern"));
  }
  ::globfree(&matches);
  return result;
}

// Only the History lookup follows includes. The helper scans the top-level options as before.
template <typename OnFlag, typename OnOption>
std::expected<void, std::string> scanOptions(const std::filesystem::path& path, const OnFlag& on_flag,
                                             const OnOption& on_option, bool& in_options, bool follow_includes,
                                             unsigned depth = 0) {
  constexpr unsigned kMaxIncludeDepth = 10;
  if (depth > kMaxIncludeDepth) {
    return std::unexpected(readError(path, "maximum Include depth exceeded"));
  }
  std::error_code type_error;
  if (std::filesystem::is_directory(path, type_error)) {
    return std::unexpected(readError(path, "is a directory"));
  }

  std::ifstream stream(path);
  if (!stream.is_open()) {
    return std::unexpected(readError(path, std::generic_category().message(errno)));
  }

  std::string raw_line;
  while (std::getline(stream, raw_line)) {
    std::string_view line = raw_line;
    if (const auto comment = line.find('#'); comment != std::string_view::npos) {
      line = line.substr(0, comment);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      in_options = trim(line.substr(1, line.size() - 2)) == "options";
      continue;
    }
    const auto equals = line.find('=');
    if (equals == std::string_view::npos) {
      if (in_options) {
        on_flag(line);
      }
      continue;
    }
    const auto key = trim(line.substr(0, equals));
    const auto value = line.substr(equals + 1);
    if (follow_includes && key == "Include") {
      const auto included = scanIncludes(std::filesystem::path(trim(value)), [&](const std::filesystem::path& include) {
        return scanOptions(include, on_flag, on_option, in_options, true, depth + 1);
      });
      if (!included) {
        return included;
      }
    } else if (in_options) {
      on_option(key, value);
    }
  }
  if (stream.bad()) {
    return std::unexpected(readError(path, "read failed"));
  }
  return {};
}

}  // namespace

std::expected<PacmanConfig, std::string> parsePacmanConfig(const std::filesystem::path& path) {
  PacmanConfig config;
  bool in_options = false;
  const auto scanned = scanOptions(
      path, [&config](std::string_view key) { applyFlag(key, config); },
      [&config](std::string_view key, std::string_view value) { applyOption(key, value, config); }, in_options, false);
  if (!scanned) {
    return std::unexpected(scanned.error());
  }
  return config;
}

std::expected<PacmanLogLocations, std::string> parsePacmanLogLocations(const std::filesystem::path& path) {
  PacmanLogLocations locations;
  bool in_options = false;
  bool have_log_file = false;
  bool have_db_path = false;
  const auto scanned = scanOptions(
      path, [](std::string_view /*key*/) {},
      [&](std::string_view key, std::string_view value) {
        if (key == "LogFile" && !have_log_file) {
          have_log_file = true;
          locations.log_file = std::string(trim(value));
        } else if (key == "DBPath" && !have_db_path) {
          have_db_path = true;
          locations.db_path = std::string(trim(value));
        }
      },
      in_options, true);
  if (!scanned) {
    return std::unexpected(scanned.error());
  }
  return locations;
}

}  // namespace holonight_packages_backends
