# SDD Tasks — package-helper-transactions

- [x] T-001: Spike S-1, libalpm commit event order, NOLOCK lock behaviour, PreTransaction interrupt and prepare survival (time-boxed; record in docs/sdd/package-helper-transactions/SPIKES.md)
  - REQs: REQ-F-046, REQ-F-057
  - Check: SPIKES.md has an S-1 section stating the commit event order, whether HOOK_START fires with no hook, whether `db.lck` is taken or removed under NOLOCK, and whether `alpm_trans_get_add/remove` survive a failed prepare, and it names the commit point that REQ-F-057 uses.

- [x] T-002: Spike S-2, `alpm_trans_interrupt` and the SIGINT scheme for an in-flight curl transfer (time-boxed; record in SPIKES.md)
  - REQs: REQ-F-056, REQ-NF-001
  - Check: SPIKES.md has an S-2 section stating whether `alpm_trans_interrupt` aborts a file:// or HTTP transfer, whether `pthread_kill(SIGINT)` with process-wide SIG_IGN aborts it and restores the handler, and it records the outcome (immediate abort or next-file boundary) that OI-16 adopts.

- [x] T-003: Spike S-3, chroot failure for a non-root fixture run and success under `unshare(CLONE_NEWUSER)` with a static marker tool (record in SPIKES.md)
  - REQs: REQ-C-014, REQ-C-010
  - Check: SPIKES.md has an S-3 section that records the exact failure of a non-root hook run with `EPERM`, and a passing run of the same hook inside a user namespace with the static marker, and it states the uid_map lines used.

- [x] T-004: Spike S-4, exact libalpm log text and `ALPM_ERR_*` values for a 404, a missing and a mode-000 `file://` file, and a stalled transfer (record in SPIKES.md)
  - REQs: REQ-F-028, REQ-F-029
  - Check: SPIKES.md has an S-4 section listing the literal log strings for each of the four cases, the `alpm_errno_t` for each, and the `Couldn't open file` string, and it closes OI-10 for the classifier table.

- [x] T-005: Spike S-5, corrupted-package question, file removal and retry on the same handle (record in SPIKES.md)
  - REQs: REQ-F-055
  - Check: SPIKES.md has an S-5 section stating when the corrupted question is raised, whether libalpm deletes the file after a "yes" answer, whether the commit fails afterwards, and whether the same handle can retry, with the retry design of DESIGN 6.6 confirmed or changed.

- [x] T-006: Spike S-6, GPG keyring generation time and flakiness in the CI container, TrustedOnly verification with a self-generated key and the PGPSIG path in the db desc (record in SPIKES.md)
  - REQs: REQ-C-011
  - Check: SPIKES.md has an S-6 section with a measured keyring generation time, a count of successes over repeated runs, a passing `SigLevel = Required TrustedOnly` verification of a signed fixture, and the PGPSIG format used by RepoBuilder.

- [x] T-007: Scaffold the STATIC `holonight_packages_transaction` target, the `src/transaction`, `apps/helper`, `tests/helper` and `tests/transaction` directories, and the root CMake hooks
  - REQs: REQ-C-001, REQ-C-002
  - Check: `task build` exits 0 and `cmake --build build/test --target holonight_packages_transaction` produces a STATIC archive whose `LINK_LIBRARIES` (checked by a CMake-time assert) contain no Qt, Qt D-Bus or polkit target.

- [x] T-008: Add the empty test executables `test_holonight_transaction`, `test_holonight_transaction_userns`, `test_holonight_package_helper` and `test_holonight_package_helper_polkit`, with the shared `tests/support/root_guard.h` that fails when `geteuid() == 0`, and CTest TIMEOUT entries of 10 s (30 s for userns and polkit)
  - REQs: REQ-C-010, REQ-NF-006
  - Check: `ctest --test-dir build/test -N` lists the four executables with TIMEOUT properties 10 and 30 and the new root-guard unit test passes, and `grep -rn geteuid tests/support/root_guard.h` finds the UID-0 check.

- [x] T-009: Carve-out wording in AGENTS.md and CLAUDE.md, the `CARVE_OUT` constant and its fixture in `scripts/check-layering.py`, and the C-005 exclusion for `src/transaction`, in one commit
  - REQs: REQ-C-012
  - Check: `grep -c "never run package transactions" AGENTS.md CLAUDE.md scripts/check-layering.py` reports 0 for each file, both files contain the new fixture-root sentence, and `ctest --test-dir build/test -R 'layering_policy|layering_policy_fixtures'` passes with a fixture that keeps the old phrase failing.

