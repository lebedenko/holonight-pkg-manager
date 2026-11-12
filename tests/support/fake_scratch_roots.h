#pragma once

#include "holonight_packages_application/scratch_roots.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace holonight_packages_testing {

// Hands out made-up paths and records what was created and removed; touches no file.
class FakeScratchRoots final : public holonight_packages_application::ScratchRoots {
 public:
  void setCreateFailure(bool fail) { fail_ = fail; }

  [[nodiscard]] std::expected<std::filesystem::path, holonight_packages_application::ScratchRootError> create()
      override {
    if (fail_) {
      return std::unexpected(holonight_packages_application::ScratchRootError::CannotCreate);
    }
    std::filesystem::path root = "/fake-state/upgrade-" + std::to_string(++created_);
    live_.insert(root);
    return root;
  }
  void remove(const std::filesystem::path& root) noexcept override {
    removed_.push_back(root);
    live_.erase(root);
  }
  void removeAll() noexcept override {
    ++sweeps_;
    live_.clear();
  }

  [[nodiscard]] int created() const { return created_; }
  [[nodiscard]] const std::vector<std::filesystem::path>& removed() const { return removed_; }
  [[nodiscard]] bool exists(const std::filesystem::path& root) const { return live_.contains(root); }
  [[nodiscard]] std::size_t liveCount() const { return live_.size(); }
  [[nodiscard]] int sweeps() const { return sweeps_; }

 private:
  bool fail_ = false;
  int created_ = 0;
  int sweeps_ = 0;
  std::set<std::filesystem::path> live_;
  std::vector<std::filesystem::path> removed_;
};

}  // namespace holonight_packages_testing
