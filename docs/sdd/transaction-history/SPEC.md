# Transaction history from the pacman log — SPEC (HoloNight Packages)

Status: Draft, 2026-11-23. Requirements are written in EARS. This SPEC states what the History page must do and how it is verified. It does not fix classes, file layouts or parser internals; those belong to Design. Open items that Design must settle are listed under "Open questions" and do not change any requirement.

Template labels: **[U]** Ubiquitous, **[E]** Event-driven, **[S]** State-driven, **[O]** Optional feature, **[X]** Unwanted behaviour (the "If…, then…" template). Requirement IDs: `REQ-F` functional, `REQ-NF` non-functional, `REQ-C` constraints.

## Overview

HoloNight Packages performs package changes in the privileged helper `holonight-package-helper`. Until now the app has not shown what the helper or any other tool did to the system. This feature adds a read-only History page that lists the transactions recorded in the pacman log, newest first, with the origin, a summary, the status and the per-package details of each one.

The feature has two parts:

- The helper writes one tagged line to the pacman log for each committed transaction it runs, so that a transaction started from the app can be attributed to the app and its caller.
- The app reads the pacman log, parses it without libalpm, and shows the result. Nothing is stored by the app; the pacman log is the only source.

The page reads the log on first open, not at startup. Later refreshes read only the bytes added since the previous read.

## Scope

In scope:

- The helper's history tag: one `alpm_logaction` line per committed helper transaction, its format, and the behaviour when it is refused or the write fails.
- The pacman log source: the log path from `LogFile` in `pacman.conf`, the parse of transactions, origins, per-package actions, hooks, warnings and errors, status, timestamps, and incremental reads.
- The `HistorySource` port in `src/domain`, its Qt-free adapter `PacmanLogHistorySource` in `src/backends`, and the app view-model that uses the port.
- The History page: its sidebar entry, loading state, day-grouped table, package-name filter, detail panel, and refresh.
- Non-functional limits for the first full parse and for incremental refreshes.

## Non-goals

- Undo, rollback or downgrade from a history entry.
- Reporting `.pacnew` files.
- Links from the Installed or Explore package detail panels to the History page.
- Recording refused, cancelled or pre-commit failed requests anywhere. They do not appear in history.
- Attributing AUR or yay installs to the app. yay runs `pacman -U`, so those transactions show origin `pacman`.
- Paging. The table loads the whole parsed log. Paging is a fallback that is added only if REQ-NF-001 fails.
- Log rotation handling beyond the full reparse on shrink or inode change (REQ-F-019).
- New storage: no database, cache file, setting or D-Bus method for history.
- Any change to `holonight-packaged`.

## Glossary

- **Pacman log**: the file named by `LogFile` in `pacman.conf`, default `/var/log/pacman.log`. Each line starts with a bracketed timestamp, then a bracketed tag, then the text. This SPEC uses "line" for one such line.
- **HOLONIGHT tag**: the line the helper writes, `[HOLONIGHT] Running '<action> <names…>' (uid N)`, after the timestamp of the pacman log line.
- **Transaction**: one block of the pacman log that starts with `[ALPM] transaction started` and ends with `[ALPM] transaction completed`. If a second start line appears before a completed line, the first block ends at that second start line. One transaction is one history entry.
- **Origin**: who started a transaction. **Packages** when the nearest preceding Running line is a HOLONIGHT tag (with the caller uid). **pacman** when it is a `[PACMAN] Running '…'` line (with the command line). **unknown** when there is no such line.
- **Running line**: a `[PACMAN] Running '…'` or `[HOLONIGHT] Running '…'` line.
- **Hook line**: an `[ALPM] running '<name>.hook'...` line.
- **Pre-transaction hook**: a hook line between a transaction's Running line and its start line (libalpm `PreTransaction` hooks, for example `71-dkms-remove.hook`).
- **Post-transaction hook**: a hook line after a transaction's completed line, before the next boundary line.
- **Boundary line**: a Running line, or a transaction start line. A boundary ends the attachment of hook, warning and error lines to the previous transaction (REQ-F-009).
- **Lock file**: the pacman database lock, `db.lck` in the database directory (`DBPath`, see Open questions).
- **Consumed offset**: the byte offset up to which the source has read complete lines of the pacman log. It never passes a trailing partial line (REQ-F-020).
- **Completed**: a transaction that has a `[ALPM] transaction completed` line.
- **Interrupted**: a transaction that has no completed line and is followed by another start line, or is open at the end of the file while the lock file does not exist.
- **In progress**: a transaction that has no completed line, is open at the end of the file, and whose lock file exists.
- **Summary**: the one-line count of per-package actions of a transaction (REQ-F-028).
- **Presentation model**: the app-layer model that turns parsed entries into day groups, rows, labels and filtered results.

