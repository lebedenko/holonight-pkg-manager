# SDD Tasks — system-upgrade

Implementation and review fixes finalized on 2026-11-12. Both `task check` and `task check:integration` passed.
The five manual VM checks below remain release gates by user decision (2026-11-12); they are not claimed as passed.

## Spikes

- [x] T-001: Spike S-1 (a, b): where libalpm places `db.lck` for the scratch handle and during `alpm_db_update` and a `DOWNLOADONLY` commit, and which directories receive writes (record in docs/sdd/system-upgrade/SPIKES.md)
  - REQs: REQ-F-007, REQ-F-042, REQ-F-043
  - Check: SPIKES.md has an S-1 (a, b) section that states the `db.lck` path observed for the scratch handle and for `alpm_db_update`, and the before and after manifests of live `sync/`, live `local/`, root, GPG directory and cache, and it fixes DESIGN 8.1 (a) and (b) as "design as written" or "changed".

- [x] T-002: Spike S-1 (c, d, e): package signature checks in a `DOWNLOADONLY` commit, whether the disk-space event runs there, and `alpm_db_update` return values with seeded If-Modified-Since (record in SPIKES.md)
  - REQs: REQ-F-006, REQ-F-029, REQ-F-047, REQ-F-048
  - Check: SPIKES.md has an S-1 (c, d, e) section recording errno, log lines and questions for good, bad, missing `.sig` and unknown-key fixtures at `Required` and `Optional`, whether `ALPM_EVENT_DISKSPACE_START` fires in `DOWNLOADONLY`, and the return values for unchanged and newer databases with equal mtimes, and it fixes DESIGN 8.1 (c, d, e) and the matching rows of section 11.

- [x] T-003: Spike S-1 (f): `DownloadUser` sandbox with a root-owned `0711` scratch root in a disposable container with a real `alpm` user (record in SPIKES.md)
  - REQs: REQ-F-008, REQ-C-008
  - Check: SPIKES.md has an S-1 (f) section stating whether the sandboxed downloader can write `S/dbpath/sync` under the `0711` root, the value of `alpm_option_get_sandboxuser`, and the chosen permission setting (or the stop-and-ask note when none works), which fixes D-9.

- [x] T-004: Spike S-2: keyring update visible to gpgme in the same process, through a new handle, a second transaction and a re-executed child, with gpg `keyboxd` on and off (record in SPIKES.md)
  - REQs: REQ-F-055, REQ-F-056
  - Check: SPIKES.md has an S-2 section recording outcomes (i), (ii) and (iii) for both `keyboxd` settings and selects the main-step mode of DESIGN 8.2 (in-process or child process, or the SPEC fallback), and it marks phase 13 of DESIGN 5.6 as final or provisional.

- [x] T-005: Spike S-3: promotion by `rename` or copy-then-rename across two tmpfs mounts in `unshare -Urm`, with stale-handle and `SIGKILL` observations (record in SPIKES.md)
  - REQs: REQ-F-053, REQ-F-077
  - Check: SPIKES.md has an S-3 section that records the `EXDEV` result across mounts, inode and mtime behaviour of a same-mount rename, reader results during promotion, the state left by a `SIGKILL` between copy and rename, whether libalpm ignores `.hn-promote-*` files in `sync/`, and whether a registered live handle loads new content (DESIGN 8.3 and section 11 are fixed).

- [x] T-006: Spike S-4: internal sync timeout and scratch size cap from measured compressed `core`, `extra` and `multilib` database sizes and throttled transfers at 8, 1 and 0.128 Mbit/s (record in SPIKES.md)
  - REQs: REQ-F-003, REQ-F-010, REQ-NF-002
  - Check: SPIKES.md has an S-4 section listing the measured database and signature sizes, the total transfer time at each throttle, and the byte gap between two `dlcb` events at gigabit speed, and it confirms or changes the 480 s timeout and 256 MiB cap of DESIGN 8.4.

- [x] T-007: Revise SPEC and DESIGN for the spike outcomes
  - REQs: REQ-F-003, REQ-F-008, REQ-F-010, REQ-F-042, REQ-F-053, REQ-F-055
  - Check: every provisional item of DESIGN section 11 is marked confirmed or changed, and each changed item names the SPIKES.md section that caused it.

## Preparatory refactors

- [x] T-008: Extract `FailureReporter` from `ExecutionDriver` (`failureContext`, `failureNames`, `failureResult`, `packageNameFor`) with no change of behaviour
  - REQs: REQ-C-001, REQ-F-074
  - Check: `ctest --test-dir build/test -R 'transaction'` passes with no edit to any existing install or remove test file (`git diff --stat -- tests/transaction` shows no change to existing expectations), and `src/transaction/src/failure_reporter.h` exists.

- [x] T-009: Lock ownership fix (finding F-2): `DatabaseLockGuard::ownedLockPath()`, the terminator removes only that path, and `HelperConfig::lockFile()` is deleted
  - REQs: REQ-F-049, REQ-F-050, REQ-F-074
  - Check: the child-process test `lock_finalization_child` shows that the terminator removes the owned lock path and leaves a pre-created foreign `db.lck` in place, and `grep -rn lockFile apps/helper src/transaction` finds no match.

- [x] T-010: Layering rules: amend H-004 (`alpm_db_update` allowed only in `sync_refresher.cpp`), amend H-006 (drop `Upgrade`, keep `Answer`, `History`, PackageKit, AUR and `-U`), and add H-013 (`SyncPromotion` only in `promotion.{h,cpp}` and `upgrade_driver.cpp`), each with passing and failing fixtures in `tests/policy/test_layering_policy.py`
  - REQs: REQ-C-001, REQ-F-071, REQ-F-072
  - Check: `ctest --test-dir build/test -R 'layering_policy|layering_policy_fixtures'` passes, with a fixture where `alpm_db_update` in `sync_refresher.cpp` passes and in another transaction file fails, a `PrepareUpgrade` method passes H-006, and a `SyncPromotion` reference outside the two allowed files fails H-013.

