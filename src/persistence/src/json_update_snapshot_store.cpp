#include "holonight_packages_persistence/json_update_snapshot_store.h"

#include "holonight_packages_persistence/catalog_lock.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>
#include <QUuid>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <span>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace holonight_packages_persistence {

namespace {

using holonight_packages_domain::CheckedSnapshot;
using holonight_packages_domain::PendingUpdate;
using holonight_packages_domain::SnapshotFileState;
using holonight_packages_domain::SnapshotLoad;
using holonight_packages_domain::SnapshotStoreError;

// Closes on scope exit.
struct Descriptor {
  int fd = -1;
  Descriptor() = default;
  explicit Descriptor(int value) : fd(value) {}
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&&) = delete;
  Descriptor& operator=(Descriptor&&) = delete;
  ~Descriptor() {
    if (fd >= 0) {
      ::close(fd);
    }
  }
};

constexpr double kMaxExactInteger = 9007199254740992.0;  // 2^53

std::unexpected<SnapshotStoreError> storeError(const std::string& message) {
  return std::unexpected(SnapshotStoreError{message});
}

std::int64_t toSeconds(std::chrono::system_clock::time_point time) {
  return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

QByteArray serialize(const CheckedSnapshot& snapshot) {
  QJsonArray updates;
  int count = 0;
  for (const PendingUpdate& update : snapshot.snapshot.updates) {
    QJsonObject row;
    row["name"] = QString::fromStdString(update.name);
    row["installedVersion"] = QString::fromStdString(update.installedVersion);
    row["availableVersion"] = QString::fromStdString(update.availableVersion);
    row["repository"] = QString::fromStdString(update.repository);
    row["downloadSizeBytes"] = static_cast<double>(update.downloadSizeBytes);
    row["installedSizeDeltaBytes"] = static_cast<double>(update.installedSizeDeltaBytes);
    row["ignored"] = update.ignored;
    updates.append(row);
    if (!update.ignored) {
      ++count;
    }
  }
  QJsonObject root;
  root["schemaVersion"] = JsonUpdateSnapshotStore::kSchemaVersion;
  root["fetchedAt"] = static_cast<double>(toSeconds(snapshot.fetchedAt));
  root["databasesFound"] = snapshot.snapshot.databasesFound;
  root["dataAsOf"] = static_cast<double>(toSeconds(snapshot.snapshot.dataAsOf));
  root["count"] = count;
  root["updates"] = updates;
  return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool integral(const QJsonValue& value, double minimum, std::int64_t& out) {
  if (!value.isDouble()) {
    return false;
  }
  const double number = value.toDouble();
  if (!std::isfinite(number) || number != std::floor(number) || number < minimum || number > kMaxExactInteger) {
    return false;
  }
  out = static_cast<std::int64_t>(number);
  return true;
}

bool string(const QJsonObject& object, const char* key, std::string& out) {
  const QJsonValue value = object.value(key);
  if (!value.isString()) {
    return false;
  }
  out = value.toString().toStdString();
  return true;
}

// Returns nullopt for anything that is not a complete, consistent version-1 document.
std::optional<CheckedSnapshot> parse(const QByteArray& bytes) {
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return std::nullopt;
  }
  const QJsonObject root = document.object();
  std::int64_t version = 0;
  std::int64_t fetchedAt = 0;
  std::int64_t dataAsOf = 0;
  std::int64_t count = 0;
  if (!integral(root.value("schemaVersion"), 0, version) || version != JsonUpdateSnapshotStore::kSchemaVersion ||
      !integral(root.value("fetchedAt"), 0, fetchedAt) || !integral(root.value("dataAsOf"), 0, dataAsOf) ||
      !integral(root.value("count"), 0, count) || !root.value("databasesFound").isBool() ||
      !root.value("updates").isArray()) {
    return std::nullopt;
  }

  const auto maximumSeconds =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::duration::max()).count();
  if (fetchedAt > maximumSeconds || dataAsOf > maximumSeconds) {
    return std::nullopt;
  }
  CheckedSnapshot result{};
  result.fetchedAt = std::chrono::system_clock::time_point{std::chrono::seconds{fetchedAt}};
  result.snapshot.databasesFound = root.value("databasesFound").toBool();
  result.snapshot.dataAsOf = std::chrono::system_clock::time_point{std::chrono::seconds{dataAsOf}};
  std::int64_t nonIgnored = 0;
  for (const auto entry : root.value("updates").toArray()) {
    if (!entry.isObject()) {
      return std::nullopt;
    }
    const QJsonObject row = entry.toObject();
    PendingUpdate update;
    std::int64_t download = 0;
    std::int64_t delta = 0;
    if (!string(row, "name", update.name) || !string(row, "installedVersion", update.installedVersion) ||
        !string(row, "availableVersion", update.availableVersion) || !string(row, "repository", update.repository) ||
        !integral(row.value("downloadSizeBytes"), 0, download) ||
        !integral(row.value("installedSizeDeltaBytes"), -kMaxExactInteger, delta) || !row.value("ignored").isBool()) {
      return std::nullopt;
    }
    update.downloadSizeBytes = static_cast<std::uint64_t>(download);
    update.installedSizeDeltaBytes = delta;
    update.ignored = row.value("ignored").toBool();
    if (!update.ignored) {
      ++nonIgnored;
    }
    result.snapshot.updates.push_back(std::move(update));
  }
  if (nonIgnored != count) {
    return std::nullopt;
  }
  return result;
}

