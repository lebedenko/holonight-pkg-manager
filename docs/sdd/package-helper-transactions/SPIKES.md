# Spikes — package-helper-transactions

Throwaway programs for these spikes are not kept in the repository. Each section records the question, the method, the observed result and the consequence for SPEC.md and DESIGN.md. Environment: pacman 7.1.0, libalpm 16.0.1, run as a non-root user against a temp-dir fixture root with a `file://` repository (`SigLevel` 0).

## S-1 — commit event order, lock behaviour, interrupt (T-001) — DONE 2026-11-09

Method: a C++ program that initialises libalpm against a fixture root, registers one `file://` sync repository, installs or removes fixture packages, and prints every event with the state of `<dbpath>/db.lck` at that moment.

### Observed

1. **Lock.** Without `ALPM_TRANS_FLAG_NOLOCK`, libalpm creates `db.lck` inside `alpm_trans_init` (not at commit) and removes it in `alpm_trans_release`. It is present during prepare, download, hooks and the whole commit. With `ALPM_TRANS_FLAG_NOLOCK`, `alpm_trans_prepare` works, but `alpm_trans_commit` fails immediately with `ALPM_ERR_HANDLE_LOCK` ("transaction commit attempt when database is not locked"), for install and for remove. `NOLOCK` is therefore usable for read-only resolution only.
2. **Event order for an install** (no hooks, no `CheckSpace`): `RESOLVEDEPS_*`, `INTERCONFLICTS_*` (during prepare); then inside commit `PKG_RETRIEVE_*`, `KEYRING_*`, `INTEGRITY_*`, `LOAD_*`, `FILECONFLICTS_*`, `TRANSACTION_START`, `PACKAGE_OPERATION_START/DONE` per package, `TRANSACTION_DONE`. `DISKSPACE_*` does not appear unless `CheckSpace` is set.
3. **Hooks.** With an empty hook directory, no `HOOK_START` is emitted, so `TRANSACTION_START` is the first event at which libalpm may change the root. With hooks present, `HOOK_START(PRE)`, `HOOK_RUN_START`, `SCRIPTLET_INFO`, `HOOK_RUN_DONE`, `HOOK_DONE(PRE)` come after `FILECONFLICTS_DONE` and before `TRANSACTION_START`; the PostTransaction pair follows `TRANSACTION_DONE`. This confirms the commit-point definition of DESIGN 6.7 (first PRE `HOOK_START` or `TRANSACTION_START`) and the event order. OI-14 is closed.
4. **Hooks as non-root.** The fixture hooks (`Exec = /usr/bin/true`) logged "command failed to execute correctly" (the `chroot` fails without root), but a hook without `AbortOnFail` only warns and the commit still returns 0. The hook failure semantic of REQ-F-073 therefore needs `AbortOnFail` fixtures (T-057); S-3 covers the user-namespace route.
5. **`alpm_trans_interrupt` only works late.** It returns `-1` and has no effect when called from `PKG_RETRIEVE_START`, `INTEGRITY_START`, `LOAD_START`, `FILECONFLICTS_START` and the PreTransaction `HOOK_START`: the commit ran to the end and installed everything. Called from `TRANSACTION_START` it returns 0, stops the package loop, and `alpm_trans_commit` still returns 0 (installed set: nothing). Called from the first `PACKAGE_OPERATION_START` it returns 0 and leaves exactly the first package installed (`lib` of two). So interrupt is effective only at or after `TRANSACTION_START`, is not an error exit, and leaves a **partial** installed set.
6. **Failed prepare.** A prepare that fails on an unresolvable dependency returns -1 (`ALPM_ERR_UNSATISFIED_DEPS`) after raising a `SKIP_UNRESOLVABLE`-type question (question type 16 in this run; the default answer 0 fails the prepare). After `alpm_trans_release` the same handle accepts `alpm_trans_init`, `alpm_add_pkg` and a successful prepare. Handles survive a failed prepare.

### Consequences (SPEC and DESIGN need these changes)

