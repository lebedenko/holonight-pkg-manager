# Transaction history from the pacman log — DESIGN (HoloNight Packages)

Status: Draft, 2026-11-23. This design implements `docs/sdd/transaction-history/SPEC.md` (36 REQ-F, 3 REQ-NF, 9 REQ-C). It reuses the shared `pacman.conf` parser, the `TransactionSeams` test hooks, the `ModelListRefresh` path and the Installed page patterns. It adds no storage, no D-Bus method and no change to `holonight-packaged`.

## 1. Intent in one paragraph

The helper writes one `[HOLONIGHT] Running '…' (uid N)` line through `alpm_logaction` immediately before it commits a transaction. The History page reads the pacman log through a Qt-free `HistorySource` port. The adapter keeps a small carry-over parse state between reads, so a refresh reads only the bytes after the consumed offset. The first read happens when the page is first opened, on a `QtConcurrent` worker. The result is a delta (`reset`, `drop_last`, `append`) that a reversed-index list model applies. A filter proxy and a QML page show the rows and the detail panel.

## 2. Findings that shape the design

These come from reading the code and the reference log. Each one changes something the SPEC does not state.

| # | Finding | Consequence |
|---|---------|-------------|
| F-1 | `PacmanConfig` has `log_file` but no `DBPath`, and `PacmanSettings` (the helper) must not carry a database path (`pacman_settings.h`, tested by `PacmanSettings.IgnoresPathsAndExternalDownloadCommands`). | A separate `parsePacmanLogLocations` in the shared parser (D-6, OQ-H1). |
| F-2 | `ExecutionDriver::attempt()` calls `PlanResolver::resolve(Execute)`, which runs `alpm_trans_init` and `alpm_trans_prepare`, then `samePlan`, then `commit()`. The re-resolution is the last plan validation, and it runs after trans init. A tag written before trans init would stay behind when the re-resolution refuses with `PlanChanged`, `Busy` or `NeedsDecision`. | The tag goes between `samePlan` and `alpm_trans_commit` (section 7, D-13). REQ-F-001's call order is amended (section 14, item 1). |
| F-3 | `UpgradeDriver` commits up to two libalpm transactions (`keyringStep`, then `mainStep`), each through `commitStep()`. Each one writes its own `[ALPM] transaction started`. REQ-F-007 gives origin `unknown` to a start line that has another start line between it and its Running line. | One tag per `commitStep()` (section 14, item 2). |
| F-4 | `PreTransaction` hooks log `[ALPM] running '….hook'...` between the Running line and `transaction started` (354 of 1,178 transactions on the reference log). | They attach to the following transaction as pre-transaction hooks (amended REQ-F-009). The tag must precede them, so it is written before `alpm_trans_commit`. |
| F-5 | `PlanService` already knows the caller uid (`active_->owner.uid`, from `DBusCallerIdentity::uidOf`), but `ExecutionContext` does not carry it. | Add `caller_uid` to `ExecutionContext` (D-14). |
| F-6 | `PackageListRefresh` has two methods and `TransactionFlow::requestRefresh()` calls exactly one of them per outcome. `ModelListRefresh` already holds single-flight repeat logic. | One new target, `setHistoryTarget(HistoryModel*)`, called from both methods. No new port (D-11). |
| F-7 | `WorkspaceWindow.qml` puts every page in a `StackLayout`, so every page is instantiated at startup. `Component.onCompleted` cannot mean "opened". | The page has an `active` property bound to `currentPage === "history"`; `onActiveChanged` calls `ensureLoaded()` (D-11). |
| F-8 | The `release` preset has `BUILD_TESTS=OFF`. `task test` runs Debug. | The benchmark is its own executable, built in `build/bench` in Release (section 10.4). |
| F-9 | `test_holonight_packages_checks` is libalpm-free and has a `no_libalpm` ctest. `holonight_packages_backends` links libalpm. | Adapter tests go to `test_holonight_packages`; presentation tests go to `checks`; view-model tests go to a new executable that links no backends (REQ-F-034). |
| F-10 | `check-layering.py` rule C-001 bans `dbpath`, `databasePath`, `database_path` and `alpm_*` in `src/domain`, `src/application`, `apps`, `qml`. Rule C-005 bans `alpm_trans_*` outside `src/transaction` and `tests/policy`. | The port and the view-model never name the database path. REQ-C-002 is already enforced by C-005; only a fixture case is added. |
| F-11 | `Sidebar.qml` has a second `HnNavigationDelegate`, `sidebarSettingsNav`, in a separate footer after the spacer. | "Last page entry" (REQ-F-023) means last in the inner page column. |
| F-12 | `alpm_logaction(handle, "HOLONIGHT", fmt, …)` writes the timestamp itself in the format of the `[ALPM]` lines and returns 0 or -1. The format must end with `\n`. The handle's log file is `PacmanConfig::log_file` (`AlpmSession::initialize`). | The tag uses the same file and timestamp format as every other line. |

## 3. Components and module placement

### 3.1 Diagram and dependency direction

```
 holonight-packages (GUI)                                          holonight-package-helper (root)
 +----------------------------------------------+                  +--------------------------------------+
 | qml/history  HistoryView, HistoryTable,      |                  | apps/helper  HelperService           |
 |   HistoryRow, HistoryDetailPanel, ...        |                  |   -> PlanService (src/application)   |
 |        |                                     |                  |        sets ExecutionContext.caller_uid
 | apps/packages/app                            |                  | src/transaction                      |
 |   HistoryModel (QAbstractListModel)          |                  |   history_tag.{h,cpp} (format, write)|
 |   HistoryFilterModel (QSortFilterProxyModel) |                  |   ExecutionDriver::commit()          |
 |   ModelListRefresh.setHistoryTarget()        |                  |   UpgradeDriver::commitStep()        |
 |        |  uses                               |                  |          |                           |
 | src/application  history_presentation        |                  |          v alpm_logaction            |
 |   (labels, summary, local day; Qt-free)      |                  +----------+---------------------------+
 |        |  uses                               |                             |  appends one line
 | src/domain  history_source.h  (port + types) |                             v
 |        ^  implements                         |                      /var/log/pacman.log  (LogFile)
 | src/backends  PacmanLogHistorySource         |  reads (read-only)          ^
 |   pacman_log_parser, pacman_log_timestamp ---+-----------------------------+
 |   -> holonight_packages_pacman_config        |
 +----------------------------------------------+
```

Dependency direction: `qml` -> `apps/packages/app` -> `src/application` -> `src/domain` <- `src/backends`. `apps/packages/app/PackagesApplication.cpp` (the composition root) is the only place that names `PacmanLogHistorySource`. `src/backends` history files include the standard library, `holonight_packages_domain/history_source.h` and `pacman_config.h`. `src/transaction` stays Qt-free; it gets one new file.

### 3.2 New and changed files

| Path | Owner target | New/changed | Content |
|------|--------------|-------------|---------|
| `src/domain/include/holonight_packages_domain/history_source.h` | `holonight_packages_domain` | new | `HistoryEntry` and parts, `HistoryReadMode`, `HistoryReadResult`, `HistorySource` |
| `src/domain/src/history_source.cpp` | same | new | out-of-line `HistorySource::~HistorySource()` (as `package_source.cpp`) |
| `src/domain/include/holonight_packages_domain/transaction_engine.h` | same | changed | `ExecutionContext::caller_uid` |
| `src/backends/src/pacman_config.{h,cpp}` | `holonight_packages_pacman_config` | changed | `PacmanLogLocations` + `parsePacmanLogLocations` (`LogFile`, `DBPath`), sharing the `[options]` scanner with `parsePacmanConfig`; `PacmanConfig` unchanged |
| `src/backends/src/pacman_log_timestamp.{h,cpp}` | `holonight_packages_backends` | new | pure timestamp parse |
| `src/backends/src/pacman_log_parser.{h,cpp}` | same | new | pure line parser with carry-over state |
| `src/backends/include/holonight_packages_backends/pacman_log_history_source.h` | same | new | adapter, options, I/O seam |
| `src/backends/src/pacman_log_history_source.cpp` | same | new | adapter |
| `src/backends/CMakeLists.txt` | same | changed | add the four `.cpp` and the header |
| `src/application/include/holonight_packages_application/history_presentation.h` + `src/history_presentation.cpp` | `holonight_packages_application` | new | labels, summary, local day, time label |
| `src/application/src/plan_service.cpp` | same | changed | `context.caller_uid = active_->owner.uid` in `startRunning()` |
| `src/transaction/src/history_tag.{h,cpp}` | `holonight_packages_transaction` | new | `formatHistoryTag`, `HistoryTagWriter` |
| `src/transaction/src/execution_driver.{h,cpp}` | same | changed | one call in `commit()`, `tag_written_` flag |
| `src/transaction/src/upgrade_driver.{h,cpp}` | same | changed | one call in `commitStep()` |
| `apps/packages/app/HistoryModel.{h,cpp}` | `holonight-packages` | new | list model and async load |
| `apps/packages/app/HistoryFilterModel.{h,cpp}` | same | new | name filter and selection |
| `apps/packages/app/ModelListRefresh.{h,cpp}` | same | changed | `setHistoryTarget` |
| `apps/packages/app/PackagesApplication.{h,cpp}` | same | changed | construct source and model; `historyModel` initial property |
| `apps/packages/CMakeLists.txt` | same | changed | add sources; add both headers to `qt_add_qml_module SOURCES` (QML_ELEMENT) |
| `qml/history/HistoryView.qml`, `HistoryToolbar.qml`, `HistoryTable.qml`, `HistoryColumns.qml`, `HistoryRow.qml`, `HistoryDayHeader.qml`, `HistoryDetailPanel.qml`, `HistoryDetailContent.qml` | QML module `HolonightPackages` | new | page (picked up by the `GLOB_RECURSE` on `qml/*.qml`) |
| `qml/workspace/Sidebar.qml`, `WorkspaceWindow.qml` | same | changed | enable entry, page index 3 |
| `scripts/check-layering.py`, `tests/policy/test_layering_policy.py` | policy | changed | rules Y-001..Y-003, H-014 (section 11) |
| `Taskfile.yml` | tooling | changed | `bench:history` |
| tests | see section 10 | new | |

