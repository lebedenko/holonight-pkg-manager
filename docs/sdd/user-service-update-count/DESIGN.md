# User Service and D-Bus Update Count — Design

Spec: `SPEC.md` v0.2. Status: draft.

## 0. Findings that shape the design

- `UpdateSource::loadUpdates()` is already blocking, read-only, network-free and cached through `AlpmConnectionCache` (parsed sync dbs are reused per instance). A long-lived service benefits directly: unchanged dbs are not re-parsed.
- `UpdatesModel` already contains the summary/state logic for the UI. The service needs the same rules without Qt Quick; the reusable piece is `UpdateSummary` in `application`.
- `src/platform` and `src/advisor` are interface stubs that AGENTS.md says to keep. D-Bus wiring therefore goes in the new executable, not in `platform`.
- Pacman replaces sync dbs by rename, and `-Syu` touches several files, so watchers must debounce and re-arm.

## 1. Components

### `src/application/` (static, Qt Core + Concurrent)
- `update_status.h` — plain struct `UpdateStatus { State state; uint32 count, ignoredCount; uint64 downloadSizeBytes; int64 dataAsOfEpoch; std::string lastError; }` and `enum class UpdateState { Loading, Ready, NoDatabases, Error }`.
- `update_status_builder.{h,cpp}` — pure function `UpdateStatus buildStatus(const expected<UpdateSnapshot, UpdateSourceError>&, const optional<UpdateStatus>& previous)`; uses `UpdateSummary`; on error keeps previous count/totals.
- `UpdateMonitor` (`QObject`, `update_monitor.{h,cpp}`) — owns `shared_ptr<UpdateSource>`, a `QFileSystemWatcher`, a single-shot debounce `QTimer`, and a `QFutureWatcher`. API: `status()`, `refresh()`, signal `statusChanged(UpdateStatus)`. Constructed with `UpdateMonitorOptions { watchPaths; debounce }`.
  - States: Idle → (trigger) Debouncing → Running → Idle; trigger while Running sets `rerun_`.
  - After each trigger the watcher re-adds its paths (rename replacement).
  - Evaluation via `QtConcurrent::run` as in `UpdatesModel`.

### `apps/packaged/` (new executable `holonight-packaged`)
- `main.cpp` — `QCoreApplication`, builds `AlpmUpdateSource` with the same default pacman paths as `PackagesApplication.cpp` (extract those defaults into one shared helper; do not duplicate), constructs `UpdateMonitor`, registers the bus name; exits 0 if the name is taken.
- `UpdatesAdaptor` (`QDBusAbstractAdaptor`) — class info `D-Bus Interface = org.holonight.Packages1.Updates`; maps `UpdateStatus` to properties; emits `PropertiesChanged` via `QDBusMessage::createSignal` on `statusChanged`.
- `dbus/org.holonight.Packages1.Updates.xml` — checked-in contract.
- `data/org.holonight.Packages1.service` (D-Bus activation, `SystemdService=holonight-packaged.service`) and `data/holonight-packaged.service` (systemd user unit `Type=dbus`).
- Install rules: binary to `bindir`; D-Bus service to `share/dbus-1/services`; unit to `lib/systemd/user`. Works with `DESTDIR`. Path substitution through `configure_file`.

### `apps/packages/` (T-010)
- `UpdateStatusClient` (`QDBusInterface` + watcher on the bus name) exposing `count`/`available` to QML; sidebar badge bound to it. Absent service ⇒ `available == false`, no badge, no error.

## 2. Data flow

```
dbpath change / startup / Refresh()
        │ (debounce 2 s)
        ▼
UpdateMonitor ── QtConcurrent ──▶ AlpmUpdateSource::loadUpdates()
        │                              (cached sync dbs)
        ▼
buildStatus(snapshot|error, previous) ──▶ statusChanged
        ▼
UpdatesAdaptor ──▶ PropertiesChanged on org.holonight.Packages1.Updates
```

Failure: `buildStatus` returns `Error` with `lastError`, previous Count retained; the next Trigger retries. No retry timer (NF-003).

## 3. Interfaces

D-Bus (summary; XML is authoritative):

| Member | Type | Notes |
|---|---|---|
| `State` | s | `loading` \| `ready` \| `no-databases` \| `error` |
| `Count` | u | non-ignored updates |
| `IgnoredCount` | u | |
| `DownloadSizeBytes` | t | non-ignored total |
| `DataAsOf` | x | oldest sync db mtime, Unix s; 0 when unknown |
| `LastError` | s | empty when none |
| `Refresh()` | method | async, immediate return |

`UpdateMonitor` is testable without D-Bus; the adaptor is a thin mapping tested with a private bus.

## 4. Decisions

1. **Monitor in `application`, adaptor in the executable.** Keeps `platform` a stub and the monitor unit-testable with plain Qt. Alternative (fill `platform`) rejected: needs an AGENTS.md ownership change for no gain.
2. **Reuse the live-db `UpdateSource` unchanged.** No sync, so the service stays unprivileged and offline; freshness equals the user's last sync .
3. **Properties + `PropertiesChanged`, no custom signal.** Standard clients (including shell code) get change notifications for free.
4. **Counts only over D-Bus.** Package lists stay in-process for the UI; avoids designing a serialised package schema before the advisor exists.
5. **Filesystem watch with debounce, not polling.** Matches how updates actually appear (pacman writes dbs) and meets NF-003.
6. **Session bus only; no idle exit.** The service must keep watching. Revisit with the helper.

## 5. Alternatives considered

- Timer polling every N minutes: wasteful, laggy; rejected.
- Shell calling the library directly: violates "shell owns no package logic".
- Embedding the service in the UI process: dies with the window; fails the "works without UI/shell" goal.

## 6. Test strategy

| Test | Covers |
|---|---|
| `update_status_builder_test` (fake snapshots) | F-001, F-002 |
| `update_monitor_test` (fake source, temp dirs, `QSignalSpy`) | F-002..F-004, NF-003/004 |
| `update_monitor_integration_test` with `AlpmUpdateSource` on `tests/fixtures/pacman/updates` and sync-file integrity check | C-001 |
| `packaged_dbus_test`, `packaged_process_test`, `update_status_client_test` (all in `tests/packaged/`): private `dbus-daemon --session`, adaptor + client | F-005, F-006, F-007 (two instances) |
| install check under `DESTDIR` | F-007 |
| `update_status_client_test` | F-008 |

Tests start their own bus; if `dbus-daemon` is missing the D-Bus tests fail with a remediation message, not a silent skip.

## 7. Risks

- `QFileSystemWatcher` loses watches on replaced files → re-arm after every event (F-004 test).
- D-Bus tests need `dbus-daemon` in the CI image (shared CI infrastructure initiative owns the image).
- Default pacman paths currently live in `PackagesApplication.cpp`; extraction must not change UI behavior.
- Name/path accepted but not yet implemented on the `holonight-shell` side.

## Related Documents

`SPEC.md`, `TASKS.md`, `docs/sdd/pending-updates/DESIGN.md`, `docs/ideas/01-high-level-project-idea.md`

## Change History

| Version | Date | Author | Notes |
|---|---|---|---|
| 0.1 | 2026-10-08 | SDD Stage 2 | Initial draft from SPEC v0.1 |
| 0.2 | 2026-10-08 | Review | Aligned with SPEC v0.2 (decisions resolved) |
