#include "StateDirScratchRoots.h"

#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace holonight_helper {
namespace {

namespace fs = std::filesystem;
using holonight_packages_application::ScratchRootError;

constexpr std::string_view kPrefix = "upgrade-";
constexpr mode_t kTraverseOnly = 0711;

}  // namespace

StateDirScratchRoots::StateDirScratchRoots(fs::path state_directory) : state_(std::move(state_directory)) {}

bool StateDirScratchRoots::ours(const fs::path& root) const {
  return root.parent_path() == state_ && root.filename().string().starts_with(kPrefix);
}

std::expected<fs::path, ScratchRootError> StateDirScratchRoots::create() {
  std::error_code error;
  fs::create_directories(state_, error);
  if (error || ::chmod(state_.c_str(), kTraverseOnly) != 0) {
    return std::unexpected(ScratchRootError::CannotCreate);
  }
  std::string pattern = (state_ / (std::string(kPrefix) + "XXXXXX")).string();
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  if (::mkdtemp(buffer.data()) == nullptr) {
    return std::unexpected(ScratchRootError::CannotCreate);
  }
  fs::path root(buffer.data());
  struct stat status{};
  if (::chmod(root.c_str(), kTraverseOnly) != 0 || ::lstat(root.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) ||
      status.st_uid != ::geteuid()) {
    fs::remove_all(root, error);
    return std::unexpected(ScratchRootError::CannotCreate);
  }
  return root;
}

void StateDirScratchRoots::remove(const fs::path& root) noexcept {
  if (!ours(root)) {
    return;
  }
  std::error_code error;
  fs::remove_all(root, error);  // removes symlinks themselves, never what they point to
}

void StateDirScratchRoots::removeAll() noexcept {
  std::error_code error;
  for (const auto& entry : fs::directory_iterator(state_, error)) {
    if (entry.path().filename().string().starts_with(kPrefix)) {
      fs::remove_all(entry.path(), error);
    }
  }
}

}  // namespace holonight_helper
