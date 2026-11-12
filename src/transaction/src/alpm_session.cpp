#include "alpm_session.h"

#include "holonight_packages_transaction/database_lock.h"
#include "signature_policy.h"

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <vector>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::PlanAction;
using holonight_packages_domain::TransactionErrorCode;
using holonight_packages_domain::TransactionFailure;

constexpr std::size_t kMaxLogLines = 4096;

TransactionFailure failure(TransactionErrorCode code) {
  return TransactionFailure{.code = code, .names = {}, .choices = {}};
}

void configurePaths(alpm_handle_t* handle, const TransactionConfig& config,
                    const holonight_packages_backends::PacmanConfig& options,
                    const holonight_packages_backends::PacmanRepositories& repositories,
                    std::vector<std::filesystem::path>& hookDirs) {
  const std::filesystem::path& root = config.root;
  alpm_option_set_logfile(handle, (options.log_file.empty() ? root / "var/log/pacman.log" : options.log_file).c_str());
  alpm_option_set_gpgdir(handle, repositories.gpg_directory.c_str());
  if (options.cache_dirs.empty()) {
    alpm_option_add_cachedir(handle, config.cache_directory.c_str());
  }
  for (const auto& directory : options.cache_dirs) {
    alpm_option_add_cachedir(handle, directory.c_str());
  }
  // The system hook directory is always searched first; configured directories (default /etc/pacman.d/hooks) follow
  // and override same-named hooks, as pacman does.
  hookDirs = AlpmSession::hookDirectories(config, options);
  for (const auto& directory : hookDirs) {
    alpm_option_add_hookdir(handle, directory.c_str());
  }
  for (const std::string& architecture : repositories.architectures) {
    alpm_option_add_architecture(handle, architecture.c_str());
  }
}

void configurePolicy(alpm_handle_t* handle, const holonight_packages_backends::PacmanConfig& options,
                     const holonight_packages_backends::PacmanRepositories& repositories) {
  for (const std::string& pattern : options.no_upgrade) {
    alpm_option_add_noupgrade(handle, pattern.c_str());
  }
  for (const std::string& pattern : options.no_extract) {
    alpm_option_add_noextract(handle, pattern.c_str());
  }
  for (const std::string& name : options.ignore_pkgs) {
    alpm_option_add_ignorepkg(handle, name.c_str());
  }
  for (const std::string& name : options.ignore_groups) {
    alpm_option_add_ignoregroup(handle, name.c_str());
  }
  alpm_option_set_default_siglevel(handle, static_cast<int>(repositories.default_sig_level));
  alpm_option_set_checkspace(handle, options.check_space ? 1 : 0);
  if (options.parallel_downloads > 0) {
    alpm_option_set_parallel_downloads(handle, static_cast<unsigned int>(options.parallel_downloads));
  }
  if (!options.download_user.empty()) {
    alpm_option_set_sandboxuser(handle, options.download_user.c_str());
  }
  alpm_option_set_disable_sandbox_filesystem(handle, options.disable_sandbox_filesystem ? 1 : 0);
  alpm_option_set_disable_sandbox_syscalls(handle, options.disable_sandbox_syscalls ? 1 : 0);
}

