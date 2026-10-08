# User Service and D-Bus Update Count Specification

**Feature**: A read-only, unprivileged per-user service (`holonight-packaged`) that keeps the pending-update summary current and publishes it on the session D-Bus, so HoloNight Shell (and the Packages UI) can show an update count without owning package logic.

**Status**: Requirements specification

**Date**: 2026-10-08

---

## Executive Summary

Phase 1 of `docs/ideas/01-high-level-project-idea.md` ends with "update count exposed over D-Bus" and a shell indicator. Everything before that is built (installed, explore, pending updates). This feature adds the missing process: `holonight-packaged`, a D-Bus-activated systemd user service. It reuses the existing `UpdateSource` port and `AlpmUpdateSource` adapter unchanged, re-runs the comparison when the local or sync databases change on disk, and exposes the result as D-Bus properties, a change signal and a `Refresh` method.

The service never synchronises databases, never touches the network and never runs privileged code. Database sync stays deferred (see `docs/sdd/pending-updates/DESIGN.md` §10).

---

## Non-Goals

- Database synchronisation of any kind (live or `checkupdates`-style temporary dbpath). Decided: not wanted (OQ-1).
- Privileged helper, Polkit, transactions of any kind.
- Desktop notifications (consumer: shell); shell indicator itself (lives in `holonight-shell`).
- AUR updates, update risk/advisor data, per-package update lists over D-Bus (count and totals only).
- Transaction history, persistence of results across service restarts.
- System bus exposure. Session bus only.
- Idle exit of the service (it must keep watching; revisit with the helper design).
- Launching or focusing the UI over D-Bus (the shell uses the desktop entry).
- Changing the Updates page data flow (it keeps its in-process `UpdateSource`).

---

## Glossary

**Update Status**: Snapshot-derived summary: state, count, ignored count, download total, data-as-of, last error.
**Monitor**: Application-layer object owning the `UpdateSource`, scheduling evaluations and holding the latest Update Status.
**Evaluation**: One call of `UpdateSource::loadUpdates()` on a worker thread.
**Trigger**: Startup, a debounced filesystem change in the local or sync database directory, or a `Refresh` call.

---

## Functional Requirements

### REQ-F-001: Update Status derivation
**Ubiquitous**: The Monitor shall derive Update Status from an `UpdateSnapshot` using the existing `UpdateSummary` rules: count and download total exclude ignored rows; ignored rows are counted separately.
**Acceptance**: Test with 10 normal + 3 ignored fixture rows yields `Count == 7`, `IgnoredCount == 3`, download total excluding ignored.

### REQ-F-002: States
**Ubiquitous**: Update Status `State` shall be one of `loading`, `ready`, `no-databases`, `error`. `no-databases` is `databasesFound == false`; `error` carries `LastError` and retains the previous Count.
**Acceptance**: Model of each transition against a fake `UpdateSource`; a failure after a success keeps the earlier Count and sets `error`.

### REQ-F-003: Triggers and debounce
**Event-driven**: When the service starts, when files in the configured `local/` or `sync/` directories change, or when `Refresh` is called, the Monitor shall schedule an Evaluation.
**Constraint**: Filesystem triggers shall be debounced (default 2 s) so a multi-file `pacman -Syu` yields one Evaluation. At most one Evaluation runs at a time; triggers arriving meanwhile coalesce into one follow-up.
**Acceptance**: Test emitting 20 change events within the window yields exactly one Evaluation; a trigger during a running Evaluation yields exactly one more.

### REQ-F-004: Watching
**Ubiquitous**: The watcher shall survive replacement of watched directories/files (pacman rewrites sync dbs via rename) by re-arming after each trigger.
**Acceptance**: Test that replaces a file in a temp dbpath twice and observes two Evaluations.