std::filesystem::path provenancePath(const std::filesystem::path& snapshot, const QByteArray& bytes) {
  return snapshot.parent_path() /
         ("provenance-" + QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString() + ".json");
}

std::optional<std::chrono::system_clock::time_point> repositoryTimestamp(const QJsonObject& row) {
  std::int64_t seconds = 0;
  if (!integral(row.value("timestamp"), 0, seconds)) {
    return std::nullopt;
  }
  const auto text = row.value("timestampNs").toString();
  bool converted = false;
  const auto nanoseconds = text.toLongLong(&converted);
  if (!converted || nanoseconds < 0 || QString::number(nanoseconds) != text) {
    return std::nullopt;
  }
  const std::chrono::system_clock::time_point time{
      std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds{nanoseconds})};
  return toSeconds(time) == seconds ? std::optional(time) : std::nullopt;
}

void loadProvenance(CheckedSnapshot& snapshot, const std::filesystem::path& file, const QByteArray& bytes) {
  std::error_code metadataError;
  if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(provenancePath(file, bytes), metadataError)) ||
      metadataError) {
    return;
  }
  QFile metadata(QString::fromStdString(provenancePath(file, bytes).string()));
  if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 1024 * 1024) {
    return;
  }
  const auto root = QJsonDocument::fromJson(metadata.readAll()).object();
  const QString generation = root.value("generation").toString();
  if (root.value("version").toInt() != 1 || !generation.startsWith("catalog-") || generation.contains('/') ||
      generation.contains("..") || !root.value("repositories").isArray()) {
    return;
  }
  const auto directory = file.parent_path() / generation.toStdString();
  std::error_code error;
  if (!std::filesystem::is_directory(std::filesystem::symlink_status(directory, error)) || error) {
    return;
  }
  std::vector<holonight_packages_domain::RepositoryProvenance> repositories;
  for (const auto value : root.value("repositories").toArray()) {
    const auto row = value.toObject();
    const auto name = row.value("name").toString();
    const auto identity = row.value("identity").toString();
    const auto digest = row.value("digest").toString();
    const auto timestamp = repositoryTimestamp(row);
    if (name.isEmpty() || name.contains('/') || name.contains("..") || identity.size() != 64 || digest.size() != 64 ||
        !timestamp) {
      return;
    }
    if (std::ranges::any_of(repositories, [&](const auto& repo) { return repo.name == name.toStdString(); }) ||
        row.value("priority").toInt(-1) != static_cast<int>(repositories.size())) {
      return;
    }
    const auto database = directory / (name.toStdString() + ".db");
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(database, error)) || error) {
      return;
    }
    QFile input(QString::fromStdString(database.string()));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!input.open(QIODevice::ReadOnly) || !hash.addData(&input) || hash.result().toHex() != digest.toLatin1()) {
      return;
    }
    repositories.push_back({
        .name = name.toStdString(),
        .identity = identity.toStdString(),
        .digest = digest.toStdString(),
        .timestamp = *timestamp,
        .database = database,
        .checked = true,
    });
  }
  if (!repositories.empty()) {
    snapshot.snapshot.repositories = std::move(repositories);
  }
}