std::expected<std::vector<AlpmSession::Repository>, TransactionFailure> registerRepositories(
    alpm_handle_t* handle, const holonight_packages_backends::PacmanRepositories& repositories, const LogSink& journal,
    bool withServers) {
  std::vector<AlpmSession::Repository> registered;
  for (const auto& repository : repositories.repositories) {
    // A repository without any SigLevel is registered at libalpm's own default, 0, as REQ-F-039 words it: its
    // databases still load, and the signature policy then refuses every install (SI-14).
    const int level = repository.sig_level_explicit ? static_cast<int>(repository.sig_level) : 0;
    alpm_db_t* database = alpm_register_syncdb(handle, repository.name.c_str(), level);
    if (database == nullptr) {
      if (journal) {
        journal(LogLevel::Error,
                "cannot register repository " + repository.name + ": " + alpm_strerror(alpm_errno(handle)));
      }
      return std::unexpected(failure(TransactionErrorCode::DatabaseUnavailable));
    }
    if (withServers) {
      for (const std::string& server : repository.servers) {
        alpm_db_add_server(database, server.c_str());
      }
    } else {
      // libalpm refuses a commit for a repository without any server, even when every file is cached, so the live
      // upgrade handle gets one server that cannot answer: a download it ever needed would fail (REQ-F-056).
      alpm_db_add_server(database, AlpmSession::kNoDownloadServer);
    }
    registered.push_back(AlpmSession::Repository{
        .name = repository.name,
        .db = database,
        .sig_level = static_cast<std::uint32_t>(alpm_db_get_siglevel(database)),
        .sig_level_explicit = repository.sig_level_explicit,
    });
  }
  return registered;
}

}  // namespace

std::vector<std::filesystem::path> AlpmSession::hookDirectories(
    const TransactionConfig& config, const holonight_packages_backends::PacmanConfig& options) {
  std::vector<std::filesystem::path> directories{config.root / "usr/share/libalpm/hooks"};
  if (options.hook_dirs.empty()) {
    directories.push_back(config.root / "etc/pacman.d/hooks");
  }
  directories.insert(directories.end(), options.hook_dirs.begin(), options.hook_dirs.end());
  return directories;
}

std::expected<std::unique_ptr<AlpmSession>, TransactionFailure> AlpmSession::open(const TransactionConfig& config,
                                                                                  PlanAction action, LogSink logSink) {
  auto settings = holonight_packages_backends::loadPacmanSettings(config.pacman_conf_path,
                                                                  holonight_packages_backends::machineArchitecture());
  if (!settings) {
    if (logSink) {
      logSink(LogLevel::Error, redactUrlCredentials("cannot load pacman.conf: " + settings.error()));
    }
    return std::unexpected(failure(TransactionErrorCode::ConfigurationError));
  }
  return initialize(config, config.database_path, std::move(*settings), action == PlanAction::Install, true,
                    std::move(logSink));
}

namespace {

// "/srv/db/" and "/srv/db" name the same directory.
std::string normalizedDirectory(const std::filesystem::path& path) {
  std::string text = path.lexically_normal().string();
  while (text.size() > 1 && text.back() == '/') {
    text.pop_back();
  }
  return text;
}

}  // namespace

std::expected<holonight_packages_backends::PacmanSettings, TransactionFailure> AlpmSession::loadUpgradeSettings(
    const TransactionConfig& config, const LogSink& logSink) {
  holonight_packages_backends::PacmanDiagnostics diagnostics;
  auto settings = holonight_packages_backends::loadPacmanSettings(
      config.pacman_conf_path, holonight_packages_backends::machineArchitecture(), &diagnostics);
  std::vector<std::string> unsupported = diagnostics.unsupported;
  // The helper takes its paths from its command line; a pacman.conf that names other ones describes another system.
  if (!diagnostics.db_path.empty() &&
      normalizedDirectory(diagnostics.db_path) != normalizedDirectory(config.database_path)) {
    unsupported.emplace_back("DBPath");
  }
  if (!diagnostics.root_dir.empty() && normalizedDirectory(diagnostics.root_dir) != normalizedDirectory(config.root)) {
    unsupported.emplace_back("RootDir");
  }
  if (!unsupported.empty()) {
    if (logSink) {
      logSink(LogLevel::Error, "pacman.conf sets options the system upgrade does not support");
    }
    return std::unexpected(TransactionFailure{
        .code = TransactionErrorCode::ConfigurationError,
        .names = std::move(unsupported),
        .choices = {},
    });
  }
  if (!diagnostics.repository_without_server.empty()) {
    if (logSink) {
      logSink(LogLevel::Error, "repository " + diagnostics.repository_without_server + " has no Server");
    }
    return std::unexpected(TransactionFailure{
        .code = TransactionErrorCode::ConfigurationError,
        .names = {diagnostics.repository_without_server},
        .choices = {},
    });
  }
  if (!settings) {
    if (logSink) {
      logSink(LogLevel::Error, redactUrlCredentials("cannot load pacman.conf: " + settings.error()));
    }
    return std::unexpected(failure(TransactionErrorCode::ConfigurationError));
  }
  return std::move(*settings);
}

