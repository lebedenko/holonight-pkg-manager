#pragma once

#include "holonight_packages_domain/transaction_plan.h"
#include "holonight_packages_transaction/log_sink.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace holonight_packages_transaction {

// What a plan does, as far as hook triggers care.
struct HookInstallFact {
  std::string name;
  bool upgrade = false;  // an older version is already installed
};
struct HookRemovalFact {
  std::string name;
  std::vector<std::string> files;  // paths relative to the root, from the local database
};

// SI-8: which hooks libalpm will probably run for a plan. Reads *.hook files from `hookDirs` in order (a file name in a
// later directory overrides the same name in an earlier one), runs hooks sorted by file name ignoring the suffix,
// PreTransaction before PostTransaction. Package triggers are evaluated against the plan; path triggers are evaluated
// against the file lists of removed packages. A path trigger for an install or upgrade cannot be evaluated before the
// download, so such a hook is listed with `conditional = true`. Nothing is executed. This is a preview, not the
// authority: libalpm decides what runs (REQ-F-062).
[[nodiscard]] std::vector<holonight_packages_domain::HookPreview> previewHooks(
    std::span<const std::filesystem::path> hookDirs, std::span<const HookInstallFact> installs,
    std::span<const HookRemovalFact> removals, const LogSink& journal);

}  // namespace holonight_packages_transaction
