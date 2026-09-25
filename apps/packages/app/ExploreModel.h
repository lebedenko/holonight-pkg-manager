#pragma once

#include "holonight_packages_application/explore_search.h"
#include "holonight_packages_domain/explore_source.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <vector>

// Read-only search over the packages in the configured sync databases. The initial load starts in the constructor and
// builds the search index off the GUI thread; reload() repeats it so databases synced outside the application are
// picked up. Searching, selection and state live here so they survive page navigation; nothing is persisted.
class ExploreModel : public QAbstractListModel {
  Q_OBJECT
  QML_NAMED_ELEMENT(ExploreModel)
  QML_UNCREATABLE("ExploreModel is provided by the application")
  Q_PROPERTY(ViewState state READ state NOTIFY stateChanged)
  // True while the initial load or a reload is in flight.
  Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
  // An index exists to search: false while Loading, NoDatabases and Error.
  Q_PROPERTY(bool searchEnabled READ searchEnabled NOTIFY stateChanged)
  // Initial-load failure (state Error) only; reload failures with an index use reloadErrorMessage.
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
  // Empty unless the latest reload failed while an index is still searchable.
  Q_PROPERTY(QString reloadErrorMessage READ reloadErrorMessage NOTIFY stateChanged)
  // Raw text of the search field.
  Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
  // Total matches before the cap.
  Q_PROPERTY(int matchCount READ matchCount NOTIFY stateChanged)
  // Empty unless matchCount exceeds the rows shown.
  Q_PROPERTY(QString footerText READ footerText NOTIFY stateChanged)
  Q_PROPERTY(QString noMatchesText READ noMatchesText NOTIFY stateChanged)
  // Modification time of the oldest sync database; invalid when there is no data.
  Q_PROPERTY(QDateTime dataAsOf READ dataAsOf NOTIFY stateChanged)
  Q_PROPERTY(QString dataAsOfLabel READ dataAsOfLabel NOTIFY stateChanged)
  Q_PROPERTY(bool databasesStale READ databasesStale NOTIFY stateChanged)
  Q_PROPERTY(QString staleHintText READ staleHintText CONSTANT)
  Q_PROPERTY(QString repositoryScopeNote READ repositoryScopeNote CONSTANT)
  Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)
  Q_PROPERTY(QVariantMap currentPackage READ currentPackage NOTIFY currentPackageChanged)

 public:
  // Loading is used only while there is no index to keep searching.
  enum class ViewState : std::uint8_t { Loading, NoDatabases, Error, Hint, NoMatches, Results };
  Q_ENUM(ViewState)

  enum Role : std::uint16_t {
    NameRole = Qt::UserRole + 1,
    AvailableVersionRole,
    RepositoryRole,
    DescriptionRole,
    DownloadSizeRole,
    DownloadSizeLabelRole,
    InstalledSizeRole,
    InstalledSizeLabelRole,
    UrlRole,
    LicensesRole,
    DependenciesRole,
    OptionalDependenciesRole,
    IsInstalledRole,
    InstalledVersionRole,
    InstalledBadgeTextRole,
    InstalledVersionDiffersRole,
  };

  using Clock = std::function<std::chrono::system_clock::time_point()>;

  static constexpr std::chrono::milliseconds kDefaultSearchDebounce{150};

  // `now` and `debounce` are injectable for deterministic staleness and debounce tests.
  explicit ExploreModel(std::shared_ptr<holonight_packages_domain::ExploreSource> source, QObject* parent = nullptr,
                        Clock now = &std::chrono::system_clock::now,
                        std::chrono::milliseconds debounce = kDefaultSearchDebounce);
  ~ExploreModel() override;

  ExploreModel(const ExploreModel&) = delete;
  ExploreModel& operator=(const ExploreModel&) = delete;
  ExploreModel(ExploreModel&&) = delete;
  ExploreModel& operator=(ExploreModel&&) = delete;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] ViewState state() const;
  [[nodiscard]] bool loading() const;
  [[nodiscard]] bool searchEnabled() const;
  [[nodiscard]] QString errorMessage() const;
  [[nodiscard]] QString reloadErrorMessage() const;
  [[nodiscard]] QString searchText() const;
  [[nodiscard]] int matchCount() const;
  [[nodiscard]] QString footerText() const;
  [[nodiscard]] QString noMatchesText() const;
  [[nodiscard]] QDateTime dataAsOf() const;
  [[nodiscard]] QString dataAsOfLabel() const;
  [[nodiscard]] bool databasesStale() const;
  [[nodiscard]] static QString staleHintText();
  [[nodiscard]] static QString repositoryScopeNote();
  [[nodiscard]] int currentRow() const;
  [[nodiscard]] QVariantMap currentPackage() const;
  // Number of searches actually executed; diagnostics and debounce tests.
  [[nodiscard]] int searchCount() const;

  // Stores the raw text immediately and (re)starts the debounce timer. No-op when unchanged.
  void setSearchText(const QString& text);
  // Out-of-range rows select nothing (-1).
  void setCurrentRow(int row);

  // Single-flight: ignored while a load or reload is in progress. Never retried automatically.
  Q_INVOKABLE void reload();

 signals:
  void stateChanged();
  void searchTextChanged();
  void currentRowChanged();
  void currentPackageChanged();

 private:
  // Copyable, as QFutureWatcher requires.
  struct LoadedIndex {
    std::shared_ptr<const holonight_packages_application::ExploreIndex> index;
    bool databasesFound = true;
    std::chrono::system_clock::time_point dataAsOf;
  };
  using LoadResult = std::expected<LoadedIndex, holonight_packages_domain::ExploreSourceError>;

  void startLoading();
  void onLoadFinished();
  void applyLoaded(LoadedIndex loaded);
  void applyFailure(const holonight_packages_domain::ExploreSourceError& error);
  void runQuery(std::shared_ptr<const holonight_packages_application::ExploreIndex> replacement = nullptr);
  void setResults(std::shared_ptr<const holonight_packages_application::ExploreIndex> index,
                  std::vector<std::uint32_t> rows);
  void reconcileSelection();
  void setSelection(int row);
  [[nodiscard]] const holonight_packages_domain::SyncPackage* packageAt(int row) const;
  [[nodiscard]] QVariantMap packageMap(int row) const;
  [[nodiscard]] QString identityAt(int row) const;

  std::shared_ptr<holonight_packages_domain::ExploreSource> source_;
  Clock now_;
  QFutureWatcher<LoadResult> watcher_;
  QTimer search_timer_;
  std::shared_ptr<const holonight_packages_application::ExploreIndex> index_;
  std::vector<std::uint32_t> results_;
  QString search_text_;
  // Trimmed query the current rows correspond to.
  QString applied_query_;
  std::size_t match_count_ = 0;
  bool loading_ = false;
  ViewState state_ = ViewState::Loading;
  QString error_message_;
  QString reload_error_message_;
  QDateTime data_as_of_;
  bool databases_stale_ = false;
  int search_count_ = 0;
  int current_row_ = -1;
  // repository + "/" + name of the selected package, so the selection survives result swaps.
  QString selected_identity_;
  QVariantMap current_package_;
};
