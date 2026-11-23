#pragma once

#include "holonight_packages_application/history_presentation.h"
#include "holonight_packages_domain/history_source.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QFutureWatcher>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtQmlIntegration/qqmlintegration.h>

#include <cstdint>
#include <memory>
#include <vector>

struct HistoryModelOptions {
  std::uint32_t current_uid;  // the user that runs the app; their own runs are labelled "Packages"
  holonight_packages_application::UserNameResolver resolve_user;

  [[nodiscard]] static HistoryModelOptions system();
};

// The transactions of the pacman log for the History page, newest first (transaction-history DESIGN 5.6). Reads run on
// a QtConcurrent worker through the HistorySource port only (REQ-F-022, REQ-F-034); nothing is read until
// ensureLoaded() (REQ-F-017).
class HistoryModel : public QAbstractListModel {
  Q_OBJECT
  QML_NAMED_ELEMENT(HistoryModel)
  QML_UNCREATABLE("HistoryModel is provided by the application")
  Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
  Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged)
  Q_PROPERTY(QString emptyReason READ emptyReason NOTIFY stateChanged)
  Q_PROPERTY(int entryCount READ entryCount NOTIFY stateChanged)

 public:
  // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): preserve Qt model role constants.
  enum Role : std::uint16_t {
    SequenceRole = Qt::UserRole + 1,
    StartedAtRole,
    DayKeyRole,
    TimeLabelRole,
    OriginKindRole,
    OriginLabelRole,
    CommandLineRole,
    SummaryRole,
    StatusKindRole,
    StatusLabelRole,
    ActionsRole,
    PreHooksRole,
    HooksRole,
    MessagesRole,
    PackageNamesRole,
  };

  explicit HistoryModel(std::shared_ptr<holonight_packages_domain::HistorySource> source,
                        HistoryModelOptions options = HistoryModelOptions::system(), QObject* parent = nullptr);
  HistoryModel(const HistoryModel&) = delete;
  HistoryModel(HistoryModel&&) = delete;
  HistoryModel& operator=(const HistoryModel&) = delete;
  HistoryModel& operator=(HistoryModel&&) = delete;
  ~HistoryModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] bool loading() const;
  [[nodiscard]] bool loaded() const;
  [[nodiscard]] QString emptyReason() const;
  [[nodiscard]] int entryCount() const;

  // The first call starts a full read; later calls do nothing.
  Q_INVOKABLE void ensureLoaded();
  // Starts an incremental read, or queues one repeat while a read runs. Before the first load it loads.
  Q_INVOKABLE void refresh();
  // What the post-transaction refresh calls: nothing before the page was first opened, else refresh().
  void refreshIfLoaded();

  // A locale long date for a "YYYY-MM-DD" section key.
  Q_INVOKABLE [[nodiscard]] static QString formatDay(const QString& day_key);

 signals:
  void stateChanged();

 private:
  void startRead(holonight_packages_domain::HistoryReadMode mode);
  void onReadFinished();
  struct ReadResult {
    holonight_packages_domain::HistoryReadResult history;
    std::vector<QStringList> package_names;
    QSet<std::uint32_t> foreign_users;
  };

  void apply(ReadResult loaded);
  void resolveUsers(const QSet<std::uint32_t>& users);
  void applyUserName(std::uint32_t uid, std::string name);

  std::shared_ptr<holonight_packages_domain::HistorySource> source_;
  HistoryModelOptions options_;
  std::vector<holonight_packages_domain::HistoryEntry> entries_;  // file order, oldest first
  std::vector<QStringList> package_names_;
  QHash<std::uint32_t, std::optional<std::string>> user_names_;
  QFutureWatcher<ReadResult> watcher_;
  QString empty_reason_;
  bool load_requested_ = false;
  bool loading_ = false;
  bool loaded_ = false;
  bool refresh_pending_ = false;
};
