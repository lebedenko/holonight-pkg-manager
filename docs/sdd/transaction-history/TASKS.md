# SDD Tasks — transaction-history

Derived from SPEC.md (2026-11-23 amendments) and DESIGN.md section 14 (SPEC issues 1 and 2 are applied: the tag is written after the final re-validation and once per libalpm commit). Order is bottom-up by dependency.

## Shared configuration and domain port

- [x] T-001: Add `parsePacmanLogLocations` (`LogFile`, `DBPath`, default `/var/lib/pacman`) to the shared pacman.conf parser, leaving `PacmanConfig` unchanged
  - REQs: REQ-F-005, REQ-F-013
  - Check: `pacman_config_test` `PacmanLogLocations.*` cases return the configured paths, and the existing `PacmanConfig` and `PacmanSettings` cases (including `IgnoresPathsAndExternalDownloadCommands`) pass unchanged.

- [x] T-002: Declare the `HistorySource` port and its types in `src/domain` (`history_source.h`, `history_source.cpp`) with no Qt or libalpm header
  - REQs: REQ-F-033, REQ-F-034, REQ-C-001
  - Check: a `tests/domain/history_source_test.cpp` case compiles a fake `HistorySource` in `test_holonight_packages_checks`, and `grep -nE '#include <(Q|alpm)' src/domain/include/holonight_packages_domain/history_source.h src/domain/src/history_source.cpp` prints nothing.

## Timestamp and line parser (`src/backends`, pure)

- [x] T-003: Timestamp parser `pacman_log_timestamp.{h,cpp}`: current format with offset and legacy format read as local time, with `hasTimestampShape` and `parseTimestamp`
  - REQs: REQ-F-015, REQ-F-016
  - Check: `tests/backends/pacman_log_timestamp_test.cpp` shows that `[2026-11-23T10:05:18+0200]` yields 08:05:18 UTC under `TZ=UTC`, that `[2019-01-01 12:00]` yields 11:00 UTC under `TZ=Europe/Berlin`, and that the truncated `[2026-11-2` yields nullopt.

- [x] T-004: `parseLines` transaction blocks: one entry per start line, origin from the nearest Running line, completed status, and interrupted status when a second start line follows
  - REQs: REQ-F-006, REQ-F-007, REQ-F-012, REQ-F-014
  - Check: `tests/backends/pacman_log_parser_test.cpp` cases `BlocksInFileOrder`, `OriginFromRunningLines` and `StartStartCompleted` (string literals, no files) yield exactly the expected origins (`Packages` uid 1000, `pacman` with command, `unknown`) and the statuses completed, interrupted, completed.

- [x] T-005: `parseLines` per-package actions with versions, taken from the reference-log line formats of DESIGN 5.3
  - REQs: REQ-F-008
  - Check: the `ActionKinds` case in `pacman_log_parser_test` feeds the five real lines of DESIGN 5.3 and gets five actions whose old and new versions match the table (upgraded and downgraded carry both, installed and reinstalled carry only the new one, removed carries only the old one), and a differential case agrees with a `std::regex` oracle on a seeded sample.

- [x] T-006: `parseLines` warnings and errors attached to their transaction, `[ALPM-SCRIPTLET]` excluded, and malformed or unknown-tag lines skipped without failing
  - REQs: REQ-F-010, REQ-F-011, REQ-F-016
  - Check: the `MessagesAndMalformed` cases show that `warning: foo: …` and `error: bar: …` in a hook region both appear on the transaction, that `[ALPM-SCRIPTLET] warning: scriptlet text` leaves the warning list empty, and that the five bad inputs (no timestamp, `[2026-11-2`, `[FOO] bar`, invalid UTF-8, and one valid transaction) return exactly one transaction.

- [x] T-007: `parseLines` pre-transaction and post-transaction hooks, with a Running line that has no start line discarding its hooks
  - REQs: REQ-F-009
  - Check: `pacman_log_parser_test` `HookAttachment` shows that the fixture of REQ-F-009 lists two post-transaction hooks on the first transaction and one pre-transaction hook on the second with no post-transaction hooks, and that hooks after a Running line with no start line attach to no transaction.

