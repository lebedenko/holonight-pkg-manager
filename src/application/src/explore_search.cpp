#include "holonight_packages_application/explore_search.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unicode/stringpiece.h>
#include <unicode/unistr.h>
#include <utility>
#include <vector>

namespace holonight_packages_application {

namespace {

std::string foldCase(std::string_view text) {
  std::string folded;
  icu::UnicodeString::fromUTF8(icu::StringPiece(text.data(), static_cast<int32_t>(text.size())))
      .foldCase()
      .toUTF8String(folded);
  return folded;
}

constexpr bool isAsciiSpace(char character) { return character == ' ' || (character >= '\t' && character <= '\r'); }

std::string_view trimAscii(std::string_view text) {
  while (!text.empty() && isAsciiSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && isAsciiSpace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

struct Hit {
  std::uint8_t rank;
  std::uint32_t row;
};

}  // namespace

ExploreIndex::ExploreIndex(std::vector<holonight_packages_domain::SyncPackage> packages)
    : packages_(std::move(packages)) {
  folded_names_.reserve(packages_.size());
  folded_descriptions_.reserve(packages_.size());
  for (const auto& package : packages_) {
    folded_names_.push_back(foldCase(package.name));
    folded_descriptions_.push_back(foldCase(package.description));
  }
}

SearchResult searchPackages(const ExploreIndex& index, std::string_view query, std::size_t limit) {
  const std::string needle = foldCase(trimAscii(query));
  SearchResult result;
  if (needle.empty()) {
    return result;
  }

  std::vector<Hit> hits;
  for (std::size_t i = 0; i < index.folded_names_.size(); ++i) {
    const std::string& name = index.folded_names_[i];
    std::uint8_t rank = 0;
    if (name == needle) {
      rank = 0;
    } else if (name.starts_with(needle)) {
      rank = 1;
    } else if (name.contains(needle)) {
      rank = 2;
    } else if (index.folded_descriptions_[i].contains(needle)) {
      rank = 3;
    } else {
      continue;
    }
    hits.push_back(Hit{.rank = rank, .row = static_cast<std::uint32_t>(i)});
  }

  result.totalMatches = hits.size();
  const auto shown = static_cast<std::ptrdiff_t>(std::min(limit, hits.size()));
  const auto before = [&index](const Hit& lhs, const Hit& rhs) {
    if (lhs.rank != rhs.rank) {
      return lhs.rank < rhs.rank;
    }
    if (const int by_name = index.folded_names_[lhs.row].compare(index.folded_names_[rhs.row]); by_name != 0) {
      return by_name < 0;
    }
    return lhs.row < rhs.row;
  };
  std::partial_sort(hits.begin(), hits.begin() + shown, hits.end(), before);

  result.rows.reserve(static_cast<std::size_t>(shown));
  for (std::ptrdiff_t i = 0; i < shown; ++i) {
    result.rows.push_back(hits[static_cast<std::size_t>(i)].row);
  }
  return result;
}

}  // namespace holonight_packages_application
