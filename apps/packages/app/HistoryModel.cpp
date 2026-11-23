#include "HistoryModel.h"

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <chrono>
#include <cstddef>
#include <exception>
#include <iterator>
#include <string>
#include <unistd.h>
#include <utility>

using holonight_packages_domain::HistoryAction;
using holonight_packages_domain::HistoryActionKind;
using holonight_packages_domain::HistoryEntry;
using holonight_packages_domain::HistoryOriginKind;
using holonight_packages_domain::HistoryReadMode;
using holonight_packages_domain::HistorySeverity;
using holonight_packages_domain::HistoryStatus;
namespace presentation = holonight_packages_application;

namespace {

QString qs(const std::string& text) { return QString::fromStdString(text); }
QString qs(std::string_view text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }

QString originKindName(HistoryOriginKind kind) {
  switch (kind) {
    case HistoryOriginKind::Packages:
      return QStringLiteral("packages");
    case HistoryOriginKind::Pacman:
      return QStringLiteral("pacman");
    case HistoryOriginKind::Unknown:
      break;
  }
  return QStringLiteral("unknown");
}

QString statusKindName(HistoryStatus status) {
  switch (status) {
    case HistoryStatus::Completed:
      return QStringLiteral("completed");
    case HistoryStatus::InProgress:
      return QStringLiteral("inProgress");
    case HistoryStatus::Interrupted:
      break;
  }
  return QStringLiteral("interrupted");
}

QString versionText(const HistoryAction& action) {
  if (action.kind == HistoryActionKind::Upgraded || action.kind == HistoryActionKind::Downgraded) {
    return qs(action.old_version) + QStringLiteral(" → ") + qs(action.new_version);
  }
  return qs(action.kind == HistoryActionKind::Removed ? action.old_version : action.new_version);
}

QVariantList actionList(const HistoryEntry& entry) {
  QVariantList actions;
  actions.reserve(static_cast<qsizetype>(entry.actions.size()));
  for (const HistoryAction& action : entry.actions) {
    actions.append(QVariantMap{
        {QStringLiteral("kind"), qs(presentation::actionKindLabel(action.kind))},
        {QStringLiteral("kindLabel"), qs(presentation::actionKindLabel(action.kind))},
        {QStringLiteral("name"), qs(action.name)},
        {QStringLiteral("oldVersion"), qs(action.old_version)},
        {QStringLiteral("newVersion"), qs(action.new_version)},
        {QStringLiteral("versionText"), versionText(action)},
    });
  }
  return actions;
}

QVariantList messageList(const HistoryEntry& entry) {
  QVariantList messages;
  for (const auto& message : entry.messages) {
    const QString severity =
        message.severity == HistorySeverity::Error ? QStringLiteral("error") : QStringLiteral("warning");
    messages.append(QVariantMap{
        {QStringLiteral("severity"), severity},
        {QStringLiteral("text"), qs(message.text)},
    });
  }
  return messages;
}

QStringList stringList(const std::vector<std::string>& values) {
  QStringList list;
  list.reserve(static_cast<qsizetype>(values.size()));
  for (const std::string& value : values) {
    list.append(qs(value));
  }
  return list;
}

QStringList packageNames(const HistoryEntry& entry) {
  QStringList names;
  QSet<QString> seen;
  for (const HistoryAction& action : entry.actions) {
    const QString name = qs(action.name);
    if (!seen.contains(name)) {
      seen.insert(name);
      names.append(name);
    }
  }
  return names;
}

}  // namespace

HistoryModelOptions HistoryModelOptions::system() {
  return {.current_uid = static_cast<std::uint32_t>(getuid()), .resolve_user = presentation::systemUserName};
}

HistoryModel::HistoryModel(std::shared_ptr<holonight_packages_domain::HistorySource> source,
                           HistoryModelOptions options, QObject* parent)
    : QAbstractListModel(parent), source_(std::move(source)), options_(std::move(options)) {
  connect(&watcher_, &QFutureWatcher<ReadResult>::finished, this, &HistoryModel::onReadFinished);
}