// A hash-keyed published sidecar is immutable, including when two snapshots serialize identically.
std::optional<std::expected<void, SnapshotStoreError>> reusePublishedProvenance(const CheckedSnapshot& snapshot,
                                                                                const std::filesystem::path& file,
                                                                                const QByteArray& bytes) {
  QFile published(QString::fromStdString(file.string()));
  if (published.open(QIODevice::ReadOnly) && published.readAll() == bytes) {
    CheckedSnapshot previous{};
    loadProvenance(previous, file, bytes);
    if (previous.snapshot.repositories.size() == snapshot.snapshot.repositories.size()) {
      bool equivalent = true;
      for (std::size_t index = 0; index < snapshot.snapshot.repositories.size(); ++index) {
        const auto& old = previous.snapshot.repositories[index];
        const auto& next = snapshot.snapshot.repositories[index];
        equivalent = equivalent && old.name == next.name && old.identity == next.identity &&
                     old.digest == next.digest && old.timestamp == next.timestamp;
      }
      if (equivalent) {
        return std::expected<void, SnapshotStoreError>{};
      }
    }
    return storeError("snapshot content hash already identifies a different catalog");
  }
  return std::nullopt;
}

std::expected<void, SnapshotStoreError> saveProvenance(const CheckedSnapshot& snapshot,
                                                       const std::filesystem::path& file, const QByteArray& bytes) {
  if (snapshot.snapshot.repositories.empty()) {
    return {};
  }
  if (const auto reused = reusePublishedProvenance(snapshot, file, bytes)) {
    return *reused;
  }
  const auto generation = "catalog-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  const auto directory = file.parent_path() / generation;
  std::error_code error;
  if (!std::filesystem::create_directory(directory, error) || error) {
    return storeError("cannot create catalog generation");
  }
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace,
                               error);
  QJsonArray repositories;
  for (const auto& repository : snapshot.snapshot.repositories) {
    if (repository.name.empty() || repository.name.contains('/') || repository.name.contains("..")) {
      return storeError("invalid repository identity");
    }
    const auto destination = directory / (repository.name + ".db");
    if (!std::filesystem::copy_file(repository.database, destination, error) || error) {
      return storeError("cannot retain repository database");
    }
    QFile input(QString::fromStdString(destination.string()));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!input.open(QIODevice::ReadOnly) || !hash.addData(&input) ||
        hash.result().toHex().toStdString() != repository.digest) {
      return storeError("repository changed during publication");
    }
    if (::fsync(input.handle()) != 0) {
      return storeError("cannot sync retained repository database");
    }
    repositories.append(QJsonObject{
        {"name", QString::fromStdString(repository.name)},
        {"identity", QString::fromStdString(repository.identity)},
        {"digest", QString::fromStdString(repository.digest)},
        {"priority", repositories.size()},
        {"timestamp", static_cast<double>(toSeconds(repository.timestamp))},
        {
            "timestampNs",
            QString::fromStdString(std::to_string(
                std::chrono::duration_cast<std::chrono::nanoseconds>(repository.timestamp.time_since_epoch()).count())),
        },
    });
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) is variadic.
  const Descriptor catalogDirectory(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (catalogDirectory.fd < 0 || ::fsync(catalogDirectory.fd) != 0) {
    return storeError("cannot sync catalog generation");
  }
  QSaveFile metadata(QString::fromStdString(provenancePath(file, bytes).string()));
  const auto content = QJsonDocument(QJsonObject{
                                         {"version", 1},
                                         {"generation", QString::fromStdString(generation)},
                                         {"repositories", repositories},
                                     })
                           .toJson();
  if (!metadata.open(QIODevice::WriteOnly) || metadata.write(content) != content.size() || !metadata.commit()) {
    return storeError("cannot publish catalog provenance");
  }
  return {};
}

std::string describe(const std::string& what, const std::filesystem::path& path, int error) {
  return what + " " + path.string() + ": " + std::strerror(error);
}