## Requirements

### 1. Helper tag

### REQ-F-001: Tag on authorized, validated transactions [E]
**Statement:** When the helper has authorized a transaction request and the plan validation of that request has succeeded, the helper shall write exactly one line through `alpm_logaction` with the tag `HOLONIGHT`, after the final re-validation of the plan and immediately before the transaction is committed, once for each libalpm transaction it commits.
**Acceptance criteria:**
- Integration test on a fixture root under a temporary directory with `file://` repositories: an authorizing seam allows an install of a fixture package. The pacman log then contains exactly one line with the `[HOLONIGHT]` tag, and it matches `\[HOLONIGHT\] Running 'install <name>' \(uid <N>\)$`, where `<name>` is the package and `<N>` the uid the seam reports for the caller.
- A recorded call order shows the authorization call, then transaction initialization and the final plan re-validation, then the `alpm_logaction` call, then commit.
- A fixture upgrade whose plan includes a keyring step commits two libalpm transactions and writes two HOLONIGHT lines, one immediately before each `transaction started` block, so both history entries have origin Packages.

### REQ-F-002: Tag format with no other text [U]
**Statement:** The helper shall write the HOLONIGHT tag only in the form `Running '<action> <names…>' (uid N)`, where `<action>` is one of `install`, `remove` or `upgrade`, `<names…>` are the package names already validated by the plan, separated by single spaces, and `N` is the caller's uid. An `upgrade` line has no names. No other free text shall be written with the tag.
**Acceptance criteria:**
- A unit test that formats one install of `foo bar`, one remove of `foo` and one upgrade, all by uid 1000, yields exactly `Running 'install foo bar' (uid 1000)`, `Running 'remove foo' (uid 1000)` and `Running 'upgrade' (uid 1000)`.
- A fixture integration test that runs an upgrade without a keyring step shows exactly one HOLONIGHT line in the log (one per transaction block), with no characters after `(uid N)` other than the end of the line.

### REQ-F-003: Refused requests write nothing [X]
**Statement:** If the helper refuses a transaction request, whether at authorization, at plan validation or at the final re-validation before commit, then the helper shall write no HOLONIGHT line.
**Acceptance criteria:**
- With an authorizer that denies every request, the pacman log's byte length and SHA-256 are the same before and after `Execute` is refused, and the log holds no `[HOLONIGHT]` text.
- With a fixture plan that fails validation (the conflict case of the package helper SPEC), the log is unchanged and holds no `[HOLONIGHT]` text.

### REQ-F-004: Failed tag write does not change the transaction [X]
**Statement:** If the `alpm_logaction` write of the HOLONIGHT tag fails, then the helper shall run the transaction unchanged, and the history entry for that transaction shall have origin `unknown`.
**Acceptance criteria:**
- With a seam that makes the tag write fail, `Execute` returns `Succeeded`, and the installed package set equals the set produced by a control run with the write enabled.
- With the same seam, the history source reports origin `unknown` for that transaction in a fixture log that has no `[PACMAN] Running` line before its start.

### 2. Source and parsing

### REQ-F-005: Log path from pacman.conf [U]
**Statement:** The `PacmanLogHistorySource` shall take the log path from the `LogFile` key of `pacman.conf`, read through the shared pacman.conf parser, and shall use `/var/log/pacman.log` when the key is empty.
**Acceptance criteria:**
- With a fixture `pacman.conf` that sets `LogFile` to a file under the test's temporary directory, the resolved path equals that file. The source reads that file and not the default.
- With a fixture `pacman.conf` that does not set `LogFile`, the resolved path equals `/var/log/pacman.log`. The test asserts the resolved path only; the default file is not opened.

### REQ-F-006: One entry per transaction block [E]
**Statement:** When the source parses a pacman log, the source shall create one history entry for each transaction, in file order, with the timestamp of its start line.
**Acceptance criteria:**
- A fixture with three complete transaction blocks yields three entries whose start timestamps follow file order.
- A fixture with a complete block, a block that is followed by a second start line, and a complete block yields three entries: completed, interrupted, completed (REQ-F-014).

