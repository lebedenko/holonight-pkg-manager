# Online update check — SPEC (HoloNight Packages)

Status: Decisions approved by the user on 2026-10-09; ready for the implementation plan. Requirements are written in EARS. No open decisions remain; the spikes S-1..S-3 in DESIGN section 8.3 are verification tasks, not decisions.

**Revision 2026-10-09.** What changed:

- Rewritten: REQ-F-019 (no-snapshot state; `Updates.Count` keeps its meaning), REQ-F-022 and REQ-F-023 (config file, silent default when missing, warn-and-clamp), REQ-F-024, REQ-F-025 (the GUI calls D-Bus `CheckNow()` through a client port), REQ-F-026 and REQ-F-027 (single coordinator lives in `holonight-packaged`; `flock` is a safety net), REQ-F-030 (approved, marker removed), REQ-F-031 (adds the service-unavailable failure), REQ-F-032 to REQ-F-034 (packaged is the single writer; GUI reads the snapshot file read-only and never deletes it), REQ-F-006 and REQ-F-009 (the last scratch directory is retained; stale contents are never read), REQ-F-039, REQ-C-009 (worker-thread rule applies to the packaged-side checker calls), REQ-C-004 wording, REQ-NF-006.
- Added (next free numbers): REQ-F-042 (scratch retention), REQ-F-043 (GUI hand-off through the snapshot file), REQ-F-044 (service unavailable shown inline), REQ-F-045 (on-demand request while checking or in cooldown is a no-op on the service).
- Decided and moved into requirements: startup delay 60 s, jitter, T_max 120 s, minimum interval 15 min, on-demand cooldown 10 s, scratch mode 0700, on-demand failure does not affect backoff, D-Bus names (DESIGN 4.5), config location (DESIGN 7.1).
- Changed architecture: the GUI process (`holonight-packages`) no longer runs a checker, scheduler or coordinator. Only `holonight-packaged` checks.
- "Open questions" now lists nothing open.

## Overview

`holonight-packaged` discovers new package updates itself, the way `checkupdates` does. It refreshes a private, user-owned copy of the sync databases and computes pending updates from that copy. The real pacman database (`/var/lib/pacman`) is never written, no root privilege is needed, and no partial-upgrade risk is introduced because no package transaction is run.

The feature adds:

- a backend-neutral `UpdateChecker` port in `src/domain`, next to `UpdateSource`;
- an `AlpmUpdateChecker` adapter in `src/backends`, implemented with native libalpm calls only;
- scheduling, single-flight coordination, backoff and last-good retention in the application layer, wrapping `UpdateMonitor`, running only inside `holonight-packaged`;
- a persisted last-good snapshot in `src/persistence`, written only by `holonight-packaged` and read by the GUI process as the hand-off channel for the update list;
- D-Bus extension of `holonight-packaged`, and a "Check now" control and status line on the Updates page. The control calls the D-Bus method `CheckNow()` (bus activation starts the service if it is not running); the GUI never runs a check itself.

The app remains transactionally inert. The feature never installs, removes or upgrades packages, and never changes the Installed or Explore pages.

## Scope and baselines

- Application: `holonight-pkg-manager`, Arch Linux / libalpm first, backend-neutral by design.
- Components touched: `src/domain` (port, error codes, `BackendCapabilities`), `src/backends` (`AlpmUpdateChecker`), `src/application` (coordinator, scheduler, backoff), `src/persistence` (snapshot cache), `apps/packaged` (D-Bus adaptor, introspection XML, composition of checker, coordinator and scheduler), `apps/packages` (Updates view-model, D-Bus `UpdateCheckClient`, read-only snapshot reader), `qml/updates`, `tests/`, `AGENTS.md`, `CLAUDE.md`.
- Style and acceptance vocabulary follow `docs/sdd/unified-qtquick-controls/SPEC.md`.
- Architecture rules follow `CLAUDE.md`: dependencies point toward `domain`; application code has no libalpm or QML types.

### Terms

- **Real dbpath**: the configured pacman database path, default `/var/lib/pacman`.
- **Scratch dbpath**: a user-owned run directory created per check, containing a `local` symlink to the real local database and a copy of the real `sync` directory. The most recent run directory is retained after the check (REQ-F-042).
- **Snapshot**: the result of one successful check: the pending-update list, its count, and `snapshotFetchedAt`.
- **Last-good snapshot**: the most recent successful snapshot, in memory or persisted.
- **Neutral error code**: one of the `UpdateCheckError` codes. No libalpm message text crosses the port.
- **Logical names**: `lastCheckTime`, `lastCheckSucceeded`, `lastError`, `snapshotFetchedAt`, `canCheckForUpdates`. These are the names used in this spec. Wire names on D-Bus are decided in DESIGN 4.5 (interface `org.holonight.Packages1.UpdateCheck`: `CheckNow`, `CanCheck`, `Checking`, `LastCheckTime`, `LastCheckSucceeded`, `LastCheckError`, `SnapshotFetchedAt`, `StatusChanged`).
- **Automatic check**: a check started by the scheduler in `holonight-packaged`. **On-demand check**: a check started by the D-Bus method `CheckNow()`, which is also what the "Check now" control calls.
- **Service**: the `holonight-packaged` process. **GUI process**: `holonight-packages`.
- **Snapshot file**: the persisted snapshot. `holonight-packaged` is its only writer; the GUI process only reads it.
- **Update service unavailable**: a client-side condition, not an `UpdateCheckError` code: the D-Bus call to `CheckNow()` failed because the service cannot be reached or activated.

