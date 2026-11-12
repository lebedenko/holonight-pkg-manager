# System upgrade from the Updates page — SPEC (HoloNight Packages)

Status: Implementation finalized, 2026-11-12. Automated acceptance passed; the five manual VM checks in TASKS.md remain release gates. Requirements are written in EARS. This SPEC extends `docs/sdd/package-helper-transactions/SPEC.md` (the helper, "helper SPEC") and `docs/sdd/gui-package-transactions/SPEC.md` (the GUI, "GUI SPEC"). It delivers OQ-07 of the GUI SPEC. Where this SPEC changes a helper or GUI requirement, the change is named in "Amendments to other specs" and applies to the upgrade action only unless it says otherwise.

Template labels: **[U]** Ubiquitous, **[E]** Event-driven, **[S]** State-driven, **[O]** Optional feature, **[X]** Unwanted behaviour. Requirement IDs: `REQ-F` functional, `REQ-NF` non-functional, `REQ-C` constraints.

## Overview

HoloNight Packages can install and remove one official package at a time. It cannot bring the system up to date. An install that needs a newer version of an installed package is refused with `PartialUpgradeRequired`, and the user has no remedy inside the app. This feature adds a full system upgrade, the equivalent of `pacman -Syu` for the official repositories. The user starts it with "Update system…" on the Updates page.

The upgrade runs in the privileged helper `holonight-package-helper`. It follows the same path as install and remove: the helper prepares a plan and a preview, the user confirms, polkit authorizes, and the helper runs exactly the previewed plan. Three differences matter:

- Preparing an upgrade refreshes the sync databases. It does so into a private scratch directory, so the live databases stay unchanged until the user confirms.
- Executing an upgrade has a point of no return. Before it, the system is unchanged. After it, a failure leaves a partly upgraded system. This SPEC reports that case and does not prevent it.
- Sync and upgrade are one helper action. No other action refreshes the sync databases (REQ-F-071).

## Scope

In scope:

- A new helper method `PrepareUpgrade(options)`, its plan lifecycle, and the preview of an upgrade.
- A private scratch root for the sync databases fetched at prepare time, and its cleanup.
- Question policy, authorization (polkit action `org.holonight.packages.upgrade`), the fetch phase, the live-database guard, promotion, the keyring-first ordering and the main step.
- New progress phases and status codes, and `ProtocolVersion` 2.
- The GUI: the Updates page button, the prepare and preview flow, execution, results, and the "Update system…" button in the `PartialUpgradeRequired` refusal dialog.
- The `pacman.conf` values the upgrade path reads and refuses.

## Non-goals

- Per-package update, multi-select of upgrades, or choosing which upgrades to apply. The preview is accepted or refused as a whole.
- A standalone database refresh (`-Sy` by itself), in the helper or in the GUI.
- Scheduled or automatic upgrades. Only a user confirmation starts one.
- AUR and foreign packages, PKGBUILD handling and any build step.
- Downgrades.
- `.pacnew` handling, reboot or post-upgrade advice (roadmap Phase 5).
- Transaction history.
- Flatpak and other package sources.
- Changing install or remove behaviour, except the new button in the `PartialUpgradeRequired` refusal dialog (REQ-F-089).

## Glossary

