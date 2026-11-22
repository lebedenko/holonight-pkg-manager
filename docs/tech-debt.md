# Tech debt backlog

Seed items for future `/sdd` sessions. Each entry captures a known problem and the evidence behind it, not a
design — architecture and task breakdown belong to the SDD run that picks the item up (typically landing in
`docs/sdd/<slug>/`).

## Linear repository lookup per installed package in AlpmPackageSource

**Status:** Open, low priority. Revisit only if profiling shows it.

**Problem:** `toPackage()` in `src/backends/src/alpm_package_source.cpp` scans every registered sync database for
each installed package to find its repository, which costs O(n·m) (n = installed packages, m = registered repos).
With m≈3 this was negligible in the measurements taken for the former sync-database re-parse item (1595 installed
packages, 110-400 ms per enumeration, almost all of it sync-database parsing). That re-parse was fixed by
`AlpmConnectionCache` in `src/persistence/` (`docs/sdd/alpm-sync-db-cache`, commit `4e5055d`, 2026-09-05), so this
scan is the one remaining per-enumeration cost from that review.

**Suggested scope:** profile an enumeration with the cache warm first. Fix this only if the scan shows up there,
for example with a name-to-repository map that is built once per cached connection.

## Review: download sandbox for the system upgrade (libalpm, bubblewrap or systemd-run)

**Status:** Open, review due: `docs/sdd/system-upgrade` landed in commit `22f96b8` (2026-11-12). Run it with the
manual VM checks (T-081 to T-085 in `docs/sdd/system-upgrade/TASKS.md`); T-082 and T-085 cover cancel and the
`DownloadUser` sandbox.

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