## Scope

In scope: check port and adapter; scratch-copy refresh; status and failure policy; scheduling, backoff and single-flight; snapshot persistence; D-Bus extension; Updates page status line and control; tests and the test-sync carve-out in `AGENTS.md` and `CLAUDE.md`.

## Non-goals

- No desktop notifications and no shell UI. The shell lives in the holonight-shell repository; this feature exposes count, status and a change signal only.
- No install, remove or upgrade transactions. The app stays transactionally inert.
- No battery or metered-connection awareness.
- No network-online triggers.
- No AUR or Flatpak update checks.
- No changes to the Installed or Explore pages.
- No changes to the behaviour of `UpdateSource::loadUpdates()` beyond what is stated in REQ-F-008.

## Requirements

Template labels: **[U]** Ubiquitous, **[E]** Event-driven, **[S]** State-driven, **[C]** Conditional, **[X]** Unwanted behaviour.

### 1. Domain port, error model and backend contract

### REQ-F-001: Update checker port [U]
**Statement:** The domain layer shall declare an `UpdateChecker` port, beside `UpdateSource`, exposing `checkForUpdates()` that returns `std::expected<UpdateSnapshot, UpdateCheckError>`.
**Acceptance criteria:**
- A test double that implements only the port compiles and runs in `tests/application` without linking libalpm.
- A fake returning a snapshot yields that exact snapshot through `std::expected::value()`.
- A fake returning an error yields that exact error code through `std::expected::error()`.

### REQ-F-002: Error codes [U]
**Statement:** The `UpdateCheckError` type shall have exactly the codes `NetworkUnavailable`, `RepositoryUnreachable`, `Busy` and `Unknown`, each with a non-empty user-presentable message.
**Acceptance criteria:**
- A test enumerates the codes and asserts there are exactly four.
- Each code maps to a non-empty fixed message string from one mapping table.
- The messages contain no raw libalpm error text.

### REQ-F-003: Capability flag [U]
**Statement:** `BackendCapabilities` shall expose a `canCheckForUpdates` flag.
**Acceptance criteria:**
- The Alpm backend reports `canCheckForUpdates` as true.
- A test backend reporting false results in the Updates view-model reporting check unavailable (see REQ-F-039).

### 2. Alpm check adapter

### REQ-F-004: Check computes updates from a scratch copy [E]
**Statement:** When `checkForUpdates()` is called on `AlpmUpdateChecker`, the adapter shall refresh a scratch copy of the sync databases from the configured repositories and compute pending updates from that copy.
**Acceptance criteria:**
- With a `file://` fixture repository under a temp directory containing a package newer than the installed version, the snapshot lists that package with its installed and new versions.
- With a fixture repository containing no newer versions, the check succeeds with an empty update list.

### REQ-F-005: Scratch location [E]
**Statement:** When a check starts, the adapter shall create its scratch dbpath under the scratch directory, which by default is `$XDG_CACHE_HOME/holonight-packages/checkdb`, falling back to `$HOME/.cache/holonight-packages/checkdb` when `XDG_CACHE_HOME` is unset.
**Acceptance criteria:**
- With `XDG_CACHE_HOME` set to a temp directory, the scratch dbpath is created under that directory.
- With `XDG_CACHE_HOME` unset and `HOME` set to a temp directory, the scratch dbpath is created under `$HOME/.cache/holonight-packages/checkdb`.

### REQ-F-006: Scratch layout and isolation [E]
**Statement:** When a check starts, the adapter shall create the scratch dbpath with a `local` symlink to the real local database and a copy of the real `sync` directory, and shall apply every database refresh only to the copy.
**Acceptance criteria:**
- After a check, tests inspect the retained run directory (REQ-F-042) without any test hook: the scratch `local` entry is a symlink that resolves to the real local database directory.
- After a check, the scratch `sync` files differ from the real `sync` files whenever the fixture repository changed.
- After a check, the real `sync` directory is byte-identical to its state before the check (see REQ-C-003).

### REQ-F-007: Shared computation [U]
**Statement:** The pending-update computation used by `AlpmUpdateChecker` shall be the same helper used by `UpdateSource::loadUpdates()`.
**Acceptance criteria:**
- For identical sync database contents, `loadUpdates()` and `checkForUpdates()` return identical `PendingUpdate` sequences in a test.
- Source search shows exactly one implementation of the comparison logic, referenced from both call paths.

### REQ-F-008: Local-only contract preserved [U]
**Statement:** `UpdateSource::loadUpdates()` shall read local state only, shall perform no network access and shall write nothing.
**Acceptance criteria:**
- With `Server=file:///nonexistent` configured, `loadUpdates()` returns the same result as before this feature and creates no scratch directory.
- The real dbpath manifest (file names, sizes, SHA-256, mtimes) is identical before and after the call.

