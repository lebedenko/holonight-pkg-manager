# Online update check — DESIGN (HoloNight Packages)

Spec: `docs/sdd/online-update-check/SPEC.md` (62 requirements). Status: decisions approved by the user on 2026-10-09; ready for the implementation plan.

**Revision 2026-10-09.** What changed:

- All proposals P-1..P-16 are now **DECIDED (user, 2026-10-09)**. Section 13 lists them as decided. The only remaining open items are the verification spikes S-1..S-3 (section 8.3), which are tasks, not decisions.
- **P-14 reversed.** The GUI process (`holonight-packages`) no longer runs `AlpmUpdateChecker`, `UpdateCheckService` or `UpdateCheckScheduler`. "Check now" calls D-Bus `CheckNow()` on `holonight-packaged` (bus-activated) through a new `UpdateCheckClient` port. Only `holonight-packaged` checks, schedules, single-flights, applies cooldown and writes the snapshot. Affected: sections 0 (F5), 1, 2.2, 2.5, 2.6, 2.7, 3.1 to 3.5, 4.1, 4.3, 4.6, 4.7, 8, 9, 10, 11, 12 (D10), 14 (R-13, R-14), 15.
- **P-16 is now mandatory and changed in role.** The snapshot file is the hand-off channel for the update list (D-Bus carries no list). The GUI reads it read-only on `StatusChanged` / property changes and through a file watcher. `UpdateSnapshotStore::load()` is read-only; a separate `discardInvalid()` is called only by the writer (packaged).
- **D12:** the last scratch run directory is kept. The `beforeCleanup` hook is removed (sections 4.4, 5.2, 5.4, 8.4, 12).
- **D2:** Option A (`Updates.Count` unchanged; `SnapshotFetchedAt == 0` means "Not checked yet").
- **D9:** a missing file or key silently uses 6 h; a present-but-invalid value warns once and uses 6 h; a value below 15 min is clamped with one warning.
- New client-side failure "Update service unavailable" (not a fifth error code). New SPEC requirements REQ-F-042 to REQ-F-045.

Section 12 lists the SPEC/code conflicts that were found while reading the code. Section 13 lists every decision in one table.

## 0. Findings from the existing code that shape the design

| # | Finding (file) | Consequence |
|---|---|---|
| F1 | `AlpmUpdateSource::loadUpdates()` (`src/backends/src/alpm_update_source.cpp`) is local-only. It does the comparison inline, against sync databases that `AlpmConnectionCache` registers in alphabetical file order. `update_matching.cpp` holds only `isIgnored`. | REQ-F-007 needs the comparison extracted into one helper. Section 5.2 does that. |
| F2 | `parsePacmanConfig` (`src/backends/src/pacman_config.cpp`) reads only `[options] IgnorePkg/IgnoreGroup`. It has no `Server`, `Include`, `SigLevel`, `GPGDir` or `Architecture`. Real `pacman.conf` files use `Include = /etc/pacman.d/mirrorlist`. | The checker needs a new repository parser (`pacman_repositories`). libalpm does not parse `pacman.conf`, and `pacman-conf` is an external process, which REQ-C-002 forbids. |
| F3 | `UpdateMonitor` (`src/application/src/update_monitor.cpp`) runs `loadUpdates()` on the global `QtConcurrent` pool, watches `local/` and `sync/` of the real dbpath, and its status is the single source for D-Bus. | The online snapshot is adopted into the monitor (section 3.4). No second status path. |
| F4 | `UpdateStatusService` is a `QObject` that exports one `UpdatesAdaptor`. The checked-in XML is `apps/packaged/dbus/org.holonight.Packages1.Updates.xml`. | The new D-Bus members go on a second interface and adaptor (section 4.5, REQ-F-040). |
| F5 | `UpdatesModel` (`apps/packages/app/UpdatesModel.h`) loads through its own `UpdateSource` in the GUI process. It does not read the packaged service. `UpdateStatusClient` (`apps/packages/app/UpdateStatusClient.h`) reads only `Updates.Count`/`State`, watches the bus name with a `QDBusServiceWatcher`, and by design never starts the service. | The GUI does not check by itself (decided, P-14 reversed). It calls `CheckNow()` on packaged through a new `UpdateCheckClient` that follows the `UpdateStatusClient` pattern (service watcher, `PropertiesChanged`) but deliberately lets D-Bus activation start the service for the call. The update list reaches the GUI through the snapshot file (section 3.5). |
| F6 | `src/persistence` links `PkgConfig::Alpm` PUBLIC (for `AlpmConnectionCache`). | A snapshot store in the same target would drag libalpm into the application-layer tests, against REQ-NF-007. Section 2 adds a separate libalpm-free target. |
| F7 | `tests/CMakeLists.txt` builds one big `test_holonight_packages` that links backends and persistence. | REQ-NF-007 needs a separate, libalpm-free test executable. |
| F8 | `holonight-config` (`../holonight-config`) provides TOML documents (`readDocument`, `DocumentSchema`, locked atomic saves) and `resolveAppearancePath`. Its README says each application owns its own schema and file. | See P-9 for the interval configuration. |
| F9 | `libalpm` (`/usr/include/alpm.h`, pacman 7.1 on this machine) has `alpm_db_update`, `alpm_db_add_server`, `alpm_option_set_disable_dl_timeout`, `alpm_option_set_parallel_downloads`, `alpm_option_set_gpgdir`, `alpm_db_get_siglevel` and `ALPM_ERR_*`. `alpm_db_update` takes the handle's own lock file (`<dbpath>/db.lck`). It has no cancel API. | Basis for sections 5 and 5.4. |

## 1. Overview of the approach

```
GUI process (holonight-packages)                    │  holonight-packaged (the only process that checks)
 "Check now" ► UpdateCheckModel                     │
   ► UpdateCheckClient (port) ──D-Bus CheckNow()────┼──►  scheduler (automatic) / UpdateCheckAdaptor::CheckNow
                                                    │              │  requestCheck(origin)   (home thread)
                                                    │              ▼
                                                    │      UpdateCheckService ── single-flight, join, cooldown, last-good, persistence
                                                    │              │  private 1-thread QThreadPool
                                                    │              ▼
                                                    │      UpdateChecker (port) ──► AlpmUpdateChecker ──► scratch copy ──► alpm_db_update ──► shared comparison helper
                                                    │              │ expected<UpdateSnapshot, UpdateCheckError>
                                                    │              ▼
                                                    │      stamp fetchedAt ► UpdateSnapshotStore.save (atomic) ► UpdateMonitor.adoptOnline ► D-Bus properties
                                                    │                                              │
 UpdateCheckModel / UpdatesModel ◄── read-only load ◄── snapshot file ◄──────────────────────────────┘
        ▲ StatusChanged / PropertiesChanged (D-Bus) and QFileSystemWatcher trigger the read
```

The real pacman database is never written. Section 5 has the algorithm.

## 2. Components, locations and link direction

New files are marked `+`. Edited files are marked `~`. Public headers live under `include/<target-name>/`, as in the existing targets.

### 2.1 `src/domain` (target `holonight_packages_domain`, links only Qt Core privately)

| File | Content |
|---|---|
| `+include/holonight_packages_domain/update_checker.h`, `+src/update_checker.cpp` | `UpdateCheckErrorCode`, `UpdateCheckError`, the single message table, `CheckedSnapshot`, and the `UpdateChecker` port. |
| `+include/holonight_packages_domain/backend_capabilities.h` | `BackendCapabilities` (header-only). |
| `+include/holonight_packages_domain/update_snapshot_store.h`, `+src/update_snapshot_store.cpp` | `UpdateSnapshotStore` port (read-only `load()`, writer-only `save()` and `discardInvalid()`) and `SnapshotStoreError`. |
| `~include/holonight_packages_domain/holonight_packages_domain.h` | Includes the three new headers. |
| `~CMakeLists.txt` | Adds the two `.cpp` files. |

The domain gains no dependency. It contains no libalpm, dbpath or QML types.

### 2.2 `src/application` (target `holonight_packages_application`; links domain, Qt Core and Concurrent as today)

| File | Content |
|---|---|
| `+include/holonight_packages_application/update_check_ports.h` | `Clock`, `RandomSource`, `OneShotTimer`. |
| `+include/.../update_check_policy.h`, `+src/update_check_policy.cpp` | `UpdateCheckPolicy`, `nextAutomaticDelay`, `resolveCheckInterval`. Pure functions. |
| `+include/.../update_check_status.h` | `UpdateCheckStatus`, `UpdateCheckOutcome`, `CheckOrigin`. |
| `+include/.../update_check_service.h`, `+src/update_check_service.cpp` | `UpdateCheckService`: coordinator, last-good snapshot, persistence orchestration, logging. Instantiated only in `holonight-packaged`. |
| `+include/.../update_check_scheduler.h`, `+src/update_check_scheduler.cpp` | `UpdateCheckScheduler`: startup delay, periodic timer, backoff. |
| `+include/.../update_check_runtime.h`, `+src/update_check_runtime.cpp` | Production `SystemClock`, `MtRandomSource`, `QtOneShotTimer`. |
| `+include/.../snapshot_selection.h`, `+src/snapshot_selection.cpp` | `selectFresherSnapshot` (section 3.4). |
| `+include/.../update_age_format.h`, `+src/update_age_format.cpp` | Pure age bucketing ("3 h") for the status line. |
| `~include/.../update_monitor.h`, `~src/update_monitor.cpp` | Adds `adoptOnline(const CheckedSnapshot&)`. Behaviour without an adopted snapshot is unchanged. |
| `~CMakeLists.txt` | Adds the new sources. Link lines are unchanged. |

The application layer has no libalpm and no QML types. It stays Qt Core and Concurrent only, as `CLAUDE.md` and `AGENTS.md` describe it.

### 2.3 `src/backends` (target `holonight_packages_backends`; PUBLIC domain, Qt Core, Qt Network; PRIVATE Alpm, persistence)

| File | Content |
|---|---|
| `+include/holonight_packages_backends/alpm_update_checker.h`, `+src/alpm_update_checker.cpp` | `AlpmUpdateCheckerOptions` and `AlpmUpdateChecker`. |
| `+src/pending_update_computation.h/.cpp` | The shared comparison helper. Both `AlpmUpdateSource` and `AlpmUpdateChecker` call it (REQ-F-007). |
| `+src/pacman_repositories.h/.cpp` | Parses `[repo]` sections, `Server`, `Include` (mirrorlist files), `SigLevel`, `GPGDir`, `Architecture`, `$repo` and `$arch`. Fails closed. |
| `+src/scratch_dir.h/.cpp` | Scratch root validation, `flock`, `mkdtemp`, sweep (keep only the newest run directory), symlink-safe removal that never follows `local`. |
| `+src/alpm_error_mapping.h/.cpp` | The only errno-to-code table. |
| `+include/.../backend_capabilities_alpm.h` | `inline BackendCapabilities alpmBackendCapabilities()` returning `{.canCheckForUpdates = true}`. |
| `~src/alpm_update_source.cpp` | The loop body moves into the shared helper. Behaviour is bit-identical. |
| `~CMakeLists.txt` | Adds the sources. No new link dependency. |

### 2.4 `src/persistence`

| File | Content |
|---|---|
| `+include/holonight_packages_persistence/json_update_snapshot_store.h`, `+src/json_update_snapshot_store.cpp` | `JsonUpdateSnapshotStore : UpdateSnapshotStore`. |
| `+include/.../cache_locations.h`, `+src/cache_locations.cpp` | `appCacheDir(const EnvLookup&)`, which returns `$XDG_CACHE_HOME/holonight-packages` or `$HOME/.cache/holonight-packages`. |
| `~CMakeLists.txt` | **New target `holonight_packages_snapshot_store`** (STATIC). It contains the two new `.cpp` files and links `PUBLIC holonight_packages_domain Qt6::Core` only. The existing `holonight_packages_persistence` is unchanged. |

The new target exists so that application-layer and persistence tests link no libalpm (REQ-NF-007, finding F6). It is also what lets the GUI read the snapshot file without linking more than it already does. `AGENTS.md` lists the static libraries, so this extra target needs a one-line mention there. See discrepancy D13.

### 2.5 `apps/packaged`

| File | Content |
|---|---|
| `+UpdateCheckAdaptor.h/.cpp` | `QDBusAbstractAdaptor` for `org.holonight.Packages1.UpdateCheck`. |
| `~UpdateStatusService.h/.cpp` | New constructor overload `UpdateStatusService(UpdateMonitor*, UpdateCheckService*, QObject* parent = nullptr)`. The existing one-argument constructor stays and exports only the Updates interface. |
| `~dbus/org.holonight.Packages1.Updates.xml` | Appends a second `<interface>`. The existing interface block is untouched. |
| `~main.cpp` (composition root) | Builds `AlpmUpdateChecker`, `JsonUpdateSnapshotStore` (the single writer), `UpdateCheckService`, `UpdateCheckScheduler`, and the config loader. Calls `AlpmUpdateChecker::sweepStale()` and the store's `discardInvalid()` (when the load found an invalid file) before the first check. Adds the `--check-interval-minutes` option. |
| `+PackagedConfig.h/.cpp` | Reads `packages.toml` through `HoloNight::Config` (P-9). Only this file and `main.cpp` know about holonight-config. |
| `~CMakeLists.txt` | Library `holonight_packaged_service` gains the adaptor and keeps `PUBLIC holonight_packages_application Qt6::Core Qt6::DBus`. Executable `holonight-packaged` gains `holonight_packages_snapshot_store` and `HoloNight::Config`. |

No new activation file is needed. `org.holonight.Packages1.service.in` and `holonight-packaged.service.in` are unchanged.

### 2.6 `apps/packages` and `qml/`

