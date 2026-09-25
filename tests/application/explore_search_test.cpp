#include "holonight_packages_application/explore_search.h"

#include <algorithm>
#include <cstddef>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace holonight_packages_application {
namespace {

using holonight_packages_domain::SyncPackage;

SyncPackage package(std::string name, std::string description = "", std::string repository = "extra") {
  return SyncPackage{.name = std::move(name),
                     .version = "1.0-1",
                     .repository = std::move(repository),
                     .description = std::move(description)};
}

std::vector<std::string> names(const ExploreIndex& index, const SearchResult& result) {
  std::vector<std::string> out;
  out.reserve(result.rows.size());
  for (const auto row : result.rows) {
    out.push_back(index.packages()[row].name);
  }
  return out;
}

ExploreIndex vimIndex() {
  return ExploreIndex({package("nano", "Pico editor clone"), package("neovim", "Fork of Vim aiming to improve"),
                       package("gvim", "Vi Improved, with GUI"), package("vimb", "Vim-like browser"),
                       package("vim-runtime", "Runtime files for vim"), package("vim", "Vi Improved")});
}

TEST(ExploreSearch, RanksExactPrefixContainsThenDescription) {
  const auto index = vimIndex();

  const auto result = searchPackages(index, "vim");

  EXPECT_EQ(names(index, result), (std::vector<std::string>{"vim", "vim-runtime", "vimb", "gvim", "neovim"}));
  EXPECT_EQ(result.totalMatches, 5U);
}

TEST(ExploreSearch, AlphabeticalWithinRank) {
  const auto index = vimIndex();

  const auto result = searchPackages(index, "vim");

  const auto found = names(index, result);
  EXPECT_LT(std::ranges::find(found, "vim-runtime"), std::ranges::find(found, "vimb"));
}

TEST(ExploreSearch, MatchesDescriptionOnly) {
  const auto index = vimIndex();

  const auto result = searchPackages(index, "pico");

  EXPECT_EQ(names(index, result), (std::vector<std::string>{"nano"}));
}

TEST(ExploreSearch, IsCaseInsensitive) {
  const auto index = vimIndex();

  EXPECT_EQ(names(index, searchPackages(index, "VIM")), names(index, searchPackages(index, "vim")));
  EXPECT_EQ(names(index, searchPackages(index, "pIcO")), (std::vector<std::string>{"nano"}));
}

TEST(ExploreSearch, FoldsUnicodeInDescriptionsAndQueries) {
  const ExploreIndex index({package("cafe", "Café editor"), package("other", "Straße utilities")});

  EXPECT_EQ(names(index, searchPackages(index, "CAFÉ")), (std::vector<std::string>{"cafe"}));
  EXPECT_EQ(names(index, searchPackages(index, "STRASSE")), (std::vector<std::string>{"other"}));
}

TEST(ExploreSearch, TrimsQueryAndIgnoresEmpty) {
  const auto index = vimIndex();

  EXPECT_EQ(names(index, searchPackages(index, "  vim \t")), names(index, searchPackages(index, "vim")));
  EXPECT_EQ(searchPackages(index, "").totalMatches, 0U);
  EXPECT_TRUE(searchPackages(index, " \t\n").rows.empty());
}

TEST(ExploreSearch, MultiWordQueryIsOneLiteralSubstring) {
  const auto index = ExploreIndex({package("a", "kernel module loader"), package("b", "module for the kernel")});

  const auto result = searchPackages(index, "kernel mod");

  EXPECT_EQ(names(index, result), (std::vector<std::string>{"a"}));
}

TEST(ExploreSearch, SameNameInTwoRepositoriesKeepsOriginalOrder) {
  const auto index = ExploreIndex({package("dup", "", "core"), package("dup", "", "extra")});

  const auto result = searchPackages(index, "dup");

  EXPECT_EQ(result.rows, (std::vector<std::uint32_t>{0, 1}));
}

TEST(ExploreSearch, CapsResultsButCountsAllMatches) {
  std::vector<SyncPackage> packages;
  packages.reserve(600);
  for (int i = 0; i < 600; ++i) {
    packages.push_back(package("pkg" + std::to_string(1000 + i)));
  }
  const ExploreIndex index(std::move(packages));

  const auto result = searchPackages(index, "pkg");

  EXPECT_EQ(result.rows.size(), kMaxSearchResults);
  EXPECT_EQ(result.totalMatches, 600U);
  EXPECT_EQ(index.packages()[result.rows.front()].name, "pkg1000");
}

TEST(ExploreSearch, ExactlyAtTheCapShowsNoCapEffect) {
  std::vector<SyncPackage> packages;
  packages.reserve(kMaxSearchResults);
  for (std::size_t i = 0; i < kMaxSearchResults; ++i) {
    packages.push_back(package("pkg" + std::to_string(1000 + i)));
  }
  const ExploreIndex index(std::move(packages));

  const auto result = searchPackages(index, "pkg");

  EXPECT_EQ(result.rows.size(), kMaxSearchResults);
  EXPECT_EQ(result.totalMatches, kMaxSearchResults);
}

TEST(ExploreSearch, NoMatchGivesEmptyResult) {
  const auto index = vimIndex();

  const auto result = searchPackages(index, "zzzz");

  EXPECT_TRUE(result.rows.empty());
  EXPECT_EQ(result.totalMatches, 0U);
}

}  // namespace
}  // namespace holonight_packages_application
