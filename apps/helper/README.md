# holonight-package-helper

The privileged half of HoloNight Packages: a small system D-Bus service that installs and removes packages from the
official Arch repositories through libalpm. It is started by the bus when a client calls it, authorizes every change through
polkit, and exits when it has nothing to do. The design and the requirements are in
`docs/sdd/package-helper-transactions/` (`SPEC.md`, `DESIGN.md`, `SPIKES.md`).

## What it does and does not do

- It installs and removes official-repository packages, nothing else: no system upgrade, no AUR, no local files or URLs,
  no PackageKit.
- It never refreshes the sync databases. An install that would need a newer version of something already installed
  (a partial upgrade) is refused with `PartialUpgradeRequired`; there is no override.
- It reads `/etc/pacman.conf` only. A caller sends an action and package names and nothing else, never a path, a
  repository, a server or a resolved package list.
- It refuses every install when a repository's `SigLevel` is weaker than `Required` + `TrustedOnly` (or not set at all).

## Client contract

Interface `org.holonight.PackageHelper1.Transactions` on the system bus, object `/org/holonight/PackageHelper1`
(`dbus/org.holonight.PackageHelper1.xml`).

1. `PreparePlan(action, names, options)` needs no authorization. `action` is `install` or `remove`; `options` must be
   empty. The reply carries a status, a plan ID and a preview. **Use a call timeout above 10 seconds** (the call
   resolves dependencies; the default 25 s of most bindings is enough, a shorter one is not).
2. `Execute(plan_id)` takes only the plan ID. polkit is asked at this point (`org.holonight.packages.install` or
   `.remove`), then the helper re-resolves the plan and runs it only if it is unchanged. The reply arrives when the
   transaction ends, so **use a call timeout of at least one hour** and treat `NoReply` as "unknown", not as failure.
3. `Cancel(plan_id)` is accepted until the transaction starts changing the system; after that it answers
   `PastPointOfNoReturn`. Cancellation also returns `PastPointOfNoReturn` once the engine has retired its control, while its final result is awaiting delivery. A cancel that is accepted while the work runs ends the helper process after the `Cancelled`
   reply; the bus starts a new one on the next call.
4. A plan belongs to the connection that prepared it, lives for 5 minutes and can be executed once. A helper restart
   forgets all plans (`PlanNotFound`).
5. `Progress(plan_id, phase, current, total, item)` is sent only to the unique name of the plan's owner. Subscribe with a
   match rule on the helper's well-known name (or its unique name) for interface
   `org.holonight.PackageHelper1.Transactions`, member `Progress`, on the connection that called `PreparePlan`. Phases:
   `Resolving`, `Downloading`, `Verifying`, `Hooks`, `Installing`, `Removing`, `Done`. `Hooks` can occur twice (before and
   after `Installing`), and `Done` is always last and only after success.
6. Refusals and failures are normal replies with a code, never D-Bus errors. A reply never contains free text: only codes,
   package names, versions and enumerated warnings (`HookFailed`). Raw libalpm output goes to the journal.
7. One transaction runs at a time. A second `PreparePlan` or `Execute` answers `Busy`.

If the helper was killed during a transaction, libalpm's lock file `/var/lib/pacman/db.lck` may be left behind. The helper
keeps the lock intact during the 150 ms reply grace period. Finalization then holds the transaction lifecycle mutex,
checks whether its own libalpm session owns the lock (libalpm writes no PID), removes only an owned lock, and exits
with status 0 without releasing the mutex. Worker acquisition, release and handle teardown cannot race this cleanup.
If the worker released first, a foreign replacement lock is preserved. Filesystem removal errors do not prevent exit.
After an external kill, check that no package manager is running before removing a stale lock by hand.

## Building and testing

```bash
cmake --build build/test --target holonight-package-helper test_holonight_package_helper \
  test_holonight_package_helper_polkit test_holonight_transaction test_holonight_transaction_userns
ctest --test-dir build/test -R 'holonight_(helper|transaction|userns)|helper_|userns_'
```

