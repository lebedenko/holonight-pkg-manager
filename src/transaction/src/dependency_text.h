#pragma once

#include <alpm.h>
#include <cstdlib>
#include <memory>
#include <string>

namespace holonight_packages_transaction {

// libalpm returns a malloc'd string from alpm_dep_compute_string; this owns it for the duration of the call.
[[nodiscard]] inline std::string dependencyText(const alpm_depend_t* dependency) {
  const std::unique_ptr<char, decltype(&std::free)> text(alpm_dep_compute_string(dependency), &std::free);
  return text ? std::string(text.get()) : std::string();
}

}  // namespace holonight_packages_transaction
