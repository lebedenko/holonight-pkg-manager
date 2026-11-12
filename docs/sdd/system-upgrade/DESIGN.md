# System upgrade from the Updates page — DESIGN (HoloNight Packages)

Status: Implementation finalized, 2026-11-12. Automated acceptance passed; the five manual VM checks in TASKS.md remain release gates. This design implements `docs/sdd/system-upgrade/SPEC.md` (94 REQ-F, 7 REQ-NF, 10 REQ-C). It extends the designs of `docs/sdd/package-helper-transactions/DESIGN.md` ("helper DESIGN") and `docs/sdd/gui-package-transactions/DESIGN.md` ("GUI DESIGN"). It reuses their components. It adds no parallel stack.

Parts that depended on a spike were marked provisional. All spikes have run; section 11 records the outcome of each.

## 1. Intent in one paragraph

`PrepareUpgrade` syncs the databases into a private scratch root and resolves a full system upgrade against them. The live databases stay unchanged. `Execute` downloads the packages into the cache from the scratch handle, takes the live lock, checks that nobody refreshed the live databases, re-resolves, promotes the scratch databases over the live ones (the point of no return), commits `archlinux-keyring` alone, and then commits the rest from the cache.

## 2. Findings that shape the design

These come from reading the code. Each one changes something the SPEC does not state.

| # | Finding | Consequence |
|---|---------|-------------|
| F-1 | `scripts/check-layering.py` rule **H-004** bans `alpm_db_update` and `alpm_option_set_fetchcb` in `src/transaction` and `apps/helper`. Rule **H-006** bans a `<method name="...Upgrade...">` in `apps/helper`. | Both rules are amended (section 13). `alpm_db_update` is allowed in one file only. H-006 keeps `Answer` and `History`. A new artifact check pins the method list (REQ-F-072). |
| F-2 | `DatabaseLockGuard::ownsLock()` is true for any session that began a non-`NOLOCK` transaction. `main.cpp` then removes the *live* `<dbpath>/db.lck`. A fetch-phase transaction on the scratch dbpath would make the terminator delete a lock that another pacman owns. | The guard records the lock *path* of the owning session (`ownedLockPath()`). The terminator removes that path and no other. See 5.7. |
| F-3 | `loadPacmanSettings` fails closed on a repository without servers, with a string error. It does not read `XferCommand`, `DBPath`, `RootDir` or `Usage`. | A structured diagnostics out-parameter is added to the parser (5.2). REQ-F-018 and REQ-F-076 need names. |
| F-4 | libalpm runs its disk-space check inside the commit. It has no standalone call. `DOWNLOADONLY` returns before it. | REQ-F-048 cannot call "libalpm's check" before the point of no return. The design uses a `statvfs` estimate before promotion, and libalpm's own check runs again inside each step (5.6, section 14, item 2). |
| F-5 | The `DownloadUser` download sandbox runs the downloader as another user. A scratch root owned by root with mode `0700` cannot be entered by that user. | The state directory and the scratch root use mode `0711` (traverse, no listing), so the sandbox stays on (REQ-F-008 amended). Decision D-9. Spike S-1(f) confirms it. |
| F-6 | A fetch-phase signature check uses the *old* keyring. A package signed by a key that only the new keyring knows fails there. | Keyring-first would help nothing on its own. The fetch tolerates that one failure when a keyring step exists, and an explicit verifier defers "key missing" until after the keyring step (D-6, SPIKES S-1 c). |
| F-7 | REQ-F-029 (unknown key gives `KeyImportRequired`) and REQ-F-047 (unknown key gives `SignatureInvalid`) describe the same fixture. | The design separates them by cause (5.5). See section 14, item 3. |
| F-8 | `PrepareUpgrade` has no `outcome` field, but REQ-F-032 wants it to answer `Cancelled`. | A new status code `Cancelled`, valid for `PrepareUpgrade` only. See section 14, item 1. |
| F-9 | The unit has no `ProtectSystem`. The artifact check forbids `ReadWritePaths`. | No `ReadWritePaths` is needed. Only `StateDirectory` is added (7.4). |
| F-10 | `ModelListRefresh` re-reads Installed and Explore only. It does not touch Updates or `CheckNow`. | A second port method `refreshAfterUpgrade()` (6.6). |

## 3. Components and module placement

### 3.1 Diagram

```
 GUI process (holonight-packages)                         holonight-packaged (session)
 +--------------------------------------------------+     +-----------------------+
 | qml/updates  UpdatesView ("Update system…")      |     | CheckNow() (unchanged)|
 | qml/transactions  Preparing / UpgradePreview /   |     +-----------^-----------+
 |                   Progress / Result pages        |                 | after every outcome
 |        |                                         |                 |
 | TransactionController + TransactionRowModel      |                 |
 |        |  (apps/packages/app)                    |  ModelListRefresh.refreshAfterUpgrade()
 | TransactionFlow  (src/application)  ------------------------------+
 |        |  port: domain::TransactionService       |
 | DBusTransactionService + HelperWire (adapter)    |
 +--------|-----------------------------------------+
          | system bus: PrepareUpgrade / Execute / Cancel(""|id) / Progress
 +--------v-----------------------------------------+
 | holonight-package-helper (root, systemd unit)    |
 | apps/helper: HelperAdaptor, HelperService,       |
 |   WireTypes, PolkitAuthorizer, HelperTerminator, |
 |   StateDirScratchRoots (creates scratch roots)   |
 |        |  application ports                      |
 | src/application: PlanService, PlanStore,         |
 |   ScratchRoots (port), Authorizer, Terminator    |
 |        |  domain port: TransactionEngine         |
 | src/transaction  AlpmTransactionEngine           |
 |   +- UpgradeResolver   (sysupgrade -> UpgradeDetail)
 |   +- ScratchDbpath     (layout, seed, local link)
 |   +- SyncRefresher     (alpm_db_update, only file allowed)
 |   +- GuardSnapshot     (SHA-256, size, mtime)
 |   +- PackageVerifier   (sha256 + signature)
 |   +- SyncPromotion     (rename / copy+rename, rollback)
 |   +- UpgradeDriver     (fetch, lock, guard, steps)
 |   +- FailureReporter   (extracted from ExecutionDriver)
 |   (reused: AlpmSession, QuestionPolicy, EventMapper,
 |    CommitGate, download_classifier, error_mapping)
 +--------------------------------------------------+
          libalpm  ->  scratch dbpath (private)  /  live dbpath (promotion, commit)
```

### 3.2 Placement

| Part | Module | New or changed |
|------|--------|----------------|
| `PlanAction::Upgrade`, `UpgradeDetail`, `UpgradeWorkspace`, `SyncFileStamp`, `UpgradePrepared` | `src/domain` (`transaction_plan.h`, new `upgrade_plan.h`) | new/changed |
| New `TransactionErrorCode` values, new `ProgressPhase` values, `ProgressUpdate` step fields | `src/domain` | changed |
| `TransactionEngine::prepareUpgrade`, `executeUpgrade` | `src/domain/transaction_engine.h` | changed |
| `TransactionService`: `queryProtocolVersion`, upgrade via `PlanRequest{Upgrade}` | `src/domain/transaction_service.h` | changed |
| `PlanService::prepareUpgrade`, cancel of a preparation, upgrade authorization, scratch lifecycle | `src/application` | changed |
| `PlanStore` discard observer | `src/application` | changed |
| `ScratchRoots` port | `src/application/include/.../scratch_roots.h` | new |
| `TransactionFlow` upgrade states, step view, `UpdateSystem` action | `src/application` | changed |
| `PackageListRefresh::refreshAfterUpgrade()` | `src/application` | changed |
| Engine classes (see 3.1) | `src/transaction` | new files, one extraction |
| `pacman` diagnostics (`unsupported`, `repository_without_server`) | `src/backends/src/pacman_*` (target `holonight_packages_pacman_config`) | changed |
| `StateDirScratchRoots`, `HelperService::prepareUpgrade`, adaptor slot, wire types, identity constants, `--state-dir` | `apps/helper` | new/changed |
| XML, policy, unit | `apps/helper/dbus`, `apps/helper/data` | changed |
| `DBusTransactionService` (PrepareUpgrade call, probe, 10 min timeout), `HelperWire` (preview decode, step unpacking), `TransactionController`, `TransactionRowModel`, `TransactionMessages`, `ModelListRefresh` | `apps/packages/app` | changed |
| Updates button, upgrade preview page, preparing page, refusal button | `qml/updates`, `qml/transactions` | new/changed |

Naming rules of AGENTS.md apply. Classes are `CamelCase`. Files in `src/transaction/src` are `snake_case`. Private members end in `_`.

**Layering words.** `check-layering.py` rejects the words polkit and QDBus in `src/domain`, `src/application` and `src/transaction`, comments included. All new code and comments there say "authorization" and "bus". The action ID is a plain string.

## 4. Domain and application types

### 4.1 Domain

```cpp
// transaction_plan.h
enum class PlanAction : std::uint8_t { Install, Remove, Upgrade };   // planActionName: "install" | "remove" | "upgrade"

// upgrade_plan.h
struct UpgradeEntry     { std::string name, old_version, new_version, repository; bool operator==(const UpgradeEntry&) const = default; };
struct ReplacementEntry { std::string old_name, old_version, new_name, new_version, repository; /* == */ };
struct NewDependencyEntry { std::string name, version, repository; /* == */ };
struct HeldBackEntry    { std::string name, installed_version; /* == */ };

struct UpgradeDetail {
  std::vector<UpgradeEntry> upgrades;               // sorted by name
  std::vector<ReplacementEntry> replacements;       // sorted by new_name
  std::vector<NewDependencyEntry> new_dependencies; // sorted by name
  std::vector<HeldBackEntry> held_back;             // sorted by name
  std::vector<std::string> keyring_packages;        // sorted; empty means no keyring step (REQ-F-023)
  std::uint64_t download_bytes = 0;                 // volatile: not part of equality (REQ-F-025)
  std::int64_t size_change_bytes = 0;               // volatile
  [[nodiscard]] bool keyring() const noexcept { return !keyring_packages.empty(); }
  [[nodiscard]] std::uint8_t stepCount() const noexcept { return keyring() ? 2 : 1; }
};

struct SyncFileStamp { std::string name; std::string sha256; std::uint64_t size = 0; std::int64_t mtime_ns = 0; };
struct UpgradeWorkspace { std::filesystem::path scratch_root; std::vector<SyncFileStamp> guard; };
struct UpgradePrepared  { ResolvedPlan plan; std::vector<SyncFileStamp> guard; };

// ResolvedPlan gains:  std::optional<UpgradeDetail> upgrade;   (set iff action == Upgrade; `changes` and `hooks` stay empty)
```

