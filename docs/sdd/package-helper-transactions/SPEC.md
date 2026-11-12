# Package helper transactions — SPEC (HoloNight Packages)

Status: Draft for user review, revision 2026-11-09. Requirements are written in EARS. This revision aligns the SPEC with `DESIGN.md` (spec issues SI-1..SI-20) and applies the user's decisions on OI-04, OI-05, OI-07, OI-08, OI-11, SI-9, SI-12 and SI-14. "Open items" lists what is resolved (with the decision) and what is still open.

Template labels: **[U]** Ubiquitous, **[E]** Event-driven, **[S]** State-driven, **[C]** Conditional/Optional, **[X]** Unwanted behaviour.

## Overview

`holonight-package-helper` is a very small, privileged transaction helper for Arch Linux (libalpm). It is activated on the system D-Bus, authorized per action through polkit, and exits when idle. It installs and removes packages from the official repositories and nothing else.

The helper owns the plan. The unprivileged caller (the `holonight-packages` GUI or the `holonight-packaged` service) passes only an action and package names. The helper resolves them against the real sync databases, returns a plan ID and a preview, and later executes only the plan that was previewed. Before running, it re-resolves the plan. If the result differs from the preview, it refuses.

The feature adds:

- a privileged system-bus service with `PreparePlan`, `Execute`, `Cancel` and a `Progress` signal;
- a pure transaction core in a STATIC library (`holonight_packages_transaction`) that has no D-Bus, polkit or root dependency and runs against a fixture root in tests;
- an `Authorizer` port (polkit in production, a fake in tests);
- the bus policy, polkit policy, system D-Bus activation file and systemd unit that surround the service.

The application stays otherwise unchanged. The GUI and the update check keep their present behaviour. Wiring the GUI to the helper is a later feature.

## Scope

In scope:

- Install and remove of packages that come from official repositories, including their resolved dependencies.
- Plan preparation, a short-lived single-use plan, re-resolution and plan equality.
- Removal with dependency orphans (`-Rs` equivalent, no cascade, `.pacsave` kept).
- Refusal rules: invalid names, ignored packages (`IgnorePkg` and `IgnoreGroup`), protected packages, dependents, partial upgrades, insecure signature levels (weaker than `Required`, or `TrustAll`), decisions, stale mirrors, and pre-commit libalpm failures.
- Polkit actions, the one-transaction-at-a-time rule and the per-caller plan limits.
- Structured progress, questions with safe defaults, cancellation before the commit point, hook-failure outcomes.
- The D-Bus interface, its activation, the systemd hardening list and idle exit.
- Tests on a fixture root and a private test bus, and the wording change in `CLAUDE.md`, `AGENTS.md` and the layering script.

## Non-goals

- System upgrade (`-Syu` or any upgrade of the whole system) and any partial upgrade. *Superseded for the upgrade action
  only by `docs/sdd/system-upgrade/SPEC.md` (2026-11-12); install and remove still never upgrade anything.*
- AUR packages, PKGBUILD handling and any build step.
- Transaction history (a persisted record of past transactions).
- An interactive question protocol. Questions are never sent to the caller; they get safe defaults or become `NeedsDecision` (REQ-F-053).
- GUI wiring of the Installed, Explore or Updates pages to the helper.
- `-U` and local-file installs (`pacman -U`, paths to `.pkg.tar` files, URLs).
- PackageKit or any other cross-distribution adapter.
- Refreshing sync databases (`-Sy`), including as part of an install. *Superseded for the upgrade action only: the
  system upgrade refreshes them into its scratch root and promotes them (REQ-F-027 as amended).*
- Caller-supplied configuration, paths, repositories, servers or resolved package lists.
- Disk-space delta in the preview. Replacements of installed packages are refused with `NeedsDecision`, not previewed (REQ-F-024).
- Making `XferCommand` or any other pacman frontend key work. The helper ignores `DBPath`, `RootDir`, `XferCommand`, `CleanMethod` and `Color` (REQ-F-019).

## Glossary

- **Caller**: a D-Bus client that calls the helper. Its **bound identity** is its unique bus name and Unix UID at the time the plan was prepared.
- **Plan**: the helper-owned record created by `PreparePlan`. It holds the action, the requested names, the preview, its creation time and its owner. It is held only in memory.
- **Preview**: the list of targets, dependency installs, removals, orphans, hooks and expiry that a plan shows to the caller. The hook list is an approximation (REQ-F-002).
- **Plan equality**: the comparison defined in REQ-F-043. It decides whether a re-resolved plan may run.
- **Re-resolution**: the second resolution of a plan's request, performed after authorization, under the database lock and immediately before the commit point.
- **Sync databases**: the repository databases already on disk under the configured DB path. The helper reads them and never refreshes them.
- **Official repository**: a repository configured in `/etc/pacman.conf`. The helper has no other source.
- **Pacman settings parser**: the repository's own `pacman.conf` parser (`src/backends`, libalpm-free), extended for the keys the helper needs. libalpm has no configuration parser; the helper passes the parsed values to libalpm and reads the effective signature levels back from it (REQ-F-019, REQ-F-040).
- **Orphan**: an installed package, installed as a dependency, that no remaining installed package requires after the named targets are removed. It is removed together with the targets (the `-Rs` semantics).
- **Protected set**: the dependency closure of the `base` meta-package together with the packages named in `HoldPkg`.
- **Commit point**: the first of two libalpm events during a transaction: the start of the first PreTransaction hook, or the start of the transaction proper (REQ-F-057). Download, key checks, integrity checks, file-conflict checks and disk-space checks come before it and roll back to an unchanged installed set.
- **Work slot**: the helper's single libalpm work position. It is in one of the states `Idle`, `Preparing`, `Authorizing` or `Running` (REQ-F-031).
- **Database lock**: the libalpm lock file (`alpm_option_get_lockfile`, normally `<dbpath>/db.lck`), created and removed by the helper itself while libalpm runs with its no-lock flag (REQ-F-046).
- **Safe default**: the non-acting answer the helper gives to a libalpm question when no caller is able to answer it. It is "no" for yes/no questions. A recorded choice question (provider, replace, conflict) is never acted on; it makes the plan fail with `NeedsDecision` (REQ-F-053).
- **Warning**: an enumerated token in the `Execute` result that does not change the outcome. The only token in this version is `HookFailed` (REQ-F-073).
- **Fixture root**: a temporary directory that holds a fixture `pacman.conf`, a fixture DB path, a cache directory, a keyring and `file://` fixture repositories. Tests run only against it.
- **Authorizer**: the port that answers "may this caller perform this action" (polkit in production, a fake in tests).
- **Journal**: the systemd journal, where the helper writes raw libalpm text and operational logs.
- **Helper**: the `holonight-package-helper` process, running as root and activated by the system bus.

---

## Requirements

### 1. Plan lifecycle

### REQ-F-001: Preparation needs no authorization [E]
**Statement:** When `PreparePlan(action, names, options)` is called, the helper shall resolve the request against the configured sync databases without requiring polkit authorization, and shall return a plan ID and a preview.
**Acceptance criteria:**
- With an Authorizer that denies every request, `PreparePlan` succeeds and the Authorizer records zero calls.
- Two consecutive `PreparePlan` calls for the same request return different plan IDs, each 32 lowercase hexadecimal characters.

### REQ-F-002: Preview content [E]
**Statement:** When `PreparePlan` succeeds, the helper shall return a preview that lists every explicit target, every dependency or orphan the plan changes, the action, old version and new version for each, the repository for each install, the hooks that will run, and the plan's expiry time. The hook list shall contain every hook whose package trigger matches the plan and, marked as conditional, every hook whose file trigger cannot be excluded before the packages are downloaded. The hook list is a preview and not the authority for which hooks libalpm runs (REQ-F-062).
**Acceptance criteria:**
- A fixture install of `app`, which depends on the uninstalled `lib`, previews exactly `app` and `lib`, each with its repository name, and no other package.
- A fixture removal of `app` with the orphan `lib` previews `app` as an explicit target and `lib` as an orphan, in separate fields, and both entries have an empty repository (remove plans open no sync database).
- A fixture hook file with a package trigger in the fixture HookDir appears by name in the preview's hook list, not marked conditional.
- A fixture hook file with a file trigger that cannot be excluded for a package to be installed appears in the hook list marked conditional.
- The hook reader executes nothing: a fixture hook whose action writes a marker leaves the marker absent after `PreparePlan`.

### REQ-F-003: Plans are held in memory only [U]
**Statement:** The helper shall hold plan contents and previews only in process memory and shall never write them to disk.
**Acceptance criteria:**
- After a `PreparePlan` call, a manifest of the fixture cache directory, the fixture state directory and the fixture root shows no new file.
- The plan ID and the preview text are absent from every file written under the fixture root.

### REQ-F-004: Plans are bound to the caller [U]
**Statement:** Each plan shall be bound to the D-Bus unique name and the Unix UID of the caller that created it.
**Acceptance criteria:**
- A second private-bus connection that calls `Execute` or `Cancel` with the plan ID receives `PlanNotFound`.
- With the UID resolver test double returning a UID different from the creator's for the executing caller, `Execute` returns `PlanNotFound`.
- With a UID lookup that fails, `Execute` and `Cancel` return `PlanNotFound` and `PreparePlan` returns `InternalError`.

### REQ-F-005: Plan lifetime [S]
**Statement:** While a plan has not been executed, the helper shall keep the plan for 300 seconds after its creation time. A plan whose age is greater than 300 seconds is expired; a plan of exactly 300 seconds is still valid. The lifetime is checked when `Execute` starts, so a polkit prompt that lasts longer than 300 seconds does not discard a plan already being executed.
**Acceptance criteria:**
- With a fake clock, `Execute` at 299 seconds after creation reaches the authorization step.
- With a fake clock, `Execute` at 301 seconds after creation returns `PlanExpired`.
- With a fake clock, `Execute` at exactly 300 seconds after creation reaches the authorization step.

### REQ-F-006: Expired plan is discarded [X]
**Statement:** If `Execute` is called for a plan more than 300 seconds old, then the helper shall return `PlanExpired` and discard the plan.
**Acceptance criteria:**
- After an expired `Execute`, a second `Execute` with the same plan ID returns `PlanNotFound`, and the Authorizer records no call.

