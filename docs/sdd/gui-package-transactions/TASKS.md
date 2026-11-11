# SDD Tasks — gui-package-transactions

- [x] T-001: Domain port and value types: `TransactionService` port with `PreparedPlan`, `ServiceError` and the reply aliases, the four `parse*` functions, CMake and umbrella-header entries, and the domain tests
  - REQs: REQ-F-038, REQ-C-002
  - Check: `ctest --test-dir build/dev -R 'transaction_service|holonight_packages_checks_no_qtdbus'` passes, where the transaction_service cases compile a test double that implements only the port and assert that each progress phase, outcome, action and warning name parses to its enumerator, and `git diff --name-only -- apps/helper src/transaction` prints nothing.

- [x] T-002: `PackageListRefresh` port header and the test doubles `FakeTransactionService` (records calls, holds callbacks until released, emits progress) and `FakePackageListRefresh` (counter), under `tests/application`, with their own test
  - REQs: REQ-F-019, REQ-NF-001
  - Check: `ctest --test-dir build/dev -R TransactionDoubles` passes, with a case showing that a held `preparePlan` callback is delivered exactly once when released and that the fake's call log is empty before the first call.

- [x] T-003: `TransactionFlow` preparation side: the states, `request`, `dismiss`, refusal and HelperUnavailable on preparation, Busy and Try again on preparation, the one-request rule and the open-dialog counter, with the flow kept Qt-free (test suite `TransactionFlowPrepare`)
  - REQs: REQ-F-001, REQ-F-002, REQ-F-006, REQ-F-007, REQ-F-021, REQ-F-022, REQ-F-023, REQ-F-024, REQ-F-028, REQ-F-029, REQ-F-030, REQ-F-031, REQ-F-037, REQ-F-039, REQ-NF-001
  - Check: `ctest --test-dir build/dev -R TransactionFlowPrepare` passes, with one case per taxonomy code asserting zero Execute calls and a Close-only action list (Try again is offered only for Busy, PlanNotFound and PlanExpired, per SPEC REQ-F-023), a second request during Preparing leaving one PreparePlan call, and `ctest --test-dir build/dev -R holonight_packages_checks_no_qtdbus` still passing.

- [x] T-004: `TransactionFlow` execution and outcome side: Confirm passes only the plan ID, Execute at most once per plan, Succeeded with the hook warning, Failed, Cancelled, Unknown with no retry, HelperUnavailable on Execute, NotAuthorized back to Previewed with Confirm on the same plan, PlanExpired and PlanNotFound offering Try again, and one list refresh per outcome (test suite `TransactionFlowExecute`)
  - REQs: REQ-F-005, REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019, REQ-F-020, REQ-F-023, REQ-F-025, REQ-F-026, REQ-F-027, REQ-F-028
  - Check: `ctest --test-dir build/dev -R TransactionFlowExecute` passes, asserting exactly one refresh request for each of Succeeded, Failed, Cancelled and Unknown and none for NotAuthorized, that NotAuthorized followed by Confirm records two Execute calls with the same plan ID and no PreparePlan, and that an Unknown outcome records one Execute and no second PreparePlan.

- [x] T-005: `TransactionFlow` progress and cancel flag: progress filtering by plan ID, the regression rule per phase occurrence, the latching cancel flag over every phase, Cancel while Authorizing or Executing with the Cancelling state, and an ok Cancel reply that changes nothing else (test suite `TransactionFlowProgress`)
  - REQs: REQ-F-008, REQ-F-010, REQ-F-012, REQ-F-013
  - Check: `ctest --test-dir build/dev -R TransactionFlowProgress` passes, covering the cancel flag true after Resolving, Downloading and Verifying, false after Hooks, Installing, Removing and Done, a later Resolving not re-enabling it, an event with another plan ID changing nothing, and a Downloading 4/10 followed by 2/10 leaving current at 4.