| File | Content |
|---|---|
| `+app/UpdateCheckClient.h` | The `UpdateCheckClient` port (abstract `QObject`): `checkNow()`, `status()` (service reachable, `CanCheck`, `Checking`, last-check fields, `SnapshotFetchedAt`), signals `statusChanged()`, `checkCompleted()` (the D-Bus `StatusChanged`) and `checkNowFailed()`. Faked in tests (`FakeUpdateCheckClient`). |
| `+app/DBusUpdateCheckClient.h/.cpp` | QtDBus implementation of the port. Follows the `UpdateStatusClient` pattern (a `QDBusServiceWatcher` on `org.holonight.Packages1`, `GetAll` on appearance, `PropertiesChanged` for interface `org.holonight.Packages1.UpdateCheck`), but `checkNow()` is an asynchronous call with D-Bus auto-start enabled, so the call activates `holonight-packaged` when it is not running. A D-Bus error reply or activation timeout emits `checkNowFailed()`. `UpdateStatusClient` itself stays unchanged and still never starts the service. |
| `+app/SnapshotFileReader.h/.cpp` | Read-only reader for the snapshot file: calls `JsonUpdateSnapshotStore::load()` (never `save()` or `discardInvalid()`), and watches the cache directory with `QFileSystemWatcher` (with a short debounce, because the writer renames a temporary file over the target). Emits `snapshotRead(std::optional<CheckedSnapshot>)`. Tolerates an absent or corrupt file. |
| `+app/UpdateCheckModel.h/.cpp` | QML-facing view-model for the check control and status line (section 4.6). |
| `~app/UpdatesModel.h/.cpp` | Adds `applyCheckedSnapshot(const CheckedSnapshot&)`; merges the snapshot read from the file with its own local `loadUpdates()` result through `selectFresherSnapshot` (P-13). The constructor and all existing members are unchanged. |
| `~app/PackagesApplication.h/.cpp` (composition root) | Builds `DBusUpdateCheckClient`, `SnapshotFileReader` (over a `JsonUpdateSnapshotStore` used read-only) and `UpdateCheckModel`. It builds no `AlpmUpdateChecker`, `UpdateCheckService` or `UpdateCheckScheduler`. Passes `updateCheckModel` as a new initial property. |
| `~CMakeLists.txt` | Adds the new files to the executable and to `qt_add_qml_module(... SOURCES ...)`. Link line gains `holonight_packages_snapshot_store` and `Qt6::DBus` (already used by `UpdateStatusClient`). |
| `+qml/updates/UpdatesCheckBar.qml` | The control and status rows (section 4.7). |
| `~qml/updates/UpdatesView.qml`, `~qml/workspace/WorkspaceWindow.qml` | Embeds `UpdatesCheckBar`, and forwards the new `updateCheckModel` property. |

REQ-C-007 is respected: nothing under `qml/packages` or `qml/explore` changes.

### 2.7 Link direction (every arrow points toward `domain`)

```
apps/packages ──► application ──► domain            (UpdateMonitor/UpdatesModel path as today; no checker, no coordinator)
      │──────────► backends ─────► domain            (existing local-only sources only; AlpmUpdateChecker sits in the same static library but is never constructed here, and a layering rule forbids naming it under apps/packages)
      │──────────► snapshot_store ► domain           (read-only load of the snapshot file)
      │──────────► Qt6::DBus ──► holonight-packaged  (runtime only: CheckNow(), properties, StatusChanged)
apps/packaged ──► (service lib) ──► application ──► domain
      │──────────► backends, snapshot_store, HoloNight::Config   (composition root only)
backends ──► persistence (private, existing AlpmConnectionCache use) ──► domain
tests/application_checks ──► application, snapshot_store, domain         (no libalpm, no QML)
```

## 3. Data flow

### 3.1 Sequence

```mermaid
sequenceDiagram
    autonumber
    participant VM as UpdateCheckModel + UpdatesModel (GUI process)
    participant Cli as UpdateCheckClient (GUI, QtDBus)
    participant Bus as UpdateCheckAdaptor / UpdatesAdaptor (packaged)
    participant Sch as UpdateCheckScheduler (packaged, home thread)
    participant Svc as UpdateCheckService (packaged, home thread)
    participant Pool as private QThreadPool(1)
    participant Chk as UpdateChecker (AlpmUpdateChecker)
    participant Store as UpdateSnapshotStore (writer: packaged)
    participant Mon as UpdateMonitor (packaged)
    participant File as snapshot file
    participant Rd as SnapshotFileReader (GUI, read-only)

    VM->>Cli: checkNow()  [user activates "Check now"]
    Cli->>Bus: D-Bus CheckNow()  (bus activation starts packaged if needed)
    alt service cannot be reached or activated
        Cli-->>VM: checkNowFailed -> inline "Update service unavailable"
    else reached
        Bus->>Svc: requestCheck(OnDemand)
    end
    Note over Sch,Svc: an automatic check enters the same way: Sch->>Svc: requestCheck(Automatic)
    alt a check is already running
        Svc-->>Svc: join: remember requester, no new work
    else cooldown active (OnDemand only)
        Svc-->>Svc: no-op, status unchanged
    else idle
        Svc->>Svc: checking = true; log "check started"
        Svc-->>Bus: PropertiesChanged(Checking = true)
        Bus-->>Cli: PropertiesChanged -> control disabled and busy
        Svc->>Pool: run(checker->checkForUpdates)
        Pool->>Chk: checkForUpdates()
        Chk-->>Pool: expected<UpdateSnapshot, UpdateCheckError>
        Pool->>Pool: fetchedAt = clock.now()
        opt success
            Pool->>Store: save(CheckedSnapshot)  (tmp + rename)
            Store->>File: atomic replace
        end
        Pool-->>Svc: result (future finished, delivered on home thread)
        Svc->>Svc: update lastGood / status; log "check finished"
        opt success
            Svc->>Mon: snapshotAdopted -> adoptOnline(snapshot)
            Mon-->>Bus: PropertiesChanged on Updates (Count, DataAsOf, ...)
        end
        Svc-->>Bus: PropertiesChanged on UpdateCheck + StatusChanged() (once)
        Svc-->>Sch: checkCompleted(outcome)
        Sch->>Sch: arm next timer (interval+jitter, or backoff step)
        Bus-->>Cli: StatusChanged / PropertiesChanged
        Cli-->>VM: statusChanged -> failure text or age line
        VM->>Rd: read now (and the file watcher fires independently)
        Rd->>File: load() read-only
        File-->>Rd: CheckedSnapshot or absent/corrupt (tolerated, never deleted)
        Rd-->>VM: snapshotRead -> UpdatesModel.applyCheckedSnapshot (selectFresherSnapshot against local loadUpdates)
    end
```

### 3.2 Threads

- All state changes happen on the "home" thread, which is the thread that owns `UpdateCheckService` (the main thread of `holonight-packaged`; the GUI process has no such service).
- `requestCheck()` may be called from any thread. When called from a foreign thread it posts to the home thread with a queued `QMetaObject::invokeMethod`. This yields single-flight without a mutex and meets REQ-F-027 and REQ-C-009.
- The checker runs only in a private `QThreadPool` with `maxThreadCount = 1`. It does not use the global pool. The global pool is also used by `UpdatesModel`, `InstalledPackagesModel` and `ExploreModel`, and a check can legitimately block for up to the total bound. On a one-core machine that could starve every page load.
- Persistence runs on the same worker thread, right after the checker returns, so the GUI/D-Bus thread never does disk I/O for the snapshot file. `fetchedAt` is stamped on the worker thread by `Clock::now()` straight after the checker returns, which is the "completion time" of REQ-F-017.
- D-Bus calls never block (REQ-NF-002). `CheckNow()` only calls `requestCheck(OnDemand)` and returns.

### 3.3 Single-flight, join and cooldown

State machine (home thread): `Idle -> Running -> Idle`.

- `requestCheck` while `Running`: add the requester callback to `joined_` and return. No second checker call (REQ-F-026).
- On completion, every requester receives the same `UpdateCheckOutcome`, and `checkCompleted` is emitted once.
- **DECIDED (user, 2026-10-09) P-6**: an `OnDemand` request (D-Bus `CheckNow()`) that arrives less than 10 s after the previous check completed is a documented no-op: it does not call the checker and changes no status. `CheckNow()` while `Checking` is also a no-op (it joins the running check). Automatic requests are not subject to the cooldown. Cooldown and join live only in `holonight-packaged`; the GUI keeps no cooldown of its own and learns the effect through the `Checking` property.
- REQ-F-027 holds because there is exactly one `UpdateCheckService`, in `holonight-packaged`; every request, scheduled or from D-Bus, goes through it. The `flock` is only a safety net (3.5).

### 3.4 How an online snapshot reaches the displayed list (freshness rule)

The existing displayed list comes from the local comparison (`loadUpdates`). An online snapshot is newer information about the mirrors, but the local comparison is better information about the installed set. A rule decides which one is shown:

**SUPERSEDED by repository selection below (2026-10-09). Historical P-13**: `selectFresherSnapshot(local, online)` returns the online snapshot unless the local result's `dataAsOf` (oldest real sync database mtime) is greater than or equal to the online snapshot's `dataAsOf`. In that case the local result wins.

Why this rule:

- After the user runs `pacman -Syu`, the real sync databases carry the same or a later mirror mtime as the scratch copy had. Local wins, and it correctly reflects the new installed set.
- At service start, a persisted snapshot from 2 hours ago beats a local result computed from a database that was last synced 3 days ago.
- No event bookkeeping is needed, and the rule is a pure function that is easy to test.

Known gap, listed as risk R-11: an installed-set change without a sync (for example `pacman -S pkg` with a stale database) does not flip the winner.

In `holonight-packaged`, `UpdateMonitor::adoptOnline()` stores the snapshot and recomputes `status_` through `selectFresherSnapshot` using the last local result. `onEvaluationFinished()` applies the same rule. Without an adopted snapshot, `UpdateMonitor` behaves exactly as today, so the existing tests are unaffected.

In the GUI process the rule applies where `UpdatesModel` merges its own local `loadUpdates()` result with the snapshot that `SnapshotFileReader` read from the file: `UpdatesModel::applyCheckedSnapshot()` and the model's own `reload()` both call `selectFresherSnapshot`. An absent snapshot means the local result is shown unchanged.

The persisted snapshot is loaded by `holonight-packaged` at service start (REQ-F-033) and emitted once through `snapshotAdopted` before the first check finishes. The GUI reads the same file independently at its own start.

### 3.5 Two processes, one coordinator

**DECIDED (user, 2026-10-09) P-14 (reversed):** only `holonight-packaged` checks. It owns the scheduler, the single `UpdateCheckService` (single-flight, join, cooldown), the checker and the snapshot writes. The GUI process (`holonight-packages`) has no checker, scheduler or coordinator.

- **GUI to service.** "Check now" calls `CheckNow()` on `org.holonight.Packages1` through the `UpdateCheckClient` port. The bus name is activatable, so the call starts `holonight-packaged` if it is not running. If the call fails (name not activatable, service crashed, activation timeout), the client emits `checkNowFailed()` and the view-model shows "Update service unavailable" inline (REQ-F-044). This is a client-side message, not an `UpdateCheckErrorCode`; the four codes stay.
- **Busy and join.** While a check runs, the GUI sees `Checking == true` through `PropertiesChanged` and disables the control, so a GUI-originated join is rare. `CheckNow()` during a check, or within the 10 s cooldown, is still a no-op on the service (REQ-F-045), covering races, other clients and a stale GUI state.
- **List hand-off.** D-Bus carries counts and status only, not the update list. The snapshot file is therefore the hand-off channel. Packaged writes it atomically on success (tmp + rename, mode 0600). The GUI reads it with `JsonUpdateSnapshotStore::load()` (read-only) when it gets D-Bus `StatusChanged` or an `UpdateCheck` `PropertiesChanged`, at GUI start, and whenever `QFileSystemWatcher` reports a change in the cache directory (**DECIDED P-16, now mandatory**). The watcher covers the case where packaged checked while the GUI was not connected or the signal was missed.
- **Tolerating a bad file.** `load()` never writes or deletes. A missing, truncated or unknown-version file reads as "no snapshot" in the GUI, and the GUI keeps showing its local list. Only packaged, the single writer, calls `discardInvalid()`, at its own start and only for an invalid file (REQ-F-034).
- **Safety-net `flock`.** `AlpmUpdateChecker` still holds `flock(LOCK_EX|LOCK_NB)` on `<scratchRoot>/check.lock` for the whole check. It is no longer the primary GUI/service mechanism. It protects against a second `holonight-packaged` instance (for example started by hand with another bus) and against a wedged detached checker thread (5.1). A check that finds it held returns `Busy`.
- **Convergence.** The sidebar badge (`Updates.Count` from packaged) and the Updates list (local result merged with the snapshot file) agree after each check because both derive from the same snapshot and the same freshness rule.

## 4. Interfaces

### 4.1 Domain

```cpp
// holonight_packages_domain/update_checker.h
enum class UpdateCheckErrorCode : std::uint8_t { NetworkUnavailable, RepositoryUnreachable, Busy, Unknown };

inline constexpr std::array<UpdateCheckErrorCode, 4> kAllUpdateCheckErrorCodes{
    UpdateCheckErrorCode::NetworkUnavailable, UpdateCheckErrorCode::RepositoryUnreachable,
    UpdateCheckErrorCode::Busy, UpdateCheckErrorCode::Unknown};

// One mapping table in update_checker.cpp. Messages are fixed strings: no libalpm text can reach them.
[[nodiscard]] std::string_view updateCheckErrorMessage(UpdateCheckErrorCode code) noexcept;
// Stable lower-case token for D-Bus and logs: "network-unavailable", "repository-unreachable", "busy", "unknown".
[[nodiscard]] std::string_view updateCheckErrorName(UpdateCheckErrorCode code) noexcept;

struct UpdateCheckError {
  UpdateCheckErrorCode code = UpdateCheckErrorCode::Unknown;
  [[nodiscard]] std::string_view message() const noexcept { return updateCheckErrorMessage(code); }
  bool operator==(const UpdateCheckError&) const = default;
};

// The result of one successful check, stamped by the service.
struct CheckedSnapshot {
  UpdateSnapshot snapshot;                              // existing type: pending_update.h
  std::chrono::system_clock::time_point fetchedAt;      // completion time of the check
  bool operator==(const CheckedSnapshot&) const = default;
};

class UpdateChecker {
 public:
  UpdateChecker() = default;  // copy/move defaulted, virtual dtor out of line, as UpdateSource does
  virtual ~UpdateChecker();
  // Refreshes a private copy of the package metadata from the configured repositories and compares it with the
  // installed set. Blocking, network access, never writes system package metadata. Call from a worker thread only.
  [[nodiscard]] virtual std::expected<UpdateSnapshot, UpdateCheckError> checkForUpdates() const = 0;
};
```

Messages (one fixed table, all non-empty; the exact wording is editable copy and not a decision item):

| Code | Message |
|---|---|
| NetworkUnavailable | "No network connection to the package servers." |
| RepositoryUnreachable | "A package repository could not be reached." |
| Busy | "Another package operation is using the package database." |
| Unknown | "The check failed for an unexpected reason." |

