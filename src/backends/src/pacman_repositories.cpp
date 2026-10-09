#include "pacman_repositories.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
#include <glob.h>
#include <span>
#include <sstream>
#include <sys/utsname.h>
#include <system_error>

namespace holonight_packages_backends {

namespace {

constexpr std::string_view kWhitespace = " \t\r";
constexpr int kMaxIncludeDepth = 8;

std::string_view trim(std::string_view text) {
  const auto first = text.find_first_not_of(kWhitespace);
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(kWhitespace);
  return text.substr(first, last - first + 1);
}

std::vector<std::string> words(std::string_view value) {
  std::vector<std::string> result;
  std::istringstream stream{std::string(value)};
  std::string word;
  while (stream >> word) {
    result.push_back(word);
  }
  return result;
}

// The bits a SigLevel setting decided, and their values; bits outside the mask come from the enclosing level.
struct SigSetting {
  std::uint32_t mask = 0;
  std::uint32_t value = 0;
};

void applyToken(SigSetting& setting, std::uint32_t signature, std::uint32_t optional, std::uint32_t marginal,
                std::uint32_t unknown, std::string_view action) {
  const auto set = [&setting](std::uint32_t bits, bool enable) {
    setting.mask |= bits;
    setting.value = enable ? (setting.value | bits) : (setting.value & ~bits);
  };
  if (action == "Never") {
    set(signature | optional, false);
  } else if (action == "Optional") {
    set(signature, true);
    set(optional, true);
  } else if (action == "Required") {
    set(signature, true);
    set(optional, false);
  } else if (action == "TrustedOnly") {
    set(marginal | unknown, false);
  } else {  // TrustAll
    set(marginal | unknown, true);
  }
}

std::expected<SigSetting, std::string> parseSigLevel(std::string_view value) {
  SigSetting setting{};
  const std::vector<std::string> tokens = words(value);
  if (tokens.empty()) {
    return std::unexpected("SigLevel has no value");
  }
  for (const std::string& token : tokens) {
    std::string_view action = token;
    bool package = true;
    bool database = true;
    if (action.starts_with("Package")) {
      database = false;
      action.remove_prefix(7);
    } else if (action.starts_with("Database")) {
      package = false;
      action.remove_prefix(8);
    }
    static constexpr std::array<std::string_view, 5> kActions{
        "Never", "Optional", "Required", "TrustedOnly", "TrustAll",
    };
    if (std::ranges::find(kActions, action) == kActions.end()) {
      return std::unexpected("Unrecognised SigLevel value '" + token + "'");
    }
    if (package) {
      applyToken(setting, kSigPackage, kSigPackageOptional, kSigPackageMarginalOk, kSigPackageUnknownOk, action);
    }
    if (database) {
      applyToken(setting, kSigDatabase, kSigDatabaseOptional, kSigDatabaseMarginalOk, kSigDatabaseUnknownOk, action);
    }
  }
  return setting;
}

std::uint32_t merge(const SigSetting& setting, std::uint32_t base) {
  return (setting.value & setting.mask) | (base & ~setting.mask);
}

std::vector<std::filesystem::path> expandGlob(const std::filesystem::path& pattern) {
  std::vector<std::filesystem::path> matches;
  glob_t result{};
  if (::glob(pattern.c_str(), GLOB_NOSORT, nullptr, &result) == 0) {
    const std::span<char*> paths(result.gl_pathv, result.gl_pathc);
    for (const char* path : paths) {
      matches.emplace_back(path);
    }
  }
  ::globfree(&result);
  return matches;
}

struct RawRepository {
  std::string name;
  std::vector<std::string> servers;
  SigSetting sig;
};

struct Parser {
  std::string machine;
  std::string gpgdir;
  std::vector<std::string> architectures;
  SigSetting global_sig;
  std::vector<RawRepository> repositories;

