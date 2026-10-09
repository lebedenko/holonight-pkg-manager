#include "holonight_packages_persistence/json_update_snapshot_store.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
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

std::string describe(const std::string& what, const std::filesystem::path& path, int error) {
  return what + " " + path.string() + ": " + std::strerror(error);
}

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

}  // namespace

JsonUpdateSnapshotStore::JsonUpdateSnapshotStore(std::filesystem::path file, JsonUpdateSnapshotStoreHooks hooks)
    : file_(std::move(file)), hooks_(std::move(hooks)) {}

std::expected<SnapshotLoad, SnapshotStoreError> JsonUpdateSnapshotStore::load() const {
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

  const QByteArray bytes = serialize(snapshot);
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