```cpp
// holonight_packages_domain/backend_capabilities.h  (new type; see discrepancy D1)
struct BackendCapabilities {
  // NOLINTNEXTLINE(readability-identifier-naming): preserve the established public data contract.
  bool canCheckForUpdates = false;
  bool operator==(const BackendCapabilities&) const = default;
};

// holonight_packages_domain/update_snapshot_store.h
struct SnapshotStoreError { std::string message; };   // developer-facing, logged only

enum class SnapshotFileState : std::uint8_t { Absent, Valid, Invalid };   // Invalid: corrupt, truncated, unknown schema version, count mismatch

struct SnapshotLoad {
  std::optional<CheckedSnapshot> snapshot;      // set iff state == Valid
  SnapshotFileState state = SnapshotFileState::Absent;
};

class UpdateSnapshotStore {
 public:
  virtual ~UpdateSnapshotStore();
  // READ-ONLY. Safe in any process (the GUI uses it). A missing file gives {nullopt, Absent}; a corrupt, truncated or
  // unknown-version file gives {nullopt, Invalid}. It never modifies or deletes the file (REQ-F-034). An I/O error
  // that prevents reading (for example permission denied) is an error.
  [[nodiscard]] virtual std::expected<SnapshotLoad, SnapshotStoreError> load() const = 0;
  // WRITER ONLY (holonight-packaged). Atomic: temporary file in the same directory, then rename. On failure the
  // previous file is intact (REQ-F-035).
  [[nodiscard]] virtual std::expected<void, SnapshotStoreError> save(const CheckedSnapshot& snapshot) = 0;
  // WRITER ONLY. Removes the file if it is still invalid (re-checked inside the call). Called by packaged after a load
  // that reported Invalid. The GUI process never calls it.
  [[nodiscard]] virtual std::expected<void, SnapshotStoreError> discardInvalid() = 0;
};
```

### 4.2 Application ports for fakes (REQ-NF-007)

```cpp
// update_check_ports.h
class Clock {                      // wall clock, for timestamps and cooldown
 public:
  virtual ~Clock();
  [[nodiscard]] virtual std::chrono::system_clock::time_point now() const = 0;   // thread-safe
};
class RandomSource {
 public:
  virtual ~RandomSource();
  [[nodiscard]] virtual std::int64_t uniform(std::int64_t lo, std::int64_t hi) = 0;   // inclusive
};
class OneShotTimer {
 public:
  virtual ~OneShotTimer();
  virtual void start(std::chrono::milliseconds delay, std::function<void()> onTimeout) = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual bool active() const = 0;
};
```

Production implementations are in `update_check_runtime.h`: `SystemClock`, `MtRandomSource` (seeded from `std::random_device`) and `QtOneShotTimer` (a single-shot `QTimer`).

Test fakes (in `tests/application/`, usable from `tests/apps/` because that include directory is already configured):

- `fake_clock.h`: `FakeClock` with `advance(duration)`. It also owns `FakeTimer` instances and fires due timeouts in order as time advances, so a 48 h simulation runs in milliseconds.
- `fake_random.h`: `SeededRandom` (fixed seed) and `ScriptedRandom` (returns a queue of values).
- `fake_update_checker.h`: `FakeUpdateChecker : UpdateChecker`. It has an `enqueue(expected<…>)` queue as `FakeUpdateSource` does, plus `setBlocking/release`, `callCount()`, `maxConcurrency()` and `callerThreads()`.
- `fake_snapshot_store.h`: in-memory store with injectable load/save failures.

### 4.3 Policy, scheduler, service

```cpp
// update_check_policy.h
struct UpdateCheckPolicy {                       // defaults are DECIDED (user, 2026-10-09): P-1..P-6
  std::chrono::seconds startupDelay{60};
  std::chrono::minutes interval{360};
  std::chrono::minutes maxJitter{10};
  std::array<std::chrono::minutes, 3> backoff{std::chrono::minutes{5}, std::chrono::minutes{15},
                                              std::chrono::minutes{60}};
  std::chrono::minutes minInterval{15};
  std::chrono::seconds onDemandCooldown{10};
};

// consecutiveFailures == 0 -> interval +/- jitter; 1..3 -> backoff[n-1] exactly (no jitter); >= 4 -> interval +/- jitter.
// The jitter bound is min(maxJitter, interval / 10).
[[nodiscard]] std::chrono::milliseconds nextAutomaticDelay(const UpdateCheckPolicy& policy, int consecutiveFailures,
                                                           RandomSource& random);

struct IntervalResolution {
  std::chrono::minutes interval;
  std::optional<std::string> warning;      // exactly one warning text when the input was unusable
};
// raw == nullopt (file or key missing): default 6 h, NO warning (D9, decided). Present but empty, non-numeric or <= 0:
// default 6 h plus exactly one warning. Below minInterval (15 min): clamped to 15 min plus exactly one warning.
[[nodiscard]] IntervalResolution resolveCheckInterval(std::optional<std::string_view> raw, const UpdateCheckPolicy& policy);
```

```cpp
// update_check_status.h
enum class CheckOrigin : std::uint8_t { Automatic, OnDemand };

struct UpdateCheckOutcome {
  bool succeeded = false;
  std::optional<UpdateCheckErrorCode> error;                 // set iff !succeeded
  std::chrono::system_clock::time_point completedAt;
  CheckOrigin origin = CheckOrigin::Automatic;
};

struct UpdateCheckStatus {
  bool checking = false;
  bool hasCheckResult = false;                               // false until the first check completes
  bool lastCheckSucceeded = false;
  std::optional<UpdateCheckErrorCode> lastError;
  std::chrono::system_clock::time_point lastCheckTime{};     // meaningful iff hasCheckResult
  std::optional<std::chrono::system_clock::time_point> snapshotFetchedAt;   // nullopt: no snapshot yet
  std::optional<int> count;                                  // non-ignored count of the last-good snapshot
  bool operator==(const UpdateCheckStatus&) const = default;
};
```

```cpp
// update_check_service.h
class UpdateCheckService : public QObject {
  Q_OBJECT
 public:
  struct Dependencies {
    std::shared_ptr<holonight_packages_domain::UpdateChecker> checker;        // required
    std::shared_ptr<holonight_packages_domain::UpdateSnapshotStore> store;    // required
    std::shared_ptr<Clock> clock;                                             // required
    holonight_packages_domain::BackendCapabilities capabilities;
  };
  UpdateCheckService(Dependencies dependencies, UpdateCheckPolicy policy, QObject* parent = nullptr);
  ~UpdateCheckService() override;      // waits for the worker; the worker owns its own shared_ptrs

  // Loads the persisted snapshot (REQ-F-033/034) and emits snapshotAdopted if one is valid. If load() reported
  // Invalid, calls store->discardInvalid() (this service is the single writer).
  void start();
  // Thread-safe. Joins a running check; an OnDemand request while checking or inside the cooldown is a no-op
  // (REQ-F-045); calls onDone on the home thread. Callers: the scheduler and UpdateCheckAdaptor::CheckNow only.
  void requestCheck(CheckOrigin origin, std::function<void(const UpdateCheckOutcome&)> onDone = {});

  [[nodiscard]] const UpdateCheckStatus& status() const;
  [[nodiscard]] const std::optional<holonight_packages_domain::CheckedSnapshot>& lastGood() const;
  [[nodiscard]] bool canCheckForUpdates() const;          // capabilities.canCheckForUpdates

 signals:
  void statusChanged(const holonight_packages_application::UpdateCheckStatus& status);   // on any status field change
  void checkCompleted(const holonight_packages_application::UpdateCheckOutcome& outcome);   // exactly once per completed check
  void snapshotAdopted(const holonight_packages_domain::CheckedSnapshot& snapshot);        // success, or the valid snapshot loaded at start
};
```

`checkCompleted` fires once per finished check. This is the "one change signal per completed successful check" of REQ-F-014. The D-Bus `StatusChanged()` signal (4.5) is bound to it.

```cpp
// update_check_scheduler.h   (QObject; no process, no libalpm)
class UpdateCheckScheduler : public QObject {
  Q_OBJECT
 public:
  UpdateCheckScheduler(UpdateCheckService& service, std::unique_ptr<OneShotTimer> timer,
                       std::shared_ptr<RandomSource> random, UpdateCheckPolicy policy);   // policy.interval already resolved
  void start();   // arms the startup delay (REQ-F-020)
 private slots:
  void onCheckCompleted(const UpdateCheckOutcome& outcome);   // only Automatic outcomes advance the failure counter
};
```

- After an `Automatic` outcome the scheduler arms `nextAutomaticDelay(policy, failures, random)`; success sets `failures = 0` (REQ-F-029). A `Busy` outcome counts as a failure (REQ-F-030).
- **DECIDED (user, 2026-10-09) P-5**: an `OnDemand` failure does not touch the failure counter and does not re-arm the timer. An `OnDemand` success re-arms the timer to `interval ± jitter` from its completion and resets the counter, because a success proves connectivity and the data is fresh.
- The timer is armed only after a check finishes, so there is never a second check pending while one runs (REQ-F-021).

### 4.4 Backend

```cpp
// holonight_packages_backends/alpm_update_checker.h
struct AlpmUpdateCheckerTestHooks {                 // empty in production; used by the REQ-F-010 test
  std::function<void()> afterCopyStarted;           // runs between "copy begins" and the second lock check
  // No cleanup hook: the last run directory is retained after the check, so REQ-F-006 tests inspect it directly.
};

struct AlpmUpdateCheckerOptions {
  std::filesystem::path databaseRoot;      // libalpm root, as AlpmUpdateSourceOptions
  std::filesystem::path databasePath;      // REAL dbpath (read only), containing local/ and sync/
  std::filesystem::path pacmanConfPath;    // repositories, SigLevel, GPGDir, Architecture, Ignore*
  std::filesystem::path scratchRoot;       // e.g. $XDG_CACHE_HOME/holonight-packages/checkdb; no default inside the adapter
  std::chrono::seconds totalBound{120};    // T_max (P-3)
  AlpmUpdateCheckerTestHooks hooks;
};

class AlpmUpdateChecker final : public holonight_packages_domain::UpdateChecker {
 public:
  explicit AlpmUpdateChecker(AlpmUpdateCheckerOptions options);
  ~AlpmUpdateChecker() override;
  [[nodiscard]] std::expected<holonight_packages_domain::UpdateSnapshot, holonight_packages_domain::UpdateCheckError>
  checkForUpdates() const override;
 private:
  std::shared_ptr<const AlpmUpdateCheckerOptions> options_;   // shared with the watchdog thread (5.4)
};
```

As for `AlpmUpdateSourceOptions`, the adapter holds no default pacman paths; the composition roots pass the existing `kDefaultPacman*` constants.

Single mapping authority (`src/alpm_error_mapping.h`):

```cpp
enum class ServerKinds : std::uint8_t { AllLocal, SomeRemote };      // all servers are file:// ?
[[nodiscard]] holonight_packages_domain::UpdateCheckErrorCode classifyAlpmError(alpm_errno_t error, ServerKinds kinds) noexcept;
```

The shared comparison helper (`src/pending_update_computation.h`):

```cpp
// Compares the installed packages with the first sync database (in the given order) that has the package.
[[nodiscard]] std::vector<holonight_packages_domain::PendingUpdate> computePendingUpdates(
    alpm_list_t* installedPackages, const std::vector<alpm_db_t*>& syncDatabases, const PacmanConfig& ignoreRules);
```

### 4.5 D-Bus additions (compatible; REQ-F-040/041)

**Compatibility statement.** The change is additive and backward compatible, but it is not an extension of the existing `org.holonight.Packages1.Updates` interface. It adds a second interface, `org.holonight.Packages1.UpdateCheck`, on the same object path `/org/holonight/Packages1` and the same bus name. The new interface is a second `<interface>` element in the same checked-in XML file `apps/packaged/dbus/org.holonight.Packages1.Updates.xml`. The existing `Updates` interface block stays byte-identical, so the REQ-F-040 diff criterion holds.

Why not add members to `Updates`: the existing test `PackagedDbusTest.IntrospectionMatchesCheckedInContract` (`tests/packaged/packaged_dbus_test.cpp:185`) asserts that `Refresh` is the only method of the `Updates` interface (the loop fails for any other `method ` member). Adding `CheckNow` there would force a change to an existing test, against the REQ-F-040 acceptance criterion "existing D-Bus client tests pass unmodified". The test's `membersOf()` only collects the interface named `org.holonight.Packages1.Updates`, so a second interface does not touch it. See discrepancy D3.

**DECIDED (user, 2026-10-09) P-10 (with D3, D4)**, names and types:

| Member | Kind | Signature | Meaning |
|---|---|---|---|
| `CheckNow` | method | `()` | Requests an on-demand check and returns immediately. The only caller path from the GUI. Bus-activates the service when it is not running. Joins a running check (no-op while `Checking`). Within the 10 s cooldown it is a documented no-op. |
| `CanCheck` | property | `b`, read | `canCheckForUpdates`. |
| `Checking` | property | `b`, read | A check is running. |
| `LastCheckTime` | property | `x`, read | Completion time of the latest attempt, Unix seconds. `0` if none yet. |
| `LastCheckSucceeded` | property | `b`, read | Outcome of the latest attempt. `false` while `LastCheckTime == 0`. |
| `LastCheckError` | property | `s`, read | `""`, or `network-unavailable`, `repository-unreachable`, `busy`, `unknown`. |
| `SnapshotFetchedAt` | property | `x`, read | Completion time of the last successful check, Unix seconds. `0` = no snapshot (unknown). |
| `StatusChanged` | signal | `()` | Emitted exactly once when a check completes (success or failure), after the properties have been updated. |

Property names use the prefix `LastCheck…` because the existing `Updates.LastError` property already means something else (free text of the last local-evaluation failure). See D4. Property changes also go out as `org.freedesktop.DBus.Properties.PropertiesChanged` for interface `…UpdateCheck`, following the existing convention in `UpdateStatusService::emitPropertiesChanged`; `Checking` therefore reaches clients at both ends of a check, while `StatusChanged()` fires once per completion.

Introspection XML to append inside `<node>` after the existing interface:

```xml
  <interface name="org.holonight.Packages1.UpdateCheck">
    <!-- Starts an online check (private copy of the sync databases) and returns immediately.
         Joins a running check; ignored for 10 s after the previous check completed. -->
    <method name="CheckNow"/>
    <!-- False when the backend cannot check online. -->
    <property name="CanCheck" type="b" access="read"/>
    <property name="Checking" type="b" access="read"/>
    <!-- Unix seconds of the latest attempt's completion; 0 = never checked. -->
    <property name="LastCheckTime" type="x" access="read"/>
    <property name="LastCheckSucceeded" type="b" access="read"/>
    <!-- "", "network-unavailable", "repository-unreachable", "busy" or "unknown". -->
    <property name="LastCheckError" type="s" access="read"/>
    <!-- Unix seconds of the last successful check; 0 = no snapshot. -->
    <property name="SnapshotFetchedAt" type="x" access="read"/>
    <!-- Emitted once per completed check, success or failure. -->
    <signal name="StatusChanged"/>
  </interface>
```