## Adapter `PacmanLogHistorySource` (`src/backends`, no Qt, no libalpm)

- [x] T-008: Test support: `tests/support/history_fixture.h` (temporary directory, `writeConf`, `append`, `truncateAndWrite`, `renameOver`, lock helpers), the `guardedIo()` seam, and `counting_history_source.h`
  - REQs: REQ-C-003
  - Check: a test that calls `guardedIo()` with the path `/var/log/pacman.log` fails, and `ctest -R history` passes on a machine where `/var/log/pacman.log` is absent.

- [x] T-009: `PacmanLogHistorySource` path resolution, read-only open through `HistoryFileIo`, and explained empty states for a missing log, an unreadable log and an unreadable `pacman.conf`
  - REQs: REQ-F-005, REQ-F-021, REQ-F-036
  - Check: `pacman_log_history_source_test` cases `LogPathFromConf`, `DefaultLogPath` (the guarded `io` records no open of the default file), `MissingLogHasReason`, `UnreadableLogHasReason` (mode 000, skipped as root) and `UnreadableConfHasReason` pass, and the counting `io` records only `O_RDONLY` opens.

- [x] T-010: Incremental reads: consumed offset after complete lines only, 1 MiB chunks cut at the last newline, and a delta result with `drop_last` and `append`
  - REQs: REQ-F-018, REQ-F-020
  - Check: `IncrementalReadsOnlyAppended` (1,000 transactions plus one appended: bytes read equal the appended byte count, `drop_last` is 0, the first 1,000 entries are unchanged) and `PartialLineNotConsumed` (`consumedOffset()` equals the start of the partial line, and after appending `leted\n` the entry is completed) both pass.

- [x] T-011: Full reparse when the log shrinks or its inode changes
  - REQs: REQ-F-019
  - Check: `TruncateTriggersFullReparse` and `RenameOverChangesInodeTriggersFullReparse` pass, showing that the counting `io` records a read of the whole new file and that the entries equal a fresh `Full` read.

- [x] T-012: Lock probe for open transactions: in progress while `db.lck` exists, interrupted when it is absent, evaluated at every read
  - REQs: REQ-F-013, REQ-F-014
  - Check: `InProgressWithLock`, `LockRemovedWithoutNewBytes` (zero bytes read and `drop_last` 1), `InterruptedAtEndNoLock` and `InterruptedStartStart` pass, using the `LockProbe` seam for the lock state.

## Presentation helpers (`src/application`, std only)

- [x] T-013: Presentation helpers `history_presentation.{h,cpp}`: origin labels with the resolver seam, summary text in the fixed kind order, status and action-kind labels, the local day, and the `HH:MM` time label
  - REQs: REQ-F-015, REQ-F-025, REQ-F-027, REQ-F-028
  - Check: `tests/application/history_presentation_test.cpp` shows that a current-uid origin is `Packages`, uid 1001 with a resolver returning `alice` gives `Packages · alice`, `12 upgraded · 1 installed · 2 removed` and `1 removed` are produced exactly, pacman and unknown give `pacman` and `unknown`, and `TimeLabelLegacy` shows `12:00` under `TZ=Europe/Berlin`.

## History model and filter (`apps/packages/app`)

- [x] T-014: `HistoryModel` construction and first load: no read at construction, one full read on the first `ensureLoaded()` on a `QtConcurrent` worker, and a view-model that names only the port
  - REQs: REQ-F-017, REQ-F-022, REQ-F-034
  - Check: `tests/history/history_model_test.cpp` cases `NoReadAtConstruction` (zero calls) and `FirstOpenOneFullParse` pass, `ReadsRunOffTheMainThread` records a worker thread for both the full and the incremental read, and `test_holonight_packages_history` links no backends target and passes with the fake `HistorySource`.