- [x] T-006: `TransactionFlow` close rules: `closable`, `closeRequested` decisions per state, the deferred close resolved by the Cancel reply (ok, PlanNotFound or fault), PastPointOfNoReturn keeping the window open with the close-unavailable notice (test suite `TransactionFlowClose`)
  - REQs: REQ-F-032, REQ-F-033, REQ-F-034, REQ-F-035, REQ-F-036
  - Check: `ctest --test-dir build/dev -R TransactionFlowClose` passes, with a Previewed close recording one Cancel and signalling closeReady after its reply, a Preparing close recording no Cancel, an Executing close after Hooks returning Ignore with the CloseUnavailable notice and no Cancel, and a PastPointOfNoReturn reply leaving the state Executing.

- [x] T-007: `ModelListRefresh` adapter in `apps/packages/app`, implementing `PackageListRefresh` over the real `InstalledPackagesModel` and `ExploreModel`, and repeating a refresh that arrives during a load (test suite `ModelListRefresh`, tests in this task)
  - REQs: REQ-F-019
  - Check: `ctest --test-dir build/dev -R ModelListRefresh` passes, where a refresh requested while the MockPackageSource-backed Installed model is loading is repeated exactly once after the load ends and the Explore reload is requested once per outcome.

- [x] T-008: `HelperWire`: the wire structures with QDBus operators and type registration, the pure `decodePrepareReply`, `decodeExecuteReply`, `decodeCancelReply` and `decodeProgress` decoders, and `classifyBusError`, with bus-free tests (test suites `HelperWire` and `HelperErrorClassification`)
  - REQs: REQ-F-046, REQ-F-047, REQ-F-050, REQ-C-004
  - Check: `ctest --test-dir build/dev -R 'HelperWire|HelperErrorClassification'` passes, with a case asserting the QDBusMetaType signatures equal the `type` attributes in `apps/helper/dbus/org.holonight.PackageHelper1.xml`, an unknown status code `SomethingElse` decoding to InternalError with no names, an undecodable Execute reply mapping to NoReply, and each bus error name of the DESIGN 6.4 table mapping to its kind.

- [x] T-009: `TransactionMessages` table: one entry per taxonomy code and per note (`QT_TRANSLATE_NOOP` resolved through `QCoreApplication::translate`), the action-aware PackageNotFound wording, `removeConfirmLabel`, `phaseLabel`, and names inserted as plain text (test suite `TransactionMessages`, tests in this task)
  - REQs: REQ-F-048, REQ-F-049
  - Check: `ctest --test-dir build/dev -R TransactionMessages` passes, with a case that iterates the 24 codes and every note and asserts a non-empty text, a case asserting the PackageNotFound install and remove texts differ, and a case asserting a package name `<b>x</b>` appears literally in the message.

- [x] T-010: Private-bus helper client target: the scripted `FakeHelperObject` (reads the helper XML and rejects a call whose signature differs), a `HelperBusFixture` that starts a private `dbus-daemon`, the `test_holonight_packages_helper_client` target, and the `helper_client_requires_dbus_daemon` WILL_FAIL test (test suite `HelperClientBus`)
  - REQs: REQ-NF-005, REQ-NF-006, REQ-C-004
  - Check: `ctest --test-dir build/dev -R 'helper_client_requires_dbus_daemon|HelperClientBus'` passes, where the WILL_FAIL case fails with PATH emptied, a self-test shows the fake rejects a `PreparePlan` call with an extra argument using InvalidArgs, and `grep -rn systemBus tests/helper_client` finds nothing.

- [x] T-011: `DBusTransactionService` call path: constructor with `Timeouts` and `timeouts()`, the lazy connection factory, asynchronous `preparePlan`, `execute` and `cancel` with one watcher per call, and the mapping of no owner, timeout, dropped helper and error replies (test suite `DBusTransactionServiceCalls`, tests in this task)
  - REQs: REQ-F-041, REQ-F-042, REQ-F-043, REQ-F-045, REQ-F-046, REQ-F-050, REQ-C-004
  - Check: `ctest --test-dir build/dev -R DBusTransactionServiceCalls` passes, with the default timeouts equal to 30 000, 3 600 000 and -1, an injected 300 ms Execute timeout reporting NoReply with one Execute recorded, a helper held for 2 s letting a 100 ms timer fire at least 10 times, and a missing helper reporting HelperUnavailable.