`UpdateCheckAdaptor` (`apps/packaged/UpdateCheckAdaptor.h`):

```cpp
class UpdateCheckAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.holonight.Packages1.UpdateCheck")
  Q_PROPERTY(bool CanCheck READ canCheck)
  Q_PROPERTY(bool Checking READ checking)
  Q_PROPERTY(qlonglong LastCheckTime READ lastCheckTime)
  Q_PROPERTY(bool LastCheckSucceeded READ lastCheckSucceeded)
  Q_PROPERTY(QString LastCheckError READ lastCheckError)
  Q_PROPERTY(qlonglong SnapshotFetchedAt READ snapshotFetchedAt)
 public:
  explicit UpdateCheckAdaptor(UpdateStatusService* service);   // setAutoRelaySignals(false), like UpdatesAdaptor
 public slots:
  void CheckNow();                                              // NOLINT: slot name is the D-Bus method name
 signals:
  void StatusChanged();                                         // NOLINT: signal name is the D-Bus signal name
};
```

**No-snapshot state on the wire (D2 Option A, P-15; DECIDED, user, 2026-10-09).** `Updates.Count` keeps its meaning and type (`u`). While `Updates.State` is `loading`, no count is known; `Count` is `0` there as it is today, and clients must read `State`. "No online snapshot yet" is `SnapshotFetchedAt == 0`, shown in the GUI as the "Not checked yet" status line (`hasSnapshot == false`). A strict "unknown" encoding for `Count` would change an existing member and is not used.

### 4.6 Updates view-model (GUI process)

A new object, so that `UpdatesModel` (already 15 properties) stays focused. It talks to `holonight-packaged` only through the `UpdateCheckClient` port (section 2.6) and never to a service object. Placement and wording are **DECIDED (user, 2026-10-09) P-11**.

```cpp
// apps/packages/app/UpdateCheckModel.h
class UpdateCheckModel : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(UpdateCheckModel)
  QML_UNCREATABLE("UpdateCheckModel is provided by the application")
  Q_PROPERTY(bool available READ available NOTIFY changed)               // not (service reachable and CanCheck == false) (REQ-F-039)
  Q_PROPERTY(bool serviceUnavailable READ serviceUnavailable NOTIFY changed)   // last CheckNow() failed at D-Bus level (REQ-F-044)
  Q_PROPERTY(bool checking READ checking NOTIFY changed)
  Q_PROPERTY(bool checkNowEnabled READ checkNowEnabled NOTIFY changed)   // available && !checking (REQ-F-037)
  Q_PROPERTY(bool hasSnapshot READ hasSnapshot NOTIFY changed)           // false: "Not checked yet" (REQ-F-019)
  Q_PROPERTY(QDateTime snapshotFetchedAt READ snapshotFetchedAt NOTIFY changed) // mirrored service metadata
  Q_PROPERTY(QDateTime displayedSnapshotFetchedAt READ displayedSnapshotFetchedAt NOTIFY changed)
  Q_PROPERTY(QString snapshotAgeText READ snapshotAgeText NOTIFY changed)   // "Last checked 3 h ago" (REQ-F-036)
  Q_PROPERTY(QDateTime lastCheckTime READ lastCheckTime NOTIFY changed)
  Q_PROPERTY(bool lastCheckSucceeded READ lastCheckSucceeded NOTIFY changed)
  Q_PROPERTY(bool failed READ failed NOTIFY changed)                      // a result exists and it failed
  Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY changed)     // the neutral token, e.g. "busy"
  Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY changed)
  Q_PROPERTY(QString failureText READ failureText NOTIFY changed)         // "Last check failed: <message>" (REQ-F-038), or "Update service unavailable" (REQ-F-044)
 public:
  using Clock = std::function<std::chrono::system_clock::time_point()>;
  UpdateCheckModel(UpdateCheckClient* client, SnapshotFileReader* reader, UpdatesModel* updates, Clock now,
                   std::unique_ptr<OneShotTimer> ageTimer, QObject* parent = nullptr);
  Q_INVOKABLE void checkNow();     // client->checkNow(); exactly one client call per activation (REQ-F-025)
 signals:
  void changed();
};
```

- `snapshotAgeText` is recomputed from the injected `Clock` and refreshed every 60 s through the injected `OneShotTimer`, so tests use fakes. Age bucketing (`update_age_format`): under 1 min "just now", under 1 h "N min", under 48 h "N h", otherwise "N d". Text: `tr("Last checked %1 ago")`.
- All status fields come from `UpdateCheckClient::status()` (D-Bus properties). `checking` is the `Checking` property, so the control is disabled and busy from `PropertiesChanged`, not from a local request flag.
- The model forwards `SnapshotFileReader::snapshotRead` to `UpdatesModel::applyCheckedSnapshot`, and asks the reader to re-read when the client emits `checkCompleted` or a status change that moves `SnapshotFetchedAt`.
- `UpdateCheckClient::checkNowFailed` sets `serviceUnavailable` and `failureText = tr("Update service unavailable")`. The next successful `CheckNow()` call or status change from the service clears it. This is neutral client-side text; it is not an `UpdateCheckErrorCode`.
- The model emits no popup, dialog or notification. It exposes only text and flags (REQ-F-031).

### 4.7 QML (Updates page)

**DECIDED (user, 2026-10-09) P-11** placement and wording, in `qml/updates/UpdatesView.qml` and a new `UpdatesCheckBar.qml`:

```
Updates  ·····························  [ Last check failed: <message> ]  [ Check now ]  [ Reload ]
Last checked 3 h ago                                                  <- quiet caption row, textMuted
[ headline: Pending updates | Total download | Data as of … ]
```