- [x] T-011: Pacman configuration diagnostics out-parameter (`PacmanDiagnostics` with `repository_without_server`), with the old overloads passing `nullptr` and returning unchanged results
  - REQs: REQ-F-018, REQ-F-074
  - Check: `ctest --test-dir build/test -R 'pacman'` passes unchanged for the existing cases, and a new `pacman_diagnostics_test` case shows that a repository section with no `Server` line sets `repository_without_server` to that repository's name.

## Transaction core (`src/transaction`, Qt-free)

- [x] T-012: Domain upgrade types (`UpgradeEntry`, `ReplacementEntry`, `NewDependencyEntry`, `HeldBackEntry`, `UpgradeDetail`, `PlanAction::Upgrade`) and `samePlan` for upgrades, which ignores download bytes, size change and mirror hosts
  - REQs: REQ-F-019, REQ-F-025
  - Check: a domain test in `tests/domain` shows that changing only a mirror host or a download size leaves two upgrade plans equal, and that changing one new version, one held-back version or the keyring flag makes them unequal.

- [x] T-013: New error codes `SyncFailed`, `SyncDatabaseChanged`, `KeyImportRequired`, `UpgradePartiallyApplied`, `Cancelled` appended after `InternalError`, `kTransactionErrorCodeCount` set to 29, and the phases `Syncing`, `Promoting`, `KeyringStep`, `MainStep` with `ProgressUpdate::step_index` and `step_count`
  - REQs: REQ-F-065, REQ-F-068
  - Check: a `tests/domain` taxonomy test asserts that `kTransactionErrorCodeCount` is 29, that each new name round-trips through `transactionErrorName` and `parseTransactionErrorCode`, and that the existing enumerator ordinals are unchanged.

- [x] T-014: `validateUpgradeRequest` for the options map and the rejection of action `upgrade` in `PreparePlan`
  - REQs: REQ-F-002, REQ-F-074
  - Check: a domain test shows that `validateUpgradeRequest` returns `InvalidArgument` for a map containing `noconfirm` or `IgnorePkg`, accepts an empty map, and that `validateRequest` returns `InvalidArgument` for action `upgrade`, with the existing validation tests unchanged.

