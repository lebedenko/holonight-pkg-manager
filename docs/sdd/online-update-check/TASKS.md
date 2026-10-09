# SDD Tasks — online-update-check

- [x] T-001: Record the test sync carve-out in AGENTS.md and CLAUDE.md, and mention the holonight_packages_snapshot_store target in AGENTS.md
  - REQs: REQ-C-006
  - Check: `grep -F "tests may sync only fixture repositories under a temp dir via file://"` finds exactly one line in each of AGENTS.md and CLAUDE.md, and `grep -F holonight_packages_snapshot_store AGENTS.md` finds a one-line mention; no other file changes.

- [x] T-002: Spike S-1, watchdog, FIFO, libalpm/libcurl timeout and libcurl init race (time-boxed; record in docs/sdd/online-update-check/SPIKES.md)
  - REQs: REQ-NF-001
  - Check: SPIKES.md has an S-1 section recording whether `alpm_db_update` on a never-written FIFO `file://` database blocks in `open()`, the time to return with `alpm_option_set_disable_dl_timeout` left at its default, and the libcurl init race outcome; if the watchdog of DESIGN 5.1 is judged unacceptable, the note records the fallback (forked child, which needs a REQ-C-002 wording change) and the task stays open until that is recorded.

- [x] T-003: Spike S-2, signature verification as non-root with DatabaseOptional and with SigLevel Required (gpgdir and gnupg state; time-boxed; record in SPIKES.md)
  - REQs: REQ-C-004
  - Check: SPIKES.md has an S-2 section recording, from a non-root run, whether `alpm_db_update` succeeds with the Arch default `DatabaseOptional` and what it needs with `Required`, and names the fail-closed outcome (`Unknown`) as the result when gnupg state is not writable.

- [x] T-004: Spike S-3, confirm pacman 7.1's built-in default SigLevel when pacman.conf sets none (record in SPIKES.md before T-019)
  - REQs: REQ-C-004
  - Check: SPIKES.md has an S-3 section stating the default level pacman 7.1 applies, with its source or a fixture read-back via `alpm_db_get_siglevel` as evidence, and states how the parser (T-019) must encode the default.

- [x] T-005: Domain UpdateChecker port, UpdateCheckError codes with one message table, and BackendCapabilities
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003
  - Check: `ctest -R update_checker` passes: exactly four codes exist, every message is non-empty and comes from the single table, a test double that implements only the port returns its exact snapshot and exact error through `std::expected`, and a default-constructed `BackendCapabilities` has `canCheckForUpdates == false`.

- [x] T-006: Domain UpdateSnapshotStore port with SnapshotLoad, SnapshotFileState and SnapshotStoreError types
  - REQs: REQ-F-034
  - Check: a test double implementing `load()` (read-only), `save()` and `discardInvalid()` compiles in the domain test target, and `grep -rE 'alpm|dbpath|QML' src/domain` finds nothing.

- [x] T-007: Application ports (Clock, RandomSource, OneShotTimer), fake clock, fake random and fake timer, and the libalpm-free test executable test_holonight_packages_checks
  - REQs: REQ-NF-007, REQ-F-021
  - Check: `ctest -R holonight_packages_checks` passes a FakeClock case that fires timers in due order without real waiting, the executable does not link libalpm (checked from its link line), and its CTest entry sets a 10 s `TIMEOUT`.

- [x] T-008: Pure policy functions nextAutomaticDelay and resolveCheckInterval with UpdateCheckPolicy defaults
  - REQs: REQ-F-021, REQ-F-022, REQ-F-023, REQ-F-028
  - Check: `ctest -R update_check_policy` passes: `nullopt` yields 360 min with zero warnings, `""`, `abc` and `0` each yield 360 min with exactly one warning, `5` yields 15 min with exactly one warning, `120` yields 120 min; a 48 h seeded run gives every interval inside [360 min − J, 360 min + J] with J = 10 min and not all equal; failure counts 1, 2 and 3 return exactly 5, 15 and 60 min.

