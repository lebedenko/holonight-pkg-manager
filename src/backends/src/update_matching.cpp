#include "update_matching.h"

#include <algorithm>
#include <fnmatch.h>
#include <span>
#include <string>

namespace holonight_packages_backends {

namespace {

bool matchesAny(std::span<const std::string> patterns, const std::string& text) {
  return std::ranges::any_of(
      patterns, [&text](const std::string& pattern) { return fnmatch(pattern.c_str(), text.c_str(), 0) == 0; });
}

}  // namespace

bool isIgnored(const std::string& name, std::span<const std::string> groups,
               std::span<const std::string> ignore_pkg_patterns, std::span<const std::string> ignore_group_patterns) {
  if (matchesAny(ignore_pkg_patterns, name)) {
    return true;
  }
  return std::ranges::any_of(groups,
                             [&](const std::string& group) { return matchesAny(ignore_group_patterns, group); });
}

}  // namespace holonight_packages_backends