- [x] T-015: Configuration checks for upgrades: unsupported options (`XferCommand`, `DBPath` and `RootDir` when different from the helper's values, `Usage` other than `All`) refused with `ConfigurationError` naming the option, and the repository without a server refused with `ConfigurationError` naming it
  - REQs: REQ-F-018, REQ-F-075, REQ-F-076
  - Check: `upgrade_prepare_test` shows that `XferCommand = /usr/bin/curl -o %o %u` gives `ConfigurationError` naming `XferCommand`, that `ParallelDownloads` and `HoldPkg` are accepted, that the libalpm initialization seam counter stays 0 on every refusal, and that `PreparePlan` for install with the same option still proceeds.

- [x] T-016: Signature level check for upgrades, run before any network use, returning `InsecureSigLevel`
  - REQs: REQ-F-031
  - Check: `upgrade_signature_test` shows that `SigLevel = Optional` on one repository returns `InsecureSigLevel` with the fetch callback counter at 0, and that `Required TrustedOnly` on every repository does not return it.

- [x] T-017: `ScratchDbpath` layout: the scratch `dbpath`, the `local` symlink to the live `local/`, seeded copies of the live `sync/` files that keep their mtimes, the post-copy re-stat with one repeat, and `Busy` when the live `db.lck` exists
  - REQs: REQ-F-007
  - Check: `scratch_dbpath_test` shows that the live `sync/` and `local/` manifests are identical before and after `ScratchDbpath::prepare`, that `local` is a symlink to the live `local` directory, that the seeded files keep their mtimes, and that a present live `db.lck` returns `Busy` with no file created under the scratch root.

- [x] T-018: `GuardSnapshot` of the live `*.db` and `*.db.sig` files (SHA-256, size, mtime, added and removed files) and its comparison
  - REQs: REQ-F-051
  - Check: `guard_snapshot_test` shows that rewriting `core.db` with identical content and a new mtime, changing a size, adding or removing a `.db.sig` each report a change, and that adding a `*.files` file to `sync/` reports no change.

- [x] T-019: `SyncRefresher` happy path: `alpm_db_update` on a detached worker into the scratch `sync/`, with `alpm_db_update` as the only call of that function in the code base
  - REQs: REQ-F-006, REQ-F-017
  - Check: `sync_refresher_test` on two `file://` fixture repositories at v2 shows that the scratch `sync/core.db` and `extra.db` have SHA-256 values equal to the fixture databases, that the live manifest is unchanged, and that a second repository directory named by an extra `Server` line receives zero requests.

- [x] T-020: `SyncRefresher` timeout and failure mapping: the `sync_timeout` deadline (`worker_stuck` set), a failed download mapped to `SyncFailed`, and a signature failure of a database mapped to `DatabaseUnavailable`
  - REQs: REQ-F-003, REQ-F-009, REQ-F-011
  - Check: `sync_refresher_test` with `sync_timeout = 1s` and a held download returns `SyncFailed` with `worker_stuck` set within 3 seconds, an absent repository directory returns `SyncFailed` with no `db.lck` left in the live dbpath, and a replaced `core.db.sig` returns `DatabaseUnavailable`.

- [x] T-021: Scratch size cap and `Syncing` progress: the cap checked at each download event and at the first event of a file with a known total, and one `Syncing` event per repository
  - REQs: REQ-F-010, REQ-F-034, REQ-NF-002
  - Check: `sync_refresher_test` with `scratch_cap_bytes = 1024` against a 4 KiB database returns `SyncFailed`, the three-repository fixture passes at the production cap, and the two-repository fixture records exactly two `Syncing` events with `i` values 1 then 2 and `n` equal to 2.

- [x] T-022: `UpgradeResolver` classification on a scratch handle with `NOLOCK` previews: upgrades, replacements, new dependencies, held-back packages (`alpm_sync_get_new_version` with `alpm_pkg_should_ignore`), and download and size sums
  - REQs: REQ-F-012, REQ-F-019, REQ-F-020, REQ-F-021
  - Check: `upgrade_resolver_test` on the fixture of two upgrades, one replacement, one new dependency and one held-back package returns exactly those entries in their four lists, the download and size sums equal the fixture sums, and a change of the live `sync/` files after resolution does not change the scratch result.

- [x] T-023: Keyring set (`archlinux-keyring` and the dependencies the plan must add, computed with `alpm_find_satisfier`) and the `NothingToDo` result
  - REQs: REQ-F-023, REQ-F-024
  - Check: `upgrade_resolver_test` shows that an `archlinux-keyring 2.0` upgrade gives the keyring flag true with the list `archlinux-keyring`, that an `app`-only upgrade gives false with an empty list, and that a fixture where every installed package is current returns `NothingToDo` with no plan.

- [x] T-024: Refusals of 5.4 in order: `PackageNotFound` naming a dependency broken by a held-back package, and `ProtectedPackage` for a `HoldPkg` package that the upgrade would remove
  - REQs: REQ-F-022, REQ-F-075
  - Check: `upgrade_resolver_test` shows that `IgnorePkg = foo` with `foo 1.0` installed and `app 2.0` requiring `foo>=2` returns `PackageNotFound` naming `foo`, that `HoldPkg = foo` with an upgrade that removes `foo` returns `ProtectedPackage` naming `foo`, and that `HoldPkg = foo` with a newer `foo` returns a plan.

- [x] T-025: `QuestionPolicy` upgrade mode: replacements accepted, conflicts, unresolvable removals and providers refused as `NeedsDecision`, with install mode unchanged
  - REQs: REQ-F-020, REQ-F-026, REQ-F-027, REQ-F-028, REQ-F-030
  - Check: `question_policy_test` shows that in upgrade mode a conflict naming both packages, an unresolvable removal naming the package, and two providers each give `NeedsDecision`, that a `replaces` entry is accepted, and that install mode still returns `NeedsDecision` for the replacement fixture, with the existing `question_policy_test` cases unchanged.

- [x] T-026: Key import refused for upgrades: the import question is answered "no", journaled, and returns `KeyImportRequired` naming the package
  - REQs: REQ-F-029
  - Check: `upgrade_signature_test` shows that a package signed by a key outside the keyring returns `KeyImportRequired` naming that package, that the question type is in the journal, and that the GPG directory manifest is unchanged afterwards.

- [x] T-027: `PackageVerifier`: SHA-256 check against the database entry, one retry for a corrupt file, `SignatureInvalid` without retry for a bad or missing `.sig`, and key-missing for a non-keyring package deferred when a keyring step exists
  - REQs: REQ-F-046, REQ-F-047
  - Check: `package_verifier_test` shows that a corrupt file served once and then valid passes with two requests, that a file corrupt on both requests gives `DownloadFailed` with the second request last, and that a corrupt `.sig` gives `SignatureInvalid` with a download count of 1.

- [x] T-028: `UpgradeDriver` skeleton with phases 1 to 3 (configuration re-check, pre-fetch re-resolution, `statvfs` space estimate only when `CheckSpace` is on)
  - REQs: REQ-F-048, REQ-F-052
  - Check: `upgrade_execute_test` shows that with `CheckSpace = yes` and the disk-space seam reporting too little space the result is `DiskSpace` with the live manifest unchanged, that with `CheckSpace` disabled the estimate seam counter stays 0, and that a pre-fetch re-resolution that differs returns `PlanChanged` with no download request made.

- [x] T-029: `UpgradeDriver` fetch phase 4 and verify phase 5: `DOWNLOADONLY` on the scratch dbpath into the configured `CacheDir`, `Downloading` and `Verifying` progress, and the mapping of absent files to `SyncDatabaseStale` and other failures to `DownloadFailed`
  - REQs: REQ-F-042, REQ-F-043, REQ-F-044, REQ-F-045
  - Check: `upgrade_execute_test` with three upgrades and one new dependency shows four cached files whose SHA-256 values equal the fixtures while the live manifest is unchanged, that a deleted `foo 2.0` file gives `SyncDatabaseStale` naming `foo` with no cached file for `foo`, and that a connection error gives `DownloadFailed` and never `SyncDatabaseStale`.

- [x] T-030: Live lock (phase 7) and guard (phase 8): the live lock taken before the guard, `Busy` with a pre-created `db.lck` (file kept, plan kept), and `SyncDatabaseChanged` with the plan consumed
  - REQs: REQ-F-050, REQ-F-051
  - Check: `upgrade_execute_test` shows that the `guard-checked` seam finds `db.lck` in the live dbpath, that a pre-created `db.lck` returns `Busy` within 1 second with the file not removed, and that a rewritten `core.db` with a new mtime returns `SyncDatabaseChanged` with the live manifest identical and no `db.lck` left.

- [x] T-031: Re-resolution after the guard (phase 9): a difference or refusal returns `PlanChanged` and consumes the plan
  - REQs: REQ-F-052
  - Check: `upgrade_execute_test` shows that an install of a new package run between prepare and execute (changing the live `local/`) returns `PlanChanged` with the live manifest identical, and that with no change the `promotion-starting` seam is reached.

- [x] T-032: `SyncPromotion` per-file rename with `.sig` before `.db`, rollback of already replaced files from `S/rollback`, and `InternalError` on failure
  - REQs: REQ-F-053, REQ-F-054
  - Check: `sync_promotion_test` shows that a fault at `promotion-rename-2` over three files restores the first file's SHA-256 to its value before `Execute`, leaves all three files unchanged and returns `InternalError` with the local manifest unchanged, and that a successful promotion gives each live file the SHA-256 of its scratch file with no temporary file left.

- [x] T-033: Cross-filesystem promotion: copy to `<live>/sync/.hn-promote-<name>.tmp`, then rename over the live file, using the `promote_by_copy` seam (implemented as a forced copy instead of a `same_filesystem` override)
  - REQs: REQ-F-077
  - Check: `sync_promotion_test` shows that with `same_filesystem` false the file is copied to a `.hn-promote-*.tmp` file and then renamed, that a fault after the copy leaves the live file unchanged and the temporary file is removed by the start-up sweep, and that with `same_filesystem` true the rename is used with no copy made.

- [x] T-034: Point of no return: `CommitGate` at phase 10, `PastPointOfNoReturn` for `requestCancel()` after promotion starts, and an upgrade that continues after the owner has gone
  - REQs: REQ-F-058, REQ-F-059
  - Check: `upgrade_execute_test` shows that `requestCancel` from the `promotion-starting` seam returns `PastPointOfNoReturn` and the execution outcome is `Succeeded`, and that a seam at `main-step-started` that drops the owner still lets the upgrade end with the local database showing the new versions.

- [x] T-035: Keyring step (phase 12): the keyring commit as its own transaction before the main step, the keyring step failure mapped to `UpgradePartiallyApplied` with step `keyring` and an empty changed list
  - REQs: REQ-F-055, REQ-F-060
  - Check: `upgrade_execute_test` with `archlinux-keyring` and `app` records the stage order `commit:keyring` then `commit:main`, shows the keyring at its new version when `main-step-started` fires, and with a `keyring-commit` fault returns `UpgradePartiallyApplied` naming `keyring` with an empty changed list, no new upgraded package version and live `sync/` files equal to the scratch files.

- [x] T-036: Main step (phase 13) from the cache: no download request, failure with step token `main`, `UpgradePartiallyApplied` with the changed list of completed packages, and the keyring listed first when its step finished
  - REQs: REQ-F-056, REQ-F-061, REQ-F-062
  - Check: `upgrade_execute_test` shows that deleting one cached file before the main step fails the main step with the fetch callback counter for that step at 0, that a `main-package-done` fault on the second of three packages gives `UpgradePartiallyApplied` with `main` and a changed list containing only the first, and that after a successful keyring step the changed list starts with `archlinux-keyring` at its new version.

- [x] T-037: Step markers and phase order: one `KeyringStep` (1, 2) before one `MainStep` (2, 2), or one `MainStep` (1, 1) with no keyring, and no marker for install or remove
  - REQs: REQ-F-057, REQ-F-065
  - Check: `upgrade_execute_test` records the phase sequence `Downloading`, `Verifying`, `Promoting`, `KeyringStep`, `Installing`, `Done` for a keyring fixture with the markers in the stated order, records no `KeyringStep` and one `MainStep` (1, 1) for a fixture without keyring, and records no `Done` for a failed upgrade.

- [x] T-038: Result of an upgrade: the changed list (a replacement as one `remove` and one `install` entry), the warnings limited to `HookFailed`, and success with a failing post-transaction hook
  - REQs: REQ-F-064, REQ-F-066, REQ-F-067
  - Check: `upgrade_execute_test` shows that the changed list of a successful fixture upgrade equals the preview's upgrade, replacement and new-dependency entries with each entry once, that a non-zero PostTransaction hook gives `Succeeded` with warnings exactly `{HookFailed}` and a complete changed list, and that a failed upgrade before promotion has an empty changed list.

- [x] T-039: Cancel during the fetch and the other pre-promotion failures leave the live state unchanged: `Cancelled` with the lock file removed, and `SyncFailed`, `SyncDatabaseChanged`, `PlanChanged` and `Cancelled` each leaving the live manifest identical
  - REQs: REQ-F-049, REQ-NF-006
  - Check: `upgrade_execute_test` shows that cancelling with the fetch held returns `Cancelled` with `db.lck` absent and the live and local manifests unchanged, and that after each of these pre-promotion refusals the live manifest equals its value before the first call and a repeated `PrepareUpgrade` returns the same preview as the first.

- [x] T-040: Raw libalpm text stays out of results, and a partly upgraded system is completed by the next upgrade
  - REQs: REQ-F-063, REQ-F-069, REQ-NF-004
  - Check: a marker injected into a libalpm error of the fetch phase and of the main step appears in the `LogSink` capture and in no engine result or progress text, and after `UpgradePartiallyApplied` at `main` on three upgrades a second prepare lists only the remaining packages, its execute returns `Succeeded`, and a third prepare returns `NothingToDo`.

- [x] T-041: Engine dispatch: `AlpmTransactionEngine::prepareUpgrade` and `executeUpgrade`, the two pure virtual methods with fakes in `tests/support/fake_transaction_engine.h`, and the Qt-free build check
  - REQs: REQ-C-001, REQ-C-003, REQ-F-074
  - Check: the upgrade test executables link `holonight_packages_transaction` only, `grep -rln 'QtCore\|QDBus\|polkit' src/transaction` prints nothing, every repository URL in the upgrade fixtures starts with `file://` and points into the fixture root, and the install and remove suites pass unchanged.

## Helper (`apps/helper`)

- [x] T-042: `ScratchRoots` port, `StateDirScratchRoots` (`mkdtemp` under the state directory, mode `0711`, owner check, `remove` without following symlinks, `removeAll` of `upgrade-*` entries) and the `--state-dir` option
  - REQs: REQ-F-008, REQ-F-015, REQ-NF-002
  - Check: `state_dir_scratch_roots_test` shows that `create` gives a directory owned by UID 0 with mode `0711` under the fixture state directory, that `remove` deletes a `local` symlink without touching the live `local` directory, and that `removeAll` deletes only `upgrade-*` entries.

- [x] T-043: `PlanService::prepareUpgrade` with the slot check, the per-UID limit of 16 and the total limit of 64, before any scratch root is created, and no authorization for preparation
  - REQs: REQ-F-001, REQ-F-004, REQ-F-005
  - Check: `plan_service_upgrade_test` shows that a denying Authorizer records zero calls and the prepare returns a 32-character lowercase hexadecimal plan ID, that a second prepare and a `PreparePlan` for install during a held preparation return `Busy` within 100 ms, and that the 17th prepare of one UID returns `Busy` with zero scratch `create` calls.

- [x] T-044: Discard observer on `PlanStore` wired to `ScratchRoots::remove`, covering expiry, consume, owner disconnect and cancel, and the upgrade plan lifetime rules
  - REQs: REQ-F-013, REQ-F-036, REQ-F-037, REQ-F-070, REQ-NF-002
  - Check: `plan_store_test` and `plan_service_upgrade_test` with `FakeScratchRoots` show one `remove` per discard of a prepared plan by cancel, owner disconnect and expiry, that `Execute` at 301 seconds returns `PlanExpired` with zero Authorizer calls, that at exactly 300 seconds it reaches authorization, and that a second `Execute` returns `PlanNotFound`.

- [x] T-045: Upgrade execution gate: action `org.holonight.packages.upgrade` with the three count details and no package name, `NotAuthorized` keeping the plan, and `Busy` for prepare and execute during an upgrade execution
  - REQs: REQ-F-038, REQ-F-040, REQ-F-041
  - Check: `plan_service_upgrade_test` shows that the fake Authorizer records the action, a `system-bus-name` subject with the caller's unique name, the `AllowUserInteraction` flag, the plan ID as cancellation ID and three integer details, that a denying Authorizer returns `NotAuthorized` with zero `executeUpgrade` calls and the scratch still present, and that `Busy` is returned within 100 ms while the fetch is held.

- [x] T-046: Cancel and disconnect during preparation: `Cancel("")` answers the preparation with `Cancelled`, a second caller gets `PlanNotFound`, and an owner disconnect is a cancel
  - REQs: REQ-F-032, REQ-F-033, REQ-F-035
  - Check: on the private bus with the download held and a recording terminator, `Cancel("")` returns success, `PrepareUpgrade` returns `Cancelled`, the scratch root is absent, and the terminator is invoked once after the reply; a second connection's `Cancel("")` returns `PlanNotFound`; and dropping the owner's connection removes the scratch root within 1 second with the live manifest unchanged.

- [x] T-047: Internal sync timeout and scratch cap in the helper: the timeout constant below 600 seconds, `SyncFailed` after the timeout with the process ended after the reply, and the timing rules tested with a seam
  - REQs: REQ-F-003, REQ-NF-003
  - Check: a static test reads the internal sync timeout constant and finds a value below 600 seconds, a private-bus test with the internal timeout set to 1 second returns `SyncFailed` within 6 seconds with the live manifest unchanged and the terminator invoked after the reply, and the upgrade test binaries finish within 60 seconds in the `test` preset.

- [x] T-048: Wire types for the upgrade preview (`UpgradeItemWire`, `ReplacementWire`, `NewDependencyWire`, `HeldBackWire`, `UpgradePreviewWire`) with `toWire` for upgrade plans, and the wire code table with the new codes
  - REQs: REQ-F-019, REQ-F-068, REQ-F-069
  - Check: `wire_types_test` round-trips each new wire type with the field order of the signature `(a(ssss)a(sssss)a(sss)a(ss)bastxx)`, and a test asserts that the code table lists 29 codes including `Cancelled`, with the GUI copy of the wire types kept equal to the helper copy.

- [x] T-049: `PrepareUpgrade` adaptor method with a delayed reply, options limited to an empty map, the introspection XML, and `ProtocolVersion` 2 with additions only
  - REQs: REQ-C-002, REQ-F-002, REQ-F-003, REQ-F-017, REQ-F-024, REQ-F-072, REQ-F-073
  - Check: `helper_upgrade_dbus_test` shows that `noconfirm` returns `InvalidArgument` with the libalpm initialization counter at 0, that a `NothingToDo` reply carries an empty plan ID, that `ProtocolVersion` reads 2, and that the XML declares only `PreparePlan`, `PrepareUpgrade`, `Execute` and `Cancel` with one `options` argument on `PrepareUpgrade`, documents the 10-minute call timeout, and differs from version 1 only by additions.

- [x] T-050: Targeted progress and responsiveness: `Progress("", "Syncing", ...)` goes only to the caller's unique bus name, and property reads and `Cancel` for another plan answer within 1 second during a held upgrade
  - REQs: REQ-F-034, REQ-NF-001
  - Check: on the private bus with two fixture repositories, the owner receives two `Syncing` signals with `n` equal to 2 and `i` values 1 then 2, a second connection with a match rule for `Progress` receives zero signals, and with the sync or fetch held a property read returns within 1 second and `Cancel` for another plan returns `PlanNotFound` within 1 second.

- [x] T-051: Polkit action `org.holonight.packages.upgrade` in the policy file with `auth_admin_keep` for `any`, `inactive` and `active`, and the artifact check that reads the constant, the default and the policy action
  - REQs: REQ-C-005, REQ-F-039
  - Check: `scripts/check-helper-artifacts.py` passes with the new action in the policy file and fails when that action is removed from it, and the install and remove action IDs and their defaults in the same file are unchanged.

- [x] T-052: systemd unit `StateDirectory=holonight-package-helper` and `StateDirectoryMode=0711` with a comment naming the change, and the artifact check that requires both keys and keeps `ReadWritePaths` forbidden
  - REQs: REQ-C-008
  - Check: `systemd-analyze verify` on the installed unit reports no error (`tests/helper/check_install.cmake`), and `scripts/check-helper-artifacts.py` fails when `StateDirectory` is removed from the unit.

- [x] T-053: Start-up sweep and killed-helper recovery: `removeAll` and the `<live>/sync/.hn-promote-*` sweep before the first call, with the scratch root and the temporary file of a killed helper removed on the next start
  - REQs: REQ-F-015, REQ-F-016, REQ-F-077
  - Check: a directory pre-created in the fixture state directory is absent before the first `PreparePlan` answers with the live manifest unchanged, a forked child that holds the sync through the seam is killed with `SIGKILL` and the next start leaves no scratch root before it answers, the next `PrepareUpgrade` returns a plan, and a stale `.hn-promote-core.db.tmp` in the live `sync/` is removed at start.

- [x] T-054: Journal of every `Execute` outcome (plan ID, outcome, code), with raw libalpm text written to the journal only
  - REQs: REQ-F-059, REQ-NF-004
  - Check: `journal_terminator_test` shows that the journal capture contains the plan ID, outcome and code of an `Execute` whose owner disconnected at `main-step-started`, and that an injected libalpm marker string appears in the journal capture and in no reply or signal.

- [x] T-055: Install and remove regression: the helper suites pass with the expected values unchanged, and full install and remove leave the live `sync/` manifest identical before and after each
  - REQs: REQ-C-002, REQ-F-071, REQ-F-074
  - Check: `ctest --test-dir build/test -R 'helper'` passes with no edit to an expected value in the existing helper tests, and a full install and a full remove on the fixture leave the live `sync/` manifest byte-identical before and after each.

- [x] T-056: End-to-end upgrade over the private bus (success path): the changed list equals the preview, a replacement removes the old package, `HoldPkg` does not block an upgrade, and the fixture `CacheDir` receives the files
  - REQs: REQ-F-020, REQ-F-066, REQ-F-075
  - Check: `helper_upgrade_dbus_test` runs a v1 to v2 upgrade with `newpkg` replacing `oldpkg` and `HoldPkg = foo` with a newer `foo`, and it shows that `oldpkg` is not installed afterwards, `newpkg` is, the changed list equals the preview entries with each entry once, and the cache holds the package files.

- [x] T-057: Scratch root removed before every `Execute` reply, a denied `Execute` keeps the plan for a later retry, and no reply contains the scratch path or raw text
  - REQs: REQ-F-014, REQ-F-041, REQ-F-069
  - Check: `helper_upgrade_dbus_test` shows that the scratch root is absent when the `Succeeded`, `SyncDatabaseChanged` and `Cancelled` replies are received, that a denying Authorizer followed by an allowing one runs the upgrade on the second `Execute`, and that a byte search of every captured reply and signal finds no `StateDirectory` value.

- [x] T-058: Cancel over the bus during the fetch and after promotion: a cancel during the fetch returns `Cancelled` with the terminator invoked once after the reply, and a cancel after promotion starts returns `PastPointOfNoReturn` with the process still running
  - REQs: REQ-F-049, REQ-F-058
  - Check: with the fetch held and a recording terminator, `Cancel(planId)` returns success, `Execute` returns `Cancelled`, the terminator is invoked once after the reply, and `db.lck` and the scratch root are absent; with the `promotion-starting` seam calling `Cancel`, the reply is `PastPointOfNoReturn`, the terminator is not invoked and `Execute` returns `Succeeded`.

## GUI (`apps/packages`, `src/application`, `qml/updates`, `qml/transactions`)

- [x] T-059: Ports and doubles: `TransactionService::queryProtocolVersion` with `ProbeMode`, `PlanRequest{Upgrade}`, `PackageListRefresh::refreshAfterUpgrade`, and the fakes `FakeTransactionService` and `FakePackageListRefresh` extended for them
  - REQs: REQ-C-006, REQ-F-088
  - Check: `ctest --test-dir build/dev -R 'TransactionDoubles|holonight_packages_checks_no_qtdbus'` passes, with a case showing that the fake records a `refreshAfterUpgrade` call and a `queryProtocolVersion` call with its mode.

- [x] T-060: `TransactionFlow` upgrade preparation: `Syncing` progress with `i` of `n`, `Cancel` calling `cancel("")` once with the `Cancelled` reply moving to Close only, `NothingToDo` moving to Close only with no preview built, and Busy, PlanNotFound and PlanExpired offering Try again
  - REQs: REQ-F-080, REQ-F-081, REQ-F-092
  - Check: `ctest --test-dir build/dev -R TransactionFlowUpgradePrepare` passes, with one case per reply asserting the action list (Close only for `NothingToDo` and `Cancelled`, Try again and Close for Busy) and zero Execute calls.

- [x] T-061: `TransactionFlow` upgrade preview and confirmation: the preview taken from `plan.upgrade`, Confirm calling `execute` once with the plan ID only, `NotAuthorized` returning to Previewed, and the step counter kept from the last marker
  - REQs: REQ-F-082, REQ-F-084, REQ-F-090
  - Check: `ctest --test-dir build/dev -R TransactionFlowUpgradeExecute` passes, showing one Execute with only the plan ID, that `NotAuthorized` followed by Confirm records two Execute calls with the same plan ID and no PrepareUpgrade, and that markers `KeyringStep` (1, 2) and `MainStep` (2, 2) set the step text to "step 1 of 2" then "step 2 of 2".

- [x] T-062: `TransactionFlow` cancel latch and close rules for upgrades: Cancel enabled until the first `Promoting`, `closeRequested` returning Ignore after it with the close-unavailable notice, and a late `PastPointOfNoReturn` reply shown as an outcome
  - REQs: REQ-F-085, REQ-F-086
  - Check: `ctest --test-dir build/dev -R TransactionFlowUpgradeCancel` passes, showing the cancel control enabled after `Verifying` and disabled after `Promoting`, a close request during Promoting returning Ignore with the close-unavailable notice, and a `PastPointOfNoReturn` reply leaving the window open with the result shown.

- [x] T-063: `TransactionFlow` outcome handling for upgrades: `SyncDatabaseChanged` with Try again and Close, Close only for every other upgrade code, the partial-upgrade result naming the step, and the keyring note on success
  - REQs: REQ-F-087, REQ-F-091, REQ-F-093
  - Check: `ctest --test-dir build/dev -R TransactionFlowUpgradeOutcome` passes, showing that `SyncDatabaseChanged` gives Try again and Close with Try again calling PrepareUpgrade once and no Execute, that `SyncFailed`, `KeyImportRequired` and `DownloadFailed` give Close only, and that a `Succeeded` reply whose changed list contains `archlinux-keyring` carries the keyring note.

- [x] T-064: Refusal button: a `PartialUpgradeRequired` refusal of an install offers `UpdateSystem` beside Close, and pressing it starts the upgrade flow without retrying the install
  - REQs: REQ-F-089
  - Check: `ctest --test-dir build/dev -R TransactionFlowUpgradeRefusal` passes, showing the refusal actions are Close and UpdateSystem, that pressing UpdateSystem calls PrepareUpgrade once and never PreparePlan again, and that the flow returns to Idle before the new request.

- [x] T-065: `HelperWire` and `DBusTransactionService` for upgrades: the `PrepareUpgrade` call with an empty options map and a 10-minute timeout, the decoding of the preview and status tuples, and the v1 golden file of the existing signatures
  - REQs: REQ-C-002, REQ-C-006, REQ-F-003, REQ-F-082
  - Check: `ctest --test-dir build/dev -R helper_client` passes, with an adapter test that asserts the `PrepareUpgrade` call uses an empty `a{sv}` and a call timeout of 600000 ms, that a preview reply decodes into the four upgrade lists, the keyring fields and the sizes, and that the existing method signatures match the committed version 1 golden file.

- [x] T-066: Protocol probe and button rules: `probeHelper` with no auto start on the page, `requestUpgrade` probing with auto start on the click, `HelperTooOld` below version 2 without calling PrepareUpgrade, and the button enabled for an empty list
  - REQs: REQ-F-078, REQ-F-079
  - Check: `ctest --test-dir build/dev -R TransactionControllerUpgrade` passes, showing that a fake reporting ProtocolVersion 1 gives `upgradeEnabled` false, the `HelperTooOld` reason and zero PrepareUpgrade calls, and that ProtocolVersion 2 with an empty Updates list gives an enabled button, which is disabled while a transaction runs.

- [x] T-067: Preview rows and controller properties: the rows ordered upgrades, replacements, new dependencies and held back with `ReplacesRole`, the keyring notice, download and size texts, and a 1000-row model built in one pass
  - REQs: REQ-F-082, REQ-F-083, REQ-NF-007
  - Check: `ctest --test-dir build/dev -R 'transaction_row_model|TransactionController'` passes, where a 500-row fixture yields 500 rows with the kinds in the stated order and a 1000-row model builds within 500 ms.

- [x] T-068: Message table for upgrades: `TransactionMessages` keys of REQ-F-094, the 29-row `failureText`, and the fixed table with plain-text insertion of names, versions and step tokens only
  - REQs: REQ-F-094, REQ-F-087
  - Check: `ctest --test-dir build/dev -R transaction_messages` passes, asserting a non-empty entry for every key of REQ-F-094 and for every new code, and a test fails when any upgrade dialog text is built from a reply string other than a package name, version or step token.

- [x] T-069: `ModelListRefresh::refreshAfterUpgrade`: re-read of Installed, Explore and Updates, and one `CheckNow()` call of `holonight-packaged` after each outcome
  - REQs: REQ-F-088, REQ-C-006
  - Check: `ctest --test-dir build/dev -R ModelListRefresh` passes, where each of `Succeeded`, `Cancelled`, `SyncFailed` and `NothingToDo` records one re-read per list and one `CheckNow` call, and `grep -rn 'alpm' apps/packages/app` finds no match in the refresh adapter.

- [x] T-070: "Update system…" button on the Updates page: enabled from the controller state and independent of the list contents, with the `HelperTooOld` tooltip and disabled during a transaction
  - REQs: REQ-F-078
  - Check: the QML source test of `qml/updates` and the policy fixtures pass, showing that `updateSystemButton` has no binding to the list state, that its tooltip shows `upgradeDisabledReason`, and that the button is disabled while the controller reports a running transaction.

- [x] T-071: Preparing and preview pages: the sync text with Cancel for upgrades, `UpgradePreviewPage` with kind section headers, the keyring notice, sizes and expiry, a scrollable list with no checkbox or selection control, and the 640 width
  - REQs: REQ-F-080, REQ-F-082, REQ-F-083
  - Check: the QML source test shows that `UpgradePreviewPage.qml` contains a `ListView` with `section.property: "kind"`, a vertical `ScrollBar` and no `CheckBox` or selection control, that the keyring notice is hidden when the flag is false, and that the preparing page shows "Syncing repository 1 of 2" then "2 of 2" from fake signals.

- [x] T-072: Progress and result pages: the step counter above the phase, Cancel disabled from `Promoting`, the close-unavailable message with the window kept open, and the result actions including "Update system…" for a partial upgrade
  - REQs: REQ-F-084, REQ-F-085, REQ-F-086, REQ-F-087
  - Check: the QML tests show "step 1 of 2" then "step 2 of 2" on `TransactionProgressPage`, the Cancel control disabled after a fake `Promoting` signal, a close request during `Promoting` leaving the window open with the close-unavailable text, and a fake `UpgradePartiallyApplied` reply showing Close only with the step-specific message directing the user to run "Update system…" again (REQ-F-087).

- [x] T-073: Refusal dialog page: the `UpdateSystem` action beside Close for `PartialUpgradeRequired`, with the message from the table
  - REQs: REQ-F-089
  - Check: a QML test with a fake `PartialUpgradeRequired` refusal shows exactly the actions Close and "Update system…" and the message equal to its table entry, and the refusal page source contains no action that retries the install.

- [x] T-074: Dual-style runtime acceptance for the new dialogs: `tests/runtime/test_transaction_dialogs.cpp` run in Holonight and in Fusion
  - REQs: REQ-F-083, REQ-NF-005
  - Check: `QT_QUICK_CONTROLS_STYLE=Fusion ctest --test-dir build/test -R test_transaction_dialogs` and the default-style run both pass, rendering the preview with 500 rows and the progress dialog with the step counter with no QML warning in the output.

- [x] T-075: GUI source policy: no pointer or focus automation in the upgrade tests, no libalpm or pacman call in `apps/packages` and `apps/packaged` for the upgrade, no per-package upgrade, multi-select, standalone refresh, scheduled, AUR or downgrade action, and the three helper methods as the only upgrade calls
  - REQs: REQ-C-004, REQ-C-006, REQ-C-007, REQ-F-072
  - Check: the policy script added for this task passes, a search of the upgrade test sources finds no pointer or focus call, and the action list of `TransactionController` contains none of the excluded actions.

## Docs and release

- [x] T-076: Amend the helper SPEC (REQ-F-027, error taxonomy rows, `ProtocolVersion` 2) and the GUI SPEC (REQ-F-022 with "Update system…", OQ-07 resolved), as in the "Amendments to other specs" section
  - REQs: REQ-C-010, REQ-F-068, REQ-F-073
  - Check: `grep -n 'SyncDatabaseChanged' docs/sdd/package-helper-transactions/SPEC.md` finds the taxonomy rows, the amended REQ-F-027 text names `docs/sdd/system-upgrade/SPEC.md` REQ-F-071, and the GUI SPEC contains the amended REQ-F-022 and the resolved OQ-07.

- [x] T-077: User-facing notes: CHANGELOG entry, README and `apps/packages/README.md` describing the Updates page button, and packaging notes for the `StateDirectory` change
  - REQs: REQ-C-008, REQ-F-078
  - Check: `grep -n 'Update system' README.md apps/packages/README.md CHANGELOG.md` finds an entry in each file, and the packaging note names `StateDirectory=holonight-package-helper` and mode `0711`.

- [x] T-078: Helper README client contract: `PrepareUpgrade`, the action `org.holonight.packages.upgrade`, the 10-minute call timeout, and `ProtocolVersion` 2
  - REQs: REQ-F-003, REQ-F-039, REQ-C-009
  - Check: `grep -n 'PrepareUpgrade\|org.holonight.packages.upgrade\|600' apps/helper/README.md` finds the client-contract entries, and the README states `ProtocolVersion` 2.

## Automated acceptance

- [x] T-079: `task check` on the feature branch
  - REQs: REQ-C-009
  - Passed 2026-11-12: final pinned-container build, 1,094 CTest cases with zero failures and five namespace-dependent skips, formatting, clang-tidy, QML lint and metadata, import/layering policy fixtures, REUSE, and dual-style runtime acceptance. Final review regressions cover terminal shutdown, empty main-step completion, installed replacement targets, constrained previews and both protocol-probed upgrade actions.
  - Check: `task check` exits 0 after all tasks T-001 to T-078 are done.

- [x] T-080: `task check:integration` on the feature branch
  - REQs: REQ-C-008, REQ-C-009
  - Passed 2026-11-12: full acceptance plus Release staging, installation checks and isolated runtime launches in default Holonight and environment/command-line/configured Fusion. Providers and the acceptance image remain at their existing published pins.
  - Check: `task check:integration` exits 0, including the install test that runs `systemd-analyze verify` on the helper unit.

## Manual release gates

- [ ] T-081: Manual check 1 in a disposable Arch VM: a real `-Syu` from a month-old Archive snapshot that includes a keyring update
  - REQs: REQ-F-055, REQ-F-065, REQ-C-009
  - Check: `apps/helper/README.md` has a dated entry for item 1 stating that the keyring step ran first and the upgrade reported `Succeeded`.

- [ ] T-082: Manual check 2 in the VM: a cancel during a throttled fetch
  - REQs: REQ-F-049, REQ-NF-002, REQ-C-009
  - Check: `apps/helper/README.md` has a dated entry for item 2 stating that the result was `Cancelled`, that `/var/lib/pacman/db.lck` was absent, and that the scratch root was gone.

- [ ] T-083: Manual check 3 in the VM: an external `pacman -Sy` between the preview and the confirmation
  - REQs: REQ-F-051, REQ-C-009
  - Check: `apps/helper/README.md` has a dated entry for item 3 stating that `Execute` returned `SyncDatabaseChanged` and that a new preparation then returned a plan.

- [ ] T-084: Manual check 4 in the VM: an install refused with `PartialUpgradeRequired`, then "Update system…", then the upgrade, then the same install
  - REQs: REQ-F-089, REQ-F-063, REQ-C-009
  - Check: `apps/helper/README.md` has a dated entry for item 4 stating that the install succeeded on the second attempt after the upgrade reported `Succeeded`.

- [ ] T-085: Manual check on the hardened unit: a `DownloadUser` sandbox run with `PrepareUpgrade` and `Execute` as root under the installed unit (S-1 f)
  - REQs: REQ-F-008, REQ-C-008, REQ-C-009
  - Check: `apps/helper/README.md` has a dated entry stating that the sandboxed download completed under the installed unit with the scratch root mode `0711` and the result of the `Execute`.