### REQ-F-007: Origin from the nearest Running line [E]
**Statement:** When the source finds a transaction start line, the source shall set the origin of that transaction from the nearest preceding Running line, provided that no other transaction start line lies between that Running line and the start line. A HOLONIGHT line gives origin Packages with the uid; a `[PACMAN] Running` line gives origin pacman with the command line; without such a line the origin is unknown.
**Acceptance criteria:**
- A fixture with `[HOLONIGHT] Running 'remove foo' (uid 1000)` followed by a start line yields origin Packages with uid 1000.
- A fixture with `[PACMAN] Running 'pacman -U /tmp/x.pkg.tar.zst'` followed by a start line yields origin pacman with that command line.
- A fixture with a start line and no preceding Running line yields origin unknown.
- A fixture with a Running line, then another start line, then a start line yields origin unknown for the second start.

### REQ-F-008: Per-package actions with versions [E]
**Statement:** When a transaction block contains an ALPM action line for a package, the source shall record one action of kind installed, upgraded, downgraded, reinstalled or removed for that package, with the old version and the new version wherever the line gives them.
**Acceptance criteria:**
- A fixture block with one line of each kind (installed, upgraded, downgraded, reinstalled, removed) yields five actions with the correct kinds. The upgraded and downgraded actions carry both versions; the installed action carries only the new version; the removed action carries only the old version; the reinstalled action carries only the new version.
- Line formats are copied from the reference log (the developer machine's real pacman log, see REQ-NF-001); see Open questions, OQ-H6.

### REQ-F-009: Pre- and post-transaction hooks [E]
**Statement:** When a hook line lies between a Running line and the next transaction start line, the source shall attach it to that next transaction as a pre-transaction hook. When a hook line follows the completed line of a transaction with no Running line in between, the source shall attach it to that transaction as a post-transaction hook, until the next boundary line.
**Acceptance criteria:**
- A fixture with a completed line, then two hook lines, then `[PACMAN] Running '…'`, then a third hook line, then a start line: the first transaction lists the two hooks as post-transaction hooks, and the second lists the third hook as a pre-transaction hook and has no post-transaction hooks yet.
- A fixture with a completed line, then a hook line, then a start line with no Running line between them attaches the hook to the first transaction as a post-transaction hook.
- A fixture whose Running line is followed by a hook line but by no start line (for example a failed download) attaches that hook to no transaction, and the next Running line discards it.

### REQ-F-010: Warnings and errors belong to the transaction [E]
**Statement:** When a `warning:` or `error:` line lies inside the boundaries of a transaction defined by REQ-F-009, the source shall attach that line to the transaction.
**Acceptance criteria:**
- A fixture with `warning: foo: …` inside a transaction and `error: bar: …` in its hook region yields both lines on that transaction's warnings and errors list.
- A `warning:` line after the next boundary line is not attached to the earlier transaction.

### REQ-F-011: Scriptlet output is excluded [U]
**Statement:** The source shall exclude every `[ALPM-SCRIPTLET]` line from the warnings and errors of the entry, and from any text the detail panel shows.
**Acceptance criteria:**
- A fixture block with `[ALPM-SCRIPTLET] warning: scriptlet text` yields an entry whose warning list is empty.
- A GUI detail-panel test for that entry shows no text containing `scriptlet text`.

### REQ-F-012: Completed status [E]
**Statement:** When a transaction start line is followed by its completed line, the source shall set the status of that transaction to completed.
**Acceptance criteria:**
- A fixture with one start and one completed line yields one entry with status completed.

### REQ-F-013: In progress while the lock file exists [S]
**Statement:** While a transaction is open at the end of the pacman log and the lock file exists, the source shall report that transaction as in progress.
**Acceptance criteria:**
- A fixture with an open block at end of file and a `db.lck` file in the fixture database directory yields status in progress.
- After the `db.lck` file is removed, a refresh that reads no new bytes changes the status of that entry to interrupted. This shows the status is evaluated at each read and not stored.

### REQ-F-014: Interrupted transactions [X]
**Statement:** If a transaction start line is followed by another transaction start line before any completed line, or the log ends inside an open transaction while the lock file does not exist, then the source shall report the transaction as interrupted.
**Acceptance criteria:**
- A fixture with start, start, completed yields two entries: the first with status interrupted, the second with status completed.
- A fixture ending with an open block and no `db.lck` yields status interrupted.

### REQ-F-015: Timestamps in both formats, shown in local time [U]
**Statement:** The source shall parse the current timestamp format `[2026-11-23T10:05:18+0200]` and the legacy format `[2019-01-01 12:00]`, where the legacy format has no offset and is read as local time, and the app shall display every timestamp in local time.
**Acceptance criteria:**
- With `TZ=UTC`, the current-format fixture `[2026-11-23T10:05:18+0200]` yields the instant 08:05:18 UTC.
- With `TZ=Europe/Berlin`, the legacy-format fixture `[2019-01-01 12:00]` yields the instant 11:00 UTC, and the presentation model shows it as `12:00`.

### REQ-F-016: Malformed lines are skipped [X]
**Statement:** If a line is malformed or has an unknown tag, then the source shall skip that line and continue the parse, and the parse shall not fail.
**Acceptance criteria:**
- A fixture that contains a line without a timestamp, a truncated timestamp `[2026-11-2`, an unknown tag `[FOO] bar`, bytes that are not valid UTF-8, and one valid transaction returns a successful parse with exactly that one transaction.

### 3. Reads, refreshes and missing logs

### REQ-F-017: First full parse on first open [E]
**Statement:** When the History page is opened for the first time in a session, the app shall perform one full parse of the pacman log, and the app shall not read the pacman log at startup.
**Acceptance criteria:**
- A test with a counting source seam records zero reads after application startup with the History page never opened.
- Opening the History page records exactly one full parse.

### REQ-F-018: Incremental refresh reads only new bytes [E]
**Statement:** When a refresh is requested after one of the app's own transactions, or by the manual refresh action, the source shall read only the bytes after the consumed offset.
**Acceptance criteria:**
- A fixture log with 1,000 transactions is parsed once. One transaction is then appended. The counting seam records bytes read by the refresh equal to the number of appended bytes.
- After the refresh, the entries equal the previous 1,000 entries followed by the new one, and the first 1,000 entries are unchanged.

### REQ-F-019: Full reparse on shrink or inode change [X]
**Statement:** If the pacman log is smaller than the consumed offset, or its inode differs from the inode recorded at the last read, then the source shall perform a full reparse.
**Acceptance criteria:**
- A fixture log is truncated to a smaller size and rewritten. The counting seam records a read of the whole new file.
- A fixture log is written to a new file of the same size and renamed over the old one, which changes the inode. The counting seam records a read of the whole file, and the entries equal a fresh full parse.

### REQ-F-020: Trailing partial line is not consumed [U]
**Statement:** The source shall not advance the consumed offset past a trailing line that has no newline, and shall consume that line only in a later read after its newline has been written.
**Acceptance criteria:**
- A fixture log ends with `[2026-11-23T10:05:18+0200] [ALPM] transaction comp` and no newline. The consumed offset, read through a test accessor on the adapter outside the port, equals the byte offset of the start of that partial line.
- After appending `leted\n` to the same file, the next refresh yields that transaction with status completed.

### REQ-F-021: Missing or unreadable log is an explained empty state [X]
**Statement:** If the pacman log is missing or cannot be read, then the source shall return an empty result with a non-empty reason, and the History page shall show that reason in an empty state and shall not show an error dialog.
**Acceptance criteria:**
- A source call on a path that does not exist returns zero entries and a reason string that names the path.
- A fixture log with permissions `000`, read by a non-root test, returns zero entries and a reason.
- A QML test with the source returning zero entries and a reason shows an empty-state element with that reason text, and asserts that no error dialog is open.

### REQ-F-022: Parsing runs on a worker thread [E]
**Statement:** When the source runs a full parse or an incremental refresh, the app shall run it on a worker thread started with QtConcurrent, and not on the UI thread.
**Acceptance criteria:**
- A counting seam records the thread of each parse call. For the first full parse and for an incremental refresh, the recorded thread is not the main application thread.

### 4. Presentation and page

### REQ-F-023: History is the last sidebar entry [U]
**Statement:** The sidebar shall show the History entry as the last page entry, after Installed (the Settings entry stays in the sidebar footer below the page list), and the entry shall be enabled and emit `pageRequested("history")` when it is activated.
**Acceptance criteria:**
- A QML test loads `qml/workspace/Sidebar.qml`, checks that `sidebarHistoryNav` is enabled and is the last `HnNavigationDelegate` in the page list, and invokes its activation handler, which must emit `pageRequested("history")`.
- The `enabled: false` placeholder that exists in `Sidebar.qml` today is removed; the QML test fails against the placeholder.

### REQ-F-024: Loading state during the first parse only [S]
**Statement:** While the first full parse is running, the History page shall show a loading indicator and no table rows. While a later refresh is running, the History page shall keep its rows and its selection.
**Acceptance criteria:**
- A QML test with a source seam that blocks the first read shows a loading indicator (objectName set by Design) and zero table rows. After the seam is released, the loading indicator is gone and the table shows the seam's entries.
- A QML test with a selected row and a seam that blocks a refresh shows no loading indicator, the same row count, and the same selected entry while the refresh is blocked.

### REQ-F-025: Rows newest first, grouped by day [U]
**Statement:** The presentation model shall show one row per transaction, newest first, grouped by calendar day in local time.
**Acceptance criteria:**
- A GTest on the presentation model with entries on three calendar days yields three day groups in descending day order, and within each group the rows are in descending time order.
- The test uses a fixed time zone set in the test environment.

### REQ-F-026: Row fields [U]
**Statement:** Each row of the History table shall show the time, the origin, the summary and the status of its transaction.
**Acceptance criteria:**
- A QML test over a fixture entry checks that the row delegate exposes the four texts: local time, origin label, summary and status label.
- The dual-style runtime acceptance (REQ-C-006) shows the rows in the Holonight and Fusion styles without clipped columns.

### REQ-F-027: Origin label [U]
**Statement:** The presentation model shall label an origin Packages as `Packages`. When the entry's uid differs from the current user's uid, it shall add the user name resolved from that uid. It shall label origin pacman as `pacman` and origin unknown as `unknown`.
**Acceptance criteria:**
- A GTest with an entry whose uid equals the current uid yields the label `Packages`, with no user name.
- A GTest with an entry whose uid is 1001, and a resolver seam that maps 1001 to `alice`, yields a label that contains `alice`.
- GTests with pacman and unknown origins yield the labels `pacman` and `unknown`.

### REQ-F-028: Summary text [U]
**Statement:** The presentation model shall build the summary of a transaction from the counts of its actions, showing each nonzero count as `<n> <kind>` and separating the parts with ` · `.
**Acceptance criteria:**
- A GTest with 12 upgraded, 1 installed and 2 removed actions yields exactly `12 upgraded · 1 installed · 2 removed`.
- A GTest with one removed action yields exactly `1 removed`.

### REQ-F-029: Filter by package name [E]
**Statement:** When the user changes the filter text, the History table shall show the transactions that have at least one action whose package name matches the filter text.
**Acceptance criteria:**
- A GTest on the filter proxy with transaction T1 touching `linux` and `linux-headers` and transaction T2 touching `git` only: filter `linux` shows T1 only; filter `git` shows T2 only; empty filter shows both.
- The match is a case-insensitive substring match on the package name, the same rule `InstalledPackagesFilterModel` uses: filter `LINUX` shows T1.

### REQ-F-030: Detail panel [E]
**Statement:** When the user selects a transaction, the detail panel shall show its per-package actions with old and new versions, the pre-transaction and post-transaction hooks that ran, its warning and error lines, and, for origin pacman, the command line.
**Acceptance criteria:**
- A QML test selects a fixture transaction with two actions, two hooks and one warning. The panel shows an arrow form `old → new` for the upgraded action, the hook names under separate "before" (pre-transaction) and "after" (post-transaction) sections, and the warning text. A section with no hooks is not shown.
- For a fixture pacman-origin transaction the panel shows the command line. For a Packages-origin transaction it shows no command-line element.

### REQ-F-031: Refresh after transaction outcome and manual refresh [E]
**Statement:** When the app receives a transaction outcome from its own transaction service, or the user invokes the History refresh action, the History page shall request an incremental refresh through the same path.
**Acceptance criteria:**
- A GTest with a fake transaction service emitting one outcome records one refresh request, and the counting seam shows an incremental read and not a full parse.
- A QML test invokes the refresh action of the History page (through its handler, with no pointer input) and records one refresh request on the same path.

### REQ-F-032: History page changes no packages [U]
**Statement:** The History page shall offer no action that changes a package, and no action that requests a transaction from the helper.
**Acceptance criteria:**
- A QML test enumerates every button and action on the History page and asserts that none is bound to a helper client call or a transaction request signal.
- A policy fixture for `task layering-check` fails when the History view-model source includes a helper client header.

### 5. Architecture and compatibility

### REQ-F-033: Port in the domain, adapter without Qt or libalpm [U]
**Statement:** The `HistorySource` port shall be declared in `src/domain` without Qt or libalpm headers, and `PacmanLogHistorySource` shall be implemented in `src/backends` without Qt or libalpm, using only the standard library and the shared pacman.conf parser.
**Acceptance criteria:**
- `task layering-check` passes. A fixture that adds `#include <QString>` to the history adapter makes it fail.
- The history adapter's source and header files include no Qt or libalpm header. The layering check fails on a fixture that adds `#include <alpm.h>` to them. (The `holonight_packages_backends` target itself links Qt6::Core and libalpm for other adapters; this requirement is about the history files, not the target.)

### REQ-F-034: View-model depends only on the port [U]
**Statement:** The application's History view-model shall obtain history only through the `HistorySource` port.
**Acceptance criteria:**
- A GTest of the view-model with a fake `HistorySource` passes without linking `PacmanLogHistorySource`.
- A policy fixture for `task layering-check` fails when the view-model includes the adapter header.

### REQ-F-035: holonight-packaged and its D-Bus API are unchanged [U]
**Statement:** The `holonight-packaged` service shall not change, and the D-Bus API shall gain no history method or property.
**Acceptance criteria:**
- A diff of the `holonight-packaged` introspection XML against the base commit is empty, and a search for `History` in the D-Bus XML returns nothing.
- The existing `holonight-packaged` tests pass unchanged.

### REQ-F-036: The log is only read [U]
**Statement:** The source shall open the pacman log read-only and shall not write, truncate, rename or remove it.
**Acceptance criteria:**
- The SHA-256 and modification time of a fixture log are the same before and after a full parse, an incremental refresh, and a parse with the lock file present or absent.
- A counting seam records the open flags, which are read-only.

## Non-functional requirements

### REQ-NF-001: First full parse budget [E]
**Statement:** When the first full parse of a pacman log of 5 MiB and 60,000 lines runs, the source shall finish it in under 500 ms on the developer machine, on a worker thread, and not on the UI thread.
**Acceptance criteria:**
- A benchmark-style GTest, run in a Release build, generates a fixture with a fixed seed of about 5 MiB, 60,000 lines and 1,178 transactions in a temporary directory. The median of five timed full parses is below 500 ms.
- The same measurement is run once on the real pacman log of the reference machine (4.5 MiB, 53,000 lines, 1,178 transactions) and its time is recorded in the change description.

### REQ-NF-002: Incremental refresh budget [E]
**Statement:** When an incremental refresh runs after one transaction has been appended to a log of the size in REQ-NF-001, the source shall finish it in under 20 ms.
**Acceptance criteria:**
- The benchmark of REQ-NF-001 runs twenty iterations of: append one transaction of about 1 KiB, then refresh. The median refresh time is below 20 ms.
- The same benchmark runs twenty refreshes with no new bytes; the median is below 20 ms.

### REQ-NF-003: UI stays responsive during a parse [U]
**Statement:** While a full parse of the REQ-NF-001 fixture runs, the application event loop shall keep processing events, with no event waiting more than 50 ms.
**Acceptance criteria:**
- A GTest starts the parse on the worker thread, posts a timer event to the main thread every 10 ms, and records the largest delay between the post and its delivery. The largest delay is below 50 ms.

## Constraints

### REQ-C-001: Layering and ownership [U]
**Statement:** The feature shall keep the ownership and dependency direction of `AGENTS.md`: the port in `src/domain`, the adapter in `src/backends`, the view-model in the application layer, and Qt-free domain and backend code.
**Acceptance criteria:**
- `task layering-check` passes with the new policy fixtures of REQ-F-033 and REQ-F-034.

### REQ-C-002: No package change in production code on the History path [U]
**Statement:** Production GUI code and the application layer shall not change packages on the History path, and the feature shall leave the helper request path as it is except for the tag of REQ-F-001.
**Acceptance criteria:**
- A policy fixture for `task layering-check` fails when a History source file calls a transaction or `alpm_trans_*` function.
- A grep in the History sources for helper client calls returns no match.

### REQ-C-003: Tests use fixture logs under a temporary directory [U]
**Statement:** Tests of the history source, view-model and page shall read only fixture logs and fixture `pacman.conf` files under a temporary directory, and shall not read `/var/log/pacman.log` or the system database directory.
**Acceptance criteria:**
- The test harness sets a guard that fails any test that opens the default log path.
- The test run passes on a machine where `/var/log/pacman.log` is absent.

### REQ-C-004: Helper tests use a fixture root and no pointer or focus input [U]
**Statement:** Tests of the helper tag shall run only against a fixture root under a temporary directory, with `file://` repositories, and no history test shall automate pointer or focus input.
**Acceptance criteria:**
- The REQ-F-001 to REQ-F-004 integration tests run with a fixture root. The test asserts that its root is under the temporary directory.
- A grep of the history test sources for pointer or focus automation (`QTest::mouse`, `QTest::keyClick` on focus, or external input tools) returns no match.

### REQ-C-005: No new persistent storage [U]
**Statement:** The feature shall add no persistent storage: no database, cache file, setting or file written by the app. Its only inputs are the pacman log and `pacman.conf`.
**Acceptance criteria:**
- A test that runs a full parse and an incremental refresh in a temporary directory shows the directory listing and file contents unchanged.
- A review of the change finds no new settings key, no new file path and no write to the pacman log.

### REQ-C-006: UI changes pass policy, runtime and QML checks [U]
**Statement:** Every UI change of this feature shall pass the policy fixtures, the dual-style runtime acceptance in the default Holonight style and in Fusion, and the existing QML tests.
**Acceptance criteria:**
- `task qml-lint` and the QML policy fixtures pass.
- The runtime acceptance for the History page passes in both styles, and the existing QML tests pass.

### REQ-C-007: Helper changes pass integration checks [E]
**Statement:** When the helper tag change is committed, the repository shall pass `task check:integration`.
**Acceptance criteria:**
- `task check:integration` is green on the commit that contains the REQ-F-001 change.

### REQ-C-008: Clean formatting and static analysis [U]
**Statement:** The C++ changes of this feature shall pass the pinned formatter check and clang-tidy with warnings as errors.
**Acceptance criteria:**
- `task format-check` and `task tidy` pass with no diagnostics.

### REQ-C-009: Focused GTest coverage [U]
**Statement:** Each parser, presentation and helper requirement shall have a focused GTest that exercises the behaviour it states.
**Acceptance criteria:**
- The traceability table below names at least one GTest for every REQ-F and REQ-NF that the GTest column lists, and each named test exists in the change.

## Verification

Traceability of requirements to verification. "GTest" is a unit or component test under `tests/`. "QML test" is a test of the QML page or sidebar. "Policy fixture" is a fixture checked by `task layering-check` or `task qml-lint`. "Runtime acceptance" is the dual-style Holonight and Fusion run. "Integration" is `task check:integration` on a fixture root.

| Requirement | Verification |
|---|---|
| REQ-F-001 | Integration (fixture root, call order) |
| REQ-F-002 | GTest (format); Integration (log line) |
| REQ-F-003 | Integration (refusal, log checksum) |
| REQ-F-004 | Integration (failed tag write) |
| REQ-F-005 | GTest (path resolution from fixture `pacman.conf`) |
| REQ-F-006 | GTest (parser fixtures) |
| REQ-F-007 | GTest (origin fixtures) |
| REQ-F-008 | GTest (action fixture, reference-log lines) |
| REQ-F-009 | GTest (hook attachment fixtures) |
| REQ-F-010 | GTest (warning and error attachment) |
| REQ-F-011 | GTest; QML test (detail panel) |
| REQ-F-012 | GTest |
| REQ-F-013 | GTest (lock file present, then removed) |
| REQ-F-014 | GTest (interrupted fixtures) |
| REQ-F-015 | GTest with `TZ` set |
| REQ-F-016 | GTest (malformed fixture) |
| REQ-F-017 | GTest (counting seam, startup); QML test (page open) |
| REQ-F-018 | GTest (append 1,000 plus one) |
| REQ-F-019 | GTest (truncate; rename over with new inode) |
| REQ-F-020 | GTest (partial line) |
| REQ-F-021 | GTest (missing, unreadable); QML test (empty state) |
| REQ-F-022 | GTest (thread recording) |
| REQ-F-023 | QML test (Sidebar); Policy fixture (page order) |
| REQ-F-024 | QML test (blocking seam) |
| REQ-F-025 | GTest (presentation model) |
| REQ-F-026 | QML test; Runtime acceptance (both styles) |
| REQ-F-027 | GTest (origin labels, resolver seam) |
| REQ-F-028 | GTest (summary text) |
| REQ-F-029 | GTest (filter proxy) |
| REQ-F-030 | QML test (detail panel) |
| REQ-F-031 | GTest (fake transaction service); QML test (refresh action) |
| REQ-F-032 | QML test (action enumeration); Policy fixture |
| REQ-F-033 | Policy fixture (`task layering-check`); CMake link check |
| REQ-F-034 | GTest (fake source); Policy fixture |
| REQ-F-035 | Diff check of the D-Bus XML; existing `holonight-packaged` tests |
| REQ-F-036 | GTest (checksum, mtime, open flags) |
| REQ-NF-001 | Benchmark-style GTest (Release); recorded measurement on the reference log |
| REQ-NF-002 | Benchmark-style GTest (append and refresh) |
| REQ-NF-003 | GTest (event-latency probe) |
| REQ-C-001 | Policy fixture (`task layering-check`) |
| REQ-C-002 | Policy fixture; grep check |
| REQ-C-003 | Test guard; run without `/var/log/pacman.log` |
| REQ-C-004 | Integration (fixture root); grep check |
| REQ-C-005 | GTest (directory and file snapshot); review |
| REQ-C-006 | Policy fixture (`task qml-lint`); Runtime acceptance; QML tests |
| REQ-C-007 | Integration (`task check:integration`) |
| REQ-C-008 | `task format-check`; `task tidy` |
| REQ-C-009 | Review of the traceability table against the test list |

## Risks

- **R-1 Log growth against the 500 ms budget.** The pacman log is never rotated here; it is 4.5 MiB today. The first full parse cost grows with the file, so REQ-NF-001 may fail later. Paging is the fallback (see Non-goals). REQ-NF-001 is measured on the reference log each time the budget is reviewed.
- **R-2 yay attribution.** yay runs `pacman -U` for AUR packages, so their transactions show origin `pacman`. This is a known limit, not corrected by this feature.
- **R-3 Hook attachment heuristic.** REQ-F-009 attaches hook lines by position. Pre-transaction hooks of a transaction that has no Running line (origin unknown) are attached to the previous transaction as post-transaction hooks. Tests cover the documented cases only.
- **R-8 Orphan HOLONIGHT line.** The tag is written before `alpm_trans_commit`, which also downloads. A download failure or a cancel during the download leaves a HOLONIGHT line with no transaction block after it. It has no effect while the next transaction has its own Running line, which pacman and the helper always write.
- **R-4 Status race at the lock.** In progress and interrupted depend on the lock file at read time. A transaction that finishes between the log read and the lock check can be shown with the wrong status until the next refresh (REQ-F-013 covers the correction).
- **R-5 Silent unknown origin.** A failed tag write (REQ-F-004) makes the origin unknown with no warning to the user. This follows the decided behaviour.
- **R-6 Log rotation.** Only a shrink or an inode change triggers a full reparse (REQ-F-019). Earlier history in a rotated file is not shown.
- **R-7 Local time ambiguity.** The legacy format has no offset. Around daylight-saving transitions the local-time reading can be ambiguous.

## Open questions

These do not change any requirement. Design must settle them and record the answer in its own document.

- **OQ-H1 Database directory and lock file.** REQ-F-013 needs the lock file `db.lck` in the database directory. The shared `pacman.conf` parser (`PacmanConfig`) does not expose `DBPath`. Design either adds `DBPath` to the shared parser, which changes `holonight_packages_pacman_config`, or uses the default `/var/lib/pacman/`. The first choice is preferred.
- **OQ-H2 Unreadable `pacman.conf`.** No decision is recorded. This SPEC assumes that the source returns the same explained empty state as REQ-F-021, with the parser's error text as the reason.
- **OQ-H3 Summary order.** REQ-F-028 gives the order `upgraded · installed · removed` from the example. The position of `downgraded` and `reinstalled` is not decided.
- **OQ-H4 Filter match rule.** Resolved: case-insensitive substring, matching `InstalledPackagesFilterModel` (REQ-F-029).
- **OQ-H5 Label for another user.** REQ-F-027 requires the resolved user name when the uid differs. The exact label format (for example `Packages · alice`) is for Design.
- **OQ-H6 Action line patterns.** REQ-F-008 needs the exact ALPM action line formats. Design pins them against the reference log.
- **OQ-H7 Existing placeholder.** `qml/workspace/Sidebar.qml` already contains a disabled `sidebarHistoryNav`. REQ-F-023 makes it enabled and removes `enabled: false`.

## Revision history

- 2026-11-23: amended after Design: tag after the final re-validation and once per libalpm commit (REQ-F-001 to REQ-F-003); pre- and post-transaction hooks (REQ-F-009, REQ-F-030); loading state on the first parse only (REQ-F-024); reinstalled version, Settings footer, refresh benchmark and consumed-offset accessor clarified (REQ-F-008, REQ-F-020, REQ-F-023, REQ-NF-002); R-8 added. OQ-H1 to OQ-H7 resolved in DESIGN section 12.
- 2026-11-23: first draft. Decisions of the design session encoded as given: log source, helper tag, parsing rules, incremental reads, GUI scope, non-functional limits, constraints, and non-goals.
