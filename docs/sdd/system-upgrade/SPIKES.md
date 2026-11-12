# Spikes — system-upgrade

Throwaway programs for these spikes are not kept in the repository. Each section records the question, the method, the
observed result and the consequence for SPEC.md and DESIGN.md. Environment unless stated otherwise: pacman 7.1.0
(libalpm 16), run as a non-root user against a `FixtureRoot` world under the temp dir with `file://` repositories and
`SigLevel` 0, linked against `holonight_transaction_test_support`.

## S-1 (a, b) — lock placement and written directories (T-001) — DONE 2026-11-12

Method: a C++ program builds a world with repositories `core` (`alpha`) and `extra` (`beta`), installs both at
`1.0-1`, publishes `2.0-1` to the `file://` servers and puts the `1.0-1` databases back into the live `sync/`, with an
mtime one day old. It then builds the scratch layout of DESIGN 5.3 under `<world>/state/upgrade-spike`:
`dbpath/local` is a symlink to the live `local/`, `dbpath/sync/` holds seeded copies of the live databases with their
mtimes kept. One handle is opened with `alpm_initialize(<world>/root, S/dbpath)`, both repositories are registered at
level 0 with their `file://` server, and the cache and gpg directories of the world are set. The download, event and
log callbacks print every event together with the presence of `S/dbpath/db.lck` and the live `<world>/db/db.lck`.
Manifests (kind, content hash, size, mtime; symlinks not followed) of live `sync/`, live `local/`, the install root,
the gpg directory, the cache and the scratch root are taken before, after `alpm_db_update(handle, syncdbs, 0)`, and
after a `DOWNLOADONLY` transaction (`alpm_trans_init(DOWNLOADONLY)`, `alpm_sync_sysupgrade`, prepare, commit, release).

### Observed

1. **`alpm_initialize`** with the symlinked `local` works: the local database loads both packages through the link.
   No lock is created.
2. **`alpm_db_update` takes the lock itself, in the scratch dbpath.** `S/dbpath/db.lck` exists from
   `DB_RETRIEVE_START` through every `INIT`/`COMPLETED` download event to `DB_RETRIEVE_DONE`, and is gone when the call
   returns. The live `db.lck` never appears. The call returns 0 (both databases were newer on the server).
3. **`DOWNLOADONLY`** takes `S/dbpath/db.lck` in `alpm_trans_init` and drops it in `alpm_trans_release`, as for a
   normal transaction (helper S-1). The live `db.lck` never appears. Commit returns 0.
4. **Event order of the `DOWNLOADONLY` commit:** `RESOLVEDEPS`, `INTERCONFLICTS` (prepare), then `PKG_RETRIEVE_START`,
   the package downloads, `PKG_RETRIEVE_DONE`, `KEYRING_START/DONE`, `INTEGRITY_START/DONE`, and the commit ends. There
   is no `LOAD`, `FILECONFLICTS`, `DISKSPACE`, `TRANSACTION_START` or hook event. So a `DOWNLOADONLY` commit **runs the
   keyring and integrity checks** (input for S-1 c, T-002).
5. **Written directories.** After the sync: only `S/dbpath/sync/core.db` and `extra.db` changed. Live `sync/`, live
   `local/` (through the link), the root, the gpg directory and the cache are unchanged. After the fetch: only the
   cache gained `alpha-2.0-1-any.pkg.tar.zst` and `beta-2.0-1-any.pkg.tar.zst`. Everything else, including the scratch
   root, is unchanged. The installed versions stay `1.0-1`.

### Consequences

- DESIGN 8.1 (a): **design as written.** The scratch handle locks only `S/dbpath/db.lck`, for the sync and for the
  fetch. Another package manager is never blocked by the preparation or the fetch phase, and the live lock is first
  taken in phase 7.
- DESIGN 8.1 (b): **design as written.** The `local` symlink is usable, and nothing outside the scratch root and
  `CacheDir` is written. The read-only copy fallback is not needed.
- F-2 is confirmed as a real hazard: during phases 4 and 5 the owned lock is the scratch one, so the terminator must
  remove the session's own lock path (T-009), not the live lock file.
- `alpm_db_update` needs no transaction and no live lock, so phase ordering in DESIGN 5.3 stays.
- Item 4 is handed to T-002: whether the integrity step of `DOWNLOADONLY` rejects signatures (it runs `KEYRING` and
  `INTEGRITY`), which matters for D-6.