void reclaimUnusedGenerations(const std::filesystem::path& file, const std::filesystem::path& directory,
                              const QByteArray& bytes) {
  std::error_code fsError;
  // Snapshot replacement is the publication point. No reader holds a catalog lease during reclamation.
  const auto currentMetadata = provenancePath(file, bytes);
  QFile metadata(QString::fromStdString(currentMetadata.string()));
  std::string currentGeneration;
  if (metadata.open(QIODevice::ReadOnly)) {
    currentGeneration =
        QJsonDocument::fromJson(metadata.readAll()).object().value("generation").toString().toStdString();
  }
  for (const auto& entry : std::filesystem::directory_iterator(directory, fsError)) {
    const auto name = entry.path().filename().string();
    if (name.starts_with("catalog-") && name != currentGeneration) {
      std::filesystem::remove_all(entry.path(), fsError);
    } else if (name.starts_with("provenance-") && name.ends_with(".json") && entry.path() != currentMetadata) {
      std::filesystem::remove(entry.path(), fsError);
    }
  }
}

}  // namespace

JsonUpdateSnapshotStore::JsonUpdateSnapshotStore(std::filesystem::path file, JsonUpdateSnapshotStoreHooks hooks)
    : file_(std::move(file)), hooks_(std::move(hooks)) {}

std::optional<holonight_packages_domain::CheckHistory> JsonUpdateSnapshotStore::loadHistory() const {
  QFile file(QString::fromStdString(file_.string() + ".history.json"));
  if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) {
    return std::nullopt;
  }
  const auto root = QJsonDocument::fromJson(file.readAll()).object();
  std::int64_t completed = 0;
  if (root.value("version").toInt() != 1 || !integral(root.value("completed"), 0, completed) ||
      !root.value("succeeded").isBool()) {
    return std::nullopt;
  }
  const auto maximum =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::duration::max()).count();
  if (completed > maximum) {
    return std::nullopt;
  }
  holonight_packages_domain::CheckHistory history{
      .completed = std::chrono::system_clock::time_point{std::chrono::seconds{completed}},
      .succeeded = root.value("succeeded").toBool(),
      .error = std::nullopt,
  };
  if (!history.succeeded) {
    for (const auto code : holonight_packages_domain::kAllUpdateCheckErrorCodes) {
      if (root.value("error").toString() == QString::fromUtf8(holonight_packages_domain::updateCheckErrorName(code))) {
        history.error = code;
      }
    }
    if (!history.error) {
      return std::nullopt;
    }
  }
  return history;
}

std::expected<void, SnapshotStoreError> JsonUpdateSnapshotStore::saveHistory(
    const holonight_packages_domain::CheckHistory& history) {
  std::error_code error;
  std::filesystem::create_directories(file_.parent_path(), error);
  if (error) {
    return storeError("cannot create check history directory");
  }
  QSaveFile file(QString::fromStdString(file_.string() + ".history.json"));
  const auto bytes =
      QJsonDocument(
          QJsonObject{
              {"version", 1},
              {"completed", static_cast<double>(toSeconds(history.completed))},
              {"succeeded", history.succeeded},
              {
                  "error",
                  history.error ? QString::fromUtf8(holonight_packages_domain::updateCheckErrorName(*history.error))
                                : QString(),
              },
          })
          .toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    return storeError("cannot save check history");
  }
  return {};
}

