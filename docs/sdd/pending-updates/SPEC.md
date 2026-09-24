# Pending Updates Detection and Display Specification

**Feature**: Read-only detection of pending official-repository package updates on Arch Linux (libalpm) by comparing installed packages against the current live sync databases, with a basic Updates page UI in the Qt Quick application.

**Status**: Requirements specification

**Date**: 2026-09-24

---

## Executive Summary

The HoloNight Packages application shall provide users with a read-only view of available package updates from official Arch repositories, without modifying the system package databases and without any network access.

Detection is a single operation: a comparison of installed packages against the **current live sync databases** at `/var/lib/pacman/sync/` (or the configured dbpath). It runs when the Updates page loads and again when the user presses the **Reload** button. Reload exists to pick up databases the user synced manually (outside this application) while the application is open; it is a cheap, local, network-free re-run of the same comparison. Synchronising the databases is not done by this application in this iteration; it is deferred to a future packages daemon.

The Updates page shall display a tabular list of pending updates with install-size deltas, download sizes and repository information. Packages marked as ignored in pacman.conf shall be displayed but flagged and excluded from headline counts and size totals. The page shall show when the data was current (the modification time of the oldest sync database) and, when that data is more than 7 days old, a soft hint that the user's package databases are out of date and should be synced with their package manager before pressing Reload. A failed reload shall retain the previous list and show a non-blocking banner.

---

## Non-Goals

The following are explicitly **out of scope** for this specification and will not be implemented in this iteration:

- Unprivileged or in-app synchronisation of package databases, including `checkupdates`-style refresh into a temporary directory
- Downloading of databases or packages, and any network access by the application in this feature
- Timeout handling for network or download operations
- AUR (Arch User Repository) update detection or display
- Replacement handling via the `replaces=` field (not yet part of the domain model)
- Categorization or grouping of updates by semantic type (kernel, drivers, session, applications, etc.)
- Update risk/advisor card or decision-support UI (addressed in a later phase)
- Package installation, removal, or transaction execution
- D-Bus API or system service exposure (reserved for a future daemon component)
- Background/periodic reload
- Persistence of update results or timestamps across application restarts
- Desktop notifications or integration with the system notification daemon
- User-initiated cancellation of a load or reload in progress
- Manual cache invalidation or administrative cache-clear operations
- Parsing of `pacman.conf` beyond the `IgnorePkg` and `IgnoreGroup` options
- Default landing page selection (whether Updates becomes the first page or appears as a navigation entry is a design decision, not a requirement)

### Deferred

Database synchronisation is deferred to the future `holonight-packaged` daemon / privileged helper, which can update the live sync databases with the required privileges. A `checkupdates`-style unprivileged refresh was prototyped and verified feasible during design; it is recorded in the DESIGN document ("Deferred: database sync") so it can be revived when the daemon lands.

---

## Glossary

**Sync Database**: The repository metadata database(s) configured in pacman, typically located in `/var/lib/pacman/sync/` (e.g., `core.db`, `extra.db`). Contains metadata for packages available to install from official repositories.

**Live Sync Databases**: The actual, persistent sync databases at the system's configured dbpath (e.g., `/var/lib/pacman/sync/`). These are updated only by the user's package manager outside this application and must never be modified by any operation in this specification.

**Official Package**: A package present in at least one sync (repository) database. This specification handles only official packages; AUR and foreign packages are out of scope.

**Ignored Package** (pacman.conf IgnorePkg/IgnoreGroup): A package listed in pacman's configuration as explicitly ignored during system upgrades. Ignored packages shall be listed in the Updates page but flagged visually and excluded from update counts and total download size.

**Load**: The read-only comparison of installed packages against the live sync databases, run when the Updates page first loads.

**Reload**: A user-triggered re-run of the load, initiated by the Reload button. It makes no network access and modifies nothing; its purpose is to pick up databases the user synced manually while the application is open.