## S-1 (c, d, e) — signatures at fetch, disk space, If-Modified-Since (T-002) — DONE 2026-11-12

Method: a second program, one process per case (libalpm initialises gpgme once per process). A `core` repository with
`alpha` installed at `1.0-1` and `2.0-1` on the `file://` server, signed by a fixture key that is trusted in the world
keyring. A `DOWNLOADONLY` sysupgrade runs with a question callback that answers 0 and logs every question.

### Observed (c): package signatures in a `DOWNLOADONLY` commit

| `.sig` | Level | Commit | errno | Log and questions | Cache afterwards |
|---|---|---|---|---|---|
| good | Required | 0 | — | `KEYRING`, `INTEGRITY` events | package and `.sig` |
| good | Optional | 0 | — | same | package and `.sig` |
| corrupt | Required | -1 | 37 `ALPM_ERR_PKG_INVALID_SIG` | "signature format error", "missing required signature", question `CORRUPTED_PKG` reason 37 | package and `.sig` |
| corrupt | Optional | -1 | 37 | "signature format error", question `CORRUPTED_PKG` reason 37 | package and `.sig` |
| missing | Required | -1 | 52 `ALPM_ERR_LIBCURL` | "failed retrieving file '….sig'", `PKG_RETRIEVE_FAILED` | package only |
| missing | Optional | 0 | — | — | package only |
| unknown key | Required | -1 | 4 | `KEY_DOWNLOAD_START/DONE`, "keyring is not writable" (non-root), "required key missing from keyring" | package and `.sig` |
| unknown key | Optional | -1 | 4 | same | package and `.sig` |
| any of the above | Never (0) | 0 | — | no signature work; **the `.sig` is not downloaded** | package only |

1. A `DOWNLOADONLY` commit verifies signatures with the handle's level, as a normal commit does. So it uses the old
   keyring.
2. `Optional` does not help with an unknown key: a signature that is present must verify.
3. At level `Never` libalpm does not download the `.sig` at all. Arch's sync databases carry no embedded signatures,
   so a later check (main step, `PackageVerifier`) would have nothing to verify. Relaxing the fetch level is
   therefore not an option.
4. libalpm downloads every file before the keyring check. When the keyring check fails (unknown key), the package
   and its `.sig` are already in the cache.
