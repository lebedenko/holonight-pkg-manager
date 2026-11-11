# holonight-packages

The Qt Quick GUI of HoloNight Packages. It shows the Installed, Updates and Explore pages and can **install** one
official-repository package (Explore details panel) or **remove** one (Installed details panel). It never changes
packages itself: every change is a request to the privileged `holonight-package-helper` on the system bus
(`apps/helper/README.md` has the helper's client contract). The design and the requirements are in
`docs/sdd/gui-package-transactions/` (`SPEC.md`, `DESIGN.md`, `TASKS.md`).

## How a change runs

1. The user activates Install or Remove. The GUI calls `PreparePlan(action, [name], {})` and shows the helper's preview
   in a confirmation dialog: the packages that will be installed or removed, the hooks that will run and when the
   confirmation expires.
2. A refusal (for example `PartialUpgradeRequired`: the system must be updated outside this app first) is shown at this
   step, before any authorization prompt, and has no Confirm button.
3. Confirm calls `Execute(plan_id)`. The helper asks polkit for authorization, re-resolves the plan and runs it. A modal
   progress dialog shows the phase and the current item. Cancel is available until the first `Hooks` phase; after
   that the window cannot be closed until the transaction ends.
4. The dialog ends in a result (done, failed, cancelled, outcome unknown, or helper unavailable) with a fixed message
   per code, and the Installed list and the Explore index are re-read. The selected Explore details update their Installed indicator too.

The transaction dialog receives keyboard focus when opened. Escape dismisses confirmation and result pages;
it is disabled while a transaction executes.

Where the code lives:

| Part | Location |
|------|----------|
| Port (`TransactionService`) and plain value types | `src/domain/include/holonight_packages_domain/transaction_service.h` |
| State machine (`TransactionFlow`) and `PackageListRefresh` port | `src/application/` |
| D-Bus adapter and wire decoding | `apps/packages/app/DBusTransactionService.*`, `HelperWire.*` |
| View-model, row models, message table | `apps/packages/app/TransactionController.*`, `TransactionRowModel.*`, `TransactionMessages.*` |
| Window close guard | `apps/packages/app/WindowCloseGuard.*` |
| Dialogs | `qml/transactions/` |

## Running it with the helper

Install the helper and its polkit policy and bus configuration (`apps/helper/README.md`), make sure a polkit agent is
running in the session, then start `holonight-packages`. Without the helper (or without a system bus) Install and
Remove show "The package helper is not available" and change nothing.

## Manual verification: package transactions

These steps need a real polkit agent, a real system bus and real root, so they run on a disposable Arch VM (not in CI).
The change counts as done only when all seven items have passed. Record each result with the date; a failed item is
fixed and re-run, and the failure and the fix are recorded in the same line (REQ-C-005).

1. Install a small official package from the Explore page. The polkit prompt appears, the progress dialog shows the
   phases, and the Installed list shows the package after the result. — passed 2026-11-11; result: initially failed
   (Installed list refreshed but Explore details stayed stale); fixed by binding the detail Installed indicator to
   refreshed package state; tester confirmed authorization, progress and the successful rerun; date: 2026-11-11
2. Remove a package from the Installed page. The polkit prompt appears, the Confirm button shows the package count, and
   the list no longer shows the package after the result. — passed 2026-11-11; result: tester confirmed
   confirmation count, authorization and refreshed list; date: 2026-11-11
3. Cancel during a slow download on a throttled mirror. The result is "Cancelled", `/var/lib/pacman/db.lck` is absent
   and the Installed list is unchanged. — passed 2026-11-11; result: tester cancelled the throttled `figlet`
   download; installed-package manifest matched the baseline and no database lock remained; date: 2026-11-11
4. Install a package that needs a newer version of an installed package. The refusal is `PartialUpgradeRequired`, the
   dialog offers only Close and nothing changes. — passed 2026-11-11; result: `spdlog` with installed
   `fmt` 11.2.0-1 was refused before authorization; only Close was offered; package manifest unchanged and no
   database lock remained; date: 2026-11-11
5. Deny the polkit prompt. The dialog returns to the preview with the note, and Confirm again succeeds with the same
   plan within five minutes. — passed 2026-11-11; result: tester denied authorization for `sl`, observed
   the note on the same preview and then authenticated successfully; only `sl` was added and no lock remained;
   date: 2026-11-11
6. Try to close the window after the first `Hooks` phase. The window stays open with the close-unavailable message,
   and the transaction finishes before the window closes. — passed 2026-11-11; result: tester attempted
   window close during the 30-second `cowsay` PreTransaction hook, observed the close-unavailable notice and
   uninterrupted completion; only `cowsay` was added and no lock remained; date: 2026-11-11
7. Start the GUI with the helper package removed or not activatable. Install and Remove show the helper-unavailable
   message with Close only, and no polkit prompt appears. — passed 2026-11-11; result: tester verified
   Install for `figlet` and Remove for `sl` while the helper unit was runtime-masked; neither prompted for
   authorization; package manifest unchanged and no lock remained; date: 2026-11-11

## Verification status (2026-11-11)

Final `task check` and `task check:integration` passed with the published AppStreamQt-capable image
pinned in `scripts/ci/image.txt` and unchanged pinned providers. Both runs report 918 CTest cases, zero
failures and five namespace-dependent skips, including both runtime styles. Quality gates and Release
installation/runtime checks passed. The initial image lacked AppStreamQt; publishing and pinning the
verified replacement resolved that failure. T-026 is complete.

The helper and transaction trees are unchanged from `main`; the helper interface XML SHA-256 matches `main`.

The disposable `hn-transactions-sdd` has independent storage, UUID, MAC address and UEFI state; the original
`hn-plasma` remains powered off and unchanged. Initial boot and console issues were resolved by resetting guest
identity and UEFI state, using software virtio graphics and replacing the failing custom greeter with SDDM.
Arch, a non-root Plasma administrator session, system D-Bus and the graphical polkit agent were verified.
The Release build with pinned providers is installed; desktop-entry launch and real helper activation passed.
Quiesced checkpoints were taken before scenarios and after the Explore detail fix.

All seven human checks and Escape dismissal/protection passed on 2026-11-11. The normal mirror and helper
activation were restored, and the test delay hook was retired. Detailed evidence stays under ignored `build/`
and in the disposable guest. T-027 is complete and the SDD is finalized.