- [x] T-010: Layering rules H-001 to H-012 of DESIGN 3.5 with one passing and one failing fixture each in `tests/policy/test_layering_policy.py`
  - REQs: REQ-C-002, REQ-C-003, REQ-C-004, REQ-C-008, REQ-C-009, REQ-C-011, REQ-F-018, REQ-F-027, REQ-F-040
  - Check: `ctest --test-dir build/test -R 'layering_policy|layering_policy_fixtures'` passes with twelve new fixture pairs, and a fixture with `alpm_db_update` in `src/transaction` or a `"Required"` literal in `apps/helper` makes the script exit non-zero.

- [x] T-011: Domain error codes (24 codes with `transactionErrorName()` and the REQ-F-030 evaluation-order table), plan, preview, progress, result and warning types, and the `TransactionEngine` and `ExecutionControl` ports with a test double
  - REQs: REQ-F-002, REQ-F-030, REQ-F-049, REQ-F-051
  - Check: `ctest --test-dir build/test -R transaction_plan` passes a case asserting the exact name string for each of the 24 codes and the seven progress phase names, a test double implementing only the port compiles and returns its exact result, and `grep -rE 'alpm|QDBus|polkit' src/domain` finds nothing.

- [x] T-012: Extend `src/backends/src/pacman_config.{h,cpp}` with HoldPkg, NoUpgrade, NoExtract, CacheDir, HookDir, LogFile, DownloadUser, DisableSandbox*, CheckSpace and ParallelDownloads, and split the libalpm-free `holonight_packages_pacman_config` STATIC target that backends links PUBLIC
  - REQs: REQ-F-019
  - Check: `ctest --test-dir build/test -R 'pacman_config|alpm_backends'` passes unmodified existing cases plus a new case where `HoldPkg = foo` parses to `foo` and `CacheDir`, `HookDir` and `GPGDir` keep their values, and `ldd` of `holonight_packages_pacman_config` shows no libalpm.

- [x] T-013: `pacman_settings` (`loadPacmanSettings(path, arch)`), `sig_level_explicit` per repository and globally, the rule that DBPath, RootDir and XferCommand are ignored, and ConfigurationError for an unreadable or unparseable file
  - REQs: REQ-F-020, REQ-F-040
  - Check: `ctest --test-dir build/test -R pacman_settings` passes: an unset SigLevel gives `sig_level_explicit` false, a repository `SigLevel = Required` overrides a global `Never`, an unparseable SigLevel token is an error, and a fixture with an unreadable file returns ConfigurationError.

- [x] T-014: `FixtureRoot` harness with the guards (root `/`, path outside the canonical temp dir, `/var/lib/pacman`, `/etc/pacman.conf`, non-file `Server`), `manifest.h` moved from the file-repo fixture with a forwarding include, and `PackageBuilder` and `RepoBuilder` for signed-or-unsigned `.pkg.tar.zst` files, sync and local db entries, and the stale, corrupt, mode-000 and newer-version mutations
  - REQs: REQ-C-009, REQ-F-027, REQ-F-044
  - Check: `ctest --test-dir build/test -R 'harness_guard|fixture_builders'` passes death tests for `/`, an outside path, `/var/lib/pacman`, `/etc/pacman.conf` and an `https://` Server, and a built package's `.PKGINFO` lists the declared depends and provides.

- [x] T-015: `GpgKeyring` in `tests/support/gpg_keyring.{h,cpp}` that generates an ed25519 keyring in the fixture GNUPGHOME, signs fixtures with `gpg --detach-sign`, and provides a second keyring whose key is absent from the first
  - REQs: REQ-C-011
  - Check: `ctest --test-dir build/test -R gpg_keyring` passes a case where a fixture signed by the generated key verifies under `SigLevel = Required` and the unsigned copy fails, and `git ls-files tests | xargs grep -l "BEGIN PGP PRIVATE KEY"` returns nothing.

- [x] T-016: Application ports and fakes: `Authorizer`, `PlanIdGenerator` with `SystemPlanIdGenerator` (`QRandomGenerator::system()`, 32 lowercase hex), `FakeAuthorizer` that records calls, `FakeTransactionEngine`, `FakeCallerIdentity` and reuse of `FakeClock`
  - REQs: REQ-C-007, REQ-F-001
  - Check: `ctest --test-dir build/test -R plan_id_generator` passes a case where two generated IDs differ and each matches `^[0-9a-f]{32}$`, and `FakeAuthorizer` records zero calls after a `PreparePlan` in the checks executable.