### REQ-F-005: D-Bus interface
**Ubiquitous**: The service shall own `org.holonight.Packages1` (OQ-2, decided) on the session bus and export at `/org/holonight/Packages1` the interface `org.holonight.Packages1.Updates` with:
- Properties (read-only): `State` (s), `Count` (u), `IgnoredCount` (u), `DownloadSizeBytes` (t), `DataAsOf` (x, Unix seconds, 0 when unknown), `LastError` (s, empty when none).
- Method: `Refresh()` (no reply payload; returns immediately, does not wait for the Evaluation).
- Standard `org.freedesktop.DBus.Properties.PropertiesChanged` emitted when any property changes.
**Acceptance**: Test client on an isolated session bus reads all properties, calls `Refresh`, and receives `PropertiesChanged` with the new Count.

### REQ-F-006: Introspection contract
**Ubiquitous**: The interface shall be defined in a checked-in introspection XML that the adaptor is generated from or verified against.
**Acceptance**: A test compares the running object's introspection with the XML.

### REQ-F-007: Activation and lifecycle
**Ubiquitous**: The project shall install a session D-Bus service file and a systemd user unit (`Type=dbus`, `BusName=` matching REQ-F-005) so the first bus call starts the service.
**Constraint**: A second instance shall exit cleanly when the name is taken. SIGTERM shall exit without partial state.
**Acceptance**: Install check under a temporary `DESTDIR` lists both files; two-instance test.

### REQ-F-008: Consumer client (last task)
**Optional-feature**: Where the service is on the bus, the Packages UI sidebar shall show `Count` from it; where it is absent the UI shall behave exactly as today.
**Acceptance**: QML/model test with an absent service shows no badge and no error.

---

## Non-Functional Requirements

- **REQ-NF-001**: `task build`, `task test`, `task format-check`, `task tidy` clean; `task qml-lint` clean if QML changes.
- **REQ-NF-002**: Tests are hermetic: temp dbpath fixtures from `tests/fixtures/pacman/updates`, a private `dbus-daemon --session`, no network, no real HOME/XDG, no real pacman dbpath.
- **REQ-NF-003**: The idle service shall not poll; Evaluations happen only on a Trigger.
- **REQ-NF-004**: Evaluation shall never block the Qt event loop.

## Constraints

- **REQ-C-001**: Read-only. No writes to, or locking of, pacman databases; the existing sync-file integrity check (checksums, sizes, mtimes) also runs after service Evaluations.
- **REQ-C-002**: All paths come from options, none hardcoded in the Monitor or adaptor (as `AlpmUpdateSourceOptions`).
- **REQ-C-003**: Dependency direction preserved: Monitor in `application` (Qt Core and Concurrent only); D-Bus adaptor and wiring in `apps/packaged`; `platform` and `advisor` stay stubs.
- **REQ-C-004**: The service exposes no method that accepts a command, path or package name.

---

## Resolved Decisions

- **OQ-1** No sync, live or temporary. The count is as fresh as the user's last sync.
- **OQ-2** `org.holonight.Packages1` / `/org/holonight/Packages1` accepted.
- **OQ-3** The UI sidebar client (REQ-F-008) stays in this iteration.

---

## Traceability

| Req | Verification |
|---|---|
| F-001..F-004 | GTest on `UpdateMonitor` with fake source and temp dirs |
| F-005, F-006 | GTest with private session bus |
| F-007 | install check + two-instance test |
| F-008 | model/QML test |
| NF-001 | `task check` |
| NF-002, NF-003, C-* | inspection + tests |

## Related Documents

- `docs/ideas/01-high-level-project-idea.md` (Phase 1, process split)
- `docs/sdd/pending-updates/SPEC.md`, `DESIGN.md` §10
- `docs/sdd/alpm-sync-db-cache/SPEC.md`
- `AGENTS.md`

## Change History

| Version | Date | Author | Notes |
|---|---|---|---|
| 0.1 | 2026-10-08 | SDD Stage 1 | Initial draft |
| 0.2 | 2026-10-08 | Review | Resolved OQ-1..3; REQ-F-008 kept in scope |