- [x] T-015: `HistoryModel` rows and roles: newest first with row `r` mapped to `N-1-r`, day-grouped `dayKey`, computed role values, the `loading`, `loaded` and `emptyReason` properties, and the delta applied with reset, `drop_last` and inserts
  - REQs: REQ-F-025, REQ-F-026, REQ-F-027, REQ-F-028
  - Check: `GroupsByDayNewestFirst` (fixed `TZ`, entries on three days) shows three day groups in descending order with rows in descending time order, and a `RowRoles` case shows the time, origin, summary and status labels for a fixture entry.

- [x] T-016: `HistoryModel` refresh: `refresh()` starts one incremental read, a refresh that arrives during a read queues exactly one repeat, and `refreshIfLoaded()` does nothing before the first load
  - REQs: REQ-F-031
  - Check: `history_model_test` cases `RefreshBeforeLoadIsNoop`, `RefreshDuringReadRepeatsOnce` and `RefreshIsIncremental` pass, with the counting source showing one incremental read and no second full parse.

- [x] T-017: `HistoryFilterModel` name filter: case-insensitive substring on `PackageNamesRole`, selection tracked by `sequence`, `currentRow`, `currentEntry` and `get(row)`
  - REQs: REQ-F-029
  - Check: `tests/history/history_filter_model_test.cpp` shows that filter `linux` keeps only T1, `git` keeps only T2, an empty filter keeps both, `LINUX` keeps T1, and the selected entry survives an insert at row 0.

## Composition root and refresh path

- [x] T-018: Wire `PacmanLogHistorySource` and `HistoryModel` in `PackagesApplication` and add `ModelListRefresh::setHistoryTarget`, called from both refresh methods, with the destruction order of DESIGN 5.9
  - REQs: REQ-F-017, REQ-F-031, REQ-F-034
  - Check: `tests/apps/history_model_refresh_test.cpp` shows that a fake transaction service emitting one outcome through `refreshAfterOutcome` and through `refreshAfterUpgrade` each cause one incremental read, that a counting source records zero reads after application construction, and that `grep -rn PacmanLogHistorySource apps/packages` matches only `PackagesApplication.cpp`.

## QML (`qml/workspace`, `qml/history`)

- [x] T-019: Enable the History sidebar entry as the last page entry, remove the `enabled: false` placeholder, and add the history page as index 3 of `WorkspaceWindow`
  - REQs: REQ-F-023
  - Check: `sidebar_history_test` loads `qml/workspace/Sidebar.qml`, shows that `sidebarHistoryNav` is enabled and the last `HnNavigationDelegate` of the page column, and that its activation handler emits `pageRequested("history")`, and the test fails against the current placeholder. No test automates pointer or focus input.

- [x] T-020: `HistoryView` shell: `active` triggers `ensureLoaded()` once, the loading state shows only during the first parse, the empty state shows the reason text with no dialog, and the refresh path is kept during later reads
  - REQs: REQ-F-017, REQ-F-021, REQ-F-024
  - Check: `tests/history/history_view_test.cpp` cases `PageOpenStartsOneFullParse`, `LoadingHidesRows` (loading state visible, zero delegates through `childItems()`), `EmptyStateShowsReason` (no visible `QQuickPopup`) and `RefreshKeepsRowsAndSelection` pass. No test automates pointer or focus input.

- [x] T-021: `HistoryTable`, `HistoryRow`, day headers, `HistoryToolbar` search field and refresh button, with objectNames `historyRowTime`, `historyRowOrigin`, `historyRowSummary` and `historyRowStatus`
  - REQs: REQ-F-025, REQ-F-026, REQ-F-029, REQ-F-031, REQ-F-032, REQ-C-006
  - Check: `history_view_test` cases `RowShowsFourTexts`, `RefreshHandlerUsesSamePath` (one incremental read from `refreshRequested()`) and `NoTransactionActions` (no button under `historyPage` is bound to a helper or transaction call) pass, `ctest -R test_runtime_controls` passes in the Holonight and Fusion styles, and `task qml-lint` passes for these files. No test automates pointer or focus input.