- [x] T-017: Request validation in `src/domain` and `PlanService`: name grammar, `/`, `.pkg.tar`, URL schemes, 1 to 64 unique names, action and option checks, sanitized offending values of at most 256 bytes, all before any libalpm initialization
  - REQs: REQ-F-013, REQ-F-014, REQ-F-015, REQ-F-016
  - Check: `ctest --test-dir build/test -R transaction_request` passes: `lib32-foo`, `g++`, `python3.12` and `a@b` pass, `-foo`, `.foo` and `Foo` fail, `/tmp/x.pkg.tar.zst`, `https://example.org/a`, `file:///x` and `../foo` give InvalidArgument with a counting engine recording zero resolve calls, and a 1000-byte name with a newline gives at most 256 bytes with no control character.

- [x] T-018: `AlpmHandle` RAII and `AlpmTransactionEngine` skeleton with `TransactionConfig` (root, database path, pacman.conf path, fallback cache), libalpm initialization only after settings load, and no use of the DBPath or RootDir from pacman.conf
  - REQs: REQ-F-018, REQ-F-020
  - Check: `ctest --test-dir build/test -R transaction_config` passes: an unreadable fixture configuration gives ConfigurationError for `install` and for `remove` with the libalpm initialization counter at 0, and a fixture `DBPath` pointing elsewhere is not used.

- [x] T-019: Install resolution happy path: `resolve()` with NOLOCK prepare and `get_add`, previewing `app` and `lib` with their repository names, NothingToDo for a current installed package, no database update or fetch-callback setter, and a timed fixture of 50 closure packages in 500
  - REQs: REQ-F-002, REQ-F-023, REQ-F-027, REQ-NF-002
  - Check: `ctest --test-dir build/test -R resolver_install` passes: installing `app` previews exactly `app` and `lib` with their repository names, installing a current package returns NothingToDo, the sync-directory manifest is unchanged, and the 50-of-500 install resolves in under 10 s.

- [x] T-020: Stage 1 install refusals: PackageNotFound for missing targets (exact names only, no group or provide), PackageIgnored for `IgnorePkg` and for `IgnoreGroup` members
  - REQs: REQ-F-021, REQ-F-022, REQ-F-070
  - Check: `ctest --test-dir build/test -R resolver_install` passes: `no-such-pkg` returns PackageNotFound naming it, a provide-only name returns PackageNotFound, `IgnorePkg = foo` and `IgnoreGroup = grp` (with `a` and `b` in `grp`) return PackageIgnored naming the target, and `c` outside `grp` is not refused.

- [x] T-021: Signature policy: effective level read back with `alpm_db_get_siglevel` per enabled repository, the Required test, `DatabaseOptional` and `DatabaseNever` journaled and accepted, and no repair of pacman.conf
  - REQs: REQ-F-039, REQ-F-040, REQ-F-041
  - Check: `ctest --test-dir build/test -R signature_policy` passes: `Never`, `Optional`, `TrustAll` with `Required` and an unset level each return InsecureSigLevel for install, `Required` and `Required TrustedOnly` do not, a repository-level `Required` overrides a global `Never`, and the fixture pacman.conf bytes are unchanged after the refusal.

- [x] T-022: Question policy during prepare: provider, replace and conflict questions answered non-acting and failing with NeedsDecision (listing every candidate), ignored-package and skip-unresolvable questions mapped to PackageIgnored and PackageNotFound, all journaled by type
  - REQs: REQ-F-024, REQ-F-053
  - Check: `ctest --test-dir build/test -R question_policy` passes: a conflict-removal question during prepare returns NeedsDecision with no plan and the journal names the question type, a two-provider fixture lists both providers, and no provider index is ever applied.

- [x] T-023: Stage 2 dependency and upgrade checks: PackageNotFound for a missing dependency naming it, PartialUpgradeRequired by exact package name, and no false positive for a dependency satisfied by an installed provider
  - REQs: REQ-F-021, REQ-F-025, REQ-F-026
  - Check: `ctest --test-dir build/test -R resolver_install` passes: `foo 1.0` installed with `foo 2.0` offered gives PartialUpgradeRequired naming `foo`, `app` requiring `lib>=2` with `lib 1.0` installed gives PartialUpgradeRequired naming `lib`, `vi-impl` providing `editor` gives no refusal, and `editor-impl` installed as `editor` gives PartialUpgradeRequired.

