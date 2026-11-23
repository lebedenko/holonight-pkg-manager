#include "holonight_packages_backends/pacman_log_history_source.h"

#include "holonight_packages_backends/pacman_defaults.h"
#include "pacman_config.h"
#include "pacman_log_parser.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace holonight_packages_backends {

using holonight_packages_domain::HistoryEntry;
using holonight_packages_domain::HistoryReadMode;
using holonight_packages_domain::HistoryReadResult;
using holonight_packages_domain::HistoryStatus;

struct PacmanLogHistorySource::State {
  explicit State(PacmanLogHistorySourceOptions source_options) : options(std::move(source_options)) {
    if (!options.io.open) {
      options.io.open = [](const char* path, int flags) { return ::open(path, flags); };  // NOLINT(*-vararg)
    }
    if (!options.io.pread) {
      options.io.pread = [](int file_descriptor, void* buffer, std::size_t length, off_t offset) {
        return ::pread(file_descriptor, buffer, length, offset);
      };
    }
    if (!options.io.close) {
      options.io.close = [](int file_descriptor) { return ::close(file_descriptor); };
    }
    if (!options.lock_probe) {
      options.lock_probe = [](const std::filesystem::path& lock_file) {
        struct stat info{};
        return ::stat(lock_file.c_str(), &info) == 0;
      };
    }
    options.chunk_size = std::max<std::size_t>(options.chunk_size, 1);
  }

  void clear() {
    parse = {};
    log_path.clear();
    device = 0;
    inode = 0;
    offset = 0;
    have_state = false;
    delivered_last.reset();
  }

  PacmanLogHistorySourceOptions options;
  mutable std::mutex mutex;
  LogParseState parse;
  std::filesystem::path log_path;
  dev_t device = 0;
  ino_t inode = 0;
  std::uint64_t offset = 0;
  bool have_state = false;
  std::optional<HistoryEntry> delivered_last;
};

