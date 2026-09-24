#include "pacman_config.h"

#include <cerrno>
#include <fstream>
#include <sstream>
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
      continue;
    }
    const std::string_view key = trim(line.substr(0, equals));
    const std::string_view value = line.substr(equals + 1);
    if (key == "IgnorePkg") {
      appendWords(value, config.ignorePkgs);
    } else if (key == "IgnoreGroup") {
      appendWords(value, config.ignoreGroups);
    }
  }
  if (stream.bad()) {
    return std::unexpected(readError(path, "read failed"));
  }
  return config;
}

}  // namespace holonight_packages_backends