- **C1 (SI-4 / REQ-F-046, REQ-F-047): the helper cannot own the lock file with `NOLOCK`.** Options: (a) do not use `NOLOCK` for the execute transaction and let libalpm own `db.lck` from `alpm_trans_init`; the lock then covers re-resolution only if re-resolution runs inside that same transaction; (b) keep a helper-created lock plus a normal libalpm lock by handing the lock over (not possible without a gap). Recommended: (a). `Execute` initialises a handle, starts a locked transaction (`flags` without `NOLOCK`), re-resolves inside it, compares with the preview, and commits in the same transaction. `Busy` on a held lock is `ALPM_ERR_HANDLE_LOCK` from `alpm_trans_init`. The corrupted-package retry (REQ-F-055) releases and re-takes the lock; the gap is documented. `PreparePlan` keeps using `NOLOCK` and never commits.
- **C2 (SI-10 / REQ-F-056, REQ-F-057, REQ-F-059): `alpm_trans_interrupt` cannot implement cancel before the commit point.** It is a no-op before `TRANSACTION_START`. Cancel before the commit point needs another route (S-2 tests the fetch callback returning an error and signal delivery); for phases without such a hook (integrity, load, file conflicts) a cancel can only take effect at the next opportunity, that is, at the gate in front of `TRANSACTION_START` (the `CommitGate` must be able to refuse by failing the transaction there, which libalpm does not offer either). Needs a decision after S-2 (see below).
- **C3 (REQ-F-058): after `TRANSACTION_START`, `alpm_trans_interrupt` partially applies the plan and reports success.** The helper must not call it after the commit point, as the SPEC already says (cancel after the commit point is refused). It must never be used as a rollback.
- **C4 (REQ-F-073):** hook failure tests need `AbortOnFail` hooks and a way to run `chroot` (S-3).

### Open after S-1

- Whether the fetch callback (S-2) can abort in-flight downloads and make `alpm_trans_commit` fail cleanly before `TRANSACTION_START` with an unchanged installed set.
- Whether a cancel requested while the helper is in `INTEGRITY`, `LOAD` or `FILECONFLICTS` can be honoured by returning `1` from a `libalpm` log or event callback (it cannot, by the header), or must be deferred to the commit gate. If it cannot, REQ-F-056 should say that a cancel is honoured at the next phase boundary the helper controls (download transfer, or the gate before `TRANSACTION_START`) and that the helper ends `Cancelled` only if no package change happened.

## S-2 — cancelling an in-flight download (T-002) — DONE 2026-11-09

Method: a loopback HTTP server (spike only, never in tests) serves two 6 MB packages at 1 MB/s. `alpm_trans_commit` runs on a worker thread; 1.5 s after `PKG_RETRIEVE_START` the main thread tries one cancel action. `SIGINT` is set to `SIG_IGN` for the process beforehand.

### Observed

| Action at +1.5 s | Result |
|---|---|
| `alpm_trans_interrupt(handle)` | returns `-1`, no effect |
| `pthread_kill(worker, SIGINT)` (process `SIG_IGN`) | no effect |
| `kill(getpid(), SIGINT)` (process `SIG_IGN`) | no effect |
| `SIGTERM` to child processes (default sandbox settings, non-root) | there is no downloader child process; only the spike's own `sh` exists |

In all four runs both transfers completed (at 6.0 s and 12.0 s), the commit returned 0 and installed both packages. libalpm 16 / pacman 7.1 has no SIGINT handler and no interrupt check in its download loop. `SIGINT` is still `SIG_IGN` after the commit. The download callback (`alpm_option_set_dlcb`) is void and cannot abort.

### Consequences

- **The SIGINT scheme of DESIGN 6.8 does not work** and must be removed, together with the process-wide `SIG_IGN`.
- **`alpm_trans_interrupt` is not a pre-commit cancel** (S-1 point 5, confirmed here for downloads).
- The only in-libalpm abort for a download is `alpm_option_set_fetchcb` (a replacement downloader written by us, root-side network code). Not recommended.
- **Safe-to-abort window.** Everything before the first PRE `HOOK_START` or `TRANSACTION_START` is read-only against the root (S-1 point 2): downloads write only to the cache directory, and the key, integrity, load and file-conflict checks only read. The process can stop at any point in that window without leaving a changed root or database. It leaves behind `db.lck` (libalpm created it; the helper knows the path) and `*.part` files in the cache.
- **Candidate mechanisms for REQ-F-056** (decision needed, see the docs update):
  1. *Controlled helper exit:* on `Cancel` before the commit gate, atomically (under the gate mutex) mark the plan cancelled, unlink the lock, send the `Cancelled` reply, then exit the process immediately. The next call re-activates the helper through the bus. Simple and reliable. Not testable in-process: it needs a seam (the test fake records "would exit"), plus one end-to-end test that runs the helper as a child of the test.
  2. *Deferred cancel:* accept `Cancel`, let the download and verification phases finish, and stop at `TRANSACTION_START` with `alpm_trans_interrupt` (S-1 point 5: installs nothing, `commit` returns 0). Cancel latency is the remaining download time, and PreTransaction hooks (if any) have already run by then.
  3. *Run the libalpm transaction in a forked child process* and kill it on cancel. It isolates libalpm and makes cancel exact, but needs fork inside a multithreaded Qt process and a protocol between parent and child; larger than this feature.
- A sandboxed download (root plus `DownloadUser`) runs the transfer in a forked child in libalpm; that case cannot be observed as non-root and becomes a manual-VM check (add to the manual checklist).

## S-3 — hooks and `chroot` without root (T-003) — DONE 2026-11-09