### REQ-F-009: Stale scratch is never reused [X]
**Statement:** If a scratch dbpath from an earlier run exists when a check starts, then the adapter shall not use any of its contents for the new check.
**Acceptance criteria:**
- A test plants a corrupt or fake sync database in an older run directory and in a stray file in the scratch directory; the check result equals the result of a check with an empty scratch directory.
- The previous check's retained run directory is also never read: a test that makes the retained directory's databases disagree with the real ones gets the same result as a clean-root check.
- Removal of old run directories is governed by REQ-F-042.

### REQ-F-010: Lock present means Busy [X]
**Statement:** If a pacman database lock file is present in the real dbpath when a check starts, or appears before the scratch copy completes, then the adapter shall return `Busy` and leave the last-good snapshot unchanged.
**Acceptance criteria:**
- With a pre-created `db.lck` in a fixture real dbpath, the check returns `Busy` within 1 second.
- With a test hook that creates `db.lck` between the start of the copy and its completion, the check returns `Busy`.
- The last-good snapshot count and list are unchanged after either case.

### REQ-F-011: Missing repository directory [X]
**Statement:** If a configured `file://` repository directory does not exist, then the adapter shall return `RepositoryUnreachable`.
**Acceptance criteria:**
- With `Server=file://<tmp>/missing/$repo`, the check returns `RepositoryUnreachable` with a non-empty message.

### REQ-F-012: Network failure mapping [X]
**Statement:** If libalpm reports a network-level transfer failure, then the adapter shall return `NetworkUnavailable`.
**Acceptance criteria:**
- A mapping-function unit test covers each libalpm download error code classified as network-level and asserts `NetworkUnavailable`.
- The mapping table is the single authority for error classification.

### REQ-F-013: Unclassified error [X]
**Statement:** If libalpm reports an error that is not in the classification table, then the adapter shall return `Unknown`.
**Acceptance criteria:**
- A mapping-function unit test with an unlisted error code asserts `Unknown`.

### REQ-F-042: Scratch retention and cleanup [S]
**Statement:** The adapter shall create a new run directory for every check, shall keep the most recent run directory after the check completes (success or failure), and shall remove all older run directories so that at most one remains at rest and at most two exist while a check runs. At the start of each check, under the exclusion lock and before creating the new run directory, and once at service start, the adapter shall remove every run directory except the newest. Removal shall never follow symbolic links, including the retained directory's `local` symlink.
**Acceptance criteria:**
- Three consecutive checks (one failing) leave exactly one run directory at rest, the one from the third check; a probe during the third check sees at most two.
- A test with stale `run-*` directories planted before the check finds only the newest planted one plus the new one during the check, and only the new one afterwards.
- A planted run directory whose `local` entry points at a sentinel directory outside the scratch root is removed, and the sentinel directory and its contents are unchanged (manifest equal).
- The same sentinel check holds for the real-dbpath fixture: the manifest of the real `local` directory is identical after removal.
- Peak scratch use is about twice the size of the real `sync` directory, and at rest about once (asserted loosely in acceptance, for example below 3x).
- Stale or retained contents are never read by a new check (REQ-F-009).

### 3. Snapshot, status and last-good policy

### REQ-F-014: Success replaces the snapshot [E]
**Statement:** When a check completes successfully, the service shall replace the displayed update count and update list with the new snapshot and emit one change signal.
**Acceptance criteria:**
- With a fake checker returning two updates, the count becomes 2 and the list has two entries.
- Exactly one change signal is emitted per completed successful check.

### REQ-F-015: Failure keeps last-good [X]
**Statement:** If a check fails, then the service shall keep the last-good update count and update list unchanged.
**Acceptance criteria:**
- After a successful check with three updates, a failing check with `NetworkUnavailable` leaves the count at 3 and the list identical, both on D-Bus and in the Updates view-model.
- A count of zero from a failed check is never published.

### REQ-F-016: Failure status fields [X]
**Statement:** If a check fails, then the service shall set `lastCheckSucceeded` to false, set `lastError` to the failing neutral code, and set `lastCheckTime` to the completion time of the failing attempt.
**Acceptance criteria:**
- With a fake clock, after a failure the three fields have the expected values.
- After a subsequent success, `lastCheckSucceeded` is true and `lastError` is empty.

### REQ-F-017: Snapshot age is tracked separately [U]
**Statement:** The service shall keep `snapshotFetchedAt`, the completion time of the last successful check, separately from `lastCheckTime`.
**Acceptance criteria:**
- Success at time T, then failure at T+3 h, yields `snapshotFetchedAt` = T and `lastCheckTime` = T+3 h.

### REQ-F-018: Status fields are exposed [U]
**Statement:** The service shall expose `lastCheckTime`, `lastCheckSucceeded`, `lastError` and `snapshotFetchedAt` on D-Bus under the wire names of DESIGN 4.5, and the Updates view-model shall expose each field, filled from the D-Bus properties.
**Acceptance criteria:**
- A D-Bus test connection reads each field and it equals the internal state of the service.
- The Updates view-model, fed by a fake `UpdateCheckClient` status, exposes each field with the same value.

### REQ-F-019: No-snapshot state before any check [S]
**Statement:** While no successful snapshot exists, `SnapshotFetchedAt` shall be `0`, the Updates view-model shall report `hasSnapshot` as false, and the Updates page shall show a "Not checked yet" status line and shall not present the list or count as freshly checked. `Updates.Count` keeps its existing meaning and type; it is not required to be unknown on the wire.
**Acceptance criteria:**
- On a fresh start with no snapshot file, before any check completes, the D-Bus `SnapshotFetchedAt` is `0` and the existing `Updates.Count` and `Updates.State` behave exactly as before this feature.
- In that state the view-model has `hasSnapshot == false` and the status text is "Not checked yet"; it contains no "Last checked" text and no age.
- Runtime acceptance under both styles shows the "Not checked yet" line, not an age line.