std::expected<std::unique_ptr<AlpmSession>, TransactionFailure> AlpmSession::openUpgrade(
    const TransactionConfig& config, UpgradeRole role, const std::filesystem::path& database_path,
    const TransactionSeams* seams, LogSink logSink) {
  auto settings = loadUpgradeSettings(config, logSink);
  if (!settings) {
    return std::unexpected(std::move(settings.error()));
  }
  if (seams != nullptr && seams->observe) {
    seams->observe("alpm-initialize");
  }
  auto session =
      initialize(config, database_path, std::move(*settings), true, role == UpgradeRole::Scratch, std::move(logSink));
  if (!session) {
    return session;
  }
  std::vector<RepositoryLevel> levels;
  for (const auto& repository : (*session)->repositories()) {
    levels.push_back(RepositoryLevel{
        .name = repository.name,
        .level = repository.sig_level,
        .explicit_level = repository.sig_level_explicit,
    });
  }
  if (auto weak = checkInstallSignatureLevels(levels, (*session)->logSink_)) {
    return std::unexpected(std::move(*weak));
  }
  return session;
}

std::expected<std::unique_ptr<AlpmSession>, TransactionFailure> AlpmSession::initialize(
    const TransactionConfig& config, const std::filesystem::path& database_path,
    holonight_packages_backends::PacmanSettings settings, bool registerSync, bool withServers, LogSink logSink) {
  std::unique_ptr<AlpmSession> session(new AlpmSession());
  session->settings_ = std::move(settings);
  session->logSink_ = std::move(logSink);
  const auto& options = session->settings_.options;
  const auto& repositories = session->settings_.repositories;

  alpm_errno_t error = ALPM_ERR_OK;
  session->handle_ = alpm_initialize(config.root.c_str(), database_path.c_str(), &error);
  if (session->handle_ == nullptr) {
    if (session->logSink_) {
      session->logSink_(LogLevel::Error, std::string("alpm_initialize failed: ") + alpm_strerror(error));
    }
    return std::unexpected(failure(TransactionErrorCode::ConfigurationError));
  }
  alpm_handle_t* handle = session->handle_;
  alpm_option_set_eventcb(handle, &AlpmSession::eventTrampoline, session.get());
  alpm_option_set_questioncb(handle, &AlpmSession::questionTrampoline, session.get());
  alpm_option_set_logcb(handle, &AlpmSession::logTrampoline, session.get());
  alpm_option_set_dlcb(handle, &AlpmSession::downloadTrampoline, session.get());
  alpm_option_set_progresscb(handle, &AlpmSession::progressTrampoline, session.get());

  configurePaths(handle, config, options, repositories, session->hookDirs_);
  configurePolicy(handle, options, repositories);
  if (registerSync) {
    auto registered = registerRepositories(handle, repositories, session->logSink_, withServers);
    if (!registered) {
      return std::unexpected(registered.error());
    }
    session->repositories_ = std::move(*registered);
  }
  return session;
}

namespace {
struct LifecycleState {
  std::mutex mutex;
  // A NOLOCK preview can coexist with the owning session.
  const AlpmSession* owner = nullptr;
  std::filesystem::path owner_lock;  // the lock file libalpm created for `owner`
};

LifecycleState& lifecycleState() {
  static LifecycleState state;
  return state;
}
}  // namespace

DatabaseLockGuard::DatabaseLockGuard() : lock_(lifecycleState().mutex) {}

bool DatabaseLockGuard::ownsLock() const noexcept { return lock_.owns_lock() && lifecycleState().owner != nullptr; }

