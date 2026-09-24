#pragma once

#include <span>
#include <string>

namespace holonight_packages_backends {

// Mirrors libalpm's alpm_pkg_should_ignore(): the package is ignored when an IgnorePkg pattern matches its name or
// an IgnoreGroup pattern matches one of its groups, using fnmatch(3) globs. Pass the groups of the sync (newer)
// package. Kept free of libalpm so the cache-owned handle is never mutated with ignore options.
[[nodiscard]] bool isIgnored(const std::string& name, std::span<const std::string> groups,
                             std::span<const std::string> ignore_pkg_patterns,
                             std::span<const std::string> ignore_group_patterns);

}  // namespace holonight_packages_backends
