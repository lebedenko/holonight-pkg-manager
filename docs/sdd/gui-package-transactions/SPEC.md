# GUI package transactions — SPEC (HoloNight Packages)

Status: Finalized 2026-11-11. Automated acceptance and all seven required human VM checks passed. Requirements are written in EARS. The helper's contract is fixed by `docs/sdd/package-helper-transactions/SPEC.md` and `apps/helper/dbus/org.holonight.PackageHelper1.xml`; this SPEC does not change it. "Resolved decisions" records the accepted defaults and deviations from the stage-0 decisions.

Template labels: **[U]** Ubiquitous, **[E]** Event-driven, **[S]** State-driven, **[C]** Conditional/Optional, **[X]** Unwanted behaviour.

## Purpose

HoloNight Packages is transactionally inert today: the Installed, Updates and Explore pages show packages and cannot change them. The privileged helper `holonight-package-helper` can install and remove official-repository packages, but nothing in the GUI calls it.

This feature wires the GUI to the helper for one action at a time:

- **Install** from the Explore page, for one package.
- **Remove** from the Installed page, for one package.

Every change follows the same path: the GUI asks the helper for a plan, shows the preview, and runs the previewed plan only after the user confirms. The helper asks polkit for authorization. The GUI shows progress and a final result, and then re-reads the Installed list from libalpm.

## Scope

In scope:

- Install of one package on the Explore page and Remove of one package on the Installed page.
- The preview and confirmation dialog, built from the helper's `PreparePlan` reply.
- A modal progress dialog that follows `Execute`, with the helper's `Progress` signal.
- The result states: success, failure with a message per code, cancelled, and unknown outcome.
- Refusals at the preview step, with a fixed message per code.
- A `TransactionService` port in `src/domain` with plain types, and a QDBus adapter in `apps/packages`.
- A fake `TransactionService` for tests.
- Timeouts, the one-action-at-a-time rule and the close rules.
- The manual VM checklist that closes the cycle.

## Non-goals

- The Updates page and any system upgrade (`-Syu`, or any upgrade of the whole system), including the remedy for `PartialUpgradeRequired`.
- Multi-select, batch installs and batch removes.
- AUR packages, local files and URLs.
- Transaction history.
- Reattaching to a transaction that is running when the GUI starts. The GUI does not read a running transaction's progress.
- Any change to `apps/helper`, `src/transaction` or the helper's D-Bus interface.
- A streaming log view. The helper never returns free text.
- Refreshing the sync databases.
- Any package operation outside the helper: the GUI never invokes libalpm or pacman to change packages itself.

## Glossary