- [x] T-009: UpdateCheckService core: status and outcome types, last-good snapshot, success replaces, failure keeps last-good, status fields and snapshotFetchedAt
  - REQs: REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-019
  - Check: `ctest -R update_check_service` passes: a fake success with two updates gives count 2 and one `checkCompleted`; a `NetworkUnavailable` failure after a three-update success keeps count 3 and an identical list; failure sets `lastCheckSucceeded` false and `lastError`, and the next success clears `lastError`; success at T then failure at T+3 h gives `snapshotFetchedAt == T` and `lastCheckTime == T+3 h`; with an empty store `snapshotFetchedAt` is unset.

- [x] T-010: UpdateCheckService single-flight, join, OnDemand cooldown, private one-thread pool and start/finish logging
  - REQs: REQ-F-026, REQ-F-027, REQ-F-045, REQ-C-009, REQ-NF-005
  - Check: `ctest -R update_check_service` passes: 50 concurrent requests from threads give `maxConcurrency() == 1`; while a fake is blocked, two more requests add no checker call and all requesters get the same outcome; `CheckNow` 9 s after completion makes no call and 11 s after makes one; `callerThreads()` never contains the home thread; a captured log has start and finish lines with outcome and duration.

- [x] T-011: UpdateCheckService persistence orchestration: load at start, discard invalid, save on success only, keep memory when a write fails
  - REQs: REQ-F-019, REQ-F-032, REQ-F-033, REQ-F-034, REQ-F-035
  - Check: `ctest -R update_check_service` passes: `start()` with a valid fake store emits `snapshotAdopted` before any checker call; `start()` with an Invalid load calls `discardInvalid()` exactly once; a failed check never calls `save()`; an injected save failure keeps the new count in memory and logs the failure; an empty store gives `SnapshotFetchedAt == 0`.

- [x] T-012: UpdateCheckScheduler startup delay, periodic timer, configured interval and OnDemand effect on the timer
  - REQs: REQ-F-020, REQ-F-021, REQ-F-022, REQ-F-030
  - Check: `ctest -R update_check_scheduler` passes: with a fake clock the fake checker is not called before 60 s and is called exactly once at 60 s; a 2 h configured interval over a fake 12 h run gives checks every 2 h within jitter; an OnDemand failure leaves the failure count and the pending automatic timer unchanged; an OnDemand success re-arms the timer to interval plus jitter and resets the count.

- [x] T-013: Scheduler failure backoff, reset on success, Busy as a failed attempt, and the 8-attempts-per-24-h bound
  - REQs: REQ-F-028, REQ-F-029, REQ-F-030, REQ-NF-003
  - Check: `ctest -R update_check_scheduler` passes: an always-failing fake gives gaps of 5 min, 15 min, 1 h and then 6 h plus jitter; the sequence fail, fail, success, fail gives a next gap of 5 min; a fake returning `Busy` follows the same gaps; a 24 h always-failing fake-clock run records at most 8 automatic attempts.

- [x] T-014: Pure helpers selectFresherSnapshot (freshness rule) and the age bucketing of update_age_format
  - REQs: REQ-F-036, REQ-F-043
  - Check: `ctest -R 'snapshot_selection|update_age_format'` passes: `selectFresherSnapshot` returns the online snapshot unless the local `dataAsOf` is greater than or equal to the online `dataAsOf`; a snapshot 3 h old renders text containing "3 h", under one minute renders "just now", and the bucketing matches DESIGN 4.6 for minutes, hours and days.

- [x] T-015: UpdateMonitor::adoptOnline applying the freshness rule, with existing monitor behaviour unchanged
  - REQs: REQ-F-014
  - Check: the existing update monitor tests pass unmodified, and a new `update_monitor_adopt` case shows that `adoptOnline` with a fresher snapshot sets the monitor count to the snapshot count while a fresher local result keeps the local count.

- [x] T-016: Target holonight_packages_snapshot_store (libalpm-free) with JSON save through temp file and rename, and appCacheDir cache locations
  - REQs: REQ-F-005, REQ-F-032, REQ-F-035, REQ-NF-004, REQ-NF-007
  - Check: `ctest -R json_update_snapshot_store` passes: the saved file contains schemaVersion, fetchedAt, count and updates; a fault injected before the rename leaves the previous file byte-identical with no partial target; `appCacheDir` returns the XDG_CACHE_HOME path and the HOME fallback; with a read-only cache directory (test asserts `geteuid() != 0`) the old file is unchanged and the in-memory count updates; `ldd` of test_holonight_packages_checks shows no libalpm.