- [x] T-012: `DBusTransactionService` progress subscription on the calling connection with the sender-restricted match rule, `progressMatchRule()`, plan-ID filtering, and the contract test that runs the adapter against the real `HelperService` over `PlanService` (test suites `DBusTransactionServiceProgress` and `HelperContract`, tests in this task)
  - REQs: REQ-F-044, REQ-F-047, REQ-C-004
  - Check: `ctest --test-dir build/dev -R 'DBusTransactionServiceProgress|HelperContract'` passes, where an unrelated connection's `Progress` for the current plan ID reaches the handler zero times while the fake helper's events arrive, all 24 wire codes decode to themselves, and the adapter decodes the helper's own `toWire` encodings to the expected outcome, changed list and warnings for prepare, denied execute, allowed execute and cancel.

- [x] T-013: `TransactionRowModel` (`QAbstractListModel` with the DESIGN 4.7 roles, reset on each content change) and the new `test_holonight_packages_transactions` target with its `no_qtdbus` CTest (test suite `TransactionRowModel`, tests in this task)
  - REQs: REQ-F-003, REQ-F-049
  - Check: `ctest --test-dir build/dev -R 'TransactionRowModel|holonight_packages_transactions_no_qtdbus'` passes, with a case asserting that target, dependency and orphan rows of a fake preview carry their kind, repository and version roles and the hook rows carry the conditional role, and a case asserting that a row name `<b>x</b>` is returned unchanged.

- [x] T-014: `TransactionController` preview and progress side: the `State` and `Page` enums with QMetaEnum completeness, `requestInstall`, `requestRemove`, `confirm`, `dismiss`, `cancelRun`, the confirm label and expiry properties, progress value and indeterminate flag, the cancel, closable and open-dialog properties, and the full install and remove isolation test (test suites `TransactionControllerPreview` and `TransactionIsolation`)
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-013, REQ-F-029, REQ-F-031, REQ-F-032, REQ-NF-002
  - Check: `ctest --test-dir build/dev -R 'TransactionControllerPreview|TransactionIsolation'` passes, asserting that Remove with orphans lib and util gives the label `Remove 3 packages`, that a Downloading 3/10 event gives progressValue 0.3 with a determinate bar, that Resolving 0/0 is indeterminate, and that the isolated HOME and XDG manifest is unchanged after a full install and a full remove through the fake.

- [x] T-015: `TransactionController` result side: result rows and the changed list, refusal and failure messages from `TransactionMessages`, the action lists per state, the NotAuthorized and hook-warning notices, `tryAgain`, and plain-text names (test suite `TransactionControllerResult`, tests in this task)
  - REQs: REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-021, REQ-F-022, REQ-F-023, REQ-F-024, REQ-F-027, REQ-F-049
  - Check: `ctest --test-dir build/dev -R TransactionControllerResult` passes, asserting that `actions` is exactly Close for every code other than Busy, PlanNotFound and PlanExpired, that PartialUpgradeRequired offers only Close, that a Cancelled outcome exposes no result rows even when the reply carries one, that tryAgain after Busy records one PreparePlan, and that a `<b>x</b>` package name appears literally in `message` and `packageName`.

- [x] T-016: `WindowCloseGuard` event filter on the `QQuickView` that accepts, defers or ignores `QEvent::Close` through `TransactionController::handleCloseRequest` and `closeReady` (test suite `WindowCloseGuard`, tests in this task)
  - REQs: REQ-F-033, REQ-F-034, REQ-F-035
  - Check: `ctest --test-dir build/dev -R WindowCloseGuard` passes, where a `QCoreApplication::sendEvent` of a `QCloseEvent` during Executing with the cancel flag set records one Cancel and keeps the window open until the fake Cancel reply, a close during Preparing is accepted with no Cancel call, and a close after Hooks is ignored with the close-unavailable notice.