- **GUI**: the `holonight-packages` executable, its QML and its view-models.
- **Helper**: the `holonight-package-helper` system D-Bus service, activated by the bus, which performs the transaction.
- **Port**: `TransactionService`, the abstract interface in `src/domain` that the view-model uses. It has no Qt D-Bus or polkit types.
- **Adapter**: the QDBus implementation of the port in `apps/packages`. It is the only code that talks to the helper.
- **Fake service**: the test implementation of the port. It scripts any reply, progress sequence and delay.
- **Transaction view-model**: the GUI-side state machine that owns one install or remove request from preparation to result. It holds no D-Bus types. Its class name is set in the design.
- **Plan**: the helper-owned record created by `PreparePlan`. Its ID is an opaque string. The GUI holds it in memory only.
- **Preview**: the reply of `PreparePlan`: the explicit targets, dependency installs, removals, orphans, hooks and expiry.
- **Confirmation dialog**: the dialog that shows the preview, or a refusal, and offers Confirm, Cancel, Try again or Close.
- **Progress dialog**: the modal dialog that shows the phase, the current item and a bar while `Execute` runs.
- **Authorizing**: the period from the `Execute` call until the first `Progress` signal for that plan. It includes the polkit prompt.
- **Point of no return**: the first `Hooks`, `Installing` or `Removing` progress phase of a transaction (and `Done`, which follows them). A progress event can reach the GUI after the helper's commit point has already passed, so the GUI treats a `PastPointOfNoReturn` reply to `Cancel` as a normal event. The helper's commit point (helper SPEC, REQ-F-057) is no later than the first `Hooks` phase, so after this point `Cancel` is refused with `PastPointOfNoReturn` (helper SPEC, REQ-F-058).
- **Outcome**: the result of `Execute`: `Succeeded`, `Failed` (with a code), `Cancelled`, or **Unknown** when the GUI got no reply (the adapter's own value, not a helper value).
- **Result code**: one of the helper's 24 error codes (the error taxonomy in the helper SPEC), or the GUI-side `HelperUnavailable`.
- **Warning**: an enumerated token in the `Execute` result. Only `HookFailed` exists.
- **Changed list**: the `Execute` result's list of (name, action, old version, new version) entries.
- **Work slot (helper)**: the helper's single transaction position. `Busy` means it is not free.

---

## Requirements

### 1. Preparation and preview

### REQ-F-001: Install preparation [E]
**Statement:** When the user activates Install for a package on the Explore page, the GUI shall call `PreparePlan` on the helper with action `install`, names containing exactly that one package name, and an empty options map, and shall enter the `preparing` state.
**Acceptance criteria:**
- A view-model test with the fake service records one `PreparePlan` call whose arguments are `install`, a one-element name list equal to the row's name, and an empty options map.
- The view-model reports `preparing` immediately after the request, before the fake service answers.

### REQ-F-002: Remove preparation [E]
**Statement:** When the user activates Remove for a package on the Installed page, the GUI shall call `PreparePlan` with action `remove`, names containing exactly that one package name, and an empty options map, and shall enter the `preparing` state.
**Acceptance criteria:**
- A view-model test records one `PreparePlan` call with `remove` and a one-element name list equal to the row's name.
- No install call is recorded for the Remove activation.

### REQ-F-003: Preview content [E]
**Statement:** When `PreparePlan` returns a preview, the GUI shall show the confirmation dialog with every explicit target, every dependency install, every removal and orphan, the old and new version of each, the repository of each install, the hooks listed in the preview with their conditional marker, and the plan's expiry time.
**Acceptance criteria:**
- With a fake preview of one target `app` and one dependency install `lib`, the dialog model holds exactly two change rows, with `app` marked as the target and `lib` as a dependency, and each row shows its repository.
- With a fake preview containing one conditional hook, the hook row carries the conditional marker, and a preview with no hooks shows no hook row.
- The expiry time from the preview is shown; the dialog model holds the value the fake service returned, unchanged.

### REQ-F-004: Remove preview lists extra removals and counts them [S]
**Statement:** While a remove preview lists packages other than the requested target (orphans or extra removals), the GUI shall list each of them by name in the confirmation dialog, and shall label the Confirm button `Remove N packages`, where N is the number of packages in the removal set.
**Acceptance criteria:**
- With a fake remove preview of target `app` and orphans `lib` and `util`, the Confirm label reads `Remove 3 packages` and the list shows `lib` and `util` as separate rows.
- With a fake remove preview of the target alone, the Confirm label reads `Remove 1 package`, and no extra row appears.

### REQ-F-005: Confirm executes only the plan ID [E]
**Statement:** When the user activates Confirm in the confirmation dialog, the GUI shall call `Execute` with only the plan ID returned by `PreparePlan`, and shall enter the `authorizing` state.
**Acceptance criteria:**
- The fake service records one `Execute` call whose single argument equals the plan ID from the preview.
- The fake service records no package names or action with the `Execute` call (the port's execute method has no such parameter).

### REQ-F-006: Dismissing a preview releases the plan [E]
**Statement:** When the user dismisses a confirmation dialog that shows a preview without executing it, the GUI shall call `Cancel` with the plan ID and shall return to the `idle` state without calling `Execute`.
**Acceptance criteria:**
- The fake service records one `Cancel` call with the preview's plan ID and no `Execute` call.
- A second `Execute` attempt for the same plan ID cannot happen because the view-model holds no reference to the plan after dismissal.

### 2. Execution, progress and results

### REQ-F-007: View-model states [U]
**Statement:** The GUI shall model one request with exactly these states: `idle`, `preparing`, `previewed`, `refused`, `authorizing`, `executing`, `succeeded`, `failed`, `cancelled`, `unknown` and `helperUnavailable`, and `executing` shall carry a flag that says whether cancelling is currently allowed.
**Acceptance criteria:**
- A state-machine test enumerates the states and fails if the view-model exposes any other value.
- Each state transition in REQ-F-001 to REQ-F-006 and REQ-F-012 to REQ-F-019 is covered by at least one test that asserts the resulting state.

### REQ-F-008: Progress dialog content [E]
**Statement:** When `Execute` is called, the GUI shall open the progress dialog, which shows the current phase label and the current item name from the latest `Progress` signal for the plan.
**Acceptance criteria:**
- With the fake service emitting `Downloading` with current 3, total 10 and item `foo`, the dialog model shows phase `Downloading` and item `foo`.
- With the fake service emitting `Installing` with item `bar`, the phase label changes to the installing label and the item to `bar`.

### REQ-F-009: Determinate and indeterminate progress [S]
**Statement:** While the latest progress event has a total greater than zero, the progress dialog shall show a determinate bar at `current / total`; while the total is zero, it shall show an indeterminate bar.
**Acceptance criteria:**
- With `Downloading`, current 3, total 10, the bar value is 0.3 and the bar is determinate.
- With `Resolving`, current 0, total 0, the bar is indeterminate.

### REQ-F-010: Progress events are filtered and applied in order [E]
**Statement:** When a `Progress` event arrives, the GUI shall apply it to the transaction view-model only if its plan ID equals the current plan's ID, and shall keep the events of one phase in arrival order.
**Acceptance criteria:**
- With the fake service emitting an event with another plan ID, the view-model's phase, current, total and item do not change.
- With the fake service emitting `Downloading` 4/10 then 2/10, the displayed current stays at 4, because `current` never decreases within one occurrence of a phase (helper SPEC REQ-F-049; `Hooks` can occur twice per transaction), and the test asserts the view-model ignores an event only when its phase equals that of the previously applied event and its `current` is smaller.

### REQ-F-011: No log view [U]
**Statement:** The progress dialog shall show only the phase label, the current item name, the bar and the cancel control, and shall contain no text area, log view or list of past events.
**Acceptance criteria:**
- A runtime test reads the progress dialog's child items through the view-model's fixture and finds no item of a text-area or list kind.

### REQ-F-012: Cancel is enabled only before the point of no return [S]
**Statement:** While the transaction is `authorizing`, or `executing` with a last progress phase of `Resolving`, `Downloading` or `Verifying`, the GUI shall enable the Cancel control. From the first `Hooks`, `Installing`, `Removing` or `Done` phase onwards, the GUI shall disable it.
**Acceptance criteria:**
- The view-model's cancel flag is true in `authorizing`, and in `executing` after each of `Resolving`, `Downloading` and `Verifying`.
- The cancel flag is false after a `Hooks` event, after an `Installing` event, after a `Removing` event and after `Done`, and the Cancel control is disabled in the runtime test driven by that state.
- The cancel flag latches to false: after any of those phases, a later `Resolving`, `Downloading` or `Verifying` event (for example the second `Hooks` occurrence's neighbours or a reordered event) does not set it true again.
- (The `Hooks` rule replaces the stage-0 wording; see OQ-04, resolved.)

### REQ-F-013: Cancel calls the helper [E]
**Statement:** When the user activates Cancel while the cancel flag is true, the GUI shall call `Cancel` with the plan ID, and shall show the `Cancelling` label until the `Execute` outcome arrives.
**Acceptance criteria:**
- The fake service records one `Cancel` call with the plan ID.
- While the fake `Execute` is still pending after the `Cancel` call, the view-model shows `Cancelling` and the Cancel control is disabled.

### REQ-F-014: Success result [E]
**Statement:** When `Execute` returns outcome `Succeeded`, the GUI shall enter the `succeeded` state and show the changed list, one row per entry with name, action and new version.
**Acceptance criteria:**
- With a fake `Succeeded` result containing two changed entries, the result model holds exactly those two rows in order.
- The `succeeded` state shows no Try again control.

### REQ-F-015: Failure result [E]
**Statement:** When `Execute` returns outcome `Failed` with a result code, the GUI shall enter the `failed` state, show the message for that code from the message table (REQ-F-048), and show the changed list when it is not empty.
**Acceptance criteria:**
- With a fake `Failed` result with code `TransactionFailed` and a changed list of one entry, the failure message is the table's `TransactionFailed` entry and the result model holds that one row.
- With a fake `Failed` result with code `DiskSpace` and an empty changed list, no changed row is shown.

### REQ-F-016: Cancelled result [E]
**Statement:** When `Execute` returns outcome `Cancelled`, the GUI shall enter the `cancelled` state and show the message that nothing was changed.
**Acceptance criteria:**
- With a fake `Cancelled` outcome, the view-model enters `cancelled` and the message equals the table's `Cancelled` entry.
- The `cancelled` state shows no changed row, even if the fake result carries one.

### REQ-F-017: Hook-failure warning [E]
**Statement:** When `Execute` returns outcome `Succeeded` with the warning `HookFailed`, the GUI shall show the success state together with the table's hook-warning note.
**Acceptance criteria:**
- With a fake `Succeeded` result with warning `HookFailed`, the success state carries the hook-warning note and the changed rows are still shown.
- With a fake `Succeeded` result with no warnings, no hook-warning note is shown.

### REQ-F-018: Unknown outcome after a lost reply [E]
**Statement:** When the adapter reports that the `Execute` call got no reply, the GUI shall enter the `unknown` state, show the table's unknown message, and shall not call `Execute` or `PreparePlan` again for that request.
**Acceptance criteria:**
- With the fake service failing `Execute` with the no-reply error, the view-model enters `unknown`, the fake records exactly one `Execute` call and no second `PreparePlan`.
- The unknown message tells the user to check the Installed list, and the view-model has no retry control in this state.

### REQ-F-019: Installed list is re-read after every outcome [E]
**Statement:** When the view-model reaches `succeeded`, `failed`, `cancelled` or `unknown`, the GUI shall ask the Installed list and the Explore index (which shows the "Installed" badge from the same package state) to re-read their contents from libalpm exactly once for that outcome, and shall repeat a request that the Installed model dropped because a load was already in progress.
**Acceptance criteria:**
- For each of the four outcomes, a counter on the list refresh port increments by exactly one, and no increment happens for a refusal at preparation or for `NotAuthorized`.
- With the Installed model already loading when the outcome arrives, the refresh is repeated once the load ends, so the list is not left stale; the Explore index is reloaded once per outcome.
- The refresh is requested after the result state is entered, so a list that shows the old content until the refresh completes still shows the correct result message.

### REQ-F-020: Execute is called at most once per plan [U]
**Statement:** The GUI shall call `Execute` at most once for each plan ID, except that after a `NotAuthorized` reply it may call `Execute` again with the same plan ID when the user activates Confirm again (REQ-F-026).
**Acceptance criteria:**
- Across every outcome in REQ-F-014 to REQ-F-018, the fake service records no second `Execute` for the same plan ID.
- After `NotAuthorized`, the second `Execute` call uses the same plan ID and is recorded only after the user activates Confirm.

### 3. Refusals, retries and short errors

### REQ-F-021: Refusals at preparation show no Confirm [E]
**Statement:** When `PreparePlan` returns a refusal code, the GUI shall show the confirmation dialog in the `refused` state with the table message for that code and without a Confirm button, and shall not call `Execute`.
**Acceptance criteria:**
- For each of the codes `InvalidArgument`, `PackageNotFound`, `PackageIgnored`, `ProtectedPackage`, `BreaksDependents`, `NeedsDecision`, `PartialUpgradeRequired`, `InsecureSigLevel`, `DatabaseUnavailable`, `ConfigurationError`, `NothingToDo` and `InternalError` (the full set `PreparePlan` can return, plus `Busy`, which REQ-F-023 covers), the fake service records zero `Execute` calls and the dialog model has no Confirm action.
- The refusal message for `PackageNotFound` contains the package names from the reply.

### REQ-F-022: PartialUpgradeRequired says the system must be updated outside the app [C]
**Statement:** Where the refusal code is `PartialUpgradeRequired`, the GUI shall show the table message stating that the system must be updated first, outside this app, and shall offer no action other than Close.
**Acceptance criteria:**
- With a fake `PartialUpgradeRequired` refusal, the dialog's actions are exactly Close, and the message equals the table entry, which names the installed packages listed in the reply.
- The dialog offers no control that starts a system upgrade, adds an override or changes the request.

### REQ-F-023: Busy, PlanNotFound and PlanExpired offer Try again [C]
**Statement:** Where the code of a refusal at preparation or of a failed `Execute` is `Busy`, `PlanNotFound` or `PlanExpired`, the GUI shall offer a Try again action that calls `PreparePlan` again with the same action and names and returns to the `preparing` state.
**Acceptance criteria:**
- (`PlanNotFound` and `PlanExpired` can only come from `Execute`; the rule applies to whichever call returns the code.)
- With a fake `PreparePlan` refusal `Busy`, the dialog offers Try again and Close; activating Try again records a second `PreparePlan` with the same action and names.
- With a fake `Execute` failure `PlanNotFound`, the dialog offers Try again; activating it records one `PreparePlan` and no `Execute`.
- With a fake `Execute` failure `PlanExpired`, the dialog offers Try again and Close; activating Try again records one `PreparePlan` and no `Execute`, and the new preview carries a new plan ID.

### REQ-F-024: All other codes offer Close only [U]
**Statement:** The GUI shall offer only a Close action for every refusal and failure code other than `Busy`, `PlanNotFound` and `PlanExpired`, and for `Cancelled`, `unknown` and `helperUnavailable`.
**Acceptance criteria:**
- For each code in the table, except `Busy`, `PlanNotFound` and `PlanExpired`, the dialog's action list is exactly Close.
- The same holds for the `Cancelled`, `unknown` and `helperUnavailable` states.

### REQ-F-025: Denied or cancelled authorization returns to the preview [E]
**Statement:** When `Execute` returns `NotAuthorized` (polkit denied, challenged or failed, or the user cancelled the prompt), the GUI shall return to the `previewed` state with the note that authorization was not granted and the Confirm control enabled, and shall not enter the `failed` state.
**Acceptance criteria:**
- With a fake `NotAuthorized` reply, the view-model is in `previewed`, the note text equals the table's `NotAuthorized` entry, and the Confirm control is enabled.
- The `failed` state is not entered, and the outcome count recorded for the list refresh (REQ-F-019) is unchanged.

### REQ-F-026: Confirm again reuses the same plan [E]
**Statement:** When the user activates Confirm again after `NotAuthorized`, the GUI shall call `Execute` with the same plan ID that was returned by `PreparePlan`, while the plan is still valid on the helper.
**Acceptance criteria:**
- After one `NotAuthorized` and one Confirm activation, the fake service records two `Execute` calls, both with the same plan ID.
- The view-model does not call `PreparePlan` in this path.

### REQ-F-027: Expired confirmation [X]
**Statement:** If `Execute` returns `PlanExpired` (the plan is older than 300 seconds), then the GUI shall show the table's `PlanExpired` message with Try again and Close (REQ-F-023).
**Acceptance criteria:**
- With a fake `PlanExpired` reply, the dialog's actions are exactly Try again and Close, and the message equals the table entry.
- The expired message is not shown for any other code.

### REQ-F-028: Helper unavailable [X]
**Statement:** If the adapter reports that the helper's name has no owner or cannot be activated, then the GUI shall enter `helperUnavailable`, show the table's helper-unavailable message and call no further helper method for that request.
**Acceptance criteria:**
- With the fake service reporting unavailable on `PreparePlan`, the view-model is in `helperUnavailable`, the fake records no `Execute`, and the message equals the table entry.
- With the fake service reporting unavailable on `Execute`, the view-model is in `helperUnavailable` and the fake records no further call.

### 4. Concurrency and closing

### REQ-F-029: Install and Remove controls are disabled while busy [S]
**Statement:** While the view-model is in `preparing`, `previewed`, `authorizing` or `executing`, the GUI shall disable every Install and Remove control in the Explore and Installed views.
**Acceptance criteria:**
- For each of the four states, the view-model's allow-action flag is false, and a runtime test driven through the view-model reads every Install and Remove control in both views as disabled.
- In `idle`, `succeeded`, `failed`, `cancelled`, `refused`, `unknown` and `helperUnavailable`, the same controls are enabled.

### REQ-F-030: One request in flight [U]
**Statement:** The GUI shall hold at most one transaction request at a time, and shall ignore an Install or Remove request that arrives while a request is not `idle` or in a terminal result state.
**Acceptance criteria:**
- With the view-model in `preparing`, a second Install request leaves the `PreparePlan` call count at one.
- With the view-model in `executing`, a Remove request records no `PreparePlan` call.

### REQ-F-031: No second dialog [U]
**Statement:** The GUI shall have at most one confirmation dialog or progress dialog open at any time.
**Acceptance criteria:**
- The view-model's open-dialog counter never exceeds one across the full sequence in REQ-F-001 to REQ-F-019, as asserted by the test.

### REQ-F-032: Progress dialog cannot be dismissed while the transaction runs [S]
**Statement:** While the transaction is `authorizing` or `executing`, the progress dialog shall not be closable by its close request, Escape or any other dismissal, and shall stay open until a terminal result state is entered.
**Acceptance criteria:**
- The dialog's closable property is false in `authorizing` and `executing`, and true in each terminal state.
- A runtime test that drives the view-model to `executing` finds the dialog still open after the fake service emits further progress events.

### REQ-F-033: Closing before the point of no return cancels, then exits [E]
**Statement:** When the window close request arrives while the view-model is `previewed`, `authorizing`, or `executing` with the cancel flag true, the GUI shall call `Cancel` with the plan ID, and shall exit after the `Cancel` reply has been received or the `Cancel` call has failed.
**Acceptance criteria:**
- With the view-model in `executing` with the cancel flag true, a close request records one `Cancel` call with the plan ID and the application's quit request is raised only after the fake `Cancel` reply.
- With the view-model in `previewed`, a close request records one `Cancel` call and then quits.

### REQ-F-034: Closing while preparing exits without waiting [E]
**Statement:** When the window close request arrives while the view-model is `preparing`, the GUI shall exit without calling `Cancel`, because the helper discards a plan whose caller disconnects (helper SPEC, REQ-F-012).
**Acceptance criteria:**
- With the fake service holding `PreparePlan` unanswered, a close request quits the application and the fake records no `Cancel` and no `Execute` call.

### REQ-F-035: Closing after the point of no return is ignored [X]
**Statement:** If the window close request arrives while the view-model is in `executing` with the cancel flag false, then the GUI shall ignore the close request, keep the window open and show the message that closing is unavailable until the transaction finishes.
**Acceptance criteria:**
- With the view-model in `executing` after a `Hooks` event, a close request leaves the window open, records no `Cancel` call, and shows the table's close-unavailable message.
- The window closes after the view-model enters a terminal state and the user closes it again.

### REQ-F-036: A late PastPointOfNoReturn from Cancel is handled the same way [X]
**Statement:** If `Cancel` returns `PastPointOfNoReturn`, then the GUI shall keep the transaction running, show the table's close-unavailable message, and treat any pending close request as ignored.
**Acceptance criteria:**
- With the fake `Cancel` reply `PastPointOfNoReturn`, the view-model stays in `executing`, shows the close-unavailable message and the progress dialog stays open.
- A close request sent after that reply is ignored as in REQ-F-035.
- With the fake `Cancel` reply `PlanNotFound` (the helper may answer this too), the view-model takes no action and keeps waiting for the `Execute` outcome; a pending window close then proceeds.

### REQ-F-037: Helper busy at GUI start [X]
**Statement:** If the first `PreparePlan` after the GUI starts returns `Busy` because another operation (including one left by a previous GUI process that was force-quit) holds the helper, then the GUI shall show the message `Another package operation is running` with Try again and Close.
**Acceptance criteria:**
- With the fake service returning `Busy` on the first call, the dialog message equals the table's `Busy` entry, which reads `Another package operation is running`, and the actions are Try again and Close.
- Activating Try again calls `PreparePlan` again with the same action and names (REQ-F-023).

### 5. Port, adapter and transport

### REQ-F-038: Port with plain types in src/domain [U]
**Statement:** The `TransactionService` port shall live in `src/domain`, shall expose the plan, preview, result code, outcome, progress event and warning as plain value types, and shall contain no Qt D-Bus, polkit or libalpm type.
**Acceptance criteria:**
- The port header and its value types compile in a target that does not link QtDBus.
- `scripts/check-layering.py` rules H-001 (`QDBus`, `<QtDBus`, `polkit`) and C-001 (`<alpm.h>`, `alpm_*`, `dbpath`, `databasePath`) cover `src/domain` and pass. (A literal search for the words "alpm" or "D-Bus" is not the gate: existing comments legitimately mention libalpm.)
- New domain code and comments avoid the words `polkit`, `QDBus` and `D-Bus`.

### REQ-F-039: Application layer stays free of transport terms [U]
**Statement:** The application layer (`src/application`) shall use the port only, and shall contain no D-Bus, polkit or libalpm term.
**Acceptance criteria:**
- `scripts/check-layering.py` rules H-001 and C-001 cover `src/application` and pass, and new application code and comments avoid the words `polkit`, `QDBus` and `D-Bus`.
- The view-model's tests link only to the port and the fake service.

### REQ-F-040: The adapter lives in apps/packages [U]
**Statement:** The QDBus adapter that implements the port shall live in `apps/packages`, and it shall be the only code in the GUI that calls the helper.
**Acceptance criteria:**
- A source search finds `org.holonight.PackageHelper1` and the `Transactions` interface name only in the adapter and in tests.
- The adapter is constructed in the GUI's composition root, and the view-model receives it through the port.

### REQ-F-041: PreparePlan call timeout is 30 seconds [E]
**Statement:** When the adapter calls `PreparePlan`, it shall use a call timeout of 30 000 milliseconds.
**Acceptance criteria:**
- QtDBus cannot read back the timeout of a pending call, so the adapter stores its `Timeouts` and exposes `timeouts()`, and the call sites read only that member. A unit test asserts `timeouts().prepare_ms == 30 000` from the production constructor default, and a short-timeout bus test (REQ-F-045) proves the member reaches the call.

### REQ-F-042: Execute call timeout is one hour [E]
**Statement:** When the adapter calls `Execute`, it shall use a call timeout of 3 600 000 milliseconds.
**Acceptance criteria:**
- `timeouts().execute_ms == 3 600 000` from the production constructor default (read through the adapter's `timeouts()` accessor, as in REQ-F-041).
- The `Cancel` call uses the default bus timeout, and the test asserts it is not set to the one-hour value.

### REQ-F-043: Calls are asynchronous [U]
**Statement:** The adapter shall make every helper call asynchronously, so that the GUI's event loop keeps running while a call waits for a reply.
**Acceptance criteria:**
- With a fake helper object that holds the `PreparePlan` reply for 2 seconds, a 100 ms repeating timer in the test's event loop fires at least 10 times before the reply arrives.
- No call to the adapter blocks the thread that owns the GUI.

### REQ-F-044: Progress match rule on the calling connection [E]
**Statement:** When the adapter prepares a plan, it shall subscribe with a match rule for the helper's well-known name, the interface `org.holonight.PackageHelper1.Transactions` and the member `Progress`, on the same bus connection that makes the `PreparePlan` and `Execute` calls, and shall pass only signals for its current plan ID to the view-model.
**Acceptance criteria:**
- A private-bus test with a fake helper and a second, unrelated connection emitting `Progress` for the same plan ID receives zero events on the adapter, while the fake helper's own emissions reach the adapter.
- QtDBus cannot read back an installed match rule, so the adapter's `progressMatchRule()` returns the documented rule string, which contains the interface name, the member `Progress` and the helper's well-known name; the negative test above proves the sender restriction is in force.

### REQ-F-045: No reply from Execute is Unknown [X]
**Statement:** If the `Execute` call times out or the bus reports no reply, then the adapter shall report the outcome `Unknown` to the view-model, and shall not report `Failed`, shall not send `Execute` again and shall not send `PreparePlan`.
**Acceptance criteria:**
- With a fake helper that holds `Execute` past an injected short timeout, the adapter reports `Unknown`, and the fake helper records one `Execute` call.
- The timeout value used in this test is injected by the constructor; the production default stays 3 600 000 ms (REQ-F-042).

### REQ-F-046: Helper not owned or not activatable [X]
**Statement:** If the bus reports that the helper's well-known name has no owner or cannot be activated, then the adapter shall report the error `HelperUnavailable` to the view-model.
**Acceptance criteria:**
- On a private bus with no helper registered for the name, a `PreparePlan` call makes the adapter report `HelperUnavailable`, and the view-model enters `helperUnavailable`.
- The adapter does not report `HelperUnavailable` for a `NotAuthorized` or any other domain code.

### REQ-F-047: Unknown wire codes do not reach the user [X]
**Statement:** If a reply carries a result code that is not in the helper's error taxonomy, then the adapter shall map it to `InternalError`, and shall not pass the raw string to the view-model.
**Acceptance criteria:**
- With a fake helper reply carrying the code `SomethingElse`, the adapter reports `InternalError` and the view-model's message equals the table's `InternalError` entry.
- Each of the 24 taxonomy codes maps to itself with no change.

### REQ-F-050: Rejected calls and undecodable replies [X]
**Statement:** If the bus answers a helper call with an error reply (for example `AccessDenied` or `InvalidArgs`), then the adapter shall report it as `Rejected`, which the GUI shows as `InternalError` ("nothing was changed"). If a reply cannot be decoded, the adapter shall treat it as no reply, which for `Execute` is the `Unknown` outcome, and for `PreparePlan` and `Cancel` is `InternalError`.
**Acceptance criteria:**
- With a fake helper answering `PreparePlan` with a D-Bus error reply, the view-model shows the table's `InternalError` entry and the fake records no `Execute`.
- With a fake helper answering `Execute` with a reply whose signature does not match the XML, the view-model enters `unknown` and no second `Execute` or `PreparePlan` is sent.

### 6. Messages

### REQ-F-048: Fixed message table [U]
**Statement:** The GUI shall display every message from one fixed, translatable table keyed by result code or GUI state, and shall have an entry for each of the 24 taxonomy codes and for the states `Succeeded`, `Cancelled`, `Unknown`, `HelperUnavailable`, the hook-warning note, the `NotAuthorized` note, the close-unavailable message and the `Cancelling` label.
**Acceptance criteria:**
- A unit test asserts that the table has a non-empty entry for each of the 24 taxonomy codes and each listed state or note.
- A test fails if any dialog text is built from a string in a helper reply other than a package name or version.

### REQ-F-049: Names are rendered as plain text [U]
**Statement:** The GUI shall insert package names and versions from the helper's reply into messages and rows as plain text, never as rich text or markup.
**Acceptance criteria:**
- With a fake reply whose package name contains `<b>x</b>`, the rendered message and row contain the literal angle brackets and no bold text.
- The QML for the message and rows uses the plain text format and no rich-text format.

### 7. Non-functional requirements

### REQ-NF-001: State machine testable without D-Bus [U]
**Statement:** The transaction view-model and its state machine shall be testable without a bus, a helper or a Qt event loop timing dependency, using only the fake service.
**Acceptance criteria:**
- The state-machine test target builds without QtDBus, and its tests pass with the bus absent.
- The tests wait on signals with a bounded spy and do not use `sleep` or fixed delays to reach a state.

### REQ-NF-002: No transaction state on disk [U]
**Statement:** The GUI shall hold plan IDs, previews and transaction results only in memory and shall write none of them to disk.
**Acceptance criteria:**
- After a full install and a full remove test with the fake service, a manifest of the GUI's isolated configuration, data and cache directories shows no new or changed file from this feature.

### REQ-NF-003: Both styles render the dialogs [U]
**Statement:** The confirmation and progress dialogs shall load and render in both the Holonight and the Fusion styles, with no QML warnings.
**Acceptance criteria:**
- The runtime acceptance module opens each dialog state in the default style and with `QT_QUICK_CONTROLS_STYLE=Fusion`, and both runs complete with no QML warning.
- The policy fixtures and the existing QML tests pass in both styles.

### REQ-NF-004: Tests drive the GUI through the view-model only [U]
**Statement:** The QML and runtime tests for this feature shall change state through the view-model and the fake service, and shall not simulate pointer or focus input.
**Acceptance criteria:**
- A source search of the new test files finds no `QTest::mouseClick`, `mouseClick`, `forceActiveFocus` or pointer-event injection.
- Each runtime test names the view-model call that drives it.

### REQ-NF-005: Adapter tests use a private bus [U]
**Statement:** The adapter tests shall start their own `dbus-daemon` on a private session-type address and shall never connect to the system bus.
**Acceptance criteria:**
- A source search of the adapter tests finds no `systemBus` and no use of the system bus address.
- The test's bus address is a temporary socket path created by the test.

### REQ-NF-006: Missing dbus-daemon is reported, not passed [X]
**Statement:** If the adapter tests cannot start `dbus-daemon`, then they shall fail or report the CTest skip status, and shall not pass silently.
**Acceptance criteria:**
- Every test in the bus test binary needs the bus (bus-free adapter tests live in the general test binary), so with `dbus-daemon` removed from the path the bus binary ends with a failure or a CTest skip status and reports no passed test. A `WILL_FAIL` CTest run with an empty PATH enforces this.

### REQ-NF-007: task check stays green [U]
**Statement:** The feature shall keep `task check` green, including the build, tests, format check, lint and the policy checks.
**Acceptance criteria:**
- `task check` completes with exit status 0 on the feature branch in the containerized acceptance.

### REQ-NF-008: Existing pages behave as before without an action [U]
**Statement:** The Installed, Updates and Explore pages shall keep their present behaviour when the user takes no transaction action.
**Acceptance criteria:**
- The existing QML tests for these pages pass unchanged in both styles.
- No new network or bus call is made on page load by this feature (a fake-service count is zero after opening each page).

### 8. Constraints

### REQ-C-001: The GUI does not run transactions itself [U]
**Statement:** The GUI shall run no package transaction itself. It shall request each transaction from the helper, and the repository rules that say the GUI never runs package transactions shall be amended in the same change to say that the GUI only requests transactions from the helper.
**Acceptance criteria:**
- `scripts/check-layering.py` rule C-005 (no `alpm_trans_*` and no `pacman -S/-U/-R` in `src`, `apps`, `qml`, `tests`) and rule C-001 pass. (A literal search for "pacman" in `apps/packages` is not the gate: the composition root legitimately uses `kDefaultPacmanRoot` and `Alpm*Source`.)
- `AGENTS.md`, `CLAUDE.md` (local, gitignored), `README.md` (the "transactionally inert" line) and `CHANGELOG.md` are amended: none states that the GUI is transactionally inert or that production code in the GUI never runs package transactions, and they state that the GUI only requests transactions from the helper. The single carve-out line required by `check_carve_out` in `scripts/check-layering.py` is kept verbatim, and `task layering-check` passes.

### REQ-C-002: Helper and transaction core unchanged [U]
**Statement:** This feature shall make no change to `apps/helper`, `src/transaction` or the helper's D-Bus interface XML.
**Acceptance criteria:**
- A diff of the feature against its base shows no change under `apps/helper/` or `src/transaction/`, and the interface XML is byte-identical.

### REQ-C-003: Scope limits of the GUI actions [U]
**Statement:** The GUI shall offer Install only on the Explore page and Remove only on the Installed page, one package at a time, and shall offer no transaction control on the Updates page or any multi-select control.
**Acceptance criteria:**
- A runtime test on the Updates page finds no enabled Install or Remove control.
- A runtime test on the Explore page finds no Remove control, and on the Installed page finds no Install control.

### REQ-C-004: Wire contract follows the helper's XML [U]
**Statement:** The adapter shall send only the arguments the helper's interface declares (an action and a list of names with an empty options map for `PreparePlan`, and one plan ID for `Execute` and `Cancel`), and shall decode replies using the signatures in the helper's interface XML.
**Acceptance criteria:**
- A test fake built from the helper's interface XML rejects any extra argument, and the adapter's calls pass.
- A decode test feeds a reply built to the XML's `Execute` signature and the adapter produces the expected outcome, changed list and warnings.

### REQ-C-005: Manual VM checklist closes the cycle [U]
**Statement:** The feature shall not count as done until a person has run the following checklist on a disposable Arch VM with a real polkit agent, the real system bus and real root, and has recorded each result, with the date, in a section titled `Manual verification: package transactions` in `apps/packages/README.md`.
**Acceptance criteria:**
- The README section lists each of the seven items below, each with a pass or fail result and a date, and the cycle is closed only when all seven pass. A failed item is fixed and re-run, and the failure and fix are recorded in the same section.
- Item 1: Install a small official package from the Explore page. The polkit prompt appears, the progress dialog shows the phases, and the Installed list shows the package after the result.
- Item 2: Remove a package from the Installed page. The polkit prompt appears, the Confirm label shows the count, and the list no longer shows the package after the result.
- Item 3: Cancel during a slow download on a throttled mirror. The result is `Cancelled`, `/var/lib/pacman/db.lck` is absent, and the Installed list is unchanged.
- Item 4: Install a package that needs a newer version of an installed package. The refusal is `PartialUpgradeRequired`, the dialog offers only Close, and nothing changes.
- Item 5: Deny the polkit prompt. The dialog returns to the preview with the note, and Confirm again succeeds with the same plan within five minutes.
- Item 6: Try to close the window during the point of no return (after the first `Hooks` phase). The window stays open with the close-unavailable message, and the transaction finishes before the window closes.
- Item 7: Start the GUI with the helper package removed or not activatable. Install and Remove show the helper-unavailable message with Close only, and no polkit prompt appears.

---

## Message table

The table is the only source of text for refusals, failures and states (REQ-F-048). The wording below states the intent; the final strings are fixed in the translation table. Package names come from the reply and are inserted as plain text (REQ-F-049). Actions are the ones REQ-F-021 to REQ-F-028 allow.

| Key | Intent of the message | Actions |
|-----|----------------------|---------|
| `InvalidArgument` | The request was rejected as invalid. Nothing was changed. | Close |
| `PackageNotFound` (install) | A package was not found in the official repositories: {names}. | Close |
| `PackageNotFound` (remove) | {names} is not installed. Nothing was changed. | Close |
| `PackageIgnored` | {names} is excluded by the system pacman configuration. Nothing was changed. | Close |
| `ProtectedPackage` | {names} is a protected system package and cannot be changed here. | Close |
| `BreaksDependents` | Removing this would break installed packages: {names}. Nothing was changed. | Close |
| `NeedsDecision` | The change needs a choice (provider, replacement or conflict) that this app does not make. Use pacman for it. Nothing was changed. | Close |
| `PartialUpgradeRequired` | This change would upgrade installed packages: {names}. Update the system first, outside this app. This app does not upgrade the system. Nothing was changed. | Close |
| `InsecureSigLevel` | A repository is configured without strict signature checks: {names}. Installation is refused until the configuration is fixed outside this app. Nothing was changed. | Close |
| `SyncDatabaseStale` | The repository is out of date: {name} is listed but not available. Nothing was changed. | Close |
| `DownloadFailed` | Downloading {name} failed. Check the network connection. Nothing was changed. | Close |
| `FileConflicts` | Files of {names} conflict with each other or with existing files. Nothing was changed. | Close |
| `DiskSpace` | There is not enough disk space. Nothing was changed. | Close |
| `SignatureInvalid` | The signature of {names} could not be verified. Nothing was changed. | Close |
| `DatabaseUnavailable` | The package database is missing or damaged. Nothing was changed. | Close |
| `ConfigurationError` | The pacman configuration could not be read. Nothing was changed. | Close |
| `NothingToDo` | The package is already installed at the current version. | Close |
| `PlanNotFound` | The confirmation is no longer valid. | Try again, Close |
| `PlanExpired` | The confirmation expired after five minutes. | Try again, Close |
| `PlanChanged` | The package situation changed before the change started. Nothing was changed. Start again from the list. | Close |
| `NotAuthorized` (note in preview) | Authorization was not granted. Nothing was changed. | Confirm, Cancel |
| `Busy` | Another package operation is running. Try again when it finishes. | Try again, Close |
| `PastPointOfNoReturn` | Closing is unavailable until the transaction finishes. | none (window stays open) |
| `TransactionFailed` | The transaction failed. The packages listed were changed before the failure. | Close |
| `TransactionFailed` (empty changed list) | The transaction failed. Nothing was changed. | Close |
| `InternalError` | An unexpected error stopped the operation. Nothing was changed. | Close |
| `Succeeded` | Done. The packages listed were changed. | Close |
| `HookFailed` (warning note) | Done, but a post-install hook failed. Details are in the system journal. | (with Succeeded) |
| `Cancelled` | Cancelled. Nothing was changed. | Close |
| `Unknown` | The helper did not answer, so the outcome is unknown. Check the Installed list before doing anything else. | Close |
| `HelperUnavailable` | The package helper is not available. Check that it is installed. | Close |
| `Cancelling` (label) | Cancelling. | none |

Note on `PackageNotFound`: the helper also uses this code for a remove target that is not installed, so the wording depends on the action of the request. It is still one table key (REQ-F-048).

Note on `TransactionFailed`: the first row applies when the changed list is not empty; the second row when it is empty.

---

## Traceability summary

| Category | Count | Acceptance layer (Test plan) |
|----------|-------|------------------------------|
| Functional (REQ-F) | 50 | Layer 1 (view-model unit tests with the fake service), Layer 2 (adapter tests on a private bus), Layer 3 (runtime and QML tests) |
| Non-functional (REQ-NF) | 8 | Layers 1, 2 and 3; `task check` (REQ-NF-007) |
| Constraints (REQ-C) | 5 | Source and repository checks; the manual VM checklist (REQ-C-005) |

Test plan by layer:

1. **View-model state machine** (fake service): every state in REQ-F-007; every reply code in the message table; `Busy` and retry; the close-blocking rule (REQ-F-033 to REQ-F-036); the list refresh after each outcome (REQ-F-019).
2. **Adapter** (fake helper object on a private `dbus-daemon`): timeouts (REQ-F-041, REQ-F-042, REQ-F-045), the `Progress` match rule (REQ-F-044), no reply mapped to `Unknown` (REQ-F-045), helper unavailable (REQ-F-046), the missing-daemon behaviour (REQ-NF-006).
3. **QML and runtime** (both styles, driven through the view-model): dialog loading and rendering (REQ-NF-003), button enablement per state (REQ-F-029, REQ-F-012), per-view scope (REQ-C-003).
4. **Container acceptance**: `task check` (REQ-NF-007).
5. **Manual VM checklist**: REQ-C-005.

Cross-reference to the helper SPEC: REQ-F-009 (retry after `NotAuthorized` within the plan lifetime), REQ-F-012 (disconnect discards a plan), REQ-F-031 (one transaction), REQ-F-049 (phases), REQ-F-051 (result), REQ-F-056 and REQ-F-058 (cancel before and after the commit point), REQ-F-071 (plan limits).

---

## Risks

| # | Risk | Mitigation | Requirements |
|---|------|------------|--------------|
| 1 | `PartialUpgradeRequired` is common on a rolling release, and the GUI has no remedy yet, so many installs will be refused. | The refusal states the system must be updated outside the app; no override is offered; the remedy is a later feature (OQ-07). | REQ-F-022, REQ-C-005 (item 4) |
| 2 | A confirmation left open longer than 5 minutes produces `PlanNotFound` or `PlanExpired` on Confirm. | `PlanNotFound` and `PlanExpired` offer Try again (OQ-05, decided); the expiry time is shown in the preview. | REQ-F-003, REQ-F-023, REQ-F-027 |
| 3 | No reattach: a GUI force-quit during a transaction leaves the helper running. The next GUI start sees a generic `Busy`. | A specific message at start; Try again; no hidden recovery. | REQ-F-037 |
| 4 | A helper killed mid-transaction can leave `db.lck`, so every later transaction is `Busy` (helper SPEC REQ-F-047 and `apps/helper/README.md`). | Message states the operation is running or blocked; the lock is removed by hand as documented for the helper. | REQ-F-037 |
| 5 | The GUI disables Cancel at `Hooks` while the helper's commit point is the first `Hooks` event. A user who waits for the enabled state could still see `PastPointOfNoReturn`. | The adapter and view-model treat a late `PastPointOfNoReturn` as a normal outcome; the deviation is accepted (OQ-04). | REQ-F-012, REQ-F-036 |
| 6 | The GUI blocks its thread on a helper call, so the window freezes during a long `Execute`. | Asynchronous calls only; a runtime test with a held reply. | REQ-F-043 |
| 7 | An `Unknown` outcome looks like a failure, and the user retries an install that already happened. | Unknown has no retry; the message tells the user to check the list; the list is re-read. | REQ-F-018, REQ-F-019 |
| 8 | A package name or message with markup changes the rendered dialog. | Plain text only; a test with markup in a name. | REQ-F-049 |
| 9 | Qt types leak into `src/domain` or `src/application`, which breaks the layering rules. | Port with plain types; source searches for transport terms. | REQ-F-038, REQ-F-039 |
| 10 | The adapter tests need `dbus-daemon` in the CI container, and a silent skip hides a broken adapter. | The test fails or reports a CTest skip status; the container image is checked for the binary. | REQ-NF-005, REQ-NF-006 |
| 11 | The repository rules say the GUI never runs transactions and the GUI is inert, so the feature conflicts with the rule text. | The rule text is amended in the same change (REQ-C-001). | REQ-C-001 |
| 12 | The manual checklist cannot be automated, so the cycle may be declared done without it. | The cycle is open until the README section records seven passes. | REQ-C-005 |

---

## Resolved decisions

The following decisions and defaults are accepted. Finalization still requires automated acceptance and all seven human VM passes (REQ-C-005).

- **OQ-01 — RESOLVED by DESIGN §8.1.** The Installed detail header already has a Remove button, which is wired to the controller. Explore gets an Install button in the same slot, with "More options" hidden there. The disabled rule is REQ-F-029.
- **OQ-02 — Queued packages.** Accepted default: no queue and no special UI, because only one action runs at a time (REQ-F-030). A disabled control is enough.
- **OQ-03 — RESOLVED by DESIGN §7.2 (2026-11-11).** The repo marks strings with `qsTr`/`tr` but has no catalogs or `lupdate`. The message table uses `QT_TRANSLATE_NOOP` English strings resolved through `QCoreApplication::translate`; no catalogs are added (see OQ-13). The table is a fixed set of translatable keys (REQ-F-048).
- **OQ-04 — RESOLVED (2026-11-11, accepted).** Cancel is disabled from the `Hooks` phase onwards (REQ-F-012), because the helper's commit point is the start of the first PreTransaction hook. This replaces the stage-0 wording "until Installing or Removing".
- **OQ-05 — RESOLVED (2026-11-11).** `PlanExpired` also offers Try again, as `Busy` and `PlanNotFound` do (REQ-F-023, REQ-F-027).
- **OQ-06 — Count label for Install.** Remove has a count label. Accepted default: the Install Confirm label reads `Install` and the dependency rows are listed (REQ-F-003).
- **OQ-07 — Remedy for `PartialUpgradeRequired`.** The GUI offers no remedy in this feature. A later feature may offer a way to bring the system up to date. Accepted default: none in this SPEC.
- **OQ-08 — RESOLVED (SC-4).** Success is an empty `code` string, `Cancel` returns `(sasa(ssas))`, `expires_at` is epoch seconds, and outcomes are `Succeeded`, `Failed`, `Cancelled`. The adapter decodes these fields using the XML and checks them with contract tests (REQ-C-004).
- **OQ-09 — Expiry handling in the GUI.** The GUI could disable Confirm after the preview's expiry time, instead of letting the helper reject it. Accepted default: no client-side expiry check in this SPEC; the helper decides (REQ-F-027).
- **OQ-10 — Closing during `preparing`.** REQ-F-034 exits without `Cancel`, relying on the helper discarding a plan when its caller disconnects (helper SPEC, REQ-F-012). Accepted default; no Cancel is sent while preparing.
- **OQ-11 — RESOLVED (2026-11-11).** This feature creates `apps/packages/README.md` and records the checklist there (REQ-C-005).
- **OQ-12 — RESOLVED (2026-11-11).** `AGENTS.md` and `CLAUDE.md` are amended in this feature (REQ-C-001). Note: `CLAUDE.md` is gitignored, so only `AGENTS.md` is tracked by git; the CLAUDE.md edit is local.
- **OQ-13 — RESOLVED (2026-11-11, design questions OQ-D1 to OQ-D5 accepted at their defaults).** (D1) The refresher also reloads the Explore index and repeats a dropped request (REQ-F-019). (D2) The `Preparing` page has no dismiss control. (D3) No translation catalogs or `lupdate` in this feature. (D4) The stale `qml/packages`/`qml/explore` half of the C-007 base check in `scripts/check-layering.py` is removed in this change. (D5) A bus error reply is shown as `InternalError` (REQ-F-050).