- [x] T-024: Protected set (`base` closure from installed or sync, plus HoldPkg, cycle-safe, cap of 4096 giving InternalError) and ProtectedPackage for install targets and dependencies that would change an installed protected package
  - REQs: REQ-F-035, REQ-F-036, REQ-NF-003
  - Check: `ctest --test-dir build/test -R protected_set` passes: a fixture `base` requiring `core-a` puts `core-a` in the set, `HoldPkg = foo` puts `foo` in it, a three-package cycle terminates in under 2 s, a 4097-package closure gives InternalError with the local manifest unchanged, and installing an upgrade of `foo` gives ProtectedPackage naming `foo`.

- [x] T-025: Check-order enforcement across stage 1 and stage 2 as in REQ-F-030
  - REQs: REQ-F-030
  - Check: `ctest --test-dir build/test -R resolver_install` passes a fixture where a HoldPkg upgrade plus a protected change returns ProtectedPackage (not PartialUpgradeRequired) and an ignored target needing a provider decision returns PackageIgnored (not NeedsDecision), and a unit test asserts the order table of T-011 against the resolver's order.

- [x] T-026: Removal set semantics: named targets plus installed dependencies no remaining package requires, no cascade, explicit or otherwise-required packages not listed, PackageNotFound for absent targets, empty repository field, and no sync database registered for remove
  - REQs: REQ-F-032, REQ-F-034, REQ-F-038, REQ-NF-003
  - Check: `ctest --test-dir build/test -R resolver_remove` passes: removing `app` previews `lib` as an orphan, `lib` explicitly installed or required by `other` is not listed, removing `not-installed-pkg` gives PackageNotFound naming it, and a remove plan succeeds with the fixture repository directories deleted.

- [x] T-027: Removal refusals: BreaksDependents listing installed dependents outside the removal set with the database manifest unchanged, ProtectedPackage for a remove target or orphan in the protected set, and no signature evaluation for remove plans
  - REQs: REQ-F-033, REQ-F-036, REQ-F-042
  - Check: `ctest --test-dir build/test -R resolver_remove` passes: removing `lib` with `app` installed gives BreaksDependents naming `app` and an identical manifest, removing `core-a` gives ProtectedPackage, and a remove plan succeeds under a fixture configuration that is insecure for install.

- [x] T-028: Hook preview reader (package-trigger hooks by name, conditional file-trigger hooks, no execution) and `samePlan` equality over the fixed field set, ignoring download size and mirror URL
  - REQs: REQ-F-002, REQ-F-043
  - Check: `ctest --test-dir build/test -R 'transaction_plan|hook_preview'` passes: a fixture package-trigger hook appears unconditional, a file-trigger hook appears conditional, a hook that writes a marker leaves the marker absent after PreparePlan, changing only a mirror URL or size keeps plans equal, and changing one version, adding an orphan or reordering hooks makes them unequal.

- [x] T-029: `PlanStore` in `src/application`: in-memory only, bound to unique name and UID, PlanNotFound identical for unknown and foreign IDs, 300 s lifetime with exact boundaries, expired discard, executed plans never run again, retryable keep and consume rules, 16 plans per UID and 64 in total, cancel and disconnect discard of pending plans, all on FakeClock
  - REQs: REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-011, REQ-F-012, REQ-F-071, REQ-NF-006
  - Check: `ctest --test-dir build/test -R plan_store` passes with a FakeClock case where Execute at 299 s and at exactly 300 s reaches authorization and at 301 s returns PlanExpired, the 17th plan for one UID returns Busy, the 65th in total returns Busy, and the two error replies have identical bytes.

- [x] T-030: `PlanService` prepare path: the work slot (Busy while Preparing or Running), capacity reservation before any engine call, and the rule that PreparePlan never calls the Authorizer
  - REQs: REQ-F-001, REQ-F-031
  - Check: `ctest --test-dir build/test -R plan_service` passes: with a blocked fake `resolve`, a second PreparePlan returns Busy, a PreparePlan with a denying Authorizer succeeds with zero Authorizer calls, and a plan ID is 32 lowercase hex.

