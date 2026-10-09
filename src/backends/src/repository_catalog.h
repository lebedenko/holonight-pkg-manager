#pragma once

#include "holonight_packages_domain/pending_update.h"
#include "pacman_repositories.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <optional>

namespace holonight_packages_backends {

inline std::optional<holonight_packages_domain::RepositoryProvenance> describeRepository(
    const PacmanRepository& repository, const PacmanRepositories& configuration, const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) || error) {
    return std::nullopt;
  }
  QFile file(QString::fromStdString(path.string()));
  if (!file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file)) {
    return std::nullopt;
  }
  QJsonArray servers;
  for (const auto& server : repository.servers) {
    servers.append(QString::fromStdString(server));
  }
  QJsonArray architectures;
  for (const auto& architecture : configuration.architectures) {
    architectures.append(QString::fromStdString(architecture));
  }
  QJsonObject identity{
      {"name", QString::fromStdString(repository.name)},
      {"servers", servers},
      {"signature", static_cast<double>(repository.sig_level)},
      {"gpg", QString::fromStdString(configuration.gpg_directory.string())},
      {"architectures", architectures},
  };
  const auto modified = std::filesystem::last_write_time(path, error);
  if (error) {
    return std::nullopt;
  }
  return holonight_packages_domain::RepositoryProvenance{
      .name = repository.name,
      .identity =
          QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
              .toHex()
              .toStdString(),
      .digest = hash.result().toHex().toStdString(),
      .timestamp = std::chrono::clock_cast<std::chrono::system_clock>(modified),
      .database = path,
  };
}

}  // namespace holonight_packages_backends
