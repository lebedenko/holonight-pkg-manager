#pragma once

#include "holonight_packages_application/update_summary.h"
#include "holonight_packages_domain/pending_update.h"
#include "holonight_packages_domain/update_source.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQmlIntegration/qqmlintegration.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <vector>

// Pending official-repository updates for the Updates page. The initial load starts in the constructor; reload()
// re-runs the same read-only comparison so databases the user synced outside the application are picked up.
class UpdatesModel : public QAbstractListModel {
  Q_OBJECT
  QML_NAMED_ELEMENT(UpdatesModel)
  QML_UNCREATABLE("UpdatesModel is provided by the application")
  Q_PROPERTY(ViewState state READ state NOTIFY stateChanged)
  // Initial-load failure (state Error) only; reload failures with a previous list use reloadErrorMessage.
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
  // True while the initial load or a reload is in flight.
  Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
  Q_PROPERTY(int updateCount READ updateCount NOTIFY stateChanged)
  Q_PROPERTY(int ignoredCount READ ignoredCount NOTIFY stateChanged)
  Q_PROPERTY(quint64 totalDownloadBytes READ totalDownloadBytes NOTIFY stateChanged)
  Q_PROPERTY(QString totalDownloadLabel READ totalDownloadLabel NOTIFY stateChanged)
  // Modification time of the oldest sync database; invalid when there is no data.
  Q_PROPERTY(QDateTime dataAsOf READ dataAsOf NOTIFY stateChanged)
  Q_PROPERTY(QString dataAsOfLabel READ dataAsOfLabel NOTIFY stateChanged)
  Q_PROPERTY(bool databasesStale READ databasesStale NOTIFY stateChanged)
  // Empty unless the latest reload failed while a previous list is still shown.
  Q_PROPERTY(QString reloadErrorMessage READ reloadErrorMessage NOTIFY stateChanged)
  Q_PROPERTY(QString staleHintText READ staleHintText CONSTANT)
  Q_PROPERTY(QString officialOnlyNote READ officialOnlyNote CONSTANT)

 public:
  // Loading is used only when there is no previous result to keep showing.
  enum class ViewState : std::uint8_t { Loading, Updates, UpToDate, NoDatabases, Error };
  Q_ENUM(ViewState)

  enum Role : std::uint16_t {
    NameRole = Qt::UserRole + 1,
    InstalledVersionRole,
    AvailableVersionRole,
    RepositoryRole,
    DownloadSizeRole,
    DownloadSizeLabelRole,
    SizeDeltaRole,
    SizeDeltaLabelRole,
    IsIgnoredRole,
  };

  using Clock = std::function<std::chrono::system_clock::time_point()>;

  // `now` is injectable for deterministic staleness tests.
  explicit UpdatesModel(std::shared_ptr<holonight_packages_domain::UpdateSource> source, QObject* parent = nullptr,
                        Clock now = &std::chrono::system_clock::now);
  ~UpdatesModel() override;

  UpdatesModel(const UpdatesModel&) = delete;
  UpdatesModel& operator=(const UpdatesModel&) = delete;
  UpdatesModel(UpdatesModel&&) = delete;
  UpdatesModel& operator=(UpdatesModel&&) = delete;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] ViewState state() const;
  [[nodiscard]] QString errorMessage() const;
  [[nodiscard]] bool loading() const;
  [[nodiscard]] int updateCount() const;
  [[nodiscard]] int ignoredCount() const;
  [[nodiscard]] quint64 totalDownloadBytes() const;
  [[nodiscard]] QString totalDownloadLabel() const;
  [[nodiscard]] QDateTime dataAsOf() const;
  [[nodiscard]] QString dataAsOfLabel() const;
  [[nodiscard]] bool databasesStale() const;
  [[nodiscard]] QString reloadErrorMessage() const;
  [[nodiscard]] static QString staleHintText();
  [[nodiscard]] static QString officialOnlyNote();

  // Single-flight: ignored while a load or reload is in progress. Never retried automatically.
  Q_INVOKABLE void reload();

 signals:
  void stateChanged();

 private:
  using LoadResult =
      std::expected<holonight_packages_domain::UpdateSnapshot, holonight_packages_domain::UpdateSourceError>;

  void startLoading();
  void onLoadFinished();
  void applySnapshot(holonight_packages_domain::UpdateSnapshot snapshot);
  void applyFailure(const holonight_packages_domain::UpdateSourceError& error);
  [[nodiscard]] bool hasResult() const;

  std::shared_ptr<holonight_packages_domain::UpdateSource> source_;
  Clock now_;
  QFutureWatcher<LoadResult> watcher_;
  std::vector<holonight_packages_domain::PendingUpdate> updates_;
  holonight_packages_application::UpdateSummary summary_;
  bool loading_ = false;
  ViewState state_ = ViewState::Loading;
  QString error_message_;
  QString reload_error_message_;
  QDateTime data_as_of_;
  bool databases_stale_ = false;
};