- [x] T-031: `IdleMonitor` on `OneShotTimer` at 60 s, with a pending plan or an active transaction keeping the helper alive
  - REQs: REQ-F-068
  - Check: `ctest --test-dir build/test -R idle_monitor` passes: with FakeClock the monitor does not fire at 59 s and fires at 60 s, and a pending plan keeps it from firing until the plan is consumed.

- [x] T-032: Database lock through libalpm (spike S-1): the Execute transaction starts without the no-lock flag, so the lock covers the re-resolution and the commit; PreparePlan and pending plans hold none; a lock held elsewhere is Busy and is never removed
  - REQs: REQ-F-046, REQ-F-047
  - Check: `ctest --test-dir build/test -R database_lock` passes: a pre-created lock gives Busy in under 1 s and is still present afterwards, a hook created during re-resolution finds the lock present, and the lock is absent after Execute returns and during a pending plan.

- [x] T-033: `Execute` authorization side in `PlanService`: lookup and PlanNotFound, expiry check at start, Authorizing state holding the slot (second Execute Busy), NotAuthorized, challenge and error keep the plan, and the polkit subject fields (system-bus-name, unique name, AllowUserInteraction, no PID, plan ID as cancellation ID)
  - REQs: REQ-F-009, REQ-F-065, REQ-F-066
  - Check: `ctest --test-dir build/test -R plan_service` passes: a fake Authorizer returning error, challenge or not-authorized each yields NotAuthorized with zero transactions and the plan still executable after an allow within 300 s, and the recorded subject is `system-bus-name` with the unique name, the flag and no process-ID field.

- [x] T-034: Re-resolution after authorization under the lock, comparison with `samePlan`, PlanChanged for changed plans and for refusals during re-resolution, ConfigurationError and DatabaseUnavailable keeping their codes and consuming the plan
  - REQs: REQ-F-010, REQ-F-044, REQ-F-045
  - Check: `ctest --test-dir build/test -R execute_flow` passes: a repository version added inside the Authorizer's allow callback gives PlanChanged, a target made ignored there gives PlanChanged with PackageIgnored in the journal, unreadable pacman.conf or database there gives ConfigurationError or DatabaseUnavailable, and the local manifest is identical after each.

- [x] T-035: `CommitGate` (atomic cancel and commit arbiter implementing ExecutionControl) and the commit-point events (first PreTransaction HOOK_START or TRANSACTION_START, per S-1)
  - REQs: REQ-F-057, REQ-F-058
  - Check: `ctest --test-dir build/test -R 'commit_gate|cancel_test'` passes: a Cancel from the Verifying callback gives Cancelled, a Cancel issued at the commit-point test hook gives PastPointOfNoReturn and the transaction still ends Succeeded, and a fixture without hooks has its commit point at the start of the transaction.

- [x] T-036: Cancel and disconnect during execution (option 1, spikes S-1/S-2): Cancel while Authorizing cancels the polkit check and initialises nothing; Cancel before the commit point claims the gate, consumes the plan, replies Cancelled, calls the Terminator (removes db.lck, ends the process) and the worker interrupts at the commit gate; a disconnect is the same cancel; engine tests stall the download through the fetch-override seam
  - REQs: REQ-F-056, REQ-F-059
  - Check: `ctest --test-dir build/test -R "Cancel"` passes: a cancel while the fetch-override seam holds the download makes Execute reply Cancelled before the recording terminator runs once, with the local manifest unchanged and the lock absent; a cancel during a fake authorization gives Cancelled with zero libalpm initialisations and no termination; a cancel at the commit point installs nothing; a cancel after the commit point is PastPointOfNoReturn. (The production terminator child-process check belongs to T-051; a real stalled download is on the manual checklist.)

- [x] T-037: Commit of the previewed plan: exact installed set for install and removal, removal without the no-save option so modified backups become `.pacsave`, and no database update during execution
  - REQs: REQ-F-037, REQ-F-048
  - Check: `ctest --test-dir build/test -R execute_flow` passes: after a fixture install the installed set equals the preview's install list, after a fixture removal of a package with a modified backup file a `.pacsave` file with the modified content exists under the fixture root.

