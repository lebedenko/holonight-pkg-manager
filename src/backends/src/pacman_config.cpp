#include "pacman_config.h"

#include <cerrno>
#include <fstream>
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

}  // namespace

std::expected<PacmanConfig, std::string> parsePacmanConfig(const std::filesystem::path& path) {
  std::error_code type_error;
  if (std::filesystem::is_directory(path, type_error)) {
    return std::unexpected(readError(path, "is a directory"));
  }

  std::ifstream stream(path);
  if (!stream.is_open()) {
    return std::unexpected(readError(path, std::generic_category().message(errno)));
  }

  PacmanConfig config;
  bool in_options = false;
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
    if (!in_options) {
      continue;
    }
    const auto equals = line.find('=');
    if (equals == std::string_view::npos) {
      applyFlag(line, config);
      continue;
    }
    applyOption(trim(line.substr(0, equals)), line.substr(equals + 1), config);
  }
  if (stream.bad()) {
    return std::unexpected(readError(path, "read failed"));
  }
  return config;
}

}  // namespace holonight_packages_backends
