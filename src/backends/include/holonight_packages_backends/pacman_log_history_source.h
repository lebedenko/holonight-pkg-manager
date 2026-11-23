#pragma once

#include "holonight_packages_domain/history_source.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <sys/types.h>

namespace holonight_packages_backends {

// I/O seam of the history source (transaction-history DESIGN D-8). Empty members call the POSIX functions.
struct HistoryFileIo {
  std::function<int(const char* path, int flags)> open;
  std::function<ssize_t(int file_descriptor, void* buffer, std::size_t length, off_t offset)> pread;
  std::function<int(int file_descriptor)> close;
};

// True when the pacman database lock file exists. Empty means "stat() succeeds".
using LockProbe = std::function<bool(const std::filesystem::path& lock_file)>;

struct PacmanLogHistorySourceOptions {
  std::filesystem::path pacman_conf_path;  // the composition root passes kDefaultPacmanConfPath
  HistoryFileIo io;
  LockProbe lock_probe;
  std::size_t chunk_size = std::size_t{1} << 20U;  // bytes per pread; tests use small values
};

// Reads the transaction history from the pacman log named by pacman.conf, read-only and without libalpm or Qt
// (REQ-F-005, REQ-F-033, REQ-F-036). Keeps the parse state between reads so that an incremental read only reads the
// appended bytes (REQ-F-018). Thread-safe: read() is serialized.
class PacmanLogHistorySource final : public holonight_packages_domain::HistorySource {
 public:
  explicit PacmanLogHistorySource(PacmanLogHistorySourceOptions options);
  PacmanLogHistorySource(const PacmanLogHistorySource&) = delete;
  PacmanLogHistorySource(PacmanLogHistorySource&&) = delete;
  PacmanLogHistorySource& operator=(const PacmanLogHistorySource&) = delete;
  PacmanLogHistorySource& operator=(PacmanLogHistorySource&&) = delete;
  ~PacmanLogHistorySource() override;

  [[nodiscard]] holonight_packages_domain::HistoryReadResult read(
      holonight_packages_domain::HistoryReadMode mode) override;

  // Test introspection (REQ-F-005, REQ-F-020); not part of the port.
  [[nodiscard]] std::filesystem::path resolvedLogPath() const;
  [[nodiscard]] std::uint64_t consumedOffset() const;

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace holonight_packages_backends