### 4. Scheduling, coordination and backoff

### REQ-F-020: First automatic check after startup [E]
**Statement:** When `holonight-packaged` starts, the scheduler shall start the first automatic check after a startup delay of 60 seconds.
**Acceptance criteria:**
- With a fake clock, the fake checker is not called before 60 s and is called exactly once at 60 s (within one clock tick).

### REQ-F-021: Periodic automatic check with jitter [S]
**Statement:** While the service is running and no check is in progress and no retry is pending, the scheduler shall run an automatic check every 6 hours plus a random jitter.
**Acceptance criteria:**
- Over a 48-hour simulated run with a seeded random source, every inter-check interval lies within [6 h − J, 6 h + J], where J is 10 minutes, capped at one tenth of the interval.
- Not all intervals are equal across the run.

### REQ-F-022: Configured interval [C]
**Statement:** Where a check interval is configured, the scheduler shall use the configured interval in place of 6 hours. The interval is read from `$XDG_CONFIG_HOME/holonight/packages.toml` (key `check_interval_minutes` in table `[updates]`, through holonight-config) by `holonight-packaged`; the command-line option `--check-interval-minutes` overrides the file.
**Acceptance criteria:**
- With a configured interval of 2 hours, a fake-clock run over 12 hours yields checks every 2 hours within the jitter bound.
- With the file value 120 and the option `--check-interval-minutes 30`, the effective interval is 30 minutes.

### REQ-F-023: Missing, invalid or too small interval [X]
**Statement:** If the configuration file or the key is missing, then the scheduler shall silently use the 6-hour interval with no warning. If the value is present but empty, non-numeric or not greater than zero, then the scheduler shall use the 6-hour interval and log exactly one warning. If the value is greater than zero but below 15 minutes, then the scheduler shall use 15 minutes and log exactly one warning.
**Acceptance criteria:**
- Two test configurations (no file, file without the key) each yield a 6-hour interval and zero warnings in the log.
- Three test configurations (empty value, `abc`, `0`) each yield a 6-hour interval and exactly one warning.
- A configuration of `5` yields 15 minutes and exactly one warning.

### REQ-F-024: CheckNow enters the coordinator [E]
**Statement:** When `CheckNow()` is called on the D-Bus interface, the service shall request a check through the same coordinator entry point used by the scheduler. The coordinator exists once, inside `holonight-packaged`.
**Acceptance criteria:**
- With the coordinator idle, one `CheckNow()` call produces exactly one call to the fake checker.
- A packaged D-Bus test shows that a `CheckNow()` call made over a private bus reaches the coordinator (observable as one fake-checker call and `Checking` becoming true).

### REQ-F-025: Check now control calls CheckNow over D-Bus [E]
**Statement:** When the "Check now" control is activated on the Updates page, the Updates view-model shall call `CheckNow()` through a D-Bus client port (an application/app-layer interface such as `UpdateCheckClient`). The GUI process shall not run a checker, scheduler or coordinator.
**Acceptance criteria:**
- A view-model test with a fake `UpdateCheckClient`: one activation produces exactly one `CheckNow` call on the fake, and no other check path exists in the GUI process.
- The packaged D-Bus test of REQ-F-024 shows that `CheckNow()` reaches the coordinator.
- The `UpdateCheckClient` call uses D-Bus activation, so the call starts `holonight-packaged` when it is not running (verified in the packaged process test with a private session bus and an activation file).

### REQ-F-026: Join a running check [S]
**Statement:** While a check is in progress, the coordinator in `holonight-packaged` shall join each new request, whether automatic or from `CheckNow()`, to that check instead of starting another one.
**Acceptance criteria:**
- With a fake checker blocked, two further requests (one scheduled, one `CheckNow()`) produce no additional checker calls.
- When the blocked check completes, all requesters receive the same outcome.

### REQ-F-027: At most one check at a time [U]
**Statement:** The service shall have at most one call to `checkForUpdates()` in progress at any time, across scheduled and D-Bus requests. The single coordinator in `holonight-packaged` provides this. An exclusive `flock` in the scratch root is only a safety net against a second `holonight-packaged` instance or a leftover wedged checker thread; a check that finds the lock held returns `Busy`.
**Acceptance criteria:**
- Fifty concurrent requests from multiple threads against a fake checker that records concurrency yield a maximum concurrency of 1.
- With the `flock` held by a second holder, the adapter returns `Busy` without starting a transfer.

### REQ-F-028: Failure backoff [X]
**Statement:** If an automatic check fails, then the scheduler shall schedule the next automatic check after 5 minutes for the first consecutive failure, 15 minutes for the second, 1 hour for the third, and after the fourth and later consecutive failures shall return to the 6-hour timer of REQ-F-021.
**Acceptance criteria:**
- With a fake clock and an always-failing checker, the gaps between automatic attempts are 5 min, 15 min, 1 h, then 6 h (plus jitter), and then 6 h (plus jitter) for each following attempt.