- Title row: the existing `HnLabel` and the `Reload` button stay. A failure text (`objectName: "updatesCheckFailure"`, error colour, elided) and a `Controls.Button` (`objectName: "updatesCheckNowButton"`, text "Check now", "Checking…" while busy, `enabled: checkNowEnabled`) go into the same `RowLayout`. A small `Controls.BusyIndicator` (`objectName: "updatesCheckBusy"`, visible while `checking`) sits next to it. If the Holonight style lacks a usable `BusyIndicator`, fall back to the existing indeterminate `ProgressBar` pattern already used on this page.
- No `ToolTip` on the new button. An attached `ToolTip` instantiates a `Popup` item on hover, and REQ-F-031 requires that no `Popup` or `Dialog` is instantiated. The existing Reload button keeps its tooltip; it is not part of the failure path.
- The status line (`objectName: "updatesCheckStatusLine"`) shows `snapshotAgeText` for displayed valid online data, "Showing local package data" when a valid online snapshot loses the freshness selection, or "Not checked yet" without a valid online snapshot.
- Both new items have `visible: updateCheckModel.available` (REQ-F-039).
- "Reload" keeps its meaning (re-read this computer's databases). Its tooltip already says so, which keeps it distinct from "Check now".
- Import and style rules from `AGENTS.md` apply: `QtQuick.Controls as Controls`, qualified instances, no imperative style.

## 5. `AlpmUpdateChecker` algorithm

Everything below is in `src/backends/src/alpm_update_checker.cpp` and its helpers. The scratch root is `$XDG_CACHE_HOME/holonight-packages/checkdb` (fallback `$HOME/.cache/holonight-packages/checkdb`), computed by the composition root through `persistence::appCacheDir` and passed in `options.scratchRoot` (REQ-F-005).

### 5.1 Watchdog wrapper (total bound, REQ-NF-001)

libalpm has no cancel call and `curl` opens `file://` paths with a blocking `open()`, so a hung transfer cannot be interrupted from outside. `checkForUpdates()` therefore does this:

1. Create a shared `Attempt` state and start the actual check body (5.2) on a `std::thread` that owns a `shared_ptr` to the options and the `Attempt`. It never references `this`.
2. The caller waits on a future with `wait_for(options.totalBound)`.
3. On completion it returns the body's result.
4. On timeout it detaches the thread, returns `NetworkUnavailable` and does not wait. The detached thread keeps the scratch `flock` open until libalpm returns. The next check in this process, or in any process, therefore sees the lock held and returns `Busy`. Single-flight survives a wedged thread without extra flags. When the thread finally returns, it removes the older run directories and keeps its own as the retained one only if no newer one was created in the meantime (otherwise it removes itself); the sweep at the next check start handles any remainder.

The per-transfer timeout comes from libalpm itself: `alpm_option_set_disable_dl_timeout(handle, 0)` keeps its built-in low-speed abort (about 10 s below 1 byte/s). **To verify in spike S-1** against the installed libalpm/libcurl, together with the FIFO behaviour.

### 5.2 Check body

1. **Resolve and validate the scratch root.** `create_directories(scratchRoot)`. Then `lstat` it. It must be a real directory (not a symlink), owned by the effective uid, with `(mode & 0022) == 0`. If the adapter created it, set mode 0700. Otherwise return `Unknown`. (REQ-NF-006, P-7.)
2. **Take the exclusion lock.** Open `<scratchRoot>/check.lock` with `O_CREAT|O_CLOEXEC|O_NOFOLLOW`, mode 0600. Call `flock(fd, LOCK_EX|LOCK_NB)`. If held, return `Busy`.
3. **Sweep (before creating the new run directory).** Under the lock no other check is live. Remove every entry in `scratchRoot` except `check.lock` and the single newest `run-*` directory (newest by `mtime`, ties broken by name); that directory is the previous check's retained one. Stray files and symlinks are removed. Removal is symlink-safe and never follows `local` (5.4). After this step at most one old run directory exists, so at most two exist while this check runs. (P-8, D12, REQ-F-042, REQ-F-009.)
4. **First lock check.** If `<databasePath>/db.lck` exists, return `Busy`. This takes microseconds, well inside the 1 s of REQ-F-010.
5. **Preconditions.** `checkLocalDatabase(databasePath)` (existing helper) must pass, because opening a handle can create the version marker, and in step 7 `local` is a symlink to the real directory. Parse `pacman.conf` with `parsePacmanConfig` (ignore rules) and the new `parsePacmanRepositories`. Any parse failure returns `Unknown`. For each repository whose servers are all `file://`, at least one server directory must exist. Otherwise return `RepositoryUnreachable` (REQ-F-011).
6. **Create the run directory.** `mkdtemp("<scratchRoot>/run-XXXXXX")` creates it with mode 0700. It is NOT removed on exit: it is the retained directory (D12). Because the previous directory is removed only in step 14, a crash between steps 6 and 14 leaves two directories, which the next sweep reduces to the newest.
7. **Lay out the scratch dbpath.** `symlink(<databasePath>/local, run/local)`. `mkdir run/sync` with mode 0700. Copy every regular `<databasePath>/sync/*.db` and `*.db.sig` file into `run/sync` (opened with `O_NOFOLLOW`; skip `*.part` and `*.files`). **After each copy, set the copy's mtime to the source's mtime** (`utimensat`). This is mandatory: `alpm_db_update(…, force=0)` sends the local file's mtime as `If-Modified-Since`, and a copy stamped "now" would make every mirror answer "not modified" and the check would never see anything new. Record `(size, mtime)` of every source file before the copy.
8. **Second lock check.** After the copy (the test hook `afterCopyStarted` runs between steps 7 and 8), if `db.lck` exists, or if any source file's `(size, mtime)` changed since it was recorded, return `Busy` (REQ-F-010, risk R-1).
9. **Open a handle on the scratch dbpath.** `alpm_initialize(databaseRoot, run, &err)`. Then `alpm_option_set_logfile(h, "/dev/null")` (belt and braces: no log file is ever opened under the system path), `alpm_option_set_gpgdir(h, <GPGDir from pacman.conf, default /etc/pacman.d/gnupg>)`, `alpm_option_add_architecture` (from `Architecture`, `auto` resolved with `uname(2)`), `alpm_option_set_default_siglevel(h, <global SigLevel>)`, `alpm_option_set_disable_dl_timeout(h, 0)`, `alpm_option_set_parallel_downloads(h, 1)`. Do not set `fetchcb`: libalpm's built-in libcurl path is used and `XferCommand` is deliberately ignored, because honouring it would start an external process (REQ-C-002).
10. **Register repositories in configuration order.** For each `[repo]`: `alpm_register_syncdb(h, name, <effective repo SigLevel>)`, then `alpm_db_add_server(db, url)` for each server, after substituting `$repo` and `$arch`. **SigLevel rule (REQ-C-004):** the effective level is the repository's own `SigLevel` if present, otherwise the global one, otherwise pacman's built-in default (to be confirmed against pacman 7.1 in task T-03). After registering, read the value back with `alpm_db_get_siglevel` and abort with `Unknown` if it differs from the parsed one. An unparseable `SigLevel` token fails closed. The code never passes a weaker level and never omits it.
11. **Refresh.** `alpm_db_update(h, alpm_get_syncdbs(h), /*force=*/0)`. A return of `-1` goes to `classifyAlpmError(alpm_errno(h), kinds)`. Returns `0` (updated) and `1` (already current) both continue. libalpm locks only the scratch `db.lck` here, never the real one.
12. **Compare.** Read installed packages from `alpm_db_get_pkgcache(alpm_get_localdb(h))` (a fresh handle, so never stale; `local` resolves to the real database through the symlink). Call the shared `computePendingUpdates(pkgcache, syncDbs, ignoreRules)`. Set `databasesFound` and `dataAsOf` from `oldestSyncDatabaseTime(run)` (the existing helper).
13. **Release the handle.** `alpm_release(h)`. The run directory stays on disk (REQ-F-042), on success and on every failure path after step 6. Tests inspect it after `checkForUpdates()` returns.
14. **Remove the previous run directories.** On every exit path after step 6 (success or failure), remove every `run-*` directory other than the current one, using the symlink-safe removal of 5.4. At rest at most one run directory remains. Then the `flock` is released when the descriptor closes.

Nothing in the body calls `alpm_trans_*`, `QProcess`, `system`, `popen` or `exec*` (REQ-C-002, REQ-C-005).

### 5.3 Error mapping table (`alpm_error_mapping.cpp`, the single authority)

| libalpm `alpm_errno_t` (or source) | `UpdateCheckErrorCode` | Note |
|---|---|---|
| `ALPM_ERR_HANDLE_LOCK` | `Busy` | Lock contention on the scratch `db.lck`. |
| `ALPM_ERR_RETRIEVE`, `ALPM_ERR_RETRIEVE_PREPARE`, `ALPM_ERR_LIBCURL`, `ALPM_ERR_EXTERNAL_DOWNLOAD`, with at least one remote server | `NetworkUnavailable` | libalpm exposes no HTTP-versus-DNS detail (D11). |
| the same four codes with every server `file://` | `RepositoryUnreachable` | Local mirror not readable. |
| `ALPM_ERR_SERVER_BAD_URL`, `ALPM_ERR_SERVER_NONE` | `RepositoryUnreachable` | |
| `ALPM_ERR_DB_INVALID`, `ALPM_ERR_DB_VERSION`, `ALPM_ERR_DB_NOT_FOUND` | `RepositoryUnreachable` | Server answered with unusable data. |
| `ALPM_ERR_DB_INVALID_SIG`, `ALPM_ERR_SIG_MISSING`, `ALPM_ERR_SIG_INVALID`, `ALPM_ERR_GPGME` | `Unknown` | Fail closed. The errno name is logged, never the URL. See D11. |
| watchdog timeout (5.1) | `NetworkUnavailable` | |
| scratch root invalid, config unparseable, `ALPM_ERR_MEMORY/SYSTEM/BADPERMS/DISK_SPACE/NOT_A_DIR/DB_OPEN/DB_WRITE/…` and every code not listed | `Unknown` | REQ-F-013: default branch. |

REQ-F-012 and REQ-F-013 are unit-tested directly against `classifyAlpmError` for every row plus an unlisted value. No libalpm message text leaves the adapter (REQ-F-002).

### 5.4 Stale scratch directories and bounded growth

- **Never reused.** Every check creates a new `mkdtemp` directory. Nothing in the retained or any older directory is read by a new check (REQ-F-009); the retained directory exists only so that tests and developers can inspect the last layout.
- **Retention policy (P-8, D12; DECIDED, user, 2026-10-09).** The last run directory is kept after a check, whether it succeeded or failed (a failed run's directory may be partial; it is never read). When a check completes, the previous run directories are removed (step 14), so at most the most recent one remains at rest. During a check at most two exist: the previous one and the current one. The sweep at check start (step 3), under the flock and before the new run directory is created, removes all but the newest. The same sweep runs once at `holonight-packaged` start (`AlpmUpdateChecker::sweepStale()`, called from the composition root before the first check), so a crash leftover is reduced to the newest directory before anything else runs.
- **Bound.** Under the single coordinator plus the `flock` safety net, peak use is about twice the size of the real `sync/` directory (the previous plus the current copy; the real directory is roughly 10 to 15 MB with core, extra and multilib, so about 20 to 30 MB). At rest it is about one copy (10 to 15 MB) plus the zero-byte `check.lock`. After a crash, the leftover is at most two such directories, and the next service start or check start reduces it to one. A wedged detached thread (5.1) keeps its directory until libalpm returns or the process exits; the next sweep reclaims it.
- **Removal is symlink-safe and never follows `local`.** The retained directory always contains `local`, a symlink to the real local database, so this matters more than before: removal must never follow it. The remover first `unlink`s `run/local` (checking with `lstat` that it is a symlink, without resolving it), then removes the rest by walking `lstat` entries with `O_NOFOLLOW`/`AT_SYMLINK_NOFOLLOW` semantics (`openat`/`unlinkat`), never `stat`, never recursing through a symlink, and never using a path-based recursive delete that dereferences links. A symlink found anywhere else is unlinked, not followed. This is what guarantees that cleanup cannot touch the real local database (REQ-C-003, REQ-F-042). A test plants a run directory whose `local` points at a sentinel directory and asserts that the sentinel is intact after removal.

### 5.5 Mapping to REQ-C-003 (real dbpath never written)

- `local` is a symlink, so a write through it would hit the real database. The only writes libalpm could attempt there are the version marker (guarded by `checkLocalDatabase` in step 5) and nothing else, because no transaction is opened.
- `alpm_option_set_logfile("/dev/null")` removes the `/var/log/pacman.log` risk.
- The container acceptance test runs the check as a non-root user against a root-owned read-only database fixture (section 8).

## 6. Persistence format

File: `<appCacheDir>/update-snapshot.json` (`$XDG_CACHE_HOME/holonight-packages/update-snapshot.json`). Directory mode 0700, file mode 0600. JSON via Qt Core (`QJsonDocument`).

```json
{
  "schemaVersion": 1,
  "fetchedAt": 1760000000,
  "databasesFound": true,
  "dataAsOf": 1759990000,
  "count": 2,
  "updates": [
    { "name": "alpha", "installedVersion": "1.0-1", "availableVersion": "2.0-1", "repository": "core",
      "downloadSizeBytes": 2048, "installedSizeDeltaBytes": 500, "ignored": false }
  ]
}
```

- `count` is the non-ignored count (`summarizeUpdates`). On load it is recomputed and a mismatch is treated as corruption. It is stored because REQ-F-032 lists it.
- Times are Unix seconds. Sizes fit a JSON double exactly below 2^53.
- **Load** (REQ-F-033/034), read-only in every process: a missing file gives `{nullopt, Absent}`. A file that is truncated, not an object, has a missing or wrong-typed field, an unknown `schemaVersion`, a `count` mismatch, or is larger than 8 MiB gives `{nullopt, Invalid}` and is left untouched. The reader then reports "no snapshot" (`SnapshotFetchedAt == 0`).
- **Discard** (REQ-F-034): `discardInvalid()` removes the file if it is still invalid. Only `holonight-packaged`, the single writer, calls it (from `UpdateCheckService::start()`). The GUI process never does, so a GUI that reads while packaged is mid-write can never destroy a good file.
- **Writer and readers.** Packaged writes; the GUI reads. Because the write is `rename`, a reader sees either the old or the new complete file, never a partial one. A GUI read that races a first-ever write sees `Absent` and picks the file up on the next watcher event.
- **Save** (REQ-F-032, REQ-NF-004): `mkstemp` in the same directory (mode 0600), `write`, `fsync`, `rename` over the target, `fsync` of the directory. Any failure removes the temporary file and leaves the old file untouched. `JsonUpdateSnapshotStore` takes a `beforeRename` fault-injection hook used by the REQ-NF-004 test. Save failures are logged and the previously published in-memory result stays (REQ-F-035). The old file's bytes are unchanged in that case.
- Failed checks never call `save` (REQ-F-032).

## 7. Resolving the configuration items

### 7.1 Interval configuration location (P-9)

`holonight-config` is usable: `HoloNightConfig` is already a staged dependency of this repo (`tooling/module.json`, `tooling/dependencies.cmake`), and `HoloNight::Config` is an imported target. It offers `readDocument`, `DocumentSchema` and `ReloadPolicy`. But it has no packages path resolver, only `resolveAppearancePath()`, and its README says each application owns its schema and file.

**DECIDED (user, 2026-10-09) P-9**:

- File `$XDG_CONFIG_HOME/holonight/packages.toml` (fallback `$HOME/.config/holonight/packages.toml`; override with `HOLONIGHT_PACKAGES_FILE`). The path resolver lives in `apps/packaged/PackagedConfig.cpp`, mirroring `resolveAppearancePath`.
- Schema: `[updates] check_interval_minutes = 360` (integer, `ReloadPolicy::Restart`).
- A command-line option `--check-interval-minutes <n>` on `holonight-packaged` overrides it. The existing `main.cpp` already takes `--root`, `--dbpath`, `--pacman-conf` and `--debounce-ms`, so tests and the process test (`tests/packaged/packaged_process_test.cpp`) can set it.
- The raw value goes through `resolveCheckInterval` (application layer, no holonight-config dependency). A missing file or key passes `nullopt` (silent 6 h default). An empty or invalid value warns once.
- Only `holonight-packaged` reads this file; the GUI does not need the interval.
- The activation files have no arguments, so end users could not change the interval without the file. That is why the file was chosen over a command-line-only option.

### 7.2 Other numeric values (all DECIDED, user, 2026-10-09)

| ID | Item | Decided value | Reason |
|---|---|---|---|
| P-1 | Startup delay (REQ-F-020) | 60 s | Keeps login I/O free and lets the network come up. |
| P-2 | Maximum jitter J (REQ-F-021) | ±10 min, capped at interval/10 | 6 h gaps get ±10 min. A 15 min interval gets ±1.5 min. |
| P-3 | T_max (REQ-NF-001) | 120 s total, plus libalpm's own per-transfer low-speed abort (about 10 s) | Three to four small databases normally take seconds. |
| P-4 | Minimum interval (REQ-F-023) | 15 min | Values from 1 up to 14 are clamped to 15 with one warning. Empty, non-numeric, zero or negative falls back to 6 h with one warning. Missing file or key: 6 h, no warning (D9). |
| P-5 | On-demand failure vs backoff | No effect on the counter or timer. On-demand success resets the counter and re-arms the timer. | A manual failure should not delay the next automatic try or hammer mirrors. |
| P-6 | On-demand cooldown | 10 s after the previous check completed | Stops a held-down button from re-checking. |
| P-7 | Scratch permissions | Root and run directories 0700, lock file 0600 | Satisfies `mode & 0022 == 0`. |
| P-8 / D12 | Scratch cleanup | New run directory per check; the last one is kept; on completion all previous run directories are removed (at most one at rest, at most two during a check); sweep keeping only the newest at each check start (before creating the new directory) and at service start | See 5.4. |
| P-12 | `Busy` | Failed attempt for status and backoff (as REQ-F-030), also returned when the `flock` safety net is held (3.5) | |

Arithmetic check for REQ-NF-003 with the proposed values: attempts at 60 s, +5 min, +15 min, +1 h, +6 h, +6 h, +6 h are 7 attempts within about 19.4 h. The 8th falls at about 25.4 h, minus at most 30 min of jitter, so at most 7 happen within 24 h (bound 8 holds).

## 8. Test strategy

### 8.1 Layout and targets

- New executable `test_holonight_packages_checks` in `tests/CMakeLists.txt`: links `GTest`, `Qt6::Core/Test/Concurrent`, `holonight_packages_application`, `holonight_packages_snapshot_store`, `holonight_packages_domain`. **No libalpm, no Qt Quick** (REQ-NF-007). Contains the domain, policy, scheduler, service, monitor-adoption and store tests. The CTest registration sets a 10 s `TIMEOUT` to enforce the 10-second budget.
- The existing `test_holonight_packages` gains the backend, D-Bus and view-model tests.
- `test_runtime_controls` (compiled acceptance) gains `UpdateCheckModel.cpp/.h`, `UpdatesModel` changes and the new QML cases. It keeps its source boundary of fakes plus real presentation models.
- Test helpers: `tests/application/fake_clock.h`, `fake_random.h`, `fake_update_checker.h`, `fake_snapshot_store.h`; `tests/apps/fake_update_check_client.h` (`FakeUpdateCheckClient`: counts `checkNow()` calls, settable status, can emit `checkNowFailed()`); `tests/backends/file_repo_fixture.h`.
- New GUI-side tests in `test_holonight_packages`: `update_check_client_test.cpp` (the QtDBus `DBusUpdateCheckClient` against a private bus with a stub service: property mirroring, `checkNow()` call, error reply gives `checkNowFailed()`), `snapshot_file_reader_test.cpp` (watcher, atomic rename, absent/corrupt tolerance, file never modified).

### 8.2 `file://` fixture repository builder (`FileRepoFixture`)

For one test it creates under a `QTemporaryDir`:

- `mirror/core.db`, `mirror/extra.db`: copies of the checked-in `tests/fixtures/pacman/updates/sync/*.db` (the "newer" content), plus a variant with no newer versions for the empty-result case.
- `real/local/`: copy of `tests/fixtures/pacman/updates/local/`; `real/sync/*.db`: older variants of the databases with an old mtime. These variants are generated by an extension of `tests/fixtures/pacman/updates/generate.sh` (deterministic, as it is now).
- `pacman.conf` with `Server = file://<tmp>/mirror` (no mirrorlist; plus an `Include` case for the parser test) and `SigLevel = Never` by default.
- **Harness guard (REQ-C-006/-C-010):** the only way to write a conf is `FileRepoFixture::writeConf`, which asserts every `Server` URL starts with `file://` and that its path is inside the fixture's temp directory. The policy script also greps `tests/` for any `Server =` that is not a `file://` URL.
- Manifest helper (path, size, SHA-256, mtime) for REQ-F-008/-C-003.

### 8.3 Spikes before implementation (cheap, to retire risk)

- **S-1**: Does `alpm_db_update` on a `file://` server honour the default low-speed timeout, and what does the FIFO fixture do (REQ-NF-001)? Expect a blocking `open()`. The watchdog in 5.1 is the answer if so. If the watchdog's detach turns out unacceptable, the fallback is a forked child of the same binary; it would need a SPEC change to REQ-C-002's wording.
- **S-2**: Does database signature verification as a non-root user work with the Arch default (`DatabaseOptional`) and with `Required` (gpgme/trustdb write needs)? `checkupdates` works as non-root with the Arch default, which is the precedent.
- **S-3**: Confirm pacman 7.1's built-in default `SigLevel` when `pacman.conf` has none.

### 8.4 Requirement to test mapping

| REQ | Test (file : case) |
|---|---|
| F-001 | `tests/domain/update_checker_test.cpp`: a fake implementing only the port; value and error round trip; the target links no libalpm. |
| F-002 | `update_checker_test.cpp`: enumerate `kAllUpdateCheckErrorCodes` (size 4), messages non-empty from the one table, no `alpm`-looking text. |
| F-003 | `update_check_model_test.cpp`: `available` follows the capability; `alpm_update_checker_test.cpp`: `alpmBackendCapabilities()` is true. |
| F-004, F-005 | `tests/backends/alpm_update_checker_test.cpp` with `FileRepoFixture`: newer package listed with versions; no-newer gives empty success; `scratch_location_test` for `XDG_CACHE_HOME` and `HOME` fallback via `appCacheDir`. |
| F-006 | Same file, inspecting the retained run directory after `checkForUpdates()` returns (no hook): `local` is a symlink resolving to the real `local`, scratch `sync` differs from real; real `sync` is byte-identical (manifest). |
| F-007 | `pending_update_computation_test.cpp`: `loadUpdates()` and `checkForUpdates()` give identical `PendingUpdate` sequences on the same databases; policy script check that the comparison exists once (`vercmp` loop only in `pending_update_computation.cpp`). |
| F-008 | `alpm_update_source_test.cpp` additions: `Server=file:///nonexistent` leaves `loadUpdates()` unchanged, no scratch directory, manifest identical. |
| F-009 | `alpm_update_checker_test.cpp`: plant a corrupt `run-*` directory and a fake db, and make the previous check's retained directory disagree with the real databases; both results equal the clean-root result. |
| F-042 | `scratch_dir_test.cpp` and `alpm_update_checker_test.cpp`: three checks (one failing) leave one run directory; a probe hook-free observer (a blocked fake transfer via the FIFO fixture) sees at most two during a check; planted stale directories are reduced to the newest by the start sweep and by the service-start sweep; a planted run directory whose `local` points at a sentinel directory is removed with the sentinel intact (manifest equal). |
| F-010 | Pre-created `db.lck` returns `Busy` in under 1 s; hook `afterCopyStarted` creates `db.lck` and gives `Busy`; last-good unchanged is asserted in `update_check_service_test.cpp`. |
| F-011 | `Server=file://<tmp>/missing` gives `RepositoryUnreachable`. |
| F-012, F-013 | `alpm_error_mapping_test.cpp`: every table row, plus an unlisted errno gives `Unknown`. |
| F-014, F-015, F-016, F-017 | `tests/application/update_check_service_test.cpp` with `FakeUpdateChecker`, `FakeClock`: count and list replaced, one `checkCompleted`; failure keeps last-good; failure fields; `snapshotFetchedAt` = T and `lastCheckTime` = T+3 h. |
| F-015 (D-Bus and view-model side) | `packaged_update_check_dbus_test.cpp` and `update_check_model_test.cpp`: counts unchanged after a failure; `Count` never published as 0 by a failure. |
| F-018 | `packaged_update_check_dbus_test.cpp` reads each property on a private bus; `update_check_model_test.cpp` checks each field with a `FakeUpdateCheckClient` status. |
| F-019 | Service test with an empty store: D-Bus `SnapshotFetchedAt == 0`, existing `Updates.Count`/`State` unchanged; model test: `hasSnapshot == false`, text "Not checked yet", no age text; runtime acceptance in both styles. |
| F-020 | `update_check_scheduler_test.cpp`: no call before 60 s, exactly one at the delay. |
| F-021, F-022 | Scheduler test, 48 h seeded run, intervals within bounds and not all equal; 2 h configured over 12 h. |
| F-023 | `update_check_policy_test.cpp`: `nullopt` (no file, no key) gives 6 h and zero warnings; empty, `abc`, `0` give 6 h and exactly one warning; `5` gives 15 min and exactly one warning. `PackagedConfig` test: no file, file without the key, and CLI override over file. |
| F-024 | `packaged_update_check_dbus_test.cpp`: `CheckNow()` over a private bus reaches the coordinator and produces exactly one checker call. |
| F-025 | `update_check_model_test.cpp`: with `FakeUpdateCheckClient`, one `checkNow()` activation produces exactly one client call and no other check path exists; `update_check_client_test.cpp`: the QtDBus client issues the `CheckNow` call with auto-start; `packaged_process_test.cpp`-style test: the call starts the service from an activation file on a private session bus. |
| F-026, F-027 | Service test: blocked fake; two more requests (scheduled and `CheckNow()`) give no extra call; the same outcome for all; 50 requests from threads give `maxConcurrency() == 1`. `alpm_update_checker_test.cpp`: with the `flock` held by a second holder the adapter returns `Busy`. |
| F-043 | `snapshot_file_reader_test.cpp` and `update_check_model_test.cpp`: status change plus a written snapshot file updates `UpdatesModel` by `selectFresherSnapshot`; atomic rename without any D-Bus signal also updates it; absent/corrupt file leaves the list and the file untouched. |
| F-044 | `update_check_model_test.cpp`: `checkNowFailed` gives "Update service unavailable", control re-enabled, enum still has four codes; `update_check_client_test.cpp`: D-Bus error reply gives `checkNowFailed`; runtime acceptance in both styles (no `Popup`/`Dialog`). |
| F-045 | `update_check_service_test.cpp` and `packaged_update_check_dbus_test.cpp`: `CheckNow()` while blocked and at 9 s vs 11 s after completion (fake clock). |
| F-028, F-029, F-030 | Scheduler test: gaps 5 min, 15 min, 1 h, then 6 h plus jitter; fail, fail, success, fail gives 5 min; `Busy` follows the same path. |
| F-031 | `test_runtime_controls.cpp` under Holonight and Fusion: after a fake failure the text is in the control row and no `Popup`/`Dialog` item exists (walk the item tree). |
| F-032, F-033, F-034, F-035 | `tests/persistence/json_update_snapshot_store_test.cpp`: fields present; restart test; missing, truncated and unknown-version fixtures: `load()` reports `Absent`/`Invalid` and leaves the file byte-identical (read-only), `discardInvalid()` removes only an invalid file and leaves a valid one; `update_check_service_test.cpp`: `start()` calls `discardInvalid()` for an `Invalid` load; read-only directory case. The F-035 test calls `ASSERT_NE(geteuid(), 0)` first (risk R-6). |
| F-036, F-037, F-038, F-039 | `update_check_model_test.cpp` (fake clock and fake client; age text "3 h"; `Checking` gives busy and disabled; four failure texts with age kept; hidden when `CanCheck` is false) plus runtime acceptance in both styles; `packaged_update_check_dbus_test.cpp` reads `CanCheck == false` for a backend without the capability. |
| F-040, F-041 | `packaged_dbus_test.cpp` stays unmodified and must pass; new `packaged_update_check_dbus_test.cpp` compares the new interface with the checked-in XML the same way `membersOf` does and calls/reads every added member. A script check diffs the `Updates` interface block against `git show HEAD:` to prove it is unchanged. |
| NF-001 | `alpm_update_checker_test.cpp`: FIFO as a database file with `totalBound = 2 s`: failure within bound + 5 s. The test unblocks the FIFO at the end so the detached thread can finish. |
| NF-002 | D-Bus test with a blocked fake checker: property read and `CheckNow()` each under 100 ms. |
| NF-003 | Scheduler test: 24 h, always failing, at most 8 automatic attempts (7 expected). |
| NF-004 | Store test with the `beforeRename` hook: previous file byte-identical, no partial target. |
| NF-005 | Service test: log capture via `qInstallMessageHandler` has start/finish lines with outcome and duration; a credential-bearing URL in the fixture conf never appears in any captured line. |
| NF-006 | `scratch_dir_test.cpp`: owner is the current uid and `mode & 0022 == 0`; rejects a symlinked or group-writable root. |
| NF-007 | CTest `test_holonight_packages_checks` links no libalpm (checked by `ldd`/target link properties in a CMake test) and runs under the 10 s `TIMEOUT`. |
| C-001 | `scripts/check-layering.py` (new CTest `layering_policy` plus `layering_policy_fixtures`, and `task layering-check` in `task check`). Fails if a file outside `src/backends` and the two composition roots (`apps/packaged/main.cpp`, `apps/packages/app/PackagesApplication.cpp`) includes `<alpm.h>` or names `alpm_*`, `dbpath` or `databasePath`. Fixtures under `tests/policy/fixtures/` prove it fires. See D6 for why it matches identifiers and includes, not the prose "sync database". |
| C-002 | Layering script rule: no `QProcess`, `system(`, `popen`, `exec*`, `fork`, `posix_spawn` in the checker sources. Test: a fake `checkupdates` first on `PATH` writes a marker file; after a successful check the marker is absent. |
| C-003 | Manifest before/after. The container lane runs the check as a non-root user against a root-owned, read-only database fixture (`chmod -R a-w`). Under root the test fails by design. |
| C-004 | `SigLevel = Required` fixture with an unsigned database gives failure, snapshot unchanged; `pacman_repositories_test.cpp`: parsed level equals applied level; unparseable token fails closed. |
| C-005 | Layering script rule: no `alpm_trans_` and no `pacman -S/-U/-R`. Test: installed set unchanged after a check. |
| C-006 | Script check for the exact line "tests may sync only fixture repositories under a temp dir via file://" in `AGENTS.md` and `CLAUDE.md`; the `FileRepoFixture` guard test (a non-`file://` URL aborts). |
| C-007 | Layering script rule: no `org.freedesktop.Notifications` and no NetworkManager subscription strings. A CI step diffs against the base branch and fails if any path under `qml/packages` or `qml/explore` changed. |
| C-008 | The existing CTest entries `runtime_controls_Holonight`, `runtime_controls_Fusion`, `runtime_qml_import_policy`, `runtime_qml_import_policy_fixtures` plus `task qml-import-check`, `qmltypes-check` and `qml-lint` must pass with the new QML. New policy fixtures cover `UpdatesCheckBar.qml`. |
| C-009 | `FakeUpdateChecker::callerThreads()` shows a thread different from the packaged main thread for requests from the scheduler and from `CheckNow()`. Layering script rule: no `UpdateChecker`, `UpdateCheckService`, `UpdateCheckScheduler` or `AlpmUpdateChecker` under `apps/packages`. |
| C-010 | Layering script rule over `tests/`: no `QTest::mouse*`/`keyClick*`/`QTest::touch`, no transaction API, no non-fixture URL. |

## 9. Migration and compatibility

- **`UpdateSource` / `loadUpdates()`**: contract unchanged (local only, no network, no writes). Only the internals change: the loop body moves to `computePendingUpdates`. `tests/backends/alpm_update_source_test.cpp` is the regression guard. Parity caveat in D5.
- **`UpdateMonitor`**: additive `adoptOnline()`. Its options, signals and existing tests are untouched. When nothing is adopted the code path is the old one.
- **`UpdateStatusService`**: the one-argument constructor stays, so `packaged_dbus_test.cpp` and `update_status_client_test.cpp` compile and pass unchanged. The two-argument overload is used by `main.cpp`.
- **D-Bus**: additive new interface (4.5). No change to the existing interface, the activation files or the install rule for the XML (the same file is installed to the same place).
- **`UpdatesModel`**: additive `applyCheckedSnapshot()`, which merges the file-read snapshot with the model's own local result. Its constructor and QML properties stay, so `updates_model_test.cpp`, `updates_view_test.cpp` and `test_runtime_controls.cpp` need no change for existing behaviour.
- **`UpdateStatusClient`**: unchanged. The sidebar badge now reflects the online snapshot automatically because the monitor adopts it. The new `DBusUpdateCheckClient` is a separate class beside it, so `update_status_client_test.cpp` is unaffected.
- **GUI D-Bus behaviour change**: the GUI now makes one call that can activate the service (`CheckNow()`), where it previously only observed. This is the only place the GUI starts `holonight-packaged`.
- **Persistence**: `AlpmConnectionCache` untouched. The snapshot store is a separate new target.
- **Docs**: `AGENTS.md` and `CLAUDE.md` get the carve-out line (REQ-C-006) before the adapter merges, and a mention of the new `holonight_packages_snapshot_store` target and the updated architecture notes (the `application` paragraph and the Testing notes).
- **Cache directory**: new files appear under `~/.cache/holonight-packages/` only. Nothing is written outside the user's cache and nothing needs a migration.

## 10. Key decisions and rationale

| # | Decision | Rationale |
|---|---|---|
| K1 | `UpdateChecker` is a new port, separate from `UpdateSource`. | `UpdateSource` is documented as local-only and is called from the GUI and the monitor. Mixing a network call in would break REQ-F-008. |
| K2 | Fresh libalpm handle on a private scratch dbpath for every check. | No cached state, no stale-handle bugs, no interaction with `AlpmConnectionCache`. |
| K3 | Copy `sync/` (preserving mtimes) instead of downloading everything. | `If-Modified-Since` keeps bandwidth low, and the copy keeps the real directory untouched. |
| K4 | `local` is a symlink, not a copy. | A copy of thousands of small files is slow. The symlink is protected by the version-file precondition and a no-transaction rule. |
| K5 | Error code is the only thing that crosses the port; messages come from one table. | REQ-F-002 and NF-005: no libalpm text, no URLs. |
| K6 | Single-flight on the home thread through queued invocations, no mutex. | Simplest correct model for REQ-F-026/027, and the same design as `UpdateMonitor`. |
| K7 | Private one-thread pool for the checker. | Isolates a long-blocking check from the global pool that page loads use. |
| K8 | `flock` on the scratch root as a safety net (second packaged instance, wedged detached thread, sweep exclusion). | Single-flight between GUI and service is no longer needed because only packaged checks; the lock still protects the scratch root and the sweep. |
| K9 | Watchdog thread with detach for T_max. | libalpm cannot be cancelled (F9). |
| K10 | Online snapshot adopted into `UpdateMonitor` through the freshness rule. | One status path for D-Bus, no duplicated summary logic, and local evidence about the installed set still wins when newer. |
| K11 | A second D-Bus interface. | Strictly additive. Keeps the existing test and any external client untouched. |
| K12 | JSON file with temp-file plus rename for the snapshot. | Satisfies REQ-NF-004. SQLite (already linked as `Qt6::Sql`) adds transactions that are not needed for one small document. |
| K13 | Separate libalpm-free `snapshot_store` target and test executable. | The only way to satisfy REQ-NF-007 given F6 and F7. |
| K14 | `UpdateCheckModel` as a separate view-model. | Keeps `UpdatesModel` focused, and tests stay cheap (REQ-C-008 risk R-5). |
| K15 | Only `holonight-packaged` checks; the GUI calls D-Bus `CheckNow()` through an `UpdateCheckClient` port (P-14 reversed). | One coordinator, one scheduler, one writer. No libalpm network code in the GUI process. The port keeps the view-model testable with a fake. |
| K16 | The snapshot file is the list hand-off to the GUI; the writer is packaged only. `load()` is read-only, `discardInvalid()` is writer-only. | D-Bus carries no list. A read-only reader cannot destroy a good file during a concurrent write. |
| K17 | Keep the last scratch run directory (D12), remove the previous ones on completion. | Tests and developers can inspect the last layout without a hook; peak 2x and rest 1x of the sync directory. |

## 11. Alternatives considered

| Alternative | Why rejected |
|---|---|
| Run the `checkupdates` script (pacman-contrib). | Forbidden by REQ-C-002. Also adds a runtime dependency and prevents precise error classification, timeouts and SigLevel control. |
| Extend `UpdateSource::loadUpdates()` with an online mode. | Breaks the local-only, no-write contract (REQ-F-008). It is also used synchronously by GUI reloads and the monitor. |
| Refresh the real sync databases (`pacman -Sy` equivalent). | Needs root. It creates partial-upgrade risk. It breaks "transactionally inert". |
| Hardlink instead of copy for `sync/`. | `fs.protected_hardlinks` blocks linking root-owned files, and `EXDEV` across filesystems. |
| Persistent scratch reused between checks. | Forbidden by REQ-F-009. Reuse also hides corruption. (Retaining the last run directory for inspection, K17, is different: it is never read by a later check.) |
| Custom `alpm_option_set_fetchcb` with Qt Network. | Reimplements transport, retries and signature download. It could allow true cancellation, but is far larger. Kept as a fallback if spike S-1 shows the watchdog is not acceptable. |
| Forked child process of the same binary to get a hard kill. | Strongest timeout, but it blurs REQ-C-002 ("no external process") and needs IPC. Fallback only. |
| `QtConcurrent` on the global pool, like `UpdateMonitor`. | A blocking check could starve page loads on small machines. |
| Dedicated raw `QThread` per check. | The private one-thread pool gives the same isolation with `QFutureWatcher` integration used elsewhere. |
| Polling loop with `sleep`. | Blocks a thread and cannot be driven by a fake clock. A single-shot `QTimer` per next check is used instead (it is armed only after a check ends). |
| systemd user timer unit instead of an in-process timer. | A second unit to install, no fake clock, no shared backoff state. Note that the in-process timer uses the monotonic clock, so a long suspend can delay the next check (R-10). |
| **CHOSEN (P-14 reversed, user, 2026-10-09):** the GUI delegates "Check now" to D-Bus `CheckNow()` on bus-activated `holonight-packaged` and reads the list from the snapshot file. | True global single-flight, one scheduler and one writer. The list is not on D-Bus, so the snapshot file (read-only in the GUI, watched) is the hand-off. SPEC REQ-F-025 and REQ-C-009 were rewritten to match. |
| **REJECTED:** an in-process `UpdateCheckService` (with `AlpmUpdateChecker`) in the GUI process, on-demand only (the former P-14). | Two coordinators in two processes need a `flock` and a `Busy` result to stay single-flight, so a GUI click during an automatic check shows a spurious "busy" failure (the former R-13). Two processes would write the snapshot file and need a bidirectional file watcher (the former optional P-16). libalpm network code and a libcurl global-init race (the former R-14) would live in the GUI. Duplicated status paths for the badge and the page. Its one advantage, "Check now" working with packaged down, is replaced by D-Bus activation of packaged. |
| Add members to the existing `Updates` interface. | Breaks the existing introspection test's "only method is Refresh" assertion (D3). |
| Strict "unknown" encoding for `Count` (for example `UINT32_MAX`). | Changes the meaning of an existing member (REQ-F-040). |
| Config via `QSettings` or environment only. | Not aligned with the holonight-config direction. Environment-only cannot be set under D-Bus activation. |

## 12. Spec/code discrepancies

| # | SPEC says | Code shows | Handling in this design |
|---|---|---|---|
| D1 | REQ-F-003: "`BackendCapabilities` shall expose a `canCheckForUpdates` flag", as if it exists. | No `BackendCapabilities` type exists anywhere in the repository. | It is created in `src/domain` (4.1). The Alpm backend exposes it through `alpmBackendCapabilities()`. |
| D2 | REQ-F-019: until a snapshot exists the count is unknown, "not 0 updates". | `UpdateMonitor` computes a real local count within moments of start, and `UpdatesModel` shows the local list. Existing `Count` is `0` while `State == loading`. | **DECIDED (user, 2026-10-09): Option A (P-15).** `Count` keeps its semantics. "No online snapshot yet" is `SnapshotFetchedAt == 0`, `hasSnapshot == false` and the "Not checked yet" status line. REQ-F-019 was rewritten to match. Option B (hide the local count) is rejected. |
| D3 | REQ-F-040/-F-041 speak of extending the existing `UpdatesAdaptor` interface, and require existing D-Bus tests to pass unmodified. | `packaged_dbus_test.cpp:185` asserts `Refresh` is the only method of `org.holonight.Packages1.Updates`. | A second interface in the same XML (4.5). Compatible additive change; not a member-level extension of `Updates`. |
| D4 | The logical name `lastError` is a neutral code. | `Updates.LastError` already exists with a different meaning (free-text local-evaluation error). | New wire name `LastCheckError`. |
| D5 | REQ-F-007: "same helper used by `loadUpdates()`". | The comparison is inline in `AlpmUpdateSource::loadUpdates()`. `AlpmConnectionCache` registers sync databases alphabetically, while pacman and the checker use `pacman.conf` order. | Extract `computePendingUpdates` (F1). The parity test uses a fixture whose configuration order equals alphabetical order. A real difference only appears with custom repositories ordered non-alphabetically. Aligning `loadUpdates()` to configuration order is not done here (it would change an existing behaviour). |
| D6 | REQ-C-001: no "dbpath, sync database concept" in `src/domain` or `src/application`. | `src/domain` already has `SyncPackage`, `ExploreSource`, and comments and fields such as `databasesFound` and "oldest sync database" in `pending_update.h`, `update_source.h` and `update_status.h`. | The policy rule matches includes and identifiers (`<alpm.h>`, `alpm_*`, `dbpath`, `databasePath`), not the phrase "sync database". New domain and application code avoids those identifiers. |
| D7 | REQ-NF-001: a never-completing FIFO makes the check fail within T_max + 5 s, using per-transfer timeouts. | libcurl opens `file://` with a blocking `open()`, and libalpm cannot cancel a transfer. Low-speed timeouts do not apply before the open. | Watchdog with detach (5.1). Spike S-1 confirms. The detached thread is a documented leak until libalpm returns. |
| D8 | REQ-C-004: use the `SigLevel` of the configured `pacman.conf`. | `parsePacmanConfig` ignores `SigLevel`, `Server`, `Include`. `AlpmConnectionCache` registers with `ALPM_SIG_USE_DEFAULT` and no servers. | New `pacman_repositories` parser with fail-closed `SigLevel` handling and read-back with `alpm_db_get_siglevel` (5.2, steps 5 and 10). |
| D9 | REQ-F-023: a missing interval logs one warning. | A fresh install has no config file and no key, so a literal reading warns on every start. | **DECIDED (user, 2026-10-09):** absent file or key means the 6 h default, silently. A present but empty, non-numeric or non-positive value warns once and uses 6 h. A value below 15 min is clamped to 15 min with one warning. REQ-F-023 was rewritten; the "missing" test case is now "no file" and "file without the key" (zero warnings), and the invalid cases use an empty value, `abc` and `0`. |
| D10 | REQ-F-025/-F-027 speak of "the service" and "the coordinator" as if there were one. | `holonight-packaged` and `holonight-packages` are separate processes. The UpdatesModel lives in the GUI. | **DECIDED (user, 2026-10-09):** exactly one coordinator, in `holonight-packaged`. The GUI calls D-Bus `CheckNow()` and reads the list from the snapshot file (3.5). The `flock` is a safety net only. REQ-F-024 to REQ-F-027 and REQ-C-009 were rewritten. |
| D11 | REQ-F-002: exactly four codes. | libalpm reports HTTP 404, DNS failure and TLS failure all as `ALPM_ERR_RETRIEVE`/`LIBCURL`. Signature failures have no matching neutral code. | **DECIDED (user, 2026-10-09):** remote failures are `NetworkUnavailable`. Signature failures are `Unknown`, with the errno name logged. Exactly four codes; no `SignatureInvalid`. The GUI-only "Update service unavailable" message is not a code. |
| D12 | REQ-F-006: "After a check, the scratch `local` entry is a symlink…; scratch `sync` files differ…". | With remove-on-completion nothing would remain after the check. | **DECIDED (user, 2026-10-09):** keep the last scratch run directory; previous ones are removed on completion; the start sweep and the service-start sweep keep only the newest. The `beforeCleanup` hook is dropped; tests inspect the retained directory. See 5.2 steps 3, 6, 13, 14 and 5.4. |
| D13 | REQ-NF-007: the application-layer test target does not link libalpm and persistence tests run without it. | One `test_holonight_packages` links backends and persistence; `holonight_packages_persistence` exports libalpm PUBLIC. `AGENTS.md` lists four static libraries. | New target `holonight_packages_snapshot_store` and a new test executable (section 8.1). `AGENTS.md` needs a one-line mention in addition to the carve-out line. |
| D14 | REQ-C-006 asks for one line in `AGENTS.md` and `CLAUDE.md` before the adapter merges. | Both files currently say tests must never sync repositories (`CLAUDE.md` testing notes, `AGENTS.md` runtime paragraph). | The edit is part of the first task of the implementation plan, ahead of the adapter. This design does not edit those files (the assignment forbids it). |
| D15 | Overview: scheduling, coordinator and backoff "wrapping `UpdateMonitor`". | `UpdateMonitor` has no hook for external snapshots. | One small additive method, `adoptOnline()`, plus the freshness rule (3.4). The scheduler and service are separate classes and do not subclass the monitor. |
| D16 | REQ-C-007: no file under `qml/packages` or `qml/explore` changes. | `WorkspaceWindow.qml` (under `qml/workspace`) must forward the new `updateCheckModel`. | Allowed, since only `qml/workspace` and `qml/updates` change. The explicit source lists in `apps/packages/CMakeLists.txt` and `tests/CMakeLists.txt` (`test_runtime_controls`) must also be extended. |
| D17 | REQ-F-039 and F-003: hide the control where `canCheckForUpdates` is false. | `CanCheck` lives on the D-Bus interface of packaged, and the GUI client does not read the property when the service is down. | Design default, to be confirmed in review: `available` is true unless the service is reachable and reports `CanCheck == false` (R-18). |

## 13. Decisions (all approved by the user, 2026-10-09)

Nothing here is a proposal any more. The only items not yet closed are the verification spikes S-1..S-3 (section 8.3), which are tasks that confirm assumptions, not decisions.

| ID | Item | Status | Decision | See |
|---|---|---|---|---|
| P-1 | Startup delay | DECIDED (user, 2026-10-09) | 60 s | 7.2 |
| P-2 | Jitter | DECIDED (user, 2026-10-09) | ±10 min, capped at interval/10 | 7.2 |
| P-3 | Total bound and per-transfer timeout | DECIDED (user, 2026-10-09) | 120 s total; libalpm low-speed abort per transfer | 5.1, 7.2 |
| P-4 | Minimum interval | DECIDED (user, 2026-10-09) | 15 min (clamp with one warning) | 7.2 |
| P-5 | On-demand failure vs automatic backoff | DECIDED (user, 2026-10-09) | No effect on backoff; on-demand success resets and re-arms | 4.3 |
| P-6 | On-demand cooldown | DECIDED (user, 2026-10-09) | 10 s, a documented no-op, only in `holonight-packaged` | 3.3 |
| P-7 | Scratch permissions | DECIDED (user, 2026-10-09) | 0700 directories, 0600 lock file | 5.2 |
| P-8 / D12 | Scratch cleanup | DECIDED (user, 2026-10-09) | Keep the last run directory; remove previous ones on completion; keep only the newest in the start sweep and the service-start sweep | 5.2, 5.4 |
| P-9 | Config location | DECIDED (user, 2026-10-09) | `$XDG_CONFIG_HOME/holonight/packages.toml`, `[updates] check_interval_minutes`, via holonight-config; `--check-interval-minutes` overrides | 7.1 |
| P-10 | D-Bus names (with D3, D4) | DECIDED (user, 2026-10-09) | Second interface `org.holonight.Packages1.UpdateCheck`; members in 4.5 | 4.5 |
| P-11 | Updates page placement and wording | DECIDED (user, 2026-10-09) | "Check now" button and failure text in the title row, "Last checked 3 h ago" caption below it | 4.7 |
| P-12 | `Busy` handling | DECIDED (user, 2026-10-09) | Failed attempt for status and backoff; also returned when the `flock` safety net is held | 3.5 |
| P-13 | Which list is displayed | DECIDED (user, 2026-10-09) | Freshness rule `selectFresherSnapshot`, applied in the GUI `UpdatesModel` (local result vs snapshot file) and in the packaged `UpdateMonitor` | 3.4 |
| P-14 | GUI check path | DECIDED (user, 2026-10-09), REVERSED from the earlier proposal | The GUI runs no checker. "Check now" calls D-Bus `CheckNow()` on bus-activated `holonight-packaged` through `UpdateCheckClient` | 3.5, 11 |
| P-15 / D2 | No-snapshot state on the wire | DECIDED (user, 2026-10-09) | Option A: `Count` unchanged; "no snapshot" is `SnapshotFetchedAt == 0` ("Not checked yet") | D2, 4.5 |
| P-16 | Snapshot file watching and hand-off | DECIDED (user, 2026-10-09), now MANDATORY | The GUI reads the snapshot file read-only on `StatusChanged` / property changes and through a file watcher; only packaged writes or discards | 3.5, 6 |
| D1, D3 to D8, D13 to D16 | Spec/code discrepancies | DECIDED (user, 2026-10-09) | Handled as written in section 12 | 12 |
| D9 | Missing interval | DECIDED (user, 2026-10-09) | Silent default for a missing file or key | 12 |
| D11 | Signature failures | DECIDED (user, 2026-10-09) | `Unknown`, exactly four codes | 12 |
| S-1 to S-3 | Verification spikes | OPEN (verification tasks, not decisions) | Timeout behaviour, signature checks as non-root, default `SigLevel` | 8.3 |

## 14. Known risks

| # | Risk | Mitigation |
|---|---|---|
| R-1 | Copy taken during a real `pacman -Sy`. | Lock check before and after, plus `(size, mtime)` re-check of the source files; `Busy`. SPEC risk 1. |
| R-2 | Mirrors hammered by repeated failures. | Backoff, 8-per-24 h bound, single-flight, cooldown. SPEC risk 2. |
| R-3 | Hung transfer stalls the worker. | Watchdog returns at T_max; the stuck thread is detached and guarded by `flock`. A permanently stuck libalpm call leaks one thread and one directory until process exit (D7). |
| R-4 | Stale persisted snapshot looks current. | The age line is always shown; the freshness rule lets a newer local sync win (3.4). |
| R-5 | Dual-style UI acceptance is expensive. | The UI is one small QML file; logic sits in `UpdateCheckModel` with cheap tests. |
| R-6 | Permission tests are meaningless as root. | Tests assert `geteuid() != 0`. The CI lane user is to be verified (lane scripts in `scripts/ci`). |
| R-7 | Scratch growth, and the retained run directory contains a `local` symlink to the real local database. | Section 5.4: at most two run directories during a check (peak about 2x the sync directory), one at rest. The start sweep and the service-start sweep keep only the newest. Removal never follows symlinks, in particular `local`; a sentinel-directory test proves it. Stale or retained contents are never read. SPEC risk 7. |
| R-8 | Flaky timing tests. | Fake clock, fake timer, seeded random everywhere. |
| R-9 | Signature verification as non-root may need writable gnupg state, so `Required` configurations could fail for every user. | Spike S-2. The failure is fail-closed (`Unknown`), never a weaker level. |
| R-10 | The monotonic `QTimer` can fire late after a long suspend. | Acceptable: the next check runs soon after resume. A wall-clock sanity check can be added later. |
| R-11 | Installed-set change without a sync does not flip the freshness rule, so an old online list can show already-installed updates. | Offline catalog reevaluation on installed-database events removes installed upgrades without another online check. |
| R-12 | `src/persistence` exposes libalpm headers to anything that links it. | The new store sits in its own target. The layering script checks includes, not include paths. |
| R-13 | (Largely resolved by P-14 reversed.) The GUI-versus-packaged `Busy` collision no longer exists because only packaged checks. What remains: the GUI shows a stale `Checking` state if packaged dies mid-check, and `Busy` can still appear for a second packaged instance or a wedged thread. | The GUI clears its busy state when the bus name disappears (service watcher). `CheckNow()` while `Checking` is a no-op on the service. The `flock` still returns `Busy` for the leftover cases. |
| R-14 | (Largely resolved.) libalpm's libcurl global initialisation race cannot involve the GUI because the GUI performs no network transfers. In packaged, a second handle in the same process is only the existing local-only `AlpmConnectionCache`. | Initialise a throwaway handle on the home thread at packaged start, or confirm in spike S-1. |
| R-16 | The snapshot file is the only carrier of the update list, so the GUI list can lag a successful check if a file event and the signal are both missed, or if the cache directory is on a filesystem where `QFileSystemWatcher` is unreliable. | The GUI also reads on `StatusChanged`/`PropertiesChanged` and at start; the age line shows the real `SnapshotFetchedAt`. |
| R-17 | A bus-activated `CheckNow()` makes the GUI start `holonight-packaged`, which then keeps running and checking every 6 h even if the user never wanted background checks. | This is the intended product behaviour (D-Bus service that watches updates). Flagged for user confirmation; see the summary. |
| R-18 | While `holonight-packaged` is not running, `CanCheck` is unknown to the GUI. | `available` is true unless the service is reachable and reports `CanCheck == false`; the control stays usable because `CheckNow()` activates the service. Flagged as D17. |
| R-15 | Credentials in mirror URLs. | URLs are never logged or returned. Tested with a credential-bearing URL (NF-005). |

## 15. Requirement traceability

Rewritten requirements are marked "(rewritten)" and new ones "(new)" (REQ-F-042 to REQ-F-045). F-030, F-031, F-033, F-035 to F-039, F-041, NF-001 and NF-006 had wording or value updates only.

| REQ | Component(s) | Test |
|---|---|---|
| F-001 | `src/domain/update_checker.h` | `tests/domain/update_checker_test.cpp` |
| F-002 | `update_checker.cpp` message table | `update_checker_test.cpp` |
| F-003 | `backend_capabilities.h`, `backend_capabilities_alpm.h`, `UpdateCheckModel` | `update_check_model_test.cpp`, `alpm_update_checker_test.cpp` |
| F-004 | `alpm_update_checker.cpp` (5.2) | `alpm_update_checker_test.cpp` |
| F-005 | `cache_locations`, `ScratchRoot` | `scratch_location_test.cpp`, `alpm_update_checker_test.cpp` |
| F-006 (rewritten) | `scratch_dir.cpp`, 5.2 step 7 | `alpm_update_checker_test.cpp` (retained run directory, no hook) |
| F-007 | `pending_update_computation.cpp` | `pending_update_computation_test.cpp`, layering script |
| F-008 | `AlpmUpdateSource` (contract unchanged) | `alpm_update_source_test.cpp` additions |
| F-009 (rewritten) | `scratch_dir.cpp` (mkdtemp, sweep) | `alpm_update_checker_test.cpp`, `scratch_dir_test.cpp` |
| F-042 (new) | `scratch_dir.cpp` (retention, sweep, symlink-safe removal), 5.2 steps 3, 6, 13, 14, 5.4 | `scratch_dir_test.cpp`, `alpm_update_checker_test.cpp` |
| F-010 | 5.2 steps 4 and 8 | `alpm_update_checker_test.cpp`, `update_check_service_test.cpp` |
| F-011 | 5.2 step 5 | `alpm_update_checker_test.cpp` |
| F-012 | `alpm_error_mapping.cpp` | `alpm_error_mapping_test.cpp` |
| F-013 | `alpm_error_mapping.cpp` | `alpm_error_mapping_test.cpp` |
| F-014 | `UpdateCheckService`, `UpdateMonitor::adoptOnline` | `update_check_service_test.cpp`, `update_monitor_adopt_test.cpp` |
| F-015 | `UpdateCheckService` | `update_check_service_test.cpp`, `packaged_update_check_dbus_test.cpp`, `update_check_model_test.cpp` |
| F-016 | `UpdateCheckService` | `update_check_service_test.cpp` |
| F-017 | `UpdateCheckService` | `update_check_service_test.cpp` |
| F-018 | `UpdateCheckAdaptor`, `DBusUpdateCheckClient`, `UpdateCheckModel` | `packaged_update_check_dbus_test.cpp`, `update_check_model_test.cpp` |
| F-019 (rewritten) | `UpdateCheckService`, `UpdateCheckModel` | `update_check_service_test.cpp`, `update_check_model_test.cpp`, runtime acceptance |
| F-020 | `UpdateCheckScheduler` | `update_check_scheduler_test.cpp` |
| F-021 | `UpdateCheckScheduler`, `nextAutomaticDelay` | `update_check_scheduler_test.cpp`, `update_check_policy_test.cpp` |
| F-022 (rewritten) | `resolveCheckInterval`, `PackagedConfig` | `update_check_scheduler_test.cpp`, `update_check_policy_test.cpp`, `PackagedConfig` test |
| F-023 (rewritten) | `resolveCheckInterval` | `update_check_policy_test.cpp` |
| F-024 | `UpdateCheckAdaptor::CheckNow` | `packaged_update_check_dbus_test.cpp` |
| F-025 (rewritten) | `UpdateCheckModel::checkNow`, `UpdateCheckClient`, `DBusUpdateCheckClient` | `update_check_model_test.cpp` (fake client), `update_check_client_test.cpp`, `packaged_update_check_dbus_test.cpp` |
| F-026 (rewritten) | `UpdateCheckService` (join, packaged only) | `update_check_service_test.cpp` |
| F-027 (rewritten) | `UpdateCheckService` (single coordinator), `flock` safety net | `update_check_service_test.cpp`, `alpm_update_checker_test.cpp` |
| F-043 (new) | `SnapshotFileReader`, `UpdatesModel::applyCheckedSnapshot`, `selectFresherSnapshot` | `snapshot_file_reader_test.cpp`, `update_check_model_test.cpp` |
| F-044 (new) | `DBusUpdateCheckClient::checkNowFailed`, `UpdateCheckModel::failureText`, `UpdatesCheckBar.qml` | `update_check_client_test.cpp`, `update_check_model_test.cpp`, runtime acceptance |
| F-045 (new) | `UpdateCheckService` (cooldown, join) | `update_check_service_test.cpp`, `packaged_update_check_dbus_test.cpp` |
| F-028 | `UpdateCheckScheduler`, `nextAutomaticDelay` | `update_check_scheduler_test.cpp` |
| F-029 | `UpdateCheckScheduler` | `update_check_scheduler_test.cpp` |
| F-030 | `UpdateCheckService`, `UpdateCheckScheduler` | `update_check_service_test.cpp`, `update_check_scheduler_test.cpp` |
| F-031 | `UpdatesCheckBar.qml`, `UpdateCheckModel` | `test_runtime_controls.cpp` (both styles) |
| F-032 | `JsonUpdateSnapshotStore`, `UpdateCheckService` | `json_update_snapshot_store_test.cpp`, `update_check_service_test.cpp` |
| F-033 | `UpdateCheckService::start` | `update_check_service_test.cpp` (restart test) |
| F-034 (rewritten) | `JsonUpdateSnapshotStore::load` (read-only), `discardInvalid` (writer only) | `json_update_snapshot_store_test.cpp`, `update_check_service_test.cpp`, `snapshot_file_reader_test.cpp` |
| F-035 | `JsonUpdateSnapshotStore::save`, `UpdateCheckService` | `json_update_snapshot_store_test.cpp`, `update_check_service_test.cpp` (non-root) |
| F-036 | `UpdateCheckModel::snapshotAgeText`, `update_age_format` | `update_check_model_test.cpp`, runtime acceptance |
| F-037 | `UpdateCheckModel::checkNowEnabled`, `UpdatesCheckBar.qml` | `update_check_model_test.cpp`, runtime acceptance |
| F-038 | `UpdateCheckModel::failureText` | `update_check_model_test.cpp`, runtime acceptance |
| F-039 | `UpdateCheckModel::available`, `UpdatesCheckBar.qml` | `update_check_model_test.cpp`, runtime acceptance |
| F-040 | XML `Updates` block unchanged, new interface only | existing `packaged_dbus_test.cpp` unmodified, XML-diff script check |
| F-041 | `UpdateCheckAdaptor`, XML second interface | `packaged_update_check_dbus_test.cpp` |
| NF-001 | 5.1 watchdog, libalpm timeouts | `alpm_update_checker_test.cpp` (FIFO) |
| NF-002 | queued `requestCheck`, private pool | `packaged_update_check_dbus_test.cpp` |
| NF-003 | `UpdateCheckPolicy` | `update_check_scheduler_test.cpp` |
| NF-004 | `JsonUpdateSnapshotStore::save` | `json_update_snapshot_store_test.cpp` |
| NF-005 | `UpdateCheckService` logging, no-URL adapter | `update_check_service_test.cpp` |
| NF-006 | `scratch_dir.cpp` step 1 | `scratch_dir_test.cpp` |
| NF-007 | `snapshot_store` target, `test_holonight_packages_checks` | CTest `TIMEOUT 10`, link check |
| C-001 | layering script | `layering_policy`, `layering_policy_fixtures` |
| C-002 | checker sources, layering script | `alpm_update_checker_test.cpp` (fake `checkupdates` on `PATH`), script |
| C-003 | 5.2 steps 4, 5, 7, 13 and 5.4 | `alpm_update_checker_test.cpp` (manifest, non-root read-only fixture) |
| C-004 | 5.2 step 10, `pacman_repositories.cpp` | `pacman_repositories_test.cpp`, `alpm_update_checker_test.cpp` (`Required`) |
| C-005 | checker sources, layering script | `alpm_update_checker_test.cpp` (installed set unchanged), script |
| C-006 | `AGENTS.md`, `CLAUDE.md`, `FileRepoFixture` | script line check, fixture guard test |
| C-007 | change set, layering script | script plus base-branch diff check |
| C-008 | `UpdatesCheckBar.qml` and policy fixtures | `runtime_controls_Holonight`, `runtime_controls_Fusion`, `runtime_qml_import_policy*`, `qml-lint` |
| C-009 (rewritten) | `UpdateCheckService` (private pool, packaged only) | `FakeUpdateChecker::callerThreads()` in service and D-Bus tests, layering script |
| C-010 | layering script over `tests/`, `FileRepoFixture` | script, fixture guard test |


### Correctness refinements
The interval resolver derives a safe minute bound from INT_MAX milliseconds including the policy's maximum positive jitter. The default bound is 35,781 minutes. Oversized positive input clamps with one warning; generated delays also guard arithmetic and the production one-shot timer uses PreciseTimer.

The GUI retains fetchedAt with the checked data. Service SnapshotFetchedAt remains mirrored independently and never supplies displayed age. Selection changes, including asynchronous local loads, notify the check model. A valid online snapshot with fresher local rows displays “Showing local package data”. Missing, invalid or unreadable snapshot loads invalidate cached online metadata on transition while retaining rows; subsequent successful local loads select local data. Property changes, completion signals and file watching all trigger read-only hand-off.

Successful CheckNow method replies acknowledge reachability even for cooldown no-ops. Completion detaches the completed run's callbacks before idle notification, and captures its outcome and snapshot before synchronous observers can start another run. Timestamp parsing checks the system_clock range before conversion. Pacman includes recursively expand in section context and repeated SigLevel directives accumulate partial settings.


### Repository catalogs and independent check history (2026-10-09)

Production AlpmUpdateSource reads the hash-keyed provenance beside the unchanged version-1 snapshot. JsonUpdateSnapshotStore copies complete checked databases into a UUID catalog generation, verifies their digests, writes the versioned provenance sidecar, then atomically replaces the snapshot. Only replacement publishes the result. Failed publication preserves the previous in-memory result as well as the old file. Catalog serialization and locking remain libalpm-free.

Repository identities hash repository name, resolved servers, effective signature policy, GPG directory and architectures. For each repository in current configured order, equivalent digests use checked data; otherwise local wins only with a strictly later repository timestamp. Missing or incompatible checked entries use local data. Fresh libalpm handles compare the selected catalog against current installed packages with existing ignore and version rules, using a private temporary layout. They perform no downloads or transactions. Both the GUI and UpdateMonitor use this backend evaluation; aggregate snapshot selection remains only for legacy ports and snapshot-only fallback rows.

The catalog lock uses shared leases throughout backend evaluation and exclusive leases throughout publication and reclamation. Readers validate generation names, regular database files and digests. After snapshot replacement, the writer reclaims obsolete generations and sidecars while readers are excluded. Failed checks and timed-out workers cannot publish a generation. Scratch run cleanup is independent of retained catalogs.

Installed database files and directories, sync paths, configuration and publication paths are watched with a debounce. Events during an active evaluation cause another evaluation; Reload follows the same path. Offline results never change check completion metadata.

Check completion history is stored separately in updates snapshot history JSON and mirrored through the existing D-Bus properties. Acknowledgements do not advance history. The page and sidebar bind the latest completion time and outcome separately from source labels and database metadata age. Busy states and failures retain displayed rows; local-only zero results use qualified local wording. Legacy or damaged provenance retains saved rows labeled previously loaded until valid evaluation succeeds.

Repository timestamps retain nanoseconds as decimal strings in the provenance sidecar, alongside metadata epoch seconds. This avoids JSON numeric precision loss and preserves equal-time selection across publication and restart. A valid snapshot without usable provenance is returned by the shared backend as previously loaded rows, so page and service count remain consistent. Missing current repository data produces an evaluation failure that retains the last usable rows and qualifies the GUI source.