`apps/helper`, `apps/packaged`, the helper D-Bus XML, polkit policy and systemd unit do not change (REQ-F-035, REQ-C-002).

## 4. Data model (`history_source.h`, namespace `holonight_packages_domain`)

```cpp
enum class HistoryActionKind : std::uint8_t { Installed, Upgraded, Downgraded, Reinstalled, Removed };
struct HistoryAction {                       // one ALPM action line
  HistoryActionKind kind;
  std::string name;
  std::string old_version;                   // upgraded, downgraded, removed; else empty
  std::string new_version;                   // installed, upgraded, downgraded, reinstalled; else empty
  bool operator==(const HistoryAction&) const = default;
};
enum class HistoryOriginKind : std::uint8_t { Packages, Pacman, Unknown };
struct HistoryOrigin {
  HistoryOriginKind kind = HistoryOriginKind::Unknown;
  std::optional<std::uint32_t> uid;          // Packages only
  std::string command;                       // Pacman only: the text between the quotes of the Running line
  bool operator==(const HistoryOrigin&) const = default;
};
enum class HistoryStatus : std::uint8_t { Completed, Interrupted, InProgress };
enum class HistorySeverity : std::uint8_t { Warning, Error };
struct HistoryMessage { HistorySeverity severity; std::string text; /* == */ };   // text after "warning: " / "error: "
struct HistoryEntry {
  std::chrono::system_clock::time_point started;   // start line; one-second resolution
  HistoryOrigin origin;
  std::vector<HistoryAction> actions;              // file order
  std::vector<std::string> pre_hooks;              // "<name>.hook" between the Running line and the start line
  std::vector<std::string> hooks;                  // "<name>.hook" after the completed line (post-transaction)
  std::vector<HistoryMessage> messages;            // warnings and errors, never [ALPM-SCRIPTLET]
  HistoryStatus status = HistoryStatus::Interrupted;
  bool operator==(const HistoryEntry&) const = default;
};

enum class HistoryReadMode : std::uint8_t { Full, Incremental };

struct HistoryReadResult {
  // What the consumer must do to its list (kept in file order, oldest first):
  bool reset = false;                 // discard everything, then append
  std::size_t drop_last = 0;          // 0 or 1: discard the last entry first (it changed); ignored when reset
  std::vector<HistoryEntry> append;
  // Non-empty only when the log cannot be shown: missing, unreadable, or pacman.conf unreadable. `reset` is true and
  // `append` is empty then. Empty reason with zero entries means a readable log without transactions.
  std::string reason;
};

class HistorySource {
 public:
  /* special members as PackageSource; virtual ~HistorySource() in history_source.cpp */
  // Blocking; intended for a worker thread. Never throws for I/O problems. Full re-reads the whole log and resets the
  // state; Incremental reads only the bytes after the consumed offset (and acts as Full when there is no state, the
  // log path changed, the inode changed or the file shrank). Not const: the source keeps the carry-over state.
  [[nodiscard]] virtual HistoryReadResult read(HistoryReadMode mode) = 0;
};
```

An entry carries no stored "open" flag. A parsed entry is `Completed` (completed line seen), `Interrupted` (closed by the next start line), or open; the adapter turns "open" into `InProgress` or `Interrupted` at every read from the lock probe (D-5).

## 5. Interfaces

### 5.1 Shared pacman.conf parser (OQ-H1)

`PacmanConfig` is **not** changed: the helper reads it through `PacmanSettings`, whose contract is that it carries no database path. The shared parser gains `struct PacmanLogLocations { log_file; db_path; }` and `parsePacmanLogLocations(path)`, which uses the same `[options]` scanner, comment and error rules as `parsePacmanConfig`. Only the history source calls it. The adapter applies the defaults `kDefaultPacmanLogPath` (`/var/log/pacman.log`) and `kDefaultPacmanDatabasePath` (`/var/lib/pacman`, `pacman_defaults.h`) and takes the lock file as `db_path / "db.lck"`. (Changed in implementation, T-001/T-009: adding `db_path` to `PacmanConfig` broke the `PacmanSettings` invariant.)

### 5.2 Timestamp (`pacman_log_timestamp.h`, pure)

```cpp
[[nodiscard]] bool hasTimestampShape(std::string_view inside_brackets) noexcept;
[[nodiscard]] std::optional<std::chrono::system_clock::time_point> parseTimestamp(std::string_view inside_brackets);
```
Current format `2026-11-23T10:05:18+0200` (24 chars): days-from-civil arithmetic, then subtract the offset; no locale, no `mktime`. Legacy format `2019-01-01 12:00` (16 chars): `std::mktime` with `tm_isdst = -1`, so the process `TZ` applies (REQ-F-015). Every recognized record requires valid UTF-8 and a valid timestamp. Both formats check the resulting instant against the range of `system_clock` before conversion.

### 5.3 Line parser (`pacman_log_parser.h`, pure, no files, no clock)

```cpp
struct LogParseState {
  std::optional<HistoryEntry> last;                 // the entry that can still change
  bool last_open = false;                           // no completed line and no later start line yet
  bool attach_open = false;                         // no boundary line since last's start line
  std::optional<HistoryOrigin> pending_origin;      // nearest Running line since the last start line
  std::vector<std::string> pending_hooks;           // hook lines since that Running line, before the next start line
  std::vector<HistoryMessage> pending_messages;     // warning/error lines in the same span (they go to the next entry)
  std::size_t entries_finished = 0;                 // entries moved to `finished` since the state was created
};

// `lines` holds only complete lines (every line ends in '\n'). Entries that can no longer change are appended to
// `finished` in file order; the entry that can still change stays in `state.last`. Malformed lines are skipped.
void parseLines(std::string_view lines, LogParseState& state, std::vector<HistoryEntry>& finished);
```

Pinned line grammar (OQ-H6). Every line is `[<ts>] [<tag>] <text>\n`. `<ts>` is the current form or the legacy form of 5.2. Tags other than `ALPM`, `PACMAN`, `HOLONIGHT` are skipped; `ALPM-SCRIPTLET` is skipped first, so scriptlet text never reaches an entry (REQ-F-011). Real examples are from this machine's `/var/log/pacman.log` (reference log: 53,473 lines, 1,178 start and 1,178 completed lines, no legacy timestamps):

| Element | Tag | Text pattern (reference regex for test oracles) | Real example |
|---|---|---|---|
| start | ALPM | exactly `transaction started` | `[2025-02-02T20:39:20+0200] [ALPM] transaction started` |
| completed | ALPM | exactly `transaction completed` | `[2025-02-02T20:39:20+0200] [ALPM] transaction completed` |
| installed | ALPM | `^installed (\S+) \((\S+)\)$` | `[2025-02-02T17:11:44+0000] [ALPM] installed iana-etc (20241206-1)` |
| upgraded | ALPM | `^upgraded (\S+) \((\S+) -> (\S+)\)$` | `[2025-02-03T00:16:54+0200] [ALPM] upgraded noto-fonts-cjk (20230817-2 -> 20240730-1)` |
| downgraded | ALPM | `^downgraded (\S+) \((\S+) -> (\S+)\)$` | `[2025-08-12T15:54:32+0300] [ALPM] downgraded uwsm (0.23.1-1 -> 0.23.0-1)` |
| reinstalled | ALPM | `^reinstalled (\S+) \((\S+)\)$` | `[2025-02-02T17:19:20+0000] [ALPM] reinstalled systemd (257.2-2)` |
| removed | ALPM | `^removed (\S+) \((\S+)\)$` | `[2025-02-02T21:50:14+0200] [ALPM] removed nvidia (565.77-12)` |
| hook | ALPM | `^running '([^']+)'\.\.\.$` | `[2025-02-02T17:11:51+0000] [ALPM] running '20-systemd-sysusers.hook'...` |
| warning | ALPM, PACMAN | `^warning: (.*)$` | `[2025-03-01T21:32:08+0200] [ALPM] warning: /etc/pacman.conf installed as /etc/pacman.conf.pacnew` |
| error | ALPM, PACMAN | `^error: (.*)$` | none exists as `[ALPM] error:` on this machine; libalpm writes the same shape as the warning line, so the fixture is hand-written |
| Running (pacman) | PACMAN | `^Running '(.*)'$`, command = group 1 | `[2025-02-02T20:39:17+0200] [PACMAN] Running 'pacman -S git'` |
| Running (app) | HOLONIGHT | `^Running '(install\|remove\|upgrade)( [a-z0-9@._+ -]+)?' \(uid ([0-9]+)\)$` | none yet (format from REQ-F-002): `[HOLONIGHT] Running 'install foo bar' (uid 1000)` |
| scriptlet (skipped) | ALPM-SCRIPTLET | any | `[2025-02-02T17:11:45+0000] [ALPM-SCRIPTLET] Initializing machine ID from random generator.` |

A line whose text matches none of these (for example `[PACMAN] synchronizing package lists`) is skipped without effect. A known tag with a malformed text (an action line without the closing parenthesis, a HOLONIGHT line without `(uid N)`) is skipped. `reinstalled` stores the one version it gives in `new_version`. `removed` stores it in `old_version`.