Method: a statically linked marker tool (`gcc -static`, 15 lines: writes `/marker-<name>` and optionally exits 7) is copied to `<root>/usr/bin/marker` of the fixture root. PRE and POST hooks run it. The same install is run as the plain user and under `unshare -Ur` (user namespace, uid 0 mapped to the user).

### Observed

| Case | Result |
|---|---|
| Plain user, PRE hook with `AbortOnFail` | `chroot` fails; log "command failed to execute correctly"; `commit` returns -1 ("failed to run transaction hooks", `ALPM_ERR_TRANS_HOOK_FAILED`); nothing installed |
| Plain user, hook without `AbortOnFail` | warning only; `commit` returns 0 |
| `unshare -Ur`, passing PRE and POST hooks | `chroot` works; both markers are created; `commit` returns 0 |
| `unshare -Ur`, PRE hook exits 7 with `AbortOnFail` | `commit` returns -1; the local database is empty; the root holds only the marker file the hook wrote |
| `unshare -Ur`, POST hook exits 7 (no `AbortOnFail`; a POST hook can never abort) | `commit` returns 0 and both packages are installed; only a log line "command failed to execute correctly" reports the failure |
| Ownership during extraction under `unshare -Ur` | libalpm logs a warning per file ("Can't set user=1000/group=1000"); harmless, the commit succeeds |

User namespaces are available here (`max_user_namespaces` 256248). Availability in the CI container is unknown and is checked by the skip policy of REQ-C-014 (T-056).

### Consequences

- The `userns` test approach of DESIGN 12.5 works. The marker tool needs no libc in the fake root.
- **REQ-F-073 (post-commit hook failure):** a failing POST hook does not change the `commit` return value (0), so `HookFailed` can only be detected from the log callback text ("command failed to execute correctly") or from a marker. The detection is therefore a log-text match; the exact string must be pinned in a test, and the DESIGN's mapping must say so. PRE hook failure is detected by `ALPM_ERR_TRANS_HOOK_FAILED` and only with `AbortOnFail`.
- libalpm ownership warnings under userns must not be treated as errors by the test harness.

## S-4 — download failure classification (T-004) — DONE 2026-11-09

Method: the S-2 harness with 200 KB packages; `alpm_trans_commit` with one repository server per case. Strings are the `LOG_ERROR` lines delivered to the log callback, with the file name and path varying.

| Case | `alpm_errno` after commit | Log line (exact) | Time |
|---|---|---|---|
| HTTP 404 | `ALPM_ERR_RETRIEVE` (49) | `failed retrieving file '<f>' from <host:port> : The requested URL returned error: 404` | 0 s |
| HTTP 500 | `ALPM_ERR_RETRIEVE` (49) | same, `... error: 500` | 0 s |
| `file://` file missing | `ALPM_ERR_LIBCURL` (52) | `failed retrieving file '<f>' from disk : Could not open file <path>` | 0 s |
| `file://` file mode 000 (non-root) | `ALPM_ERR_LIBCURL` (52) | identical to "missing" | 0 s |
| `file://` server directory missing | `ALPM_ERR_LIBCURL` (52) | identical to "missing" | 0 s |
| stalled transfer (server sends 1000 bytes, then idles) | `ALPM_ERR_LIBCURL` (52) | `failed retrieving file '<f>' from <host:port> : Operation too slow. Less than 1 bytes/sec transferred the last 10 seconds` | 16 s |

Every failure also logs `failed to retrieve some files` and raises the download callback with `ALPM_DOWNLOAD_COMPLETED` and `result = -1`; `commit` returns -1 and the data list is empty.

### Consequences

- **Absent file classification (REQ-F-028):** an HTTP 404 is recognisable from the log text `returned error: 404` (errno 49). For `file://`, "missing" and "mode 000" are indistinguishable from libcURL's text; the helper must `stat` the path after `Could not open file` (ENOENT or ENOTDIR gives `SyncDatabaseStale`; EACCES and everything else gives `DownloadFailed`). The path in the log is `<server path>/<file>` (the file name is in the line, the directory is the configured server URL).
- **Other HTTP codes (500, 403 ...)** are `DownloadFailed`.
- **Stall classification:** the text contains `Operation too slow`; it takes 10 s of no progress plus a connect/startup phase (16 s in this run). A test that waits for it on a real socket would take 16 s and needs a loopback socket, which REQ-C-009 forbids; the stall case therefore uses the fetch-override seam of DESIGN, or a unit test of the classifier on the log strings.
- The classifier is a pure function on `(alpm_errno, log lines, stat result)`; the table above is its test data.

## S-5 — corrupted package question and retry (T-005) — DONE 2026-11-09