### REQ-F-007: Unknown and foreign plans are indistinguishable [X]
**Statement:** If `Execute` or `Cancel` is called with a plan ID that is unknown or that is bound to another caller, then the helper shall return `PlanNotFound`, and the two cases shall produce identical replies.
**Acceptance criteria:**
- The error code and message bytes for an unknown plan ID equal those for another caller's plan ID.

### REQ-F-008: Executed plan cannot run again [X]
**Statement:** If `Execute` is called for a plan that has already been executed, then the helper shall return `PlanNotFound` and start no transaction.
**Acceptance criteria:**
- After a successful `Execute`, a second `Execute` with the same plan ID returns `PlanNotFound`, and the fake transaction counter remains at 1.

### REQ-F-009: Retryable refusals leave the plan intact [X]
**Statement:** If `Execute` returns `NotAuthorized` or `Busy` (work slot or database lock), then the helper shall leave the plan unconsumed, so that a later `Execute` within the original lifetime can run it.
**Acceptance criteria:**
- With the Authorizer denying, `Execute` returns `NotAuthorized`; with the Authorizer then allowing, a second `Execute` within 300 seconds of creation succeeds.
- A `Busy` reply from a second `Execute` during a transaction does not make the plan return `PlanNotFound` after that transaction ends.
- A `Busy` reply caused by a lock held elsewhere (REQ-F-047) leaves the plan executable.

### REQ-F-010: Other failures consume the plan [X]
**Statement:** If `Execute` returns `PlanChanged`, or if a transaction starts and then ends in any outcome other than success, or if `Execute` ends `Cancelled`, or if `Execute` returns `ConfigurationError` or `DatabaseUnavailable` from re-resolution, then the helper shall consume the plan.
**Acceptance criteria:**
- After `PlanChanged`, a second `Execute` with the same plan ID returns `PlanNotFound`.
- After a `Cancelled` or `TransactionFailed` outcome, a second `Execute` with the same plan ID returns `PlanNotFound`.
- After `ConfigurationError` or `DatabaseUnavailable` from re-resolution, a second `Execute` with the same plan ID returns `PlanNotFound`.

### REQ-F-011: Cancel discards an unexecuted plan [E]
**Statement:** When `Cancel(planId)` is called by the bound caller for a plan that has not started execution, the helper shall discard the plan and return success.
**Acceptance criteria:**
- After the cancel, `Execute` with the same ID returns `PlanNotFound`, and no libalpm transaction is initialized.

### REQ-F-012: Disconnect discards an unexecuted plan [E]
**Statement:** When the bus connection of a plan's bound caller disconnects while the plan is not executing, the helper shall discard the plan.
**Acceptance criteria:**
- With the bus disconnect simulated on a private bus, the plan count drops by one and the plan is not returned by any later call.

### REQ-F-071: Plan count limits [X]
**Statement:** If `PreparePlan` is called while the caller's UID already owns 16 live plans, or while the helper holds 64 live plans in total, then the helper shall return `Busy`, create no plan and initialize no libalpm handle. Plans that are expired, consumed or discarded do not count.
**Acceptance criteria:**
- With 16 live plans for one UID, the 17th `PreparePlan` for that UID returns `Busy`, the plan count stays 16, and the libalpm initialization counter does not change.
- With the UID resolver double returning a different UID, `PreparePlan` still succeeds until 64 plans exist in total, and the 65th returns `Busy`.
- After one plan is cancelled or expires (fake clock), the next `PreparePlan` succeeds.

### 2. Request validation and configuration

### REQ-F-013: Accepted name grammar [U]
**Statement:** The helper shall accept a package name only when it matches the Arch package-name grammar: characters from `[a-z0-9@._+-]` only, not beginning with `-` or `.`, with a length from 1 to 255 characters.
**Acceptance criteria:**
- The names `lib32-foo`, `g++`, `python3.12` and `a@b` pass validation.
- The names `-foo`, `.foo` and `Foo` (uppercase) fail validation.

### REQ-F-014: Invalid names are refused before libalpm [X]
**Statement:** If a requested name violates REQ-F-013, or contains `/`, the substring `.pkg.tar`, or a URL scheme such as `https://` or `file://`, then the helper shall return `InvalidArgument` naming the offending values, and shall not initialize libalpm. Offending values in the reply shall be truncated to 256 bytes and have control characters replaced.
**Acceptance criteria:**
- A test hook that counts libalpm initializations records zero for each of the examples `/tmp/x.pkg.tar.zst`, `https://example.org/a`, `file:///x` and `../foo`.
- Each of these returns `InvalidArgument` with the offending value in the reply.
- A 1000-byte invalid name with an embedded newline returns an offending value of at most 256 bytes with no control character.

### REQ-F-015: Request size and shape [X]
**Statement:** If a `PreparePlan` request contains zero names, more than 64 names, or the same name twice, then the helper shall return `InvalidArgument`.
**Acceptance criteria:**
- Three requests (zero names, 65 names, a duplicate) each return `InvalidArgument` and create no plan.

### REQ-F-016: Unknown action or option [X]
**Statement:** If the action is neither `install` nor `remove`, or the options map contains any key, then the helper shall return `InvalidArgument`. No option key is recognised in this version.
**Acceptance criteria:**
- The action `upgrade` returns `InvalidArgument`.
- The options key `noconfirm` returns `InvalidArgument`, and so does the key `SigLevel`.

### REQ-F-017: Execute takes only a plan ID [U]
**Statement:** The helper's `Execute` method shall accept only a plan ID, and shall not accept a package list, an action or any other argument.
**Acceptance criteria:**
- The introspection XML declares `Execute` with exactly one input argument of type `s`.
- A call on the private bus with an extra argument is rejected by the D-Bus layer, and the plan remains unconsumed.

### REQ-F-018: Callers supply no configuration [U]
**Statement:** The helper shall read configuration only from the pacman configuration path set by its composition root, and shall accept no path, repository, server, cache directory or configuration value from any caller.
**Acceptance criteria:**
- The introspection XML declares no argument named for a path, repository, server or config.
- A source search finds that the transaction configuration object is constructed only in the helper's command-line configuration code and in tests, and that no D-Bus method parameter feeds it.