- **Upgrade action**: the helper's system upgrade. It upgrades every installed official package for which a newer version exists in the refreshed sync databases, adds the new dependencies this requires, applies replacements, and skips held-back packages.
- **Upgrade plan**: the plan created by `PrepareUpgrade`. Its action is `upgrade`. It holds the same record as an install or remove plan (helper REQ-F-001 to REQ-F-005 and REQ-F-071, which apply unchanged), plus the scratch root and the guard snapshot.
- **Scratch root**: a private directory, owned by root with mode `0711` (others may traverse but not list it, so libalpm's `DownloadUser` sandbox can reach its own download directory), inside the helper's systemd `StateDirectory` (under `/var/lib`). It holds one upgrade plan's scratch dbpath and scratch sync databases. It is never returned to a caller.
- **Scratch dbpath**: the libalpm database path used for the prepare and fetch steps. Its `local` entry is shared read-only from the live dbpath (as `checkupdates` does). Its `sync/` directory holds the scratch sync databases.
- **Scratch sync databases**: the sync databases of all enabled repositories, downloaded into the scratch dbpath during `PrepareUpgrade`.
- **Live sync databases**: the files under `sync/` of the configured `DBPath`. Only promotion changes them.
- **Guard snapshot**: for each live sync database file, its SHA-256, size and modification time, taken during `PrepareUpgrade`.
- **Fetch phase**: a `DOWNLOADONLY` transaction on the scratch dbpath that downloads every package file of the plan into the configured `CacheDir`.
- **Promotion**: replacement of each live sync database file by the scratch file of the same name.
- **Point of no return (upgrade)**: the start of promotion. Before it, nothing in the live databases or the installed set has changed.
- **Keyring step**: the commit of `archlinux-keyring`, and of the dependencies it needs from the plan, as a transaction of its own. It runs before the main step.
- **Main step**: the commit of the remaining packages of the plan, from the cache.
- **Step counter**: the step index `i` and step count `n` of the upgrade (`n` is 2 when a keyring step exists, otherwise 1).
- **Held-back package**: a package with a newer version available that is not upgraded because `IgnorePkg` or `IgnoreGroup` lists it.
- **Replacement**: a sync package whose `replaces` entry names an installed package. The upgrade removes that installed package and installs the sync package in its place.
- **Upgrade preview**: the preview returned by `PrepareUpgrade`, defined by REQ-F-019.
- **Upgrade plan equality**: plan equality of helper REQ-F-043, extended by REQ-F-025.
- **Guard**: the check of the live sync databases against the guard snapshot (REQ-F-051).
- **Work slot**, **Busy**, **Commit gate**, **Journal**, **Safe default**: as in the helper SPEC.

---

## Requirements

### 1. Request and protocol

### REQ-F-001: Upgrade preparation needs no authorization [E]
**Statement:** When `PrepareUpgrade(options)` is called, the helper shall prepare an upgrade plan without requiring polkit authorization and shall return a status, a plan ID and an upgrade preview.
**Acceptance criteria:**
- With an Authorizer that denies every request, `PrepareUpgrade` on a fixture where `app` has a newer version returns a plan ID, and the Authorizer records zero calls.
- Two consecutive calls on the same fixture return different plan IDs, each 32 lowercase hexadecimal characters.

### REQ-F-002: Upgrade options must be empty [U]
**Statement:** The helper shall accept `PrepareUpgrade` only with an empty options map, and shall return `InvalidArgument` for any other options map.
**Acceptance criteria:**
- Options containing the key `noconfirm` return `InvalidArgument`, and the libalpm initialization counter stays 0.
- Options containing the key `IgnorePkg` return `InvalidArgument`, so a caller cannot change the pacman configuration for one upgrade.
- An empty options map on the same fixture returns a plan.

### REQ-F-003: Upgrade preparation is a delayed reply with a bounded internal timeout [E]
**Statement:** When `PrepareUpgrade` is called, the helper shall answer with a delayed reply, and shall answer `SyncFailed` when the sync download has not finished within an internal timeout or exceeds the scratch size cap; because libalpm cannot stop a running download, the helper shall then end its process after the reply, as for a cancel (REQ-F-032). The internal timeout shall be shorter than 600 seconds, so that the helper replies before the documented client call timeout of 10 minutes is reached.
**Acceptance criteria:**
- A static check of the build constants finds the internal sync timeout value below 600 seconds (value fixed by spike S-4).
- On the private bus with a fixture repository whose download is held by a test seam and the internal timeout set to 1 second, `PrepareUpgrade` returns `SyncFailed` within 6 seconds, and the live manifest is unchanged (REQ-F-007).
- The introspection XML documents a call timeout of 10 minutes for `PrepareUpgrade`.

### REQ-F-004: One upgrade preparation at a time [X]
**Statement:** If `PrepareUpgrade` is called while the work slot is `Preparing`, `Authorizing` or `Running`, or while another upgrade plan's fetch is in progress, then the helper shall return `Busy`.
**Acceptance criteria:**
- While a fixture `PrepareUpgrade` is held by a test seam, a second `PrepareUpgrade` from a second connection returns `Busy` within 100 ms.
- While the same preparation runs, a `PreparePlan` for `install` also returns `Busy`.

### REQ-F-005: Upgrade plans count against the plan limits [X]
**Statement:** If `PrepareUpgrade` is called while the caller already owns 16 live plans, or while the helper holds 64 live plans in total, then the helper shall return `Busy`, create no plan and create no scratch root.
**Acceptance criteria:**
- With 16 live plans of one UID, the 17th `PrepareUpgrade` returns `Busy`, and no directory is created under the state directory.
- After one of those plans is cancelled, the next `PrepareUpgrade` succeeds.

### 2. Scratch root and sync database freshness

### REQ-F-006: Preparation fetches sync databases into the scratch root [E]
**Statement:** When `PrepareUpgrade` runs, the helper shall download the sync databases of every enabled repository into the scratch sync databases, and shall resolve the upgrade against them.
**Acceptance criteria:**
- With two fixture repositories `core` and `extra` that list `app 2.0` and `lib 2.0` respectively, the scratch `sync/` directory contains `core.db` and `extra.db` after the reply, and their SHA-256 values equal the fixture repository databases.
- The preview lists `app` and `lib` with the new versions, although the live sync databases still list the old versions.

### REQ-F-007: Preparation leaves the live databases unchanged [U]
**Statement:** The helper shall not modify, create or remove any file under the live `sync/` or `local/` directories during `PrepareUpgrade`, whether the call succeeds or refuses.
**Acceptance criteria:**
- A manifest of the live `sync/` and `local/` directories (names, sizes, SHA-256 and modification times) is identical before and after a successful `PrepareUpgrade`.
- The same manifest is identical after a `PrepareUpgrade` that returns `SyncFailed` because the fixture mirror is unreachable.

### REQ-F-008: The scratch root is private and never returned [U]
**Statement:** The helper shall create the scratch root as a directory owned by root with mode `0711` inside its state directory (itself mode `0711`), keep libalpm's `DownloadUser` sandbox in effect for the database sync, and shall not return its path, its file names or its contents to any caller.
**Acceptance criteria:**
- After a successful `PrepareUpgrade` run as root in the fixture, the scratch root has owner UID 0 and mode `0711`, and a non-root user cannot list it (`opendir` fails with `EACCES`).
- With `DownloadUser` set in the fixture configuration, the scratch handle has the same sandbox user as an install handle (checked through `alpm_option_get_sandboxuser`).
- No reply field of the `PrepareUpgrade` call, and no `Progress` signal, contains the scratch root path (a byte search of the captured replies finds no `StateDirectory` value).

### REQ-F-009: Unreachable mirror during preparation [X]
**Statement:** If the sync database of any enabled repository cannot be downloaded, then the helper shall return `SyncFailed`, remove the scratch root and change nothing live.
**Acceptance criteria:**
- With a fixture repository whose URL points to an absent directory, `PrepareUpgrade` returns `SyncFailed`, and the scratch root no longer exists afterwards.
- After that reply, the live manifest is identical to the one taken before the call, and no `db.lck` file exists.

### REQ-F-010: Scratch size cap [X]
**Statement:** If the scratch sync databases together exceed the scratch size cap (value fixed by spike S-4), then the helper shall stop the download, return `SyncFailed`, and remove the scratch root.
**Acceptance criteria:**
- With a test seam that sets the cap to 1 KiB and a fixture database of 4 KiB, `PrepareUpgrade` returns `SyncFailed` and the scratch root is absent afterwards.
- With the cap at its production value, the three-repository fixture is accepted.

### REQ-F-011: Sync database signature failure [X]
**Statement:** If a scratch sync database fails its signature check under the configured database signature level, then the helper shall return `DatabaseUnavailable`, remove the scratch root and change nothing live.
**Acceptance criteria:**
- With a fixture `core.db.sig` replaced by a signature of another key, `PrepareUpgrade` returns `DatabaseUnavailable`, and the live manifest is unchanged.
- With the same fixture and the original signature, the call does not return `DatabaseUnavailable`.

### REQ-F-012: Scratch root kept for the plan [E]
**Statement:** When `PrepareUpgrade` returns a plan, the helper shall keep the scratch root until the plan is discarded, executed or expired, and shall resolve the upgrade for `Execute` against the scratch sync databases only.
**Acceptance criteria:**
- After the reply, the scratch root exists and its database hashes equal those recorded at prepare time.
- A change to the live `sync/` files between prepare and execute does not change the preview of the plan (the guard then refuses at REQ-F-051, not the re-resolution).

### REQ-F-013: Scratch root removed when a plan is discarded [E]
**Statement:** When an upgrade plan is discarded by expiry, by `Cancel` of the bound caller, or by the disconnect of the bound caller, the helper shall remove its scratch root in the same step that discards the plan.
**Acceptance criteria:**
- After a `Cancel` of a prepared plan, the scratch root is absent and a second `Execute` returns `PlanNotFound`.
- After the owner's connection is dropped with a plan prepared, the scratch root is absent within 1 second.
- After expiry on a fake clock at 301 seconds, the scratch root is absent when `Execute` returns `PlanExpired`.

### REQ-F-014: Scratch root removed after execution [E]
**Statement:** When `Execute` of an upgrade plan returns, whatever its outcome, the helper shall remove the scratch root before it sends the reply.
**Acceptance criteria:**
- After a `Succeeded` upgrade, the scratch root is absent when the reply is received.
- After a `Failed` upgrade with `SyncDatabaseChanged`, the scratch root is absent when the reply is received.
- After a `Cancelled` upgrade, the scratch root is absent when the reply is received.

### REQ-F-015: Stale scratch roots are removed at start [U]
**Statement:** The helper shall remove every scratch root it finds in its state directory before it accepts its first call after start.
**Acceptance criteria:**
- With a directory pre-created in the fixture state directory with one database file in it, the first `PreparePlan` call after the helper starts finds the directory absent.
- The live manifest is unchanged by the start-time removal.

### REQ-F-016: Helper killed during preparation [X]
**Statement:** If the helper process ends while an upgrade is being prepared, then the next helper process shall find the scratch root left behind, remove it under REQ-F-015, and leave the live databases unchanged.
**Acceptance criteria:**
- A test seam kills the helper process while the sync download is held. The next call starts a new helper, the scratch root is absent before the new helper answers, and the live manifest is identical to the one taken before the killed call.
- The next `PrepareUpgrade` on that fixture returns a plan.

### REQ-F-017: The caller supplies no repository or server [U]
**Statement:** The helper shall take the repositories and their servers only from the pacman configuration set by its composition root, and no argument of `PrepareUpgrade` shall carry a repository, a server or a URL.
**Acceptance criteria:**
- The introspection XML declares the single argument `options` for `PrepareUpgrade`, and no argument named for a server, a URL or a repository.
- With a fixture `Server` line pointing to a second directory that holds a newer `app`, the scratch download requests only the URLs built from the configured `Server` lines (the second directory receives zero requests).

### REQ-F-018: Repository without a server [X]
**Statement:** If a repository enabled in the pacman configuration has no `Server` line, then the helper shall return `ConfigurationError` naming the repository, and shall create no plan.
**Acceptance criteria:**
- With a fixture repository section that has no `Server` line, `PrepareUpgrade` returns `ConfigurationError` naming that repository, and the scratch root is absent afterwards.
- With the `Server` line added, the same fixture returns a plan.

### 3. Preview

### REQ-F-019: Upgrade preview content [E]
**Statement:** When `PrepareUpgrade` returns a plan, the helper shall return a preview that lists, in separate fields: each upgrade with the name, the old version, the new version and the repository; each replacement with the old and the new package name; each new dependency to install with its name, version and repository; each held-back package with its name and installed version; the keyring flag and the keyring package names; the total download size in bytes; and the installed-size change in bytes; and the plan's expiry time.
**Acceptance criteria:**
- With a fixture of two upgrades, one replacement, one new dependency and one held-back package, the preview has exactly those entries in the four lists, each entry in its own field.
- The download size equals the sum of the sizes of the fixture package files, and the installed-size change equals the sum of the new sizes minus the old sizes of the fixture packages.

### REQ-F-020: Replacements are accepted and shown [E]
**Statement:** When a sync package has a `replaces` entry for an installed package in an upgrade, the helper shall accept the replacement, list it in the preview as a replacement of the old package by the new one, and shall not return `NeedsDecision` for it.
**Acceptance criteria:**
- With a fixture `newpkg` that replaces the installed `oldpkg`, `PrepareUpgrade` returns a plan whose replacement list contains `oldpkg` and `newpkg`.
- After a successful `Execute` of that plan, `oldpkg` is not installed and `newpkg` is.

### REQ-F-021: Held-back packages are skipped and listed [E]
**Statement:** When a package with a newer version is listed in `IgnorePkg` or in an `IgnoreGroup` group, the helper shall not upgrade it, shall list it as held back in the preview, and shall keep it at its installed version.
**Acceptance criteria:**
- With `IgnorePkg = foo` and `foo 2.0` in the scratch databases while `foo 1.0` is installed, the held-back list contains `foo` with version `1.0`, and no upgrade entry names `foo`.
- After a successful `Execute` of that plan, `foo` is still at `1.0`.

### REQ-F-022: Held-back package that breaks dependency resolution [X]
**Statement:** If a held-back package makes a dependency of an upgraded or new package unresolvable, then the helper shall return `PackageNotFound` naming the unresolvable dependency, and shall create no plan.
**Acceptance criteria:**
- With `foo 1.0` held back by `IgnorePkg` and a sync package `app 2.0` that requires `foo>=2`, `PrepareUpgrade` returns `PackageNotFound` naming `foo`, and no plan ID is returned.

### REQ-F-023: Keyring-first flag [E]
**Statement:** When the plan contains `archlinux-keyring` or a dependency of it that the plan must install or upgrade, the helper shall set the keyring flag in the preview and list those packages as the keyring packages.
**Acceptance criteria:**
- With a fixture `archlinux-keyring 2.0` upgrade, the keyring flag is true and the keyring package list is `archlinux-keyring`.
- With a fixture that upgrades `app` only, the keyring flag is false and the keyring package list is empty.

### REQ-F-024: Nothing to upgrade [X]
**Statement:** If the scratch sync databases offer no upgrade, no replacement and no new dependency for the installed set, then the helper shall return `NothingToDo` with an empty plan ID and create no plan.
**Acceptance criteria:**
- On a fixture where every installed package is at the version in the scratch databases, `PrepareUpgrade` returns `NothingToDo` and no plan ID.
- The live manifest is unchanged after that reply.

### REQ-F-025: Upgrade plan equality [U]
**Statement:** The helper shall treat two upgrade plans as equal only when they have the same action, the same set of upgrades, replacements, new dependencies and held-back packages (each entry compared by name, old version, new version and repository), the same keyring flag and keyring package list, and no refusal condition applying to either plan. Download sizes and mirror URLs shall not take part in the comparison.
**Acceptance criteria:**
- A unit test shows that changing only a mirror URL or a download size leaves two upgrade plans equal.
- A unit test shows that changing one new version, one held-back version or the keyring flag makes two upgrade plans unequal.

### 4. Questions raised by libalpm during an upgrade

### REQ-F-026: Conflicts are refused with a decision [X]
**Statement:** If the upgrade would install a package that conflicts with an installed package which the plan does not replace, then the helper shall return `NeedsDecision` naming both packages, and shall create no plan.
**Acceptance criteria:**
- With an installed `old-x` and a sync `new-y` that conflicts with `old-x` and does not replace it, `PrepareUpgrade` returns `NeedsDecision` naming `old-x` and `new-y`.
- With the same packages and a `replaces` entry in `new-y` for `old-x`, the call does not return `NeedsDecision` (REQ-F-020).

### REQ-F-027: Removals with unresolvable dependencies are refused [X]
**Statement:** If libalpm would remove an installed package because it has an unresolvable dependency after the upgrade, then the helper shall return `NeedsDecision` naming the package to be removed, and shall create no plan.
**Acceptance criteria:**
- With an installed `ext-tool` that requires `lib<2` and an upgrade of `lib` to `2.0` where `ext-tool` has no newer version, `PrepareUpgrade` returns `NeedsDecision` naming `ext-tool`.

### REQ-F-028: Provider choices are refused with a decision [X]
**Statement:** If the upgrade needs a dependency that has several providers, then the helper shall return `NeedsDecision` listing the providers, and shall not accept the first provider.
**Acceptance criteria:**
- With two sync providers `prov-a` and `prov-b` for a new dependency `virt`, `PrepareUpgrade` returns `NeedsDecision` naming both, and no plan ID is returned.
- The answer given to libalpm for the provider question is the non-acting answer, and the journal names the question type.

### REQ-F-029: Key import during an upgrade is refused [X]
**Statement:** If libalpm asks to import a signing key during `PrepareUpgrade` or during the fetch phase, then the helper shall answer the question with "no", journal it and return `KeyImportRequired` naming the packages whose signature needs the key, and nothing live shall change.
**Acceptance criteria:**
- With a fixture package signed by a key that is not in the keyring, `Execute` returns `KeyImportRequired` naming that package, the GPG directory manifest is unchanged, and the live manifest is unchanged.
- The same fixture in an install plan still returns `SignatureInvalid` (REQ-F-031).
- Exception, when the plan has a keyring step: a package that is not a keyring package and whose key is missing from the current keyring is not refused at fetch; its check is deferred to the main step, which verifies it with the updated keyring (SPIKES S-1 c). With a fixture whose keyring step adds that key, `Execute` returns `Succeeded`. With a fixture whose keyring step does not add it, `Execute` returns `UpgradePartiallyApplied` with step token `main`, and the changed list holds the keyring package (REQ-F-062).

### REQ-F-030: Upgrade question policy is limited to upgrades [U]
**Statement:** The helper shall apply REQ-F-020 and REQ-F-026 to REQ-F-029 only to upgrade plans. Install and remove plans shall keep helper REQ-F-024 and REQ-F-053 unchanged.
**Acceptance criteria:**
- With the replacement fixture of REQ-F-020, `PreparePlan` for `install` returns `NeedsDecision` naming both packages.
- The same fixture accepted by `PrepareUpgrade` returns a plan, so the two actions differ by design.

### REQ-F-031: Signature trust rules apply to upgrades [U]
**Statement:** The helper shall apply helper REQ-F-039, REQ-F-040 and REQ-F-054 to the scratch sync databases and to the upgrade's packages, and shall return `InsecureSigLevel` for an upgrade whose enabled repository has a weak signature level.
**Acceptance criteria:**
- With `SigLevel = Optional` on one repository, `PrepareUpgrade` returns `InsecureSigLevel` and the fetch callback counter stays 0.
- With `SigLevel = Required TrustedOnly` on every repository, the same fixture does not return `InsecureSigLevel`.

### 5. Cancellation, disconnect and expiry during preparation and before execution

### REQ-F-032: Cancel cancels the caller's own preparation [E]
**Statement:** When the bound caller calls `Cancel("")` while its `PrepareUpgrade` is in flight, the helper shall cancel that preparation, answer the `PrepareUpgrade` call with status code `Cancelled`, remove the scratch root, answer `Cancel` with success and end the helper process after the reply, because libalpm cannot stop a running download.
**Acceptance criteria:**
- With the sync download held by a test seam and a recording terminator, `Cancel("")` returns success, `PrepareUpgrade` returns `Cancelled`, the scratch root is absent, the terminator is invoked once and only after the reply.
- The live manifest and the absence of `db.lck` hold after the cancel.

### REQ-F-033: Cancel with an empty ID from another caller [X]
**Statement:** If a caller other than the one whose preparation is in flight calls `Cancel("")`, then the helper shall return `PlanNotFound` and shall not affect that preparation.
**Acceptance criteria:**
- With a second connection calling `Cancel("")` during a held preparation, the reply is `PlanNotFound`, and the preparation later returns a plan.

### REQ-F-034: Preparation progress goes to its caller only [E]
**Statement:** While `PrepareUpgrade` runs, the helper shall send `Progress("", "Syncing", i, n, repo)` signals, where `i` counts the repositories downloaded and `n` is the number of enabled repositories, only to the unique bus name of the caller.
**Acceptance criteria:**
- With two fixture repositories, the owner receives two `Syncing` signals with `n` equal to 2 and `i` values 1 and 2, in that order.
- A second connection with a match rule for `Progress` receives zero signals during the preparation.

### REQ-F-035: Owner disconnect during preparation is a cancel [X]
**Statement:** If the bus connection of the caller whose `PrepareUpgrade` is in flight disconnects, then the helper shall treat it as `Cancel("")` under REQ-F-032.
**Acceptance criteria:**
- With the owner's connection dropped while the download is held, the scratch root is absent within 1 second, the live manifest is unchanged and no `db.lck` exists.

### REQ-F-036: Owner disconnect after preparation discards the plan [X]
**Statement:** If the bus connection of the bound caller of an upgrade plan disconnects before `Execute` is called, then the helper shall discard the plan under helper REQ-F-012 and remove its scratch root.
**Acceptance criteria:**
- With a prepared plan and the owner's connection dropped, the scratch root is absent and a later `Execute` by another connection returns `PlanNotFound`.

### REQ-F-037: Expired upgrade plan [X]
**Statement:** If `Execute` is called for an upgrade plan more than 300 seconds after its creation, then the helper shall return `PlanExpired`, discard the plan and remove its scratch root, as helper REQ-F-005 and REQ-F-006 require.
**Acceptance criteria:**
- With a fake clock at 301 seconds, `Execute` returns `PlanExpired`, the scratch root is absent, and the Authorizer records no call.
- With a fake clock at exactly 300 seconds, `Execute` reaches the authorization step.

### REQ-F-038: Busy during an upgrade execution [X]
**Statement:** If `PrepareUpgrade` or `Execute` is called while an upgrade plan is being executed, then the helper shall return `Busy`, and an `Execute` that returned `Busy` shall leave its plan unconsumed.
**Acceptance criteria:**
- While an upgrade is held in its fetch phase, a second `PrepareUpgrade` and a second `Execute` each return `Busy` within 100 ms.
- After the first upgrade ends, the second `Execute` of its own plan, if still within 300 seconds, returns a result rather than `PlanNotFound`.

### 6. Authorization and start of execution

### REQ-F-039: Upgrade authorization identity [U]
**Statement:** The polkit policy shall define the action `org.holonight.packages.upgrade` for upgrade plans, with the defaults `auth_admin_keep` for the `any`, `inactive` and `active` categories.
**Acceptance criteria:**
- A check of the policy XML finds the action ID and exactly these three defaults.
- The install and remove action IDs and their defaults are unchanged in the same file (helper REQ-F-063 and REQ-F-064).

### REQ-F-040: Upgrade authorization subject [E]
**Statement:** When `Execute` of an upgrade plan reaches authorization, the helper shall call polkit `CheckAuthorization` for `org.holonight.packages.upgrade` with the same subject, flags and cancellation rules as helper REQ-F-065, and with a detail that carries the number of upgrades, replacements and new packages, not their names.
**Acceptance criteria:**
- The fake Authorizer records the action `org.holonight.packages.upgrade`, a `system-bus-name` subject with the caller's unique name, the `AllowUserInteraction` flag, and the plan ID as cancellation ID.
- The recorded details contain three integers and no package name.

### REQ-F-041: Denied upgrade keeps the plan [X]
**Statement:** If the polkit check for an upgrade returns an error, a challenge or a refusal, then the helper shall return `NotAuthorized`, leave the plan unconsumed and perform no fetch.
**Acceptance criteria:**
- With a denying Authorizer, `Execute` returns `NotAuthorized`, the fetch callback counter stays 0, and the scratch root still exists.
- With the Authorizer then allowing, a second `Execute` within 300 seconds of creation runs the upgrade.

### 7. Fetch phase

### REQ-F-042: Fetch phase downloads the package files into the cache [E]
**Statement:** When an upgrade plan is authorized, the helper shall run a fetch phase that downloads every package file of the plan into the configured `CacheDir` through a `DOWNLOADONLY` transaction on the scratch dbpath, before any live change.
**Acceptance criteria:**
- With a fixture of three upgrades and one new dependency, after the fetch phase (held by a test seam before promotion) the fixture `CacheDir` contains four package files whose SHA-256 values equal the fixture files.
- At that point the live manifest is unchanged.

### REQ-F-043: Fetch writes only the cache and the scratch root [U]
**Statement:** During the fetch phase the helper shall write only under the configured `CacheDir` and the scratch root, and shall not write under the live `sync/`, `local/`, the installed root or the GPG directory (no key is imported in this version, REQ-F-029).
**Acceptance criteria:**
- A manifest of the fixture root excluding `CacheDir` and the scratch root is identical before and after a fetch phase that ends with a cancel.
- The GPG directory manifest is identical after a successful fetch phase.

### REQ-F-044: Package missing from the mirror during fetch [X]
**Statement:** If the download of a planned package fails with an absent file (helper REQ-F-028 rules), then the helper shall return `SyncDatabaseStale` naming the package, and leave the live databases and the installed set unchanged.
**Acceptance criteria:**
- With the `foo 2.0` package file deleted from a fixture repository that still lists it, `Execute` returns `SyncDatabaseStale` naming `foo`, and the live manifest equals the manifest before the call.
- The local database manifest is unchanged, and the cache contains no file for `foo`.

### REQ-F-045: Other download failure during fetch [X]
**Statement:** If the download of a planned package fails for any reason other than an absent file, then the helper shall return `DownloadFailed` naming the package, and leave the live state unchanged.
**Acceptance criteria:**
- With a fixture package file made unreadable (mode 000, non-root test run), `Execute` returns `DownloadFailed`, and the live manifest is unchanged.
- With a test seam that reports a connection error for one package, `Execute` returns `DownloadFailed`, and never `SyncDatabaseStale`.

### REQ-F-046: Corrupt download is retried once during fetch [O]
**Statement:** Where a downloaded package fails its checksum during the fetch phase, the helper shall delete the file and download it once more; if the second download also fails the checksum, the helper shall return `DownloadFailed` naming the package.
**Acceptance criteria:**
- With a fixture serving a corrupt file on the first request and the valid file on the second, `Execute` proceeds past the fetch phase.
- With a fixture serving a corrupt file on both requests, `Execute` returns `DownloadFailed`, and the second request is the last one made.

### REQ-F-047: Invalid package signature during fetch [X]
**Statement:** If a downloaded package fails its signature check during the fetch phase, then the helper shall not retry it, shall return `SignatureInvalid` naming the package and shall leave the live state unchanged.
**Acceptance criteria:**
- With a fixture package whose `.sig` is corrupt (or a fixture package without its `.sig`, which libalpm reports as a download failure of the `.sig` file and which the helper classifies as `SignatureInvalid`, helper SPIKES.md), `Execute` returns `SignatureInvalid` naming the package, and the download count for that package is 1. A signature by a key missing from the keyring is `KeyImportRequired` (REQ-F-029), not this requirement.
- The live manifest is unchanged afterwards.

### REQ-F-048: Disk space refusal before the point of no return [O]
**Statement:** Where `CheckSpace` is enabled in `pacman.conf`, the helper shall estimate the free space before the point of no return (download bytes on the cache filesystem, positive installed-size change plus a margin on the root filesystem, by `statvfs`), and shall return `DiskSpace` when either is short; libalpm's own disk-space check still runs inside each step. Where `CheckSpace` is disabled, the helper shall not run the estimate.
**Acceptance criteria:**
- With `CheckSpace = yes` and the disk-space seam reporting less free space than the estimate needs, `Execute` returns `DiskSpace` and the live manifest is unchanged.
- With `CheckSpace` disabled and the same seam, the estimate is not called (its counter stays 0) and the outcome is not `DiskSpace` before the point of no return.
### REQ-F-049: Cancel during the fetch phase [E]
**Statement:** When the bound caller calls `Cancel(planId)` while the fetch phase runs, the helper shall answer the `Execute` call with outcome `Cancelled`, remove the lock file its transaction holds, remove the scratch root, leave the live state unchanged, and end the helper process after the reply, as helper REQ-F-056 does for install and remove.
**Acceptance criteria:**
- With a fetch held by a test seam and a recording terminator, `Cancel` returns success, `Execute` returns `Cancelled`, the terminator is invoked exactly once after the reply, `db.lck` is absent, and the scratch root is absent.
- The live manifest and the fixture `local/` manifest are unchanged.

### 8. Point of no return: guard, re-resolution, promotion and steps

### REQ-F-050: Live lock before the guard [E]
**Statement:** When the fetch phase ends, the helper shall take the live libalpm database lock by starting the commit transaction on the live dbpath, before it checks the guard and before it promotes anything.
**Acceptance criteria:**
- A test seam at the guard finds `db.lck` present in the live dbpath.
- With a pre-created `db.lck`, `Execute` returns `Busy` within 1 second, the plan stays executable (helper REQ-F-009), and the pre-created file is not removed.

### REQ-F-051: Guard against changed live sync databases [E]
**Statement:** When the live lock is taken, the helper shall compare each live sync database file with its guard snapshot by SHA-256, size and modification time. If any file differs, or a file has been added or removed, then the helper shall return `SyncDatabaseChanged`, consume the plan, remove the scratch root, and change nothing live. The guard covers the `*.db` and `*.db.sig` files of the live `sync/` directory; other files there (for example `*.files` from `pacman -Fy`) are outside it.
**Acceptance criteria:**
- With a fixture that runs `pacman -Sy` (or a test that rewrites `core.db` with the same content and a new mtime) between `PrepareUpgrade` and `Execute`, `Execute` returns `SyncDatabaseChanged`, the live manifest is identical to the one before `Execute`, and `db.lck` is absent.
- A second `PrepareUpgrade` on that fixture after the refusal returns a plan.

### REQ-F-052: Re-resolution before promotion [E]
**Statement:** When the guard passes, the helper shall re-resolve the upgrade against the scratch sync databases and the live `local/` database, and shall compare the result with the preview under upgrade plan equality (REQ-F-025). If the result differs, or the re-resolution refuses, then the helper shall return `PlanChanged`, consume the plan and change nothing live.
**Acceptance criteria:**
- With an install of a new package from the fixture that runs between prepare and execute (changing the live `local/` database), `Execute` returns `PlanChanged`, and the live manifest is identical to the one before `Execute`.
- With no change, the re-resolution matches and the plan proceeds to promotion.

### REQ-F-053: Promotion starts the point of no return [E]
**Statement:** When the guard and the re-resolution pass, the helper shall start promotion, which is the point of no return for the upgrade. From that moment `Cancel` is refused (REQ-F-058), and promotion replaces each live sync database file by the scratch file of the same name, one file at a time, with a rename that is atomic per file.
**Acceptance criteria:**
- A test seam placed at the start of promotion finds the live `sync/` files unchanged and `Cancel` then returns `PastPointOfNoReturn`.
- After promotion, the SHA-256 of each live `sync/` file equals the scratch file of the same name, and no temporary file remains in the live `sync/` directory.

### REQ-F-054: Promotion failure rolls back the databases [X]
**Statement:** If promotion fails before the first package change, then the helper shall restore the previous content of every live sync database file it has already replaced, return `InternalError`, and leave the installed set unchanged.
**Acceptance criteria:**
- With a fault injected into the rename of the second of three sync database files, the first file's SHA-256 equals its value before `Execute`, all three files are unchanged, and `Execute` returns `InternalError`.
- The local database manifest is unchanged.

### REQ-F-055: Keyring step runs first, in its own transaction [E]
**Statement:** When the plan has a keyring step, the helper shall commit the keyring packages as a transaction of their own, before the main step, and shall start the main step only after the keyring step has succeeded.
**Acceptance criteria:**
- With a fixture upgrade of `archlinux-keyring` and `app`, the transaction event log records the keyring commit, then the main commit, and the progress phase `KeyringStep` precedes the main step's first `Installing` signal.
- After a successful keyring step the keyring package has its new version in the local database before the main step begins (checked by the seam at the start of the main step).

### REQ-F-056: Main step installs from the cache only [E]
**Statement:** When the main step runs, the helper shall commit the remaining plan packages from the cache. If libalpm needs to download a package during the main step, then the main step shall fail, and the helper shall make no download request.
**Acceptance criteria:**
- With a test seam that deletes one cached package file between the fetch phase and the main step, the main step fails and the fetch callback counter for the main step stays 0.
- The failure is reported under REQ-F-061 with the step token `main`.

### REQ-F-057: Step counter in progress [U]
**Statement:** When an upgrade step starts, the helper shall send one marker `Progress` signal with phase `KeyringStep` or `MainStep`, `current` equal to the step index `i` and `total` equal to the step count `n`; the other signals of the step keep the version 1 meaning of `current` and `total`, and a client shows "step i of n" from the last marker it received.
**Acceptance criteria:**
- With a keyring fixture, the signal sequence contains exactly one `KeyringStep` (1, 2) followed later by exactly one `MainStep` (2, 2), and the keyring step's `Installing` signals come between them.
- With no keyring in the plan, the sequence contains no `KeyringStep` and exactly one `MainStep` (1, 1).
- Install and remove plans never send `KeyringStep` or `MainStep`.

### REQ-F-058: Cancel after the point of no return is refused [X]
**Statement:** If `Cancel` is called for an upgrade plan after promotion has started, then the helper shall return `PastPointOfNoReturn` and let the upgrade continue unchanged.
**Acceptance criteria:**
- With a `Cancel` issued by a seam at the start of the keyring step, the reply is `PastPointOfNoReturn`, and the `Execute` outcome is `Succeeded`.
- The helper does not end its process when the cancel is refused.

### REQ-F-059: Disconnect after the point of no return [X]
**Statement:** If the bound caller disconnects after promotion has started, then the helper shall let the upgrade continue to its end, and shall record its result in the journal.
**Acceptance criteria:**
- With the owner's connection dropped at the start of the main step, the local database shows the new versions after the upgrade ends, and the journal records the result.

### 9. Failures after the point of no return

### REQ-F-060: Keyring step failure after promotion [X]
**Statement:** If the keyring step fails after promotion has started, then the helper shall return outcome `Failed` with code `UpgradePartiallyApplied`, the step token `keyring` in the status names, and an empty changed list, and shall not start the main step.
**Acceptance criteria:**
- With a fixture keyring package whose commit is made to fail, `Execute` returns `UpgradePartiallyApplied` with the step token `keyring`, the changed list is empty, and the local database has no new version for any upgraded package.
- The live `sync/` files equal the scratch files (promotion was complete).

### REQ-F-061: Main step failure after promotion [X]
**Statement:** If the main step fails after promotion has started, then the helper shall return outcome `Failed` with code `UpgradePartiallyApplied`, the step token `main` in the status names, and the changed list of the packages whose changes completed (helper REQ-F-060 rules).
**Acceptance criteria:**
- With a fault injected while installing the second of three main-step packages, `Execute` returns `UpgradePartiallyApplied` with the step token `main`, and the changed list contains only the first package.
- The local database contains only that package's change.

### REQ-F-062: Keyring updated and main step failed [X]
**Statement:** If the keyring step succeeded and the main step failed, then the helper shall return the changed list with the keyring package and its new version, so that the result shows the keyring was updated, together with code `UpgradePartiallyApplied` and step token `main`.
**Acceptance criteria:**
- With a keyring step that succeeds and a main-step fault as in REQ-F-061, the changed list contains `archlinux-keyring` with its new version as first entry.
- The status names contain `main` and do not contain `keyring`.

### REQ-F-063: Re-running after a partial upgrade completes it [U]
**Statement:** After an `UpgradePartiallyApplied` result, a new upgrade plan prepared and executed on the same system shall complete the remaining changes as an ordinary upgrade, without a special mode.
**Acceptance criteria:**
- After an `UpgradePartiallyApplied` result at step `main` on a fixture with three upgrades, the next `PrepareUpgrade` lists only the not-yet-upgraded packages, and its `Execute` returns `Succeeded`.
- After that, `PrepareUpgrade` returns `NothingToDo`.

### REQ-F-064: Hook failure after promotion [X]
**Statement:** If a PostTransaction hook fails after promotion has started, then the helper shall keep the outcome `Succeeded` and add the warning `HookFailed`, as helper REQ-F-073 does.
**Acceptance criteria:**
- With a fixture PostTransaction hook that exits non-zero, the upgrade outcome is `Succeeded`, the warnings list contains `HookFailed`, and the changed list is complete.

### 10. Progress and result

### REQ-F-065: Upgrade progress phases [E]
**Statement:** When an upgrade runs, the helper shall emit `Progress` signals whose phases occur in the order `Syncing` (during `PrepareUpgrade`), then `Downloading`, `Verifying` (fetch phase), then `Promoting`, then `KeyringStep` when a keyring step exists, then `Hooks`, `Installing` and the remaining existing phases, with `Done` last. `Done` shall be sent only after success.
**Acceptance criteria:**
- A keyring fixture records the phase sequence `Syncing`, `Downloading`, `Verifying`, `Promoting`, `KeyringStep`, `Installing`, `Done`, with the signals of each phase in the stated order.
- A failed upgrade records no `Done` signal.

### REQ-F-066: Upgrade result uses the existing result structure [E]
**Statement:** When `Execute` of an upgrade completes, the helper shall return the existing result structure, with the outcome, the error code and names when the outcome is `Failed`, the changed packages (each upgrade, each replacement as the removed old package and the installed new package, and each new dependency) and the warnings.
**Acceptance criteria:**
- After a successful fixture upgrade, the changed list equals the preview's upgrade, replacement and new-dependency lists, each entry once.
- After a failed upgrade before promotion, the changed list is empty.

### REQ-F-067: Warnings for upgrades [U]
**Statement:** The helper shall use only the warning token `HookFailed` for upgrades, as helper REQ-F-051 requires.
**Acceptance criteria:**
- The warning tokens emitted over the fixtures of this section are a subset of `{HookFailed}`.

### REQ-F-068: New status codes [U]
**Statement:** The helper shall use the status codes `SyncFailed`, `SyncDatabaseChanged`, `KeyImportRequired`, `UpgradePartiallyApplied` and `Cancelled` (the last for `PrepareUpgrade` only, which has no outcome field), and shall use `NothingToDo` for an upgrade that has nothing to do, in addition to the codes of helper REQ-F-024 onward.
**Acceptance criteria:**
- Each new code is returned by at least one fixture of this SPEC (REQ-F-003, REQ-F-009, REQ-F-051, REQ-F-029, REQ-F-060), and the introspection documentation lists each new code.
- A unit test asserts that the error taxonomy table of this SPEC and the helper's code table contain the same code names.

### REQ-F-069: Replies carry no free text [U]
**Statement:** The helper shall return for every upgrade reply, progress signal and result only codes, package names, version strings, enumerated warning and step tokens, and integers.
**Acceptance criteria:**
- A byte search of every reply and signal captured in the upgrade fixtures finds no string outside the enumerations, names and versions of the fixture.
- A forced libalpm error carrying a unique marker string (helper REQ-F-052) leaves the marker out of every upgrade reply and appears in the journal.

### 11. Protocol and compatibility

### REQ-F-070: Plan lifetime and limits for upgrades [U]
**Statement:** The helper shall apply helper REQ-F-005, REQ-F-006, REQ-F-007 and REQ-F-071 to upgrade plans without change: a 300 second lifetime, a single use, the caller binding and the plan count limits.
**Acceptance criteria:**
- An upgrade plan executed once returns `PlanNotFound` on a second `Execute` (helper REQ-F-008).
- An upgrade plan created for one UID is not returned to another UID (helper REQ-F-004).

### REQ-F-071: The live sync databases are refreshed only by the upgrade action [U]
**Statement:** The live sync databases shall be refreshed only by the upgrade action of this SPEC, and by no other helper method, transaction or GUI or service code path. This is the amended form of helper REQ-F-027 (see "Amendments to other specs").
**Acceptance criteria:**
- A test that runs a full install and a full remove on a fixture leaves the live `sync/` manifest identical before and after each.
- A source search finds the database refresh and promotion calls only in the upgrade code path.

### REQ-F-072: No standalone refresh method [U]
**Statement:** The helper's D-Bus interface shall offer no method that refreshes the sync databases without also upgrading the system, and the GUI shall offer no action that does so.
**Acceptance criteria:**
- The introspection XML declares exactly the methods `PreparePlan`, `PrepareUpgrade`, `Execute` and `Cancel`.
- The GUI action list contains no action whose result is a refreshed database without an upgrade.

### REQ-F-073: Protocol version 2 [E]
**Statement:** When a client reads the `ProtocolVersion` property, the helper shall return 2, and the change from 1 shall be additive only.
**Acceptance criteria:**
- A property read on the private bus returns 2.
- The introspection XML of version 2 declares every method, signal and argument of version 1 with the same signature, and adds only `PrepareUpgrade`, its preview type, the new codes and the new action.

### REQ-F-074: Install and remove behaviour is unchanged [U]
**Statement:** The helper shall keep the behaviour of install and remove plans as specified in the helper SPEC, except for the codes added by REQ-F-068, which are returned by the upgrade path only.
**Acceptance criteria:**
- The helper test suite for install and remove passes without change to its expected values.
- A remove plan on a fixture with a replacement in the sync databases is prepared and executed as before.

### 12. Configuration

### REQ-F-075: Upgrade reads the pacman configuration [U]
**Statement:** The upgrade path shall read `CacheDir`, `GPGDir`, `HookDir`, `NoExtract`, `IgnorePkg`, `IgnoreGroup`, `HoldPkg`, `CheckSpace`, `SigLevel` and the repository list from the pacman configuration through the pacman settings parser (helper REQ-F-019, REQ-F-040), and shall use `HoldPkg` only to refuse removal of a protected package (helper REQ-F-036), not to hold back a version change of a protected package.
**Acceptance criteria:**
- With `CacheDir` set in the fixture configuration, the fetched files land in that directory.
- With `HoldPkg = foo` and a fixture upgrade of `foo` to a newer version, the upgrade includes `foo` and does not return `ProtectedPackage`.
- With `HoldPkg = foo` and a fixture upgrade that would remove `foo`, the upgrade returns `ProtectedPackage` naming `foo`.

### REQ-F-076: Unsupported configuration options are refused [X]
**Statement:** If the pacman configuration contains an option that the upgrade path does not support (the list is fixed by OQ-U2; `XferCommand` is on it), then the helper shall return `ConfigurationError` for `PrepareUpgrade`, naming the option, and shall create no plan. The helper shall not ignore such an option silently for the upgrade.
**Acceptance criteria:**
- With `XferCommand = /usr/bin/curl -o %o %u` in the fixture configuration, `PrepareUpgrade` returns `ConfigurationError` naming `XferCommand`, the libalpm initialization counter stays 0, and the live manifest is unchanged.
- With the same option in the configuration, `PreparePlan` for `install` keeps helper REQ-F-019 (the option is ignored and the install proceeds).

### REQ-F-077: Promotion across filesystems [O]
**Statement:** Where the scratch root is on a different filesystem from the live `sync/` directory, the helper shall promote each file by first copying it to a temporary file inside the live `sync/` directory and then renaming that file over the live file, so that each live file is replaced atomically.
**Acceptance criteria:**
- With a fixture where the scratch root and the live `sync/` directory are on different filesystems (or a test seam that reports them as different), a promotion that is interrupted after the copy leaves the live file unchanged and the temporary file is removed on the next start.
- With both on one filesystem, the rename is used and no copy is made.

### 13. Graphical interface

### REQ-F-078: Update system button [E]
**Statement:** When the Updates page is shown and no transaction is running, the GUI shall show an "Update system…" button in the page header, enabled whatever the Updates list contains, including when the list is empty or says the system is up to date.
**Acceptance criteria:**
- With a fake service reporting `ProtocolVersion` 2 and an empty Updates list, the button is enabled.
- With a transaction running in the fake service, the button is disabled.

### REQ-F-079: Helper too old [X]
**Statement:** If the helper reports a `ProtocolVersion` below 2, then the GUI shall show the "Update system…" button disabled, with the message "The package helper is too old for system updates" (table key `HelperTooOld`), and shall not call `PrepareUpgrade`.
**Acceptance criteria:**
- With a fake service reporting `ProtocolVersion` 1, the button is disabled, the message equals the table entry, and the fake service records zero `PrepareUpgrade` calls.

### REQ-F-080: Preparation dialog [E]
**Statement:** When the user presses "Update system…", the GUI shall call `PrepareUpgrade` with an empty options map, shall show syncing progress with the repository name and the count `i` of `n` from the `Syncing` signals, and shall offer Cancel, which calls `Cancel("")`.
**Acceptance criteria:**
- With a fake service that emits `Syncing` for two repositories, the dialog shows "Syncing repository 1 of 2" and then "2 of 2" (table keys with plain-text repository names).
- Pressing Cancel calls `Cancel("")` once, and the dialog then closes on the `Cancelled` reply.

### REQ-F-081: Up to date message [E]
**Statement:** When `PrepareUpgrade` returns `NothingToDo`, the GUI shall show the message "System is up to date" with a Close action only, and shall show no preview.
**Acceptance criteria:**
- With a fake `NothingToDo` reply, the dialog text equals the table entry, its only action is Close, and no preview dialog is created.

### REQ-F-082: Upgrade preview content [E]
**Statement:** When `PrepareUpgrade` returns a plan, the GUI shall show the preview in one dialog, with separate sections for upgrades (name, old version, new version, repository), replacements, new dependencies, held-back packages, the keyring notice when the keyring flag is set, the total download size, the installed-size change and the plan's expiry time.
**Acceptance criteria:**
- With a fake preview of two upgrades, one replacement, one new dependency and one held-back package, every section shows its entries, and the keyring notice is absent.
- With the keyring flag set, the keyring notice names the keyring package from the reply.

### REQ-F-083: Long preview lists [U]
**Statement:** The GUI shall keep the preview dialog scrollable and shall show every row of a preview of up to 1000 rows, without a per-package selection control.
**Acceptance criteria:**
- With a fake preview of 500 upgrades, the list model holds 500 rows, the last row is reachable by scrolling, and no checkbox or selection control is present.
- Both Holonight and Fusion styles pass the dual-style runtime check for this dialog (REQ-NF-005).

### REQ-F-084: Confirm starts execution once [E]
**Statement:** When the user presses Confirm in the preview, the GUI shall call `Execute` once with the plan ID of the preview and shall show a modal progress dialog with the step counter "step i of n", the phase and the current item.
**Acceptance criteria:**
- With a fake service, one `Execute` call is recorded, carrying the plan ID from the preview and no other argument.
- With a fake keyring fixture, the dialog text shows "step 1 of 2" during the keyring step and "step 2 of 2" during the main step.

### REQ-F-085: Cancel is enabled only before promotion [S]
**Statement:** While the upgrade progress dialog is open, the GUI shall enable Cancel only until the first `Promoting` signal, and shall disable it from that signal onwards.
**Acceptance criteria:**
- With a fake signal sequence `Downloading`, `Verifying`, `Promoting`, the Cancel control is enabled after `Verifying` and disabled after `Promoting`.
- A late `PastPointOfNoReturn` reply is shown as a normal outcome (GUI REQ-F-036).

### REQ-F-086: Closing the progress dialog past promotion is refused [X]
**Statement:** If the user closes the upgrade progress window, or presses Escape, after promotion has started, then the GUI shall ignore the request and show the close-unavailable message, as GUI REQ-F-035 does.
**Acceptance criteria:**
- With a fake service in the `Promoting` phase, a close request leaves the window open and shows the close-unavailable message.
- The window closes after the fake service delivers the final result.

### REQ-F-087: Results of an upgrade [E]
**Statement:** When `Execute` of an upgrade ends, the GUI shall show the result for its outcome: "System updated" for `Succeeded`, the message for `Cancelled`, the code message for `Failed`, and for `UpgradePartiallyApplied` a message that names the failed step (keyring or main) and tells the user to press "Update system…" again. When the changed list contains the keyring package, the success or partial message shall also say that the keyring was updated.
**Acceptance criteria:**
- With a fake `UpgradePartiallyApplied` reply with step token `keyring`, the message equals the table entry for that step, and its actions are Close only.
- With a fake `Succeeded` reply whose changed list contains `archlinux-keyring`, the message contains the keyring note.

### REQ-F-088: Lists are refreshed after every upgrade outcome [E]
**Statement:** After any outcome of an upgrade, including refusals, cancellation and `NothingToDo`, the GUI shall re-read the Installed, Updates and Explore lists and call the `CheckNow()` method of `holonight-packaged`.
**Acceptance criteria:**
- With a fake service and each of `Succeeded`, `Cancelled`, `SyncFailed` and `NothingToDo`, the fake records a list re-read for each of the three pages and one `CheckNow` call after the outcome.

### REQ-F-089: Update system button in the refusal dialog [E]
**Statement:** When `PreparePlan` for install is refused with `PartialUpgradeRequired`, the GUI shall offer an "Update system…" button in the refusal dialog in addition to Close. When the user presses it, the GUI shall close the refusal dialog and start the upgrade flow of REQ-F-080 to REQ-F-088, and shall not retry the install.
**Acceptance criteria:**
- With a fake `PartialUpgradeRequired` refusal, the dialog actions are Close and "Update system…", and the message equals the table entry (GUI REQ-F-022 is amended accordingly).
- Pressing "Update system…" calls `PrepareUpgrade` once and never calls `PreparePlan` again for the install.

### REQ-F-090: Denied authorization returns to the preview [X]
**Statement:** If `Execute` of an upgrade returns `NotAuthorized`, then the GUI shall return to the preview with the same note as for install (GUI REQ-F-025), and Confirm shall call `Execute` again with the same plan ID.
**Acceptance criteria:**
- With a fake `NotAuthorized` reply followed by a success, two `Execute` calls are recorded with the same plan ID.

### REQ-F-091: Changed live databases offer a new preparation [X]
**Statement:** If `Execute` returns `SyncDatabaseChanged`, then the GUI shall show the table message for it with the actions Try again and Close, and Try again shall start the flow again from `PrepareUpgrade`.
**Acceptance criteria:**
- With a fake `SyncDatabaseChanged` reply, the actions are Try again and Close.
- Pressing Try again calls `PrepareUpgrade` once and no `Execute` before the new preview is confirmed.

### REQ-F-092: Busy at preparation [X]
**Statement:** If `PrepareUpgrade` returns `Busy`, `PlanNotFound` or `PlanExpired` for a confirmation, then the GUI shall offer Try again, as GUI REQ-F-023 does.
**Acceptance criteria:**
- With a fake `Busy` reply, the actions are Try again and Close, and the message equals the table entry.

### REQ-F-093: Other codes offer Close only [U]
**Statement:** For every code of this SPEC that has no action in REQ-F-089 to REQ-F-092, the GUI shall offer Close only, as GUI REQ-F-024 does.
**Acceptance criteria:**
- With fake replies `SyncFailed`, `KeyImportRequired` and `DownloadFailed`, each dialog has Close as its only action.

### REQ-F-094: Messages come from one fixed table [U]
**Statement:** The GUI shall show every upgrade message from the message table of GUI REQ-F-048, which this SPEC extends with the keys `HelperTooOld`, `Syncing`, `SystemUpToDate`, `Upgrade.Preview`, `HeldBack`, `Keyring`, `UpgradePartiallyApplied.keyring`, `UpgradePartiallyApplied.main`, `SyncFailed`, `SyncDatabaseChanged`, `KeyImportRequired` and `NothingToDo.Upgrade`, and it shall insert only package names and versions from the reply.
**Acceptance criteria:**
- A unit test asserts that the table has a non-empty entry for every key listed here and for every new code.
- A test fails if any upgrade dialog text is built from a string of the reply other than a package name, a version or a step token.

### 14. Non-functional requirements

### REQ-NF-001: Responsiveness during preparation and execution [U]
**Statement:** While `PrepareUpgrade` or `Execute` of an upgrade is running, the helper shall answer `Cancel` and property reads within 1 second.
**Acceptance criteria:**
- With the sync download held by a seam, a property read returns within 1 second.
- With the fetch phase held by a seam, `Cancel` for another plan returns `PlanNotFound` within 1 second.

### REQ-NF-002: Bounded disk footprint of the scratch root [U]
**Statement:** The scratch root shall never hold more than the scratch size cap, and the helper shall remove it on every discard path of REQ-F-013, REQ-F-014 and REQ-F-016.
**Acceptance criteria:**
- A test that fills the scratch root to the cap then refuses one more byte (REQ-F-010) shows the root size never exceeds the cap.
- After each discard path of REQ-F-013, REQ-F-014 and REQ-F-016, a recursive listing of the state directory finds no scratch root.

### REQ-NF-003: Deterministic timing tests [U]
**Statement:** The timing rules of this SPEC (300 seconds, 600 seconds for the client timeout, the internal sync timeout and the scratch cap) shall be tested with a fake clock or a test seam, and the whole upgrade test set shall finish within 60 seconds of wall-clock time, except for the bounded latency tests of REQ-NF-001.
**Acceptance criteria:**
- A timing test reads the internal sync timeout value from the test seam and does not depend on wall-clock sleeps longer than 6 seconds.
- The upgrade test set passes in the `test` preset within 60 seconds.

### REQ-NF-004: Raw libalpm text stays in the journal [X]
**Statement:** If libalpm reports an error on any upgrade path, then the helper shall write the raw text only to the journal, as helper REQ-F-052 requires, and shall leave it out of every reply and signal.
**Acceptance criteria:**
- A marker injected into an error of the fetch phase and of the main step appears in the journal capture and in no reply.

### REQ-NF-005: Dual-style acceptance for the new dialogs [U]
**Statement:** The new preparation, preview, progress, result and refusal-button UI shall pass the dual-style runtime acceptance (Holonight and Fusion), together with the policy fixtures and the existing QML tests.
**Acceptance criteria:**
- The runtime acceptance module, run in both styles, renders the preview with 500 rows and the progress dialog with the step counter without a QML warning.

### REQ-NF-006: Recovery is idempotent [U]
**Statement:** After any refusal or failure before promotion, the live state shall be identical to its state before `PrepareUpgrade`, and a repeated upgrade on the same fixture shall behave as the first.
**Acceptance criteria:**
- After each of `SyncFailed`, `SyncDatabaseChanged`, `NotAuthorized`, `PlanChanged` and `Cancelled` before promotion, the live manifest equals the manifest taken before the first call.
- A repeated `PrepareUpgrade` after each of these returns the same preview as the first.

### REQ-NF-007: Preview rendering time [U]
**Statement:** The GUI shall build the preview model for 1000 rows in under 500 ms on the CI runner.
**Acceptance criteria:**
- A model test with 1000 fixture upgrade rows finishes its build step within 500 ms.

---

## Constraints

### REQ-C-001: Transaction core stays Qt-free [U]
**Statement:** The upgrade logic shall live in the STATIC library `holonight_packages_transaction`, without Qt or D-Bus dependencies. D-Bus, polkit and the scratch root's directory creation shall live in `apps/helper`.
**Acceptance criteria:**
- A build check finds no Qt or D-Bus include in `src/transaction`.
- The upgrade unit tests link `holonight_packages_transaction` only.

### REQ-C-002: Additive D-Bus changes only [U]
**Statement:** The helper's existing methods, signals and their signatures shall not change. `PrepareUpgrade`, the upgrade preview type and the new codes and action are the only additions.
**Acceptance criteria:**
- A diff of the introspection XML between the version of this SPEC and the previous one shows additions only.
- The helper's existing client tests pass without change.

### REQ-C-003: Tests use fixture roots only [U]
**Statement:** Automated tests shall run upgrades only against a fixture root under a temporary directory, with `file://` fixture repositories, and never against the real system (helper REQ-C-009).
**Acceptance criteria:**
- A test run on a host with a real `/var/lib/pacman` finds no change to it.
- Every repository URL in the upgrade fixtures starts with `file://` and points into the fixture root.

### REQ-C-004: No pointer or focus automation [U]
**Statement:** The GUI tests for the upgrade flow shall drive the view-model and the fake service, and shall never automate pointer or focus input (GUI REQ-NF-004).
**Acceptance criteria:**
- A source search of the upgrade tests finds no pointer or focus automation call.

### REQ-C-005: Polkit actions and artifacts agree [U]
**Statement:** The new action `org.holonight.packages.upgrade` shall be declared in the polkit policy and listed in the artifact check that keeps the helper's artifacts in agreement (`scripts/check-helper-artifacts.py`), and the install and remove actions shall stay unchanged.
**Acceptance criteria:**
- The artifact check passes with the new action, and fails when the new action is removed from the policy.
- The install and remove defaults are the same as before this SPEC.

### REQ-C-006: The GUI and the monitor do not change packages [U]
**Statement:** The GUI shall change packages only through the helper's `PrepareUpgrade`, `Execute` and `Cancel`, and `holonight-packaged` shall only call `CheckNow` and read its snapshot (AGENTS.md rule, unchanged).
**Acceptance criteria:**
- A source search finds no libalpm or pacman invocation in `apps/packages` and `apps/packaged` for the upgrade.
- The GUI's upgrade calls are the three listed helper methods and the `CheckNow` call of `holonight-packaged`.

### REQ-C-007: Out-of-scope actions are absent [U]
**Statement:** The product shall offer no per-package upgrade, multi-select, standalone database refresh, scheduled upgrade, AUR or foreign package action, downgrade, `.pacnew` or reboot advice as part of this feature.
**Acceptance criteria:**
- The GUI action list and the helper's method list contain none of these.

### REQ-C-008: Systemd unit allows the scratch root [U]
**Statement:** The helper's systemd unit shall declare its state directory so that the scratch root is created under `/var/lib` and the hardening list of helper REQ-C-015 stays as it is, unless a directive must change for the scratch root, in which case the change is named in the unit file's comment and tested in the manual checklist.
**Acceptance criteria:**
- The unit file declares `StateDirectory` for the helper, and `systemd-analyze verify` reports no error for it.
- The manual checklist item for the hardened unit passes with a `PrepareUpgrade` and an `Execute`.

### REQ-C-009: Checks of the repository stay green [U]
**Statement:** The feature shall not count as done until `task check` and `task check:integration` are green, the dual-style runtime acceptance passes, and the manual checklist below is recorded in `apps/helper/README.md`.
**Acceptance criteria:**
- The two `task` commands exit 0 on the feature branch.
- `apps/helper/README.md` contains a section that lists each manual item with its result and date.

### REQ-C-010: Helper SPEC amended before acceptance [U]
**Statement:** Before this SPEC is accepted, the helper SPEC shall carry the amended REQ-F-027 of "Amendments to other specs", the new codes in its error taxonomy, and the `ProtocolVersion` change, so that the two specs agree.
**Acceptance criteria:**
- A check of `docs/sdd/package-helper-transactions/SPEC.md` finds the amended REQ-F-027 text and the four new codes.

---

## Error taxonomy additions

The helper SPEC's taxonomy (REQ-F-030 order of evaluation, the code table) applies. These rows are added for the upgrade action.

| Code | Meaning | Raised by | Reply data |
|------|---------|-----------|------------|
| `SyncFailed` | Sync databases could not be fetched (mirror unreachable, internal timeout or scratch size cap) | REQ-F-003, REQ-F-009, REQ-F-010 | None |
| `SyncDatabaseChanged` | A live sync database changed after prepare; the plan is consumed and a new preparation is needed | REQ-F-051 | None |
| `KeyImportRequired` | libalpm asked to import a signing key during prepare or fetch; refused | REQ-F-029 | Package names |
| `UpgradePartiallyApplied` | A step failed after promotion; the system is partly upgraded | REQ-F-060, REQ-F-061 | Step token (`keyring` or `main`) and changed packages |
| `Cancelled` (`PrepareUpgrade` only) | The caller cancelled its own preparation, or disconnected | REQ-F-032, REQ-F-035 | None |
| `NothingToDo` (upgrade) | The scratch databases offer no change | REQ-F-024 | None |

Warning token: `HookFailed` only (REQ-F-064). Outcome `Cancelled` after `PrepareUpgrade` or `Execute` (REQ-F-032, REQ-F-049). `PastPointOfNoReturn` for `Cancel` after promotion (REQ-F-058).

## Verification

Automated tests run only against a fixture root under a temporary directory, with `file://` fixture repositories (version 1 to version 2), never against the real system (REQ-C-003). The GUI tests use a fake transaction service. The new dialogs pass the dual-style runtime acceptance in Holonight and Fusion (REQ-NF-005). Tests never automate pointer or focus input (REQ-C-004). `task check` and `task check:integration` must be green (REQ-C-009).

Key falsifiable checks:

- The live fixture `sync/` and `local/` directories are byte-identical before and after `PrepareUpgrade` (REQ-F-007).
- The packages are present in the fixture `CacheDir` before any live change (REQ-F-042).
- After a successful upgrade, the live sync databases equal the scratch databases, and the local database shows the new versions (REQ-F-051, REQ-F-053, REQ-F-066).
- The keyring package is committed in its own transaction before the other packages (REQ-F-055).
- `SyncDatabaseChanged` leaves every live file unchanged (REQ-F-051).
- A cancel during prepare or fetch leaves no `db.lck` and no scratch root (REQ-F-032, REQ-F-049).
- The scratch root is removed on every discard path (REQ-F-013, REQ-F-014, REQ-F-016, REQ-NF-002).

Manual verification (disposable Arch VM, real mirrors, polkit agent and root). The steps are appended to `apps/helper/README.md` when they are run, with the date and result of each:

1. A real `-Syu` from a month-old Arch Linux Archive snapshot that includes a keyring update. The keyring step runs first and the upgrade succeeds.
2. A cancel during a throttled fetch. The result is `Cancelled`, `/var/lib/pacman/db.lck` is absent, and the scratch root is gone.
3. A `pacman -Sy` run externally between the preview and the confirmation. The result is `SyncDatabaseChanged`, and a new preparation succeeds.
4. An install refused with `PartialUpgradeRequired`, then "Update system…", then the upgrade, then the same install, which succeeds.

---

## Risks

| # | Risk | Mitigation | Requirements |
|---|------|------------|--------------|
| 1 | A failure after promotion leaves a partly upgraded system. This is inherent to a `-Syu`: packages are changed one after another. | The failure is reported as `UpgradePartiallyApplied` with the failed step; the message says to run "Update system…" again; the next run completes the work. The risk is reported, not prevented. | REQ-F-060, REQ-F-061, REQ-F-063, REQ-NF-006 |
| 2 | Prepare uses the network and disk as root without authorization. | One preparation at a time (`Busy`); the plan count limits; the scratch size cap; the internal timeout; the scratch root is private and removed on every discard path. | REQ-F-001, REQ-F-004, REQ-F-005, REQ-F-010, REQ-F-013, REQ-NF-002 |
| 3 | The preview can be stale against the mirror within 5 minutes. | Accepted. The helper installs exactly the previewed set, and any change in the live state or the local database refuses the run (`SyncDatabaseChanged`, `PlanChanged`). | REQ-F-012, REQ-F-051, REQ-F-052 |
| 4 | libalpm cannot abort a running download, so a cancel during the fetch needs a process end. | The helper ends after the reply, as for install and remove; the bus starts a new process on the next call. | REQ-F-032, REQ-F-049 |
| 5 | The keyring update is not seen by the signature code of the main step (spike S-2). | Fallbacks in the open questions: re-execute the helper for the main step, or refuse with a message to run the upgrade again. The keyring step is committed alone first. | REQ-F-055, REQ-F-056 |
| 6 | Promotion across filesystems or an interrupted rename corrupts a live sync database (spike S-3). | Per-file rename; copy-then-rename when filesystems differ; rollback of the already replaced files on failure. | REQ-F-053, REQ-F-054, REQ-F-077 |
| 7 | A user with a `pacman.conf` option that the upgrade path does not support gets a silently different result. | Refused with `ConfigurationError` naming the option, for the upgrade only. | REQ-F-076 |
| 8 | The `PartialUpgradeRequired` button leads the user into an upgrade they did not choose. | The button only starts the preview flow; the preview is shown and must be confirmed; the install is not retried. | REQ-F-089, REQ-F-082 |
| 9 | The Update system button is always enabled even when the list says the system is up to date, so the user runs a useless flow. | The flow ends at `NothingToDo` with "System is up to date" and no preview; the button exists because the on-disk lists may be stale. | REQ-F-078, REQ-F-081 |
| 10 | Helper and GUI are updated at different times (protocol skew). | `ProtocolVersion` 2 is additive; the GUI disables the button below 2. | REQ-F-073, REQ-F-079 |

---

## Open questions and spikes

Spikes. The spec does not depend on their outcome beyond the fallbacks stated.

- **S-1 — DOWNLOADONLY on the scratch dbpath with a shared `local`.** Questions: where libalpm places `db.lck` for the scratch handle; whether the scratch handle writes into `CacheDir`; whether package signatures are verified at fetch. Fallback: if the scratch handle cannot share `local` through a symlink, the scratch root holds a read-only copy of `local/`, with the same guard against live changes.
- **S-2 — Keyring update visible to gpgme in the same process.** Fallbacks, in order: re-execute the helper for the main step (a new process loads the keyring); if that is not possible, refuse the main step with `UpgradePartiallyApplied` and step token `keyring`, and the message asks the user to run "Update system…" again.
- **S-3 — Promotion by rename or by copy, per filesystem.** The decision is in REQ-F-053 and REQ-F-077.
- **S-4 — Internal sync timeout and scratch size cap values.** REQ-F-003 requires the timeout below 600 seconds; REQ-F-010 and REQ-NF-002 need the cap. Values are fixed by the design after the spike.

Open questions for Design:

- **OQ-U1 — Encoding of the step counter in `Progress`.** REQ-F-057 requires `i` and `n` in the existing arguments. Design fixes the encoding (for example in `current` and `total` of the step phases, with item counts elsewhere). The GUI requirement REQ-F-084 depends on it.
- **OQ-U2 — Exact list of unsupported `pacman.conf` options for the upgrade.** `XferCommand` is on it (REQ-F-076). Design decides for `ParallelDownloads`, `CleanMethod`, `DBPath` and `RootDir`.
- **OQ-U3 — Code for an unsupported option.** This SPEC uses `ConfigurationError`, because the decided code list has no other fit. Design may add a dedicated code, which is a change to REQ-F-068.
- **OQ-U4 — Unresolvable held-back dependency code.** REQ-F-022 uses `PackageNotFound`, the code the helper already uses for a missing dependency. Confirm in Design.
- **OQ-U5 — Preview wire type.** The `PrepareUpgrade` preview is a new D-Bus type, defined in the introspection XML by Design (REQ-F-073).
- **OQ-U6 — Re-resolution at promotion.** REQ-F-052 adds a re-resolution against the live `local/` database after the guard. It follows helper REQ-F-044 and is not in the decided sequence; Design confirms it.

## Amendments to other specs

These amendments are part of this change. The helper SPEC REQ-F-027 amendment was applied on 2026-11-12; the others are applied with the implementation (REQ-C-010).

**Helper SPEC REQ-F-027 (amended).** Replace the statement with: "The helper shall not refresh the sync databases during `PreparePlan`, `Execute` of an install or remove plan, or `Cancel`. The live sync databases are refreshed only by the system upgrade action (REQ-F-071 of `docs/sdd/system-upgrade/SPEC.md`), and the sync download of that action writes only into the scratch root." Its acceptance criteria stay, with the manifest check applied to install and remove.

**Helper SPEC error taxonomy.** Add the rows of "Error taxonomy additions". The helper SPEC's non-goal "System upgrade (`-Syu` or any upgrade of the whole system)" and its non-goal "Refreshing sync databases (`-Sy`), including as part of an install" are superseded for the upgrade action only.

**Helper SPEC ProtocolVersion.** The property changes from 1 to 2 (REQ-F-073).

**GUI SPEC REQ-F-022 (amended).** The `PartialUpgradeRequired` dialog offers Close and "Update system…" (REQ-F-089). **GUI SPEC OQ-07 (resolved).** The remedy is the upgrade action of this SPEC.

**Helper README.** The client contract gains `PrepareUpgrade`, the `org.holonight.packages.upgrade` action and the timeout of 10 minutes (added with the implementation).

## Revision history

- 2026-11-12: spike outcomes (SPIKES.md S-1 to S-4). REQ-F-029 gains the keyring-step deferral of a missing key. Values of REQ-F-003 and REQ-F-010 fixed by S-4 (480 s, 256 MiB). No other requirement changed.

- 2026-11-12: review of DESIGN.md section 14. Scratch root mode `0711` with libalpm's `DownloadUser` sandbox kept (REQ-F-008); `Cancelled` status code for `PrepareUpgrade` (REQ-F-032, REQ-F-068); timeout and size cap end the process (REQ-F-003); disk space by `statvfs` estimate (REQ-F-048); signature fixture separated from key-missing (REQ-F-047); guard scope (REQ-F-051); step markers `KeyringStep`/`MainStep` (REQ-F-057, OQ-U1 resolved). Moving the downloads into a bubblewrap or systemd-run worker is deferred to a follow-up review (`docs/tech-debt.md`).

- 2026-11-12: first draft. Decisions 1 to 12 of the design session encoded. Edge cases of the session are covered by REQ-F-003, REQ-F-009, REQ-F-010, REQ-F-016, REQ-F-029, REQ-F-033, REQ-F-035 to REQ-F-037, REQ-F-048, REQ-F-051, REQ-F-058, REQ-F-060, REQ-F-064 and REQ-NF-001. One step was added beyond the decided sequence: re-resolution after the guard (REQ-F-052, OQ-U6).