### REQ-F-029: Backoff reset on success [E]
**Statement:** When an automatic check succeeds, the scheduler shall reset the consecutive failure count to zero.
**Acceptance criteria:**
- Sequence fail, fail, success, fail yields a next gap of 5 minutes after the final failure.

### REQ-F-030: Busy counts as a failed attempt [X]
**Statement:** If the checker returns `Busy`, then the coordinator shall treat the attempt as a failed check for status and backoff purposes, and shall keep the last-good snapshot.
**Acceptance criteria:**
- With a fake checker returning `Busy`, `lastError` is `Busy`, the count is unchanged, and the next automatic gap follows REQ-F-028. (Decided by the user, 2026-10-09.)
- An on-demand check that fails does not change the consecutive failure count or the pending automatic timer; an on-demand check that succeeds resets the count and re-arms the timer to the interval plus jitter (decided, 2026-10-09).

### REQ-F-031: On-demand failure is inline only [X]
**Statement:** If an on-demand check fails, then the Updates page shall show the failure inline next to the "Check now" control and shall not open any modal dialog, popup or notification. This also applies to the client-side "Update service unavailable" failure (REQ-F-044).
**Acceptance criteria:**
- Runtime acceptance under both styles: after a fake failure, the failure text appears in the control row and no `Popup` or `Dialog` item is instantiated.

### REQ-F-045: CheckNow while checking or in cooldown is a no-op [X]
**Statement:** If `CheckNow()` is called while a check is in progress, or less than 10 seconds after the previous check completed, then the service shall not start another check and shall not change any status field; `CheckNow()` shall still return immediately.
**Acceptance criteria:**
- With a blocked fake checker, a second `CheckNow()` produces no additional checker call (join, REQ-F-026) and returns within 100 ms.
- With a fake clock, `CheckNow()` 9 s after a completed check produces no checker call and no change of `lastCheckTime`; `CheckNow()` 11 s after produces one call.
- Automatic checks are not subject to the cooldown.

### 5. Persistence

### REQ-F-032: Persist on success [E]
**Statement:** When a check succeeds, `holonight-packaged`, as the single writer, shall write the snapshot (update list, count, `snapshotFetchedAt`, schema version) to the cache directory through `src/persistence`.
**Acceptance criteria:**
- After a successful check, the cache file exists and contains those fields.
- After a failed check, the cache file is unchanged.

### REQ-F-033: Load persisted snapshot at start [E]
**Statement:** When the service starts and a valid persisted snapshot exists, the service shall load it as the last-good snapshot before any check completes.
**Acceptance criteria:**
- Restart test: the count is available before the first fake-checker call, and `snapshotFetchedAt` equals the stored value.

### REQ-F-034: Invalid persisted snapshot [X]
**Statement:** If the persisted snapshot is missing, corrupt or has an unknown schema version, then the reader shall treat it as "no snapshot" (`SnapshotFetchedAt == 0`, REQ-F-019). Loading is read-only. Only `holonight-packaged`, the single writer, shall remove an invalid file, through a separate discard operation called only by the writer; the GUI process shall never delete or modify the file.
**Acceptance criteria:**
- Three fixtures (missing file, truncated file, unknown schema version) each produce the no-snapshot state and do not crash, in both the service and a GUI-side reader.
- After the GUI-side reader loads each invalid fixture, the file is byte-identical to before.
- After the service starts with each invalid fixture that exists, the file is removed by the writer's discard operation.

### REQ-F-035: Write failure keeps memory and old file [X]
**Statement:** If writing the snapshot fails, then the service shall keep the new snapshot in memory, log the failure, and leave the previously persisted file intact.
**Acceptance criteria:**
- With a cache directory made read-only after an old snapshot was written, a successful check still updates the in-memory count, the log contains the failure, and the old file's bytes are unchanged.
- Tests that rely on read-only permissions must run as a non-root user (see Risks).

### REQ-F-043: Snapshot file is the hand-off to the GUI [E]
**Statement:** When the GUI process receives the D-Bus `StatusChanged` signal or a `PropertiesChanged` for the `UpdateCheck` interface, or when the snapshot file changes on disk, the GUI process shall read the snapshot file read-only and merge it into the Updates list by the freshness rule (DESIGN 3.4, `selectFresherSnapshot`) against its own local `loadUpdates()` result. The GUI shall watch the snapshot file (this is mandatory) and shall tolerate the file being absent or corrupt without error, deletion or crash.
**Acceptance criteria:**
- With a fake `UpdateCheckClient` emitting a status change and a snapshot file written by the test, the `UpdatesModel` list shows the snapshot's entries when it is fresher than the local result, and the local result otherwise.
- Writing a new valid snapshot file (atomic rename) without any D-Bus signal also updates the model within the file-watcher delay.
- With the file absent or corrupt, the model keeps its current list, reports no snapshot, and the file is untouched.

### 6. Updates page

### REQ-F-036: Snapshot age line [S]
**Statement:** While a valid online snapshot supplies the displayed rows, the Updates page shall show its age as a quiet status line, for example "Last checked 3 h ago" (placement and wording decided, DESIGN 4.7).
**Acceptance criteria:**
- With a fake clock and the displayed persisted snapshot’s `fetchedAt` three hours earlier, the view-model text contains "3 h" and the rendered line appears in both styles.
- The line is not rendered as a modal or toast.

