#pragma once

#include "holonight_packages_domain/transaction_error.h"
#include "holonight_packages_domain/transaction_request.h"
#include "holonight_packages_transaction/log_sink.h"
#include "holonight_packages_transaction/transaction_config.h"
#include "holonight_packages_transaction/transaction_seams.h"
#include "pacman_settings.h"
#include "question_policy.h"

#include <alpm.h>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace holonight_packages_transaction {

// One libalpm handle for one operation: a preview, a re-resolution, or an execution (DESIGN 6.1). It is never reused
// across plans, so no cache outlives the operation it served. Everything the handle needs comes from pacman.conf and
// the TransactionConfig; nothing comes from a caller.
class AlpmSession {
 public:
  struct Repository {
    std::string name;
    alpm_db_t* db = nullptr;
    std::uint32_t sig_level = 0;  // read back from libalpm after registration
    bool sig_level_explicit = false;
  };

  // Loads pacman.conf first; any failure there is ConfigurationError and libalpm stays uninitialised (REQ-F-020).
  // Sync databases are registered for installs only: a remove works without them (REQ-F-038).
  [[nodiscard]] static std::expected<std::unique_ptr<AlpmSession>, holonight_packages_domain::TransactionFailure> open(
      const TransactionConfig& config, holonight_packages_domain::PlanAction action, LogSink logSink);

  // system-upgrade (DESIGN 5.2, 5.3, 5.6): a scratch handle syncs into its own dbpath with the configured servers; a
  // live handle commits from the cache and registers the repositories without servers, so it can never download.
  enum class UpgradeRole : unsigned char { Scratch, Live };
  // The only server of a live upgrade handle (DESIGN D-7, revised): a file:// directory that does not exist.
  static constexpr const char* kNoDownloadServer = "file:///nonexistent/holonight-package-helper/no-download";

  // The configuration checks of the upgrade, before libalpm is initialised: unsupported options, a repository without
  // a server, then any other parse failure, each ConfigurationError (REQ-F-018, REQ-F-075, REQ-F-076).
  [[nodiscard]] static std::expected<holonight_packages_backends::PacmanSettings,
                                     holonight_packages_domain::TransactionFailure>
  loadUpgradeSettings(const TransactionConfig& config, const LogSink& logSink);

  // Opens a handle on `database_path` for the upgrade, after loadUpgradeSettings(); then refuses a signature level
  // weaker than Required + TrustedOnly with InsecureSigLevel before any network use (REQ-F-031). `seams` may be null;
  // its observe("alpm-initialize") runs right before alpm_initialize, and its fetch replaces the downloader.
  [[nodiscard]] static std::expected<std::unique_ptr<AlpmSession>, holonight_packages_domain::TransactionFailure>
  openUpgrade(const TransactionConfig& config, UpgradeRole role, const std::filesystem::path& database_path,
              const TransactionSeams* seams, LogSink logSink);

  // The hook directories libalpm searches, in order: the system directory, then pacman.conf's HookDir (default
  // <root>/etc/pacman.d/hooks). Later directories override earlier ones.
  [[nodiscard]] static std::vector<std::filesystem::path> hookDirectories(
      const TransactionConfig& config, const holonight_packages_backends::PacmanConfig& options);

  ~AlpmSession();
  AlpmSession(const AlpmSession&) = delete;
  AlpmSession& operator=(const AlpmSession&) = delete;
  AlpmSession(AlpmSession&&) = delete;
  AlpmSession& operator=(AlpmSession&&) = delete;

  [[nodiscard]] alpm_handle_t* handle() const noexcept { return handle_; }
  [[nodiscard]] const holonight_packages_backends::PacmanSettings& settings() const noexcept { return settings_; }
  [[nodiscard]] const std::vector<Repository>& repositories() const noexcept { return repositories_; }
  [[nodiscard]] const std::vector<std::filesystem::path>& hookDirs() const noexcept { return hookDirs_; }
  [[nodiscard]] QuestionPolicy& questions() noexcept { return questions_; }
  [[nodiscard]] const std::vector<std::string>& logLines() const noexcept { return logLines_; }
  // Forgets the log lines collected so far, so the next libalpm call can be classified on its own.
  void clearLog() noexcept { logLines_.clear(); }

  // Observers that run on the thread driving libalpm.
  using EventHandler = std::function<void(const alpm_event_t&)>;
  using DownloadHandler = std::function<void(const std::string& file, alpm_download_event_type_t type, void* data)>;
  using ProgressHandler = std::function<void(alpm_progress_t, const std::string& item, int percent, std::size_t total,
                                             std::size_t current)>;
  void setEventHandler(EventHandler handler) { eventHandler_ = std::move(handler); }
  void setDownloadHandler(DownloadHandler handler) { downloadHandler_ = std::move(handler); }
  void setProgressHandler(ProgressHandler handler) { progressHandler_ = std::move(handler); }

  // alpm_trans_init / alpm_trans_release. `flags` as for libalpm. At most one transaction is open.
  [[nodiscard]] bool beginTransaction(int flags);
  void releaseTransaction();
  [[nodiscard]] bool transactionOpen() const noexcept { return transactionOpen_; }

 private:
  AlpmSession() = default;

  [[nodiscard]] static std::expected<std::unique_ptr<AlpmSession>, holonight_packages_domain::TransactionFailure>
  initialize(const TransactionConfig& config, const std::filesystem::path& database_path,
             holonight_packages_backends::PacmanSettings settings, bool registerSync, bool withServers,
             LogSink logSink);

  static void eventTrampoline(void* context, alpm_event_t* event);
  static void questionTrampoline(void* context, alpm_question_t* question);
  static void logTrampoline(void* context, alpm_loglevel_t level, const char* format, va_list arguments);
  static void downloadTrampoline(void* context, const char* file, alpm_download_event_type_t type, void* data);
  static void progressTrampoline(void* context, alpm_progress_t progress, const char* item, int percent,
                                 std::size_t total, std::size_t current);

  alpm_handle_t* handle_ = nullptr;
  holonight_packages_backends::PacmanSettings settings_;
  std::vector<Repository> repositories_;
  std::vector<std::filesystem::path> hookDirs_;
  QuestionPolicy questions_;
  std::vector<std::string> logLines_;
  LogSink logSink_;
  EventHandler eventHandler_;
  DownloadHandler downloadHandler_;
  ProgressHandler progressHandler_;
  bool transactionOpen_ = false;
};

}  // namespace holonight_packages_transaction