5. As root with a writable keyring, libalpm would ask the `IMPORT_KEY` question here (non-root it stops at "keyring
   is not writable"). The answer must be no (REQ-F-029). This branch needs the VM check (T-085).

### Observed (d): disk space

With `alpm_option_set_checkspace(handle, 1)` a `DOWNLOADONLY` commit emits no `DISKSPACE_START/DONE`. libalpm's check
runs only in a real commit.

### Observed (e): `alpm_db_update` with a seeded scratch copy

| Server file vs. seeded copy | Return | Seeded copy after |
|---|---|---|
| same content, same mtime | 1 (up to date) | unchanged |
| **different content, same mtime (to the second)** | **1** | **unchanged (stale)** |
| newer mtime | 0 (updated) | server content |

libalpm sends If-Modified-Since from the seeded file's mtime, also for `file://`. It trusts the time only.

### Consequences

- **D-6 revised.** The fetch handle keeps the configured package level, so `.sig` files reach the cache. A commit
  that fails only in the keyring check with "required key missing from keyring" is tolerated when the plan has a
  keyring step and every planned file and `.sig` is in the cache; `PackageVerifier` (phase 5) then checks the
  checksums (libalpm's integrity step did not run) and the signatures, deferring key-missing for non-keyring packages
  to the main step. Without a keyring step the failure is `KeyImportRequired`. DESIGN F-6, phase 4, 8.1 (c) and D-6
  are updated.
- Classification at fetch matches the helper's rules: corrupt `.sig` is errno 37 (`SignatureInvalid`, no retry,
  REQ-F-047); a missing `.sig` at `Required` is a download failure of a `.sig` file (`SignatureInvalid`, helper
  SPIKES); missing at `Optional` passes.
- **(d) design as written:** the `statvfs` estimate of phase 3 stays (REQ-F-048 as amended).
- **(e) design as written:** seeding is safe in production because pacman sets each database mtime from the server's
  Last-Modified, and a new mirror database has a later time. Tests must give the server file and the seeded copy
  mtimes at least a second apart (DESIGN 8.1 e uses a day).

## S-1 (f) — `DownloadUser` sandbox and the scratch root mode (T-003) — DONE 2026-11-12

Method: a libalpm-only program, run as root in a container of the development image (pacman 7.1.0, real `alpm`
user, uid 979). It builds `/var/lib/hn-state/upgrade-spike/dbpath/{sync,local→/var/lib/pacman/local}` with chosen
modes, sets `alpm_option_set_sandboxuser(handle, "alpm")`, registers `core` with a `file:///srv/repo` server (a copy
of a real `core.db`) and calls `alpm_db_update`. A baseline run does the same against the live `/var/lib/pacman`.

### Observed

| Container | State dir | Scratch root | `dbpath`, `sync` | Sandbox user | Result |
|---|---|---|---|---|---|
| default Docker | any | any | any | `alpm` | fails: "Landlock ruleset could not be applied: Operation not permitted", "switching to sandbox user 'alpm' failed!" — **the baseline against `/var/lib/pacman` fails the same way** |
| `--privileged` | 0700 | 0700 | 0755 | `alpm` | fails: "could not open file …/sync/download-XXXXXX/core.db.part: Permission denied" |
| `--privileged` | 0711 | 0711 | 0755 | `alpm` | **succeeds**; `core.db` ends owned by root, mode 0644 |
| `--privileged` | 0711 | 0711 | 0711 | `alpm` | **succeeds** |
| `--privileged` | 0711 | 0711 | 0700 | `alpm` | fails: permission denied, as above |
| `--privileged` | 0700 | 0700 | 0755 | none | succeeds (no sandbox) |
| `--privileged` | — | — | live `/var/lib/pacman` | `alpm` | succeeds (baseline) |

1. libalpm creates `sync/download-XXXXXX/` for the sandboxed child, which then opens `<file>.part` inside it. The
   child needs search (`x`) permission on every directory above it; read permission is not needed.
2. Mode `0711` (root) on the state directory, the scratch root, `dbpath` and `dbpath/sync` is enough. The finished
   database is moved into place by root and is owned by root.
3. The Landlock part of the sandbox needs `no_new_privs` or `CAP_SYS_ADMIN`. A default Docker container has neither,
   so the sandbox (even pacman's own) cannot be tested in the CI container. The helper unit sets
   `NoNewPrivileges=yes`, so it works there; that is the T-085 VM check.

### Consequences

- **D-9 confirmed** with `0711` for the state directory, the scratch root, `dbpath` and `dbpath/sync` (DESIGN 5.3
  updated from 0755 to 0711). REQ-F-008 holds as amended: others can traverse but not list.
- Automated tests run without a sandbox user (as the existing transaction tests do) and check the modes and the
  configured sandbox user only (REQ-F-008 criteria). The sandboxed download itself is verified in the VM (T-085).

## S-2 — keyring update visible to gpgme in the same process (T-004) — DONE 2026-11-12

Method: a world keyring that trusts key A. Package `app 2.0-1` is signed by key B, which is not in the keyring. In one
process: (1) `alpm_pkg_load(handle, file, 1, ALPM_SIG_PACKAGE)` on the package; (2) key B is imported and given
ultimate owner trust in the world keyring with `gpg --homedir` (the effect of `pacman-key --populate` run by the
keyring package's install script), and the gpg agent is stopped; then the same check through (ii) the same handle, (i)
a new handle, and (iii) a child process (the program re-executes itself). The run is repeated with `use-keyboxd` in
the keyring's `common.conf`. gpg 2.4.9.

### Observed

| Check | plain keyring | `use-keyboxd` |
|---|---|---|
| (1) before the update | -1, "required key missing from keyring" | same |
| (ii) same handle, after | 0 | 0 |
| (i) new handle, after | 0 | 0 |
| (iii) child process | 0 | 0 |

gpgme starts `gpg` for each verification, and `gpg` reads the keyring on each start. libalpm's once-per-process gpgme
initialisation fixes the keyring *directory* (helper SPIKES), not its content. The main step and the keyring step use
the same directory, the configured `GPGDir`.

### Consequences

- **DESIGN 8.2: in-process main step, variant (i)** (a new live handle or transaction after the keyring step), as
  designed. Phase 13 of DESIGN 5.6 is final. The child-process variant and the SPEC fallback are not needed.
- Not covered here: the real keyring package runs `pacman-key --populate` from its install script as root under the
  install root. That is part of manual check 1 (T-081).

## S-3 — promotion by rename or by copy (T-005) — DONE 2026-11-12

Method: inside `unshare -Urm`, two tmpfs mounts, one holding a live dbpath (`sync/` plus a `local` link) and one a
scratch dbpath. `core.db` has 300 packages at `1.0-1` (live) and `2.0-1` (scratch, mtime one hour later).

### Observed

1. `rename(scratch, live)` across the two mounts fails with `EXDEV` ("Invalid cross-device link").
2. Copy to `<live>/sync/.hn-promote-core.db.tmp` with the mtime kept, then `rename` over the live file: succeeds, the
   mtime equals the scratch file, libalpm reads the new content.
3. A rename inside one mount keeps the inode and the mtime.
4. A reader thread that opens a new handle and loads the whole `core` package cache in a loop, while the live file is
   replaced 200 times by copy-then-rename, only ever saw the complete old or the complete new database ("old",
   "new"), never a mix or an empty or failed load.
5. A child killed with `SIGKILL` after writing the temporary copy and before the rename leaves the temporary file
   behind and the live file unchanged (old content).
6. A `.hn-promote-core.db.tmp` file in `sync/` does not disturb `alpm_register_syncdb` or loading: libalpm opens
   databases by name only.
7. A handle that **registered** `core` before the promotion but had not read it yet loads the new content on its
   first read. A handle that **read** the database before the promotion keeps its old package cache after it.

### Consequences

- DESIGN 5.8 and REQ-F-077 confirmed: rename on one filesystem, copy-then-rename otherwise; each live file is replaced
  atomically for readers; the start-up sweep of `.hn-promote-*` is needed (item 5) and harmless to libalpm (item 6).
- Phase 7 of DESIGN 5.6 is confirmed with one rule made explicit: **the live handle must not read a sync database
  before promotion** (no package-cache or search call on it in phases 7 to 11). Phase 8 compares files, and phase 9
  resolves on the scratch handle, so nothing needs it. A unit test asserts that the live handle's first sync read
  comes after the `promotion-done` seam. DESIGN 5.6 updated.

## S-4 — internal sync timeout and scratch size cap (T-006) — DONE 2026-11-12

Method: the real databases of this machine (`core.db` 130,504 bytes, `extra.db` 9,084,385 bytes; Arch publishes no
database signatures, `multilib` is not enabled here and is about 0.15 MB on a mirror) are served by a throttled
loopback HTTP server (spike only) that sends `Last-Modified` and `Content-Length` in 16 KiB chunks. A libalpm-only
program runs `alpm_db_update` into an empty scratch dbpath with `ParallelDownloads = 5` and logs every download event.

### Observed

| Link | `core.db` done | `extra.db` done | Result |
|---|---|---|---|
| loopback, unthrottled | 0.0 s | 0.0 s | 0 |
| 8 Mbit/s | 0.1 s | 9.2 s | 0 |
| 1 Mbit/s | 0.9 s | 72.7 s | 0 |
| 128 kbit/s | — | about 570 s (computed: 9.08 MB at 16 kB/s; not run) | would exceed 480 s |
| stall after 2 MB of `extra.db` | 0.0 s | — | -1 after 17.0 s, errno 52: "Operation too slow. Less than 1 bytes/sec transferred the last 10 seconds" |

- Largest byte gap between two progress events of one file: 173,056 bytes (unthrottled loopback), 16,384 bytes
  when throttled. The `total` is known from the first progress event when the server sends `Content-Length`.
- libcurl's low-speed abort (about 10 s below 1 byte/s, as libalpm sets it) ends a real stall. It does not end a
  slow drip above 1 byte/s, so the absolute internal timeout is still needed.

### Consequences

- **Internal sync timeout: 480 s confirmed.** It completes the official databases on links down to about
  160 kbit/s (9.2 MB in 480 s). Slower links get `SyncFailed`; the 600 s client timeout (decided in Stage 0) does
  not allow more. Recorded as a known limit in DESIGN 8.4 and in the user-facing notes (T-077).
- **Scratch size cap: 256 MiB confirmed.** The official databases need about 9.4 MB, so the cap leaves room for
  large third-party repositories. The overshoot with an unknown length is bounded by the measured event gap
  (under 0.2 MiB), far below the 8 MiB threshold of DESIGN 8.4, so the cap is not lowered.
- libalpm's own low-speed abort stays enabled (`disable_dl_timeout` 0), as designed.