namespace {

HistoryReadResult unavailable(std::string reason) {
  HistoryReadResult result;
  result.reset = true;
  result.reason = std::move(reason);
  return result;
}

std::string openFailureReason(const std::filesystem::path& path, int error) {
  if (error == ENOENT) {
    return "The pacman log " + path.string() + " does not exist.";
  }
  return "Cannot read the pacman log " + path.string() + ": " + std::strerror(error) + ".";  // NOLINT(*-mt-unsafe)
}

// Closes the descriptor through the seam on every return path.
class Descriptor {
 public:
  Descriptor(int file_descriptor, const HistoryFileIo& file_io) : fd_(file_descriptor), io_(file_io) {}
  Descriptor(const Descriptor&) = delete;
  Descriptor(Descriptor&&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor& operator=(Descriptor&&) = delete;
  ~Descriptor() { io_.close(fd_); }

  [[nodiscard]] int get() const { return fd_; }

 private:
  int fd_;
  const HistoryFileIo& io_;
};

void parseBufferedLine(std::string_view line, std::string& pending, LogParseState& parse,
                       std::vector<HistoryEntry>& finished) {
  if (pending.empty()) {
    parseLines(line, parse, finished);
    return;
  }
  pending.append(line);
  parseLines(pending, parse, finished);
}

// Reads [offset, size) in chunks and passes every complete line to the parser. A line may span chunks: its start is
// carried over. A trailing partial line is not consumed, so `offset` stops at its first byte (REQ-F-020). Returns the
// errno of a failed pread.
std::optional<int> readLines(const PacmanLogHistorySourceOptions& options, int file_descriptor, std::uint64_t size,
                             std::uint64_t& offset, LogParseState& parse, std::vector<HistoryEntry>& finished) {
  std::string pending;  // bytes read after `offset`
  std::string chunk;
  bool skipping = false;  // inside an over-long line, until its newline
  const std::size_t chunk_size = std::min(options.chunk_size, kMaxLogLineLength + 1);
  std::uint64_t read_position = offset;
  while (read_position < size) {
    const auto wanted = static_cast<std::size_t>(std::min<std::uint64_t>(size - read_position, chunk_size));
    chunk.resize(wanted);
    const ssize_t got = options.io.pread(file_descriptor, chunk.data(), wanted, static_cast<off_t>(read_position));
    if (got < 0) {
      return errno;
    }
    if (got == 0) {
      break;  // the file shrank while being read; the next read notices it
    }
    const std::string_view bytes(chunk.data(), static_cast<std::size_t>(got));
    std::size_t begin = 0;
    while (begin < bytes.size()) {
      const std::size_t newline = bytes.find('\n', begin);
      const std::size_t end = newline == std::string_view::npos ? bytes.size() : newline;
      const std::size_t length = end - begin;
      skipping = skipping || length > kMaxLogLineLength - pending.size();
      if (newline == std::string_view::npos) {
        if (!skipping) {
          pending.append(bytes.substr(begin));
        }
        break;
      }

      if (!skipping) {
        parseBufferedLine(bytes.substr(begin, length + 1), pending, parse, finished);
      }
      pending.clear();
      skipping = false;
      begin = newline + 1;
      offset = read_position + begin;
    }
    read_position += bytes.size();
  }
  return std::nullopt;
}

}  // namespace

PacmanLogHistorySource::PacmanLogHistorySource(PacmanLogHistorySourceOptions options)
    : state_(std::make_unique<State>(std::move(options))) {}

PacmanLogHistorySource::~PacmanLogHistorySource() = default;

std::filesystem::path PacmanLogHistorySource::resolvedLogPath() const {
  const std::scoped_lock lock(state_->mutex);
  return state_->log_path;
}

std::uint64_t PacmanLogHistorySource::consumedOffset() const {
  const std::scoped_lock lock(state_->mutex);
  return state_->offset;
}

HistoryReadResult PacmanLogHistorySource::read(HistoryReadMode mode) {
  State& state = *state_;
  const std::scoped_lock lock(state.mutex);

  // 1. The log and lock paths come from pacman.conf (REQ-F-005, OQ-H1, OQ-H2).
  const auto config = parsePacmanLogLocations(state.options.pacman_conf_path);
  if (!config) {
    state.clear();
    return unavailable(config.error());
  }
  const std::filesystem::path log_path =
      config->log_file.empty() ? std::filesystem::path(kDefaultPacmanLogPath) : config->log_file;
  const std::filesystem::path lock_file =
      (config->db_path.empty() ? std::filesystem::path(kDefaultPacmanDatabasePath) : config->db_path) / "db.lck";

  bool full = mode == HistoryReadMode::Full || !state.have_state || log_path != state.log_path;

  // 2. Read-only open (REQ-F-036) and the explained empty states (REQ-F-021).
  const int file_descriptor = state.options.io.open(log_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (file_descriptor < 0) {
    const int error = errno;
    state.clear();
    state.log_path = log_path;
    return unavailable(openFailureReason(log_path, error));
  }
  const Descriptor descriptor(file_descriptor, state.options.io);
  struct stat info{};
  if (::fstat(descriptor.get(), &info) != 0) {
    const int error = errno;
    state.clear();
    state.log_path = log_path;
    return unavailable(openFailureReason(log_path, error));
  }
  const auto size = static_cast<std::uint64_t>(info.st_size);

  // 3. A rotated or truncated log is parsed again from the start (REQ-F-019).
  if (!full && (info.st_ino != state.inode || info.st_dev != state.device || size < state.offset)) {
    full = true;
  }
  if (full) {
    state.parse = {};
    state.offset = 0;
    state.delivered_last.reset();
  }

  // 4. Read [offset, size) and parse the complete lines (REQ-F-018, REQ-F-020).
  std::vector<HistoryEntry> combined;
  if (const auto error = readLines(state.options, descriptor.get(), size, state.offset, state.parse, combined)) {
    state.clear();
    state.log_path = log_path;
    return unavailable(openFailureReason(log_path, *error));
  }

  // 5. The open tail is in progress while pacman holds its lock, else interrupted (REQ-F-013, REQ-F-014). The probe
  // runs after the read, and its result is not stored in the parse state, so the next read evaluates it again.
  if (state.parse.last) {
    HistoryEntry tail = *state.parse.last;
    if (state.parse.last_open) {
      tail.status = state.options.lock_probe(lock_file) ? HistoryStatus::InProgress : HistoryStatus::Interrupted;
    }
    combined.push_back(std::move(tail));
  }

  // 6. The delta: the first combined entry is the previously delivered last entry in its current form.
  HistoryReadResult result;
  result.reset = full;
  if (!full && state.delivered_last && !combined.empty() && combined.front() == *state.delivered_last) {
    combined.erase(combined.begin());
  } else if (!full && state.delivered_last) {
    result.drop_last = 1;
  }
  if (!combined.empty() || full) {
    state.delivered_last = combined.empty() ? std::nullopt : std::optional<HistoryEntry>(combined.back());
  }
  result.append = std::move(combined);

  state.log_path = log_path;
  state.device = info.st_dev;
  state.inode = info.st_ino;
  state.have_state = true;
  return result;
}

}  // namespace holonight_packages_backends
