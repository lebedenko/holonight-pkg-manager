#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace holonight_packages_backends {

// The subset of pacman.conf options that the update check and the package helper need. Every other key and section is
// ignored.
struct PacmanConfig {
  // [options] IgnorePkg, accumulated over all lines, whitespace-split. Entries may be fnmatch(3) globs.
  std::vector<std::string> ignore_pkgs;
  // [options] IgnoreGroup, same rules as ignore_pkgs.
  std::vector<std::string> ignore_groups;
  // [options] HoldPkg: packages that the helper refuses to remove or change (REQ-F-035). Not globbed.
  std::vector<std::string> hold_pkgs;
  // [options] NoUpgrade / NoExtract, whitespace-split.
  std::vector<std::string> no_upgrade;
  std::vector<std::string> no_extract;
  // [options] CacheDir and HookDir, in file order; empty means "use the built-in default".
  std::vector<std::filesystem::path> cache_dirs;
  std::vector<std::filesystem::path> hook_dirs;
  // [options] LogFile; empty means "use the built-in default".
  std::filesystem::path log_file;
  // [options] DownloadUser; empty means no sandbox user.
  std::string download_user;
  // [options] DisableSandbox, DisableSandboxFilesystem, DisableSandboxSyscalls (the first implies both others).
  bool disable_sandbox_filesystem = false;
  bool disable_sandbox_syscalls = false;
  // [options] CheckSpace (a flag, no value).
  bool check_space = false;
  // [options] ParallelDownloads; 0 means "use the built-in default".
  int parallel_downloads = 0;

  bool operator==(const PacmanConfig&) const = default;
};

// Parses `[section]` headers, `key = value` lines and `#` comments (whole-line and trailing). There is no Include
// support. A missing or unreadable file is an error, never an empty config: an empty ignore list would silently
// inflate the update count.
[[nodiscard]] std::expected<PacmanConfig, std::string> parsePacmanConfig(const std::filesystem::path& path);

}  // namespace holonight_packages_backends
