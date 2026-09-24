# Pending Updates Detection and Display — Design

**Feature**: Read-only detection of pending official-repository package updates (libalpm) by comparing installed
packages against the current live sync databases, surfaced on a new Updates page with a local "Reload" button.

**Status**: Design (Stage 2 of SDD cycle)

**Date**: 2026-09-24

**Input**: `docs/sdd/pending-updates/SPEC.md` v1.2 (EARS requirements, referenced throughout as `REQ-*`)

**Baseline this design extends**: `docs/sdd/installed-packages-list/DESIGN.md` (port in `domain`, `AlpmPackageSource`,
model in `apps/packages/app/`, `QtConcurrent::run` + `QFutureWatcher`), `docs/sdd/alpm-sync-db-cache/DESIGN.md`
(`AlpmConnectionCache` lease), `docs/sdd/installed-page-ui/DESIGN.md` (sidebar, `HnListDelegate` rows).

---

## 0. Findings that shape the design

Facts about the current code base and about libalpm on this machine (pacman 7.1.0.r9, libalpm 16.0.1) that the rest of
the document depends on. "Verified" means observed in a throwaway experiment (§9); nothing touched `/var/lib/pacman`
except read-only copies of its files.

1. **Nothing parses `pacman.conf` today.** `AlpmPackageSource` and `AlpmConnectionCache` discover repositories from
   `<dbpath>/sync/*.db` and register them with no servers and no ignore lists. Flagging ignored packages needs
   `IgnorePkg` and `IgnoreGroup`, so a small config reader is new work (§3.3). It reads only those two options.
2. **Navigation is not wired.** `WorkspaceWindow.qml` hosts `InstalledPackagesView` directly; `Sidebar.qml` has four
   `HnNavigationDelegate`s, `Installed` is hard-coded `checked: true`, the other three are `enabled: false`.
   No page switching exists (§4.5).
3. **Existing async pattern** (`InstalledPackagesModel`): constructor starts `QtConcurrent::run`, a
   `QFutureWatcher<Result>` delivers `finished()` on the GUI thread, `load_in_progress_` guards re-entry, results
   swap in with `beginResetModel()/endResetModel()`, one `statusChanged()` notifies every property.
4. **Test layout**: a single `test_holonight_packages` executable (plus `test_runtime_controls`), fixtures under
   `tests/fixtures/pacman/<name>/{local,sync}`, `HOLONIGHT_TEST_FIXTURES_DIR` compile definition, internal backend
   headers reachable through `${PROJECT_SOURCE_DIR}/src/backends/src`, models compiled straight into the test target
   from `apps/packages/app/*.cpp`, tests copy a fixture into a `QTemporaryDir` when they need to mutate it.
5. **The application makes no network calls in this feature and never writes under the pacman dbpath.** Databases are
   synced by the user outside the application; the Reload button only re-reads them (§10 for the deferred sync).

---

## 1. Components and file layout

### `src/domain/` (existing `STATIC` target, no new CMake edges)

| File | Contents |
|---|---|
| `include/holonight_packages_domain/pending_update.h` (new) | `PendingUpdate`, `UpdateSnapshot` (§3.1) |
| `include/holonight_packages_domain/update_source.h` (new) | `UpdateSourceErrorCode`, `UpdateSourceError`, abstract `UpdateSource` port (§3.2) |
| `src/update_source.cpp` (new) | Out-of-line `UpdateSource::~UpdateSource()` (same rationale as `package_source.cpp`) |
| `include/holonight_packages_domain/holonight_packages_domain.h` (modified) | Umbrella header also includes the two new headers |
| `CMakeLists.txt` (modified) | Adds `src/update_source.cpp` |

The new headers use only `std::` types (`std::string`, `std::chrono`, `std::expected`); no Qt, no libalpm (REQ-F-013).

### `src/application/` (existing `STATIC` target)

| File | Contents |
|---|---|
| `include/holonight_packages_application/update_summary.h` (new) | `UpdateSummary { int updateCount; int ignoredCount; std::uint64_t totalDownloadBytes; }`, `summarizeUpdates(span<const PendingUpdate>)`, `sortUpdatesByName(vector<PendingUpdate>&)` |
| `src/update_summary.cpp` (new) | Implementations. Ignored rows count toward `ignoredCount` only, never toward `updateCount` or `totalDownloadBytes` (REQ-F-002, REQ-F-009) |
| `include/holonight_packages_application/package_size_formatter.h` (modified) | Adds `formatSignedSizeBytes(std::int64_t)` -> `"+12.3 MiB"`, `"-340 KiB"`, `"0 B"`; reuses `formatSizeBytes` |
| `CMakeLists.txt` (modified) | Adds `src/update_summary.cpp` |

No `UpdatesUseCase`: unlike installed packages there is nothing to orchestrate between the port and the model except
sorting and summing, which are pure functions above. The model holds the port directly, so its tests need only a
fake `UpdateSource` (REQ-F-013).

### `src/backends/` (existing `STATIC` target)

Public:

| File | Contents |
|---|---|
| `include/holonight_packages_backends/alpm_update_source.h` (new) | `AlpmUpdateSourceOptions`, `AlpmUpdateSource : UpdateSource`. Never includes `<alpm.h>` |

Internal (`src/`, reachable by tests via the existing include path):