### REQ-F-037: Control disabled while checking [S]
**Statement:** While a check is in progress, as reported by the D-Bus `Checking` property, the "Check now" control shall be disabled and shall show a busy indicator.
**Acceptance criteria:**
- With the view-model in the busy state (fake client reporting `Checking`), the control's `enabled` property is false and the busy indicator is visible, under both styles.

### REQ-F-038: Failure status text [X]
**Statement:** If `lastCheckSucceeded` is false, then the Updates page shall show "Last check failed: " followed by the user-presentable message for `lastError`, and shall preserve the displayed-data status.
**Acceptance criteria:**
- For each of the four codes, the view-model text equals the prefix plus the mapped message, and the displayed-data status is unchanged.

### REQ-F-039: Hidden when unsupported [C]
**Statement:** Where the update service reports `CanCheck` (`canCheckForUpdates`) as false, the Updates page shall hide the "Check now" control and the check status line.
**Acceptance criteria:**
- With a fake client reporting false, the view-model reports both hidden, and the runtime acceptance shows neither item.
- A packaged D-Bus test with a backend reporting false reads `CanCheck == false`.
- Behaviour while the service is not running is covered by REQ-F-044.

### REQ-F-044: Update service unavailable is shown inline [X]
**Statement:** If the `CheckNow()` D-Bus call fails because `holonight-packaged` cannot be reached or activated, then the Updates view-model shall expose the inline failure text "Update service unavailable" in the same place and manner as other on-demand failures, shall re-enable the control, and shall not open any modal dialog, popup or notification. This is a client-side error and not a fifth `UpdateCheckErrorCode`.
**Acceptance criteria:**
- With a fake `UpdateCheckClient` whose `checkNow()` reports a D-Bus error, the view-model's failure text is "Update service unavailable", the control is enabled again, and `UpdateCheckErrorCode` still has exactly four values.
- Runtime acceptance under both styles: the text appears in the control row and no `Popup` or `Dialog` item is instantiated.
- A subsequent successful `CheckNow()` call clears the text.

### 7. D-Bus interface

### REQ-F-040: Existing members unchanged [U]
**Statement:** The `holonight-packaged` D-Bus interface shall keep every existing `UpdatesAdaptor` member and property with its current name and signature.
**Acceptance criteria:**
- A diff of the introspection XML shows no removed or changed existing element.
- The existing D-Bus client tests pass unmodified.

### REQ-F-041: Additive members [U]
**Statement:** The D-Bus interface shall add `CheckNow()`, the status properties of REQ-F-018 and a change signal, as additions to the existing introspection XML.
**Acceptance criteria:**
- The introspection XML contains a second interface `org.holonight.Packages1.UpdateCheck` with the `CheckNow` method, the status properties and the `StatusChanged` signal, exactly as in DESIGN 4.5 (decided: a second interface, so the existing `Updates` interface block stays byte-identical).
- A D-Bus test client can call and read each added member.

### 8. Non-functional requirements

### REQ-NF-001: Bounded network time [U]
**Statement:** Each libalpm network transfer during a check shall have a bounded timeout, and each check shall end with success or failure within a total bound T_max of 120 seconds.
**Acceptance criteria:**
- A `file://` fixture whose transfer never completes (for example a FIFO under a temp directory that is never written) makes the check return a failure code within T_max plus 5 seconds (the test configures a smaller T_max).

### REQ-NF-002: D-Bus responsive during a check [S]
**Statement:** While a check is in progress, the D-Bus service shall answer property reads and `CheckNow()` calls within 100 milliseconds.
**Acceptance criteria:**
- With a blocked fake checker, a test measures a property read and a `CheckNow()` call, each completing within 100 ms.

### REQ-NF-003: Bounded automatic attempts [U]
**Statement:** The scheduler shall make no more than 8 automatic check attempts in any 24-hour window under permanent failure.
**Acceptance criteria:**
- A fake-clock run of 24 hours with an always-failing checker records at most 8 automatic attempts. The schedule of REQ-F-028 gives 7 attempts before jitter.

### REQ-NF-004: Atomic snapshot write [U]
**Statement:** The service shall write the snapshot file atomically, by writing a temporary file in the same directory and renaming it over the target.
**Acceptance criteria:**
- With a fault injected between the temporary write and the rename, the previous snapshot file is byte-identical afterwards and no partial target file exists.

### REQ-NF-005: Logging without credentials [U]
**Statement:** The service shall log each check start and finish with duration and neutral code, and shall never log a repository URL that contains credentials.
**Acceptance criteria:**
- A log capture during a check contains start and finish lines with outcome and duration.
- A fixture configuration with `Server=file://user:secret@...` (or any credential-bearing URL form in the test) produces no log line containing the credential string.

### REQ-NF-006: Scratch directory ownership [U]
**Statement:** The scratch directory shall be owned by the current user and shall not be writable by other users.
**Acceptance criteria:**
- After creation, the owner is the current uid, the scratch root and run directories have mode 0700 and the lock file has mode 0600 (so `mode & 0022` is zero). A symlinked or group-writable scratch root is rejected.

### REQ-NF-007: Fast application-layer tests [U]
**Statement:** The scheduling, backoff, join and persistence tests shall run without libalpm and without real time, using a fake clock.
**Acceptance criteria:**
- The application-layer test target does not link libalpm.
- The full set of these tests completes in under 10 seconds on the CI container.