Method: a fixture package (`lib`) has one byte flipped in the served `file://` repository, so its SHA-256 no longer matches the sync database entry. `SigLevel` is 0, so the failure comes from the checksum. The question callback answers `remove = 1` or `remove = 0`; then the same handle is released, re-initialised and the transaction repeated, with or without the file fixed in between.

### Observed

- The question is `ALPM_QUESTION_CORRUPTED_PKG` (type 8). It is raised **during `INTEGRITY_START`**, after `PKG_RETRIEVE_*`, before `TRANSACTION_START`. It carries the cached file path and `reason = ALPM_ERR_PKG_INVALID_CHECKSUM` (36).
- Whatever the answer, `alpm_trans_commit` returns -1 with `alpm_errno = ALPM_ERR_PKG_INVALID_CHECKSUM` (36). The question only decides whether the cached file is deleted.
- `remove = 1`: the cached file is deleted. A second attempt on the **same handle** (`alpm_trans_release`, `alpm_trans_init`, `alpm_add_pkg`, `alpm_trans_prepare`, `alpm_trans_commit`) downloads the file again (`PKG_RETRIEVE_START` is raised) and succeeds if the repository now serves a good file; it raises the same question and fails again if it does not.
- `remove = 0`: the corrupt file stays in the cache and the second attempt reuses it (no download) and fails the same way.
- Nothing is installed after the failed attempts; the local database stays empty.

### Consequences

- DESIGN SI-5 holds: libalpm itself never retries; the retry is a helper-level re-run of the whole execution (REQ-F-055), and the answer to the question must be "yes" (remove) for the retry to download again.
- The question arrives before the commit point, so the retry is allowed only before `TRANSACTION_START`, as REQ-F-055 says.
- A corrupted package ends as `TransactionFailed` (checksum, invalid, open errors) after the second failure, matching DESIGN 10.2.

## S-6 — throwaway keyring and `Required` verification (T-006) — DONE 2026-11-09

Method: `gpg --quick-gen-key ... ed25519 sign never` in a temp `GNUPGHOME` (no passphrase), `--import-ownertrust` with trust 6 (ultimate), the public key imported into a temp libalpm `GPGDir`, packages signed with `gpg --detach-sign` (`<pkg>.sig` next to the package in the `file://` repository). libalpm runs with `ALPM_SIG_PACKAGE` (= `Required`, `TrustedOnly`) and sync-database level 0 (the repository registered with `ALPM_SIG_USE_DEFAULT`; `alpm_db_get_siglevel` returns the default, 1).

### Observed

- **Cost:** key generation takes about 20 ms (10 keys in 0.21 s); no flakiness in 10 runs; no entropy wait with ed25519. Risk 6 of the SPEC is small on this machine; the CI image is still to be confirmed by T-015.
- **Good signature, trusted key:** `commit` returns 0. libalpm logs the harmless warning "Public keyring not found; have you run 'pacman-key --init'?" (it looks for `pubring.gpg`, gnupg 2.4 writes `pubring.kbx`); verification still works.
- **Signature file missing** (unsigned package): libalpm tries to download `<pkg>.sig`; the failure surfaces as a download failure, `alpm_errno = ALPM_ERR_LIBCURL` (52), log `failed retrieving file '<pkg>.sig' from disk : Could not open file <path>.sig`. It is **not** reported as a signature error.
- **Key present but not trusted** (no ownertrust): INTEGRITY phase; log `<pkg>: signature from "<uid>" is unknown trust`; the `CORRUPTED_PKG` question is raised with `reason = ALPM_ERR_PKG_INVALID_SIG` (37); `commit` returns -1, `alpm_errno = 37`.
- **Tampered signature:** same as above with log `signature from "<uid>" is invalid`.
- **Signing key absent from the keyring:** `commit` returns -1 and `alpm_errno` is **0** ("unexpected error"); logs `keyring is not writable` and `required key missing from keyring`; no `IMPORT_KEY` question was raised (no keyserver path; the keyring was read-only for the spike).
- The sync database entries carry no `PGPSIG` (`repo-add` without `--include-sigs`); the detached `.sig` file is used.

### Consequences

- **SignatureInvalid classification (DESIGN 10.2) needs three rules:** `alpm_errno == ALPM_ERR_PKG_INVALID_SIG` (37) after the integrity phase; a missing-signature download failure (the failed file name ends in `.sig`); and `alpm_errno == 0` with the log text `required key missing from keyring`. The `file://` ENOENT rule of REQ-F-028 must **not** classify a missing `.sig` as `SyncDatabaseStale`.
- The `CORRUPTED_PKG` question also carries signature failures (reason 37). REQ-F-055 retries a "corrupted package" once; a signature failure is not corruption, so the retry should apply only to reasons checksum (36) and invalid package (not 37). Decision for the docs update.
- The harness can generate keyrings cheaply (T-015); a second keyring covers "unknown key" and a copy without ownertrust covers "unknown trust".
