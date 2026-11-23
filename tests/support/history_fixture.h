#pragma once

#include "holonight_packages_backends/pacman_log_history_source.h"

#include <QTemporaryDir>

#include <atomic>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

namespace holonight_test {

// What a guarded HistoryFileIo saw (REQ-C-003, REQ-F-022, REQ-F-036).
struct HistoryIoRecord {
  std::mutex mutex;
  std::vector<std::string> opened_paths;
  std::vector<int> open_flags;
  std::vector<std::thread::id> threads;
  std::atomic<std::size_t> bytes_read{0};
  std::atomic<std::size_t> pread_calls{0};

  void reset() {
    const std::scoped_lock lock(mutex);
    opened_paths.clear();
    open_flags.clear();
    threads.clear();
    bytes_read = 0;
    pread_calls = 0;
  }
};

inline constexpr std::string_view kSystemPacmanLog = "/var/log/pacman.log";

// A HistoryFileIo that calls the POSIX functions, records every call, and fails the test when it is asked to open
// the system pacman log (REQ-C-003).
inline holonight_packages_backends::HistoryFileIo guardedIo(const std::shared_ptr<HistoryIoRecord>& record) {
  holonight_packages_backends::HistoryFileIo file_io;
  file_io.open = [record](const char* path, int flags) {
    {
      const std::scoped_lock lock(record->mutex);
      record->opened_paths.emplace_back(path);
      record->open_flags.push_back(flags);
      record->threads.push_back(std::this_thread::get_id());
    }
    if (std::string_view(path) == kSystemPacmanLog) {
      ADD_FAILURE() << "a history test opened the system pacman log";
      errno = EACCES;
      return -1;
    }
    return ::open(path, flags);  // NOLINT(*-vararg)
  };
  file_io.pread = [record](int file_descriptor, void* buffer, std::size_t length, off_t offset) {
    const ssize_t got = ::pread(file_descriptor, buffer, length, offset);
    ++record->pread_calls;
    if (got > 0) {
      record->bytes_read += static_cast<std::size_t>(got);
    }
    return got;
  };
  file_io.close = [](int file_descriptor) { return ::close(file_descriptor); };
  return file_io;
}

// A temporary pacman.conf, log and database directory for one test.
class HistoryFixture {
 public:
  HistoryFixture() {
    EXPECT_TRUE(directory_.isValid());
    root_ = std::filesystem::path(directory_.path().toStdString());
    log_ = root_ / "pacman.log";
    db_ = root_ / "db";
    std::filesystem::create_directories(db_);
    writeConf(log_, db_);
  }

  [[nodiscard]] const std::filesystem::path& root() const { return root_; }
  [[nodiscard]] const std::filesystem::path& conf() const { return conf_; }
  [[nodiscard]] const std::filesystem::path& log() const { return log_; }
  [[nodiscard]] std::filesystem::path lockFile() const { return db_ / "db.lck"; }

  // Rewrites pacman.conf; an empty path leaves the key out.
  void writeConf(const std::filesystem::path& log_path, const std::filesystem::path& db_path) {
    conf_ = root_ / "pacman.conf";
    std::ofstream out(conf_, std::ios::trunc);
    out << "[options]\n";
    if (!log_path.empty()) {
      out << "LogFile = " << log_path.string() << "\n";
    }
    if (!db_path.empty()) {
      out << "DBPath = " << db_path.string() << "\n";
    }
  }

  void append(std::string_view text) const { std::ofstream(log_, std::ios::binary | std::ios::app) << text; }

  // Same inode, new (usually smaller) content.
  void truncateAndWrite(std::string_view text) const {
    std::ofstream(log_, std::ios::binary | std::ios::trunc) << text;
  }

  // A new file renamed over the log: a new inode, as log rotation gives.
  void renameOver(std::string_view text) const {
    const auto replacement = root_ / "pacman.log.new";
    std::ofstream(replacement, std::ios::binary | std::ios::trunc) << text;
    std::filesystem::rename(replacement, log_);
  }

  void touchLock() const { std::ofstream(lockFile()) << ""; }
  void removeLock() const { std::filesystem::remove(lockFile()); }

  [[nodiscard]] holonight_packages_backends::PacmanLogHistorySourceOptions options(
      const std::shared_ptr<HistoryIoRecord>& record) const {
    holonight_packages_backends::PacmanLogHistorySourceOptions result;
    result.pacman_conf_path = conf_;
    result.io = guardedIo(record);
    return result;
  }

 private:
  QTemporaryDir directory_;
  std::filesystem::path root_;
  std::filesystem::path conf_;
  std::filesystem::path log_;
  std::filesystem::path db_;
};

}  // namespace holonight_test