### 9. Constraints

### REQ-C-001: No libalpm above the adapter [U]
**Statement:** No libalpm header or sync-database concept (dbpath, sync database) shall appear in `src/domain`, `src/application` or `apps/*`, except composition code that constructs the backend.
**Acceptance criteria:**
- A policy check fails when a file outside `src/backends` and the composition root includes a libalpm header or names a dbpath or sync-database type.

### REQ-C-002: libalpm only, no helper script [U]
**Statement:** `AlpmUpdateChecker` shall use libalpm API calls only and shall not start the `checkupdates` script or any other external process.
**Acceptance criteria:**
- With a fake `checkupdates` placed first on `PATH` that writes a marker file, the check succeeds and the marker is absent.
- Source search finds no `QProcess`, `system`, `popen` or `exec` use in the adapter.

### REQ-C-003: Real dbpath is never written [U]
**Statement:** The adapter shall never create, modify or delete any file under the real dbpath or other system package metadata.
**Acceptance criteria:**
- A SHA-256, size and mtime manifest of a fixture real dbpath taken before and after a check is identical.
- The check runs as a non-root user against a real dbpath owned by root in a container test, so any write would fail loudly.

### REQ-C-004: Signature level never lowered [U]
**Statement:** The scratch refresh shall verify signatures with the `SigLevel` of the configured pacman.conf and shall never weaken it.
**Acceptance criteria:**
- A fixture repository with `SigLevel = Required` and an unsigned database causes the check to fail, and the snapshot is not replaced.
- Options-level, repository-level and nested includes retain their section context; repeated partial SigLevel directives retain earlier settings, and malformed included tokens fail closed.
- The effective level applied equals the level parsed from pacman.conf (read back from libalpm); an unparseable `SigLevel` token fails closed. Signature failures map to `Unknown` (decided, D11); there is no fifth error code.

### REQ-C-005: No transactions [U]
**Statement:** No code in this feature shall call a libalpm transaction API or spawn `pacman -S`, `-U` or `-R`.
**Acceptance criteria:**
- Source search finds no `alpm_trans_` call and no install, upgrade or remove invocation in the new code.
- A test confirms the installed-package set is unchanged after a check.

### REQ-C-006: Test sync carve-out **(required by feature)** [U]
**Statement:** Tests may sync only fixture repositories located under a temporary directory and addressed by `file://` URLs. This carve-out shall be recorded in `AGENTS.md` and `CLAUDE.md` in one line each, before the adapter is merged.
**Acceptance criteria:**
- Both files contain the line "tests may sync only fixture repositories under a temp dir via file://".
- A test harness assertion rejects any non-`file://` server URL and any path outside the temp directory.

### REQ-C-007: Out-of-scope exclusions [U]
**Statement:** The feature shall not add notifications, shell UI, battery or metered checks, network-online triggers, AUR or Flatpak checks, or changes to the Installed or Explore pages.
**Acceptance criteria:**
- The change set contains no file under `qml/packages` or `qml/explore`.
- Source search finds no `org.freedesktop.Notifications` client code and no NetworkManager signal subscription.

### REQ-C-008: UI changes need dual-style acceptance [U]
**Statement:** Any change to Updates QML shall ship with policy fixtures and shall pass runtime acceptance under the Holonight and Fusion styles and all existing QML tests.
**Acceptance criteria:**
- The CTest run includes the runtime acceptance for both styles and the policy fixtures, and all pass.

### REQ-C-009: Worker-thread calls only [U]
**Statement:** The `UpdateChecker` port shall be called only from a worker thread inside `holonight-packaged`, never from that process's main (D-Bus) thread. The GUI process never calls the port.
**Acceptance criteria:**
- A fake checker records the calling thread; the recorded thread differs from the main thread for every call made through the scheduler and through `CheckNow()`.
- Source search finds no `UpdateChecker`, `UpdateCheckService` or `AlpmUpdateChecker` use under `apps/packages`.

### REQ-C-010: Test restrictions [U]
**Statement:** Tests shall never automate pointer or focus input, run package transactions, or sync real repositories.
**Acceptance criteria:**
- The test harness and policy checks reject any test that calls a transaction API, any repository URL that is not a fixture, and any input-automation helper.

## Open questions

None. Every earlier open item was decided by the user on 2026-10-09 and now lives in the requirements:

- D-Bus names, signatures and the second-interface approach: REQ-F-041 and DESIGN 4.5.
- Scratch location, permissions and cleanup (new run directory per check, last one retained): REQ-F-005, REQ-F-042, REQ-NF-006.
- Config file location and format: REQ-F-022, REQ-F-023 and DESIGN 7.1.
- Placement and wording of the Updates page control and status line: REQ-F-036 and DESIGN 4.7.
- SigLevel equal to pacman.conf, never weakened: REQ-C-004.
- Startup delay 60 s (REQ-F-020), jitter (REQ-F-021), T_max 120 s (REQ-NF-001), minimum interval 15 min (REQ-F-023), on-demand failure not affecting backoff (REQ-F-030), on-demand cooldown 10 s (REQ-F-045), `Busy` counted as a failed attempt (REQ-F-030).
- No-snapshot representation: REQ-F-019 (`Updates.Count` unchanged; `SnapshotFetchedAt == 0`).
- GUI check path: the GUI calls D-Bus `CheckNow()`; only `holonight-packaged` checks (REQ-F-025, REQ-F-043, REQ-F-044).