- [x] T-017: JSON load read-only for missing, truncated and unknown-version fixtures, discardInvalid for invalid files only, and a restart test
  - REQs: REQ-F-033, REQ-F-034
  - Check: `ctest -R json_update_snapshot_store` passes: for the missing, truncated and unknown-schema fixtures `load()` returns Absent or Invalid and the file is byte-identical afterwards; `discardInvalid()` removes only an invalid file and leaves a valid one; after a restart the count is available before the first checker call and `snapshotFetchedAt` equals the stored value.

- [x] T-018: Shared computePendingUpdates helper extracted from AlpmUpdateSource::loadUpdates, with loadUpdates behaviour unchanged
  - REQs: REQ-F-007, REQ-F-008
  - Check: `ctest -R alpm_update_source` passes unmodified plus a `pending_update_computation` parity case; with `Server=file:///nonexistent` `loadUpdates()` returns its pre-change result, creates no scratch directory and leaves the real dbpath manifest unchanged; `grep` finds the version-comparison loop only in `pending_update_computation.cpp`.

- [x] T-019: pacman_repositories parser for [repo] sections, Server, Include mirrorlists, SigLevel (fail-closed), GPGDir, Architecture, $repo and $arch
  - REQs: REQ-C-004
  - Check: `ctest -R pacman_repositories` passes: servers from an `Include` mirrorlist are parsed, `$repo` and `$arch` are substituted, a repository-level SigLevel overrides the global one, an unparseable SigLevel token is a parse error, and the default level applied when none is set equals the value recorded in SPIKES.md S-3.

- [x] T-020: Scratch root validation: create 0700, lstat checks (real directory, owned by the effective uid, no group or other write), and check.lock flock with mode 0600 and O_NOFOLLOW
  - REQs: REQ-NF-006, REQ-F-027
  - Check: `ctest -R scratch_dir_test` passes: the created root and run directories are owned by the current uid with `mode & 0022 == 0`, the lock file mode is 0600; a symlinked root and a group-writable root are each rejected as `Unknown`; with the flock held by a second descriptor acquisition reports Busy and no transfer is started.

- [x] T-021: Scratch retention: mkdtemp run directories, start sweep and service-start sweep keeping only the newest, symlink-safe removal that never follows `local`
  - REQs: REQ-F-042, REQ-F-009
  - Check: `ctest -R scratch_dir_test` passes: planted stale `run-*` directories reduce to the newest planted one plus the new one during the sweep and to the new one afterwards; a planted run directory whose `local` symlink points at a sentinel directory is removed while the sentinel's manifest (path, size, SHA-256, mtime) is unchanged.