HistoryModel::~HistoryModel() = default;

int HistoryModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant HistoryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= entries_.size()) {
    return {};
  }
  // Row 0 is the newest entry (D-10).
  const std::size_t sequence = entries_.size() - 1 - static_cast<std::size_t>(index.row());
  const HistoryEntry& entry = entries_[sequence];
  switch (role) {
    case SequenceRole:
      return QVariant::fromValue(static_cast<quint32>(sequence));
    case StartedAtRole:
      return QDateTime::fromSecsSinceEpoch(
          std::chrono::duration_cast<std::chrono::seconds>(entry.started.time_since_epoch()).count());
    case DayKeyRole:
      return qs(presentation::dayKey(presentation::localDay(entry.started)));
    case TimeLabelRole:
      return qs(presentation::timeLabel(entry.started));
    case OriginKindRole:
      return originKindName(entry.origin.kind);
    case OriginLabelRole:
      return qs(presentation::originLabel(entry.origin, options_.current_uid,
                                          [this](std::uint32_t uid) { return user_names_.value(uid); }));
    case CommandLineRole:
      return entry.origin.kind == HistoryOriginKind::Pacman ? qs(entry.origin.command) : QString();
    case SummaryRole:
      return qs(presentation::summaryText(entry.actions));
    case StatusKindRole:
      return statusKindName(entry.status);
    case StatusLabelRole:
      return qs(presentation::statusLabel(entry.status));
    case ActionsRole:
      return actionList(entry);
    case PreHooksRole:
      return stringList(entry.pre_hooks);
    case HooksRole:
      return stringList(entry.hooks);
    case MessagesRole:
      return messageList(entry);
    case PackageNamesRole:
      return package_names_[sequence];
    default:
      return {};
  }
}

QHash<int, QByteArray> HistoryModel::roleNames() const {
  return {
      {SequenceRole, "sequence"},
      {StartedAtRole, "startedAt"},
      {DayKeyRole, "dayKey"},
      {TimeLabelRole, "timeLabel"},
      {OriginKindRole, "originKind"},
      {OriginLabelRole, "originLabel"},
      {CommandLineRole, "commandLine"},
      {SummaryRole, "summary"},
      {StatusKindRole, "statusKind"},
      {StatusLabelRole, "statusLabel"},
      {ActionsRole, "actions"},
      {PreHooksRole, "preHooks"},
      {HooksRole, "hooks"},
      {MessagesRole, "messages"},
      {PackageNamesRole, "packageNames"},
  };
}

bool HistoryModel::loading() const { return loading_; }
bool HistoryModel::loaded() const { return loaded_; }
QString HistoryModel::emptyReason() const { return empty_reason_; }
int HistoryModel::entryCount() const { return static_cast<int>(entries_.size()); }

void HistoryModel::ensureLoaded() {
  if (load_requested_) {
    return;
  }
  load_requested_ = true;
  startRead(HistoryReadMode::Full);
}

void HistoryModel::refresh() {
  if (!load_requested_) {
    ensureLoaded();
    return;
  }
  if (loading_) {
    refresh_pending_ = true;
    return;
  }
  startRead(HistoryReadMode::Incremental);
}

void HistoryModel::refreshIfLoaded() {
  if (load_requested_) {
    refresh();
  }
}

QString HistoryModel::formatDay(const QString& day_key) {
  const QDate date = QDate::fromString(day_key, Qt::ISODate);
  return date.isValid() ? QLocale().toString(date, QLocale::LongFormat) : day_key;
}

