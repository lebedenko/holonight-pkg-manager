#include "alpm_session.h"

#include <cstdarg>
#include <cstdio>
#include <filesystem>
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
    alpm_handle_t* handle, const holonight_packages_backends::PacmanRepositories& repositories,
    const LogSink& journal) {
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
    for (const std::string& server : repository.servers) {
      alpm_db_add_server(database, server.c_str());
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

  std::unique_ptr<AlpmSession> session(new AlpmSession());
  session->settings_ = std::move(*settings);
  session->logSink_ = std::move(logSink);
  const auto& options = session->settings_.options;
  const auto& repositories = session->settings_.repositories;

  alpm_errno_t error = ALPM_ERR_OK;
  session->handle_ = alpm_initialize(config.root.c_str(), config.database_path.c_str(), &error);
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
  if (action == PlanAction::Install) {
    auto registered = registerRepositories(handle, repositories, session->logSink_);
    if (!registered) {
      return std::unexpected(registered.error());
    }
    session->repositories_ = std::move(*registered);
  }
  return session;
}

AlpmSession::~AlpmSession() {
  if (handle_ != nullptr) {
    releaseTransaction();
    alpm_release(handle_);
  }
}

bool AlpmSession::beginTransaction(int flags) {
  if (transactionOpen_) {
    return false;
  }
  transactionOpen_ = alpm_trans_init(handle_, flags) == 0;
  return transactionOpen_;
}

void AlpmSession::releaseTransaction() {
  if (transactionOpen_) {
    alpm_trans_release(handle_);
    transactionOpen_ = false;
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