- [x] T-022: The single classifyAlpmError mapping table (alpm_error_mapping)
  - REQs: REQ-F-012, REQ-F-013
  - Check: `ctest -R alpm_error_mapping` passes: every row of DESIGN 5.3 maps as written for both ServerKinds values (network codes give NetworkUnavailable with a remote server and RepositoryUnreachable with only file:// servers, ALPM_ERR_HANDLE_LOCK gives Busy), and an unlisted `alpm_errno_t` value gives Unknown.

- [x] T-023: FileRepoFixture test helper and the file:// harness guard
  - REQs: REQ-C-006, REQ-C-010
  - Check: `ctest -R file_repo_fixture` passes: the fixture builds a mirror and a real dbpath under a QTemporaryDir; a death test shows that `writeConf` aborts for any Server URL that is not `file://` or whose path is outside the temp directory; the policy grep over tests/ finds no non-file:// `Server =` line.

- [x] T-024: AlpmUpdateChecker happy path: scratch layout (local symlink, sync copy with preserved mtimes), refresh, comparison, and the alpmBackendCapabilities flag
  - REQs: REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-C-005
  - Check: `ctest -R alpm_update_checker` passes: a fixture with a newer package returns it with installed and available versions; a no-newer fixture returns success with an empty list; the retained run directory's `local` is a symlink resolving to the real local directory and its `sync` differs from the real one, which stays byte-identical; the scratch path is under XDG_CACHE_HOME when set and under HOME/.cache otherwise; `alpmBackendCapabilities().canCheckForUpdates` is true; the installed set is unchanged.

- [x] T-025: AlpmUpdateChecker failure paths: db.lck before and during the copy gives Busy, a missing file:// directory gives RepositoryUnreachable, an unsigned SigLevel=Required fixture fails, no checkupdates helper runs, and credentials never reach the logs
  - REQs: REQ-F-010, REQ-F-011, REQ-C-002, REQ-C-004, REQ-NF-005
  - Check: `ctest -R alpm_update_checker` passes: a pre-created `db.lck` gives Busy in under 1 s; the `afterCopyStarted` hook creating `db.lck` gives Busy; `Server=file://<tmp>/missing` gives RepositoryUnreachable with a non-empty message; the unsigned `SigLevel = Required` fixture fails with no snapshot produced; with a fake `checkupdates` first on PATH that writes a marker, the check succeeds and the marker is absent; a credential-bearing Server URL appears in no captured log line.

- [x] T-026: Watchdog wrapper: total bound T_max with a detached worker thread, tested with a never-completing FIFO
  - REQs: REQ-NF-001
  - Check: `ctest -R alpm_update_checker` passes the FIFO case: with `totalBound` set to 2 s, a never-written FIFO database makes `checkForUpdates()` return NetworkUnavailable within 7 s, and the test then unblocks the FIFO so the detached thread finishes.

- [x] T-027: Failure-path and isolation tests: real dbpath manifest unchanged after success and failure, planted corrupt or stale run directories never read, and a non-root guard
  - REQs: REQ-C-003, REQ-F-009
  - Check: `ctest -R alpm_update_checker` passes: the SHA-256, size and mtime manifest of a fixture real dbpath is identical before and after a successful and a failed check; a planted corrupt run directory, a stray file and a retained directory that disagrees with the real databases each give the clean-root result; the test fails when `geteuid() == 0`, and the container lane runs it as a non-root user against a root-owned read-only dbpath.

- [x] T-028: PackagedConfig: packages.toml through holonight-config with [updates] check_interval_minutes, XDG path with HOLONIGHT_PACKAGES_FILE override, and the --check-interval-minutes option
  - REQs: REQ-F-022, REQ-F-023
  - Check: `ctest -R packaged_config` passes: no file gives 6 h with zero warnings; a file without the key gives 6 h with zero warnings; a file value of 120 with the option 30 gives an effective 30 min; the default path resolves under XDG_CONFIG_HOME with the HOME fallback.

- [x] T-029: UpdateCheckAdaptor for org.holonight.Packages1.UpdateCheck with CheckNow, six read properties and StatusChanged, as a second interface in the checked-in XML
  - REQs: REQ-F-018, REQ-F-039, REQ-F-040, REQ-F-041
  - Check: `ctest -R packaged_dbus` passes unmodified; `git diff` on apps/packaged/dbus/org.holonight.Packages1.Updates.xml shows only added lines forming a new second `<interface>` element that declares `CheckNow`, the properties CanCheck (b), Checking (b), LastCheckTime (x), LastCheckSucceeded (b), LastCheckError (s), SnapshotFetchedAt (x) and the `StatusChanged` signal; the existing Updates block is byte-identical.

- [x] T-030: holonight-packaged composition root: AlpmUpdateChecker, JSON store, UpdateCheckService, scheduler, sweepStale and discardInvalid before the first check, and the two-argument UpdateStatusService constructor
  - REQs: REQ-F-020, REQ-F-022, REQ-C-009
  - Check: `ctest -R packaged_dbus` still passes, `holonight-packaged --help` lists `--check-interval-minutes`, `grep -n 'UpdateCheckScheduler' apps/packaged/main.cpp` finds the wiring, and a packaged run against a fixture makes no checker call before 60 s.

- [x] T-031: Packaged D-Bus and activation tests on a private bus: CheckNow reaches the coordinator, property reads and CheckNow answer within 100 ms during a blocked check, CanCheck false for a non-capable backend, activation file starts the service, and three checks leave one run directory
  - REQs: REQ-F-018, REQ-F-019, REQ-F-024, REQ-F-025, REQ-F-039, REQ-F-042, REQ-F-045, REQ-NF-002
  - Check: `ctest -R packaged_update_check` passes: one `CheckNow()` gives exactly one fake-checker call and `Checking` becomes true; with the fake blocked, a property read and a `CheckNow()` each complete within 100 ms; a backend reporting `canCheckForUpdates` false yields `CanCheck == false`; with a private session bus and an activation file, `CheckNow()` starts `holonight-packaged`; three consecutive fixture checks (one failing) leave exactly one run directory; `CheckNow()` within the cooldown makes no call; `SnapshotFetchedAt == 0` with no snapshot and `Updates.Count` unchanged.

- [x] T-032: GUI UpdateCheckClient port and DBusUpdateCheckClient (QtDBus, service watcher, CheckNow with auto-start, checkNowFailed on error)
  - REQs: REQ-F-018, REQ-F-025, REQ-F-044
  - Check: `ctest -R update_check_client` passes on a private bus with a stub service: mirrored property values equal the stub's, `checkNow()` issues exactly one `CheckNow` call with auto-start enabled, and a D-Bus error reply or a missing name emits `checkNowFailed` once; `git diff` shows `UpdateStatusClient` unchanged.

- [x] T-033: SnapshotFileReader: read-only load, QFileSystemWatcher with debounce, tolerance of absent or corrupt files
  - REQs: REQ-F-034, REQ-F-043
  - Check: `ctest -R snapshot_file_reader` passes: an atomic rename of a new valid snapshot emits `snapshotRead` within the debounce with no D-Bus signal; initial absent and corrupt fixtures emit no snapshot and do not crash; deletion, corruption or read failure after a valid snapshot clears cached metadata and emits one invalidation while preserving GUI rows; corrupt files stay byte-identical; the test shows that neither `save()` nor `discardInvalid()` is called.

- [x] T-034: UpdateCheckModel view-model: available, checkNowEnabled, hasSnapshot with "Not checked yet", snapshotAgeText, failureText for the four codes and "Update service unavailable", one client call per activation
  - REQs: REQ-F-003, REQ-F-015, REQ-F-019, REQ-F-025, REQ-F-036, REQ-F-037, REQ-F-038, REQ-F-039, REQ-F-044
  - Check: `ctest -R update_check_model` passes with FakeUpdateCheckClient: one `checkNow()` gives exactly one client call; `Checking` true gives `checkNowEnabled` false; each of the four codes gives "Last check failed: " plus the mapped message with the age text still present; `checkNowFailed` gives "Update service unavailable" with the control re-enabled; an empty snapshot gives `hasSnapshot` false and "Not checked yet" with no age; a displayed persisted snapshot 3 h old gives text containing "3 h"; `available` is false when CanCheck is false.

- [x] T-035: UpdatesModel::applyCheckedSnapshot and PackagesApplication wiring (DBusUpdateCheckClient, SnapshotFileReader, UpdateCheckModel) with apps/packages CMake sources
  - REQs: REQ-F-015, REQ-F-043
  - Check: `ctest -R updates_model` passes with the existing cases unmodified plus new cases: a fresher snapshot replaces the list and an older one leaves the local list; a failed check leaves count and list unchanged; the holonight-packages executable builds, and `grep -rE 'AlpmUpdateChecker|UpdateCheckService|UpdateCheckScheduler' apps/packages` finds nothing.

- [x] T-036: QML UpdatesCheckBar.qml, UpdatesView and WorkspaceWindow forwarding of updateCheckModel, and the qml module and test_runtime_controls source lists
  - REQs: REQ-C-007, REQ-F-031, REQ-F-036, REQ-F-037
  - Check: `task qml-lint` and `task qml-import-check` exit 0; `git diff --name-only` lists no file under qml/packages or qml/explore; UpdatesCheckBar exposes objectNames updatesCheckNowButton, updatesCheckFailure, updatesCheckBusy and updatesCheckStatusLine, each with `visible` or `enabled` bound to updateCheckModel; the test_runtime_controls target builds.

- [x] T-037: Policy fixtures for UpdatesCheckBar and dual-style runtime acceptance under Holonight and Fusion
  - REQs: REQ-C-008, REQ-F-019, REQ-F-031, REQ-F-036, REQ-F-037, REQ-F-038, REQ-F-039, REQ-F-044
  - Check: `ctest -R 'runtime_controls_Holonight|runtime_controls_Fusion|runtime_qml_import_policy'` passes under both styles: a failure shows its text in the control row with no Popup or Dialog item instantiated; "Not checked yet" shows with no age line; with `Checking` the button is disabled and the busy indicator visible; "Update service unavailable" shows and the button is enabled again.

- [x] T-038: Layering and policy script scripts/check-layering.py with fixtures, CTest entries layering_policy and layering_policy_fixtures, and task layering-check
  - REQs: REQ-C-001, REQ-C-002, REQ-C-005, REQ-C-006, REQ-C-007, REQ-C-009, REQ-C-010
  - Check: `ctest -R 'layering_policy|layering_policy_fixtures'` passes; the script fails on the fixtures for a libalpm include or dbpath name outside src/backends and the composition roots, QProcess, system, popen or exec in checker sources, `alpm_trans_` calls, checker names under apps/packages, QTest mouse or keyClick in tests, a notifications or NetworkManager string, and a missing carve-out line in AGENTS.md or CLAUDE.md; the script exits 0 on the repository.

- [ ] T-039: Final verification: task check, task ci rehearsal, existing tests unmodified, and the 10 s budget and no-libalpm link for the checks executable
  - REQs: REQ-F-040, REQ-F-046, REQ-F-047, REQ-F-048, REQ-F-049, REQ-NF-007, REQ-C-008
  - Check: `task check` and `task ci` exit 0; `git diff --stat main` shows no change to tests/packaged/packaged_dbus_test.cpp or tests/packaged/update_status_client_test.cpp, and only additions to existing backend tests; `ctest -R holonight_packages_checks` finishes within its 10 s TIMEOUT; `ldd` of test_holonight_packages_checks shows no libalpm.
  - Additional acceptance: regress recursive/options/nested includes and repeated signature settings; verify invalidation retains rows but clears provenance, local reload notifications, property-only hand-off, persisted age despite newer service metadata, and successful no-op acknowledgement. Verify interval cap and timestamp boundaries with UBSan, precise production timers, and reentrant completion with distinct run callbacks. Run dual-style local-data, no-snapshot, failure and busy acceptance.

Verification evidence (2026-10-09):

- task check passed: build, tests, formatting, C++/QML lint, import/layering policy fixtures and generated QML metadata.
- Full test run: 466/466 passed, including Holonight/Fusion runtime acceptance, private D-Bus tests and the no-libalpm link check.
- UBSan checks suite: 71/71 passed in 432 ms with halt_on_error enabled; interval and timestamp boundary regressions are included.
- Separate formatting and lint passes cover the new, untracked C++ files that the repository workflow does not enumerate.
- Existing D-Bus contract and update-status-client tests remain unchanged; monitor/process fixture edits are lint-only.
- CI rehearsal: licensing passed; build-test and static-checks failed before project validation because all retries of the pinned noto-fonts archive download timed out. Logs: build/ci/20261009T171112Z-yeks2dp8/. T-039 remains open until the full rehearsal passes.

### Repository catalog correction (2026-10-09)

- Replaced aggregate production freshness selection with per-repository digest/timestamp selection in configured order.
- Retained complete checked catalogs with hash-keyed provenance, cache leases, atomic snapshot publication and safe reclamation.
- Added offline installed-package reevaluation and independent persisted check history for page/sidebar presentation.
- Preserved the pending documented `task run:packaged` development task.
- CI rehearsal logs: `build/ci/20261009T182406Z-twve3z6s/`. Licensing passed. Build-test and static-checks stopped before project validation because all pinned noto-fonts archive download attempts timed out. T-039 remains open pending a successful full rehearsal.
- Two prior regression expectations were corrected: failed snapshot persistence must preserve the published result rather than advance the in-memory count.
- Final correction regressions: 480/480 tests passed, including the five-upgrade repository selection, offline installed-package changes, publication leases, cache recovery, nanosecond timestamp preservation, policy fixtures and Holonight/Fusion runtime acceptance. The libalpm-free checks executable passed in 0.476 s; editor metadata refresh passed.
- Final `task check PRESET=test` passed, including formatting, full C++ lint, QML lint, import/layering policy checks and generated QML metadata. T-039 remains open because the full CI rehearsal is blocked by the dependency download described above.