void HistoryModel::startRead(HistoryReadMode mode) {
  loading_ = true;
  emit stateChanged();
  watcher_.setFuture(QtConcurrent::run([source = source_, mode, current_uid = options_.current_uid] {
    ReadResult result;
    try {
      result.history = source->read(mode);
    } catch (const std::exception& exception) {
      result.history.reset = true;
      result.history.reason = std::string("Reading the pacman log failed: ") + exception.what();
    } catch (...) {
      result.history.reset = true;
      result.history.reason = "Reading the pacman log failed.";
    }
    result.package_names.reserve(result.history.append.size());
    for (const auto& entry : result.history.append) {
      result.package_names.push_back(packageNames(entry));
      if (entry.origin.kind == HistoryOriginKind::Packages && entry.origin.uid && *entry.origin.uid != current_uid) {
        result.foreign_users.insert(*entry.origin.uid);
      }
    }
    return result;
  }));
}

void HistoryModel::onReadFinished() {
  apply(watcher_.future().takeResult());
  loading_ = false;
  loaded_ = true;
  emit stateChanged();
  if (std::exchange(refresh_pending_, false)) {
    startRead(HistoryReadMode::Incremental);
  }
}

void HistoryModel::apply(ReadResult loaded) {
  auto& result = loaded.history;
  resolveUsers(loaded.foreign_users);
  empty_reason_ = qs(result.reason);
  if (result.reset) {
    beginResetModel();
    entries_ = std::move(result.append);
    package_names_ = std::move(loaded.package_names);
    endResetModel();
    return;
  }
  auto first_new = result.append.begin();
  auto first_names = loaded.package_names.begin();
  if (result.drop_last > 0 && !entries_.empty()) {
    if (first_new != result.append.end()) {
      // The revised last entry replaces the old one in place, so its row and the selection survive (D-12).
      entries_.back() = std::move(*first_new);
      package_names_.back() = std::move(*first_names);
      ++first_names;
      ++first_new;
      emit dataChanged(index(0), index(0));
    } else {
      beginRemoveRows({}, 0, 0);
      entries_.pop_back();
      package_names_.pop_back();
      endRemoveRows();
    }
  }
  if (first_new != result.append.end()) {
    const int count = static_cast<int>(std::distance(first_new, result.append.end()));
    beginInsertRows({}, 0, count - 1);
    entries_.insert(entries_.end(), std::make_move_iterator(first_new), std::make_move_iterator(result.append.end()));
    package_names_.insert(package_names_.end(), std::make_move_iterator(first_names),
                          std::make_move_iterator(loaded.package_names.end()));
    endInsertRows();
  }
}

void HistoryModel::resolveUsers(const QSet<std::uint32_t>& users) {
  if (!options_.resolve_user) {
    return;
  }
  using UserName = std::optional<std::string>;
  for (auto uid : users) {
    if (user_names_.contains(uid)) {
      continue;
    }
    user_names_.insert(uid, std::nullopt);
    auto* watcher = new QFutureWatcher<UserName>(this);
    connect(watcher, &QFutureWatcher<UserName>::finished, this, [this, uid, watcher] {
      auto name = watcher->future().takeResult();
      watcher->deleteLater();
      if (name && !name->empty()) {
        applyUserName(uid, std::move(*name));
      }
    });
    watcher->setFuture(QtConcurrent::run([resolve = options_.resolve_user, uid] -> UserName {
      try {
        return resolve(uid);
      } catch (...) {
        return std::nullopt;
      }
    }));
  }
}

void HistoryModel::applyUserName(std::uint32_t uid, std::string name) {
  user_names_.insert(uid, std::move(name));
  int first = -1;
  const int count = rowCount();
  for (int row = 0; row < count; ++row) {
    const auto& origin = entries_[entries_.size() - 1 - static_cast<std::size_t>(row)].origin;
    if (origin.kind == HistoryOriginKind::Packages && origin.uid == uid) {
      if (first < 0) {
        first = row;
      }
    } else if (first >= 0) {
      emit dataChanged(index(first), index(row - 1), {OriginLabelRole});
      first = -1;
    }
  }
  if (first >= 0) {
    emit dataChanged(index(first), index(count - 1), {OriginLabelRole});
  }
}
