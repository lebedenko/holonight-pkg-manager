#include "UpdateCheckModel.h"

#include "holonight_packages_application/update_age_format.h"
#include "holonight_packages_domain/update_checker.h"

#include <QLocale>

#include <utility>

namespace {

using holonight_packages_domain::UpdateCheckErrorCode;

constexpr std::chrono::seconds kAgeRefresh{60};

std::optional<UpdateCheckErrorCode> codeForToken(const QString& token) {
  for (const UpdateCheckErrorCode code : holonight_packages_domain::kAllUpdateCheckErrorCodes) {
    if (token == QString::fromUtf8(holonight_packages_domain::updateCheckErrorName(code))) {
      return code;
    }
  }
  return std::nullopt;
}

QDateTime toDateTime(std::chrono::system_clock::time_point time) {
  return QDateTime::fromSecsSinceEpoch(
      std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
}

}  // namespace

UpdateCheckModel::UpdateCheckModel(UpdateCheckClient* client, SnapshotFileReader* reader, UpdatesModel* updates,
                                   Clock now, std::unique_ptr<holonight_packages_application::OneShotTimer> ageTimer,
                                   QObject* parent)
    : QObject(parent),
      client_(client),
      reader_(reader),
      updates_(updates),
      now_(std::move(now)),
      age_timer_(std::move(ageTimer)) {
  connect(client_, &UpdateCheckClient::statusChanged, this, [this] { onClientStatusChanged(); });
  connect(client_, &UpdateCheckClient::checkCompleted, this, [this] {
    service_unavailable_ = false;
    reader_->readNow();
    emit changed();
  });
  connect(client_, &UpdateCheckClient::checkNowFailed, this, [this] {
    service_unavailable_ = true;
    emit changed();
  });
  connect(reader_, &SnapshotFileReader::snapshotRead, this,
          [this](const holonight_packages_domain::CheckedSnapshot& snapshot) {
            updates_->applyCheckedSnapshot(snapshot);
            emit changed();
          });
  connect(client_, &UpdateCheckClient::checkNowSucceeded, this, [this] {
    service_unavailable_ = false;
    emit changed();
  });
  connect(reader_, &SnapshotFileReader::snapshotInvalidated, updates_, &UpdatesModel::invalidateCheckedSnapshot);
  connect(updates_, &UpdatesModel::stateChanged, this, &UpdateCheckModel::changed);
  armAgeTimer();
}

void UpdateCheckModel::onClientStatusChanged() {
  // Any word from the service means it is reachable again.
  service_unavailable_ = false;
  reader_->readNow();
  emit changed();
}

void UpdateCheckModel::armAgeTimer() {
  age_timer_->start(std::chrono::duration_cast<std::chrono::milliseconds>(kAgeRefresh), [this] {
    emit changed();
    armAgeTimer();
  });
}

bool UpdateCheckModel::available() const {
  const UpdateCheckClientStatus& status = client_->status();
  return !status.serviceReachable || status.canCheck;
}

bool UpdateCheckModel::checking() const { return client_->status().checking; }

bool UpdateCheckModel::checkNowEnabled() const { return available() && !checking(); }

std::optional<std::chrono::system_clock::time_point> UpdateCheckModel::fetchedAt() const {
  return updates_->displayedFetchedAt();
}

bool UpdateCheckModel::hasSnapshot() const { return updates_->hasCheckedSnapshot(); }

QDateTime UpdateCheckModel::snapshotFetchedAt() const {
  return client_->status().snapshotFetchedAt == 0 ? QDateTime()
                                                  : QDateTime::fromSecsSinceEpoch(client_->status().snapshotFetchedAt);
}

QDateTime UpdateCheckModel::displayedSnapshotFetchedAt() const {
  const auto time = fetchedAt();
  return time ? toDateTime(*time) : QDateTime();
}

QString UpdateCheckModel::snapshotAgeText() const {
  const auto time = fetchedAt();
  if (!time.has_value()) {
    return {};
  }
  const QString age = QString::fromStdString(holonight_packages_application::formatSnapshotAge(now_(), *time));
  if (age == QLatin1String("just now")) {
    return tr("Checked data saved just now");
  }
  return tr("Checked data saved %1 ago").arg(age);
}

QString UpdateCheckModel::statusLineText() const {
  if (!hasSnapshot()) {
    return tr("Not checked yet");
  }
  return fetchedAt() ? snapshotAgeText() : tr("Showing local package data");
}

QString UpdateCheckModel::checkHistoryText() const {
  if (!lastCheckTime().isValid()) {
    return hasSnapshot() ? tr("Check history unavailable") : tr("Not checked yet");
  }
  return lastCheckSucceeded()
             ? tr("Last check succeeded: %1").arg(QLocale().toString(lastCheckTime(), QLocale::ShortFormat))
             : tr("Last check failed: %1").arg(QLocale().toString(lastCheckTime(), QLocale::ShortFormat));
}

QDateTime UpdateCheckModel::lastCheckTime() const {
  return client_->status().lastCheckTime == 0 ? QDateTime()
                                              : QDateTime::fromSecsSinceEpoch(client_->status().lastCheckTime);
}

bool UpdateCheckModel::lastCheckSucceeded() const {
  return client_->status().lastCheckTime != 0 && client_->status().lastCheckSucceeded;
}

bool UpdateCheckModel::failed() const {
  return client_->status().lastCheckTime != 0 && !client_->status().lastCheckSucceeded;
}

QString UpdateCheckModel::lastErrorCode() const { return failed() ? client_->status().lastCheckError : QString(); }

QString UpdateCheckModel::lastErrorMessage() const {
  if (!failed()) {
    return {};
  }
  const auto code = codeForToken(client_->status().lastCheckError);
  return QString::fromUtf8(
      holonight_packages_domain::updateCheckErrorMessage(code.value_or(UpdateCheckErrorCode::Unknown)));
}

QString UpdateCheckModel::failureText() const {
  if (service_unavailable_) {
    return tr("Update service unavailable");
  }
  if (failed()) {
    return tr("Last check failed: %1").arg(lastErrorMessage());
  }
  return {};
}

void UpdateCheckModel::checkNow() { client_->checkNow(); }