Tests run against throwaway fixture roots under the system temp directory and never need root. The hook tests run inside a
user namespace; `HOLONIGHT_REQUIRE_USERNS=1` makes them fail instead of skip when namespaces are unavailable.
`scripts/check-helper-artifacts.py` keeps the bus name, interface, action IDs, activation file, bus policy, polkit policy
and systemd unit in agreement.

## Manual verification (disposable Arch VM)

These steps need a real polkit agent, a real system bus and real root. Run on 2026-11-10 in an Arch cloud-image VM (QEMU/KVM, helper built with the release preset, signed throwaway repository, `pkttyagent` as the polkit agent): all 18 passed (one after a fix).

- [x] (passed 2026-11-10) Install a small official package from the GUI or a test client. The polkit prompt appears, and the transaction succeeds after authentication (REQ-F-063, REQ-F-065).
- [x] (passed 2026-11-10) Repeat an install within the same session without authentication if the `auth_admin_keep` cache is still valid, and confirm that it is accepted without a second prompt.
- [x] (passed 2026-11-10) Remove a package. The polkit prompt appears every time, with no cached authorization (REQ-F-064).
- [x] (passed 2026-11-10) Cancel at the polkit prompt. The reply is `NotAuthorized`, and the same plan can be executed afterwards within 5 minutes (REQ-F-009).
- [x] (passed 2026-11-10) Remove a package whose configuration file was modified. Confirm the `.pacsave` file exists (REQ-F-037).
- [x] (passed 2026-11-10) Confirm that no sync refresh happened: the modification times and sizes of `/var/lib/pacman/sync` files are unchanged after an install (REQ-F-027).
- [x] (passed 2026-11-10) Set `SigLevel = Optional` on a throwaway repository in the VM's `pacman.conf`. Confirm that install is refused with `InsecureSigLevel` and that the file is unchanged (REQ-F-039, REQ-F-041). Repeat with `SigLevel = Required TrustAll`.
- [x] (passed 2026-11-10) Install a package whose hook writes a marker in `/etc/pacman.d/hooks`. Confirm the marker exists and the hook appears in the preview (REQ-F-062).
- [x] (passed 2026-11-10) Under the hardened unit, install a package, remove a package, install a package that ships a hook and install one that ships an install scriptlet. All succeed (REQ-C-015, REQ-F-062).
- [x] (passed 2026-11-10) Install a package whose PostTransaction hook is made to fail. Confirm outcome `Succeeded` with the `HookFailed` warning (REQ-F-073).
- [x] (passed 2026-11-10) Remove a package that other installed packages require. Confirm `BreaksDependents` lists them and that nothing changed (REQ-F-033).
- [x] (failed on 2026-11-10, fixed and passed again the same day: Cancelled reply, helper exit and restart are correct, but `db.lck` stayed behind: libalpm 16.0.1 creates it empty, and `HelperTerminator` removed it only when it contained the helper's PID, so every later transaction answered `Busy`. The terminator now asks the engine whether this process's own transaction holds the lock) Cancel during a slow download (a throttled mirror in the VM). Confirm `Cancelled` and that `/var/lib/pacman/db.lck` is absent (REQ-F-056, REQ-F-046). Confirm the helper process ended and that the next call starts a new one.
- [x] (passed 2026-11-10) Create `/var/lib/pacman/db.lck` by hand, run an `Execute`, and confirm `Busy`, that the plan stays valid, and that the helper did not remove the lock (REQ-F-047).
- [x] (passed 2026-11-10) Request an HTTP mirror that answers 404 for a package file listed in the sync database. Confirm `SyncDatabaseStale` (REQ-F-028, rule 1).
- [x] (passed 2026-11-10) Call `Execute` from a client with a call timeout of at least one hour on a slow download and confirm the reply arrives (REQ-F-074).
- [x] (passed 2026-11-10) Let the helper stay idle. Confirm in the journal that it exits 60 seconds after the last plan or transaction (REQ-F-068).
- [x] (passed 2026-11-10) As an unprivileged user, call `PreparePlan` on the system bus and confirm the reply. As that user, confirm that the helper cannot own its name (REQ-C-006).
- [x] (passed 2026-11-10) Confirm that the journal holds raw libalpm text for a forced error and that the GUI or client receives only the code (REQ-F-052).