  std::expected<void, std::string> readFile(const std::filesystem::path& path, int depth, std::string& section);
  std::expected<void, std::string> beginSection(std::string_view line, std::string& section,
                                                const std::filesystem::path& path);
  std::expected<void, std::string> handleOption(std::string_view key, std::string_view value);
  std::expected<void, std::string> handleInclude(std::string_view value, const std::filesystem::path& file, int depth,
                                                 std::string& section);
  std::expected<void, std::string> handle(std::string& section, std::string_view key, std::string_view value,
                                          const std::filesystem::path& file, int depth);
};

// The text of a configuration line without its comment and surrounding blanks.
std::string_view significant(std::string_view line) {
  if (const auto comment = line.find('#'); comment != std::string_view::npos) {
    line = line.substr(0, comment);
  }
  return trim(line);
}

std::expected<void, std::string> Parser::beginSection(std::string_view line, std::string& section,
                                                      const std::filesystem::path& path) {
  (void)path;
  section = std::string(trim(line.substr(1, line.size() - 2)));
  if (section == "options") {
    return {};
  }
  if (std::ranges::any_of(repositories,
                          [&section](const RawRepository& existing) { return existing.name == section; })) {
    return std::unexpected("Repository '" + section + "' is defined twice");
  }
  repositories.push_back(RawRepository{.name = section, .servers = {}, .sig = {}});
  return {};
}

std::expected<void, std::string> Parser::readFile(const std::filesystem::path& path, int depth, std::string& section) {
  if (depth > kMaxIncludeDepth) {
    return std::unexpected("Include nesting is too deep at " + path.string());
  }
  std::error_code type_error;
  if (std::filesystem::is_directory(path, type_error)) {
    return std::unexpected("Cannot read " + path.string() + ": is a directory");
  }
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return std::unexpected("Cannot read " + path.string() + ": " + std::generic_category().message(errno));
  }
  std::string raw_line;
  while (std::getline(stream, raw_line)) {
    const std::string_view line = significant(raw_line);
    if (line.empty()) {
      continue;
    }
    std::expected<void, std::string> handled;
    if (line.front() == '[' && line.back() == ']') {
      handled = beginSection(line, section, path);
    } else if (const auto equals = line.find('='); equals != std::string_view::npos) {
      handled = handle(section, trim(line.substr(0, equals)), trim(line.substr(equals + 1)), path, depth);
    }
    if (!handled) {
      return handled;
    }
  }
  if (stream.bad()) {
    return std::unexpected("Cannot read " + path.string() + ": read failed");
  }
  return {};
}

std::expected<void, std::string> Parser::handleOption(std::string_view key, std::string_view value) {
  if (key == "GPGDir") {
    gpgdir = std::string(value);
  } else if (key == "Architecture") {
    for (std::string& word : words(value)) {
      architectures.push_back(word == "auto" ? machine : std::move(word));
    }
  } else if (key == "SigLevel") {
    auto level = parseSigLevel(value);
    if (!level) {
      return std::unexpected(level.error());
    }
    global_sig.value = merge(*level, global_sig.value);
    global_sig.mask |= level->mask;
  }
  return {};
}

std::expected<void, std::string> Parser::handleInclude(std::string_view value, const std::filesystem::path& file,
                                                       int depth, std::string& section) {
  std::filesystem::path pattern(value);
  if (pattern.is_relative()) {
    pattern = file.parent_path() / pattern;
  }
  std::vector<std::filesystem::path> included = expandGlob(pattern);
  if (included.empty()) {
    return std::unexpected("Cannot read Include " + pattern.string() + ": no such file");
  }
  std::ranges::sort(included);
  for (const auto& path : included) {
    if (auto read = readFile(path, depth + 1, section); !read) {
      return read;
    }
  }
  return {};
}

std::expected<void, std::string> Parser::handle(std::string& section, std::string_view key, std::string_view value,
                                                const std::filesystem::path& file, int depth) {
  if (key == "Include") {
    return handleInclude(value, file, depth, section);
  }
  if (section == "options") {
    return handleOption(key, value);
  }
  if (section.empty()) {
    return {};
  }
  if (key == "Server") {
    repositories.back().servers.emplace_back(value);  // for a mirrorlist, the including repository is the last one
  } else if (key == "SigLevel") {
    auto level = parseSigLevel(value);
    if (!level) {
      return std::unexpected(level.error());
    }
    auto& sig = repositories.back().sig;
    sig.value = merge(*level, sig.value);
    sig.mask |= level->mask;
  }
  return {};
}

std::string substitute(std::string url, std::string_view repo, std::string_view arch) {
  const auto replaceAll = [&url](std::string_view token, std::string_view with) {
    for (std::size_t at = url.find(token); at != std::string::npos; at = url.find(token, at + with.size())) {
      url.replace(at, token.size(), with);
    }
  };
  replaceAll("$repo", repo);
  replaceAll("$arch", arch);
  return url;
}

}  // namespace

std::string machineArchitecture() {
  utsname info{};
  if (::uname(&info) != 0) {
    return {};
  }
  return info.machine;  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
}

std::expected<PacmanRepositories, std::string> parsePacmanRepositories(const std::filesystem::path& path,
                                                                       std::string_view machine_architecture) {
  Parser parser{};
  std::string section;
  parser.machine = std::string(machine_architecture);
  if (auto read = parser.readFile(path, 0, section); !read) {
    return std::unexpected(read.error());
  }

  PacmanRepositories result;
  if (!parser.gpgdir.empty()) {
    result.gpg_directory = parser.gpgdir;
  }
  result.architectures = parser.architectures.empty() ? std::vector<std::string>{parser.machine} : parser.architectures;
  result.default_sig_level = merge(parser.global_sig, kPacmanDefaultSigLevel);
  const std::string& arch = result.architectures.front();
  for (const RawRepository& raw : parser.repositories) {
    if (raw.servers.empty()) {
      return std::unexpected("Repository '" + raw.name + "' has no Server");
    }
    PacmanRepository repository{.name = raw.name, .servers = {}, .sig_level = merge(raw.sig, result.default_sig_level)};
    for (const std::string& server : raw.servers) {
      repository.servers.push_back(substitute(server, raw.name, arch));
    }
    result.repositories.push_back(std::move(repository));
  }
  return result;
}

}  // namespace holonight_packages_backends