- [x] T-022: `HistoryDetailPanel` (on `PackageDetailFrame`) and `HistoryDetailContent`: actions as `old → new`, separate "Hooks before" and "Hooks after" sections hidden when empty, the warning text, the command line only for pacman origin, and no scriptlet text
  - REQs: REQ-F-011, REQ-F-030, REQ-F-032
  - Check: `history_view_test` cases `DetailShowsActionsHooksMessages`, `PacmanOriginShowsCommand`, `PackagesOriginHasNoCommandElement` and `ScriptletTextNeverShown` pass against a two-action, two-hook, one-warning fixture. No test automates pointer or focus input.

## Helper (`apps/helper`, `src/transaction`, `src/application`)

- [x] T-023: `ExecutionContext::caller_uid` set in `PlanService::startRunning()`, and the pure `formatHistoryTag` in `src/transaction/src/history_tag.{h,cpp}`
  - REQs: REQ-F-001, REQ-F-002
  - Check: `tests/transaction/history_tag_test.cpp` case `FormatTag` shows `Running 'install foo bar' (uid 1000)`, `Running 'remove foo' (uid 1000)` and `Running 'upgrade' (uid 1000)`, rejects an upgrade with names and a name containing a quote, and a grep of `apps/helper` finds no caller uid taken from D-Bus arguments.

- [x] T-024: `HistoryTagWriter` with the `tag-write` fault seam and the `tag-writing`, `tag-written` and `tag-failed` observe events; a failed write logs a journal warning and does not stop the transaction
  - REQs: REQ-F-002, REQ-F-004
  - Check: `history_tag_test` case `TagWriteFails` shows that `Execute` returns `Succeeded`, that `installedVersions()` equals a control run, and that the journal capture holds the warning; `grep -rn alpm_logaction src` matches only `history_tag.cpp`.

- [x] T-025: Write the tag in `ExecutionDriver::commit()` after the cancel check and before `alpm_trans_commit`, once per attempt, with `tag_written_`
  - REQs: REQ-F-001, REQ-F-003, REQ-C-004
  - Check: `history_tag_test` case `TagOrder` shows that the event subsequence `authorize`, `opened`, `resolved`, `tag-writing`, `tag-written`, `commit-point`, `committed`, `finished` holds and that the `[HOLONIGHT]` line precedes `transaction started`; `RefusedWritesNothing` (denying authorizer, and the conflict fixture) leaves the log byte length and SHA-256 unchanged; the test asserts its root is under the temporary directory and `grep -rnE 'QTest::(mouse|keyClick)' tests` finds nothing in history sources.

- [x] T-026: Write one tag per libalpm commit in `UpgradeDriver::commitStep()`, so an upgrade with a keyring step writes two lines
  - REQs: REQ-F-001, REQ-F-002
  - Check: `upgrade_execute_test` shows that an upgrade without a keyring step has exactly one `Running 'upgrade' (uid N)` line with nothing after the closing parenthesis, and that a keyring fixture has two such lines, each immediately before its own `transaction started` line.

- [x] T-027: Integration check of the failed tag through the real source: a fixture log with no `[PACMAN] Running` line before a start line gives origin `unknown`, and the appended `[HOLONIGHT]` line before a start line gives origin Packages
  - REQs: REQ-F-004, REQ-F-007
  - Check: `pacman_log_history_source_test` on the log written by `history_tag_test` `TagWriteFails` reports origin `unknown` for that transaction, and a second fixture reports origin Packages with uid 1000.

## Policy (`scripts/check-layering.py`, `tests/policy/test_layering_policy.py`)

- [x] T-028: Layering rules Y-001, Y-002, Y-003 and H-014 with passing and failing fixtures
  - REQs: REQ-F-023, REQ-F-032, REQ-F-033, REQ-F-034, REQ-C-001, REQ-C-002
  - Check: `task layering-check` passes, and the fixtures `y001-qt-include-in-history-adapter`, `y001-alpm-include-in-history-adapter`, `y002-adapter-header-in-view-model`, `y002-helper-client-in-view-model`, `y003-history-nav-disabled`, `y003-history-nav-not-last`, `h014-logaction-in-driver` each fail while their `legit-*` counterparts pass.