- [x] T-017: Layering rule `T-001` (the helper bus name only in the adapter and wire files) with its passing and failing fixtures, and removal of the stale `qml/packages` and `qml/explore` half of the C-007 base check (OQ-D4)
  - REQs: REQ-F-038, REQ-F-039, REQ-F-040, REQ-C-001
  - Check: `ctest --test-dir build/dev -R 'layering_policy|layering_policy_fixtures'` passes with the fixtures `t001-helper-name-in-controller` (script exits non-zero) and `legit-adapter-names-helper` (exits 0), and `task layering-check` exits 0 on the repository.

- [x] T-018: QML `qml/transactions` host and shared parts: `TransactionDialog` (`Controls.Popup`, explicit `focus: true`, closePolicy by `closable`, Loader switching by page), `TransactionConfirmPage`, `TransactionChangeRow` and `TransactionActionRow`, with the source-based view test (test suite `TransactionDialogView`, tests in this task)
  - REQs: REQ-F-003, REQ-F-004, REQ-F-032, REQ-F-049, REQ-NF-004
  - Check: `ctest --test-dir build/dev -R TransactionDialogView` passes, with a confirm page over a fake preview showing two rows and the expiry, the Install confirm label, the popup `closePolicy` excluding CloseOnPressOutside and following `closable`, and the test sources containing no `mouse*`, `touch*`, `key*` or `forceActiveFocus` call.

- [x] T-019: QML progress and result pages (`TransactionProgressPage`, `TransactionResultPage`): the progress page holds only the phase label, the item, the bar and the Cancel control, and the result page shows the message, hook note, changed rows, NotAuthorized and close-unavailable notes, and the buttons from `controller.actions` (test suite `TransactionDialogView`, extended in this task)
  - REQs: REQ-F-008, REQ-F-011, REQ-F-012, REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-022, REQ-F-024, REQ-F-035, REQ-F-036, REQ-F-049, REQ-NF-004
  - Check: `ctest --test-dir build/dev -R TransactionDialogView` passes, with the progress page in Executing containing no TextArea or list item, its Cancel control disabled after a Hooks event, a refusal result page offering only Close, and every HnLabel reporting `Text.PlainText`.

- [x] T-020: Button wiring: `PackageDetailHeader` Remove on the Installed panel and an Install button on the Explore panel with the enabled rule, the optional `transactionController` plumbing through `WorkspaceWindow`, the two views and the detail panels, and the `TransactionDialog` Loader in `WorkspaceWindow` (QML test in `test_holonight_packages_transactions`, test suite `TransactionButtons`, tests in this task)
  - REQs: REQ-F-001, REQ-F-002, REQ-F-029, REQ-C-003, REQ-NF-008
  - Check: `ctest --test-dir build/dev -R TransactionButtons` passes, with Install enabled only on Explore and Remove only on Installed when a controller whose allowAction is true is provided, both disabled with the tooltip "Not implemented yet" when no controller is given, and the existing explore, installed and updates view tests still pass unchanged.

- [x] T-021: Composition-root wiring in `PackagesApplication`: `registerHelperWireTypes`, the lazily connected system-bus factory, `ModelListRefresh`, `TransactionController`, the `transactionController` initial property, `WindowCloseGuard`, the destructor order of DESIGN section 10, the CMake sources, and `TransactionController` in the qmltypes required types
  - REQs: REQ-F-040
  - Check: `task build` exits 0, `grep -n "DBusTransactionService" apps/packages/app/PackagesApplication.cpp` finds the construction and nothing under `apps/helper` or `src/transaction`, `scripts/check-runtime-launches.py` passes, and `task qmltypes-check` passes with `TransactionController` listed in `required_types`.