**Data Freshness Timestamp**: The modification time (mtime) of the oldest `.db` file in the sync directory.

**Stale Hint**: A non-blocking, user-facing message displayed when the Data Freshness Timestamp is more than 7 days old, telling the user their package databases are out of date and should be synced with their package manager, and then Reload pressed.

---

## Functional Requirements

### REQ-F-001: Updates Page — UI Display

**Ubiquitous**: The Qt Quick application shall display an Updates page with a scrollable list of all pending updates from official repositories. Each row shall expose:
- Package name
- Installed version → available (newer) version
- Repository name
- Download size (human-readable)
- Installed-size delta (new installed size minus old, may be negative, human-readable)
- An "Ignored" indicator when the package is in pacman's IgnorePkg/IgnoreGroup

**Constraint**: The page shall be built from Holonight.Controls delegates following the project's existing QML conventions, and shall take all data from `UpdatesModel`; no hardcoded or mock data.

**Acceptance Criteria**:
- A QML-level test loads the page against a model with fixture updates and asserts each row exposes name, both versions, repository, download size, size delta, and ignored flag through its model roles.
- A model test with zero updates yields an empty valid list, not an error state.
- `task qml-lint` passes on the page.
- The visual layout is confirmed by the manual checklist in REQ-NF-008, not by automated or assistant-driven visual checks.

---

### REQ-F-002: Headline Statistics and Summary

**Ubiquitous**: The Updates page shall display a headline section above the update list showing:
- Count of pending updates (excluding ignored packages)
- Total download size (sum of download sizes, excluding ignored packages)
- A "Data as of {timestamp}" label indicating when the data was current (see REQ-F-006 for timestamp semantics)

**Constraint**: Ignored packages shall not be counted in the update count or included in the total download size, even though they are listed in the detail rows.

**Acceptance Criteria**:
- A test with a fixture containing 10 normal updates and 3 ignored packages verifies the headline count shows "7 updates", not "10 updates".
- A test with fixture sizes verifies the download total excludes ignored packages.
- A model test verifies the timestamp label reflects the snapshot's data timestamp and changes after a reload that yields a newer timestamp.

---

### REQ-F-003: Detection — Live Database Comparison

**Event-driven**: When the Updates page is first opened or loaded, the application shall immediately perform a read-only comparison of installed packages against the current live sync databases at the configured pacman dbpath (default `/var/lib/pacman/sync/`).

**State-driven**: The UI shall display a loading state (spinner, "Loading..." text, or progress indicator) while this comparison is in progress, and shall transition to the results view (list of updates, or up-to-date / no-databases state) upon completion.

**Constraint**: The comparison shall use the live databases as-is, without network access, copy, or download. The live databases shall never be modified (REQ-C-003).