- [x] T-029: Guarantees of no storage and no D-Bus change: a `--base` diff step for `apps/packaged` and the D-Bus XML, and a directory-snapshot test for a full read and a refresh
  - REQs: REQ-F-035, REQ-C-005
  - Check: `scripts/check-layering.py --base <base>` fails on a diff in `apps/packaged` or `apps/helper/dbus` and passes otherwise, and `NoFilesWritten` shows the directory listing and file digests of a temporary directory unchanged after a full read and an incremental refresh.

## Benchmark and latency probe

- [x] T-030: Benchmark executable `test_holonight_history_bench` with the deterministic log generator, the NF-001 median of five full parses, the `HOLONIGHT_HISTORY_BENCH_LOG` reference case, and the `task bench:history` task that builds in Release
  - REQs: REQ-NF-001
  - Check: `task bench:history` runs the Release build and the generated 60,000-line, 1,178-transaction fixture has a median full parse under 500 ms, and the executable registers no test in a Debug build.

- [x] T-031: Incremental refresh budget (twenty append-and-refresh iterations and twenty no-byte refreshes, median under 20 ms) and the event-latency probe with its sensitivity control
  - REQs: REQ-NF-002, REQ-NF-003
  - Check: `task bench:history` shows the median append-and-refresh time and the median no-byte refresh time both under 20 ms, and the probe reports the largest event delay under 50 ms while its sensitivity control reports a delay of at least 50 ms (or logs that the control was skipped).

## Documentation

- [x] T-032: Document the HOLONIGHT tag format in `apps/helper/README.md` and the History page in `apps/packages/README.md`
  - REQs: REQ-F-001, REQ-F-002, REQ-F-023
  - Check: `grep -n 'Running .install\|uid N' apps/helper/README.md` finds the tag format, and `grep -n 'History' apps/packages/README.md` finds the page description.

## Final checks

- [x] T-033: `task format` and `task format-check`
  - REQs: REQ-C-008, REQ-C-009
  - Check: `task format-check` exits 0 with no diagnostics after `task format` has run on the feature branch.

- [x] T-034: `task tidy`
  - REQs: REQ-C-008
  - Check: `task tidy` exits 0 with zero clang-tidy diagnostics against `build/dev`.

- [x] T-035: `task qml-lint`
  - REQs: REQ-C-006
  - Check: `task qml-lint` exits 0, including the QML policy fixtures for `qml/history` and `qml/workspace/Sidebar.qml`.

- [x] T-036: `task layering-check`
  - REQs: REQ-C-001, REQ-F-033, REQ-F-034
  - Check: `task layering-check` exits 0 with the new Y-001 to Y-003 and H-014 fixtures included.

- [x] T-037: `task test`
  - REQs: REQ-C-009, REQ-F-036
  - Check: `task test` exits 0 with every history GTest listed in DESIGN 10.2 present and passing, and the traceability table of SPEC section Verification has a test name for each REQ-F and REQ-NF.

- [x] T-038: Dual-style runtime acceptance through `task check`, with the History page in the Holonight and Fusion styles
  - REQs: REQ-C-006, REQ-F-026
  - Check: `task check` exits 0 and its runtime acceptance of the History page passes in both styles without clipped columns. Production launch checks only enumerate ALPM read-only with isolated HOME/XDG and desktop activation, and no test automates pointer or focus input.

- [x] T-039: `task check:integration` on the commit that contains the helper tag change
  - REQs: REQ-C-007, REQ-C-004
  - Check: `task check:integration` exits 0 on the commit that contains T-023 to T-027, and its fixture-root integration tests for the tag run under the temporary directory with no pointer or focus input.

- [x] T-040: Record the REQ-NF-001 measurement on the reference log (4.5 MiB, 53,000 lines, 1,178 transactions)
  - REQs: REQ-NF-001
  - Check: `HOLONIGHT_HISTORY_BENCH_LOG=/var/log/pacman.log` run of the bench reports a full-parse median and an entry count of 1,178, and that value is written into the change description. Production launch checks only read ALPM read-only with isolated HOME/XDG, and no test automates pointer or focus input.