- [x] T-038: `DownloadClassifier` with the three literal-matcher rules of REQ-F-028 (log text 404, `file://` `Couldn't open file` with stat ENOENT or ENOTDIR, otherwise not absent) and the stall or TLS or timeout mapping to DownloadFailed
  - REQs: REQ-F-028, REQ-F-029
  - Check: `ctest --test-dir build/test -R download_classifier` passes: each recorded log text from SPIKES S-4 maps to the rule it names, a mode-000 fixture and an `Operation too slow` fixture give DownloadFailed, and a missing `foo` file after a published db gives SyncDatabaseStale with no cache file for `foo`.

- [x] T-039: `EventMapper` for the progress phases in the order Resolving, Downloading, Verifying, Hooks, Installing or Removing, Done, with non-decreasing `current`, 100 ms coalescing except phase changes and Done, and no Hooks item for a removal without hooks
  - REQs: REQ-F-049
  - Check: `ctest --test-dir build/test -R execute_flow` passes a case that records the phase sequence of a successful fixture install with `current` never decreasing within a phase and Done last, and a removal with no hooks has no Hooks item and still ends with Done.

- [x] T-040: Question policy during Execute: a conflict-removal question before the commit point answered no and failing with NeedsDecision with an unchanged database, and an import-key question always answered no with the GPG manifest unchanged and SignatureInvalid
  - REQs: REQ-F-053, REQ-F-054
  - Check: `ctest --test-dir build/test -R question_policy` passes: a hook raising a conflict-removal question before the commit point yields Failed with NeedsDecision and an unchanged local manifest, and a fixture signed by a key outside the keyring yields Failed with SignatureInvalid, an unchanged GPG manifest and the import question journaled.

- [x] T-041: Corrupted-package retry: one whole-execution retry on a new transaction (release, init, re-resolve, compare, commit) for a checksum or invalid-package reason only; a signature failure is never retried; no retry after the commit point, no third attempt
  - REQs: REQ-F-055
  - Check: `ctest --test-dir build/test -R execute_flow` passes: a corrupt first download with a valid retry gives Succeeded, corrupt downloads on both attempts give Failed with TransactionFailed and an empty changed list with exactly two attempts, and the lock file is observed present at the start of the second attempt.

- [x] T-042: `error_mapping` as the single authority for the DESIGN 10.2 table (FileConflicts, DiskSpace, SignatureInvalid, DatabaseUnavailable, TransactionFailed, DownloadFailed or SyncDatabaseStale, InternalError) with raw libalpm text written only to the journal
  - REQs: REQ-F-052, REQ-F-072
  - Check: `ctest --test-dir build/test -R error_mapping` passes a case per table row asserting its code and a fixture that injects an error with a unique marker, where the marker appears in the captured journal and in no reply bytes.

- [x] T-043: Post-commit and pre-commit outcomes: TransactionFailed after commit with the changed list of completed changes only, InternalError with rollback for an unexpected pre-commit fault, and the structured Result shape (outcome, status code, changed list, enumerated warnings only, no free text)
  - REQs: REQ-F-051, REQ-F-060, REQ-F-061
  - Check: `ctest --test-dir build/test -R execute_flow` passes: a fault injected at the second package install returns TransactionFailed listing only the first package with the database holding only its change, and a fault during verification returns InternalError with an unchanged manifest and an empty changed list.

- [x] T-044: Service-level consume and slot rules: PlanChanged, Cancelled, Failed-after-start and success consume the plan, NotAuthorized and Busy keep it, and the slot reads Busy while Running
  - REQs: REQ-F-008, REQ-F-010, REQ-F-031
  - Check: `ctest --test-dir build/test -R plan_service` passes: after a TransactionFailed or Cancelled outcome a second Execute returns PlanNotFound, a Busy reply during a running transaction leaves the plan executable after it ends, and a second Execute or PreparePlan during Running returns Busy.

- [x] T-045: `HelperIdentity.h` constants, `HelperConfig` command line (`--root`, `--dbpath`, `--pacman-conf`, `--cache-dir`) with production defaults from the pacman defaults header, and construction of `TransactionConfig` only in `HelperConfig.cpp` and tests
  - REQs: REQ-F-018
  - Check: `holonight-package-helper --help` lists exactly the four options, and `grep -rn "TransactionConfig" apps/helper` finds construction only in `HelperConfig.cpp`, with a grep check that no D-Bus parameter reaches it.