Remaining verification tasks (not decisions): spikes S-1..S-3 in DESIGN 8.3 (timeout behaviour of `alpm_db_update`, signature checks as non-root, pacman's built-in default `SigLevel`).

## Risks

| # | Risk | Mitigation | Requirements |
|---|------|------------|--------------|
| 1 | The scratch copy is taken while a real `pacman -Sy` writes the sync directory, producing an inconsistent copy. | Check the lock before and after the copy; return `Busy`; keep last-good. | REQ-F-010 |
| 2 | Repeated failures hammer mirrors. | Fixed backoff schedule, a bound on attempts per 24 h, single-flight coordination and the 10 s on-demand cooldown, all in `holonight-packaged`. | REQ-F-028, REQ-NF-003, REQ-F-027, REQ-F-045 |
| 3 | A hung mirror stalls the worker thread and every joined "Check now". | Per-transfer timeouts and a total bound; the fixture test uses a never-completing FIFO. | REQ-NF-001, REQ-F-026 |
| 4 | A persisted snapshot after a long offline period is stale and looks current. | Snapshot age is always shown; count is never presented without age. | REQ-F-017, REQ-F-036 |
| 5 | Updates-page changes need costly dual-style runtime acceptance and policy fixtures. | Keep the UI change minimal; run both styles in CI; keep logic in the view-model where tests are cheap. | REQ-C-008, REQ-F-037 |
| 6 | Running as root in CI bypasses permission-based failure tests (read-only cache dir). | Run those tests as a non-root user; fail the test if the process is root. | REQ-F-035 |
| 7 | The scratch directory grows with every check (a full sync-database copy each time), and the retained run directory contains the `local` symlink to the real local database. | Keep only the newest run directory: at most two during a check (about 2x the sync directory), one at rest. Sweep under the lock before creating the new run directory and at service start. Removal never follows symlinks (it must not follow `local`). Stale contents are never read. | REQ-F-042, REQ-F-009, REQ-F-006 |
| 8 | Timer and jitter tests are flaky with real time. | Fake clock and seeded random source in all scheduling tests. | REQ-NF-007, REQ-F-021 |
| 9 | Test sync carve-out widens the test surface beyond the current repository rule. | Narrow carve-out to `file://` fixtures under a temp directory with a harness assertion. | REQ-C-006, REQ-C-010 |
| 10 | Signature verification diverges between the scratch config and pacman.conf. | Never weaken; fixture test with `SigLevel = Required`; exact-match question is open. | REQ-C-004 |
| 11 | Blocking libalpm work on the D-Bus thread freezes the service. | Checks run only on worker threads; D-Bus latency is bounded during a check. | REQ-C-009, REQ-NF-002 |
| 12 | Credentials in mirror URLs leak into logs or D-Bus errors. | Logs and neutral codes never include URLs; test with a credential-bearing fixture. | REQ-NF-005 |
| 13 | The GUI shows a stale or missing list because the snapshot file is the only carrier of the list (D-Bus carries counts and status only), or reads the file while it is replaced. | Atomic write by rename; the GUI watches the file and reads on `StatusChanged`; absent or corrupt files are tolerated and never deleted by the GUI. | REQ-F-043, REQ-NF-004, REQ-F-034 |
| 14 | `holonight-packaged` is down or fails to activate when the user presses "Check now". | Inline "Update service unavailable"; the control is re-enabled. | REQ-F-044, REQ-F-031 |


### REQ-F-046: Maximum check interval [X]
**Statement:** If a positive configured interval exceeds the safe timer bound, then the scheduler shall cap the interval and log exactly one warning.
**Acceptance criteria:**
- The default policy accepts 35,781 minutes without a warning and caps 35,782 minutes and LLONG_MAX at 35,781 minutes.
- The maximum positive jitter keeps the delay within INT_MAX milliseconds.
- Policy-specific jitter limits are included in the safe bound; positive numeric text exceeding LLONG_MAX also clamps with one warning.

### REQ-F-047: Displayed snapshot provenance [S]
**Statement:** While valid online data supplies the displayed rows, the Updates page shall format their age from that snapshot's persisted fetchedAt.
**Acceptance criteria:**
- Newer service metadata after a failed persistence operation does not change the displayed age.
- While a valid online snapshot exists and fresher local data supplies the rows, the status reads “Showing local package data”.
- Without a valid online snapshot, the status reads “Not checked yet”.

### REQ-F-048: Reader invalidation [X]
**Statement:** If a previously valid snapshot becomes missing, invalid or unreadable, then the GUI shall invalidate its online selection metadata.
**Acceptance criteria:**
- Existing rows remain until a subsequent successful load.
- The reader clears its cached snapshot and emits one invalidation on the transition.
- A subsequent successful local load supplies the displayed rows.

### REQ-F-049: Successful request acknowledgement [E]
**Statement:** When CheckNow receives a successful method reply, the GUI shall clear its service-unavailable error.
**Acceptance criteria:**
- A cooldown no-op reply clears the error without a check completion signal.
- The acknowledgement does not claim a completed check.
