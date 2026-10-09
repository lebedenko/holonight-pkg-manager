# Spikes — online-update-check

Environment: Arch Linux, pacman 7.1.0 / libalpm 16.0.1, run as a non-root user (uid 1000).
Probe programs were throw-away C files linked against libalpm; they are not part of the repository.

## S-1 — watchdog, FIFO, libalpm/libcurl timeout and libcurl init race (REQ-NF-001)

**Setup.** A scratch dbpath (`db/local`, `db/sync`), one sync database `core` with `Server = file://<w>/mirror`, and
`mirror/core.db` created with `mkfifo` and never opened by a writer. `alpm_db_update(h, syncdbs, force=0)` called with
libalpm's default timeout settings (`alpm_option_set_disable_dl_timeout` not called).

**Result.**

- The call **blocks in `open()`** and does not return. It was still blocked when `timeout 40` killed it (exit 124), so
  the time to return with the default timeout setting is **unbounded** (more than 40 s observed).
- libalpm's low-speed abort (about 10 s below 1 byte/s) does not apply, because no transfer ever starts: libcurl is
  stuck opening the `file://` path.
- libalpm has no cancel API. A hung transfer therefore cannot be interrupted from outside.
- The watchdog of DESIGN 5.1 (check body on a detached `std::thread`, caller waits `totalBound`, returns
  `NetworkUnavailable` on timeout) is the answer. The detached thread keeps the scratch `flock` descriptor open, so a
  later check sees the lock held and returns `Busy`. A process exit terminates the blocked thread normally.
- **Judgement: the watchdog is acceptable. No forked-child fallback is needed, so REQ-C-002 needs no wording change.**
- The test must unblock the FIFO at the end (open it for writing and close) so the detached thread can finish.

**libcurl init race.** libalpm initialises libcurl lazily inside its own download code. The installed libcurl is
recent (>= 8.x, where `curl_global_init` is thread-safe), and `holonight-packaged` uses no other libcurl consumer
(Qt Network uses its own stack). The check body runs on one thread at a time (single-flight plus `flock`), and a
detached wedged thread can only overlap a new check if the lock were not held, which the lock prevents. Outcome: no
race observed, no extra mitigation. Mitigation if it ever appears: perform a no-op `alpm_initialize`/`alpm_release`
on the main thread at service start.

## S-2 — signature verification as non-root (REQ-C-004)

Mirror served from `file://`, database `core.db` taken from `tests/fixtures/pacman/updates/sync` (unsigned, no
`core.db.sig`), empty scratch `sync/`, non-root.

| Database SigLevel registered | gpgdir | Result |
|---|---|---|
| `USE_DEFAULT` (handle default is `0` = no verification) | default | `alpm_db_update` returns 0 |
| `DATABASE \| DATABASE_OPTIONAL` (Arch `DatabaseOptional`) | default | returns 0; no gpg state needed because there is no signature |
| same | `/nonexistent-gpg` | returns 0 (nothing to verify) |
| `DATABASE` (`Required`) | default or `/nonexistent-gpg` | returns -1, `ALPM_ERR_LIBCURL` (52): the required `core.db.sig` cannot be downloaded |
| handle default `DATABASE\|DATABASE_OPTIONAL`, db registered with `DATABASE` | default | returns -1, errno 52 (a per-db level overrides the default) |

**Findings.**

- `DatabaseOptional` works as non-root for an unsigned or signed-with-known-key repository when no `.sig` exists.
  Verifying a present signature needs a readable `gpgdir` keyring; I did not build a signed fixture (no key
  material in the repository), so the signed path is not exercised here.
- `Required` on an unsigned database fails closed. The failure surfaces as `ALPM_ERR_LIBCURL`, not as a signature
  errno, so with all-`file://` servers it maps to `RepositoryUnreachable` by the DESIGN 5.3 table. That is still a
  failure with no snapshot, which is what the spec requires.
- Fail-closed outcome when gnupg state is not usable (signature present but keyring unreadable or not writable):
  libalpm returns `ALPM_ERR_GPGME` / `ALPM_ERR_SIG_*` / `ALPM_ERR_DB_INVALID_SIG`, which the mapping table sends to
  **`Unknown`**. The code never lowers the level to succeed.

## S-3 — pacman 7.1 built-in default SigLevel when pacman.conf sets none (REQ-C-004)

- `alpm_option_get_default_siglevel` on a fresh handle is `0`: libalpm itself defaults to no verification, so the
  parser **must** pass a level explicitly (never rely on `USE_DEFAULT`; `alpm_db_get_siglevel` on a
  `USE_DEFAULT` database also reads `0`).
- `pacman-conf --config <conf with no SigLevel> SigLevel` (pacman 7.1.0) prints `PackageRequired PackageTrustedOnly
  DatabaseRequired DatabaseTrustedOnly`. The built-in default in 7.1 is therefore **Required + TrustedOnly for both
  packages and databases**.
- For database registration this is `ALPM_SIG_DATABASE` (0x400) with neither `ALPM_SIG_DATABASE_OPTIONAL` nor the
  MARGINAL/UNKNOWN_OK flags. The probe confirmed `alpm_db_get_siglevel` reads back `0x400` for that value.

**Parser encoding (T-019).** When neither the repository nor the global section sets `SigLevel`, the parser yields
`DatabaseRequired | DatabaseTrustedOnly`, i.e. database level `ALPM_SIG_DATABASE`. `Optional` sets
`ALPM_SIG_DATABASE_OPTIONAL`, `TrustAll` sets MARGINAL_OK and UNKNOWN_OK, a `Package`/`Database` prefix scopes the
token, and an unrecognised token is a parse error.
