#include "scratch_dir.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace holonight_packages_backends {

namespace {

std::unexpected<ScratchError> invalid(std::string message) {
  return std::unexpected(ScratchError{.kind = ScratchFailure::Invalid, .message = std::move(message)});
}

std::string errnoText(const std::string& what, const std::filesystem::path& path) {
  return what + " " + path.string() + ": " + std::strerror(errno);
}

struct Fd {
  int value = -1;
  explicit Fd(int descriptor) : value(descriptor) {}
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  Fd(Fd&&) = delete;
  Fd& operator=(Fd&&) = delete;
  ~Fd() {
    if (value >= 0) {
      ::close(value);
    }
  }
};

void removeEntry(int parent, const char* name);

void removeDirectoryContents(int directory) {
  // fdopendir takes ownership of the descriptor it is given, so hand it a duplicate.
  const int duplicate = ::dup(directory);
  if (duplicate < 0) {
    return;
  }
  DIR* stream = ::fdopendir(duplicate);
  if (stream == nullptr) {
    ::close(duplicate);
    return;
  }
  std::vector<std::string> names;
  while (const dirent* entry = ::readdir(stream)) {
    const std::string name =
        entry->d_name;  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    if (name != "." && name != "..") {
      names.push_back(name);
    }
  }
  ::closedir(stream);
  for (const std::string& name : names) {
    removeEntry(directory, name.c_str());
  }
}

void removeEntry(int parent, const char* name) {
  struct stat info{};
  if (::fstatat(parent, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
    return;
  }
  if (!S_ISDIR(info.st_mode)) {
    ::unlinkat(parent, name, 0);  // regular files and symlinks alike; a symlink is never followed
    return;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) and openat(2) are variadic.
  const int child = ::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (child >= 0) {
    if (info.st_uid == ::geteuid()) {
      ::fchmod(child, 0700);
    }
    removeDirectoryContents(child);
    ::close(child);
  }
  ::unlinkat(parent, name, AT_REMOVEDIR);
}

struct RunEntry {
  std::filesystem::path path;
  std::string name;
  struct timespec modified{};
};

std::vector<RunEntry> listRunDirectories(const std::filesystem::path& root) {
  std::vector<RunEntry> runs;
  std::error_code fsError;
  for (const auto& entry : std::filesystem::directory_iterator(root, fsError)) {
    const std::string name = entry.path().filename().string();
    if (!name.starts_with(kScratchRunPrefix)) {
      continue;
    }
    struct stat info{};
    if (::lstat(entry.path().c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
      continue;
    }
    runs.push_back(RunEntry{.path = entry.path(), .name = name, .modified = info.st_mtim});
  }
  return runs;
}

bool newer(const RunEntry& left, const RunEntry& right) {
  if (left.modified.tv_sec != right.modified.tv_sec) {
    return left.modified.tv_sec > right.modified.tv_sec;
  }
  if (left.modified.tv_nsec != right.modified.tv_nsec) {
    return left.modified.tv_nsec > right.modified.tv_nsec;
  }
  return left.name > right.name;
}

}  // namespace

std::expected<void, ScratchError> prepareScratchRoot(const std::filesystem::path& root) {
  std::error_code fsError;
  struct stat info{};
  if (::lstat(root.c_str(), &info) != 0) {
    if (errno != ENOENT) {
      return invalid(errnoText("cannot stat", root));
    }
    if (root.has_parent_path()) {
      std::filesystem::create_directories(root.parent_path(), fsError);
      if (fsError) {
        return invalid("cannot create " + root.parent_path().string() + ": " + fsError.message());
      }
    }
    if (::mkdir(root.c_str(), 0700) != 0 && errno != EEXIST) {
      return invalid(errnoText("cannot create", root));
    }
    if (::lstat(root.c_str(), &info) != 0) {
      return invalid(errnoText("cannot stat", root));
    }
  }
  if (S_ISLNK(info.st_mode)) {
    return invalid("scratch root " + root.string() + " is a symlink");
  }
  if (!S_ISDIR(info.st_mode)) {
    return invalid("scratch root " + root.string() + " is not a directory");
  }
  if (info.st_uid != ::geteuid()) {
    return invalid("scratch root " + root.string() + " is not owned by the current user");
  }
  if ((info.st_mode & 0022) != 0) {
    return invalid("scratch root " + root.string() + " is writable by group or others");
  }
  return {};
}

ScratchLock::ScratchLock(ScratchLock&& other) noexcept : descriptor_(std::exchange(other.descriptor_, -1)) {}

ScratchLock& ScratchLock::operator=(ScratchLock&& other) noexcept {
  if (this != &other) {
    if (descriptor_ >= 0) {
      ::close(descriptor_);
    }
    descriptor_ = std::exchange(other.descriptor_, -1);
  }
  return *this;
}

ScratchLock::~ScratchLock() {
  if (descriptor_ >= 0) {
    ::close(descriptor_);  // releases the flock
  }
}

std::expected<ScratchLock, ScratchError> ScratchLock::acquire(const std::filesystem::path& root) {
  const std::filesystem::path path = root / kScratchLockName;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) and openat(2) are variadic.
  const int descriptor = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (descriptor < 0) {
    return invalid(errnoText("cannot open", path));
  }
  struct stat info{};
  if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid()) {
    ::close(descriptor);
    return invalid("lock file " + path.string() + " is not a regular file owned by the current user");
  }
  ::fchmod(descriptor, 0600);
  if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
    const int error = errno;
    ::close(descriptor);
    if (error == EWOULDBLOCK) {
      return std::unexpected(
          ScratchError{.kind = ScratchFailure::Busy, .message = "another check holds " + path.string()});
    }
    return invalid("cannot lock " + path.string() + ": " + std::strerror(error));
  }
  return ScratchLock(descriptor);
}

std::expected<std::filesystem::path, ScratchError> createRunDirectory(const std::filesystem::path& root) {
  std::string pattern = (root / (std::string(kScratchRunPrefix) + "XXXXXX")).string();
  if (::mkdtemp(pattern.data()) == nullptr) {
    return invalid(errnoText("cannot create a run directory in", root));
  }
  return std::filesystem::path(pattern);
}

void removeTreeWithoutFollowingLinks(const std::filesystem::path& path) {
  const std::filesystem::path parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) and openat(2) are variadic.
  const Fd parent_fd(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (parent_fd.value < 0) {
    return;
  }
  removeEntry(parent_fd.value, path.filename().c_str());
}

void sweepScratchRoot(const std::filesystem::path& root, const std::filesystem::path& also_keep) {
  std::vector<RunEntry> runs = listRunDirectories(root);
  std::ranges::sort(runs, newer);
  std::filesystem::path newest;
  if (!runs.empty()) {
    newest = runs.front().path;
  }
  std::error_code fsError;
  std::vector<std::filesystem::path> victims;
  for (const auto& entry : std::filesystem::directory_iterator(root, fsError)) {
    const std::filesystem::path& path = entry.path();
    if (path.filename() == kScratchLockName || path == newest || (!also_keep.empty() && path == also_keep)) {
      continue;
    }
    victims.push_back(path);
  }
  for (const auto& victim : victims) {
    removeTreeWithoutFollowingLinks(victim);
  }
}

void removeOtherRunDirectories(const std::filesystem::path& root, const std::filesystem::path& keep) {
  for (const RunEntry& run : listRunDirectories(root)) {
    if (run.path != keep) {
      removeTreeWithoutFollowingLinks(run.path);
    }
  }
}

}  // namespace holonight_packages_backends
