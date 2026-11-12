# Tech debt backlog

Seed items for future `/sdd` sessions. Each entry captures a known problem and the evidence behind it, not a
design — architecture and task breakdown belong to the SDD run that picks the item up (typically landing in
`docs/sdd/<slug>/`).

## AlpmPackageSource re-parses sync databases on every enumeration

**Status:** Open, not scheduled.

**Problem:** `AlpmPackageSource::enumerateInstalledPackages()` (`src/backends/src/alpm_package_source.cpp`) creates
a fresh `alpm_handle_t` and fully re-registers/reloads every configured sync database (`registerSyncDatabases`) on
every call, including every `InstalledPackagesModel::refresh()` from the UI. Both local package state and sync
database state affect the visible result because sync membership determines official/foreign classification and
the repository label, although sync database contents generally change less often.

**Measured evidence** (this machine, real `/var/lib/pacman`, 1595 installed packages, 3 repos —
`core.db` + `extra.db` (8.9 MiB) + `g14.db`):

| Scenario | Time |
|---|---|
| Full `enumerateInstalledPackages()` (local + sync) | 110-400 ms per call (400 ms cold, ~110 ms steady state) |
| Local database only (sync dir absent) | 14-32 ms per call |

Sync-database parsing accounts for the large majority of the cost, dominated by `extra.db`'s size. The call
already runs off the UI thread via `QtConcurrent::run` (confirmed safe in review — no UI freeze), so this is a
refresh-latency problem, not a hang. But every `refresh()` click pays the full cost regardless of whether the
sync databases changed.

**Why this isn't a quick fix:** avoiding the re-parse means caching the registered sync databases (and likely the
`alpm_handle_t` itself) across calls instead of creating a fresh handle per call as today. That's a real design
change with open questions a quick patch shouldn't paper over:

- **Lifetime/ownership**: `AlpmPackageSource` currently has no state beyond `database_root_`/`database_path_`; a
  cached handle makes it stateful.
- **Thread safety**: `enumerateInstalledPackages()` runs via `QtConcurrent::run` on background threads. Today each
  call gets its own handle, so there's no shared-state concern. A cached handle shared across calls needs an
  explicit synchronization or single-flight story.
- **Invalidation policy**: when does the cache refresh — mtime check on `sync/*.db`, explicit invalidation hook,
  time-based TTL, or "never, until the process restarts"? No existing signal in the codebase currently exposes
  "the sync databases changed."
- **Relationship to the persistence module**: `src/persistence/` is currently an empty `INTERFACE` stub
  (per `CLAUDE.md`, package transactions and caching aren't implemented yet). This caching concern plausibly
  belongs there once that module has a real design, rather than as an ad hoc field on `AlpmPackageSource`.

**Suggested scope for the SDD session:** design the sync-database caching/invalidation strategy (likely as part of
designing the `persistence` module), decide the thread-safety approach for `AlpmPackageSource` reuse across calls,
and produce a `SPEC.md`/`DESIGN.md`/`TASKS.md` under `docs/sdd/` the way `installed-packages-list` did.

**Related, smaller finding (same review pass):** `toPackage()` in the same file does an O(n·m) linear scan over
registered sync databases per installed package to determine repository membership (n = installed packages, m =
registered repos). At m≈3 this is negligible in the measurements above; revisit only if it shows up in profiling
after the caching fix, not before.

## Review: download sandbox for the system upgrade (libalpm, bubblewrap or systemd-run)

**Status:** Open, scheduled for review after `docs/sdd/system-upgrade` lands.

**Problem:** The system upgrade (`docs/sdd/system-upgrade`) keeps libalpm's own `DownloadUser` sandbox (child process as
`alpm`, Landlock, seccomp) for the database sync and the package fetch, with a `0711` scratch root (DESIGN D-9). The
downloads still run from the root helper process, and libalpm cannot interrupt a running download, so a cancel, the
sync timeout and the scratch size cap all end the helper process (DESIGN D-4).

**Options to compare** (raised 2026-11-12 during the system-upgrade design):

- **Keep libalpm's sandbox** (current). No new dependency, seccomp included; cancel needs a process end.
- **bubblewrap worker.** A `holonight-package-sync-worker` executable runs the sync and fetch under `bwrap`
  (`--unshare-all --share-net --die-with-parent --cap-drop ALL`, only `/usr`, the pacman configuration, the keyring and
  the scratch dir or `CacheDir` bound). Cancel and timeouts kill the worker, not the helper. Costs: `bubblewrap` becomes
  a hard dependency; no seccomp unless a BPF filter is supplied (libseccomp); gpg with a read-only `GPGDir` needs a
  spike; a small worker-to-helper protocol; the sandboxed run cannot be tested in the CI container.
- **systemd-run transient unit.** The same worker as a transient service (`DynamicUser`, `ProtectSystem=strict`,
  `ReadWritePaths`, `SystemCallFilter`, empty `CapabilityBoundingSet`); cancel is `StopUnit`. No new dependency and a
  systemd-built seccomp filter; costs are D-Bus to systemd1, unit lifecycle, `DynamicUser` file ownership and harder
  unit tests.

**Suggested scope for the SDD session:** decide whether the isolation and the killable worker justify the change, run a
spike as root in a disposable VM (sync, `DOWNLOADONLY` fetch, kill during download, read-only keyring), and amend the
system-upgrade SPEC (REQ-F-003, REQ-F-008, REQ-F-032, REQ-F-049) if the decision changes.