`samePlan` for `Upgrade` compares the five lists by name, old version, new version and repository, plus the keyring list. It ignores `download_bytes`, `size_change_bytes` and mirror hosts (REQ-F-025). The existing Install and Remove comparison is untouched.

New error codes, appended after `InternalError` so that existing ordinals do not move:
`SyncFailed`, `SyncDatabaseChanged`, `KeyImportRequired`, `UpgradePartiallyApplied`, `Cancelled`. `kTransactionErrorCodeCount` becomes 29. `Cancelled` is the status of a cancelled `PrepareUpgrade` (section 14, item 1). `NothingToDo` already exists and is reused (REQ-F-024).

New progress phases, appended: `Syncing`, `Promoting`, `KeyringStep`. New phase `MainStep` too. `ProgressUpdate` gains `std::uint8_t step_index = 0, step_count = 0;`, set by the flow from the last step marker (OQ-U1, section 6.3).

`validateRequest` rejects the action string `"upgrade"` in `PreparePlan` with `InvalidArgument`. A new `validateUpgradeRequest(optionKeys)` returns `InvalidArgument` for any key (REQ-F-002). It runs before libalpm is touched.

### 4.2 Domain engine port

```cpp
struct UpgradePrepareOutcome {
  std::expected<UpgradePrepared, TransactionFailure> result;
  bool worker_stuck = false;   // a libalpm call could not be stopped (timeout, size cap); the process must end
};

class TransactionEngine {
  // ... resolve() and execute() unchanged ...
  // Syncs into `scratch_root` (created by the caller), resolves the upgrade, takes the guard snapshot.
  // Reports Syncing through context.progress. Never touches the live sync or local directories (REQ-F-007).
  virtual UpgradePrepareOutcome prepareUpgrade(const std::filesystem::path& scratch_root, const ExecutionContext& context) = 0;
  // Fetch, lock, guard, re-resolution, promotion, keyring step, main step. Publishes an ExecutionControl.
  virtual ExecutionResult executeUpgrade(const ResolvedPlan& approved, const UpgradeWorkspace& workspace, const ExecutionContext& context) = 0;
};
```

The two methods are pure virtual. `tests/support/fake_transaction_engine.h` gets matching fakes.

### 4.3 Application

`ScratchRoots` port (`scratch_roots.h`, standard library only):

```cpp
class ScratchRoots {
 public:
  virtual std::expected<std::filesystem::path, ScratchRootError> create() = 0; // new directory, owner root, mode 0711
  virtual void remove(const std::filesystem::path& root) noexcept = 0;         // idempotent; never follows symlinks
  virtual void removeAll() noexcept = 0;                                       // start-up sweep (REQ-F-015)
};
```

`PlanService`:

- `Dependencies` gains `std::reference_wrapper<ScratchRoots> scratch`.
- `AuthorizationActions` gains `std::string upgrade = "org.holonight.packages.upgrade";` The artifact check reads this default (REQ-C-005).
- `prepareUpgrade(unique_name, optionKeys, PrepareReply)` and an `ActivePreparation { owner, reply, scratch_root, abandoned }` next to `ActiveExecution`.
- `StoredPlan` and `Prepared` gain `std::optional<UpgradeWorkspace> workspace`.
- `cancel(caller, "")` calls `cancelPreparation(caller)`.
- `callerGone` also calls `cancelPreparation` when the caller owns the preparation.
- `execute` picks the action ID from the plan's action, sends the three counts as details, and calls `engine_->executeUpgrade` for upgrade plans.
- `PlanStore` gets `setDiscardObserver(std::function<void(const StoredPlan&)>)`. It fires on every exit of a plan from the store (expiry in `find`, `sweepExpired`, `discardPendingFor`, `consume`). `PlanService` wires it to `scratch_.remove(workspace->scratch_root)`. This single chokepoint implements REQ-F-013, REQ-F-014 and REQ-F-036. `finishRunning` already consumes before it calls the reply, which gives "removed before the reply" (REQ-F-014).

## 5. Transaction core (`src/transaction`)

### 5.1 `TransactionConfig` and limits

`TransactionConfig` gains `UpgradeLimits upgrade_limits`:

```cpp
struct UpgradeLimits {
  std::chrono::seconds sync_timeout{480};              // REQ-F-003
  std::uint64_t scratch_cap_bytes = 256ull << 20;      // REQ-F-010, REQ-NF-002
};
static_assert(UpgradeLimits{}.sync_timeout < std::chrono::seconds(600));   // the REQ-F-003 static check
```

The helper's `main.cpp` never sets them (no flag). Tests build a `TransactionConfig` with small values. This is not a seam, so rule H-008 is untouched. Values are discussed in S-4 (8.4).

### 5.2 Configuration checks (REQ-F-017, REQ-F-018, REQ-F-075, REQ-F-076, REQ-F-031)

`parsePacmanRepositories` and `parsePacmanConfig` get an overload with `PacmanDiagnostics* out`:

```cpp
struct PacmanDiagnostics {
  std::vector<std::string> unsupported;        // fixed option tokens, see the table below
  std::string repository_without_server;       // first repository section with no Server and no Include result
};
```

The old overloads pass `nullptr`, so the update check and the install path do not change (REQ-F-074, REQ-F-076 second criterion). `AlpmSession::openUpgrade` calls the diagnostics overload and checks in this order, before `alpm_initialize` (the initialization counter stays 0):

1. `unsupported` not empty: `ConfigurationError`, `names = unsupported` (OQ-U2).
2. `repository_without_server` set: `ConfigurationError`, `names = {repository}` (REQ-F-018).
3. Parse failure of any other kind: `ConfigurationError`, no names (as today).
4. `checkInstallSignatureLevels` (rename later to `checkSignatureLevels`; same code): `InsecureSigLevel`. It runs before any network use, so the fetch callback counter stays 0 (REQ-F-031).

**OQ-U2: the list of unsupported options.** Three buckets.

| Option | Bucket | Reason |
|--------|--------|--------|
| `XferCommand` | Refused | An external downloader would replace libalpm's. The helper never runs one (H-003, H-004). Fixed by REQ-F-076. |
| `DBPath` | Refused when its normalized value differs from the helper's `--dbpath` | The helper takes paths from its command line. A different value means the user's pacman works on another database. An equal value is harmless and accepted. |
| `RootDir` | Refused when it differs from the helper's `--root` | Same reason. |
| `Usage` in a repository section | Refused unless the value is exactly `All` | The parser does not honour it. An upgrade that ignores `Usage = Search` would take packages from a repository the user excluded. Fail closed. |
| `ParallelDownloads` | Honoured | `AlpmSession` already passes it to libalpm. Progress counts completions, so order does not matter. |
| `CacheDir`, `GPGDir`, `HookDir`, `LogFile`, `Architecture`, `SigLevel`, `NoExtract`, `NoUpgrade`, `IgnorePkg`, `IgnoreGroup`, `CheckSpace`, `DownloadUser`, `DisableSandbox*` | Honoured | Already read by the parser and applied by `AlpmSession`. |
| `HoldPkg` | Honoured with REQ-F-075 meaning | Only refuses removal of a protected package (`ProtectedSet`). |
| `CleanMethod`, `Color`, `VerbosePkgLists`, `ILoveCandy`, `NoProgressBar`, `TotalDownload`, `UseSyslog`, `NoPassiveFtp`, `CacheServer`, `LocalFileSigLevel`, `RemoteFileSigLevel`, `DisableDownloadTimeout` | Ignored, by design | None changes which packages an upgrade installs or how trust is decided. `CleanMethod` only affects cache cleaning. `DisableDownloadTimeout` is ignored because the helper bounds its own sync with the internal timeout. The scratch handle always keeps libalpm's low-speed abort. |

The ignored bucket is not "silent": the options do not affect the result. The refused bucket is the only list REQ-F-076 speaks about.

### 5.3 Scratch layout and sync (`ScratchDbpath`, `SyncRefresher`)

Layout under the scratch root `S` (made by `StateDirScratchRoots`, mode 0711; `dbpath` and `dbpath/sync` mode 0711 too, SPIKES S-1 f):

```
S/dbpath/local  -> symlink to <live dbpath>/local     (as the online update check does)
S/dbpath/sync/  <repo>.db, <repo>.db.sig              (seeded, then refreshed)
S/dbpath/db.lck                                        (libalpm, scratch handle only; S-1(a))
S/rollback/     copies of the live files that promotion replaces (5.8)
```

`ScratchDbpath::prepare(config, S, repositories)`:

1. Create `dbpath`, `dbpath/sync`, the `local` symlink.
2. For each configured repository, copy `<live>/sync/<repo>.db` and `.db.sig` if present into the scratch `sync/`. The copy streams the bytes through SHA-256, keeps the mtime (`futimens`, as `AlpmUpdateChecker` does) and records a `SyncFileStamp`. Seeding lets libcurl send If-Modified-Since, so an unchanged database costs no download.
3. After the copy, `stat` each live source again. If a stamp changed, repeat the copy once. A second change returns `SyncFailed`.
4. Return the guard: one stamp for every live `sync/*.db` and `sync/*.db.sig` file, including files of repositories that are no longer configured (REQ-F-051 "added or removed"). Other files (for example `*.files` from `pacman -Fy`) are outside the guard on purpose. Section 14, item 6.

The code follows `alpm_update_checker.cpp` (symlink, seeded copy, mtime, stamps). It cannot link that file, because `holonight_packages_backends` links Qt and `src/transaction` is Qt-free (REQ-C-001). So `ScratchDbpath` is new, small, standard library plus POSIX. `AlpmUpdateChecker` is not changed.

If `<live dbpath>/db.lck` exists when `PrepareUpgrade` starts, the engine returns `Busy` without creating any scratch file. This is an addition to the SPEC. Another package manager is running, and its databases may be half written. It costs one `stat` (decision D-10).

`SyncRefresher::refresh(session, limits, progress, observer)`:

1. Open the scratch handle (`AlpmSession::openUpgrade(..., Role::Scratch)`): `alpm_initialize(root, S/dbpath)`, repositories registered at the configured level with the configured servers, `alpm_option_set_disable_dl_timeout(0)`, configured `ParallelDownloads`.
2. Run `alpm_db_update(handle, syncdbs, 0)` on a detached worker thread. This is the only call of `alpm_db_update` in the code base (rule H-004 exclusion). The calling thread waits on a condition variable with the `sync_timeout` deadline, and also wakes on cancel, on the size cap, and on completion.
3. The download callback (`setDownloadHandler`) does three things. It counts `ALPM_DOWNLOAD_COMPLETED` events of `.db` files (result 0 or 1, 1 meaning up to date) and sends `Progress("", "Syncing", i, n, repo)` for each: exactly one signal per repository (REQ-F-034). It adds the sizes it sees (`downloaded`, or `total` when known at the start) to a running sum plus the seeded bytes, and it raises the "cap exceeded" flag as soon as the sum, or a known `total`, exceeds `scratch_cap_bytes`. It never blocks.
4. Result mapping: return value >= 0 is success. A timeout, the cap, or a failed download (`ALPM_ERR_RETRIEVE`, `ALPM_ERR_LIBCURL`, `ALPM_ERR_EXTERNAL_DOWNLOAD`) returns `SyncFailed`. A failure whose failed file ends in `.sig`, or a signature error (`ALPM_ERR_SIG_INVALID`, `ALPM_ERR_SIG_MISSING`, `ALPM_ERR_DB_INVALID_SIG`), or a key-import question during the sync, returns `DatabaseUnavailable` (REQ-F-011).
5. On timeout or cap, `worker_stuck = true`: libalpm cannot stop a download. `PlanService` replies, then asks the `Terminator` to end the process (same path as Cancel). The detached thread dies with the process.

Progress during the sync goes to the preparing caller only (REQ-F-034). The signal has the empty plan ID.

### 5.4 Upgrade resolution (`UpgradeResolver`)

`UpgradeResolver::resolve(session, mode)` runs on a handle over the scratch dbpath (Role::Scratch). It mirrors `PlanResolver`.

1. `alpm_trans_init(handle, 0)` for the fetch phase and with `ALPM_TRANS_FLAG_NOLOCK` for previews. Both only touch the scratch lock file.
2. `alpm_sync_sysupgrade(handle, 0)`, then `alpm_trans_prepare`.
3. The question policy runs in upgrade mode (5.5).
4. Extraction from `alpm_trans_get_add` and `alpm_trans_get_remove`:
   - An added package whose name is installed is an **upgrade** (old version from the local database).
   - An added package that replaces an installed package listed in `alpm_trans_get_remove` is a **replacement** (the pair is found with `alpm_pkg_get_replaces`).
   - Any other added package is a **new dependency**.
   - **Held back**: every installed package for which `alpm_sync_get_new_version(pkg, syncdbs)` returns a package, and for which `alpm_pkg_should_ignore(handle, pkg)` is true (covers `IgnorePkg` and `IgnoreGroup`). No second version comparison is written (rule H-005 and F-007).
   - **Keyring set**: `archlinux-keyring` if it is in the add list, plus every add-list package needed to satisfy the dependencies of set members that the installed packages do not satisfy (transitive, computed with `alpm_find_satisfier`).
   - Download bytes: sum of `alpm_pkg_download_size`. Size change: sum of `alpm_pkg_get_isize` of added packages minus those of the packages they replace, upgrade or remove.
5. Refusals, in this order: `ProtectedPackage` (a `HoldPkg` package would be removed), `NeedsDecision` (provider, conflict not replaced, removal of an unresolvable package; from `QuestionPolicy` as today), `PackageNotFound` naming the dependency (unsatisfied dependency, including one broken by a held-back package: **OQ-U4 confirmed**, REQ-F-022), `KeyImportRequired` and `SignatureInvalid` are not raised here. If nothing is added, replaced, or removed: `NothingToDo` (REQ-F-024).

### 5.5 Question policy for upgrades (REQ-F-020, REQ-F-026 to REQ-F-030)

`QuestionPolicy` gets a mode, `Mode::Install` (default, current behaviour) or `Mode::Upgrade`. Only the mode changes. Install and Remove tests do not change (REQ-F-030, REQ-F-074).

| libalpm question | Install mode | Upgrade mode |
|------------------|--------------|--------------|
| `REPLACE_PKG` | no, recorded as `NeedsDecision` (Replace) | **yes**, recorded as an accepted replacement |
| `CONFLICT_PKG` | no, recorded | no, recorded: `NeedsDecision` naming both (REQ-F-026) |
| `REMOVE_PKGS` (unresolvable) | no, recorded | no, recorded: `NeedsDecision` naming the package to remove (REQ-F-027) |
| `SELECT_PROVIDER` | recorded, refused | recorded, refused: `NeedsDecision` listing providers (REQ-F-028) |
| `INSTALL_IGNOREPKG` | no | no (leads to `PackageNotFound`, REQ-F-022) |
| `IMPORT_KEY` | no | no, journaled |
| `CORRUPTED_PKG` | as set | as set |

Every question type is written to the journal by `askedTypes()`, as today (REQ-F-028 second criterion).

Signature failure classification in the upgrade path (resolves F-7):

- A key-missing failure of a package (libalpm log line `required key missing from keyring`, or an import-key question) gives `KeyImportRequired` naming the package.
- A bad signature, a missing `.sig`, or any other signature error gives `SignatureInvalid` naming the package.
- During the sync, every signature failure of a database gives `DatabaseUnavailable`.

### 5.6 Execute (`UpgradeDriver`)

`UpgradeDriver` follows the shape of `ExecutionDriver`: constructor takes config, journal, seams, plan, workspace, context; `run()` returns an `ExecutionResult`. It owns a `CommitGate` and publishes it through `control_changed` at the start. It shares code with `ExecutionDriver` through `FailureReporter`, extracted in the first implementation task (`failureContext`, `failureNames`, `failureResult`, `packageNameFor`, with no change of behaviour; the install and remove tests must pass untouched).

Run order. `P` means the cancel gate is checked before the phase.

| # | Phase | Handle | Lock held | What it does |
|---|-------|--------|-----------|--------------|
| 1 | Re-check | none | none | Re-run the configuration checks of 5.2 (the config may have changed since prepare). |
| 2 | Pre-fetch resolution `P` | scratch, `NOLOCK` | none | `UpgradeResolver` again. If the result differs from the preview under `samePlan`: `PlanChanged` (no download made). |
| 3 | Space check `P` | none | none | Only when `CheckSpace` is on. Seam `disk-space`. `statvfs` on the cache directory (download bytes) and on the root (positive size change plus 5 percent). Short: `DiskSpace`. |
| 4 | Fetch `P` | scratch, `DOWNLOADONLY` | scratch `db.lck` | `alpm_trans_init(DOWNLOADONLY)`, sysupgrade, prepare, commit. Packages land in the configured `CacheDir`. The handle keeps the configured package level, so libalpm downloads the `.sig` files (at level `Never` it does not fetch them, SPIKES S-1 c). libalpm downloads every file first, then runs its keyring check and its integrity check (SPIKES S-1 a, b item 4). A commit that fails only in the keyring check with "required key missing from keyring" is tolerated when the plan has a keyring step and every planned file and `.sig` is in the cache; phase 5 then decides (D-6). The import-key question is answered no. Without a keyring step that failure is `KeyImportRequired`. `EventMapper` emits `Downloading` and `Verifying`. A corrupt file is deleted and the phase repeats once (REQ-F-046). |
| 5 | Verify `P` | scratch | none | `PackageVerifier`: for each planned file, SHA-256 against the database entry (`alpm_compute_sha256sum`) and `alpm_pkg_load(..., level)` with the configured package level. Key missing for a non-keyring package while a keyring step exists: deferred to the main step. Other results map as in 5.5. A checksum mismatch deletes the file and repeats phase 4 once. |
| 6 | Release | | none | Scratch session released (scratch lock removed with the handle). Seam `fetched`. |
| 7 | Live lock `P` | live | **live `db.lck`** | Open the live session. Each repository is registered with one sentinel server that does not exist, so no later phase can download (D-7). `beginTransaction(0)` takes the lock. Lock held by another process: `Busy`, plan kept, foreign file untouched (REQ-F-050, as `ExecutionDriver` does). Seam `lock-taken`. |
| 8 | Guard `P` | | live | Compare the live `sync/*.db` and `*.db.sig` with the stamps (hash, size, mtime, added or removed files). Any difference: `SyncDatabaseChanged`. The plan is consumed. |
| 9 | Re-resolution `P` | scratch, `NOLOCK` | live | `UpgradeResolver` once more, against the scratch databases and the live `local` (through the symlink; no other process can change it now). Difference or refusal: `PlanChanged` (REQ-F-052, **OQ-U6 confirmed**; the environment codes of `reresolutionCode` keep their own code). |
| 10 | Point of no return | | live | `CommitGate::tryCommit()`. Cancelled: end. Else emit `Promoting`; seam `promotion-starting`. From here `requestCancel()` returns `PastPointOfNoReturn`. |
| 11 | Promotion | | live | `SyncPromotion` (5.8). |
| 12 | Keyring step | live, trans T0 | live | Only with a keyring set. Emit `KeyringStep` (step 1 of 2). Add the keyring set from the now-promoted databases, prepare, commit. Failure: `UpgradePartiallyApplied`, names `{keyring}`. Success: release T0, begin a new transaction (the lock is dropped for the time between the two calls, section 12.3 risk R-4). |
| 13 | Main step | live | live | Sysupgrade, prepare, compare with the preview minus the keyring set, commit. Step `2 of 2` (or `1 of 1`). Any download request fails because the only server is the sentinel (REQ-F-056). Failure: `UpgradePartiallyApplied`, names `{main}`, the changed list includes the keyring package when its step finished (REQ-F-062). |
| 14 | Finish | | | Release the transaction (lock removed). `HookFailed` warning from the libalpm log, as `ExecutionDriver::commit` does. `Done` is sent only on success (REQ-F-065). |

T0 is the transaction opened in phase 7. For a plan with no keyring step, T0 is the main-step transaction. The live handle registers the sync databases in phase 7 but must not read them (no package-cache, search or satisfier call) before promotion ends: a handle that has read a database keeps its old package cache, while one that only registered it loads the promoted file on first read (SPIKES S-3, item 7). A unit test checks that the first sync read of the live handle follows the `promotion-done` seam.

Step markers: phase 12 starts with one `KeyringStep` (1, 2) signal, phase 13 with one `MainStep` (2, 2) or (1, 1) signal (6.3). The other signals keep their version 1 meaning.

After a successful run the `changed` list is built from `PACKAGE_OPERATION_DONE` events. A replacement appears as one `remove` entry (old package) and one `install` entry (new package) (REQ-F-066). Nothing else about the result structure changes.

Failure and cleanup table:

| Condition | Phase | Result | Live state | Cleanup |
|-----------|-------|--------|------------|---------|
| Unsupported option, server missing, bad config | prepare / 1 | `ConfigurationError` + names | unchanged | scratch removed |
| Weak signature level | prepare / 1 | `InsecureSigLevel` | unchanged | scratch removed |
| Live `db.lck` present at prepare | prepare | `Busy` | unchanged | none created |
| Mirror unreachable, timeout, size cap | prepare | `SyncFailed`; process ends after the reply on timeout and cap | unchanged | scratch removed |
| Database signature bad | prepare | `DatabaseUnavailable` | unchanged | scratch removed |
| Nothing to do | prepare | `NothingToDo`, empty plan ID | unchanged | scratch removed |
| Refusals of 5.4 | prepare | their codes, no plan | unchanged | scratch removed |
| Cancel("") | prepare | `Cancelled`; Cancel replies ok; process ends | unchanged | scratch removed before the reply, again by the terminator |
| Owner disconnect | prepare | as Cancel | unchanged | as Cancel |
| Plan expired | execute | `PlanExpired` | unchanged | scratch removed by `PlanStore` |
| Denied authorization | execute | `NotAuthorized`, plan kept | unchanged | none |
| Pre-fetch plan differs | 2 | `PlanChanged`, plan consumed | unchanged | scratch removed |
| Disk space estimate short | 3 | `DiskSpace` | unchanged | scratch removed |
| Package 404 | 4 | `SyncDatabaseStale` + package | unchanged | scratch removed |
| Other download error | 4 | `DownloadFailed` + package | unchanged | scratch removed |
| Checksum twice wrong | 4, 5 | `DownloadFailed` + package | unchanged | scratch removed |
| Signature bad / `.sig` missing | 5 | `SignatureInvalid` + package, no retry | unchanged | scratch removed |
| Key missing (non-deferred) | 5 | `KeyImportRequired` + package | unchanged | scratch removed |
| Cancel during 2 to 9 | | `Cancelled`; process ends | unchanged | scratch removed; owned lock path removed by the terminator |
| Live lock held by another process | 7 | `Busy`, plan kept | unchanged | none; foreign lock untouched |
| Live sync changed | 8 | `SyncDatabaseChanged`, plan consumed | unchanged | lock released, scratch removed |
| Re-resolution differs | 9 | `PlanChanged`, plan consumed | unchanged | lock released, scratch removed |
| Promotion fault | 11 | `InternalError` | databases rolled back (5.8); installed set unchanged | lock released, scratch removed |
| Keyring step fails | 12 | `UpgradePartiallyApplied` {keyring}, changed = completed packages | new sync databases, old packages | as above |
| Main step fails | 13 | `UpgradePartiallyApplied` {main}, changed = completed | partly upgraded | as above |
| Hook fails after success | 12, 13 | `Succeeded` + `HookFailed` | upgraded | as above |
| Cancel after 10 | | `PastPointOfNoReturn`; upgrade goes on | | process does not end |
| Owner disconnect after 10 | | upgrade continues; result written to the journal by `HelperService` | | |

The raw libalpm text goes to the journal through the `LogSink` only (REQ-NF-004).

### 5.7 Lock ownership (finding F-2)

`DatabaseLockGuard` keeps its API and gains `std::optional<std::filesystem::path> ownedLockPath() const`. `AlpmSession::beginTransaction` stores `alpm_option_get_lockfile(handle)` when the flags lack `NOLOCK`. `main.cpp`'s terminator lambda removes `guard.ownedLockPath()` and nothing else, instead of `config->lockFile()`. A cancel during the scratch fetch removes the scratch lock only (which is inside the scratch root anyway). A cancel after phase 7 removes the live lock that this process took. A foreign live lock is never removed (REQ-F-050). `HelperConfig::lockFile()` is then unused and is deleted. Terminator finalization also calls `ScratchRoots::removeAll()` before `_Exit` (the second removal of the cancel path).

### 5.8 Promotion (`SyncPromotion`)

Input: the list of scratch `sync/` files and the live `sync/` directory. For each file name (`<repo>.db.sig` first, then `<repo>.db`, so a verifier never sees a new database with a missing signature file for longer than one step):

1. Copy the current live file (when present) to `S/rollback/<name>`.
2. If scratch and live are on one filesystem (`st_dev` compare, seam `same-filesystem`), `rename(scratch, live)`. Else copy the scratch file to `<live>/sync/.hn-promote-<name>.tmp` (mode 0644, mtime preserved, `fsync`) and `rename` that temporary over the live file (REQ-F-077). Seam fault points `promotion-rename-<k>`.
3. After the last rename, `fsync` the live `sync/` directory.
4. Re-hash each promoted live file against the scratch copy. A mismatch is a failure.

On a failure before the first package change, each file already replaced is restored from `rollback/` by the same copy-then-rename. Then the result is `InternalError` (REQ-F-054). A file that did not exist before and was added is removed. Restoration errors are journaled. At helper start, `main.cpp` removes any `<live>/sync/.hn-promote-*` file (REQ-F-077 first criterion). Files of repositories no longer configured are left alone.

Promotion code is reachable only from `UpgradeDriver` (REQ-F-071, rule H-013 in section 13).

### 5.9 Engine dispatch

`AlpmTransactionEngine::prepareUpgrade` runs 5.2, 5.3, 5.4 and returns the plan plus the guard. `executeUpgrade` builds an `UpgradeDriver`. `resolve` and `execute` are unchanged. The engine keeps "every operation opens its own handles; nothing is cached between plans".

## 6. Interfaces

### 6.1 D-Bus (`apps/helper/dbus/org.holonight.PackageHelper1.xml`) — OQ-U5

Additions only (REQ-C-002, REQ-F-073):

```xml
<!-- 2. Version 1 was install and remove only. Version 2 adds PrepareUpgrade; nothing else changed. -->
<property name="ProtocolVersion" type="u" access="read"/>

<!-- options: must be empty. The reply is delayed; clients must use a call timeout of 10 minutes.
     The helper replies SyncFailed after its own internal timeout, before that limit. Cancel("") cancels the
     caller's own PrepareUpgrade; the call then answers status code Cancelled. Codes added in version 2:
     SyncFailed, SyncDatabaseChanged, KeyImportRequired, UpgradePartiallyApplied, Cancelled. NothingToDo
     carries an empty plan_id. -->
<method name="PrepareUpgrade">
  <arg direction="in"  name="options"  type="a{sv}"/>
  <arg direction="out" name="status"   type="(sasa(ssas))"/>
  <arg direction="out" name="plan_id"  type="s"/>
  <arg direction="out" name="preview"  type="(a(ssss)a(sssss)a(sss)a(ss)bastxx)"/>
</method>
```

The preview type, field by field:

| Position | Signature | Content |
|----------|-----------|---------|
| 1 | `a(ssss)` | upgrades: name, old version, new version, repository |
| 2 | `a(sssss)` | replacements: old name, old version, new name, new version, repository |
| 3 | `a(sss)` | new dependencies: name, version, repository |
| 4 | `a(ss)` | held back: name, installed version |
| 5 | `b` | keyring flag |
| 6 | `as` | keyring package names |
| 7 | `t` | total download bytes |
| 8 | `x` | installed-size change in bytes (signed) |
| 9 | `x` | plan expiry, epoch seconds |

`Execute` is unchanged: `(s(sasa(ssas))a(ssss)as)`. `Cancel` is unchanged. A refusal returns an empty preview (all lists empty, `false`, zeros) like `PreparePlan`. An argument name check (rule H-007) stays satisfied: the only input is `options`.

Names that the XML documents: `PreparePlan` with action `upgrade` returns `InvalidArgument`.

### 6.2 Wire types

Helper `WireTypes.h/.cpp` adds `UpgradeItemWire (ssss)`, `ReplacementWire (sssss)`, `NewDependencyWire (sss)`, `HeldBackWire (ss)` and `UpgradePreviewWire`, with `operator<<`, `operator>>`, registration and `toWire(const ResolvedPlan&, expires_at)` for upgrade plans. `StatusWire` is unchanged. `HelperWire.h/.cpp` in the GUI gets the same copies and `decodePrepareUpgradeReply`. A test keeps the two copies equal, as it does today. `HelperService::prepareUpgrade` mirrors `preparePlan` and watches the caller. `HelperAdaptor::PrepareUpgrade(options, message)` sets a delayed reply.

`kProtocolVersion = 2` in `HelperIdentity.h`. `kUpgradeActionId` is added there. `HelperService` also journals the outcome of every `Execute` (plan ID, outcome, code), which REQ-F-059 needs when the owner has gone.

### 6.3 Progress encoding — OQ-U1

The `Progress` signature `(plan_id s, phase s, current u, total u, item s)` does not change.

- New phase tokens: `Syncing`, `Promoting`, `KeyringStep`, `MainStep`. A version 1 client never sees them, because it never calls `PrepareUpgrade`.
- `Syncing`: `plan_id = ""`, `current = i` (repositories completed), `total = n` (enabled repositories), `item` = repository name. One signal per repository.
- Step markers: when a step starts, the helper sends one marker signal, `KeyringStep` (1, 2) before the keyring transaction, and `MainStep` (2, 2), or (1, 1) with no keyring step, before the main transaction. `item` is empty. The other signals of the step (`Verifying`, `Hooks`, `Installing`, `Removing`) keep their version 1 meaning of `current` and `total`. Install and remove signals are unchanged.
- The GUI keeps the last marker it received and shows "Step i of n" next to the phase. `ProgressUpdate` and `ProgressView` gain `step_index` and `step_count`; the flow fills them from the marker, not from the wire on every signal.

Alternatives rejected: packing the step into the high bytes of `current` and `total` (works, but obscure on the wire), a text prefix in `item` (string parsing, fragile) and new arguments (changes the signature, REQ-C-002).
### 6.4 Status codes in the wire taxonomy

Five codes are added (section 4.1): `SyncFailed`, `SyncDatabaseChanged`, `KeyImportRequired`, `UpgradePartiallyApplied` and `Cancelled`. `NothingToDo` is reused. `transactionErrorName` and `parseTransactionErrorCode` cover them. The XML comment lists them. The GUI decoder maps an unknown code to `InternalError` as before (REQ-F-047 of the GUI SPEC). A test compares the SPEC's taxonomy table, the XML comment and `kTransactionErrorCodeCount` (REQ-F-068).

**OQ-U3 resolved: keep `ConfigurationError`.** Reasons: REQ-F-076 is approved with that code; REQ-F-018 already uses it with a repository name; the GUI shows one message for both ("The pacman configuration cannot be used for a system update: %1"), where `%1` is a fixed option token or a repository name taken from the reply. A dedicated code would add a wire code, a message row and a SPEC change for no behaviour the user could act on differently.

### 6.5 Authorization and policy (REQ-F-039 to REQ-F-041, REQ-C-005)