### REQ-F-019: Configuration values come from pacman.conf [U]
**Statement:** The helper shall take `CacheDir`, `GPGDir`, `HookDir`, `NoUpgrade`, `NoExtract`, `IgnorePkg`, `IgnoreGroup`, `HoldPkg`, `LogFile`, `DownloadUser`, `CheckSpace`, `ParallelDownloads`, the signature levels and the repository list from the pacman configuration as read by the pacman settings parser (the repository's own parser, extended for these keys), shall pass them to libalpm, and shall not replace them with values hard-coded in the helper. The cache directory of the transaction configuration object is only the fallback used when `pacman.conf` has no `CacheDir`. The root directory and the database path come from the composition root; `DBPath`, `RootDir` and `XferCommand` in `pacman.conf` are ignored, and the helper sets no libalpm fetch callback in production. The system hook directory `<root>/usr/share/libalpm/hooks` is always searched first; the `HookDir` entries (default `<root>/etc/pacman.d/hooks`) follow and override same-named hooks, as pacman does (spike S-1).
**Acceptance criteria:**
- With `CacheDir` and `GPGDir` set to directories in the fixture root, the downloaded package lands in the configured cache directory and the keyring is read from the configured GPG directory.
- With no `CacheDir` in the fixture configuration, the downloaded package lands in the cache directory of the transaction configuration object.
- With `HoldPkg = foo` in the fixture configuration, `foo` appears in the protected set (REQ-F-035).
- With a fixture `XferCommand` that would write a marker file and a fixture `DBPath` pointing elsewhere, the marker is absent after a full install and the database path used is the composition root's.
- With a hook of the same file name in the system directory and in a configured `HookDir`, the preview lists it once and the version from the later directory decides whether it matches.

### REQ-F-020: Unreadable configuration [X]
**Statement:** If `/etc/pacman.conf`, or the configuration path set by the composition root, cannot be read or parsed, then the helper shall return `ConfigurationError` for every action, before libalpm is initialized, and start no transaction.
**Acceptance criteria:**
- With an unreadable fixture configuration, `PreparePlan` for `install` and for `remove` each return `ConfigurationError`, and the libalpm initialization counter stays 0.

### 3. Resolution and sync-database freshness

### REQ-F-021: Unknown package or dependency [X]
**Statement:** If an install target or a dependency of an install target is not in the sync databases, or a remove target is not installed, then the helper shall return `PackageNotFound` naming the missing names. Install targets are exact package names; a group name or a virtual (provided) name is not a target.
**Acceptance criteria:**
- Installing `no-such-pkg` returns `PackageNotFound` naming `no-such-pkg`.
- Installing `app`, whose fixture dependency `missing-lib` is absent from every repository, returns `PackageNotFound` naming `missing-lib`.
- Installing a name that exists only as a provide or a group returns `PackageNotFound`.

### REQ-F-022: Ignored packages [X]
**Statement:** If any target of either action is listed in `IgnorePkg`, then the helper shall return `PackageIgnored` naming it. Targets in an `IgnoreGroup` are covered by REQ-F-070.
**Acceptance criteria:**
- With `IgnorePkg = foo` in the fixture configuration, installing `foo` and removing `foo` each return `PackageIgnored` naming `foo`.

### REQ-F-023: Nothing to change [X]
**Statement:** If every install target is already installed at the version that the sync databases offer, then the helper shall return `NothingToDo`.
**Acceptance criteria:**
- Installing an already installed, current package returns `NothingToDo`.
- Installing one current package together with one new package previews only the new package.

### REQ-F-024: Decisions are refused, not guessed [X]
**Statement:** If resolution requires a choice (a dependency with several providers, a conflict whose resolution requires removing an installed package, or a sync package that replaces an installed package), then the helper shall detect the choice while preparing the transaction, discard the prepared transaction, fail `PreparePlan` (and any re-resolution) with `NeedsDecision` naming the choice kind and its candidates, and shall create no plan. libalpm applies `replaces` only during a system upgrade and raises no question for an install, so the helper itself detects a replacement: after the prepare step it matches the `replaces` entries of every added package against the local database and reports a match as a `replace` choice.
**Acceptance criteria:**
- A fixture with two providers for one dependency returns `NeedsDecision` listing both providers, and the first provider is not installed.
- A fixture conflict that requires removing an installed package returns `NeedsDecision` naming that package.
- A fixture sync package that replaces an installed package returns `NeedsDecision` naming both, and the installed package is still present.

### REQ-F-025: Partial upgrades are refused [X]
**Statement:** If an install plan would change the installed version of a package that is already installed, whether it is a target or a resolved dependency, then the helper shall return `PartialUpgradeRequired` naming the package, with no override.
**Acceptance criteria:**
- With `foo 1.0` installed and `foo 2.0` in the sync databases, installing `foo` returns `PartialUpgradeRequired`.
- With `lib 1.0` installed, and an uninstalled `app` that requires `lib>=2`, installing `app` returns `PartialUpgradeRequired` naming `lib`.
- No request parameter or option can suppress `PartialUpgradeRequired` (REQ-F-016).

### REQ-F-026: Provided names do not count as installed [U]
**Statement:** The partial-upgrade check of REQ-F-025 shall treat a package as already installed only when the local database contains a package of that exact name, and shall not treat a dependency satisfied by an installed provider of a virtual name as an upgrade.
**Acceptance criteria:**
- With `vi-impl 1.0` installed and providing `editor`, installing `tool`, which requires `editor`, does not return `PartialUpgradeRequired`, and the preview lists only the new packages.
- With `editor-impl` installed as a package named `editor` at an older version, the same install returns `PartialUpgradeRequired`.

### REQ-F-027: The sync databases are refreshed only by the system upgrade [U]
**Statement:** The helper shall not refresh the sync databases during `PreparePlan`, `Execute` of an install or remove plan, or `Cancel`: it shall not call a libalpm database-update function and shall not download database files. The live sync databases are refreshed only by the system upgrade action (REQ-F-071 of `docs/sdd/system-upgrade/SPEC.md`), whose sync download writes only into its scratch root. (Amended 2026-11-12 by the system-upgrade SPEC.)
**Acceptance criteria:**
- A source search finds no database-update call, no `alpm_fetch_pkgurl` and no fetch-callback setter in the install and remove code paths, other than the test seam header and the system-upgrade code, and the test hook counts zero database-update calls during a full install.
- A manifest of the fixture sync directory (names, sizes, SHA-256) is identical before and after a successful install.

### REQ-F-028: Stale mirror is reported as stale [X]
**Statement:** If the download of a planned package fails and the failure is classified as an absent file, then the helper shall return `SyncDatabaseStale` and leave the installed set unchanged. The helper shall classify the failure from the libalpm log text for the failed file and, for `file://` servers, a local `stat` of the target path, by these rules in order: (1) log text containing `returned error: 404` or `404 Not Found` is absent; (2) a `file://` server with the log text `Couldn't open file` is absent when `stat` fails with `ENOENT` or `ENOTDIR`, and is not absent for any other result; (3) anything else is not absent. The literal matchers are the single authority for this classification. A failed download of a detached signature (a file name ending in `.sig`) is not a stale mirror: it ends `SignatureInvalid` (spike S-6, a missing signature surfaces as a download failure).
**Acceptance criteria:**
- A fixture repository whose sync database lists `foo 2.0` but whose `foo` package file has been deleted returns `SyncDatabaseStale`.
- After that reply, the local database manifest is identical to its state before the call, and the cache contains no file for `foo`.
- A classifier unit test over recorded log text covers rule 1 (no network fixture is used) and shows that unmatched text is never classified as absent.
- A fixture package without its `<package>.sig` file ends `SignatureInvalid`, never `SyncDatabaseStale`.

### REQ-F-029: Other download failures are not stale [X]
**Statement:** If the download of a planned package fails for any reason other than an absent file, then the helper shall return `DownloadFailed`, and shall not return `SyncDatabaseStale`.
**Acceptance criteria:**
- A fixture package file made unreadable (mode 000, non-root test run) returns `DownloadFailed`.
- A fixture whose fetch-override seam reports a transfer timeout (`Operation too slow`) returns `DownloadFailed`.
- A fixture whose fetch-override seam reports a connection or TLS error returns `DownloadFailed`.

### REQ-F-030: Check order [U]
**Statement:** The helper shall evaluate refusal conditions in two stages and return the first condition that applies. Stage 1, before libalpm prepares the transaction: name and request validation, configuration readability, `PackageNotFound` for targets, `PackageIgnored` (including `IgnoreGroup`), `InsecureSigLevel`, `ProtectedPackage` for targets, `BreaksDependents`. Stage 2, after libalpm prepares the transaction (because dependency-level conditions need the resolved set): `PackageNotFound` for dependencies, `NeedsDecision`, `ProtectedPackage` for dependencies, `PartialUpgradeRequired`, `NothingToDo`.
**Acceptance criteria:**
- A fixture install that both requires an upgrade of a `HoldPkg` package and would change a protected package returns `ProtectedPackage`, not `PartialUpgradeRequired`.
- A fixture install of an ignored target that also needs a provider decision returns `PackageIgnored`, not `NeedsDecision`.
- The order is stated once in the error taxonomy table and that table matches the test expectations; a unit test asserts the table.

### REQ-F-031: One transaction at a time [X]
**Statement:** If `PreparePlan` is called while the work slot is `Preparing` or `Running`, or `Execute` is called while the work slot is not `Idle`, then the helper shall return `Busy`. The slot is `Authorizing` from the start of the polkit check of an `Execute` until it is answered; during `Authorizing` a second `Execute` shall return `Busy` and `PreparePlan` shall remain allowed.
**Acceptance criteria:**
- While a fixture transaction is blocked in its download phase (fetch-override seam), a `PreparePlan` call and a second `Execute` call each return `Busy` within 100 ms.
- While a fake Authorizer has not answered, a second `Execute` returns `Busy` and a `PreparePlan` for another request succeeds.
- While one `PreparePlan` is blocked in a test seam, a second `PreparePlan` returns `Busy`.
- The second `Execute` leaves its plan unconsumed (REQ-F-009).

### REQ-F-070: Ignored groups [X]
**Statement:** If any target of either action belongs to a group that is listed in `IgnoreGroup`, then the helper shall return `PackageIgnored` naming the target, under the same check as REQ-F-022.
**Acceptance criteria:**
- With `IgnoreGroup = grp` in the fixture configuration and the packages `a` and `b` in group `grp`, installing `a` and removing `b` each return `PackageIgnored` naming the package.
- With the same configuration, installing `c`, which is not in `grp`, does not return `PackageIgnored`.
- The check order is as for REQ-F-022 (REQ-F-030).

### 4. Removal

### REQ-F-032: Removal set semantics [E]
**Statement:** When `PreparePlan` is called with action `remove`, the helper shall compute the removal set as the named targets plus every installed dependency that no remaining installed package requires, following libalpm's recursive-removal semantics without cascade.
**Acceptance criteria:**
- Removing `app`, which requires `lib` installed as a dependency, previews `lib` as an orphan.
- Removing `app` when `lib` is installed explicitly does not list `lib`.
- Removing `app` when `lib` is also required by an installed `other` does not list `lib`.

### REQ-F-033: Dependents block removal [X]
**Statement:** If an installed package outside the removal set requires a target or an orphan, then the helper shall return `BreaksDependents` listing those dependent packages, and shall start no transaction.
**Acceptance criteria:**
- Removing `lib` while `app` (installed, not removed) requires it returns `BreaksDependents` listing `app`.
- After that reply, the local database manifest is identical to its state before the call.

### REQ-F-034: Removing an absent package [X]
**Statement:** If a remove target is not installed, then the helper shall return `PackageNotFound` naming it.
**Acceptance criteria:**
- Removing `not-installed-pkg` returns `PackageNotFound` naming it.

### REQ-F-035: Protected set [U]
**Statement:** The protected set shall be the dependency closure of the `base` meta-package, taken from the installed `base` when it is installed and from the sync databases otherwise (and empty if neither exists), together with every package named in `HoldPkg` (names as written, not globbed). The helper shall recompute the set for every plan and every re-resolution and shall not cache it. If the closure exceeds 4096 packages, then the helper shall return `InternalError`, write a journal line and start nothing.
**Acceptance criteria:**
- With a fixture `base` that requires `core-a`, the protected set contains `core-a`.
- With `HoldPkg = foo` in the fixture configuration, the protected set contains `foo`.
- With a cyclic fixture dependency graph, the closure computation terminates (REQ-NF-003).
- With a fixture closure of 4097 packages, `PreparePlan` returns `InternalError` and the local database manifest is unchanged.
- With neither a local nor a sync `base`, the protected set equals the `HoldPkg` names.

### REQ-F-036: Protected packages are refused [X]
**Statement:** If a remove target or orphan is in the protected set, or an install would change the installed version of a package in the protected set, then the helper shall return `ProtectedPackage` naming the package.
**Acceptance criteria:**
- Removing `core-a` (a protected closure member) returns `ProtectedPackage` naming `core-a`.
- Removing a package whose removal would orphan `core-a` returns `ProtectedPackage` naming `core-a`.
- Installing a package that would upgrade the `HoldPkg` package `foo` returns `ProtectedPackage` naming `foo`.

### REQ-F-037: Removal keeps modified configuration [E]
**Statement:** When `Execute` runs a remove plan, the helper shall remove the packages without the no-save option, so that modified backup files are kept as `.pacsave`.
**Acceptance criteria:**
- After removing a fixture package whose backup file was modified, a file with the modified content and the `.pacsave` suffix exists under the fixture root.

### REQ-F-038: Removal does not depend on sync databases [U]
**Statement:** Preparing and executing a remove plan shall not depend on the sync databases, so that a remove succeeds when the configured repository directories are absent.
**Acceptance criteria:**
- With the fixture repository directories removed, a remove plan is prepared and executed, and the target is no longer installed.

### 5. Signature trust

### REQ-F-039: Weak signature levels block installs [X]
**Statement:** If the effective package signature level of any enabled repository is weaker than `Required`, or allows untrusted signers, then the helper shall return `InsecureSigLevel` for every install `PreparePlan` and every install `Execute`, naming the repositories. A level is weaker than `Required` when it is `Never`, `Optional`, or not set explicitly in the configuration. A level allows untrusted signers when it is `TrustAll` (marginal-trust or unknown-key packages are accepted). A level that is `Required TrustedOnly` passes. An explicit `SigLevel` is detected by the pacman settings parser's `sig_level_explicit` flag, so that an unset level is refused although pacman's own default would be `Required`. A repository without any `SigLevel` is registered with libalpm's own default level 0, so that its databases still load; the install is then refused with `InsecureSigLevel` (SI-14).
**Acceptance criteria:**
- Fixture configurations with a repository `SigLevel` of `Never`, `Optional`, `TrustAll` (with `Required`), and no `SigLevel` at all (neither global nor per repository) each return `InsecureSigLevel` for install.
- A fixture configuration with `SigLevel = Required` and one with `SigLevel = Required TrustedOnly` do not return `InsecureSigLevel`.
- One weak repository refuses an install whose targets come from a different, secure repository.
- `DatabaseOptional` and `DatabaseNever` do not cause `InsecureSigLevel`, and each is written to the journal when present.

### REQ-F-040: Signature level is read back from libalpm [U]
**Statement:** The helper shall parse `SigLevel` directives only in the pacman settings parser (the repository's existing `pacman_repositories` parser) and shall determine the effective level of each repository by reading it back from libalpm after registration, so that libalpm's own resolution of inherited and default levels decides. The transaction library and the helper shall contain no second `SigLevel` parser.
**Acceptance criteria:**
- A fixture configuration with a repository-level `SigLevel = Required` that overrides an options-level `Never` gives a Required result for that repository.
- A source search (layering rule) finds `SigLevel` token handling (`Required`, `Optional`, `Never`, `TrustAll` literals) only in the existing parser file.

### REQ-F-041: Signature configuration is never repaired [U]
**Statement:** The helper shall never repair, lower, override or bypass the `SigLevel` configuration, and shall offer no caller-visible way to do so.
**Acceptance criteria:**
- After an `InsecureSigLevel` refusal, the fixture `pacman.conf` bytes are unchanged.
- No method parameter of the D-Bus interface can carry a signature setting (REQ-F-016 rejects it).

### REQ-F-042: Removal ignores signature levels [E]
**Statement:** When a remove plan is prepared or executed, the helper shall not evaluate the signature level, so that the removal proceeds under an insecure configuration.
**Acceptance criteria:**
- With a fixture configuration that is insecure for install, a remove plan succeeds.

### 6. Plan equality, re-resolution and execution

### REQ-F-043: Plan equality [U]
**Statement:** The helper shall treat two plans as equal only when they have the same action, the same set of (name, new version, repository) for every install target and dependency install (without regard to whether a package is a target or a dependency), the same set of (name, installed version) for every removal and orphan (a removal that becomes an orphan is a change), the same ordered hook list compared by (name, conditional flag), and no refusal condition applying to either plan. Download sizes and mirror URLs shall not take part in equality.
**Acceptance criteria:**
- A unit test shows that changing only a mirror URL or a download size leaves two plans equal.
- A unit test shows that changing one version, adding an orphan, or changing the hook order makes two plans unequal.

### REQ-F-044: Re-resolution after authorization [E]
**Statement:** When `Execute` is authorized, the helper shall take the database lock (REQ-F-046), re-resolve the plan's request and compare the result with the preview under plan equality (REQ-F-043), before the commit point.
**Acceptance criteria:**
- The Authorizer's allow callback records the order of events, and the re-resolution is recorded after the authorization and before the commit-point test hook.
- A fixture that adds a newer package version to the repository inside the Authorizer's allow callback causes the re-resolution to see the change.

### REQ-F-045: Changed plans are refused [X]
**Statement:** If the re-resolved plan is not equal to the approved preview, or if the re-resolution itself fails with any refusal code (for example the package became ignored), then the helper shall return `PlanChanged`, record the underlying code in the journal, start no transaction, and leave the installed set and the local database unchanged. `Busy`, `InternalError`, `ConfigurationError` and `DatabaseUnavailable` raised during re-resolution keep their own codes, because they report the environment and not a change to the plan. `InsecureSigLevel` raised during re-resolution also keeps its own code (REQ-F-039 names it for every install `Execute`), and so does `NeedsDecision` (REQ-F-053: a decision that appears at `Execute` time fails the run as a decision).
**Acceptance criteria:**
- A fixture that makes `pacman.conf` unreadable, and another that makes a database unreadable, inside the Authorizer's allow callback return `ConfigurationError` and `DatabaseUnavailable` respectively, and the plan is then consumed (REQ-F-010).
- The scenario of REQ-F-044 returns `PlanChanged`.
- The fixture local database manifest after that reply is identical to the manifest before `Execute`.
- A fixture that makes a target ignored inside the Authorizer's allow callback returns `PlanChanged`, and the journal names `PackageIgnored`.
- A fixture that makes the configured `SigLevel` weak inside the Authorizer's allow callback returns `InsecureSigLevel`, not `PlanChanged`.
- A fixture in which a conflicting package is installed between the preview and `Execute` returns `NeedsDecision`, not `PlanChanged`.

### REQ-F-046: Database lock covers the whole execution [U]
**Statement:** The helper shall let libalpm take the database lock: the `Execute` transaction is started without the no-lock flag, so libalpm creates its lock file in `alpm_trans_init`, before the re-resolution, and removes it when the transaction is released. A transaction started with the no-lock flag cannot be committed (spike S-1), so the helper cannot hold the lock itself. The re-resolution therefore runs inside the same transaction that is then committed, and the lock is released before the `Execute` result is returned. `PreparePlan` runs its read-only transaction with the no-lock flag, and pending plans hold no lock, so the user's own package manager is never blocked by a pending plan. The corrupted-package retry (REQ-F-055) releases the transaction and takes the lock again, so the lock is absent for a moment between the two attempts.
**Acceptance criteria:**
- A test seam at the re-resolution and at the commit point finds the lock file present; the same seam before the handle is opened finds it absent.
- After `Execute` returns, the lock file is absent.
- During a pending plan and during `PreparePlan`, the lock file is absent.

### REQ-F-047: Lock held elsewhere means Busy [X]
**Statement:** If the database lock is held by another process when `Execute` reaches the lock step, then the helper shall return `Busy` and leave the plan unconsumed. The helper shall never remove a lock file that it did not create. Because libalpm creates and removes the file itself, a file that exists when `alpm_trans_init` runs is reported as `ALPM_ERR_HANDLE_LOCK` and is left untouched.
**Acceptance criteria:**
- With a pre-created lock file in the fixture database path, `Execute` returns `Busy` within 1 second, and the plan is still executable after the lock is removed.
- After that `Busy` reply, the pre-created lock file is still present.

### REQ-F-048: Transaction runs the previewed plan [E]
**Statement:** When `Execute` passes authorization and re-resolution, the helper shall commit the prepared transaction that was compared, for exactly the packages of the approved preview.
**Acceptance criteria:**
- After a successful fixture install, the installed set equals the preview's install list and no other package changed.
- After a successful fixture removal, the installed set equals the preview's removal list minus the removed packages.

### REQ-F-049: Structured progress [E]
**Statement:** When a transaction runs, the helper shall emit `Progress(planId, phase, current, total, item)` signals whose phases occur in the order `Resolving`, `Downloading`, `Verifying`, `Installing` or `Removing`, `Done`, with `Done` last. The `Hooks` phase occurs when hooks run: PreTransaction hooks between `Verifying` and `Installing` or `Removing`, and PostTransaction hooks after it, so it may occur twice. `current` shall not decrease within a phase. Signals may be coalesced to at most one per 100 ms per phase, except that phase changes and `Done` are always sent.
**Acceptance criteria:**
- A successful fixture install records the phase sequence in that order, with `current` never decreasing within a phase and `Done` as the final signal.
- A fixture removal with no hooks records no `Hooks` item, but still records `Done` last.
- A fixture with one PreTransaction and one PostTransaction hook (user-namespace test binary, REQ-C-014) records `Hooks` before `Installing` and again after it.

### REQ-F-050: Progress is unicast to the owner [U]
**Statement:** The helper shall deliver `Progress` signals only to the bus connection that owns the plan.
**Acceptance criteria:**
- A second private-bus connection with a match rule for `Progress` receives zero signals during a transaction, while the owner receives every signal.

### REQ-F-051: Structured result [E]
**Statement:** When `Execute` completes, the helper shall return a structured result that contains the outcome (`Succeeded`, `Failed` or `Cancelled`), the error code (with its names) when the outcome is `Failed`, for each changed package its name, action, old version and new version, and a list of warnings, each an enumerated token (`HookFailed` is the only token in this version, REQ-F-073). The result shall contain no free-form text. `Execute` shall be answered with a delayed reply, because it blocks for the duration of the transaction (REQ-F-074).
**Acceptance criteria:**
- The reply signature contains only enumerated codes, package names, version strings, enumerated warning tokens and integers.
- For a failed transaction after commit (REQ-F-060), the changed-package list is present and every entry matches a real change.
- A `Failed` result that is returned before the transaction starts has an empty changed-package list.
- For the REQ-F-073 post-commit hook failure, the outcome is `Succeeded`, the warnings list contains `HookFailed`, and the changed list is complete.

### REQ-F-052: Raw libalpm text stays in the journal [X]
**Statement:** If libalpm reports an error, then the helper shall write the raw libalpm message only to the journal, and shall return only the mapped error code and the structured fields.
**Acceptance criteria:**
- A test hook injects an error carrying a unique marker string; the marker appears in the journal capture and does not appear in any D-Bus reply.

### REQ-F-053: Questions take the safe default [E]
**Statement:** When libalpm raises a question, the helper shall answer it without blocking and without involving the caller, journal the question type, and act as follows. During `PreparePlan` and re-resolution, a provider, replace or conflict question is recorded and answered with a non-acting answer, the prepared transaction is discarded, and the request fails with `NeedsDecision` (REQ-F-024); an ignored-package question fails with `PackageIgnored`; a skip-unresolvable question fails with `PackageNotFound`. During `Execute`, a decision question that is raised before the commit point fails the run with `NeedsDecision`, a corrupted-package question follows REQ-F-055, and an import-key question follows REQ-F-054.
**Acceptance criteria:**
- A conflict-removal question raised while preparing is answered "no", the journal names the question type, and `PreparePlan` returns `NeedsDecision` with no plan.
- A provider-selection question raised while preparing returns `NeedsDecision` listing all providers, and the answer given to libalpm (index 0) is never executed.
- A test hook raising a conflict-removal question during `Execute` before the commit point is answered "no", the journal names the question type, and `Execute` returns `Failed` with `NeedsDecision` and an unchanged local database.
- A test hook raising an ignored-package question returns `PackageIgnored`; a skip-unresolvable question returns `PackageNotFound`.

### REQ-F-054: Import-key questions are always refused [U]
**Statement:** The helper shall answer every import-key question with "no", regardless of any other setting, and shall journal each one.
**Acceptance criteria:**
- A fixture package signed by a key not in the keyring causes an import-key question; the answer is "no", the GPG directory manifest is unchanged, the outcome is `Failed` with `SignatureInvalid`, and the local database is unchanged.

### REQ-F-055: Corrupted package is retried once [C]
**Statement:** Where libalpm raises a corrupted-package question for a planned package before the commit point because of a checksum or invalid-package failure, and the plan has not yet been retried, the helper shall answer "yes" (so that libalpm deletes the file) and shall repeat the whole execution once: release the transaction, take the lock again, re-resolve, compare with the approved plan, commit again. libalpm itself does not download again inside the same commit. A signature failure (the question's reason is an invalid signature) is not corruption: it is not retried and ends `SignatureInvalid`. A corruption after the commit point is not retried.
**Acceptance criteria:**
- A fixture serving a corrupt file on the first download and a valid file on the retry results in `Succeeded`.
- A fixture serving a corrupt file on both downloads results in `Failed` with `TransactionFailed` and an empty changed list, and the retry is not attempted a third time.
- A fixture package whose signature does not verify is not retried and ends `Failed` with `SignatureInvalid`.
- During the second attempt the lock file is present again, and it is absent after `Execute` returns.

### REQ-F-056: Cancel before the commit point leaves the system unchanged [E]
**Statement:** When `Cancel(planId)` is called by the bound caller while the plan is authorizing or executing and before the commit point, the helper shall leave the installed set and the local database unchanged and report the `Execute` outcome `Cancelled`. A cancel during authorization shall also cancel the polkit check and initialize no libalpm handle. A cancel while the transaction runs (re-resolution, download, key checks, integrity checks, package loading, file-conflict and disk-space checks) shall atomically claim the commit gate, consume the plan, answer `Cancel` with success, send the `Cancelled` reply to the `Execute` call, remove the lock file that its own transaction holds, and then end the helper process. The bus activates the helper again on the next call. libalpm offers no way to abort a running download or a verification step (spikes S-1 and S-2), so ending the process is the cancel mechanism for this window; everything before the commit point only reads the root and the databases and writes the cache. Partial downloads and completely downloaded package files may remain in the cache.
**Acceptance criteria:**
- With the engine blocked before the commit point (the fetch-override seam holds the download) and a recording terminator, `Cancel` returns success, `Execute` returns `Cancelled`, the terminator is invoked exactly once and only after the reply, the lock file is absent, and the manifests of the local database and the root are unchanged.
- With a fake Authorizer that has not answered, `Cancel` returns success, the Authorizer records a cancellation for the plan ID, `Execute` returns `Cancelled`, the terminator is not invoked, and the libalpm initialization counter stays 0.
- The production terminator is tested in a child process: it removes a pre-created lock file and ends with exit status 0. A real helper stalled in a real download is covered by the manual checklist (throttled mirror), because a `file://` download cannot be stalled without a test seam and the helper has none (H-008).
- After a cancel the next call to the well-known name starts a new helper process that answers normally.

### REQ-F-057: Commit point definition [U]
**Statement:** The commit point of a transaction shall be the first of these libalpm events: the start of a PreTransaction hook, or the start of the transaction proper. Everything that libalpm does earlier inside its commit call (download, key checks, integrity checks, package loading, file-conflict and disk-space checks) is before the commit point. A cancel that is accepted immediately before the commit-point event ends `Cancelled` with no package changed, although PreTransaction hooks may already have run. When the commit gate was claimed by a cancel just before the event, the worker also interrupts the transaction at the transaction start, where libalpm honours the interrupt and installs nothing (spike S-1); this covers the moment before the process has ended.
**Acceptance criteria:**
- A `Cancel` issued from the Verifying progress callback yields `Cancelled`.
- A `Cancel` issued from a test hook placed at the commit-point event yields `PastPointOfNoReturn`.
- A `Cancel` accepted immediately before the commit-point event yields `Cancelled`, the local database is unchanged, and the journal records that it was cancelled at the commit gate.
- For a fixture without any hook, the commit point is the start of the transaction proper.

### REQ-F-058: Cancel after the commit point is refused [X]
**Statement:** If `Cancel` is called after the commit point, then the helper shall return `PastPointOfNoReturn` and let the transaction continue unchanged.
**Acceptance criteria:**
- After a `PastPointOfNoReturn` reply, the fixture transaction completes and `Execute` returns `Succeeded`.

### REQ-F-059: Caller disconnect is a cancel [E]
**Statement:** When the bus connection of the bound caller disconnects during `Execute`, the helper shall treat the disconnect as `Cancel` under the rules of REQ-F-056 and REQ-F-058.
**Acceptance criteria:**
- A disconnect before the commit point (including during authorization) rolls back, and the outcome is logged as `Cancelled`.
- A disconnect after the commit point lets the transaction complete, and the result is logged to the journal.

### REQ-F-060: Failure after commit reports what changed [X]
**Statement:** If the transaction fails after the commit point, then the helper shall return `TransactionFailed` with the list of packages whose changes completed.
**Acceptance criteria:**
- A fault injected while installing the second of two packages returns `TransactionFailed` listing only the first package, and the local database contains only that package's change.

### REQ-F-061: Unexpected errors before commit roll back [X]
**Statement:** If an unexpected internal error occurs before the commit point (for example out of memory or a system error that REQ-F-072 does not map), then the helper shall roll back and return `InternalError`.
**Acceptance criteria:**
- A fault injected during verification returns `InternalError`, and the local database manifest is unchanged.

### REQ-F-062: Hooks run as part of the transaction [E]
**Statement:** When a transaction runs, the helper shall run the alpm hooks configured in the hook directories as part of that transaction.
**Acceptance criteria:**
- A fixture hook that writes a marker file causes the marker to exist after a successful install, and the `Hooks` progress phase is emitted (REQ-F-049). This test runs in the user-namespace test binary (REQ-C-014); the same behaviour is checked on a real system in the manual checklist.

### REQ-F-072: Pre-commit libalpm failures have their own codes [X]
**Statement:** If libalpm reports a failure inside `Execute` before the first package change, then the helper shall roll back, leave the installed set and the local database unchanged, and return the code given by the mapping table below, with an empty changed list. If libalpm reports a failure while preparing in `PreparePlan`, the same table applies where the error occurs. The mapping table in this SPEC is the single authority for the mapping from libalpm errors to codes.

| libalpm failure | Code |
|---|---|
| File conflicts between packages, or with the filesystem | `FileConflicts` (names the packages) |
| Not enough disk space | `DiskSpace` |
| Invalid or missing package signature, unknown or untrusted key, key import refused | `SignatureInvalid` (names the packages) |
| Sync or local database missing, invalid, or with an invalid signature | `DatabaseUnavailable` |
| Package corrupted or unopenable and still so after the one retry (REQ-F-055) | `TransactionFailed` |
| A PreTransaction hook fails (REQ-F-073) | `TransactionFailed` |
| Download failure (REQ-F-028, REQ-F-029) | `SyncDatabaseStale` or `DownloadFailed` |
| Cancel accepted (REQ-F-056) | outcome `Cancelled` |
| Out of memory, system error, anything unlisted (REQ-F-061) | `InternalError` |

**Acceptance criteria:**
- A fixture with two packages that own the same file returns `FileConflicts` naming both, and the local database manifest is unchanged.
- A fault injected as a disk-space error returns `DiskSpace`, and the local database manifest is unchanged.
- A fixture package with an invalid signature returns `SignatureInvalid` naming the package.
- A fixture with a corrupt local database entry returns `DatabaseUnavailable`.
- For each row of the table, the unit test of the error mapping asserts the code, and the raw libalpm text appears only in the journal (REQ-F-052).

### REQ-F-073: Hook failure outcomes [X]
**Statement:** If a hook fails after the commit point and after at least one package change (a PostTransaction hook, or any hook that does not abort the transaction), then the helper shall keep the outcome `Succeeded`, list the complete changed packages, and add the warning `HookFailed` to the result. If a PreTransaction hook fails, which happens before any package change, then the helper shall roll back, leave the installed set unchanged, and return `TransactionFailed` with an empty changed list. A failing PostTransaction hook does not change libalpm's return value, so the helper detects it from libalpm's warning line `command failed to execute correctly`; a test pins that exact string (spike S-3). Without `AbortOnFail` a failing PreTransaction hook produces the same warning and the transaction continues.
**Acceptance criteria:**
- A fixture PostTransaction hook that exits non-zero results in outcome `Succeeded`, `HookFailed` in warnings, and a changed list equal to the preview's install list (user-namespace test binary, REQ-C-014).
- A fixture abort-on-fail PreTransaction hook that exits non-zero results in `Failed` with `TransactionFailed`, an empty changed list and an unchanged local database (user-namespace test binary).
- The journal records each hook failure.

### REQ-F-074: Long-running Execute contract [U]
**Statement:** The helper's introspection XML shall document that `Execute` blocks until the transaction ends and that clients must use a call timeout of at least one hour, and that `PreparePlan` needs a call timeout above 10 seconds. The helper shall answer `Execute` and `PreparePlan` with delayed replies, so that the helper's main thread keeps serving `Cancel` and property reads (REQ-NF-001).
**Acceptance criteria:**
- A static check finds the timeout note in the introspection XML comments.
- On the private bus with `Execute` blocked in the fetch-override seam, `Cancel` is answered while the `Execute` call is still outstanding.

### 7. Authorization

### REQ-F-063: Install action identity and defaults [U]
**Statement:** The polkit policy shall define the action `org.holonight.packages.install` for install plans, with the defaults `auth_admin_keep` for the `any`, `inactive` and `active` categories.
**Acceptance criteria:**
- A check of the policy XML finds the action ID and exactly these three defaults.

### REQ-F-064: Remove action identity and defaults [U]
**Statement:** The polkit policy shall define the action `org.holonight.packages.remove` for remove plans, with the defaults `auth_admin` for the `any`, `inactive` and `active` categories, so that authorization is not cached.
**Acceptance criteria:**
- A check of the policy XML finds the action ID and exactly these three defaults, none of which is a `_keep` variant.

### REQ-F-065: Authorization subject [E]
**Statement:** When `Execute` reaches the authorization step, the helper shall call polkit `CheckAuthorization` for the plan's action with a `system-bus-name` subject whose name is the caller's unique bus name, and with the `AllowUserInteraction` flag set. The helper shall not identify the subject by process ID. The check shall carry the plan ID as cancellation ID, and the package names (which passed REQ-F-013) as details.
**Acceptance criteria:**
- The fake Authorizer records a subject of kind `system-bus-name`, the caller's unique name, the `AllowUserInteraction` flag, and no process-ID field.
- The polkit wire test (REQ-C-007) shows that the real polkit implementation sends the subject `("system-bus-name", {"name": <unique name>})` and flags 1 to a fake polkitd.

### REQ-F-066: Denied, challenged or failed authorization [X]
**Statement:** If the polkit check returns an error, returns a challenge result, or does not authorize the request, then the helper shall return `NotAuthorized` and start no transaction.
**Acceptance criteria:**
- Each of three fake Authorizer behaviours (error, challenge, not authorized) returns `NotAuthorized`, and the transaction counter remains 0.
- A polkit call that times out or fails on the wire (polkit wire test) returns `NotAuthorized`.

### 8. Service lifecycle and activation

### REQ-F-067: Bus activation [U]
**Statement:** The helper shall be started on demand by the system bus through its system D-Bus service file when a client calls its well-known name.
**Acceptance criteria:**
- On a private bus with the activation directory of the fixture, the first `PreparePlan` call starts the helper, and the call returns a plan.

### REQ-F-068: Idle exit [E]
**Statement:** When the helper has had no pending plan and no active transaction for 60 consecutive seconds, the helper shall exit.
**Acceptance criteria:**
- With a fake clock, the helper does not exit at 59 seconds of idleness and exits at 60 seconds.
- A pending plan keeps the helper running until the plan is consumed, expired or discarded, and the 60-second idle period starts only after that.

### REQ-F-069: No state survives a restart [U]
**Statement:** Because plans are held only in memory, every plan ID issued by a previous helper process shall return `PlanNotFound` in a new process.
**Acceptance criteria:**
- After the helper exits and is activated again, `Execute` with an ID from before the restart returns `PlanNotFound`.

---

## Non-functional requirements

### REQ-NF-001: Responsive during a transaction [S]
**Statement:** While a transaction is active, the helper shall answer property reads and `Cancel` calls within 100 milliseconds.
**Acceptance criteria:**
- With a fixture transaction blocked in its download phase (fetch-override seam), a property read and a `Cancel` call each complete within 100 ms.

### REQ-NF-002: Resolution time bound [U]
**Statement:** `PreparePlan` shall complete within 10 seconds for an install request whose resolved closure has 50 packages in a fixture repository of 500 packages.
**Acceptance criteria:**
- The timed fixture test completes within 10 seconds on the CI container.

### REQ-NF-003: Cycle-safe closure [U]
**Statement:** The dependency closure computations (the protected set and the removal set) shall visit each package once, so that they terminate on cyclic dependency graphs.
**Acceptance criteria:**
- A fixture with a three-package dependency cycle terminates within 2 seconds for both the protected-set and the removal-set computation.

### REQ-NF-004: Journal logging without credentials [U]
**Statement:** The helper shall log each plan creation, each `Execute` start and each result to the journal with the plan ID, action and outcome code, and shall never log a repository URL that contains credentials. Every libalpm log line shall pass credential redaction before it is written.
**Acceptance criteria:**
- A journal capture of a fixture run contains one creation line, one start line and one result line, each with the outcome code.
- A fixture configuration with `Server=file://user:secret@...` produces no journal line containing the credential string, including libalpm log lines.

### REQ-NF-005: Activation latency [U]
**Statement:** The helper shall answer the first call after activation within 2 seconds.
**Acceptance criteria:**
- On the private bus, the measured time from the first call to its reply is below 2 seconds for a fixture repository.

### REQ-NF-006: Deterministic time-based tests [U]
**Statement:** The helper's plan lifetime, idle exit and plan-limit tests shall use a fake clock and shall not depend on real time.
**Acceptance criteria:**
- The timing tests of REQ-F-005, REQ-F-068 and REQ-F-071 run with the fake clock and finish within 30 seconds of wall-clock time in total. The measured-latency tests of REQ-NF-001 and REQ-NF-005 are the only real-time waits and are bounded.

---

## Constraints

### REQ-C-001: Transaction core is a STATIC library [U]
**Statement:** The transaction core shall be built as a STATIC library named `holonight_packages_transaction`, which takes a configuration object containing the root directory, the database path, the pacman configuration path and a fallback cache directory (REQ-F-019), and which shall not link Qt (in particular Qt D-Bus), polkit or any root-only API.
**Acceptance criteria:**
- The library's link dependencies contain no Qt, Qt D-Bus, polkit or polkit-agent library, and a test that inspects the transaction test binary's shared-library list finds none of them.
- The library's tests construct it against a fixture root in a temporary directory with no D-Bus connection.

### REQ-C-002: Dependency direction [U]
**Statement:** Dependencies shall point toward `domain`: `src/domain` shall not include libalpm or D-Bus headers; `src/application` shall not include libalpm, D-Bus or polkit headers; libalpm shall be used only from `src/backends` and from the transaction library; D-Bus and polkit shall be used only from the helper application. `src/platform` stays an INTERFACE stub.
**Acceptance criteria:**
- A policy check fails when a file in `src/domain` or `src/application` includes a libalpm, D-Bus or polkit header, or when a file in `src/transaction` includes a Qt, D-Bus or polkit header.
- The policy check has one passing and one failing fixture for each rule.

### REQ-C-003: No external process [U]
**Statement:** The helper shall start no external program (`pacman`, a shell, a helper script) and shall not use `system`, `popen`, `exec`, `fork`, `posix_spawn` or `QProcess`, except that libalpm may run the hook and scriptlet commands that a transaction requires.
**Acceptance criteria:**
- A source search of the helper and the transaction library finds no `system`, `popen`, `exec*`, `fork`, `posix_spawn` or `QProcess` use.
- With a fake `pacman` placed first on the path that writes a marker file, a full fixture install leaves the marker absent.

### REQ-C-004: Single version comparison [U]
**Statement:** All package version comparisons shall use libalpm's version comparison, called from exactly one place in the transaction library.
**Acceptance criteria:**
- A source search finds no custom version-comparison function in the helper or the transaction library, and the layering check finds the libalpm comparison call in one file only.

### REQ-C-005: Artifacts agree [U]
**Statement:** The bus name, object path, interface name, method and signal names, action IDs, activation name and systemd unit name shall agree across the introspection XML, the polkit policy, the system bus policy file, the system service activation file and the systemd unit.
**Acceptance criteria:**
- A static check passes on the consistent set and fails when a test mutation changes one action ID or one bus name in a single file.

### REQ-C-006: Bus policy allows any user to call, polkit decides [U]
**Statement:** The system bus policy shall allow any user to send messages to the helper's well-known name and shall allow only root to own that name, leaving the authorization decision to polkit.
**Acceptance criteria:**
- A static check of the bus policy file finds a default send-destination rule for the helper's name and an own rule only for root.

### REQ-C-007: Authorizer is a port [U]
**Statement:** The helper shall call the authorization decision only through an `Authorizer` interface, with a polkit implementation used only in the production composition and a fake used in tests. Exactly one additional test binary may link the polkit implementation, to test its wire format against a fake polkit authority on the private bus.
**Acceptance criteria:**
- The transaction library does not reference the polkit implementation, and every test binary except the polkit wire test binary links only the fake Authorizer.
- The polkit wire test binary is the only test binary whose link libraries contain the polkit implementation.

### REQ-C-008: Out-of-scope exclusions [U]
**Statement:** The feature shall not add a system-upgrade method, AUR code, a `-U` or local-file parameter, a transaction-history store, a question-answer method, a PackageKit dependency, or any change to the GUI pages.
**Acceptance criteria:**
- The introspection XML declares no method whose name contains `Upgrade`, `Answer` or `History`.
- A source search finds no `packagekit`, no AUR client, and no `-U` option in the helper.

### REQ-C-009: Tests use only fixture roots [U]
**Statement:** Tests shall run package transactions only against a fixture root in a temporary directory, with repositories addressed by `file://` URLs under that directory, and a test harness shall reject any root that is `/` or lies outside the temporary directory, any database path equal to `/var/lib/pacman`, any configuration path equal to `/etc/pacman.conf`, and any non-`file://` repository URL.
**Acceptance criteria:**
- A harness unit test rejects the root `/`, a root outside the temporary directory, the database path `/var/lib/pacman`, the configuration path `/etc/pacman.conf` and an `https://` repository URL.
- The fixture root never resolves to `/var/lib/pacman` or `/etc/pacman.conf`.

### REQ-C-010: Tests never need root and never automate input [U]
**Statement:** The helper's tests shall not require root, shall fail when the test process runs with host UID 0, and shall never automate pointer or focus input. The only exception to "no capabilities" is a test binary that enters an unprivileged user namespace (REQ-C-014) so that libalpm can call `chroot`; in it the host UID shall still be non-root, checked through the namespace's UID map.
**Acceptance criteria:**
- The test run fails when executed as host UID 0, for every new test executable including the user-namespace one.
- A source search of the helper tests finds no pointer, focus or input-automation helper.

### REQ-C-011: Signed fixtures from a generated keyring [U]
**Statement:** Tests shall generate a throwaway GPG keyring in the temporary directory at test time, sign the fixture packages with it, and commit no key material. Only test code may start `gpg`; REQ-C-003 applies to the helper and the transaction library. libalpm initialises gpgme once per process with the keyring directory of the first handle that verifies a signature, so a test that verifies signatures must run in its own process (ctest starts each test separately); when several run in one binary, the later ones skip with a message.
**Acceptance criteria:**
- A source search finds no private key or keyring file under the tests.
- A fixture package signed with the generated key verifies under `SigLevel = Required`, and the same package unsigned fails verification.

### REQ-C-012: Documentation wording for the fixture-root carve-out [U]
**Statement:** `CLAUDE.md` and `AGENTS.md` shall replace the statement that tests never run package transactions with the statement that tests may run package transactions only against a fixture root under a temporary directory, with `file://` fixture repositories, and never automate pointer or focus input. The layering script's `CARVE_OUT` constant, its fixture test and both documents shall change in one commit, and the layering rules that forbid transactions elsewhere shall exclude only `src/transaction`.
**Acceptance criteria:**
- Both files contain the new sentence, and neither file contains the phrase "never run package transactions".
- The layering script passes on the changed files, fails on a fixture where either file keeps the old phrase, and still forbids `alpm_trans_*` in `src`, `apps`, `qml` and `tests` outside `src/transaction`.

### REQ-C-013: GUI and runtime acceptance unchanged [U]
**Statement:** This feature shall make no change under `qml/` or `apps/packages`, and the existing runtime QML acceptance for both styles and the existing QML tests shall pass without modification.
**Acceptance criteria:**
- The change set contains no file under `qml/` or `apps/packages`.
- The CTest run of the runtime acceptance for the Holonight and Fusion styles passes unchanged.

### REQ-C-014: Chroot-dependent tests and the skip policy [U]
**Statement:** Tests that need libalpm to run hooks or scriptlets (which use `chroot`) shall run in a separate test binary that enters an unprivileged user namespace before any thread exists and uses a statically linked marker tool inside the fixture root. If user namespaces are unavailable, then that binary shall skip every test loudly, with a visible CTest skip status and a summary line naming the reason, and shall not report them as passed. The CI job that is declared namespace-capable shall fail, not skip, when the namespace cannot be created.
**Acceptance criteria:**
- With user-namespace creation disabled by a test environment switch, the binary prints the skip reason, CTest reports the tests as Skipped (not Passed), and no test in it is reported as passed.
- With the CI require-namespace switch set and user-namespace creation disabled, the binary fails.
- In the namespace-capable CI job, the hook and scriptlet tests of REQ-F-049, REQ-F-062 and REQ-F-073 run and pass, and the job log shows zero skipped tests.

### REQ-C-015: Systemd unit hardening [U]
**Statement:** The helper's systemd unit shall be `Type=dbus` with the helper's `BusName`, `KillMode=mixed` and `TimeoutStopSec=300`, shall set `NoNewPrivileges=yes`, `LockPersonality=yes`, `RestrictRealtime=yes`, `RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6 AF_NETLINK`, `ProtectClock=yes`, `ProtectHostname=yes`, `ProtectKernelLogs=yes` and `ProtectControlGroups=yes`, and shall not set `ProtectSystem`, `ProtectHome`, `PrivateTmp`, `ReadWritePaths`, `RestrictSUIDSGID`, `ProtectKernelTunables`, `ProtectKernelModules`, `SystemCallArchitectures`, `MemoryDenyWriteExecute`, `PrivateNetwork`, `RestrictNamespaces` or `CapabilityBoundingSet`, because pacman installs files anywhere, ships setuid files, runs arbitrary hook tools, downloads over the network and calls `chroot`.
**Acceptance criteria:**
- A static check of the unit template finds every listed directive with the stated value and none of the forbidden directives.
- The static check fails on a test mutation that removes `NoNewPrivileges` and on one that adds `ProtectSystem=strict`.
- The VM smoke test in the manual checklist passes under the unit.

---

## Error taxonomy

Each refusal or failure is returned as one code. The reply carries the code, the names it refers to, and no free text. Raw libalpm text goes only to the journal (REQ-F-052). The order of evaluation of refusals is defined in REQ-F-030; the mapping from libalpm failures is defined in REQ-F-072.

| Code | Meaning | Raised by | Reply data |
|------|---------|-----------|------------|
| `InvalidArgument` | Name, count, action or option is malformed or forbidden | REQ-F-014, REQ-F-015, REQ-F-016 | Offending values |
| `PackageNotFound` | Target or dependency absent from sync databases, or remove target not installed | REQ-F-021, REQ-F-034, REQ-F-053 | Missing names |
| `PackageIgnored` | Target listed in `IgnorePkg` or in an `IgnoreGroup` group | REQ-F-022, REQ-F-070, REQ-F-053 | Names |
| `ProtectedPackage` | Removal of, or upgrade of, a package in the protected set | REQ-F-036 | Names |
| `BreaksDependents` | Installed packages outside the removal set still require a target or orphan | REQ-F-033 | Dependent names |
| `NeedsDecision` | Resolution needs a provider, conflict or replacement choice | REQ-F-024, REQ-F-053 | Choice kind, subject, candidates |
| `PartialUpgradeRequired` | Install would change an installed package | REQ-F-025 | Names |
| `InsecureSigLevel` | An enabled repository is weaker than `Required`, or is `TrustAll` (install only) | REQ-F-039 | Repository names |
| `SyncDatabaseStale` | Planned package absent on its repository | REQ-F-028 | Package name |
| `DownloadFailed` | Download failed for a reason other than an absent file | REQ-F-029 | Package name |
| `FileConflicts` | libalpm found file conflicts before any package change | REQ-F-072 | Package names |
| `DiskSpace` | libalpm found not enough disk space before any package change | REQ-F-072 | None |
| `SignatureInvalid` | Package signature invalid or missing, key unknown or untrusted, key import refused | REQ-F-072, REQ-F-054 | Package names |
| `DatabaseUnavailable` | Sync or local database missing, invalid or with an invalid signature | REQ-F-072 | None |
| `ConfigurationError` | `pacman.conf` unreadable or unparseable | REQ-F-020 | None |
| `NothingToDo` | Every install target already installed at the offered version | REQ-F-023 | None |
| `PlanNotFound` | Unknown, foreign, executed or restarted-away plan ID | REQ-F-007, REQ-F-008, REQ-F-069 | None |
| `PlanExpired` | Plan older than 300 seconds at `Execute` | REQ-F-006 | None |
| `PlanChanged` | Re-resolved plan differs from the preview, or the re-resolution refused | REQ-F-045 | None |
| `NotAuthorized` | polkit denied, challenged or failed | REQ-F-066 | None |
| `Busy` | Work slot not free, plan limits reached, or the database lock held elsewhere | REQ-F-031, REQ-F-047, REQ-F-071 | None |
| `PastPointOfNoReturn` | `Cancel` after the commit point | REQ-F-058 | None |
| `TransactionFailed` | libalpm failed (corruption after one retry, PreTransaction hook failure, or any failure after the commit point); the changed list shows what completed | REQ-F-060, REQ-F-055, REQ-F-073 | Changed packages |
| `InternalError` | Unexpected error before commit, or protected-set cap exceeded; rolled back | REQ-F-061, REQ-F-072, REQ-F-035 | None |
| `SyncFailed` | System upgrade: the sync databases could not be fetched (mirror, internal timeout, size cap) | system-upgrade REQ-F-003, REQ-F-009, REQ-F-010 | None |
| `SyncDatabaseChanged` | System upgrade: a live sync database changed after the preparation | system-upgrade REQ-F-051 | None |
| `KeyImportRequired` | System upgrade: a signing key is missing from the keyring; key import refused | system-upgrade REQ-F-029 | Package names |
| `UpgradePartiallyApplied` | System upgrade: a step failed after the point of no return | system-upgrade REQ-F-060, REQ-F-061 | Step token, changed packages |
| `Cancelled` | System upgrade: the caller cancelled its own `PrepareUpgrade`, or disconnected | system-upgrade REQ-F-032, REQ-F-035 | None |

The last five rows were added by `docs/sdd/system-upgrade/SPEC.md` (2026-11-12) and are returned for the upgrade
action only. They raise the interface's `ProtocolVersion` property from 1 to 2; the methods, signals and signatures
of version 1 are unchanged (system-upgrade REQ-F-073, REQ-C-002).

Outcome values of `Execute` (not errors): `Succeeded`, `Failed` (carries one of the codes above), `Cancelled` (REQ-F-056). Warning tokens: `HookFailed` (REQ-F-073). Codes that `PreparePlan` can return: `InvalidArgument`, `PackageNotFound`, `PackageIgnored`, `ProtectedPackage`, `BreaksDependents`, `NeedsDecision`, `PartialUpgradeRequired`, `InsecureSigLevel`, `DatabaseUnavailable`, `ConfigurationError`, `NothingToDo`, `Busy`, `InternalError`. `Cancel` returns success, `PlanNotFound` or `PastPointOfNoReturn`.

---

## Manual verification checklist

These checks need a real polkit agent, a real system bus and real root. They run on a disposable Arch VM that the user controls. Nothing here is automated, and no step automates pointer or focus input. Each step is performed by a person.

- [ ] Install a small official package from the GUI or a test client. The polkit prompt appears, and the transaction succeeds after authentication (REQ-F-063, REQ-F-065).
- [ ] Repeat an install within the same session without authentication if the `auth_admin_keep` cache is still valid, and confirm that it is accepted without a second prompt.
- [ ] Remove a package. The polkit prompt appears every time, with no cached authorization (REQ-F-064).
- [ ] Cancel at the polkit prompt. The reply is `NotAuthorized`, and the same plan can be executed afterwards within 5 minutes (REQ-F-009).
- [ ] Remove a package whose configuration file was modified. Confirm the `.pacsave` file exists (REQ-F-037).
- [ ] Confirm that no sync refresh happened: the modification times and sizes of `/var/lib/pacman/sync` files are unchanged after an install (REQ-F-027).
- [ ] Set `SigLevel = Optional` on a throwaway repository in the VM's `pacman.conf`. Confirm that install is refused with `InsecureSigLevel` and that the file is unchanged (REQ-F-039, REQ-F-041). Repeat with `SigLevel = Required TrustAll`.
- [ ] Install a package whose hook writes a marker in `/etc/pacman.d/hooks`. Confirm the marker exists and the hook appears in the preview (REQ-F-062).
- [ ] Under the hardened unit, install a package, remove a package, install a package that ships a hook and install one that ships an install scriptlet. All succeed (REQ-C-015, REQ-F-062).
- [ ] Install a package whose PostTransaction hook is made to fail. Confirm outcome `Succeeded` with the `HookFailed` warning (REQ-F-073).
- [ ] Remove a package that other installed packages require. Confirm `BreaksDependents` lists them and that nothing changed (REQ-F-033).
- [ ] Cancel during a slow download (a throttled mirror in the VM). Confirm `Cancelled` and that `/var/lib/pacman/db.lck` is absent (REQ-F-056, REQ-F-046). Confirm the helper process ended and that the next call starts a new one.
- [ ] Create `/var/lib/pacman/db.lck` by hand, run an `Execute`, and confirm `Busy`, that the plan stays valid, and that the helper did not remove the lock (REQ-F-047).
- [ ] Request an HTTP mirror that answers 404 for a package file listed in the sync database. Confirm `SyncDatabaseStale` (REQ-F-028, rule 1).
- [ ] Call `Execute` from a client with a call timeout of at least one hour on a slow download and confirm the reply arrives (REQ-F-074).
- [ ] Let the helper stay idle. Confirm in the journal that it exits 60 seconds after the last plan or transaction (REQ-F-068).
- [ ] As an unprivileged user, call `PreparePlan` on the system bus and confirm the reply. As that user, confirm that the helper cannot own its name (REQ-C-006).
- [ ] Confirm that the journal holds raw libalpm text for a forced error and that the GUI or client receives only the code (REQ-F-052).

---

## Risks

| # | Risk | Mitigation | Requirements |
|---|------|------------|--------------|
| 1 | Partial-upgrade false positives: a dependency satisfied by an installed provider is wrongly seen as an upgrade. | Compare by exact package name only; fixture cases with virtual providers in both directions. | REQ-F-025, REQ-F-026 |
| 2 | Partial-upgrade false negatives: a `replaces` or a versioned provide causes an upgrade that the check misses. | Treat any sync package that replaces an installed package as `NeedsDecision`; fixture cases for versioned provides. | REQ-F-024, REQ-F-025 |
| 3 | Plan equality is too loose (an unchanged name hides a changed version) or too strict (mirror noise breaks every execute). | Equality defined by an explicit field set; download size and URL excluded; unit tests on both sides. | REQ-F-043, REQ-F-045 |
| 4 | Time-of-check to time-of-use between the polkit prompt and the commit. | Re-resolution after authorization, under the database lock, and before the commit point. | REQ-F-044, REQ-F-045, REQ-F-046 |
| 5 | A mirror that is behind the sync database is reported as a generic failure, or a failure is wrongly reported as stale. libalpm exposes no HTTP status; curl reports a missing and an unreadable `file://` file alike. | Literal log-text matchers plus a local `stat` for `file://`; unmatched text always means `DownloadFailed`; a fixture per rule; strings pinned by spike S-4. | REQ-F-028, REQ-F-029 |
| 6 | Keyring generation in the CI container is slow or flaky. | Generate the throwaway keyring once per test process into the temporary directory, with bounded retries and a fixed key type. | REQ-C-011 |
| 7 | Unicast `Progress` is not enforced by the bus, because a broadcast signal reaches any matching subscriber. | Send with an explicit destination to the owner's unique name; test with an observing connection. | REQ-F-050 |
| 8 | Raw libalpm text leaks through the protocol. | Mapped codes only; a marker test that fails if the marker appears on the bus. | REQ-F-052, REQ-F-072 |
| 9 | A stale or forged plan ID is executed by another local user. | Binding to the unique name and the UID; indistinguishable `PlanNotFound`. | REQ-F-004, REQ-F-007 |
| 10 | Tests affect the real system. | Fixture root only, harness assertions, UID-0 failure. | REQ-C-009, REQ-C-010 |
| 11 | libalpm internals differ from the design's assumptions (event order, lock behaviour with the no-lock flag, interrupt handling). | Spikes S-1..S-6 before the implementation plan; test seams make the orchestration testable independently. | REQ-F-046, REQ-F-057 |
| 12 | libalpm cannot abort a running download (spike S-2). | Cancel ends the helper process after replying (REQ-F-056); the lock file is removed first and the bus re-activates the helper. | REQ-F-056, REQ-F-059 |
| 13 | A cancel that wins the commit-gate race still lets PreTransaction hooks run. | Documented; the outcome is `Cancelled` with no package changed; the journal records it. | REQ-F-057 |
| 14 | A crashed helper leaves the lock file and every later run is `Busy`. | The helper never removes a lock it did not create; documented; the user removes it as with pacman. | REQ-F-047 |
| 15 | Hook preview disagrees with the hooks libalpm runs (file triggers on packages not yet downloaded). | `conditional` marker; equality uses the same function on both sides; the preview is not authoritative. | REQ-F-002, REQ-F-043, REQ-F-062 |
| 16 | User namespaces are unavailable in some CI containers, so hook tests silently do not run. | Loud skip with CTest skip status; the namespace-capable job fails instead of skipping; VM checklist. | REQ-C-014, REQ-F-062 |
| 17 | A long polkit prompt holds the work slot, so other `Execute` calls get `Busy`. | Single-user assumption; `Cancel` cancels the prompt; `PreparePlan` stays allowed. | REQ-F-031, REQ-F-056 |
| 18 | `Execute` exceeds the default 25 s D-Bus call timeout. | Documented client contract in the introspection XML; the GUI wiring feature sets the timeout. | REQ-F-074 |
| 19 | Hardening directives break pacman hooks or scriptlets. | The list excludes directives that break pacman; VM smoke test under the unit. | REQ-C-015 |
| 20 | Plan creation exhausts memory or the work slot by a local user. | 16 plans per UID, 64 total, `Busy` beyond. | REQ-F-071 |

---

## Open items

Resolved items keep their number. A resolved item is not reopened by the implementation plan.

- **OI-01 — D-Bus names and signatures. Resolved (design section 5).** Well-known name `org.holonight.PackageHelper1`, object path `/org/holonight/PackageHelper1`, interface `org.holonight.PackageHelper1.Transactions`, methods `PreparePlan`, `Execute`, `Cancel`, signal `Progress`, typed `Status` and `Result` replies instead of D-Bus errors for domain outcomes. The exact signatures live in the introspection XML and are checked by REQ-C-005.
- **OI-02 — Code location. Resolved (design section 3.1).** Transaction core in `src/transaction` (STATIC `holonight_packages_transaction`), plan model, validation, equality and the engine port in `src/domain`, orchestration (plan store, service, idle monitor, `Authorizer` port) in `src/application`, D-Bus and polkit in `apps/helper`. `src/platform` stays an INTERFACE stub. REQ-C-001 and REQ-C-002 hold under this choice.
- **OI-03 — Base-closure resolution cap and cache. Resolved.** Cap 4096 packages, `InternalError` when exceeded, no cache (REQ-F-035).
- **OI-04 — Hook failure. Resolved (user decision).** A hook failure after the commit point yields outcome `Succeeded` with the `HookFailed` warning and a complete changed list. A PreTransaction hook failure rolls back and returns `TransactionFailed`. The result gains a warnings field (REQ-F-051, REQ-F-073). Detecting a post-commit hook failure uses libalpm's warning text; the exact text is pinned by spike S-4.
- **OI-05 — Signature level and database signatures. Resolved (user decision).** Package levels weaker than `Required` and `TrustAll` levels are refused (REQ-F-039). `DatabaseOptional` and `DatabaseNever` are accepted and journaled.
- **OI-06 — Authorization ordering. Resolved.** Authorize, then lock, then one re-resolution (REQ-F-044). Re-resolving before the prompt as well is rejected as redundant.
- **OI-07 — Limits on concurrent plans. Resolved (user decision).** 16 plans per UID and 64 in total, `Busy` beyond (REQ-F-071).
- **OI-08 — `IgnoreGroup`. Resolved (user decision).** Targets in an `IgnoreGroup` are refused with `PackageIgnored` (REQ-F-070).
- **OI-09 — Lock lifetime while a plan is pending. Resolved (confirmed).** The lock is held only during `Execute` (REQ-F-046).
- **OI-10 — Stale-mirror classification table. Resolved (spike S-4).** HTTP 404 and 500 end with `ALPM_ERR_RETRIEVE` and the text `The requested URL returned error: <code>`; every `file://` failure (missing file, mode 000, missing directory) and a stalled transfer end with `ALPM_ERR_LIBCURL`, the first with `Could not open file <path>` (decided by a local `stat`), the stall with `Operation too slow` after about 16 s. A missing `.sig` is a signature failure (spike S-6). The strings are recorded in `SPIKES.md`.
- **OI-11 — Systemd unit hardening. Resolved (user decision).** The design's list is accepted (REQ-C-015). It is confirmed by the VM smoke test in the manual checklist.
- **OI-12 — Journal sink. Resolved.** The helper writes to its standard error stream with journal priority prefixes under systemd; no libsystemd. Satisfies REQ-F-052 and REQ-NF-004.
- **OI-13 — Polkit subject. Resolved.** Unique name only, no start-time field (REQ-F-065).
- **OI-14 — Pre-commit hook execution order. Resolved (spike S-1).** `HOOK_START(PRE)` follows the file-conflict checks and precedes `TRANSACTION_START`; without hooks no hook event is raised, so `TRANSACTION_START` is the commit point. `alpm_trans_interrupt` is honoured from `TRANSACTION_START` on and ignored before it.
- **OI-15 — Option keys. Resolved.** None in this version; any key is `InvalidArgument` (REQ-F-016).
- **OI-16 — Cancel of an in-flight download. Resolved (spike S-2, user decision 2026-11-09).** Neither `alpm_trans_interrupt` nor SIGINT stops a running transfer. Cancel before the commit point ends the helper process after replying `Cancelled` (REQ-F-056); the bus re-activates it on the next call.
- **OI-17 — Remaining libalpm assumptions. Resolved (spikes S-3, S-5, S-6).** Hooks need `chroot`; as a plain user they fail, and under `unshare -Ur` they run (REQ-C-014). The corrupted-package question is raised during integrity checking and the same handle can run again after release (REQ-F-055). Key generation takes about 20 ms; `TrustedOnly` verification with a self-generated, ultimately trusted key works; a missing signing key ends with errno 0 and the text `required key missing from keyring`.

### Resolved design-review issues

| Issue | Resolution in this SPEC |
|---|---|
| SI-1 | REQ-F-019, REQ-F-040, glossary "Pacman settings parser" |
| SI-2, SI-3 | REQ-F-057, REQ-F-049, REQ-F-073, glossary "Commit point" |
| SI-4 | REQ-F-046, REQ-F-047, glossary "Database lock" |
| SI-5 | REQ-F-055 |
| SI-6 | REQ-F-024, REQ-F-053 |
| SI-7 | REQ-F-028, REQ-F-029, REQ-NF-001, OI-10 |
| SI-8 | REQ-F-002, REQ-F-043 |
| SI-9 | REQ-C-010, REQ-C-014 |
| SI-10 | REQ-F-056, OI-16 |
| SI-11 | REQ-F-002 |
| SI-12 | REQ-F-072, REQ-F-045, error taxonomy |
| SI-13 | REQ-F-031, glossary "Work slot" |
| SI-14 | REQ-F-039 |
| SI-15 | REQ-F-019, REQ-C-001 |
| SI-16 | REQ-C-007, REQ-F-065 |
| SI-17 | REQ-F-049 |
| SI-18 | REQ-C-012 |
| SI-19 | REQ-F-056, REQ-F-028 |
| SI-20 | REQ-F-074, REQ-F-051 |