std::optional<std::filesystem::path> DatabaseLockGuard::ownedLockPath() const {
  if (!ownsLock()) {
    return std::nullopt;
  }
  return lifecycleState().owner_lock;
}

AlpmSession::~AlpmSession() {
  const std::scoped_lock lock(lifecycleState().mutex);
  if (handle_ != nullptr) {
    if (transactionOpen_) {
      alpm_trans_release(handle_);
    }
    alpm_release(handle_);
    if (lifecycleState().owner == this) {
      lifecycleState().owner = nullptr;
      lifecycleState().owner_lock.clear();
    }
  }
}

bool AlpmSession::beginTransaction(int flags) {
  const std::scoped_lock lock(lifecycleState().mutex);
  if (transactionOpen_) {
    return false;
  }
  transactionOpen_ = alpm_trans_init(handle_, flags) == 0;
  if (transactionOpen_ && (flags & ALPM_TRANS_FLAG_NOLOCK) == 0) {
    lifecycleState().owner = this;
    const char* lock_path = alpm_option_get_lockfile(handle_);
    lifecycleState().owner_lock = lock_path != nullptr ? std::filesystem::path(lock_path) : std::filesystem::path();
  }
  return transactionOpen_;
}

void AlpmSession::releaseTransaction() {
  const std::scoped_lock lock(lifecycleState().mutex);
  if (transactionOpen_) {
    alpm_trans_release(handle_);
    transactionOpen_ = false;
    if (lifecycleState().owner == this) {
      lifecycleState().owner = nullptr;
      lifecycleState().owner_lock.clear();
    }
  }
}

void AlpmSession::eventTrampoline(void* context, alpm_event_t* event) {
  auto* session = static_cast<AlpmSession*>(context);
  if (session->eventHandler_) {
    session->eventHandler_(*event);
  }
}

void AlpmSession::questionTrampoline(void* context, alpm_question_t* question) {
  auto* session = static_cast<AlpmSession*>(context);
  session->questions_.handle(question);
  if (session->logSink_ && !session->questions_.askedTypes().empty()) {
    session->logSink_(LogLevel::Info,
                      "libalpm question answered by policy: " + session->questions_.askedTypes().back());
  }
}

void AlpmSession::logTrampoline(void* context, alpm_loglevel_t level, const char* format, va_list arguments) {
  if ((level & (ALPM_LOG_ERROR | ALPM_LOG_WARNING)) == 0) {
    return;
  }
  auto* session = static_cast<AlpmSession*>(context);
  // libalpm hands the message over as a printf format and a va_list; there is no other way to read it.
  // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  std::va_list copy;
  va_copy(copy, arguments);
  const int length = std::vsnprintf(nullptr, 0, format, copy);
  va_end(copy);
  if (length < 0) {
    return;
  }
  std::string text(static_cast<std::size_t>(length), '\0');
  std::vsnprintf(text.data(), text.size() + 1, format, arguments);
  // NOLINTEND(cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  text = redactUrlCredentials(text);
  if (session->logLines_.size() < kMaxLogLines) {
    session->logLines_.push_back(text);
  }
  if (session->logSink_) {
    session->logSink_((level & ALPM_LOG_ERROR) != 0 ? LogLevel::Error : LogLevel::Warning, text);
  }
}

void AlpmSession::downloadTrampoline(void* context, const char* file, alpm_download_event_type_t type, void* data) {
  auto* session = static_cast<AlpmSession*>(context);
  if (session->downloadHandler_) {
    session->downloadHandler_(file != nullptr ? file : "", type, data);
  }
}

void AlpmSession::progressTrampoline(void* context, alpm_progress_t progress, const char* item, int percent,
                                     std::size_t total, std::size_t current) {
  auto* session = static_cast<AlpmSession*>(context);
  if (session->progressHandler_) {
    session->progressHandler_(progress, item != nullptr ? item : "", percent, total, current);
  }
}

}  // namespace holonight_packages_transaction