`org.holonight.packages.upgrade` is added to `org.holonight.packages.policy` with `auth_admin_keep` for `allow_any`, `allow_inactive` and `allow_active`. Message: "Authentication is required to update the system: $(upgrades) upgrades, $(replacements) replacements, $(new) new packages". `PlanService` sends details `{upgrades, replacements, new}` as decimal strings, no package name. `check-helper-artifacts.py` reads the new constant from `HelperIdentity.h`, the new default from `plan_service.h` and the new action from the policy file, and fails when one is missing. It also pins the method list to `PreparePlan`, `PrepareUpgrade`, `Execute`, `Cancel` (REQ-F-072). The install and remove actions are not edited.

### 6.6 GUI-side ports

Domain `TransactionService` gains:

```cpp
using ProtocolDone = std::function<void(std::optional<std::uint32_t>)>;
enum class ProbeMode : std::uint8_t { NoAutoStart, AutoStart };
virtual void queryProtocolVersion(ProbeMode mode, ProtocolDone done) = 0;   // nullopt: unknown
```

`preparePlan(PlanRequest{Upgrade, {}}, done)` is the upgrade call; the adapter maps it to `PrepareUpgrade` with an empty options map. `cancel("", done)` cancels a preparation. `PackageListRefresh` gains `refreshAfterUpgrade()`; `ModelListRefresh` implements it as: refresh Installed, reload Explore (single-flight rules as today), `UpdatesModel::reload()`, and `UpdateCheckModel::checkNow()` (which calls `CheckNow()` over the session bus; the GUI never checks itself, rule C-009). The flow calls it after every outcome of an upgrade, including refusals, `Cancelled` and `NothingToDo` (REQ-F-088).

## 7. Operations: unit, state directory, configuration

### 7.1 State directory

`HelperConfig` gains `state_directory` (`--state-dir`, absolute, default `/var/lib/holonight-package-helper`). It is helper-only configuration and not part of `TransactionConfig`, so rule H-007 is unchanged.

`StateDirScratchRoots` (`apps/helper`, standard library only):

- `create()`: `mkdtemp("<state>/upgrade-XXXXXX")`, `chmod 0711`, owner check (`geteuid()`). The directory name is random and unrelated to the plan ID. The path is never returned to a caller (REQ-F-008).
- `remove()`: `std::filesystem::remove_all` (does not follow symlinks; the `local` link is unlinked, not entered), retried up to three times on `ENOTEMPTY`.
- `removeAll()`: remove every `upgrade-*` entry of the state directory.

`main.cpp` calls `removeAll()` and the `.hn-promote-*` sweep before `service.start()`, so the first call after a start finds nothing (REQ-F-015, REQ-F-016).

### 7.2 Plan limits

`PlanService::prepareUpgrade` checks slot and capacity (`hasRoomFor`, 16 per UID, 64 total) before it calls `ScratchRoots::create()`, so a `Busy` creates no directory (REQ-F-004, REQ-F-005).

### 7.3 Client timeouts

`DBusTransactionService::Timeouts` gains `prepare_upgrade_ms = 600'000`. Execute stays at one hour. Cancel keeps the default.

### 7.4 systemd unit (REQ-C-008)

Added to `holonight-package-helper.service.in`:

```
StateDirectory=holonight-package-helper
StateDirectoryMode=0711
```

Nothing else changes. The unit has no `ProtectSystem`, so the whole file system stays writable and no `ReadWritePaths` is needed for `/var/lib/pacman` or `CacheDir`. Adding `ProtectSystem` would break pacman hooks and scriptlets (the reason the list is minimal). `check-helper-artifacts.py` adds `StateDirectory` and `StateDirectoryMode` to the required keys and keeps `ReadWritePaths` forbidden. A comment in the unit names the change. `systemd-analyze verify` runs in the install test.

## 8. Spikes

Each spike is a throwaway program built against the `test` preset support code (`tests/support/FixtureRoot`, `RepoBuilder`, `gpg_keyring`). It is not committed to the product. Its result is written to `docs/sdd/system-upgrade/SPIKES.md` before the dependent task starts. Signature spikes run one per process (`SKIP_UNLESS_FRESH_GPGME`).

### 8.1 S-1: `DOWNLOADONLY` on a scratch dbpath with a shared `local`

Program: `spike_s1`. World: `FixtureRoot` with repositories `core` and `extra` at v1 in the live `sync/` and at v2 on the file:// servers (live mtime set one day before the server file). Build the scratch layout of 5.3. Open a handle over it. Use `TransactionSeams::fetch` to hold a download and list the world while it is held.

Observe:

- (a) Where `db.lck` appears during `alpm_db_update` and during a `DOWNLOADONLY` commit. Expected: `S/dbpath/db.lck`. Does `alpm_db_update` need the lock at all?
- (b) Which directories receive writes. Compare manifests (SHA-256, mtime) of the live `sync/`, live `local/` (through the symlink), root, gpg directory and cache before and after. Expected: only `S` and `CacheDir`.
- (c) Package signature checks in a `DOWNLOADONLY` commit. Fixtures: good `.sig`, bad `.sig`, missing `.sig`, signature by a key outside the keyring. Record errno, log lines and questions asked, with package siglevel Required and with Optional.
- (d) Whether the disk-space event (`ALPM_EVENT_DISKSPACE_START`) or any hook runs in `DOWNLOADONLY`.
- (e) `alpm_db_update` on a seeded scratch: returns 1 for an unchanged file and 0 for a newer one; what happens when both mtimes fall in the same second (file:// If-Modified-Since); whether `alpm_initialize` touches the `local` link target.
- (f) The `DownloadUser` sandbox with a root-owned `0711` scratch root. Run as root in a disposable container with a real `alpm` user (the `tests/transaction/userns_main.cpp` harness cannot map a second user). Can the sandboxed downloader write `S/dbpath/sync`?

Decisions:

| Outcome | Decision |
|---------|----------|
| (a) scratch lock only, (b) clean | Design as written. |
| (a) the lock appears in the live dbpath | Stop: the scratch handle would block other package managers during the fetch. Fall back to a scratch fetch with `NOLOCK` flags and a plain download loop is not possible (NOLOCK cannot commit). Then switch the fetch phase to the live lock (phase 7 before phase 4) and move the guard before the fetch. Provisional until this is known. |
| (b) `local` link not usable or written | Use a read-only copy of `local/` in `S`. The copy is refreshed under the live lock before phase 9. The guard and the re-resolution stay. |
| (c) `DOWNLOADONLY` verifies (observed) | The fetch handle keeps the configured level so `.sig` files reach the cache; a key-missing failure in the keyring check is tolerated only with a keyring step and phase 5 decides (D-6, revised after SPIKES S-1 c). |
| (c) `DOWNLOADONLY` does not verify | The verifier of phase 5 is the only check at fetch. No design change. |
| (d) libalpm runs the space check in `DOWNLOADONLY` | Use its result in phase 3 instead of the `statvfs` estimate. |
| (e) mtime ties break If-Modified-Since | Tests set explicit mtimes (a day apart). If production hits it, `alpm_db_update` is called with `force = 1` for repositories whose server file is newer; the seed copy is dropped. |
| (f) sandbox writes `S/dbpath/sync` | Design as written (D-9). |
| (f) sandbox still cannot write there | Inspect what libalpm needs (ownership of its temporary download directory, Landlock path). Widen only the scratch `dbpath/sync` permissions as needed. The sandbox is never switched off; if no permission setting works, stop and bring the question back to the user. |

Fallback (SPEC): if the scratch handle cannot share `local` through a symlink, the scratch root holds a read-only copy of `local/` with the same guard.

### 8.2 S-2: keyring update visible to gpgme in the same process

Program: `spike_s2`. Fixtures: a gpg directory with key A. Package `app 2` signed by key B (not in the keyring). Package `archlinux-keyring 2` whose "installation" is simulated by the `package-done` seam importing key B into the gpg directory (the same effect as `pacman-key --populate`). Run, in one process:

1. Verify `app 2`: expect key-missing.
2. Commit the keyring package in a transaction, release it.
3. Verify and commit `app 2` through (i) a new `alpm_handle_t` and (ii) a second transaction on the same handle.

Then repeat step 3 with the helper re-exec variant (iii): a child process that loads the keyring afresh. Run gpg 2.4 with `keyboxd` enabled and disabled.

Decisions:

| Outcome | Decision |
|---------|----------|
| (i) or (ii) passes | In-process main step, as designed. Prefer (i), which matches the existing "one handle per operation" rule. |
| Only (iii) passes | The main step runs in a child process started with `posix_spawn` of the helper executable in a `--main-step` mode, with the plan passed over a pipe and the result read back. This needs an exclusion in rule H-003 for one file (`src/transaction/src/step_process.cpp`) and an extra design iteration. Mark the whole of phase 13 provisional. |
| None passes | SPEC fallback: after the keyring step the driver stops and returns `UpgradePartiallyApplied` with step token `keyring`. The GUI message tells the user to press "Update system…" again. Rerunning works because the next helper process loads the new keyring (REQ-F-063). |

### 8.3 S-3: promotion by rename or by copy

Program: `spike_s3`, run in a mount namespace (`unshare -Urm`) with two tmpfs mounts, one for the scratch root and one for the live `sync/`. Observe:

- `rename` across the mounts returns `EXDEV`; the copy-then-rename path works and keeps the mtime.
- Rename inside one mount keeps inode and mtime.
- A reader that opens the live database in a loop during promotion (libalpm `alpm_db_get_pkgcache`) sees either the old or the new file, never a mix.
- Kill the process (`SIGKILL`) after the temporary copy and before the rename: the temporary file stays, the live file is unchanged, and the start-up sweep removes the temporary file.
- A live handle that registered the databases before promotion loads the new content when it first reads (the assumption of phase 12).
- Whether libalpm ignores an unknown file such as `.hn-promote-core.db.tmp` in `sync/`.

Decisions: default layout (both under `/var/lib`) is expected to be one filesystem, so rename is the normal path. If a stale handle caches the old content, phase 7 opens its live handle *after* promotion for the steps and holds the lock with a separate empty-registration handle. The post-promotion re-hash stays whatever the outcome.

### 8.4 S-4: internal sync timeout and scratch size cap

Program: `spike_s4`. Inputs: the sizes of the compressed `core`, `extra`, `multilib` databases and signatures from a real mirror (measure once, on the VM of the manual checklist); a local throttled server (`curl --limit-rate` style proxy or `tc`) at 8 Mbit/s, 1 Mbit/s and 128 kbit/s; a download callback log at each speed. Observe: total time, libalpm's low-speed abort (stall detection) behaviour, and the byte gap between two `dlcb` progress events at gigabit speed (this bounds the overshoot of the cap).

Proposed values (to be confirmed or lowered by the measurements):

| Value | Proposal | Rationale |
|-------|----------|-----------|
| Internal sync timeout | **480 s** | Under the 600 s client timeout with 120 s margin for the delayed reply, seeding, hashing, resolution and the reply latency. At 1 Mbit/s the official databases (about 10 MB) take well under 120 s, so 480 s covers a very slow link. libalpm's own low-speed abort ends a true stall long before. |
| Scratch size cap | **256 MiB** | About 25 times the size of the official databases plus signatures, so third-party repositories fit. Small against `/var/lib`. Counted as seeded bytes plus bytes downloaded. |
| Cap check | At every `dlcb` event, and at the first event of a file whose `total` is known | Stops an oversized file before it is transferred when the server sends a length. Overshoot with unknown length is bounded by the event gap (expected a few MiB). If S-4 measures a gap above 8 MiB the cap is lowered so cap plus gap stays below 256 MiB. |
| Fetch phase timeout | none | `Execute` has a one-hour client timeout and is cancellable until promotion. libcurl's low-speed abort handles stalls. |

If the spike shows the official databases are larger than expected the cap rises; the timeout does not (it is bound by the 600 s limit). Both live in `UpgradeLimits` (5.1).

Measured (SPIKES S-4): both values are confirmed. Known limit: with today's `extra.db` (9.1 MB) a link slower than about 160 kbit/s cannot finish the sync within 480 s and gets `SyncFailed`.

## 9. Graphical interface

### 9.1 Flow changes (`TransactionFlow`, `src/application`)

- `request(PlanRequest{Upgrade, {}})` starts `Preparing` like install and remove. `FlowAction` gains `UpdateSystem`.
- In `Preparing` of an upgrade, `Progress` is applied (the `Syncing` view: `current`, `total`, `item`) and `actions()` is `{Cancel}`. `cancelRun()` calls `service.cancel("")` (REQ-F-080). A `Cancelled` prepare reply moves to `Cancelled` with Close only.
- `NothingToDo` for an upgrade moves to `Refused` with Close only and the up-to-date text (REQ-F-081). No preview is built.
- `Previewed` uses `PreparedPlan.plan.upgrade`. `confirm()` calls `execute(plan_id)` once (REQ-F-084). `NotAuthorized` returns to `Previewed` with the existing note, and Confirm calls `execute` with the same plan ID (REQ-F-090).
- The cancel latch for upgrades ends at the first `Promoting` update (REQ-F-085). `closeRequested()` returns `Ignore` after that (REQ-F-086). A late `PastPointOfNoReturn` reply is shown as an outcome as the GUI SPEC REQ-F-036 requires.
- `ProgressView` gains `step_index` and `step_count`.
- `SyncDatabaseChanged`: actions `{TryAgain, Close}`; `tryAgain()` repeats `PrepareUpgrade` (REQ-F-091). `Busy`, `PlanNotFound`, `PlanExpired` keep Try again (REQ-F-092). Every other upgrade code gets Close only (REQ-F-093).
- `Refused` with `PartialUpgradeRequired` for an install gets actions `{UpdateSystem, Close}`. `startUpgradeFromRefusal()` goes to `Idle` and calls `request(Upgrade)` in one step. It does not retry the install (REQ-F-089).
- `allowNewRequest()` is false during an upgrade flow, so the Install and Remove buttons stay disabled (GUI SPEC REQ-F-029).
- `refreshAfterUpgrade()` is called once per upgrade outcome.

### 9.2 View-model

`TransactionController` gains:

- `Q_INVOKABLE void requestUpgrade()`, `Q_INVOKABLE void updateSystemFromRefusal()`, `Q_INVOKABLE void probeHelper()`.
- `bool upgradeEnabled` (button state), `QString upgradeDisabledReason` (empty, or the `HelperTooOld` text), `bool upgradeFlow`, `QString stepText` ("step i of n", empty outside the steps), `QString syncText` ("Syncing repository i of n"), `QString keyringNotice` (empty or naming the keyring packages), `QString downloadSizeText`, `QString sizeChangeText`.
- `previewRows` carries the upgrade rows. `TransactionRowModel::Row` gets `kind` values `upgrade`, `replacement`, `newdependency`, `heldback` and a `ReplacesRole` (old package name, empty otherwise). Rows are ordered upgrades, replacements, new dependencies, held back. The model is replaced as a whole. A 1000 row model builds in one pass (REQ-NF-007).
- Protocol check: `probeHelper()` runs on the first show of the Updates page and after each outcome. It calls `queryProtocolVersion(NoAutoStart)`, so it does not start the root helper just because a page opens. A running helper below version 2 disables the button with `HelperTooOld`. An unknown result (helper not running) leaves the button enabled. On the click, `requestUpgrade()` first calls `queryProtocolVersion(AutoStart)`. Below 2 it shows `HelperTooOld`, stays disabled and never calls `PrepareUpgrade` (REQ-F-079). From 2 on it starts the flow.

`TransactionMessages` adds the keys of REQ-F-094 as fixed `qsTr` entries: `HelperTooOld`, `Syncing`, `SystemUpToDate`, `Upgrade.Preview`, `HeldBack`, `Keyring`, `UpgradePartiallyApplied.keyring`, `UpgradePartiallyApplied.main`, `SyncFailed`, `SyncDatabaseChanged`, `KeyImportRequired`, `NothingToDo.Upgrade`, plus `Cancelled` and `ConfigurationError` for upgrades, and the `PartialUpgradeRequired` text with the new button. `failureText` gets 29 rows (one per code). Package names, versions, repository names and the step token are inserted as plain text; the helper's strings are never used otherwise. The partial-upgrade text names the step and tells the user to press "Update system…" again; the keyring note is appended when the changed list contains `archlinux-keyring`.

### 9.3 QML

- `qml/updates/UpdatesView.qml`: a `Controls.Button` `updateSystemButton` ("Update system…") in the check bar next to Reload, `property var transactionController: null` (optional like `updateCheckModel`), `enabled: controller !== null && controller.allowAction && controller.upgradeEnabled`, and a `Controls.ToolTip` showing `upgradeDisabledReason`. It is not bound to the list state, so it is enabled for an empty list (REQ-F-078). `WorkspaceWindow.qml` passes the controller.
- `qml/transactions/TransactionDialog.qml`: `width` is 640 for the upgrade pages. The preparing page gets the sync text and a Cancel button for upgrades.
- New `UpgradePreviewPage.qml`: a `ListView` over `previewRows` with `section.property: "kind"` headers, `clip: true`, `Controls.ScrollBar.vertical: Controls.ScrollBar {}`, fixed delegate height, no checkbox or selection control. Above the list: the keyring notice (visible when set), download size, size change and expiry. Below: Confirm and Cancel. New `UpgradeSectionHeader.qml` and `UpgradeRow.qml` (name, versions, repository, "replaces X").
- `TransactionProgressPage.qml`: shows `stepText` above the phase label when set.
- `TransactionResultPage.qml`: the buttons come from `controller.actions`. The new id `updateSystem` shows "Update system…".

All QML follows the import rules of AGENTS.md (`QtQuick.Controls as Controls`, qualified instances and attached properties, `Holonight.Core`, `Holonight.Controls`). The style is not set in code.

## 10. Test strategy

Rules for all tests: fixture roots in a temporary directory only; `file://` servers only; no pointer or focus automation; signature tests call `SKIP_UNLESS_FRESH_GPGME()` and rely on ctest running one process per test (REQ-C-003, REQ-C-004, REQ-NF-003). Fixture mtimes: live databases are written one day before the repository databases.

### 10.1 Support code

- `RepoBuilder::publishRepositoryOnly()`: writes the server database without copying it to the live `sync/` (today `publish()` copies it). A v1/v2 helper `FixtureUpgrade` installs v1 in the live world and publishes v2 on the server.
- `FixtureRoot` gets `stateDir()` and a manifest of `sync/` and `local/` (the existing `manifestOf`).
- `FakeScratchRoots` (records `create`, `remove`, `removeAll`; can hold an existing directory), extended `FakeTransactionEngine`, extended `FakeTransactionService` (protocol version, upgrade replies, `Syncing` events, held calls), `FakePackageListRefresh::refreshAfterUpgrade` counter.
- New `TransactionSeams` stages (production never sets seams, rule H-008). `observe`: `sync-started`, `synced`, `fetch-started`, `fetched`, `lock-taken`, `guard-checked`, `promotion-starting`, `promoted`, `commit:keyring`, `commit:main`, `keyring-step-done`, `main-step-started`, `disk-space-check`. `inject_fault` (non-zero stops there): `promotion-rename-<k>`, `keyring-commit`, `main-package-done`, `disk-space`, `post-promotion-verify`. New hooks: `same_filesystem(a, b) -> std::optional<bool>`. The existing `fetch` seam holds, counts or corrupts downloads for both the sync and the packages (the URL tells which). A cap or timeout test sets `UpgradeLimits` in the `TransactionConfig`.
- The event log of REQ-F-055 is the ordered list of `observe` stages recorded by the test.

### 10.2 Layers

| Layer | Binary / directory | Covers |
|-------|--------------------|--------|
| Qt-free core | `test_holonight_transaction`: `upgrade_prepare_test.cpp`, `upgrade_execute_test.cpp`, `upgrade_resolver_test.cpp`, `sync_promotion_test.cpp`, `guard_snapshot_test.cpp`, `scratch_dbpath_test.cpp`, `sync_refresher_test.cpp`, `package_verifier_test.cpp`, `upgrade_signature_test.cpp` (fresh gpgme), `pacman_diagnostics_test.cpp` | REQ-F-006 to REQ-F-012, REQ-F-017 to REQ-F-031, REQ-F-042 to REQ-F-064, REQ-F-075 to REQ-F-077, REQ-NF-002, REQ-NF-004, REQ-NF-006 |
| Domain | `tests/domain` | plan equality (REQ-F-025), code taxonomy (REQ-F-068), request validation (REQ-F-002), progress phases |
| Application | `tests/application/plan_service_upgrade_test.cpp`, `plan_store_test.cpp` | slot and limits (REQ-F-004, REQ-F-005), authorization (REQ-F-001, REQ-F-040, REQ-F-041), cancel and disconnect (REQ-F-032, REQ-F-033, REQ-F-035, REQ-F-036), expiry (REQ-F-013, REQ-F-037), busy (REQ-F-038), scratch lifecycle (REQ-F-013, REQ-F-014), terminator once after reply, flow tests (REQ-F-078 to REQ-F-093) |
| Helper service on a private bus | `tests/helper`: `helper_upgrade_dbus_test.cpp`, `wire_types_test.cpp`, `state_dir_scratch_roots_test.cpp`, `journal_terminator_test.cpp` | reply shapes and no leaked text (REQ-F-008, REQ-F-069, REQ-F-073), targeted `Progress` (REQ-F-034), responsiveness (REQ-NF-001), start sweep (REQ-F-015), killed helper (REQ-F-016: a forked child holds the sync through the seam and is killed with `SIGKILL`; the parent runs the sweep) |
| GUI client | `tests/helper_client` | adapter on a fake helper object: `PrepareUpgrade`, probe, 10 minute timeout, v1 XML signatures golden file (REQ-C-002) |
| View-model and QML source | `tests/transactions` | controller properties, row model with 500 and 1000 rows (REQ-NF-007), message table completeness (REQ-F-094), button states |
| Dual-style runtime | `tests/runtime/test_transaction_dialogs.cpp` | preview with 500 rows and progress with step counter in Holonight and Fusion without QML warnings (REQ-F-083, REQ-NF-005) |
| Policy | `tests/policy/test_helper_artifacts.py`, `test_layering_policy.py`, `layering_policy_fixtures` | new action, unit keys, method list, amended H-004/H-006, new H-013 (REQ-C-005, REQ-F-071, REQ-F-072) |
| Install check | `tests/helper/check_install.cmake` | `systemd-analyze verify`, unit and policy installed (REQ-C-008) |
| Manual (VM) | `apps/helper/README.md` section | the four items of the SPEC, plus a `DownloadUser` sandbox run for S-1(f) (REQ-C-009) |

### 10.3 Mapping of key acceptance criteria

- Live databases unchanged on every refusal (REQ-F-007, REQ-NF-006): a test helper takes `manifestOf(sync)` and `manifestOf(local)` before the call and compares after, for success, `SyncFailed`, `NothingToDo`, each refusal, and each pre-promotion failure.
- Download held (REQ-F-004, REQ-F-032, REQ-F-049, REQ-NF-001): the `fetch` seam blocks on a condition variable. The test makes the second call, the property read or the cancel, and then releases.
- Timing (REQ-F-003, REQ-NF-003): `UpgradeLimits{sync_timeout = 1s}`; the bus-level test expects `SyncFailed` within 6 seconds. The 300 s lifetime uses the existing fake clock.
- Cap (REQ-F-010): `scratch_cap_bytes = 1024` against a 4 KiB database. With the default cap the three-repository fixture passes.
- Promotion faults (REQ-F-054): `promotion-rename-2` of 3 files. All three files equal their old hashes afterwards.
- Cross filesystem (REQ-F-077): the `same_filesystem` seam. Interrupted copy: fault `promotion-rename-1` after the temporary exists; the sweep removes it.
- Keyring order (REQ-F-055): the stage log shows `commit:keyring` before `commit:main`. The local database is read in `main-step-started`.
- Past the point of no return (REQ-F-058, REQ-F-059): the seam `promotion-starting` calls `Cancel`, then drops the owner's connection.
- Partial upgrade (REQ-F-060 to REQ-F-063): `keyring-commit` and `main-package-done` faults, then a second `PrepareUpgrade` that lists only the rest.
- Replacements and held-back (REQ-F-020, REQ-F-021): `PackageSpec::replaces` and `IgnorePkg` in `ConfSpec::extraOptions`.
- Terminator (REQ-F-032, REQ-F-049): `RecordingTerminator` checks one call after the reply. A separate child-process test (like `lock_finalization_child`) checks that `ownedLockPath()` removes the scratch lock and leaves a foreign live lock (F-2).

## 11. Parts that were provisional until a spike passed

All spikes ran on 2026-11-12 (SPIKES.md). Status of each part:

| Part | Depends on | Status |
|------|-----------|--------|
| Lock placement and scratch-only writes (phases 4 to 6, 5.7) | S-1(a), S-1(b) | Confirmed. Only the scratch `db.lck`; only the scratch root and `CacheDir` are written. |
| `local` symlink (5.3) versus read-only copy | S-1(b) | Confirmed: symlink. |
| Fetch handle package level and `PackageVerifier` details (phases 4 and 5) | S-1(c) | **Changed:** the fetch keeps the configured level so `.sig` files are downloaded; a key-missing failure is tolerated only with a keyring step (D-6, F-6, phase 4). |
| Disk-space check source (phase 3) | S-1(d) | Confirmed: `statvfs` estimate; libalpm runs no space check in `DOWNLOADONLY`. |
| Seeding and If-Modified-Since use (5.3) | S-1(e) | Confirmed, with the test rule: server and seed mtimes at least a second apart. |
| Sandbox handling for the database sync (D-9) | S-1(f) | Confirmed with `0711` on all four levels (**changed** from 0755 for `dbpath` and `sync`). |
| In-process main step (phases 12 and 13) | S-2 | Confirmed: variant (i). |
| Rename versus copy, stale handle after promotion (5.8, phase 7) | S-3 | Confirmed, with the explicit rule that the live handle reads no sync database before promotion ends. |
| 480 s, 256 MiB and the cap check granularity (5.1) | S-4 | Confirmed. Known limit below about 160 kbit/s. |

## 12. Key decisions, alternatives, risks

### 12.1 Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| D-1 | Extend `TransactionEngine`, `PlanService`, `PlanStore`, `TransactionFlow`, `ExecutionContext` and `TransactionSeams`. No second service stack. | The SPEC wants the same lifecycle. One plan store, one work slot, one terminator. |
| D-2 | Preview and equality live in `ResolvedPlan::upgrade`, not in `changes`. | The five lists have different shapes. Equality is defined on them (REQ-F-025). `ExecutionDriver` is not affected. |
| D-3 | The scratch root is created and removed through the `ScratchRoots` port, and removal is hooked to the plan store's discard path. | REQ-C-001 puts directory creation in `apps/helper`. One chokepoint covers expiry, cancel, disconnect and execute. |
| D-4 | Cancel of a preparation, a sync timeout and a size cap all end in "reply, then terminator". | libalpm cannot stop a download; a stuck worker would hold the work slot forever. |
| D-5 | `PrepareUpgrade` uses an explicit `Cancelled` status code. | The reply has no outcome field. Section 14, item 1. |
| D-6 | The fetch handle keeps the configured package level (so the `.sig` files are downloaded). Its key-missing failure is tolerated only when a keyring step exists and all files are cached; `PackageVerifier` then checks every file and defers key-missing for non-keyring packages. | Otherwise a package signed by a key that only the new keyring knows fails before the keyring step could help (F-6). The final check still happens in the main step with the new keyring. |
| D-7 | The live handle of phase 7 registers each repository with one sentinel server, `file:///nonexistent/holonight-package-helper/no-download`, instead of the configured ones. | Any download in the main step then fails by construction, with no reliance on `alpm_option_set_fetchcb` (rule H-004 stays strict) (REQ-F-056). Revised during implementation: libalpm's `find_dl_candidates` refuses a commit for a repository with no server at all (`ALPM_ERR_SERVER_NONE`) before it looks at the cache, so "no servers" would fail every main step. |
| D-8 | Step counter is sent as one marker signal per step (`KeyringStep` or `MainStep`, `current = i`, `total = n`). Other signals keep their version 1 meaning. | Keeps the `Progress` signature (REQ-C-002) with no bit packing; the GUI keeps the last marker (REQ-F-057). |
| D-9 | libalpm's `DownloadUser` sandbox stays on for the database sync and the fetch. The state directory and the scratch root are `0711` root, so the sandbox user can traverse but not list them. | Keeps the downloader out of root (user decision 2026-11-12). Moving the downloads into a bubblewrap or systemd-run worker process is a follow-up review (`docs/tech-debt.md`). |
| D-10 | `PrepareUpgrade` returns `Busy` if the live `db.lck` exists. | Another package manager is running. Cheap. Consistent with the update check. |
| D-11 | The protocol version is probed without auto start on the page, and with auto start on the click. | A root service should not start because a page opened. The old-helper message stays reachable. |
| D-12 | Held-back, replacement and new-dependency classification uses libalpm calls (`alpm_sync_get_new_version`, `alpm_pkg_should_ignore`, `alpm_pkg_get_replaces`). No version comparison is written. | Rules H-005 and F-007. |
| D-13 | Promotion order: `.sig` before `.db`, per-file atomic, rollback from `S/rollback`. | REQ-F-053, REQ-F-054. |
| D-14 | `OQ-U4` (`PackageNotFound`) and `OQ-U6` (re-resolution after the guard) are confirmed. | Section 5.4 and phase 9. |

### 12.2 Alternatives considered

| Alternative | Why not |
|-------------|---------|
| **Re-sync at execute** (download the databases again after the authorization, instead of promoting the scratch ones) | The user confirmed a preview that is a function of the scratch databases. A second sync can give a different set. The design promises "exactly the previewed plan". Promotion also saves one download. |
| **Overloading `PreparePlan`** with action `upgrade` | The reply timeout, the cancel semantics (`Cancel("")`), the progress to a caller without a plan ID and the preview type all differ. REQ-F-072 names the methods, and the SPEC fixes `PrepareUpgrade`. `PreparePlan("upgrade")` is refused. |
| **Async `PlanReady` signal** instead of a delayed reply | Adds a signal and a correlation scheme for a call that already has a delayed-reply pattern in `PreparePlan` and `Execute`. The client needs a 10 minute timeout either way. |
| **`pacman` subprocess** (`pacman -Syu`, `pacman -Sw`) | Rule H-003 and the SPEC (no external process). The preview and the equality check need libalpm's transaction objects. |
| **Sync the live databases at prepare** | Changes live state before the user agrees (REQ-F-007). A later cancel would leave the system with new databases and old packages, which is a partial upgrade risk for any later install. |
| **Hold the live lock from prepare to execute** | Blocks other package managers for up to 300 seconds. Not needed with the guard. |
| **Hash only mtime for the guard** | A touched file would trigger a false refusal; a replaced file with the same mtime would pass. SHA-256 plus size plus mtime is what the SPEC states. |
| **Fetch with `alpm_fetch_pkgurl` or a custom download loop** | Rule H-004 forbids it; libalpm's `DOWNLOADONLY` already gives resume, mirrors and parallel downloads. |

### 12.3 Risks

| # | Risk | Mitigation |
|---|------|------------|
| R-1 | A failure after promotion leaves a partly upgraded system. | Reported with the step; next run completes (REQ-F-063). Not prevented, as the SPEC says. |
| R-2 | Scratch cap overshoot between two download events. | Check `total` at the first event; S-4 measures the gap; cap chosen with headroom. |
| R-3 | A stale `db.lck` of the scratch handle after a crash. | It lives in the scratch root, which is swept at start. |
| R-4 | The live lock is dropped between the keyring transaction and the main transaction. | The window is two libalpm calls. If another process takes the lock, the main step returns `UpgradePartiallyApplied` {main} with no loss; the rerun completes. A pre-created lock cannot be used, because libalpm owns the file. |
| R-5 | S-1, S-2 or S-3 do not behave as assumed. | Fallbacks in section 8. Provisional parts in section 11. |
| R-6 | `statvfs` estimate is wrong, and libalpm's own check fails after promotion. | The estimate is conservative (5 percent margin). The failure is reported as `UpgradePartiallyApplied`, which is the SPEC's partial upgrade case. |
| R-7 | The unit file loses a directive the checker pins. | `check-helper-artifacts.py` pins `StateDirectory` and keeps the forbidden list (REQ-C-005, REQ-C-008). |
| R-8 | A fixture-only seam leaks into production. | Rule H-008 is unchanged. `UpgradeLimits` are plain config with fixed defaults, no CLI flag. |
| R-9 | Seeded copy of a tampered live database. | libalpm verifies database signatures when it loads them at the configured level. A bad file fails with `DatabaseUnavailable`. |
| R-10 | Concurrent `pacman -Sy` during prepare changes files between seeding and guard. | The stamp re-check of 5.3; a change after prepare gives `SyncDatabaseChanged` at execute. |

## 13. Layering policy and artifact changes

- **H-004** (`check-layering.py`): split. `alpm_syncdb`, `alpm_fetch_pkgurl` stay banned in `apps/helper` and `src/transaction`. `alpm_db_update` is allowed only in `src/transaction/src/sync_refresher.cpp`. `alpm_option_set_fetchcb` stays allowed only in `transaction_seams.h` and `fetch_seam.h`.
- **H-006**: drop `Upgrade` from the pattern. Keep `Answer`, `History`, PackageKit, AUR and `-U`.
- **H-013 (new)**: `SyncPromotion` and `promoteSyncDatabases` may be referenced only in `promotion.{h,cpp}` and `upgrade_driver.cpp` (REQ-F-071 source search).
- **H-001, H-002**: unchanged. New code avoids the banned words.
- `tests/policy/test_layering_policy.py` gains fixtures: `alpm_db_update` in another transaction file fails, in `sync_refresher.cpp` passes; a `PrepareUpgrade` method now passes H-006; a `RescanDatabases` method still passes only if unlisted.
- `scripts/check-helper-artifacts.py`: new constant, new policy action, required unit keys, method list.

## 14. Problems found in the SPEC

1. **REQ-F-032 and REQ-F-068.** `PrepareUpgrade` has no outcome field, so `Cancelled` must be a status code. The REQ-F-068 list and the taxonomy table should add `Cancelled` (valid for `PrepareUpgrade` only). The design does it.
2. **REQ-F-048.** libalpm has no stand-alone disk-space check and `DOWNLOADONLY` does not run it. The design uses a `statvfs` estimate before promotion and libalpm's own check inside each step. The acceptance criterion about "libalpm's check" should name the estimate.
3. **REQ-F-029 and REQ-F-047.** An unknown signing key appears in both. The design uses: key missing is `KeyImportRequired`; bad or missing signature is `SignatureInvalid`. The REQ-F-047 test fixture should use a corrupt `.sig`, not an unknown key.
4. **REQ-F-008 and the `DownloadUser` sandbox.** Resolved: the SPEC now requires mode `0711` (F-5, D-9).
5. **REQ-F-060.** "An empty changed list" holds when the keyring commit fails before any package change. If libalpm changed some keyring dependency first, the design lists it (the same rule as REQ-F-061).
6. **REQ-F-051.** "Any file differs, or a file has been added or removed" is read as the `*.db` and `*.db.sig` files. `pacman -Fy` files are outside the guard.
7. **REQ-C-008.** The SPEC mentions `ReadWritePaths` through the task text. The unit has none by design (F-9).
8. **REQ-F-003.** On timeout the design also ends the helper process after the reply (D-4). The SPEC does not say it, but the work slot would otherwise stay busy.
9. **Repository files that are no longer configured** are neither promoted nor removed (5.8). The SPEC is silent.

## 15. Resolution of the open questions

| OQ | Resolution |
|----|-----------|
| OQ-U1 | One marker signal per step: `KeyringStep` (1, 2), `MainStep` (2, 2) or (1, 1); other signals keep their version 1 meaning; new phases `Syncing`, `Promoting`, `KeyringStep`, `MainStep`; `Syncing` uses `current = i`, `total = n`, `item = repository`, empty plan ID (6.3). |
| OQ-U2 | Refused: `XferCommand`, `DBPath` and `RootDir` when different from the helper's values, `Usage` other than `All`. Honoured: `ParallelDownloads` and the options already applied. Ignored by design: `CleanMethod` and the cosmetic and file-installation-only options (5.2). |
| OQ-U3 | Keep `ConfigurationError` (6.4). |
| OQ-U4 | Confirmed: `PackageNotFound` naming the dependency (5.4). |
| OQ-U5 | Preview type `(a(ssss)a(sssss)a(sss)a(ss)bastxx)` (6.1). |
| OQ-U6 | Confirmed: phase 9 re-resolves after the guard and before promotion. Differences give `PlanChanged`. |

## 16. Traceability

| Requirements | Components | Tests |
|--------------|-----------|-------|
| REQ-F-001 to REQ-F-005 | `PlanService::prepareUpgrade`, `validateUpgradeRequest`, `PlanStore` limits, `HelperService::prepareUpgrade` | `plan_service_upgrade_test`, `helper_upgrade_dbus_test`, domain validation tests |
| REQ-F-006 to REQ-F-016 | `ScratchDbpath`, `SyncRefresher`, `StateDirScratchRoots`, `PlanStore` discard observer, start sweep in `main.cpp` | `scratch_dbpath_test`, `sync_refresher_test`, `state_dir_scratch_roots_test`, killed-child test |
| REQ-F-017, REQ-F-018, REQ-F-075, REQ-F-076 | `PacmanDiagnostics`, `AlpmSession::openUpgrade` | `pacman_diagnostics_test`, `upgrade_prepare_test` |
| REQ-F-019 to REQ-F-025 | `UpgradeResolver`, `UpgradeDetail`, `samePlan` | `upgrade_resolver_test`, domain plan tests |
| REQ-F-026 to REQ-F-031 | `QuestionPolicy` upgrade mode, `PackageVerifier`, signature check | `question_policy_test`, `upgrade_signature_test` |
| REQ-F-032 to REQ-F-038 | `PlanService` cancel, disconnect and expiry paths, `HelperTerminator`, `DatabaseLockGuard::ownedLockPath` | `plan_service_upgrade_test`, `journal_terminator_test`, `lock_finalization_child` |
| REQ-F-039 to REQ-F-041 | policy file, `AuthorizationActions`, `PolkitAuthorizer` details | `check-helper-artifacts`, `plan_service_upgrade_test` |
| REQ-F-042 to REQ-F-049 | `UpgradeDriver` phases 1 to 6, `PackageVerifier`, `FailureReporter` | `upgrade_execute_test`, `package_verifier_test` |
| REQ-F-050 to REQ-F-059 | `UpgradeDriver` phases 7 to 13, `SyncPromotion`, `GuardSnapshot`, `CommitGate` | `upgrade_execute_test`, `sync_promotion_test`, `guard_snapshot_test` |
| REQ-F-060 to REQ-F-064 | `UpgradeDriver` failure mapping, `FailureReporter` | `upgrade_execute_test` |
| REQ-F-065 to REQ-F-069, REQ-NF-004 | domain phases and codes, `WireTypes` step packing, `LogSink` | `wire_types_test`, `helper_upgrade_dbus_test` |
| REQ-F-070 to REQ-F-074, REQ-C-002 | XML, `kProtocolVersion`, artifact check | `helper_contract_test` with golden v1 signatures, install and remove suites unchanged |
| REQ-F-077 | `SyncPromotion` copy path, start sweep | `sync_promotion_test` |
| REQ-F-078 to REQ-F-094 | `TransactionFlow`, `TransactionController`, `TransactionRowModel`, `TransactionMessages`, QML pages, `ModelListRefresh`, `DBusTransactionService`, `HelperWire` | `transaction_flow_test`, `transaction_controller_test`, `transaction_row_model_test`, `transaction_messages_test`, `transaction_dialog_view_test`, dual-style runtime test |
| REQ-NF-001, REQ-NF-003 | work runner thread, `UpgradeLimits` | held-download tests, timing tests |
| REQ-NF-002, REQ-NF-006 | scratch lifecycle, cap, manifests | `sync_refresher_test`, manifest assertions |
| REQ-NF-005, REQ-NF-007 | QML pages | dual-style runtime test, row model build test |
| REQ-C-001, REQ-C-006, REQ-C-007 | module placement (section 3), layering rules | `layering_policy`, `no_qtdbus` checks |
| REQ-C-003, REQ-C-004 | fixture roots, `file://` only | rules C-006, C-010, H-012 |
| REQ-C-005, REQ-C-008 | policy, unit, artifact check | `helper_artifacts`, install check |
| REQ-C-009, REQ-C-010 | `task check`, `task check:integration`, README manual list, helper SPEC amendments | manual checklist |

### Terminal shutdown and constrained previews

A stuck or abandoned preparation, or an abandoned execution, enters the terminal work-slot state before replying
and scheduling the terminator. New preparation and execution requests cannot acquire that slot. Late worker
completion preserves the terminal state. SIGTERM keeps the event loop, service and worker dependencies alive until
the scheduled terminator finalizes the process; neither idle detection nor shutdown returns an immediate quit.

The transaction popup height is bounded by the overlay height minus its outer margins. Both page loaders propagate
the assigned content size to the preview. The upgrade list has a zero minimum height and takes the remaining space,
scrolling independently while notices, sizes, expiry and Confirm/Cancel stay visible at 720×480 in both styles.

Both upgrade actions use the activation/version probe. The install refusal remains visible during the probe, its
upgrade action is disabled until the reply, and protocol v1 shows the existing older-helper explanation without
sending PrepareUpgrade. A v2 reply starts preparation.

An installed package can be both upgraded and replace another installed package. The preview includes both entries,
counts its download once, and execution progress counts unique added names. After the keyring transaction,
NothingToDo completes the main step successfully only when the approved remainder contains no package changes;
completed changes and warnings are retained.