| File | Contents |
|---|---|
| `src/alpm_update_source.cpp` (new) | Sync-directory scan, comparison, ignore flagging, `dataAsOf`. Includes `<alpm.h>` |
| `src/pacman_config.h` / `.cpp` (new) | `PacmanConfig` value type and `parsePacmanConfig(path)`; no libalpm, no Qt (§3.3) |
| `src/update_matching.h` / `.cpp` (new) | Pure helper `isIgnored(name, groups, ignore_pkg_patterns, ignore_group_patterns)` via `fnmatch(3)` (same semantics as libalpm's `_alpm_fnmatch`) |
| `CMakeLists.txt` (modified) | Adds sources; no new external dependency |

`AlpmPackageSource` and `alpm_package_source.cpp` are **not modified**.

### `src/persistence/`

No change. `AlpmConnectionCache` is reused as-is (one instance owned by `AlpmUpdateSource`).

### `apps/packages/`

| File | Contents |
|---|---|
| `app/UpdatesModel.h` / `.cpp` (new) | `QAbstractListModel` + async orchestration (§3.5). Lives next to `InstalledPackagesModel` because it is a QML-facing transformation (same reasoning as installed-packages-list DESIGN §6); this is the "application-layer `UpdatesModel`" of REQ-F-013 |
| `app/PackagesApplication.h` / `.cpp` (modified) | Builds `AlpmUpdateSource` -> `UpdatesModel`, injects `updatesModel` initial property, destroys the model after the view |
| `CMakeLists.txt` (modified) | Adds `UpdatesModel.h/.cpp`; adds the header to the QML module `SOURCES` |

### `qml/` (feature-scoped; `qt_add_qml_module` globs `qml/*.qml`, so no list edit)

| File | Contents |
|---|---|
| `qml/updates/UpdatesView.qml` | Page root: title row with Reload button, headline, inline notices, list, state messages (§4.6) |
| `qml/updates/UpdatesHeadline.qml` | Count, total download size, "Data as of" label |
| `qml/updates/UpdatesInlineNotice.qml` | Reusable non-blocking notice (used for reload banner and stale hint) |
| `qml/updates/UpdatesTable.qml` | Column header + `ListView` |
| `qml/updates/UpdateRow.qml` | `HnListDelegate` row |
| `qml/updates/UpdatesColumns.qml` | Column width constants (mirrors `PackageTableColumns.qml`) |
| `qml/workspace/Sidebar.qml` (modified) | `currentPage` property, `pageRequested(page)` signal, Updates entry enabled |
| `qml/workspace/WorkspaceWindow.qml` (modified) | `required property UpdatesModel updatesModel`; `StackLayout` switching between the two views |

### `tests/` and docs

| File | Contents |
|---|---|
| `tests/application/update_summary_test.cpp` (new) | Summary, sort, signed size formatting |
| `tests/backends/pacman_config_test.cpp` (new) | Config parser (§3.3) |
| `tests/backends/update_matching_test.cpp` (new) | Ignore matching |
| `tests/backends/alpm_update_source_test.cpp` (new) | Live comparison, ignore flagging, missing config, no-databases, `dataAsOf`, cache coexistence, sync-files-unchanged |
| `tests/apps/updates_model_test.cpp` (new) | Model against a controllable fake `UpdateSource` |
| `tests/apps/updates_view_test.cpp` (new) | Loads `UpdatesView.qml`, asserts role/state data only |
| `tests/apps/installed_packages_view_test.cpp`, `tests/runtime/test_runtime_controls.cpp` (modified) | Workspace creation now supplies `updatesModel` too |
| `tests/CMakeLists.txt` (modified) | New sources; `UpdatesModel.cpp` compiled into both `test_holonight_packages` and `test_runtime_controls`; `holonight_packages_backends` already linked |
| `tests/fixtures/pacman/updates/…` (new) | Fixture tree, §7.1 |
| `docs/sdd/pending-updates/ACCEPTANCE.md` (produced at implementation) | Manual visual checklist, REQ-NF-008 |

---

## 2. Data flow

### 2.1 Load and Reload

Reload is exactly the initial load run again; the same worker path and the same port call serve both.

```mermaid
sequenceDiagram
    participant QML as UpdatesView.qml / Reload button
    participant Model as UpdatesModel (GUI thread)
    participant Pool as QtConcurrent pool
    participant Src as AlpmUpdateSource
    participant Cache as AlpmConnectionCache (own instance)
    participant Alpm as libalpm

    Note over Model: constructor: state = Loading, loading_ = true
    QML->>Model: reload()  (later, user action)
    alt loading_
        Model-->>QML: ignored (single-flight)
    else idle
        Model->>Model: loading_ = true (state = Loading only if there is no previous list), emit stateChanged
    end
    Model->>Pool: QtConcurrent::run(source->loadUpdates())
    Pool->>Src: loadUpdates()
    Src->>Src: scan <dbpath>/sync/*.db (missing/not-dir/none => NoDatabases snapshot, returns here)
    Src->>Src: parsePacmanConfig(conf) for IgnorePkg/IgnoreGroup (unreadable => ConfigurationInvalid)
    Src->>Cache: connection() (lease; re-parses only if file list/mtimes changed)
    Src->>Alpm: alpm_initialize(root, dbpath) -> fresh local handle
    Src->>Src: compare local pkgcache vs lease.syncDatabases(); dataAsOf = oldest sync/*.db mtime
    Src-->>Pool: expected<UpdateSnapshot, UpdateSourceError>
    Pool-->>Model: QFutureWatcher::finished (GUI thread)
    Model->>Model: success: reset rows, state = Updates | UpToDate | NoDatabases, dataAsOf, stale, clear reload error
    Model-->>QML: stateChanged()
```

The path never writes under the dbpath and makes no network call (REQ-F-003, REQ-C-003, REQ-C-004).

### 2.2 Failure

Any failure (pacman.conf missing/unreadable, libalpm init/registration/parse failure, unexpected exception) yields
`std::unexpected(UpdateSourceError)`. In the model:

* **Reload failure with a previous list** (state `Updates`, `UpToDate` or `NoDatabases`): rows, `dataAsOf`, `state` stay
  exactly as they were; `loading_` becomes false; `reloadErrorMessage` is set to the reason; QML shows
  "Reload failed: {reason}. Showing data from {dataAsOfLabel}." with the previous list still interactive.
* **Initial-load failure** has no previous list: the model goes to `ViewState::Error` (rows cleared) with
  `errorMessage` set, and Reload remains available. A Reload pressed in the `Error` state is a fresh initial load
  (state returns to `Loading`, and a further failure lands in `Error` again).

No automatic retry. A later successful load clears `reloadErrorMessage`.

---

## 3. Interfaces

### 3.1 Domain types (`pending_update.h`)

```cpp
namespace holonight_packages_domain {

struct PendingUpdate {
  std::string name;
  std::string installedVersion;
  std::string availableVersion;
  std::string repository;                  // sync repo that supplied the newer version
  std::uint64_t downloadSizeBytes = 0;     // alpm_pkg_get_size(new): compressed package size
  std::int64_t installedSizeDeltaBytes = 0;  // isize(new) - isize(old); may be negative
  bool ignored = false;                    // IgnorePkg / IgnoreGroup match

  bool operator==(const PendingUpdate&) const = default;
};

struct UpdateSnapshot {
  std::vector<PendingUpdate> updates;           // includes ignored rows, unsorted
  bool databasesFound = true;                   // false => "No package databases found" (not an error)
  // mtime of the OLDEST sync/*.db. Meaningless when databasesFound == false.
  std::chrono::system_clock::time_point dataAsOf;

  bool operator==(const UpdateSnapshot&) const = default;
};

}  // namespace holonight_packages_domain
```

"No databases" is a success value (`databasesFound == false`), not an error, per REQ-F-007. "Up to date" is
`databasesFound && updates.empty()`.

### 3.2 Port (`update_source.h`)

```cpp
enum class UpdateSourceErrorCode : std::uint8_t {
  ConfigurationInvalid,   // pacman.conf unreadable/missing
  DatabaseOpenFailed,     // libalpm init/registration/parse failure
  Unknown
};

struct UpdateSourceError {
  UpdateSourceErrorCode code;
  std::string message;    // user-presentable reason (banner text)
};

class UpdateSource {
 public:
  /* rule-of-five defaulted like PackageSource */
  virtual ~UpdateSource();

  // Read-only comparison against the current live sync databases. No network, never writes. Blocking; called on a
  // worker thread. Used for both the initial load and Reload.
  [[nodiscard]] virtual std::expected<UpdateSnapshot, UpdateSourceError> loadUpdates() const = 0;
};
```

The name `loadUpdates()` is used everywhere (port, adapter, fakes, model, tests).

### 3.3 Adapter and configuration

```cpp
// src/backends/include/holonight_packages_backends/alpm_update_source.h
namespace holonight_packages_backends {

struct AlpmUpdateSourceOptions {
  std::filesystem::path database_root;       // alpm root
  std::filesystem::path database_path;      // live dbpath (contains local/ and sync/)
  std::filesystem::path pacman_conf_path;
};

class AlpmUpdateSource : public holonight_packages_domain::UpdateSource {
 public:
  explicit AlpmUpdateSource(AlpmUpdateSourceOptions options);
  ~AlpmUpdateSource() override;
  /* non-copyable, non-movable like AlpmPackageSource */

  std::expected<UpdateSnapshot, UpdateSourceError> loadUpdates() const override;

 private:
  AlpmUpdateSourceOptions options_;
  std::unique_ptr<holonight_packages_persistence::AlpmConnectionCache> connection_cache_;
};
}
```

All paths arrive through `options`; the adapter contains no `"/var/lib/pacman"`, `"/etc"` or `"/"` literals
(REQ-C-001). The single production call site is `PackagesApplication` (`"/"`, `"/var/lib/pacman"`,
`"/etc/pacman.conf"`), the same place the installed-packages wiring already holds its production literals.
The explicit `database_root` / `database_path` win over any `RootDir` / `DBPath` in the config file (which is not read).

**`PacmanConfig` (internal, `pacman_config.h`)**

```cpp
struct PacmanConfig {
  std::vector<std::string> ignore_pkgs;      // [options] IgnorePkg, all lines, whitespace-split
  std::vector<std::string> ignore_groups;    // [options] IgnoreGroup
};
[[nodiscard]] std::expected<PacmanConfig, std::string> parsePacmanConfig(const std::filesystem::path& path);
```

Supported grammar: `[section]` headers, `key = value`, `#` comments (whole-line and trailing), and, in the `[options]`
section only, `IgnorePkg` / `IgnoreGroup` accumulating across lines and whitespace-split. Every other key and
section is ignored (not an error, so real-world configs load); there is no `Include`, `Server`, `SigLevel`, `GPGDir`
or `Architecture` handling. An unreadable or missing file returns an error, which the adapter maps to
`ConfigurationInvalid`: silently treating the ignore lists as empty would inflate the update count (REQ-F-010).

### 3.4 `UpdatesModel` (`apps/packages/app/UpdatesModel.h`)

```cpp
class UpdatesModel : public QAbstractListModel {
  Q_OBJECT
  QML_NAMED_ELEMENT(UpdatesModel)
  QML_UNCREATABLE("UpdatesModel is provided by the application")
  Q_PROPERTY(ViewState state READ state NOTIFY stateChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)            // initial-load failure only
  Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)                         // initial load OR reload in flight
  Q_PROPERTY(int updateCount READ updateCount NOTIFY stateChanged)                  // excludes ignored
  Q_PROPERTY(int ignoredCount READ ignoredCount NOTIFY stateChanged)
  Q_PROPERTY(quint64 totalDownloadBytes READ totalDownloadBytes NOTIFY stateChanged)  // excludes ignored
  Q_PROPERTY(QString totalDownloadLabel READ totalDownloadLabel NOTIFY stateChanged)
  Q_PROPERTY(QDateTime dataAsOf READ dataAsOf NOTIFY stateChanged)                  // invalid when none
  Q_PROPERTY(QString dataAsOfLabel READ dataAsOfLabel NOTIFY stateChanged)          // "Data as of Sep 22, 14:30"
  Q_PROPERTY(bool databasesStale READ databasesStale NOTIFY stateChanged)
  Q_PROPERTY(QString reloadErrorMessage READ reloadErrorMessage NOTIFY stateChanged)  // empty = no failure
  Q_PROPERTY(QString officialOnlyNote READ officialOnlyNote CONSTANT)

 public:
  enum class ViewState : std::uint8_t { Loading, Updates, UpToDate, NoDatabases, Error };
  Q_ENUM(ViewState)

  enum Role : std::uint16_t {
    NameRole = Qt::UserRole + 1, InstalledVersionRole, AvailableVersionRole, RepositoryRole,
    DownloadSizeRole /*quint64*/, DownloadSizeLabelRole, SizeDeltaRole /*qint64*/, SizeDeltaLabelRole, IsIgnoredRole
  };

  using Clock = std::function<std::chrono::system_clock::time_point()>;
  explicit UpdatesModel(std::shared_ptr<holonight_packages_domain::UpdateSource> source, QObject* parent = nullptr,
                        Clock now = &std::chrono::system_clock::now);
  ~UpdatesModel() override;

  /* rowCount / data / roleNames as in InstalledPackagesModel */
  Q_INVOKABLE void reload();                 // single-flight; no-op while loading
};
```

A single `loading` property covers both the initial load and a reload; `ViewState::Loading` is used only when there is
no previous list to show (initial load, or reload from `Error`), so a reload with a previous list keeps its rows and
state while `loading` is true.

Role names (QML): `name`, `installedVersion`, `availableVersion`, `repository`, `downloadSize`, `downloadSizeLabel`,
`sizeDelta`, `sizeDeltaLabel`, `isIgnored`.

Semantics:

* One `QFutureWatcher<Outcome>`. `reload()` returns immediately if `loading_` (covers REQ-F-004 in both double-press
  and press-during-initial-load cases), so libalpm is never entered twice concurrently from the model.
* Worker lambdas catch `std::exception` and `...` and convert to `UpdateSourceError{Unknown, ...}` exactly like
  `InstalledPackagesModel::startLoading()`.
* On success: sort by name (`sortUpdatesByName`), `beginResetModel/endResetModel`, recompute `UpdateSummary`, store
  `dataAsOf`, compute `databasesStale = databasesFound && (now() - dataAsOf) > 7 days` (strictly greater: 6 d no,
  exactly 7 d no, 8 d yes), clear `reloadErrorMessage` and `errorMessage`, set `state`: `!databasesFound` ->
  `NoDatabases`; `updates.empty()` -> `UpToDate`; otherwise `Updates` (also when every row is ignored: rows listed,
  `updateCount == 0`).
* On failure with no previous list (initial load, or reload from `Error`): rows cleared, `state = Error`,
  `errorMessage` set.
* On failure with a previous list: only `reloadErrorMessage` and `loading` change.
* `dataAsOfLabel` is `tr("Data as of %1").arg(QLocale().toString(dataAsOf, QLocale::ShortFormat))`, empty when
  `dataAsOf` is invalid. `officialOnlyNote` is the constant text "AUR and foreign packages are not covered."
* The injectable clock exists only for deterministic 7-day boundary tests (REQ-F-006). No update state is written to
  disk or `QSettings` (REQ-NF-002); a fresh model always starts in `Loading`.

### 3.5 Application wiring (`PackagesApplication.cpp`)

```cpp
auto update_source = std::make_shared<holonight_packages_backends::AlpmUpdateSource>(
    holonight_packages_backends::AlpmUpdateSourceOptions{
        .database_root = "/", .database_path = "/var/lib/pacman", .pacman_conf_path = "/etc/pacman.conf"});
updates_model_ = std::make_unique<UpdatesModel>(std::move(update_source));
view_->setInitialProperties({{"installedPackagesModel", ...}, {"updatesModel", QVariant::fromValue(updates_model_.get())}});
```

`PackagesApplication::~PackagesApplication()` resets `view_`, then `updates_model_`, then `installed_packages_model_`.

---

## 4. Resolution of the open items

### 4.1 Placement and cache coexistence

* **Port and types**: `src/domain` (`update_source.h`, `pending_update.h`), because both `apps` and `backends` already
  depend on domain and tests must build the model without libalpm (same rule as `PackageSource`).
* **Adapter**: a **new class** `AlpmUpdateSource`, not an extension of `AlpmPackageSource`. The two have different
  ports and different construction inputs (`pacman_conf_path`). `AlpmPackageSource` stays untouched.
* **Model**: `apps/packages/app/UpdatesModel`, as for `InstalledPackagesModel`.
* **Cache reuse**: `AlpmUpdateSource` owns its **own** `AlpmConnectionCache(database_root, database_path)`,
  satisfying "owned per source instance" (alpm-sync-db-cache REQ-C-002). `loadUpdates()` uses `connection()` for the
  registered sync databases and a fresh scoped handle for the local database, precisely mirroring
  `AlpmPackageSource::enumerateInstalledPackages`. Because the cache re-reads when the sync `.db` file list or mtimes
  change, a Reload after the user synced manually picks the new data up, and an unchanged Reload does not re-parse
  (REQ-F-012). Cost accepted: the sync databases are parsed once per source instance, i.e. twice per process
  (Installed + Updates). Sharing one cache would require changing `AlpmPackageSource`'s constructor; deferred (§5).

### 4.2 Ignored packages and sizes

* Ignore lists come from `PacmanConfig` (`IgnorePkg`, `IgnoreGroup`, accumulated over all lines), read on every load
  and reload (cheap, and picks up config edits).
* A row is ignored iff `isIgnored(name, groups_of_new_version, ignore_pkgs, ignore_groups)`: `fnmatch(pattern, name, 0)`
  for each `IgnorePkg` pattern, or `fnmatch` of each `IgnoreGroup` pattern against each group of the **sync** package
  (`alpm_pkg_get_groups(new)`), which is what `alpm_pkg_should_ignore` does in libalpm. Implemented as a pure function
  rather than mutating the cached handle with `alpm_option_add_ignorepkg`, because the live handle belongs to the
  cache and is shared; a pure function is also unit-testable without libalpm. Verified in the experiment that
  `zeb*` and group `fruits` mark exactly the expected rows.
* Version rule: for each installed package, take the first registered repo (in directory-scan order, as
  `AlpmConnectionCache` registers them) that contains the name; list it iff `alpm_pkg_vercmp(new, old) > 0`. Equal
  and older are not listed. Installed packages absent from every sync database (foreign) are skipped.
* **Download size** per row = `alpm_pkg_get_size(new)` (compressed package size, `CSIZE`). The package cache is
  deliberately not consulted (`alpm_pkg_download_size` would report 0 for already-cached files and would require a
  `CacheDir`). **Size delta** = `alpm_pkg_get_isize(new) - alpm_pkg_get_isize(old)` as `int64_t`.
* Aggregates (`summarizeUpdates`): `updateCount` and `totalDownloadBytes` sum only `!ignored` rows; `ignoredCount`
  counts the rest. No aggregate delta is shown in v1.

### 4.3 "Data as of", stale hint, no-database state

* **`dataAsOf`** = minimum `last_write_time` over `<dbpath>/sync/*.db`, converted with
  `std::chrono::clock_cast<std::chrono::system_clock>`. This is the only freshness source; there is no other
  completion time.
* **Stale hint**: computed in the model, not the adapter: `databasesStale = databasesFound && now() - dataAsOf >
  7 days`. A Reload after the user synced manually yields a newer `dataAsOf` and the hint disappears; a failed Reload
  leaves it unchanged. QML shows it as a non-blocking notice in `Updates` and `UpToDate`, with text along the lines of
  "Your package databases are out of date. Sync them with your package manager, then press Reload." The text names no
  specific command (a bare `pacman -Sy` risks partial upgrades on Arch).
* **Distinguishing no-databases from up-to-date**: `UpdateSnapshot::databasesFound`. The adapter scans
  `<dbpath>/sync` first with `error_code` overloads: not found, not a directory, or zero `*.db` -> success with
  `databasesFound = false`, and it returns before touching the cache, libalpm or pacman.conf (so a nonexistent
  `database_path`, for which `alpm_initialize` would fail with `ALPM_ERR_NOT_A_DIR`, is also "no databases", and a
  missing pacman.conf without databases still shows "no databases"). Real I/O errors (permission denied) remain
  errors. This is intentionally more lenient than `AlpmConnectionCache`, which still rejects a non-directory `sync`
  path; the pre-scan runs before it. Model: `!databasesFound` -> `ViewState::NoDatabases`; found but no rows ->
  `ViewState::UpToDate` (also the case for an empty local database, REQ-F-007's third test).

### 4.4 Default landing page

**Recommendation: keep Installed as the default landing page; add Updates as a live, enabled navigation entry.**

Evidence: navigation is not wired at all today (§0.2), so this change must introduce page switching regardless; the
existing acceptance tests and runtime checks instantiate `WorkspaceWindow` and assert the Installed view
(`installed_packages_view_test.cpp`, `test_runtime_controls.cpp`), so changing the landing page also changes what
they exercise. The idea doc's "Updates as the primary page" is justified by "pending transaction, risk summary and
actions" (docs/ideas/01-high-level-project-idea.md, UI structure), none of which exist yet; a read-only list without
the advisor or an update action is not yet a better first screen than the inventory. The model loads at startup
either way (its constructor starts the load, as `InstalledPackagesModel` does), so flipping the default later
costs only the initial value of `WorkspaceWindow.currentPage`, which is a single property by design here.

Wiring: `Sidebar.qml` gets `property string currentPage: "installed"` and `signal pageRequested(string page)`; each
enabled `HnNavigationDelegate` binds `checked: root.currentPage === "<id>"` and `onClicked: root.pageRequested("<id>")`.
`WorkspaceWindow` holds `property string currentPage: "installed"` and a `StackLayout` whose `currentIndex` follows
it. Both views exist simultaneously (their models load at startup regardless). The "Last synced" sidebar label stays
as is (out of scope).

### 4.5 QML page structure and controls

Public `Holonight.Controls` / `Holonight.Core` types only (no `HnSelectableDelegate`; `qmllint` would not catch that,
per `docs/known-issues/qmllint-does-not-enforce-internal-types.md`, so the real test suite and runtime test are the
guard):

| UI element | Type |
|---|---|
| Row | `HnListDelegate` (as `PackageTableRow.qml`), `enabled`-style dimming for ignored rows |
| Ignored / repo badges | `HnStatusIndicator` (`Warning`, text "Ignored"); repo via existing `PackageOriginBadge` (`import "../packages"`) or `HnStatusIndicator.Neutral` |
| Reload banner, stale hint | `UpdatesInlineNotice` = `Rectangle` + `HnStatusIndicator` (`Error` / `Info`) + wrapping `HnLabel` |
| Headline card | `HnSurfaceFrame` (card role) with `HnLabel`s (`HnTypographyRole.Heading`, `Caption`), colors from `HoloniightPalette` |
| Reload | `Controls.Button` (as `OrphanFooterBar`) with `text: model.loading ? qsTr("Loading…") : qsTr("Reload")`, `enabled: !model.loading`; plus `Controls.ProgressBar { indeterminate: true; visible: model.loading }` under the headline |
| Loading / no-databases / up-to-date / error | `HnLoadingState` / `HnEmptyState` (`titleText`, `descriptionText`) with `objectName`s `loadingState`, `noDatabasesState`, `upToDateState`, `errorState` |
| Scrolling | `Controls.ScrollView` + `ListView` with `Controls.ScrollBar.vertical` |

Structure of `UpdatesView.qml` (root is an `Item`, not a bare layout, per the layout-root quirk):

```
Item (root, required property UpdatesModel updatesModel)
  ColumnLayout (anchors.fill, margins 16)
    RowLayout: HnLabel "Updates" (Heading)  |  spacer  |  Reload Button
    UpdatesInlineNotice  (reload banner)    visible: reloadErrorMessage.length > 0
                         text: "Reload failed: %1. Showing data from %2." (dataAsOf invalid => without the second sentence)
    UpdatesHeadline      visible: state is Updates or UpToDate
                         "%1 updates" | totalDownloadLabel | dataAsOfLabel (+ "N ignored" caption when ignoredCount > 0)
    ProgressBar          visible: loading
    UpdatesInlineNotice  (stale hint)       visible: databasesStale   (text in §4.3)
    UpdatesTable         visible: state === Updates  (header + ListView of UpdateRow)
  HnLoadingState   visible: state === Loading
  HnEmptyState     visible: state === NoDatabases   "No package databases found." + explanatory description
  HnEmptyState     visible: state === UpToDate      "All official-repository packages are up to date." + officialOnlyNote
  HnEmptyState     visible: state === Error         "Couldn't check for updates" + errorMessage
```

Row columns: name, `installedVersion -> availableVersion`, repository badge, download size (right aligned),
size delta (right aligned), Ignored badge. `UpdateRow` uses `required property` per role
(`name`, `installedVersion`, `availableVersion`, `repository`, `downloadSizeLabel`, `sizeDeltaLabel`, `isIgnored`).
All strings go through `qsTr`.

QML constraints from repo memory that apply: (a) never declaratively bind `UpdatesModel.<ViewState value>` to a
property of type `ViewState`; only compare (`state === UpdatesModel.Loading`), as `InstalledPackagesView.qml` does;
(b) a bare `ColumnLayout` must not be a component root; (c) delegates use `required property`.

---

## 5. Key decisions and rationale

| Decision | Rationale |
|---|---|
| No in-app database sync; Reload only re-reads the live databases | Syncing needs privileges or a temporary-copy workaround; deferred to the daemon (§10). Reload is local, cheap and lets the user pick up manual syncs |
| Single `loading` property for initial load and reload | Both are the same operation; one flag gives single-flight and the busy button with no extra state |
| `ViewState::Loading` only when there is no previous list | A reload keeps rows visible; failure then only sets `reloadErrorMessage` |
| Parse `pacman.conf` in a small internal parser, `IgnorePkg`/`IgnoreGroup` only | libalpm has none; `pacman-conf` may not exist on the test machine (REQ-NF-003); nothing else needs the config |
| Unreadable/missing pacman.conf is an error, not "nothing ignored" | A silent empty ignore list would inflate the count (REQ-F-010) |
| Ignore matching as a pure `fnmatch` function on the sync package's groups | Same semantics as `alpm_pkg_should_ignore`; avoids mutating the shared cached handle; testable without libalpm |
| New `AlpmUpdateSource`, own `AlpmConnectionCache` | SPEC REQ-F-012 wording (per-source ownership); no change to `AlpmPackageSource` |
| "No databases" as a successful snapshot flag | Type-level distinction from errors and from "up to date" (same reasoning as installed-packages-list §6 for empty vs error) |
| Stale hint computed in the model with an injected clock | Boundary tests (6 d / 7 d / 8 d / 2 h) without touching file mtimes' relationship to wall time; a fresh Reload naturally resets it |
| Failure never clears rows when a previous list exists | REQ-F-005; only `reloadErrorMessage` and `loading` change |
| Stale-hint text names no pacman command | A bare `-Sy` risks partial upgrades on Arch |
| Installed remains the landing page | §4.4 |
| Model in `apps/packages/app/` | Same rule as `InstalledPackagesModel`: QML-facing transformation |

---

## 6. Alternatives considered

| Alternative | Verdict |
|---|---|
| Keep an unprivileged temp-dbpath refresh in this iteration | Rejected by scope: sync is deferred to the daemon (§10) |
| Shell out to `checkupdates` | Rejected: needs `pacman-contrib`, no structured output, cannot be fixture-tested (REQ-NF-003/004) |
| `alpm_option_add_ignorepkg` on the handle + `alpm_pkg_should_ignore` | Rejected (mutates the cache-owned handle) |
| Treat unreadable pacman.conf as "nothing ignored" | Rejected: inflates the update count silently (REQ-F-010) |
| Separate `loading` and `reloading` properties | Rejected: no consumer needs to distinguish them; `state === Loading` already marks "no previous list" |
| One shared `AlpmConnectionCache` for both adapters | Deferred: halves memory/parse time but changes `AlpmPackageSource` ownership; add later if measured to matter |
| Load the Updates model lazily on first page visit | Rejected for v1: page switching is new; loading in the constructor matches `InstalledPackagesModel` and REQ-C-002, and the cached path is cheap |
| Updates as default landing page | §4.4 |
| Put `UpdatesModel` in `src/application` | Rejected: QML-facing model (installed-packages-list §6) |
| Do the stale-hint comparison in QML | Rejected: not unit-testable at the required boundaries |

---

## 7. Test strategy

### 7.1 Fixtures (`tests/fixtures/pacman/updates/`, REQ-NF-003/004)

```
updates/
  local/                         # installed: ALPM_DB_VERSION + packages below
  sync/{core,extra}.db           # live sync databases
  pacman.conf                    # static: [options] IgnorePkg, IgnoreGroup (multi-line, comments), unknown keys, repo sections
  README.md / build script       # how the binary .db files are regenerated (same tar recipe as populated/README.md)
  empty-sync/, no-sync/          # (or created in temp dirs by tests) no-.db and missing sync directory cases
```

Content covers: newer in sync (`alpha` 1.0-1 -> 2.0-1, `gamma` newer with negative size delta), equal (`beta`), older
in sync (`delta`), foreign (`omega`), `IgnorePkg` match (with a glob such as `ign*`), `IgnoreGroup` match
(`%GROUPS%` in the sync desc), and a package present in both repos (first registered repo wins). Tests use the
checked-in `pacman.conf` directly (paths derive from `HOLONIGHT_TEST_FIXTURES_DIR`; it contains no paths or
servers), and copy `local/`+`sync/` into a `QTemporaryDir` when they mutate mtimes or content (pattern from
`TemporaryDatabase` in existing tests). No network and no URL of any kind appears in any test (REQ-C-004).

### 7.2 Mapping to requirements

| REQ | Test(s) | Notes |
|---|---|---|
| F-001 | `updates_view_test`: page loads over fake-source model, delegates expose name, both versions, repository, sizes, ignored via roles; `UpdatesModel.RolesExposeAllFields`; zero updates -> empty valid list, `UpToDate`, not `Error`. `task qml-lint` | data only, no visuals |
| F-002 | `update_summary_test` (10 normal + 3 ignored -> count 7, total excludes ignored); `UpdatesModel.HeadlineExcludesIgnored`; label changes after a fake reload with newer `dataAsOf` | |
| F-003 | `AlpmUpdateSource.LoadListsNewerVersions`; equal/older excluded; `UpdatesModel.StartsLoadingThenLoaded` | |
| F-004 | `UpdatesModel.SecondReloadIgnoredWhileRunning` (fake counts calls); `loading` true until release then false; reload after completion starts a new one and shows the new rows/timestamp; reload during initial load ignored | fake with `std::promise` gates |
| F-005 | model: failing fake on reload keeps rows, sets `reloadErrorMessage`, exposes `dataAsOf`; later success clears it; failing initial load -> `Error`, empty rows, `reload()` accepted | |
| F-006 | `UpdatesModel.StaleBoundaries` with injected clock: 6 d no hint, 7 d no hint, 8 d hint, 2 h no hint; reload with fresh snapshot clears hint; adapter test: `dataAsOf` == oldest `.db` mtime; hint text contains no `pacman -` string | |
| F-007 | adapter + model: missing sync dir, empty sync dir -> `databasesFound == false` -> `NoDatabases`; sync with `.db` but empty local -> `UpToDate` | |
| F-008 | model: same versions -> `UpToDate`, `officialOnlyNote` non-empty, `dataAsOf` set | distinct from `NoDatabases` |
| F-009 | adapter: fixture conf marks exactly the `IgnorePkg` and `IgnoreGroup` matches; model: 5 normal + 3 ignored -> 8 rows, count 5, total excludes ignored; `update_matching_test` (glob, group) | |
| F-010 | `pacman_config_test` (multi-line `IgnorePkg`, trailing and whole-line comments, unknown keys/sections ignored, keys outside `[options]` not collected, unreadable file -> error); adapter: missing conf -> `ConfigurationInvalid`, no snapshot; model: failing reload keeps rows and shows banner | |
| F-011 | model with blocking fake: constructor and `reload()` return before source completes, `loading` true, rows appear only after release (pump events with `QSignalSpy::wait`) | |
| F-012 | adapter: modify content + mtime of a sync `.db` in a temp copy -> next `loadUpdates()` reflects it; untouched -> stable result | same technique as `alpm_connection_cache_test` |
| F-013 | model tests use fake `UpdateSource` only (no ALPM, no QML engine); inspection of includes in `update_source.h`/`pending_update.h`; `task tidy` | |
| NF-001 | covered by F-011 | |
| NF-002 | `UpdatesModel.FreshModelHasNoCarriedState`; grep for `QSettings`/file writes in the new files | |
| NF-003/004 | fixtures + README; paths derive from `HOLONIGHT_TEST_FIXTURES_DIR` | |
| NF-005/006/007 | `task format-check`, `task tidy`, `task qml-lint`, `task test` (count >= 15, no skips) | |
| NF-008 | `docs/sdd/pending-updates/ACCEPTANCE.md` checklist executed by the user, light and dark: list layout, ignored badge, headline, reload banner, stale hint text, no-databases, up-to-date, busy Reload button with progress bar, page switching | assistant does not screenshot |
| C-001 | adapter tests use fixture/temp paths only; grep for `"/var"`, `"/etc"` literals in `alpm_update_source.cpp` | |
| C-002 | inspection: `UpdatesModel` uses `QtConcurrent::run` + `QFutureWatcher`, with the same comment rationale as the installed model | |
| C-003 | SHA-256 + size + mtime of every sync file in a temp copy before/after `loadUpdates()` (repeated for a second call) | |
| C-004 | inspection (no network APIs or URLs) + `task test` offline | |
| C-005 | inspection + `qml-lint` + runtime import policy tests | |

`tests/apps/installed_packages_view_test.cpp` and `tests/runtime/test_runtime_controls.cpp` are updated to pass a
model for `updatesModel` (a fake-source `UpdatesModel`) when creating the workspace.

---

## 8. Known risks and build consequences

* **Header/library skew and libalpm versions**: only symbols that exist in the installed library are used (the
  installed `alpm.h` on this machine is newer than the shared object). Behaviour under other libalpm versions is
  unverified.
* **Two parses of the sync databases** (installed cache + updates cache) at first use; accepted, revisit if slow.
* **Behaviour vs `pacman -Syu`**: `Include`d config, `HoldPkg`, `replaces=`, provides-based updates and `NoUpgrade`
  are not modelled (SPEC non-goal for `replaces=`). Counts can differ from pacman in those cases.
* **Ignore lists from `[options]` only**: `IgnorePkg` written via an `Include`d file is not seen (no `Include`
  support); such packages would appear unflagged and be counted. Accepted for v1.
* **Download size ignores cached packages** and epoch/duplicate-repo subtleties; label is "Download size" of the
  packages, not "still to download".
* **Stale data is the user's responsibility**: without in-app sync, results are only as fresh as the last manual
  sync; the freshness label and stale hint make that visible.
* **`WorkspaceWindow` gains a required property**: any code instantiating it without `updatesModel` fails at load
  (existing tests updated in this change, listed in §1).
* **`qmlcachegen`/`qmllint`**: new QML must be checked against the two known issues in `docs/known-issues/`; the
  qmltypes/runtime import scripts (`scripts/check-qmltypes.sh`, `scripts/check-runtime-qml-imports.sh`) may need the
  new type acknowledged; confirm during implementation.
* **Build**: `apps/packages/CMakeLists.txt` adds `UpdatesModel` to sources and QML module `SOURCES`;
  `tests/CMakeLists.txt` adds the new test files and `UpdatesModel.cpp` to `test_holonight_packages` and
  `test_runtime_controls` (with header, mirroring the installed model entries). No new external dependency; no
  change to `Dockerfile.ci`, `Taskfile.yml` or CI.

---

## 9. Experiments (throwaway, scratchpad only; nothing in the repo or `/var/lib/pacman` was modified)

Environment: pacman 7.1.0.r9, libalpm 16.0.1, run as uid 1000. Programs and fixtures were built under the session
scratchpad from the repo's populated fixture plus hand-made newer databases.

**Verified (relevant to the remaining design)**

1. Two-handle comparison (sync databases from one handle, local database from a handle on the live dbpath):
   `alpm_pkg_vercmp` strictness (newer listed, older excluded), `fnmatch` on an `IgnorePkg` glob and on `%GROUPS%`,
   and `csize` / `isize` delta values as expected.
2. `alpm_option_set_disable_sandbox` is declared in `/usr/include/alpm.h` but not exported by the installed
   `libalpm.so` (link error): not used.

**Assumed (not verified)**

* Behaviour on other libalpm versions.

---

## 10. Deferred: database sync

Synchronising the databases is deliberately not part of this feature; the user syncs with their package manager and
uses Reload. It is deferred to the future `holonight-packaged` daemon / privileged helper, which can update the live
sync databases with proper privileges and could expose completion to the model (for example by triggering the same
`reload()`).

An unprivileged `checkupdates`-style refresh was prototyped during design and verified feasible: copy the live sync
`.db` files into a private mkdtemp dbpath, run `alpm_db_update()` against it as the current user, and compare with the
local database read through a second handle on the live dbpath, never touching the live sync directory. Findings worth
keeping if the approach is revived: libalpm's default downloader has no total-duration bound (a custom fetch callback
is the only in-process way to enforce one); a successful `alpm_db_update` does not prove the database parses, so
databases must be validated after download; signature verification needs an explicit gpgdir. Implementing it would
also need a config reader that understands repositories, servers, `SigLevel` and `Include`, none of which exist
today. None of this is built or specified now.

---

## Related Documents

* **Specification**: `docs/sdd/pending-updates/SPEC.md`
* **Feature context**: `docs/ideas/01-high-level-project-idea.md` (Phase 1, safe update detection)
* **UI mockup**: `docs/mockups/updates.png` (advisor, grouping, AUR row, selection checkboxes and "Review and update"
  are out of scope; the headline stats and list columns are in scope, and its refresh affordance is implemented as
  the local "Reload" button)
* **Baselines**: `docs/sdd/installed-packages-list/DESIGN.md`, `docs/sdd/alpm-sync-db-cache/DESIGN.md`,
  `docs/sdd/installed-page-ui/DESIGN.md`
* **Project instructions**: `CLAUDE.md`

## Change History

| Version | Date | Author | Notes |
|---|---|---|---|
| 1.0 | 2026-09-24 | SDD Stage 2 | Initial design from SPEC v1.1 |
| 1.1 | 2026-09-24 | Scope reduction | Removed all unprivileged database syncing (temp-dbpath refresh, libcurl fetcher, timeout, temp-dir sweep, abort, mirror fixtures, curl build change); refresh replaced by a local `reload()`; port reduced to `loadUpdates()`; freshness is always the oldest sync .db mtime; pacman.conf parser reduced to `IgnorePkg`/`IgnoreGroup`; aligned with SPEC v1.2 requirement numbering; added "Deferred: database sync" section |