**Constraint**: Only strictly newer versions (as determined by libalpm's `alpm_pkg_vercmp()`) shall be listed as updates; equal or older versions shall be excluded.

**Acceptance Criteria**:
- An adapter test with a fixture live sync directory and local database lists the installed packages that have a newer version in a sync database.
- A test with a fixture containing installed version "1.0" and available "2.0" lists the update; a test with available "1.0" (equal) or "0.9" (older) does not list an update.
- A model test starts the `UpdatesModel`, observes the loading state, waits for completion, and verifies a list (possibly empty) is exposed without any exception.

---

### REQ-F-004: Reload Button — Single-Flight

**Event-driven**: When the user presses the "Reload" button on the Updates page, the application shall re-run the live comparison of REQ-F-003 and replace the displayed data with the new result, so that databases synced manually by the user while the application is open are picked up.

**State-driven**: While a load or reload is in progress, the Reload button shall be shown in a busy/disabled state and further Reload requests shall be ignored. The page shall show a progress indicator while loading or reloading.

**Constraint**: Reload is not user-cancellable and never automatic. It makes no network access. Only one load or reload runs at a time.

**Acceptance Criteria**:
- A model test with a fake source calls `reload()` twice in a row and asserts the fake source was asked to load exactly once for the reload.
- With a fake source that delays completion, the model's `loading` property is true from the first call until completion and false afterwards; a `reload()` issued during the initial load is ignored.
- A test asserts that a reload requested after completion starts a new load and that the exposed rows and timestamp reflect the new result.

---

### REQ-F-005: Failure Handling — Non-Blocking Banner

**Unwanted-behaviour**: If a reload fails for any reason (pacman.conf unreadable, database open failure, unexpected exception), the application shall not crash and shall not clear the previous update list.

**State-driven**: After a failed reload the page shall show a non-blocking inline banner: "Reload failed: {reason}. Showing data from {previous_timestamp}." The previous list stays visible and interactive.

**State-driven**: If the initial load fails there is no previous list; the page shall show an error state (with the reason) and the Reload button shall remain available.

**Constraint**: No automatic retry; the user retries by pressing Reload.

**Acceptance Criteria**:
- A model test with a fake source that fails a reload asserts the update list is unchanged, an error message and the previous timestamp are exposed, and the model is no longer loading.
- A model test asserts a later successful reload clears the banner state.
- A model test with a fake source that fails the initial load asserts an error state with the reason, an empty list, and that a subsequent `reload()` is accepted.

---

### REQ-F-006: Freshness Label and Stale-Database Hint

**State-driven**: The Updates page shall display a "Data as of {timestamp}" label in the headline section showing the modification time (mtime) of the oldest sync `.db` file in the sync directory (e.g., "Data as of Sep 22, 14:30").

**State-driven**: If that timestamp is more than 7 days old (strictly greater), the application shall display a soft, non-blocking hint stating that the user's package databases are out of date and should be synced with their package manager, after which they can press Reload. The hint shall not suggest a specific command (a bare `pacman -Sy` risks partial upgrades on Arch). The hint does not prevent interaction and is not an error.

**Constraint**: The 7-day threshold is a soft reminder, not a hard block; the application shall function correctly even if databases are older than 7 days. The stale state shall be computed in the model using an injectable clock.

**Acceptance Criteria**:
- A model test with an injected clock and a snapshot 6 days old verifies no stale hint is exposed; exactly 7 days verifies no hint; 8 days verifies the hint is exposed.
- A test with a snapshot 2 hours old verifies the label shows a recent timestamp and no hint.
- An adapter test verifies the snapshot's data timestamp equals the mtime of the oldest sync `.db` file.
- A model test verifies a reload that yields a fresh timestamp removes the hint.
- Inspection of the hint text confirms it names no specific pacman command.

---

### REQ-F-007: No-Database State

**State-driven**: If the sync directory does not exist or contains no `.db` files (i.e., no configured official repositories), the application shall display a distinct, explicit state (not an error, not a generic empty list) with a user-facing message such as "No package databases found. Arch package management requires at least one repository to be configured in pacman.conf."

**Constraint**: This state is visually and functionally different from a success state showing zero updates (see REQ-F-008).

**Acceptance Criteria**:
- A test with an invalid or missing sync directory verifies that the application displays the "No package databases" state, not an error.
- A test with a valid but empty sync directory (directory exists, no `.db` files) verifies the same state.
- A test with a valid sync directory containing `.db` files but no installed packages verifies the "Up to date" state is shown instead (REQ-F-008).

---

### REQ-F-008: Up-to-Date State

**State-driven**: If the sync databases are present and no installed official package has a newer version available, the page shall show an explicit up-to-date state with the message "All official-repository packages are up to date."

**Constraint**: The up-to-date state still shows the freshness label and the stale hint (if applicable), and always carries a note that AUR and foreign packages are not covered.

**Acceptance Criteria**:
- A model test with fixture installed packages at the same versions as the sync databases exposes the up-to-date state, distinct from the no-databases state.
- The state exposes the official-repositories-only note.

---

### REQ-F-009: Ignored Packages — Display and Exclusion

**Ubiquitous**: Packages matched by `IgnorePkg` or `IgnoreGroup` shall appear in the update list flagged "Ignored".

**Constraint**: Ignored packages are excluded from the update count and total download size (REQ-F-002). The flag is informational only.

**Acceptance Criteria**:
- An adapter test with a fixture pacman.conf listing one package in `IgnorePkg` and one group in `IgnoreGroup` marks exactly the matching updates as ignored.
- A model test with 5 normal and 3 ignored updates lists all 8 but reports a count of 5.
- The same test verifies the total download size excludes the ignored packages.

---

### REQ-F-010: Ignore-List Configuration

**Ubiquitous**: The application shall read the `IgnorePkg` and `IgnoreGroup` options from the `[options]` section of the configured pacman.conf on every load and reload, so that ignored packages can be flagged (REQ-F-009). The values are whitespace-separated, may be spread over multiple lines (accumulated), and `#` comments are ignored. All other keys and sections are ignored.

**Unwanted-behaviour**: If pacman.conf is missing or unreadable, the load shall fail with a configuration error; it shall not silently treat the ignore lists as empty (which would inflate the update count). On a reload this follows the failure handling of REQ-F-005.

**Acceptance Criteria**:
- A parser test with `IgnorePkg` on multiple lines, trailing `#` comments, whole-line comments, and unknown keys and sections yields exactly the expected package and group lists.
- A parser test verifies that `IgnorePkg`/`IgnoreGroup` outside `[options]` (e.g. inside a repository section) are not collected.
- An adapter test with a missing pacman.conf returns a `ConfigurationInvalid` error and no snapshot.
- A model test with a fake source failing with a configuration error on reload keeps the previous list and shows the reload banner.

---

### REQ-F-011: Asynchronous Loading — Non-Blocking UI

**Event-driven**: Both the initial load (REQ-F-003) and the reload (REQ-F-004) shall run off the UI thread and shall expose a loading state while in progress.

**Constraint**: The async mechanism follows REQ-C-002.

**Acceptance Criteria**:
- A model test with a fake source that blocks until released asserts that `reload()` and the initial load return to the caller before the source completes, and that the loading state is exposed meanwhile.
- The same test asserts the results appear only after the source completes.

---

### REQ-F-012: Coexistence with the Sync-Database Cache

**Ubiquitous**: The comparison shall use the existing `AlpmConnectionCache` mechanism from `alpm-sync-db-cache` (owned per source instance, see that spec's REQ-C-002) so unchanged sync databases are not re-parsed.

**Constraint**: Queries re-read the sync databases when their file list or mtimes change, per the existing cache invalidation behaviour, so a reload picks up manually synced databases.

**Acceptance Criteria**:
- A test that changes a sync `.db` file (content and mtime) in a temporary copy of the fixture asserts the next query reflects the new content, per the existing cache invalidation behaviour.
- A test that leaves the sync files untouched asserts a second query returns the same result.

---

### REQ-F-013: Architecture — Port, Adapter, Model

**Ubiquitous**: The implementation shall follow the installed-packages layout:
- A domain-layer `UpdateSource` port defining update enumeration, with plain C++ domain types (`PendingUpdate`, result and error types).
- An ALPM adapter in `src/backends` implementing the port.
- An application-layer `UpdatesModel` orchestrating async loading and reloading for QML.
- QML under `qml/`, feature-scoped.

**Constraint**: Dependencies point domain ← backends/application ← QML. The port and its domain types shall not depend on Qt GUI or Quick.

**Acceptance Criteria**:
- `UpdatesModel` tests run against a fake `UpdateSource` with no ALPM and no QML engine.
- The port header includes no Qt GUI/Quick or libalpm headers.
- `task tidy` and the architecture checks in the repo pass.

---

## Non-Functional Requirements

### REQ-NF-001: UI Responsiveness

**Event-driven**: While a load or reload is in progress the UI thread shall not be blocked by database work.

**Acceptance Criteria**:
- Covered by REQ-F-011's model test (calls return before the source completes).
- No manual frame-time measurement is required.

---

### REQ-NF-002: Session-Scoped State

**Ubiquitous**: Update results and timestamps live in memory for the app session only and are not persisted.

**Acceptance Criteria**:
- A model test asserts a newly constructed `UpdatesModel` starts in the loading state with no result carried over.
- Code inspection confirms no file or settings writes of update state.

---

### REQ-NF-003: Test Isolation — Fixture Databases

**Ubiquitous**: All adapter and model unit tests shall use fixture sync databases, local databases and pacman configuration files checked into the repository (e.g., `tests/fixtures/pacman/`), not the live system `/var/lib/pacman`.

**Constraint**: Tests shall not require a system installation of pacman or libalpm; they link against libalpm as the backends target already does, but use only fixture databases.

**Acceptance Criteria**:
- A test runs in a CI environment without pacman installed and passes using fixture databases.
- A test on a developer machine with a different set of installed packages than the fixture returns the fixture's expected update list, not the system's installed packages.
- Inspection of test code confirms all database paths use `tests/fixtures/` or a CMake variable, not hardcoded `/var/lib/pacman`.

---

### REQ-NF-004: Fixture Coverage — Test Cases

**Ubiquitous**: The fixture suite shall include a local database, sync databases and a pacman.conf covering:
- Multiple repositories (e.g., core, extra) with a variety of packages and versions.
- Installed packages that have newer versions available in the sync databases.
- Installed packages that are equal or older than sync database versions (not listed as updates).
- Packages in pacman's IgnorePkg list (to test ignored-package logic).
- Packages in pacman's IgnoreGroup.
- Missing and empty sync directories (to test the no-database state).

**Acceptance Criteria**:
- Inspection of `tests/fixtures/pacman/` confirms a fixture `pacman.conf` with an `IgnorePkg` list and an `IgnoreGroup` list, plus a sync directory with at least 2 repository databases.
- Inspection of fixture `.db` files (or fixture generation scripts) confirms coverage of the above cases.

---

### REQ-NF-005: Code Style — C++23, Naming Conventions

**Ubiquitous**: All C++ code shall conform to:
- **Language**: C++23 standard
- **Format**: Google-based `.clang-format` (120-column line length, 2-space indentation)
- **Linting**: clang-tidy with `WarningsAsErrors: '*'` (all warnings treated as errors)
- **Naming**:
  - Classes: `CamelCase` (e.g., `UpdatesModel`, `AlpmUpdateSource`)
  - Functions: `camelCase` (e.g., `loadUpdates()`, `reload()`)
  - Private data members: `lower_case_` (e.g., `source_`, `load_in_progress_`)

**Constraint**: All changes must pass `task format-check` and `task tidy` before being considered complete.

**Acceptance Criteria**:
- Running `task format-check` on the implementation branch reports no violations.
- Running `task tidy` completes without errors or warnings.
- Code review confirms naming conventions are followed throughout.

---

### REQ-NF-006: QML Linting and Controls

**Ubiquitous**: The Updates page QML shall pass `qmllint` and use Holonight.Controls types per the project's QML conventions (HnListDelegate/HnNavigationDelegate for rows; no qmldir-internal types).

**Acceptance Criteria**:
- `task qml-lint` and the QML test suite pass.
- Code inspection confirms only public Holonight.Controls types are imported.

---

### REQ-NF-007: Testing — GTest Coverage

**Ubiquitous**: The implementation shall include focused GTest unit tests covering:
- Live comparison (REQ-F-003)
- Reload and single-flight behaviour (REQ-F-004)
- Failure scenarios on load and reload (REQ-F-005)
- Freshness labels and stale hint (REQ-F-006)
- No-database and up-to-date states (REQ-F-007, REQ-F-008)
- Ignored packages (display, count, size exclusion) (REQ-F-009)
- Ignore-list configuration parsing and missing-config failure (REQ-F-010)
- Cache invalidation on changed sync databases (REQ-F-012)
- Live sync directory unchanged after loading (REQ-C-003)

**Constraint**: Tests shall be executable via `task test` and shall pass with zero failures and zero skipped tests. No test shall require network access.

**Acceptance Criteria**:
- Running `task test` completes successfully with a test count >= 15 and zero failures.
- Test code is located in `tests/backends/`, `tests/application/`, and/or `tests/apps/` as appropriate.
- A code reviewer can identify at least one test for each REQ-F-xxx requirement.

---

### REQ-NF-008: Manual Visual Verification

**Ubiquitous**: Visual correctness is verified by a manual checklist run by the user, not by screenshots or visual checks made by the assistant or by snapshot tooling.

**Acceptance Criteria**:
- The implementation produces a checklist (e.g. in the SDD folder) covering list layout, ignored badge, headline, reload banner, stale hint text, no-databases and up-to-date states, and the busy Reload button with progress indicator, in light and dark themes.
- Automated tests assert state and data only, with no visual assertions.

---

## Constraints

### REQ-C-001: Dependency Isolation — Explicit Paths, No Hardcoding

**Ubiquitous**: The backend adapter shall not hardcode pacman paths (root, dbpath, pacman.conf). Instead, all paths shall be provided as constructor or factory parameters and propagated through all libalpm initialization calls and file operations.

**Rationale**: Enables test isolation and deployment flexibility.

**Acceptance Criteria**:
- Source code inspection confirms no occurrence of hardcoded `"/var/lib/pacman"`, `"/etc"`, `"/var"`, or `"/"` as string literals in the UpdateSource adapter.
- A test instantiates the adapter with arbitrary test-fixture paths and verifies they are used.

---

### REQ-C-002: Async Pattern Consistency

**Ubiquitous**: The async implementation for both the initial load and the reload shall follow the same pattern established by the installed-packages-list feature (InstalledPackagesModel), reusing QtConcurrent or the codebase's established async pattern.

**Rationale**: Ensures consistency and reduces cognitive load for future maintainers.

**Acceptance Criteria**:
- Code inspection confirms UpdatesModel uses the same async mechanism (QtConcurrent::run with QFutureWatcher) as InstalledPackagesModel.
- Comments or documentation explain why the chosen mechanism was selected.

---

### REQ-C-003: Live Sync Database Integrity

**Ubiquitous**: The live sync databases at the configured dbpath (default `/var/lib/pacman/sync/`) shall never be modified by any operation in this specification. Loading and reloading shall only read them.

**Rationale**: Essential for safety; any mutation of live system databases is a critical bug.

**Acceptance Criteria**:
- A test on a temporary copy of the fixture computes SHA-256 checksums, sizes and mtimes of all sync `.db` files before and after a load (and a reload) and asserts they are identical.
- If any sync file's content, size or mtime changes, the test fails with a clear message.

---

### REQ-C-004: No Network Access

**Ubiquitous**: The application shall make no network calls in this feature, and no automated test (unit, integration, or acceptance) shall attempt any network access.

**Rationale**: Ensures the feature is read-only and local, and tests are deterministic and fast.

**Acceptance Criteria**:
- Inspection of the new source and test code confirms no HTTP client, socket, or download API is used and no `http://`, `https://` or `file://` URL occurs.
- Running `task test` offline (without network access) succeeds with zero skipped tests.

---

### REQ-C-005: Holonight.Controls Integration

**Ubiquitous**: The Updates page shall use Holonight.Controls for interactive and list elements, consistent with the Installed page, and shall not use qmldir-internal types.

**Acceptance Criteria**:
- Code inspection and `task qml-lint` confirm only public Holonight.Controls types are used for interactive and list elements.

---

## Acceptance Test Plan

| Requirement | Test Type | Expected Outcome |
|---|---|---|
| REQ-F-001 | Model/QML test, qml-lint | Rows expose all fields; empty list valid |
| REQ-F-002 | Model test | Count and size exclude ignored; label reflects snapshot time |
| REQ-F-003 | Adapter + model test | Live comparison correct; newer listed, equal/older not |
| REQ-F-004 | Model test (fake source) | Second reload ignored; busy state exposed; reload after completion re-runs |
| REQ-F-005 | Model test (failing fake) | Reload failure keeps list, exposes reason and timestamp; initial failure gives error state |
| REQ-F-006 | Model test + adapter test | Oldest-mtime source; 7-day boundaries; hint wording |
| REQ-F-007 | Adapter + model test | Missing/empty sync dir gives no-databases state |
| REQ-F-008 | Model test | Up-to-date state with official-only note |
| REQ-F-009 | Adapter + model test | Ignored flagged; excluded from count and size |
| REQ-F-010 | Parser + adapter test | Ignore lists parsed; missing config gives ConfigurationInvalid |
| REQ-F-011 | Model test (blocking fake) | Calls return before source completes |
| REQ-F-012 | Adapter test | Changed sync .db re-read; unchanged stable |
| REQ-F-013 | Test + inspection | Model tested without ALPM/QML; port has no GUI/libalpm includes |
| REQ-NF-001 | Covered by REQ-F-011 | — |
| REQ-NF-002 | Model test + inspection | No persisted state |
| REQ-NF-003/004 | Inspection | Fixtures cover repos, ignore lists, missing/empty sync dir |
| REQ-NF-005 | `task format-check`, `task tidy` | Clean |
| REQ-NF-006 | `task qml-lint` | Clean |
| REQ-NF-007 | `task test` | All pass, no network |
| REQ-NF-008 | Manual checklist (user) | Checklist produced and run by user |
| REQ-C-001 | Inspection + test | No hardcoded pacman paths |
| REQ-C-002 | Inspection | Same async mechanism as InstalledPackagesModel |
| REQ-C-003 | Adapter test | Sync DB checksums, sizes and mtimes unchanged after load/reload |
| REQ-C-004 | Inspection | No network code or URLs in feature and tests |
| REQ-C-005 | Inspection, qml-lint | Public Holonight.Controls only |

---

## Related Documents

- **Feature Context**: `/home/andrii/Projects/pet/holonight/holonight-pkg-manager/docs/ideas/01-high-level-project-idea.md` (Phase 1, Updates page section)
- **UI Mockup**: `/home/andrii/Projects/pet/holonight/holonight-pkg-manager/docs/mockups/updates.png` (only the headline, list and reload affordance are in scope; the mockup's button is implemented as "Reload")
- **Installed Packages Specification** (related vertical slice): `docs/sdd/installed-packages-list/SPEC.md`
- **ALPM Sync Database Caching** (related infrastructure): `docs/sdd/alpm-sync-db-cache/SPEC.md`
- **Project Build & Style**: `/home/andrii/Projects/pet/holonight/holonight-pkg-manager/CLAUDE.md`
- **Architecture Conventions**: User memory at `/home/andrii/.claude/projects/.../memory/project_architecture_conventions.md`

---

## Change History

| Version | Date | Author | Notes |
|---|---|---|---|
| 1.0 | 2026-09-24 | SDD Stage 1 | Initial EARS specification for pending-updates feature |
| 1.1 | 2026-09-24 | SDD Stage 1 review | Removed visual/snapshot and performance criteria, unverified names, and redundant build requirements |
| 1.2 | 2026-09-24 | Scope reduction | Removed all unprivileged database syncing (temp-dbpath refresh, download, timeout, temp-dir cleanup, network fixtures). Feature is now a read-only comparison against the current live sync databases; "Refresh" became a local "Reload"; freshness is always the oldest sync .db mtime; stale hint reworded to ask the user to sync with their package manager; pacman.conf reading reduced to IgnorePkg/IgnoreGroup (new REQ-F-010); requirements renumbered (REQ-F-001..013); sync deferred to the future daemon |