- [x] T-046: `org.holonight.PackageHelper1.xml` introspection with the Transactions interface (PreparePlan, Execute, Cancel, Progress, ProtocolVersion, Busy), Execute with one input of type `s`, timeout notes in the XML comments, and the adaptor embedding it through `Q_CLASSINFO("D-Bus Introspection")`
  - REQs: REQ-C-008, REQ-F-017, REQ-F-074
  - Check: `ctest --test-dir build/test -R helper_adaptor_introspection` passes: Execute declares exactly one input argument of type `s`, the XML contains the at-least-one-hour and above-10-second timeout notes, and no method name contains `Upgrade`, `Answer` or `History`.

- [x] T-047: Delayed replies for PreparePlan, Execute and Cancel on a private bus, with Cancel and property reads served while a fixture Execute is blocked, and an extra argument to Execute rejected with the plan unconsumed
  - REQs: REQ-F-017, REQ-F-074, REQ-NF-001
  - Check: `ctest --test-dir build/test -R prepare_execute_dbus` passes: with Execute blocked in the fetch seam a Cancel reply and a property read each arrive within 100 ms while the Execute call is outstanding, and a call to Execute with two arguments fails with InvalidArgs and leaves the plan executable.

- [x] T-048: Unicast Progress to the owner's unique name, caller identity by GetConnectionUnixUser with PlanNotFound for a foreign UID, and bus disconnect of the owner discarding pending plans and cancelling running ones before the commit point
  - REQs: REQ-F-004, REQ-F-012, REQ-F-050, REQ-F-059
  - Check: `ctest --test-dir build/test -R 'unicast_progress|disconnect_test'` passes: a second connection with a Progress match rule receives zero signals while the owner receives all of them, a planted foreign UID gets PlanNotFound, and the plan count drops by one after the owner disconnects.

- [x] T-049: `JournalLogger` writing `<N>` prefixed lines to stderr, plan creation, Execute start and result lines with the outcome code, and `redactUrlCredentials` applied to every libalpm log line
  - REQs: REQ-NF-004, REQ-F-052
  - Check: `ctest --test-dir build/test -R 'journal_log|redaction'` passes: a fixture run captures exactly one creation, one start and one result line, and a `Server=file://user:secret@...` configuration yields no captured line containing `secret`.

- [x] T-050: Activation and restart: the first PreparePlan on a private bus with the generated activation file starts the helper within 2 s, and a plan ID from before a restart returns PlanNotFound in the new process
  - REQs: REQ-F-067, REQ-F-069, REQ-NF-005
  - Check: `ctest --test-dir build/test -R activation_test` passes: the first PreparePlan call returns a plan in under 2 s with the helper started by the activation file, and after the helper is stopped and reactivated an Execute with an old plan ID returns PlanNotFound.

- [x] T-051: `HelperService` and `main.cpp` composition root wiring PlanService, the adaptor, the caller watcher, unicast Progress, the journal sink, the idle exit and `HelperSignals` (SIGTERM and SIGINT to Qt signal), with no reference to test seams in `main.cpp` or `HelperConfig.cpp`
  - REQs: REQ-C-002, REQ-F-068
  - Check: `ctest --test-dir build/test -R helper_service` passes an idle exit at 60 s with FakeClock and a SIGTERM test, and `grep -nE "TransactionSeams|FaultInjector" apps/helper/main.cpp apps/helper/HelperConfig.cpp` finds nothing.

- [x] T-052: `PolkitAuthorizer` in `holonight_package_helper_polkit` calling `CheckAuthorization` with the system-bus-name subject, flags 1, plan ID as cancellation ID and names as details, `CancelCheckAuthorization` on cancel, and NotAuthorized for error, timeout or challenge, with the wire test `test_holonight_package_helper_polkit` against a fake polkitd
  - REQs: REQ-C-007, REQ-F-065, REQ-F-066
  - Check: `ctest --test-dir build/test -R polkit_authorizer_test` passes: the fake polkitd records subject `("system-bus-name", {"name": <unique name>})` and flags 1, a call that times out returns NotAuthorized, and the transaction library's link list contains no polkit library.

- [x] T-053: Polkit policy `org.holonight.packages.policy` (install with `auth_admin_keep`, remove with `auth_admin`, for any, inactive and active) and the system bus policy `org.holonight.PackageHelper1.conf` (send to the name for default, own for root only)
  - REQs: REQ-C-006, REQ-F-063, REQ-F-064
  - Check: a policy-file check (run from `ctest -R helper_artifacts`) finds `org.holonight.packages.install` with exactly `auth_admin_keep` for three categories, `org.holonight.packages.remove` with exactly `auth_admin` for three categories and no `_keep`, and a default send rule plus an own rule for root only in the bus policy.