- [x] T-022: Runtime acceptance for the dialogs in `tests/runtime/test_transaction_dialogs.cpp`, added to `test_runtime_controls`: confirm for install and for remove with count, determinate and indeterminate progress, Hooks disabling Cancel, each result page, each refusal action set, and no QML warning, each case naming the controller call that drives it
  - REQs: REQ-F-003, REQ-F-004, REQ-F-008, REQ-F-009, REQ-F-012, REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-021, REQ-F-022, REQ-F-024, REQ-NF-003, REQ-NF-004
  - Check: `ctest --test-dir build/dev -R 'runtime_controls_Holonight|runtime_controls_Fusion'` passes with the new transaction dialog cases in both runs, and `grep -nE "mouseClick|forceActiveFocus|key[A-Z]" tests/runtime/test_transaction_dialogs.cpp` finds nothing.

- [x] T-023: Runtime acceptance for busy state, scope, close and idle behaviour in both styles: Install and Remove disabled in the four busy states and enabled in idle and terminal states, no Install on Installed, no Remove on Explore or Updates, the close-unavailable notice, and no transaction call on opening a page
  - REQs: REQ-F-029, REQ-F-035, REQ-F-036, REQ-C-003, REQ-NF-003, REQ-NF-004, REQ-NF-008
  - Check: `ctest --test-dir build/dev -R 'runtime_controls_Holonight|runtime_controls_Fusion'` passes with the new busy, scope and close cases in both runs, and a fake service call count of zero after opening each of the Installed, Updates and Explore pages.

- [x] T-024: Repository documents: `AGENTS.md` (replace the "never runs package transactions" sentence and add the helper-client sentence), the local `CLAUDE.md`, the "transactionally inert" and read-only lines of `README.md`, and an Unreleased Added entry in `CHANGELOG.md`
  - REQs: REQ-C-001
  - Check: `grep -c "never run package transactions" AGENTS.md CLAUDE.md scripts/check-layering.py` prints 0 for each file, `grep -c "transactionally inert" README.md CLAUDE.md` prints 0 for each file, the carve-out line of AGENTS.md is byte-identical to its previous version, `CHANGELOG.md` has an Unreleased Added entry naming Install and Remove, and `task layering-check` exits 0.

- [x] T-025: New `apps/packages/README.md`: what the GUI does, how to run it with the helper installed and a polkit agent running, the code locations of the port, flow and adapter, a link to `docs/sdd/gui-package-transactions/`, and the section `Manual verification: package transactions` listing the seven REQ-C-005 items, each marked pending
  - REQs: REQ-C-005
  - Check: `grep -c "Manual verification: package transactions" apps/packages/README.md` prints 1, and that section contains exactly seven numbered items, each containing the word `pending` and a `result:` and `date:` slot.

- [x] T-026: Final automated acceptance: `task check` in the containerized acceptance, and confirmation that the helper and transaction trees and the interface XML are unchanged from `main`
  - REQs: REQ-NF-007, REQ-C-002
  - Passed 2026-11-11: `task check` and `task check:integration` with the published replacement image pin cover the final detail-indicator fix. Both runs report 918 CTest cases, zero failures and five namespace-dependent skips; quality gates and Release staging/runtime checks pass. The initial image lacked AppStreamQt; publishing and pinning the tested replacement fixed configuration. Helper and transaction trees, including interface XML, match `main`.
  - Check: `task check` exits 0, `git diff --name-only main -- apps/helper src/transaction` prints nothing, and `sha256sum apps/helper/dbus/org.holonight.PackageHelper1.xml` equals `git show main:apps/helper/dbus/org.holonight.PackageHelper1.xml | sha256sum`.

- [x] T-027: Manual VM checklist (a human step, run last; leave unchecked until it is run): on a disposable Arch VM with a real polkit agent, the real system bus and root, run the seven items of `Manual verification: package transactions` in `apps/packages/README.md` and record each result with its date
  - REQs: REQ-C-005
  - Passed 2026-11-11: all seven scenarios and Escape dismissal/protection were run by the tester in the disposable Arch/Plasma VM with the real helper and graphical polkit agent. Package manifests and lock checks passed; normal mirror and helper activation were restored and the delay hook retired.
  - Check: the section in `apps/packages/README.md` shows each of the seven items with a `passed YYYY-MM-DD` record or a `failed ..., fixed and passed ...` record, and no item still reads pending.
