#pragma once

#include "holonight_packages_domain/sync_package.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace holonight_packages_application {

inline constexpr std::size_t kMaxSearchResults = 500;

struct SearchResult {
  // Indexes into ExploreIndex::packages(), ranked, at most `limit`.
  std::vector<std::uint32_t> rows;
  // Matches before the cap; drives the "showing first N of M" footer.
  std::size_t totalMatches = 0;
};

// Immutable search index over one snapshot. Built once per load on the worker thread and never mutated afterwards, so
// it is safe to share read-only.
//
// Search runs synchronously on the GUI thread (DESIGN 4.4). Earlier ASCII-folding measurements on 15,287 packages
// found worst-case top-500 searches of 0.5 ms optimised and 1.6 ms in Debug. Recheck after the Unicode folding change;
// if a search exceeds ~8 ms in the shipped build, move it to a worker with a generation counter.
class ExploreIndex {
 public:
  explicit ExploreIndex(std::vector<holonight_packages_domain::SyncPackage> packages);

  [[nodiscard]] const std::vector<holonight_packages_domain::SyncPackage>& packages() const { return packages_; }
  [[nodiscard]] std::size_t size() const { return packages_.size(); }

 private:
  friend SearchResult searchPackages(const ExploreIndex& index, std::string_view query, std::size_t limit);

  std::vector<holonight_packages_domain::SyncPackage> packages_;
  // Unicode case-folded UTF-8 copies, built once.
  std::vector<std::string> folded_names_;
  std::vector<std::string> folded_descriptions_;
};

// Case-insensitive, literal search (no regex, glob or tokenising: "kernel mod" is one substring). The query is trimmed
// of ASCII whitespace first; an empty or whitespace-only query matches nothing, so callers cannot list everything by
// accident.
//
// Rank: 0 name equals query, 1 name starts with it, 2 name contains it, 3 only the description contains it. Order is
// (rank, folded name, original index), so it is deterministic. ICU folds Unicode before UTF-8 comparison.
[[nodiscard]] SearchResult searchPackages(const ExploreIndex& index, std::string_view query,
                                          std::size_t limit = kMaxSearchResults);

}  // namespace holonight_packages_application