Transitions (`attach_open` is true from a start line until the next boundary line):

| Line | Effect |
|---|---|
| Running line | `pending_origin` = origin from the line (replaces an earlier one); clear `pending_hooks` and `pending_messages` (an orphan Running line's hooks and messages are dropped); `attach_open = false`. |
| start | If `last` is open: status Interrupted. Move `last` to `finished`. New `last` with `started` from the line and `origin = pending_origin` or Unknown; `pre_hooks` = `pending_hooks`, `messages` = `pending_messages`; clear `pending_origin`, `pending_hooks` and `pending_messages`; `last_open = attach_open = true`. |
| completed | If `last` is open: status Completed, `last_open = false`. Otherwise ignored. |
| action | If `last_open`: append an action even if a new Running line has appeared. |
| hook | If `pending_origin` is set (a Running line since the last start): append to `pending_hooks` (pre-transaction). Else if `attach_open` and `last` exists and is not open: append to `last.hooks` (post-transaction). A hook inside an open block is not listed (libalpm writes hooks outside the started/completed lines). |
| warning, error | If `pending_origin` is set: append to `pending_messages`, which the next start line moves into the new entry (pre-transaction warnings such as `… is up to date -- reinstalling`). Else if `attach_open` and `last` exists: append to `last.messages`. Added in implementation (T-006), consistent with the amended REQ-F-009 boundaries. |

Complexity: one pass, no regex, no per-line allocation except for action, hook and message strings. A line is dispatched by the fixed-offset checks `[`, shape, `] [`, then the tag length and bytes (`ALPM` 4, `PACMAN` 6, `HOLONIGHT` 9, `ALPM-SCRIPTLET` 14), then `string_view::starts_with` on the text.

### 5.4 Adapter (`pacman_log_history_source.h`, no Qt, no libalpm)

```cpp
struct HistoryFileIo {                                   // I/O seam (D-8); defaults call the POSIX functions
  std::function<int(const char* path, int flags)> open = [](auto p, int f) { return ::open(p, f); };
  std::function<ssize_t(int fd, void* buffer, std::size_t length, off_t offset)> pread = /* ::pread */;
  std::function<int(int fd)> close = /* ::close */;
};
using LockProbe = std::function<bool(const std::filesystem::path& lock_file)>;   // default: stat() succeeds

struct PacmanLogHistorySourceOptions {
  std::filesystem::path pacman_conf_path;                // composition root passes kDefaultPacmanConfPath
  HistoryFileIo io;                                      // tests replace it to count bytes, flags and threads
  LockProbe lock_probe;                                  // tests replace it; empty means the default
};

class PacmanLogHistorySource final : public holonight_packages_domain::HistorySource {
 public:
  explicit PacmanLogHistorySource(PacmanLogHistorySourceOptions options);
  [[nodiscard]] holonight_packages_domain::HistoryReadResult read(holonight_packages_domain::HistoryReadMode) override;
  // Test introspection (REQ-F-005, REQ-F-020); not part of the port.
  [[nodiscard]] std::filesystem::path resolvedLogPath() const;
  [[nodiscard]] std::uint64_t consumedOffset() const;
 private:
  /* mutex, options_, LogParseState parse_, std::filesystem::path log_path_, std::uint64_t inode_, offset_,
     bool have_state_, std::optional<HistoryEntry> delivered_last_ */
};
```

`read(mode)` under one `std::mutex`:

1. `parsePacmanLogLocations(options_.pacman_conf_path)`. Follow includes in glob order with the current section, using the first definition of each path. On error: clear the state and return `{reset=true, reason = error text}` (OQ-H2). Compute `log_path` (`log_file`, or `/var/log/pacman.log` when empty; REQ-F-005) and `lock_file` (`db_path` or `/var/lib/pacman`, plus `db.lck`).
2. `full = mode == Full || !have_state_ || log_path != log_path_`.
3. `fd = io.open(log_path, O_RDONLY | O_CLOEXEC)`. On failure: clear the state and return `{reset=true, reason}`: `"The pacman log <path> does not exist."` for `ENOENT`, otherwise `"Cannot read the pacman log <path>: <strerror>."` (REQ-F-021). `fstat(fd)` gives `inode` and `size`.
4. If `!full` and (`inode != inode_` or `size < offset_`): `full = true` (REQ-F-019).
5. If `full`: reset `parse_`, `offset_ = 0`, `delivered_last_` empty.
6. Read `[offset_, size)` with `io.pread` in bounded chunks. Buffer at most 1 MiB of record content across chunks. Discard an oversized record through its terminating newline. Advance the consumed offset only at a newline, so the next refresh starts at the beginning of any unfinished record (REQ-F-020). No `pread` call is made when `size == offset_` (REQ-F-018 byte count).
7. Combine `finished` with `parse_.last`. Resolve the status of `parse_.last` when `parse_.last_open`: `lock_probe(lock_file) ? InProgress : Interrupted` (REQ-F-013, REQ-F-014). The probe runs after the file read (R-4).
8. Build the result. Full: `reset=true`, `append = combined`. Incremental: if `delivered_last_` exists, `combined[0]` is that entry in its new form; if it equals `*delivered_last_`, `append = combined[1..]`, else `drop_last = 1` and `append = combined`. Without `delivered_last_`, `append = combined`.
9. `delivered_last_ = combined.back()`, `inode_`, `offset_`, `log_path_`, `have_state_ = true`; close the descriptor.

The adapter never opens the file for write and never touches the lock file except through the probe (REQ-F-036). It holds no cache on disk (REQ-C-005).

### 5.5 Presentation (`history_presentation.h`, `src/application`, std only)

```cpp
using UserNameResolver = std::function<std::optional<std::string>(std::uint32_t uid)>;
[[nodiscard]] std::optional<std::string> systemUserName(std::uint32_t uid);          // getpwuid_r; the default resolver
[[nodiscard]] std::string originLabel(const HistoryOrigin&, std::uint32_t current_uid, const UserNameResolver&);
[[nodiscard]] std::string summaryText(std::span<const HistoryAction>);
[[nodiscard]] std::string_view statusLabel(HistoryStatus);                           // "Completed" | "Interrupted" | "In progress"
[[nodiscard]] std::string_view actionKindLabel(HistoryActionKind);
struct LocalDay { int year; int month; int day; auto operator<=>(const LocalDay&) const = default; };
[[nodiscard]] LocalDay localDay(std::chrono::system_clock::time_point);              // localtime_r
[[nodiscard]] std::string timeLabel(std::chrono::system_clock::time_point);          // "HH:MM", local
```

Rules: Packages with `uid == current_uid` gives `Packages`; otherwise `Packages · <username>`, or `Packages · uid N` when the resolver returns nothing (OQ-H5); a Packages origin without a uid gives `Packages`. `pacman` and `unknown` are literal. The summary lists upgraded, installed, downgraded, reinstalled, removed (OQ-H3) as `<n> <kind>` joined by ` · `, nonzero counts only. An entry with no actions has an empty summary, and the row shows `—`.

### 5.6 View-model `HistoryModel` (`apps/packages/app`, `QML_NAMED_ELEMENT(HistoryModel)`, `QML_UNCREATABLE`)

Constructor: `HistoryModel(std::shared_ptr<HistorySource>, HistoryModelOptions = {}, QObject* parent = nullptr)`; the options hold `current_uid` (default `getuid()`) and a `UserNameResolver` (default `systemUserName`). The constructor starts nothing (REQ-F-017).

- `QAbstractListModel`, one row per entry, row 0 newest: row `r` is `entries_[N - 1 - r]` (D-10).
- Q_PROPERTYs (all `NOTIFY stateChanged`): `bool loading`, `bool loaded` (a read finished at least once), `QString emptyReason`, `int entryCount`.
- `Q_INVOKABLE void ensureLoaded()`: the first call starts a `Full` read; later calls do nothing.
- `Q_INVOKABLE void refresh()`: starts an `Incremental` read; when a read runs, sets `refresh_pending_` (one repeat after it ends); before the first load it acts as `ensureLoaded()`.
- `void refreshIfLoaded()`: what `ModelListRefresh` calls; does nothing before the first `ensureLoaded()`, otherwise `refresh()`.
- `Q_INVOKABLE static QString formatDay(const QString& day_key)`: locale long date for a section header.
- Roles: `SequenceRole` (`sequence`, quint32, file-order index, stable across incremental reads), `StartedAtRole` (`startedAt`, QDateTime), `DayKeyRole` (`dayKey`, `yyyy-MM-dd` local), `TimeLabelRole` (`timeLabel`), `OriginKindRole` (`originKind`: `"packages"`, `"pacman"`, `"unknown"`), `OriginLabelRole` (`originLabel`), `CommandLineRole` (`commandLine`, non-empty only for pacman), `SummaryRole` (`summary`), `StatusKindRole` (`statusKind`: `"completed"`, `"interrupted"`, `"inProgress"`), `StatusLabelRole` (`statusLabel`), `ActionsRole` (`actions`: list of maps `kind`, `kindLabel`, `name`, `oldVersion`, `newVersion`, `versionText` = `old → new`, or the single version), `PreHooksRole` (`preHooks`, QStringList), `HooksRole` (`hooks`, QStringList, post-transaction), `MessagesRole` (`messages`: maps `severity` as `"warning"` or `"error"`, `text`), `PackageNamesRole` (`packageNames`, QStringList). All values are computed in `data()` from the stored `HistoryEntry`; no QML-facing copy is stored. String enums are used so QML never binds `Type.EnumValue` to a same-typed property (qmlcachegen note).
- Load: `QtConcurrent::run([source, mode] { return source->read(mode); })` in a `QFutureWatcher<HistoryReadResult>`, as in `InstalledPackagesModel::startLoading()`. The lambda catches `std::exception` and `...` and returns `{reset=true, reason="Reading the pacman log failed: <what>"}`. `onReadFinished()` applies the result: `reset` gives `beginResetModel`/`endResetModel`; otherwise `drop_last` gives `beginRemoveRows(0,0)` and the appended entries give `beginInsertRows(0, k-1)`, newest at row 0. Then it sets `emptyReason`, clears `loading`, emits `stateChanged`, and starts the pending repeat once.

### 5.7 `HistoryFilterModel` (`QSortFilterProxyModel`, `QML_ELEMENT`)

The pattern of `InstalledPackagesFilterModel`: `setDynamicSortFilter(true)`, no sorting, `beginFilterChange()`/`endFilterChange(Direction::Rows)` around `setSearchText`, a `filter_change_in_progress_` guard and `reconcileCurrentRow()` on reset, layout, insert, remove and data changes.

- `QString searchText` (`searchTextChanged`). `filterAcceptsRow`: an empty text accepts all; otherwise it accepts a row when any string of `PackageNamesRole` `contains(searchText, Qt::CaseInsensitive)` (REQ-F-029, OQ-H4).
- `int currentRow` (writable) and `QVariantMap currentEntry` (read-only, `currentEntryChanged`), built like `get(row)` from every role. The selection identity is `SequenceRole`, so a refresh that inserts rows at the top keeps the selected entry. After a reset, with no identity, or with a lost identity, `currentRow` becomes row 0, or -1 when the proxy is empty.
- `Q_INVOKABLE QVariantMap get(int row) const`.

### 5.8 QML (`qml/history`)

Imports follow the project rules (`QtQuick.Controls as Controls`, `Holonight.Core`, `Holonight.Controls`, `HolonightPackages`). Rows use `HnListDelegate` (HnSelectableDelegate is internal). No component uses a bare `ColumnLayout` or `RowLayout` as a root that sits under `Layout.preferredWidth/Height`; `HistoryView`, `HistoryDetailPanel` and `HistoryTable` have an `Item`, `HnSurfaceFrame` or `Controls.ScrollView` root.

| File | Root | Role and `objectName`s |
|---|---|---|
| `HistoryView.qml` | `Item` | `objectName: "historyPage"`. Properties: `property var historyModel: null` (optional, like `transactionController`, so existing `WorkspaceWindow` tests stay valid), `property bool active: false`, `signal refreshRequested()`. `onActiveChanged: if (active && historyModel) historyModel.ensureLoaded()`. Holds `HistoryFilterModel { id: filterModel; sourceModel: root.historyModel }`. Keeps the toolbar fixed above a vertical viewport. Its table and detail grid uses at least the combined minimum height when stacked and switches to two columns at 1092 px. States: `HnLoadingState` `historyLoadingState` visible while `loading && !loaded` (first parse only, REQ-F-024); `HnEmptyState` `historyEmptyState` visible when `loaded && !loading && entryCount === 0`, with `descriptionText: emptyReason` (a generic title `No transactions recorded` when the reason is empty). No `Dialog` or `Popup` exists in the page. |
| `HistoryToolbar.qml` | `ColumnLayout` (inside the page column, not under a `Layout.preferred*` size) | Title `History`, `HnSearchField` `historySearchField` (`Search packages in history`), refresh `HnIconButton` `historyRefreshButton` whose `onClicked` calls `root.refreshRequested()`, which the page connects to `historyModel.refresh()`. Test code calls the handler through `historyModel.refresh()` or `refreshRequested()`, never by pointer input. |
| `HistoryTable.qml` | `Controls.ScrollView` | A `Loader { objectName: "historyTableLoader"; active: historyModel.loaded && count > 0 }` around a `ListView` `historyList` on `filterModel`, `section.property: "dayKey"`, `section.criteria: ViewSection.FullString`, `section.delegate: HistoryDayHeader` (REQ-F-025). During the first parse `loaded` is false, so no row exists; during a later refresh the loader stays active and rows and selection are kept (REQ-F-024). A `HnEmptyState` `historyNoMatchState` shows `No transactions match` for a filter without matches. |
| `HistoryColumns.qml` | `QtObject` | Column widths: time, origin, summary (fill), status. Same role as `PackageTableColumns`. |
| `HistoryRow.qml` | `HnListDelegate` | Required properties `timeLabel`, `originLabel`, `summary`, `statusLabel`, `statusKind`. Four `HnLabel`s with `objectName`s `historyRowTime`, `historyRowOrigin`, `historyRowSummary`, `historyRowStatus` (REQ-F-026). `onClicked` sets `filterModel.currentRow`. The proxy row drives highlighting, and the list synchronizes its current index after processing model changes. |
| `HistoryDayHeader.qml` | `Item` | Text `historyModel.formatDay(section)`. |
| `HistoryDetailPanel.qml` | `PackageDetailFrame` (reused from `qml/packages`) | `emptyTitle: qsTr("Select a transaction to view details")`, `hasSelection: filterModel.currentRow >= 0`; content `Loader`-instantiated, so it follows the existing empty-state pattern. |
| `HistoryDetailContent.qml` | `Item` wrapping a `Column` | Header (time, origin, status), `historyDetailCommand` (visible only when `originKind === "pacman"`, REQ-F-030), an actions section (`Repeater`, one `old → new` label per action), a `historyDetailPreHooks` section ("Hooks before") and a `historyDetailPostHooks` section ("Hooks after"), each hidden when empty (REQ-F-030), a messages section. It reads `currentEntry` only; there is no scriptlet text in the model (REQ-F-011). No button here is bound to the helper client or to a transaction signal (REQ-F-032). |

Implementation notes (phase 7): `historyEmptyState` shows when `loaded && entryCount === 0`, without `!loading`, so a refresh of an empty history keeps the empty state instead of a blank page. `HistoryFilterModel` gains a read-only `int count` (`countChanged`) for `historyNoMatchState`. `HistoryDetailSection.qml` (a `ColumnLayout` that hides itself when `count === 0`) holds the actions, hooks-before, hooks-after and messages sections. The command line sits in a `Loader`, so a non-pacman entry has no `historyDetailCommand` item at all.

`Sidebar.qml`: `sidebarHistoryNav` loses `enabled: false` and gains `checked: root.currentPage === "history"` and `onClicked: root.pageRequested("history")`. `WorkspaceWindow.qml`: `property var historyModel: null`, `pageIndex` gains `history: 3`, and a fourth `HistoryView { objectName: "historyPage"; historyModel: root.historyModel; active: root.currentPage === "history" }` is appended to the `StackLayout`.

### 5.9 Wiring (`PackagesApplication.cpp`, composition root)

```cpp
auto history_source = std::make_shared<holonight_packages_backends::PacmanLogHistorySource>(
    holonight_packages_backends::PacmanLogHistorySourceOptions{
        .pacman_conf_path = holonight_packages_backends::kDefaultPacmanConfPath});
history_model_ = std::make_unique<HistoryModel>(std::move(history_source));
list_refresh_->setHistoryTarget(history_model_.get());                    // after list_refresh_ is created
// setInitialProperties: {"historyModel", QVariant::fromValue(history_model_.get())}
```
`~PackagesApplication` resets `history_model_` after `view_` and `transaction_controller_`, before `list_refresh_`'s targets die (`list_refresh_` is reset before it, so its raw pointer is gone first). `PackagesApplication.h` declares `std::unique_ptr<HistoryModel> history_model_;` before `list_refresh_`.

## 6. Data flow

### 6.1 First open

1. The app starts. `HistoryModel` exists; `PacmanLogHistorySource` exists; no file is opened (REQ-F-017).
2. The user selects History. `Sidebar` emits `pageRequested("history")`; `currentPage` changes; `HistoryView.active` becomes true; `ensureLoaded()` runs once.
3. `loading = true`; `loaded` is still false, so the table loader is inactive and the loading state shows (REQ-F-024). A `QtConcurrent` worker runs `source->read(Full)` (REQ-F-022).
4. The worker returns a `HistoryReadResult`. `onReadFinished` (main thread) applies it with one model reset, sets `emptyReason`, `loading = false`. The table loader activates; `filterModel` reconciles `currentRow` to row 0.

### 6.2 Refresh after an own transaction, and manual refresh

`TransactionFlow` calls `refreshAfterOutcome()` or `refreshAfterUpgrade()` once per outcome. Both end in `ModelListRefresh::refreshHistory()`, which calls `history_->refreshIfLoaded()`. The refresh button calls `historyModel.refresh()`. Both paths reach `HistoryModel::refresh()` (REQ-F-031). A refresh before the first open does nothing; the later first open includes the transaction through its full parse. During a read, one repeat is queued.

### 6.3 What an incremental read does

`read(Incremental)` follows 5.4. In the normal case it reads the appended bytes, runs `parseLines` on them with the saved `LogParseState`, probes the lock, and returns `drop_last` 0 or 1 plus the new entries. The model inserts rows at the top.

### 6.4 The transaction that was open at the previous read (REQ-F-013, REQ-F-020)

The state after a read is: `parse_.last` holds the open entry (or the completed entry that can still receive hooks and warnings); `last_open`, `attach_open`, `pending_origin` and `pending_hooks` hold the position inside the block. A read that ends between a Running line and its start line keeps the collected pre-transaction hooks in `pending_hooks`; they reach the entry when the start line arrives. The next read continues from `offset_` with this state. Nothing before `offset_` is read again. Cases:

- More lines of the same block arrive (actions, then `completed`): `parseLines` mutates `parse_.last`. Its status becomes Completed. `combined[0]` differs from `delivered_last_`, so the result has `drop_last = 1` and `append` starts with the revised entry.
- Hooks, warnings or errors arrive after `completed`: `attach_open` is still true, so they are appended to `parse_.last`. The same `drop_last = 1` result follows.
- A new start line arrives: `last` moves to `finished` (Interrupted if it was open) and a new `last` starts.
- No bytes arrive: `parseLines` is not called, `parse_.last` is unchanged, the lock is probed and the status is resolved again. If it changed from `InProgress` to `Interrupted` (the lock file was removed), `combined[0] != delivered_last_` and the result is `drop_last = 1` with the one revised entry. If nothing changed, the result is empty (`append` empty, `drop_last` 0).
- A partial last line: it is not passed to the parser and `offset_` stays at its first byte. When `leted\n` is appended, the next read starts at that byte, reads the whole line and completes the entry (REQ-F-020).

### 6.5 Empty states

A missing log, an unreadable log, an unreadable `pacman.conf` or a worker exception each return `reset=true`, no entries and a reason. The page shows the reason in `historyEmptyState`. No dialog is created.

## 7. Helper change

### 7.1 Where the caller uid comes from

`DBusCallerIdentity::uidOf` (apps/helper) gives the uid of the bus connection; `PlanService::identify()` builds `CallerIdentity`, and `Execute` stores it as `active_->owner`. `PlanService::startRunning()` builds the `ExecutionContext`; it now also sets `context.caller_uid = active_->owner.uid`. `ExecutionContext` (`transaction_engine.h`) gains `std::optional<std::uint32_t> caller_uid;`. `AlpmTransactionEngine::execute/executeUpgrade` already pass the context to the drivers. If the optional is empty, no tag is written.

### 7.2 Tag formatter (pure) and writer — `src/transaction/src/history_tag.{h,cpp}`

```cpp
// "Running '<action> <names…>' (uid N)"; "Running 'upgrade' (uid N)" for an upgrade. nullopt when the input is not a
// valid tag: an upgrade with names, an install or remove without names, or a name that fails isValidPackageName().
[[nodiscard]] std::optional<std::string> formatHistoryTag(holonight_packages_domain::PlanAction action,
                                                          std::span<const std::string> names, std::uint32_t uid);

enum class TagOutcome : std::uint8_t { Written, Failed, Skipped };   // Skipped: no caller uid or no valid tag

class HistoryTagWriter {
 public:
  HistoryTagWriter(const TransactionSeams& seams, LogSink journal);
  // Calls observe("tag-writing") before and observe("tag-written") or observe("tag-failed") after. The seam
  // inject_fault("tag-write") returning non-zero skips alpm_logaction and counts as a failed write.
  [[nodiscard]] TagOutcome write(alpm_handle_t* handle, holonight_packages_domain::PlanAction action,
                                 std::span<const std::string> names, std::optional<std::uint32_t> caller_uid) const;
};
```
`write` calls `alpm_logaction(handle, "HOLONIGHT", "%s\n", text.c_str())`. A non-zero result or a seam fault is logged to the journal at `Warning` (`the history tag was not written; the transaction continues`) and returns `Failed`. The transaction continues unchanged (REQ-F-004). `alpm_logaction` appears only in `history_tag.cpp` (rule H-014). Names come from `PlanRequest::names`, which `validateRequest` has already restricted to `[a-z0-9@._+-]`, so no quote, space or newline can reach the line; `formatHistoryTag` checks again with `isValidPackageName`. No free text is part of the format (REQ-F-002).

### 7.3 Insertion points

Install and remove (`execution_driver.cpp`). Order in a run: PlanService authorizes -> `ExecutionDriver::run()` opens the session -> `attempt()`: `PlanResolver::resolve(Execute)` (`alpm_trans_init`, prepare) -> `samePlan` -> `commit()`. In `ExecutionDriver::commit()`, after

```cpp
if (gate_.cancelRequested()) { return ExecutionResult::cancelled(); }   // line 220-222
```
and before `alpm_trans_commit(handle_, &data)` (line 224), add:

```cpp
if (!tag_written_) {                     // a corrupt-package retry runs attempt() twice; the tag is written once
  tag_written_ = true;
  static_cast<void>(tag_writer_.write(handle_, request_->action, request_->names, context_->caller_uid));
}
```
`tag_written_` and `tag_writer_` are new members of `ExecutionDriver`.

Upgrade (`upgrade_driver.cpp`). In `UpgradeDriver::commitStep()`, after the `fault(std::string(step) + "-commit")` check and before `live.clearLog(); alpm_trans_commit(...)`, add the same call with `PlanAction::Upgrade`, empty names and `context_->caller_uid`. `commitStep` runs once for the keyring step and once for the main step, so a keyring upgrade writes two lines (section 14, item 2). The scratch `DOWNLOADONLY` commit in `fetchOnce()` writes nothing; a refused `PrepareUpgrade` writes nothing.

Refusals write nothing (REQ-F-003): authorization denial returns in `PlanService::authorizationDecided` before `startRunning`; every plan-validation failure (`PlanChanged`, `Busy`, `NeedsDecision`, a resolver refusal, `NotAuthorized`) returns before `commit()`; a cancel before the commit returns before the tag.

### 7.4 Failure seam and call-order test

- Failure: `TransactionSeams::inject_fault` returns non-zero for the stage `"tag-write"`. No new seam field is added. `main.cpp` still does not reference `TransactionSeams` (H-008).
- Call order: `tests/transaction/history_tag_test.cpp` builds `PlanService` over a real `AlpmTransactionEngine` and a `FixtureRoot` with a `FakeAuthorizer`, `FakeCallerIdentity` (uid 1000, `tests/support`), a manual work runner and a fixed plan id generator. One `std::vector<std::string> events` is shared: `FakeAuthorizer::beforeAnswer` pushes `"authorize"`; `seams.observe` pushes every stage. The assertion is the subsequence `authorize`, `opened`, `resolved`, `tag-writing`, `tag-written`, `commit-point`, `committed`, followed by `finished`. `commit-point` is libalpm's first event inside `alpm_trans_commit`, so `tag-writing` before it proves the write precedes the commit. A second assertion reads the fixture `pacman.log`: the `[HOLONIGHT]` line comes before the `[ALPM] transaction started` line.

### 7.5 Implementation notes (phase 8)

- `ExecutionContext` lives in `holonight_packages_domain/transaction_engine.h`; `caller_uid` was added there.
- The T-027 origin checks run inside `history_tag_test` (`TagOrder` gives Packages with uid 1000, `TagWriteFails` gives unknown): the test reads the fixture log through `PacmanLogHistorySource` directly, so no log file is handed between test binaries. `test_holonight_transaction` therefore links `holonight_packages_application` and `holonight_packages_backends`.
- The fixture repository is signed (`Required DatabaseOptional`), as in `ExecutionTest`: the engine refuses an unsigned one with `InsecureSigLevel`. The refusal case uses a plan change (`PlanChanged`) on a package that is not installed, because a newer version of an installed package is refused earlier as `PartialUpgradeRequired`.
- The once-per-Execute rule is checked in the existing corrupt-package retry test (`ExecutionTest.ACorruptPackageIsDownloadedAgainOnceAndTheRetryUsesAFreshLock`): two attempts, one tag.

## 8. Decisions

| # | Decision | Rationale |
|---|---|---|
| D-1 | The parser is a pure function over complete lines with an explicit state struct; the adapter owns all file access. | Testable without files (REQ-F-006..016); one place for I/O. |
| D-2 | Hand-written line scanner; no `std::regex`. | 53,000 lines in 500 ms leaves 9 µs per line; libstdc++ regex costs several µs per short line even for a failing match and allocates. Dispatch on fixed offsets and `starts_with` costs well under 1 µs. Regexes stay in the test oracle. |
| D-3 | Carry-over parse state (`last`, `last_open`, `attach_open`, `pending_origin`, `pending_hooks`) instead of re-reading the last block. | REQ-F-018 requires the bytes read to equal the appended bytes; a re-read of the last block fails it and its cost depends on the block size (an initial install block is thousands of lines). |
| D-4 | The port returns a delta (`reset`, `drop_last`, `append`), not a full list. | The only entry that can change is the last one (hooks and messages attach to it; its status is re-evaluated). A delta keeps REQ-F-018 ("first 1,000 unchanged") and lets the model insert rows without a reset. |
| D-5 | Open status is resolved from the lock probe at every read; the parser stores only "open". | REQ-F-013 requires a re-evaluation without new bytes. |
| D-6 | `LogFile` and `DBPath` come from `parsePacmanLogLocations`, a separate entry point of the shared parser; empty means the defaults; the lock file is `<db_path>/db.lck` (OQ-H1). | One shared scanner and no second reader of `pacman.conf`, while `PacmanConfig`/`PacmanSettings` keep their "no database path" contract for the helper. |
| D-7 | An unreadable `pacman.conf` gives the same empty state as a missing log, with the parser's error text as the reason (OQ-H2). | One path in the model and the page. |
| D-8 | Two seams on the adapter: `HistoryFileIo` (`open`, `pread`, `close`) and `LockProbe`. Reads are chunks of 1 MiB cut at the last newline; a chunk without a newline is consumed as one malformed line. | Counts bytes, flags, whole-file reads and threads (REQ-F-017..019, 022, 036) without touching the real log; bounded memory for a log that grows. `std::ifstream` would hide the open flags. |
| D-9 | Labels, summary, local day and time label are pure functions in `src/application`; the Qt models only call them. | GTests without Qt (REQ-F-025, 027, 028) run in the libalpm-free, QtDBus-free `checks` target. |
| D-10 | `HistoryModel` stores entries in file order and maps row `r` to `N-1-r`; there is no sort proxy. `HistoryFilterModel` filters only, and tracks the selection by `sequence`. | Newest first without an O(n log n) sort per refresh; appends become inserts at row 0. File order is time order for a log that is only appended (R-8). |
| D-11 | The page triggers the first read through `HistoryView.active` and `ensureLoaded()`; the outcome path and the button both call `HistoryModel::refresh()`; `ModelListRefresh` gets `setHistoryTarget()`. | `StackLayout` instantiates every page (F-7). No new port; the existing single-flight pattern is reused (F-6). |
| D-12 | Only the first parse hides the table: the `Loader` is inactive until `loaded`. A refresh keeps the rows; the delta (D-4) changes at most the last entry and inserts new rows, and `HistoryFilterModel` keeps the selection by `sequence`. | Amended REQ-F-024: no flicker after each own transaction; a `Loader` still gives structural absence on first open. |
| D-13 | The tag is written in `commit()` / `commitStep()` immediately before `alpm_trans_commit`, once per libalpm commit, after trans init, prepare and `samePlan`. It is not written from a libalpm event callback. | After `samePlan` nothing can refuse the request except a cancel (checked just before) and a download or integrity failure inside the commit. Before `alpm_trans_commit` the line precedes every libalpm line of the block, including pre-transaction hooks. The order of libalpm's own `transaction started` line and the `TRANSACTION_START` event is not part of a documented contract. |
| D-14 | The uid travels in `ExecutionContext::caller_uid`. | The context already crosses the application/engine boundary and has no D-Bus or polkit words (H-001). |
| D-15 | `formatHistoryTag` is pure; `HistoryTagWriter` is the only code that calls `alpm_logaction`; the failure seam is `inject_fault("tag-write")`; the order is observed through `observe("tag-writing")`. | Reuses the existing seams; no production path can reach them (H-008). |
| D-16 | Labels: `Packages`, `Packages · <user>`, `Packages · uid N`; summary order upgraded, installed, downgraded, reinstalled, removed; empty summary shows `—`; `reinstalled` stores its version as `new_version`. | OQ-H3, OQ-H5; REQ-F-008 ("wherever the line gives them"). |
| D-17 | The benchmark and the latency probe are in a separate executable built in Release (section 10.4). | F-8; Debug timings say nothing about the 500 ms budget. |
| D-18 | No file watcher and no timer. A refresh happens after an own outcome or by the button. | The SPEC lists these two triggers only. |
| D-19 | New policy rules Y-001..Y-003 and H-014 in `check-layering.py` (section 11). | REQ-F-033, 034, 032, 023; keeps `alpm_logaction` in one file. |
| D-20 | The sidebar placeholder is enabled in place (OQ-H7). | REQ-F-023. |

## 9. Alternatives considered

| Alternative | Why not |
|---|---|
| Parse the log in `holonight-packaged` and send entries over the session bus | Non-goal and REQ-F-035: the service and its D-Bus XML stay unchanged. It would add a second reader, a wire format and latency to the first open. |
| `std::regex` per line | D-2. A micro-estimate gives seconds for 4.5 MiB in a Release libstdc++ build, against a 500 ms budget with no margin for Debug. |
| Re-read the last block on every incremental read | D-3. Violates REQ-F-018's byte count and costs more as the last block grows. |
| Carry over only an offset and re-parse from the last start line | Loses the origin (the Running line is before the start line) and the open/attach flags; it needs a second backward scan. |
| Store a parsed cache (a file, or settings) to skip the first parse | Forbidden by REQ-C-005. The parse is fast enough without it (section 10.4). |
| Return the whole entry list from every read | Copies 1,178 entries and their strings on each refresh; the delta needs no copy of the old list. |
| `QSortFilterProxyModel` sort by time, descending | O(n log n) per reset and a role-based `lessThan`; D-10 is O(1) per row. |
| Write the tag from the libalpm event callback at the commit point | D-13: the order against libalpm's own log line is not documented. It would also run inside libalpm's callback with a held transaction. The cost of D-13 is an orphan tag when a download fails after the tag; see R-2. |
| Write the tag before trans init, as REQ-F-001 states | F-2: it leaves a tag for a request that the re-resolution refuses, which contradicts REQ-F-003. |
| One tag per upgrade, before the keyring step | F-3: the main step's start line would have another start line between it and the tag, so its origin would be `unknown` (REQ-F-007). |
| A `QFileSystemWatcher` on the log | Not in the SPEC (D-18); the log changes on every pacman run and the page would reload without a user action. |
| `mmap` of the log | A shrinking file raises `SIGBUS`. `pread` with a size from `fstat` is safe. |
| A new `HistoryRefresh` port next to `PackageListRefresh` | The existing port is already called once per outcome; a second port would need a second call in `TransactionFlow`. |

## 10. Test plan

### 10.1 Fixtures and support

- `tests/support/history_fixture.h`: `HistoryFixture` (a `QTemporaryDir`, `writeConf(log_path, db_path)`, `append(text)`, `truncateAndWrite`, `renameOver`, `touchLock`/`removeLock`) and `guardedIo()`, a `HistoryFileIo` whose `open` fails the test when the path equals `/var/log/pacman.log` (REQ-C-003) and counts calls, flags (REQ-F-036), bytes read and the calling thread id.
- `tests/support/history_log_generator.{h,cpp}`: a deterministic generator (`std::mt19937_64`, seed `0x484953`) for the benchmark. Parameters: 1,178 transactions, 60,000 lines, about 5 MiB. A transaction has an optional Running line, a start line, 1 to 40 action lines of the five kinds, scriptlet lines to match the reference ratio (about 39 percent of lines), a completed line and 0 to 6 hook lines. The last transaction pads with scriptlet lines so the line count is exactly 60,000. The test asserts the line count and a size between 4.8 MiB and 5.5 MiB.
- `tests/support/counting_history_source.h`: a decorator on `HistorySource` that records the mode, the calling thread id and the number of calls; it can block on a `std::promise` (REQ-F-024).
- Fixture log text comes from literals in the tests; real lines are copied only in the line-format test (REQ-F-008).

### 10.2 Test files and targets

| Target (CMake) | Files | Notes |
|---|---|---|
| `test_holonight_packages_checks` (libalpm-free) | `tests/domain/history_source_test.cpp`, `tests/application/history_presentation_test.cpp` | Add to `test_holonight_packages_checks` sources. Set `TZ` with `setenv` + `tzset()` in `main`-level environment of the test (a `SetUp` that restores it). |
| `test_holonight_packages` | `tests/backends/pacman_log_timestamp_test.cpp`, `pacman_log_parser_test.cpp`, `pacman_log_history_source_test.cpp`, `pacman_config_test.cpp` (extend, `DBPath`), `tests/apps/history_model_refresh_test.cpp` (ModelListRefresh + fake transaction service) | Add `HistoryModel.cpp` and `HistoryFilterModel.cpp` to its source list. |
| `test_holonight_packages_history` (new; links domain, application, Qt Quick, no backends) | `tests/history/history_model_test.cpp`, `history_filter_model_test.cpp`, `history_view_test.cpp`, `sidebar_history_test.cpp` | `AUTOMOC ON`, `QT_QPA_PLATFORM=offscreen`, as `test_holonight_packages_transactions`. Its `target_link_libraries` list names no backends target (that list is the CMake link check for REQ-F-034), and rule Y-002 bans the adapter header in the view-model sources. |
| `test_runtime_controls` | add `tests/history/history_view_test.cpp`, `HistoryModel.cpp`, `HistoryFilterModel.cpp` | Runs in Holonight and Fusion (REQ-F-026, REQ-C-006), as `installed_packages_view_test.cpp` does. |
| `test_holonight_transaction` | `tests/transaction/history_tag_test.cpp` | Links `holonight_packages_application` and `holonight_packages_backends` in addition (the PlanService and the adapter origin check). |
| `test_holonight_history_bench` (new, Release only) | `tests/backends/history_bench_test.cpp` | Section 10.4. |
| policy | `tests/policy/test_layering_policy.py` | New cases, section 11. |

### 10.3 Mapping to the SPEC verification table

| REQ | Test (file::case) and seam |
|---|---|
| F-001 | `history_tag_test` `TagOrder`: PlanService over a real engine on a `FixtureRoot` with `file://` repositories; `events` subsequence of 7.4; one `[HOLONIGHT]` line matching `\[HOLONIGHT\] Running 'install <name>' \(uid <N>\)$`; the line precedes `transaction started`. |
| F-002 | `history_tag_test` `FormatTag`: `install foo bar`, `remove foo`, `upgrade` with uid 1000; rejects an upgrade with names, an install with none, a name with a quote; `RejectsInvalidInput`; `upgrade_execute_test` `AnUpgradeWritesOneHistoryTag`: exactly one HOLONIGHT line with nothing after `(uid N)`, and `AKeyringUpgradeWritesOneHistoryTagPerCommit`: two lines, each directly before its own `transaction started` (section 14, item 2). The once-per-Execute rule: `ExecutionTest.ACorruptPackageIsDownloadedAgainOnceAndTheRetryUsesAFreshLock`. |
| F-003 | `history_tag_test` `RefusedWritesNothing`: `FakeAuthorizer::answer = Denied`, then the conflict fixture of the package-helper SPEC; the seed log's byte length and SHA-256 are unchanged; no `[HOLONIGHT]` text. |
| F-004 | `history_tag_test` `TagWriteFails`: `inject_fault("tag-write")` returns 1; `Execute` is `Succeeded`; `installedVersions()` equals a control run in a second `FixtureRoot`; `PacmanLogHistorySource` over the fixture log reports origin `unknown`. |
| F-005 | `pacman_log_history_source_test` `LogPathFromConf`, `DefaultLogPath`: `resolvedLogPath()`; the default case opens nothing (the guarded `io` fails on that path). |
| F-006..F-012, F-016 | `pacman_log_parser_test`: one case per bullet of the SPEC AC, run on string literals (no files). F-008 uses the five real lines of section 5.3; a differential case checks `parseLines` against a `std::regex` reference on a seeded sample. F-016: the five bad inputs plus one valid transaction yield exactly one entry. |
| F-013, F-014 | `pacman_log_history_source_test` `InProgressWithLock`, `LockRemovedWithoutNewBytes` (checks 0 bytes read and `drop_last == 1`), `InterruptedStartStart`, `InterruptedAtEndNoLock`; also with a `LockProbe` seam for the R-4 race. |
| F-015 | `pacman_log_timestamp_test` with `TZ=UTC` and `TZ=Europe/Berlin`; `history_presentation_test` `TimeLabelLegacy` shows `12:00`. |
| F-017 | `history_model_test` `NoReadAtConstruction` (`CountingHistorySource` calls == 0), `FirstOpenOneFullParse`; `history_view_test` `PageOpenStartsOneFullParse` (sets `active`). |
| F-018 | `pacman_log_history_source_test` `IncrementalReadsOnlyAppended`: 1,000 transactions, one append; bytes read via `io.pread` == appended bytes; `drop_last == 0`; the 1,000 entries are unchanged. |
| F-019 | `TruncateTriggersFullReparse`, `RenameOverChangesInodeTriggersFullReparse` (same size, new inode); the counting `io` records a read of the whole file; result equals a fresh `Full` read. |
| F-020 | `PartialLineNotConsumed`: `consumedOffset()` equals the start of the partial line; after appending `leted\n`, the entry is Completed. |
| F-021 | `MissingLogHasReason` (reason names the path), `UnreadableLogHasReason` (mode 000, skipped when running as root), `UnreadableConfHasReason`; `history_view_test` `EmptyStateShowsReason` plus no visible `QQuickPopup`. |
| F-022 | `history_model_test` `ReadsRunOffTheMainThread`: `CountingHistorySource` thread id differs from `QThread::currentThread()` for the full and the incremental read. |
| F-023 | `sidebar_history_test`: loads `Sidebar.qml`; `sidebarHistoryNav` is enabled and is the last `HnNavigationDelegate` of the page column; `QSignalSpy` on `pageRequested` after calling the activation handler (`onClicked` via `QMetaObject::invokeMethod(item, "clicked")`, no pointer input). The test fails against the placeholder. Policy check Y-003. |
| F-024 | `history_view_test` `LoadingHidesRows`: blocking `CountingHistorySource`; `historyLoadingState` visible, `historyTableLoader` inactive (zero delegates through `childItems()`); after release the rows show. `RefreshKeepsRowsAndSelection`: a selected row, then a blocked refresh: no loading state, same row count, same `sequence` selected. |
| F-025 | `history_model_test` `GroupsByDayNewestFirst` with fixed `TZ`; consecutive `dayKey` groups descend; rows inside a group descend. |
| F-026 | `history_view_test` `RowShowsFourTexts` (objectNames `historyRowTime` etc.); runtime acceptance in both styles (no clipped columns, REQ-C-006). |
| F-027, F-028 | `history_presentation_test`: current uid gives `Packages`; uid 1001 with a resolver seam gives `Packages · alice`; unresolved gives `Packages · uid 1001`; `12 upgraded · 1 installed · 2 removed`; `1 removed`; all five kinds in order. |
| F-029 | `history_filter_model_test`: T1 (`linux`, `linux-headers`), T2 (`git`); `linux`, `git`, empty, `LINUX`; selection survives an insert at row 0. |
| F-030, F-011 (GUI) | `history_view_test` `DetailShowsActionsHooksMessages` (arrow form, one pre-transaction and one post-transaction hook in separate sections, empty hook section hidden, warning text), `PacmanOriginShowsCommand`, `PackagesOriginHasNoCommandElement`, `ScriptletTextNeverShown`. |
| F-031 | `history_model_refresh_test`: drives `ModelListRefresh::refreshAfterOutcome` and `refreshAfterUpgrade` directly (the transaction-to-refresh calls are covered by the existing `model_list_refresh_test`); `CountingHistorySource` shows one incremental read, no new full parse. `history_view_test` `RefreshHandlerUsesSamePath`: `refreshRequested()` produces one `Incremental` read. |
| F-032 | `history_view_test` `NoTransactionActions`: walks all `Controls.Button`, `Action`, `HnIconButton` items under `historyPage` and asserts none has a `clicked` connection to a `transactionController`/helper call (the page has no such property); policy Y-002 on the view-model sources. |
| F-033, F-034 | Policy fixtures Y-001, Y-002; the `test_holonight_packages_history` link list; `history_model_test` runs with a fake `HistorySource` only. |
| F-035 | Rule Y-004 (`scripts/check-layering.py --base`, fixtures `y004-*`) and Y-005 (`History` in D-Bus XML); existing packaged tests unchanged. |
| F-036 | `pacman_log_history_source_test` `OpensReadOnlyAndLeavesTheLogUnchanged`: SHA-256 and mtime of the fixture log before and after `Full`, `Incremental`, with and without the lock; the counting `io` shows only `O_RDONLY`. |
| NF-001, NF-002, NF-003 | Section 10.4: `history_bench_test` `FullParseMedianIsUnderBudget`, `IncrementalRefreshMedianIsUnderBudget`, `ParseOnTheWorkerKeepsEventsUnder50Ms`, `ProbeSeesAParseOnTheMainThread`, `HistoryBenchReference.ReferenceLog`. |
| C-001, C-002 | `task layering-check` with the new fixtures (section 11); `c005-trans-in-history-source` fixture. |
| C-003 | `guardedIo()` in every history test; the suite passes when `/var/log/pacman.log` is absent. |
| C-004 | `FixtureRoot` aborts outside the temp dir; the grep policy C-010 already bans pointer automation in `tests`. |
| C-005 | `NoFilesWritten`: directory listing and content digest of the fixture directory before and after a full read and a refresh. |
| C-006 | `task qml-lint`; QML import policy; runtime acceptance in both styles; existing QML tests. |
| C-007, C-008, C-009 | `task check:integration`; `task format-check`, `task tidy`; the table above. |

### 10.4 Benchmark, latency probe and flakiness

Executable `test_holonight_history_bench` (sources: `tests/backends/history_bench_test.cpp`, the generator, `holonight_packages_backends`, Qt6 Core and Concurrent). CMake adds `add_test(... LABELS bench)` only when `CMAKE_BUILD_TYPE` is `Release`; in other types the target exists but registers no test (so `task test` in Debug never times it). `task bench:history`:

```
cmake --preset dev -B build/bench -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build/bench --target test_holonight_history_bench
ctest --test-dir build/bench -L bench --output-on-failure
```
The integration stage (`scripts/ci/container.py integration`) must call the same three commands; the implementation task checks that script and adds the step.

- NF-001: generate the fixture once (asserted: 60,000 lines, 4.8 to 5.5 MiB, 1,178 transactions); one warm-up read; five `Full` reads, each on a fresh `PacmanLogHistorySource`; `steady_clock`; the median must be under 500 ms. The expected time is a few tens of milliseconds, so the margin is about ten times and a loaded runner does not flip it. The reference-log measurement is manual: `HOLONIGHT_HISTORY_BENCH_LOG=/var/log/pacman.log` enables one extra test case (skipped otherwise, and the only place that may name that path, through an explicit environment variable and not the guard's default) that prints the time and the entry count (1,178 expected); the value goes into the change description.
- NF-002: after the full parse, 20 iterations of "append one transaction of about 1 KiB, then `read(Incremental)`", timing the read only; the median must be under 20 ms. A second loop of 20 reads with no appended bytes (stat, lock probe, no `pread`) must also be under 20 ms.
- NF-003 probe: the parse runs on a `QtConcurrent` worker. The main thread starts a `QTimer` (`Qt::PreciseTimer`, 10 ms) whose slot records `now - expected` in `steady_clock` and re-arms for the next 10 ms from the expected time, so a late event does not shift the schedule. Sampling stops when the future finishes, before the result is applied, so the one-shot result delivery is not counted. Pass when the largest delay is under 50 ms. To avoid a flaky pass or fail from scheduler noise: (a) the worker does no queued signal, no `QMetaObject::invokeMethod` and no allocation-heavy UI work while parsing; (b) the test is skipped when `std::thread::hardware_concurrency() < 2`; (c) it takes the best of up to three attempts and fails only when all three exceed 50 ms (a systematic block fails all three; one scheduler stall does not); (d) a sensitivity control runs the same parse on the main thread through `QTimer::singleShot`, and asserts that the probe then reports a delay of at least 50 ms. Without (d) the probe could pass with a broken timer. The control is skipped when the parse itself takes under 60 ms on the machine (it cannot block long enough), and it logs that fact.

Implementation notes (phase 10):

- The sensitivity control does not skip on a fast machine: it parses repeatedly on the main thread for at least 80 ms in one blocking call, so it always blocks long enough to cross 50 ms. One Release parse takes about 4 ms, so the original rule (skip under 60 ms) would have skipped it everywhere.
- `task bench:history` passes `{{.CLI_ARGS}}` to the configure step, for example `-DHolonightQt_DIR=...` when a fresh configure would find another HolonightQt. `scripts/ci/integration.sh` runs the same three commands.
- Measured on 2026-11-23 (Release, developer machine): generated fixture (5,755,509 bytes, 60,000 lines, 1,178 transactions) median full parse 4.1 ms; append-and-refresh median 0.005 ms; refresh with no new bytes 0.002 ms; largest event delay with the parse on the worker 0.18 ms; main-thread control 72 ms. Reference log `/var/log/pacman.log` (4,464,091 bytes, 53,473 lines): 1,178 transactions, median 4.1 ms.

## 11. Layering policy changes (`scripts/check-layering.py`, `tests/policy/test_layering_policy.py`)

| Rule | Pattern | Include | Fixture cases |
|---|---|---|---|
| Y-001 | `#\s*include\s*[<"](?:Q\|alpm)` | `src/backends/src/pacman_log_`, `src/backends/include/holonight_packages_backends/pacman_log_history_source.h`, `src/domain/include/holonight_packages_domain/history_source.h` (via `name_prefixes`) | `y001-qt-include-in-history-adapter` (`#include <QString>`), `y001-alpm-include-in-history-adapter` (`#include <alpm.h>`), `legit-history-adapter-clean` |
| Y-002 | `pacman_log_history_source\|DBusTransactionService\|HelperWire\|TransactionController\|org\.holonight\.PackageHelper1` | `apps/packages/app/History*`, `qml/history` | `y002-adapter-header-in-view-model`, `y002-helper-client-in-view-model`, `legit-history-model-uses-port` |
| Y-003 | the block of `sidebarHistoryNav` in `qml/workspace/Sidebar.qml` contains neither `enabled: false` nor lacks `pageRequested("history")`; History is the last `HnNavigationDelegate` before the footer spacer | a small function `check_history_sidebar(root)` next to `check_carve_out` | `y003-history-nav-disabled`, `y003-history-nav-not-last` |
| H-014 | `\balpm_logaction\b` | `src`, `apps`, excluding `src/transaction/src/history_tag.cpp` | `h014-logaction-in-driver`, `legit-logaction-in-history-tag` |
| (C-005 case) | `alpm_trans_*` already covered | | `c005-trans-in-history-source` (`src/backends/src/pacman_log_parser.cpp`) |

H-006 already forbids `History` in a helper D-Bus method name. The C-001 rule is not changed; the new `PacmanLogLocations::db_path` name is outside its pattern (`dbpath`, `databasePath`, `database_path`).

Implementation notes (phase 9): Y-004 is the `--base` step: `git diff --name-only <base>` plus untracked files under `apps/packaged` and `apps/helper/dbus` must be empty; its fixtures build a throwaway git repository. Y-005 bans `History` in the D-Bus XML of both services (REQ-F-035). Y-003 also fails when `sidebarHistoryNav` is missing. `NoFilesWritten` lives in `pacman_log_history_source_test` and snapshots the fixture directory (names, sizes, mtimes, contents) around a full read and an incremental refresh.

## 12. Resolution of the open questions

| OQ | Answer |
|---|---|
| H1 | `PacmanLogLocations::db_path` from `parsePacmanLogLocations`; default `/var/lib/pacman`; lock `<DBPath>/db.lck` (D-6). |
| H2 | Unreadable `pacman.conf` gives an explained empty state, reason = parser error text (D-7). |
| H3 | upgraded · installed · downgraded · reinstalled · removed (5.5). |
| H4 | Case-insensitive substring on package name (5.7). |
| H5 | `Packages · <username>`; `Packages · uid N` when unresolved (5.5). |
| H6 | Pinned in 5.3, with a real example of each kind except `[ALPM] error:`. |
| H7 | The existing `sidebarHistoryNav` is enabled in place (5.8). |

## 13. Known risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | A growing log breaks the 500 ms budget. | The bench runs in the integration stage; the reference log is measured at each budget review; paging is the documented fallback (SPEC non-goal). Expected margin is about ten times. |
| R-2 | An orphan tag: the line is written, then a download or integrity check fails (or the user cancels during the download). A later transaction that has no Running line and no tag of its own (only after a failed tag write, REQ-F-004) would inherit the orphan's origin. | Rare: pacman always writes its own Running line, and a helper transaction writes its own tag. The orphan line has no effect on the page by itself. Accepted. Moving the write into the event callback would fix it at the cost of D-13's ordering uncertainty. |
| R-3 | Hook attachment by position (SPEC R-3) and the pre-transaction hooks that attach to nobody (F-4). | Documented in section 14, item 3; tests cover the SPEC cases. |
| R-4 | Status race at the lock. | The probe runs after the read; the next refresh corrects it (REQ-F-013). Tested with the `LockProbe` seam. |
| R-5 | An open transaction followed only by a Running line and no start line, while another process holds the lock, shows `InProgress` for the old entry. | Edge of the SPEC rule "open at the end of the file"; the next start line closes it. Noted, not handled. |
| R-6 | A log rewritten in place to a larger size with the same inode is not detected. | SPEC R-6 / REQ-F-019; the manual refresh does not force a full parse. A `Full` read exists in the port if a later requirement wants a "reload" action. |
| R-7 | `mktime` in the legacy-format path depends on the process `TZ`; around DST changes it is ambiguous. | `tm_isdst = -1`; SPEC R-7. Current-format lines carry an offset and do not use it. |
| R-8 | Rows are in file order, not sorted by time; a clock change makes a day group non-monotonic. | The log is append-only, so file order is the order of events. A sort by `started` would hide the clock jump; not done. |
| R-9 | The detail panel for a very large upgrade holds thousands of action labels. | `Repeater` inside a `ScrollView`; built only for the selected entry (`Loader`). The model never builds `actions` for rows that are not selected. |
| R-10 | `libalpm` logging while the pre-transaction `alpm_logaction` fails because the log file cannot be opened. | The write returns -1, the journal gets a warning, the transaction continues (REQ-F-004); libalpm's own lines would fail too. |
| R-11 | Adding `caller_uid` to `ExecutionContext` could be set from a caller-supplied value later. | The only writer is `PlanService::startRunning`, from `DBusCallerIdentity`. The D-Bus API has no uid argument (H-007 forbids new arguments with such names). |

## 14. SPEC issues

Status 2026-11-23: items 1, 2, 3, 4, 5, 6, 7 and 8 were accepted and the SPEC was amended (see its revision history). Items 9 and 10 are notes for implementation. Item 3 is now handled by `pre_hooks` (section 5.3).

1. **REQ-F-001, REQ-F-003: call order.** The AC lists "authorization, `alpm_logaction`, transaction initialization, commit". In the code, `alpm_trans_init` and `alpm_trans_prepare` are inside the re-resolution (`PlanResolver::resolve(Execute)`), which is the last plan validation and can still refuse (`PlanChanged`, `Busy`, `NeedsDecision`). A tag before trans init would be written for a request that is then refused, which contradicts REQ-F-003. The design writes the tag after `samePlan` and just before `alpm_trans_commit`. Proposed AC: "authorization, then initialization and re-resolution, then `alpm_logaction`, then commit"; the statement "immediately before the transaction is initialized and committed" becomes "immediately before the transaction is committed".
2. **REQ-F-002 and REQ-F-007: upgrades with a keyring step.** An upgrade whose plan holds `archlinux-keyring` commits two libalpm transactions, so the log has two start lines. One tag before the first would give the second block origin `unknown` (REQ-F-007: another start line lies between). The design writes one `Running 'upgrade' (uid N)` line per libalpm commit. REQ-F-002's "exactly one HOLONIGHT line" holds for an upgrade without a keyring step; with one, the log has two, and the page shows two entries for one upgrade, as it does for pacman. Proposed AC: "one line per committed libalpm transaction".
3. **REQ-F-009 and the pre-transaction hooks.** `PreTransaction` hooks log `running '….hook'...` after the Running line and before `transaction started` (354 of 1,178 on the reference log). By the boundary rule they attach to no transaction, so the detail panel lists post-transaction hooks only. This follows the SPEC as written; stated so that the page is not read as a complete hook list.
4. **REQ-F-020 AC needs an observable.** "The consumed offset equals the byte offset" requires a test accessor. The design adds `consumedOffset()` and `resolvedLogPath()` on `PacmanLogHistorySource`, outside the port.
5. **REQ-F-024 and refresh.** The text hides the rows during a refresh too, so the open table disappears briefly after each own transaction. The design keeps the selection (the model is not reset by `loading`), but the flicker is a consequence of the requirement.
6. **REQ-F-023 "last page entry".** `Sidebar.qml` has `sidebarSettingsNav` after History in a footer. The design reads "page list" as the inner page column (F-11).
7. **REQ-NF-002 wording.** "Appends one transaction, then runs twenty refreshes" leaves 19 refreshes with no new bytes. The design measures both: twenty append-then-refresh iterations, and twenty refreshes with no bytes.
8. **REQ-F-008 `reinstalled`.** The line gives one version. The design stores it as `new_version` and leaves `old_version` empty.
9. **REQ-F-009 `[ALPM] error:` sample.** The reference log has no `[ALPM] error:` line, so the error pattern is pinned from the libalpm shape and covered by a hand-written fixture only (section 5.3).
10. **REQ-F-035 AC.** "A diff of the introspection XML against the base" needs a base ref; the design uses `check-layering.py --base`, which already compares against a git ref.
