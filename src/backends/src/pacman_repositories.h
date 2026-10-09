#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace holonight_packages_backends {

// Signature levels use libalpm's alpm_siglevel_t bit values, held as plain integers so this header needs no alpm.h.
inline constexpr std::uint32_t kSigPackage = 1U << 0;
inline constexpr std::uint32_t kSigPackageOptional = 1U << 1;
inline constexpr std::uint32_t kSigPackageMarginalOk = 1U << 2;
inline constexpr std::uint32_t kSigPackageUnknownOk = 1U << 3;
inline constexpr std::uint32_t kSigDatabase = 1U << 10;
inline constexpr std::uint32_t kSigDatabaseOptional = 1U << 11;
inline constexpr std::uint32_t kSigDatabaseMarginalOk = 1U << 12;
inline constexpr std::uint32_t kSigDatabaseUnknownOk = 1U << 13;

// pacman 7.1's built-in level when pacman.conf sets no SigLevel: Required + TrustedOnly for packages and databases
// (spike S-3: `pacman-conf SigLevel` prints PackageRequired PackageTrustedOnly DatabaseRequired DatabaseTrustedOnly).
inline constexpr std::uint32_t kPacmanDefaultSigLevel = kSigPackage | kSigDatabase;

inline constexpr std::string_view kDefaultGpgDirectory = "/etc/pacman.d/gnupg";

struct PacmanRepository {
  std::string name;
  // After $repo and $arch substitution, in file order (Include mirrorlists expanded in place).
  std::vector<std::string> servers;
  // Effective level: the repository's own SigLevel merged over the global one, merged over the built-in default.
  std::uint32_t sig_level = kPacmanDefaultSigLevel;

  bool operator==(const PacmanRepository&) const = default;
};

struct PacmanRepositories {
  // Configuration order is the libalpm registration order.
  std::vector<PacmanRepository> repositories;
  std::filesystem::path gpg_directory{kDefaultGpgDirectory};
  // Resolved architectures ("auto" replaced by the machine architecture).
  std::vector<std::string> architectures;
  // The global level, passed to libalpm as the handle default.
  std::uint32_t default_sig_level = kPacmanDefaultSigLevel;

  bool operator==(const PacmanRepositories&) const = default;
};

// Parses `[repo]` sections (Server, Include, SigLevel) and [options] GPGDir, Architecture and SigLevel. Fails closed:
// an unreadable file or Include, a repeated repository name, a repository without servers or an unparseable
// SigLevel token is an error, never a weaker level. `machine_architecture` resolves "auto" (uname -m).
[[nodiscard]] std::expected<PacmanRepositories, std::string> parsePacmanRepositories(
    const std::filesystem::path& path, std::string_view machine_architecture);

// The machine architecture as reported by uname(2); empty if unavailable.
[[nodiscard]] std::string machineArchitecture();

}  // namespace holonight_packages_backends
