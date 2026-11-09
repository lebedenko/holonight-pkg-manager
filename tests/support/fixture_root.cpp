#include "fixture_root.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace holonight_transaction_testing {
namespace {

namespace fs = std::filesystem;

[[noreturn]] void refuse(const std::string& reason) {
  std::cerr << "FixtureRoot: " << reason << '\n';
  std::abort();
}

bool isInside(const fs::path& candidate, const fs::path& base) {
  const fs::path relative = candidate.lexically_relative(base);
  return !relative.empty() && *relative.begin() != "..";
}

void writeText(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::trunc | std::ios::binary);
  out << text;
}

}  // namespace

std::optional<std::string> FixtureRoot::rejectRoot(const fs::path& candidate, const fs::path& tempBase) {
  std::error_code error;
  const fs::path canonical = fs::weakly_canonical(candidate, error);
  if (error) {
    return "cannot resolve " + candidate.string();
  }
  const fs::path base = fs::weakly_canonical(tempBase, error);
  if (canonical == canonical.root_path()) {
    return "the world directory must not be the filesystem root";
  }
  if (canonical == fs::path("/var/lib/pacman") || isInside(canonical, "/var/lib/pacman") ||
      canonical == fs::path("/etc/pacman.conf")) {
    return "the world directory must not be a real pacman location";
  }
  if (error || !isInside(canonical, base)) {
    return canonical.string() + " is outside the temporary directory " + tempBase.string();
  }
  return std::nullopt;
}

std::optional<std::string> FixtureRoot::rejectServer(const std::string& url, const fs::path& base) {
  const std::string prefix = "file://";
  if (!url.starts_with(prefix)) {
    return "'" + url + "' is not a file:// URL";
  }
  std::error_code error;
  const fs::path path = fs::weakly_canonical(url.substr(prefix.size()), error);
  const fs::path world = fs::weakly_canonical(base, error);
  if (error || !isInside(path, world)) {
    return "'" + url + "' points outside the fixture world";
  }
  return std::nullopt;
}

FixtureRoot::FixtureRoot() {
  const fs::path tempBase = fs::temp_directory_path();
  std::string pattern = (tempBase / "holonight-fixture-XXXXXX").string();
  if (::mkdtemp(pattern.data()) == nullptr) {
    refuse("cannot create a temporary directory under " + tempBase.string());
  }
  base_ = pattern;
  if (const auto why = rejectRoot(base_, tempBase)) {
    refuse(*why);
  }
  for (const fs::path& directory :
       {installRoot(), localDbPath(), syncDbPath(), cacheDir(), hookDir(), gpgDir(), base_ / "repos"}) {
    fs::create_directories(directory);
  }
  fs::permissions(gpgDir(), fs::perms::owner_all, fs::perm_options::replace);
  writeText(localDbPath() / "ALPM_DB_VERSION", "9\n");
}

FixtureRoot::~FixtureRoot() {
  std::error_code error;
  // Repositories can contain deliberately unreadable files.
  for (const auto& entry :
       fs::recursive_directory_iterator(base_, fs::directory_options::skip_permission_denied, error)) {
    fs::permissions(entry.path(), fs::perms::owner_all, fs::perm_options::add, error);
  }
  fs::remove_all(base_, error);
}

holonight_packages_transaction::TransactionConfig FixtureRoot::config() const {
  return holonight_packages_transaction::TransactionConfig{
      .root = installRoot(),
      .database_path = dbPath(),
      .pacman_conf_path = confPath(),
      .cache_directory = cacheDir(),
  };
}

std::string FixtureRoot::serverUrl(const std::string& repoName) const { return "file://" + repoDir(repoName).string(); }

void FixtureRoot::writeConfText(const std::string& text) const {
  static const std::regex server(R"(^\s*Server\s*=\s*(\S+))", std::regex::multiline);
  for (std::sregex_iterator it(text.begin(), text.end(), server), end; it != end; ++it) {
    if (const auto why = rejectServer((*it)[1].str(), base_)) {
      refuse(*why);
    }
  }
  writeText(confPath(), text);
}

void FixtureRoot::writeConf(const ConfSpec& spec) const {
  std::ostringstream out;
  out << "[options]\nArchitecture = auto\nCacheDir = " << cacheDir().string() << "/\nHookDir = " << hookDir().string()
      << "/\nGPGDir = " << gpgDir().string() << "\nLogFile = " << (base_ / "pacman.log").string()
      << "\nDisableSandbox\n";
  if (spec.globalSigLevel) {
    out << "SigLevel = " << *spec.globalSigLevel << "\n";
  }
  for (const std::string& line : spec.extraOptions) {
    out << line << "\n";
  }
  for (const RepoConf& repo : spec.repos) {
    out << "\n[" << repo.name << "]\n";
    if (repo.sigLevel) {
      out << "SigLevel = " << *repo.sigLevel << "\n";
    }
    for (const std::string& server : repo.servers) {
      out << "Server = " << server << "\n";
    }
  }
  writeConfText(out.str());
}

void FixtureRoot::writeConf(const std::vector<std::string>& repoNames, const std::string& sigLevel) const {
  ConfSpec spec;
  spec.globalSigLevel = sigLevel;
  for (const std::string& name : repoNames) {
    spec.repos.push_back(RepoConf{.name = name, .servers = {serverUrl(name)}, .sigLevel = std::nullopt});
  }
  writeConf(spec);
}

void FixtureRoot::installPackage(const PackageSpec& spec, InstallReason reason) const {
  const fs::path entry = localDbPath() / (spec.name + "-" + spec.version);
  writeText(entry / "desc", localDescEntry(spec, reason));
  writeText(entry / "files", localFilesEntry(spec));
  for (const auto& [path, content] : spec.files) {
    writeText(installRoot() / path, content);
  }
}

std::map<std::string, std::string> FixtureRoot::installedVersions() const {
  std::map<std::string, std::string> versions;
  std::error_code error;
  for (const auto& entry : fs::directory_iterator(localDbPath(), error)) {
    std::ifstream desc(entry.path() / "desc");
    if (!desc.is_open()) {
      continue;
    }
    std::string name;
    std::string version;
    for (std::string line; std::getline(desc, line);) {
      if (line == "%NAME%") {
        std::getline(desc, name);
      } else if (line == "%VERSION%") {
        std::getline(desc, version);
      }
    }
    if (!name.empty()) {
      versions[name] = version;
    }
  }
  return versions;
}

bool FixtureRoot::isInstalled(const std::string& name) const { return installedVersions().contains(name); }

bool FixtureRoot::fileExists(const std::string& pathUnderRoot) const {
  std::error_code error;
  return fs::exists(installRoot() / pathUnderRoot, error);
}

std::string FixtureRoot::readFile(const std::string& pathUnderRoot) const {
  std::ifstream input(installRoot() / pathUnderRoot, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void FixtureRoot::writeHook(const std::string& fileName, const std::string& text) const {
  writeText(hookDir() / fileName, text);
}

}  // namespace holonight_transaction_testing