std::expected<SnapshotLoad, SnapshotStoreError> JsonUpdateSnapshotStore::load() const {
  const CatalogLock lease(file_);
  std::error_code fsError;
  const auto status = std::filesystem::symlink_status(file_, fsError);
  if (fsError == std::errc::no_such_file_or_directory || (!fsError && !std::filesystem::exists(status))) {
    return SnapshotLoad{.snapshot = std::nullopt, .state = SnapshotFileState::Absent};
  }
  if (fsError == std::errc::not_a_directory) {
    return SnapshotLoad{.snapshot = std::nullopt, .state = SnapshotFileState::Absent};
  }
  if (fsError) {
    return storeError("cannot stat " + file_.string() + ": " + fsError.message());
  }
  if (!std::filesystem::is_regular_file(status)) {
    return SnapshotLoad{.snapshot = std::nullopt, .state = SnapshotFileState::Invalid};
  }
  if (std::filesystem::file_size(file_, fsError) > kMaxFileBytes && !fsError) {
    return SnapshotLoad{.snapshot = std::nullopt, .state = SnapshotFileState::Invalid};
  }

  QFile input(QString::fromStdString(file_.string()));
  if (!input.open(QIODevice::ReadOnly)) {
    if (!input.exists()) {
      return SnapshotLoad{.snapshot = std::nullopt, .state = SnapshotFileState::Absent};
    }
    return storeError("cannot read " + file_.string() + ": " + input.errorString().toStdString());
  }
  const QByteArray bytes = input.read(static_cast<qint64>(kMaxFileBytes) + 1);
  if (std::cmp_greater(bytes.size(), kMaxFileBytes)) {
    return SnapshotLoad{.snapshot = std::nullopt, .state = SnapshotFileState::Invalid};
  }
  auto parsed = parse(bytes);
  if (!parsed.has_value()) {
    return SnapshotLoad{.snapshot = std::nullopt, .state = SnapshotFileState::Invalid};
  }
  if (lease.valid()) {
    loadProvenance(*parsed, file_, bytes);
  }
  return SnapshotLoad{.snapshot = std::move(parsed), .state = SnapshotFileState::Valid};
}

std::expected<void, SnapshotStoreError> JsonUpdateSnapshotStore::save(const CheckedSnapshot& snapshot) {
  const std::filesystem::path directory =
      file_.parent_path().empty() ? std::filesystem::path(".") : file_.parent_path();
  std::error_code fsError;
  const bool created = std::filesystem::create_directories(directory, fsError);
  if (fsError) {
    return storeError("cannot create " + directory.string() + ": " + fsError.message());
  }
  if (created) {
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace,
                                 fsError);
  }

  std::unique_ptr<CatalogLock> lease;
  if (!snapshot.snapshot.repositories.empty() || std::filesystem::exists(directory / ".catalog.lock", fsError)) {
    lease = std::make_unique<CatalogLock>(file_, true);
    if (!lease->valid()) {
      return storeError("cannot lock catalog cache");
    }
  }
  const QByteArray bytes = serialize(snapshot);
  if (const auto provenance = saveProvenance(snapshot, file_, bytes); !provenance) {
    return provenance;
  }
  std::string pattern = (directory / ".update-snapshot-XXXXXX").string();
  Descriptor temporary(::mkstemp(pattern.data()));
  if (temporary.fd < 0) {
    return storeError(describe("cannot create a temporary file in", directory, errno));
  }
  const std::filesystem::path temporaryPath(pattern);
  const auto fail = [&temporaryPath](const std::string& message) {
    std::error_code ignored;
    std::filesystem::remove(temporaryPath, ignored);
    return storeError(message);
  };

  std::span<const char> pending(bytes.constData(), static_cast<std::size_t>(bytes.size()));
  while (!pending.empty()) {
    const ssize_t result = ::write(temporary.fd, pending.data(), pending.size());
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return fail(describe("cannot write", temporaryPath, errno));
    }
    pending = pending.subspan(static_cast<std::size_t>(result));
  }
  if (::fsync(temporary.fd) != 0) {
    return fail(describe("cannot sync", temporaryPath, errno));
  }
  if (hooks_.beforeRename && !hooks_.beforeRename()) {
    return fail("injected failure before rename");
  }
  if (::rename(temporaryPath.c_str(), file_.c_str()) != 0) {
    return fail(describe("cannot rename onto", file_, errno));
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): open(2) is variadic.
  Descriptor directoryDescriptor(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (directoryDescriptor.fd >= 0) {
    (void)::fsync(directoryDescriptor.fd);
  }
  reclaimUnusedGenerations(file_, directory, bytes);
  return {};
}

std::expected<void, SnapshotStoreError> JsonUpdateSnapshotStore::discardInvalid() {
  const auto loaded = load();
  if (!loaded) {
    return std::unexpected(loaded.error());
  }
  if (loaded->state != SnapshotFileState::Invalid) {
    return {};
  }
  std::error_code fsError;
  std::filesystem::remove(file_, fsError);
  if (fsError) {
    return storeError("cannot remove " + file_.string() + ": " + fsError.message());
  }
  return {};
}

}  // namespace holonight_packages_persistence