- [x] T-054: System service activation file (`.service.in`, `Name` and `Exec` from the configure step) and the systemd unit template with `Type=dbus`, `BusName`, `KillMode=mixed`, `TimeoutStopSec=300` and the REQ-C-015 hardening list, excluding the forbidden directives
  - REQs: REQ-C-015, REQ-F-067
  - Check: a static check of the rendered unit finds every listed directive with its stated value and none of the forbidden directives, and it fails on a mutated copy that removes `NoNewPrivileges` or adds `ProtectSystem=strict`.

- [x] T-055: `scripts/check-helper-artifacts.py` with passing and failing fixtures and the CTest entries `helper_artifacts`, `helper_artifacts_fixtures` and `helper_install_layout`, covering bus name, object path, interface, action IDs, activation name and unit name across XML, policy, bus conf, activation and unit
  - REQs: REQ-C-005, REQ-C-006, REQ-C-015
  - Check: `ctest --test-dir build/test -R 'helper_artifacts|helper_install_layout'` passes on the repository, and the script exits non-zero when a fixture copy changes `org.holonight.packages.install` or the bus name in a single file.

- [x] T-056: The userns test binary's skip and require policy: `--gtest_list_tests` before namespace creation, uid_map host-UID check, the `HOLONIGHT_SKIP userns` summary with exit 77 on skip, `HOLONIGHT_REQUIRE_USERNS=1` failing instead of skipping, and the CTest entries `userns_skip_policy` and `userns_require_policy` with the static marker tool built with `-static`
  - REQs: REQ-C-014, REQ-C-010
  - Check: `ctest --test-dir build/test -R 'userns_skip_policy|userns_require_policy'` passes, and with `HOLONIGHT_TEST_NO_USERNS=1` the discovered userns tests are reported Skipped and none is reported Passed.

- [x] T-057: Hook and scriptlet tests in the userns binary: a hook marker exists after install with the Hooks phase emitted, a PostTransaction hook failure gives Succeeded with HookFailed and the complete changed list, an abort-on-fail PreTransaction hook gives TransactionFailed with an empty changed list and an unchanged database, and hook failures are journaled
  - REQs: REQ-C-014, REQ-F-049, REQ-F-062, REQ-F-073
  - Check: `ctest --test-dir build/test -L userns --output-junit` reports the hook and scriptlet tests of REQ-F-049, REQ-F-062 and REQ-F-073 as passed with zero skipped in a namespace-capable environment, and the pre-transaction abort case asserts an unchanged manifest.

- [x] T-058: Process and version static checks: a fake `pacman` placed first on PATH leaves its marker absent after a full fixture install, the helper and transaction sources contain no `system`, `popen`, `exec*`, `fork`, `posix_spawn` or `QProcess`, and `alpm_pkg_vercmp` or any custom comparison appears only in `version_policy.cpp`
  - REQs: REQ-C-003, REQ-C-004
  - Check: `ctest --test-dir build/test -R 'no_external_process|version_policy'` passes, and `grep -rnE "\b(system|popen|posix_spawn|QProcess)\b|exec[lv]?p?e?\(|fork\(" apps/helper src/transaction` finds no match.

- [x] T-059: Verification: `task check` exits 0 with the new executables, `task check` runs the full ctest set with the userns job requiring namespaces, the change set contains no file under `qml/` or `apps/packages`, the runtime acceptance for Holonight and Fusion passes unchanged, and the PreparePlan timing and FakeClock budgets of REQ-NF-006 hold
  - REQs: REQ-C-013, REQ-NF-006, REQ-NF-002
  - Check: `task check` exits 0, `git diff --name-only main -- qml apps/packages` prints nothing, `ctest -R 'runtime_controls_Holonight|runtime_controls_Fusion'` passes, and the timing tests of REQ-F-005, REQ-F-068 and REQ-F-071 finish within 30 s of wall-clock time in total.

- [x] T-060: Documentation and the manual VM checklist: a helper README stating the client contract (one-hour Execute timeout, 10 s PreparePlan timeout, Progress match rule, lock file removal by the user), and the SPEC manual checklist copied into a tracked section with each step marked not run
  - REQs: REQ-F-074
  - Check: `grep -F "at least one hour"` and `grep -F "10 seconds"` each find a line in the helper README, and the README contains each of the 18 manual checklist steps from SPEC.md marked as not run.
