# GUI package transactions — DESIGN (HoloNight Packages)

Status: Finalized 2026-11-11. Automated acceptance and all seven required human VM checks passed. Input: `SPEC.md` in this directory (63 requirements: 50 functional, 8 non-functional and 5 constraints; authoritative). The helper's contract is fixed by `docs/sdd/package-helper-transactions/SPEC.md` and `apps/helper/dbus/org.holonight.PackageHelper1.xml`; this design changes neither. Resolved decisions taken from the SPEC: Cancel is disabled from the `Hooks` phase onwards (OQ-04); `PlanExpired` offers Try again like `Busy` and `PlanNotFound` (OQ-05); `AGENTS.md` and `CLAUDE.md` are amended (OQ-12); `apps/packages/README.md` is created for the manual VM checklist (OQ-11).

Sections 1 to 14 describe the design. Section 15 ("Spec corrections") lists every place where the SPEC conflicts with the code or the helper contract; read it before implementation.

---

## 1. Overview

The GUI gains one capability: request an install (Explore page) or a remove (Installed page) of one official-repository package from `holonight-package-helper`, show the helper's preview, execute the previewed plan after confirmation, show progress and the result, and re-read the package lists.

The design follows the existing modular monolith (dependencies point toward `src/domain`):

- `src/domain`: a new port `TransactionService` with plain value types. It **reuses** the types the helper work already put in the domain (`PlanRequest`, `ResolvedPlan`, `PlannedChange`, `HookPreview`, `TransactionErrorCode` with its 24 codes, `TransactionFailure`, `ProgressUpdate`, `ExecutionResult`, `ChangedPackage`, `WarningToken`). Nothing new is invented for what already exists.
- `src/application`: `TransactionFlow`, a Qt-free state machine that drives the port, plus a tiny `PackageListRefresh` port. It holds all of REQ-F-001 to REQ-F-037 that are about states, transitions, cancel flag, retries and close rules, so those are tested with GTest and a fake service, with no Qt event loop and no D-Bus (REQ-NF-001).
- `apps/packages/app`: the QDBus adapter `DBusTransactionService` (the only code that names the helper), the wire decoding `HelperWire`, the QML-facing `TransactionController` and `TransactionRowModel`, the translatable message table `TransactionMessages`, the list refresher `ModelListRefresh`, and `WindowCloseGuard` (window close hook).
- `qml/transactions/`: a new feature folder with one modal popup that hosts the confirmation, progress and result pages. Install and Remove buttons are added to the existing shared detail header in `qml/packages/PackageDetailHeader.qml`.

Nothing in `apps/helper`, `src/transaction` or the interface XML changes (REQ-C-002). Existing pages behave as before unless the user takes a transaction action (REQ-NF-008): the controller is an optional (nullable) QML property, so every existing QML test keeps loading the pages without it.

## 2. Findings that shape the design

Verified by reading the repository; these settle SPEC open questions and constrain the design.

| # | Finding | Evidence | Consequence |
|---|---------|----------|-------------|
| F1 | **OQ-03 (translation).** The project marks strings for translation but has no translation pipeline: 113 `qsTr(` uses in `qml/`, `tr(` in `apps/packages/app/*.cpp` (25 uses), `QCoreApplication::translate("DataFreshness", ...)` in `DataFreshness.cpp`; there is no `.ts`/`.qm` file, no `LinguistTools`/`lupdate`/`qt_add_translations` in any CMake file, and no `QTranslator` is installed (`CMakeLists.txt` requires `Core Gui Quick Qml Network Sql DBus Concurrent`). | grep over `qml/`, `apps/`, `CMakeLists.txt`, `Taskfile.yml`; `git ls-files` shows no `.ts`. | The message table follows the existing convention (source text in English, wrapped for translation) and adds no pipeline. Adding catalogs and `lupdate` is a separate decision (OQ-D3). |
| F2 | The Installed page already has a **Remove** button in `qml/packages/PackageDetailHeader.qml` (`objectName: "packageDetailRemoveButton"`, tooltip "Not implemented yet", next to a "More options" icon button). The Explore detail panel passes `showActions: false`. | `PackageDetailHeader.qml` lines 82-150; `ExploreDetailPanel.qml`. | Resolves OQ-01 (section 8.1). |
| F3 | `InstalledPackagesModel::refresh()` is `Q_INVOKABLE`, re-runs `PackageListUseCase::enumerateInstalledPackages()` (libalpm, off-thread via `QtConcurrent`) and resets the model. It is **single-flight**: it returns silently while a load is in progress. `ExploreModel::reload()` is `Q_INVOKABLE` and also single-flight. | `InstalledPackagesModel.cpp:156-161`; `ExploreModel.h`. | The list-refresh adapter must remember a request that arrives during a load and repeat it (section 9). |
| F4 | `src/domain` already contains the whole transaction vocabulary for the helper (`transaction_error.h`, `transaction_plan.h`, `transaction_progress.h`, `transaction_request.h`, `transaction_result.h`, `transaction_engine.h`), and `TransactionErrorCode` has exactly 24 enumerators with names equal to the helper SPEC error taxonomy. `parseTransactionErrorCode()` exists; parse functions for phase, outcome, action and warning do not. | `src/domain/include/holonight_packages_domain/`. | The port reuses these types; four small `parse*` functions are added to the domain (section 4.1). |
| F5 | `dbus-daemon` is already a test prerequisite: `tests/packaged/private_bus.h` (`holonight_packages_testing::PrivateBus`, starts `dbus-daemon --session --nofork --print-address=1`) is used by `tests/packaged/*` and `tests/helper/*`, which run in `task test` inside the pinned CI image (`scripts/ci/image.txt`, `ghcr.io/lebedenko/holonight-deps@sha256:4d46...`). When the daemon is missing, `PrivateBus::start()` returns false and the fixtures `ASSERT_TRUE` it, so those tests fail rather than pass. The image itself cannot be inspected offline; its presence of `dbus-daemon` is proven by the existing green `task check`. | `tests/packaged/private_bus.h`; `tests/packaged/update_status_client_test.cpp`; `tests/helper/helper_dbus_test.cpp`; `scripts/ci/check.sh`. | No new bus helper and no Dockerfile change. REQ-NF-006 is met by the existing fixture idiom plus one `WILL_FAIL` CTest (section 12.3). |
| F6 | `QQuickView`'s root item is a `Rectangle` (`WorkspaceWindow.qml`), not a `Window`, so there is no QML `onClosing` in the page tree; the close request reaches C++ as `QEvent::Close` on the `QQuickView`. | `PackagesApplication.cpp`; `WorkspaceWindow.qml`. | Window close handling is a C++ event filter (section 8.5), not QML. |
| F7 | `HnLabel` (`../holonight-qt/qml/core/HnLabel.qml`) renders `rawText` with `textFormat: Text.PlainText` (unless `showPrefix` with `MicroHeader`). `Controls.Popup` is the transaction popup base, with explicit modality, focus, overlay parenting and state-dependent Escape policy. | holonight-qt sources. | All dialog text uses `HnLabel { rawText: ... }` (REQ-F-049); the dialog is built on `Controls.Popup`. |
| F8 | `scripts/check-layering.py` rules relevant here: `H-001` (`QDBus`, `<QtDBus`, case-insensitive `polkit` forbidden in `src/domain` and `src/application`), `C-001` (`<alpm.h>`, `alpm_*`, `dbpath`, `databasePath` forbidden outside `src/backends`, `src/transaction` and two composition roots), `C-005` (`alpm_trans_*` and `pacman -S/-U/-R` forbidden in `src`, `apps`, `qml`, `tests`), `C-006` (`AGENTS.md`, and `CLAUDE.md` when present, must contain exactly one line with the carve-out text and must not contain `never run package transactions`), `C-009`, `C-010` (no `QTest::mouse*`/`touch*` and, for new files, no `key*` in tests). | `scripts/check-layering.py`. | New domain/application code and comments must never contain the word "polkit"; the carve-out line must stay verbatim; one new rule `T-001` confines the helper's names to the adapter (section 13). |
| F9 | `tests/CMakeLists.txt` compiles `apps/packages/app/*.cpp` directly into test executables (it does not link the `holonight-packages` target). The libalpm-free, Quick-free executable `test_holonight_packages_checks` links only Core, Test, Concurrent, application, snapshot_store and domain, and has an `ldd | grep libalpm` guard test. | `tests/CMakeLists.txt`. | The same idiom gives a QtDBus-free state-machine target (section 12). |

## 3. Component diagram

```
                          ┌──────────────────────────── apps/packages (GUI process) ──────────────────────────────┐
 QML (qml/)               │                                                                                         │
 ┌──────────────────────┐ │  TransactionController (QObject, QML_UNCREATABLE)      WindowCloseGuard (event filter)  │
 │ workspace/            │ │   Q_PROPERTYs, Q_INVOKABLEs, owns 3x TransactionRowModel     │ QEvent::Close on QQuickView
 │  WorkspaceWindow.qml  │◄┼──┤                                                              ▼                      │
 │ transactions/         │ │  │       ┌────────────────────────────────────────────────────────────────────┐      │
 │  TransactionDialog    │ │  │       │ TransactionMessages (translatable table, plain QString)            │      │
 │  *Page.qml, Row.qml   │ │  │       └────────────────────────────────────────────────────────────────────┘      │
 │ packages/ explore/    │ │  │                                                                                     │
 │  PackageDetailHeader  │ │  │  ModelListRefresh ──► InstalledPackagesModel::refresh(), ExploreModel::reload()     │
 │   (Install / Remove)  │ │  │                                                                                     │
 └──────────────────────┘ │  │  DBusTransactionService (QObject) + HelperWire (decode)  ──── QtDBus, system bus ──┼──► holonight-package-helper
                          └──┼──────────────────────────────────▲──────────────────────────────────────────────────┘     (org.holonight.PackageHelper1)
                             │ uses                              │ implements
 ┌───────────────────────────▼───────────────────┐   ┌───────────┴───────────────────────────────────────────────┐
 │ src/application   (Qt-free logic, no D-Bus)   │   │ src/domain   (plain types, no Qt D-Bus, no libalpm)       │
 │  TransactionFlow  state machine               │──►│  TransactionService  (port, std::function callbacks)      │
 │  PackageListRefresh (port)                    │   │  PreparedPlan, ServiceError, parse*() helpers             │
 └───────────────────────────────────────────────┘   │  reused: PlanRequest, ResolvedPlan, PlannedChange,         │
                                                     │  HookPreview, TransactionErrorCode(24), TransactionFailure,│
 Tests: FakeTransactionService, FakePackageListRefresh│  ProgressUpdate, ExecutionResult, ChangedPackage           │
                                                     └───────────────────────────────────────────────────────────┘
```

Dependency direction: `apps/packages` → `src/application` → `src/domain`; the adapter implements a `src/domain` port. `src/application` and `src/domain` never see QtDBus. Only `DBusTransactionService.*` and `HelperWire.*` contain the strings `org.holonight.PackageHelper1` (rule `T-001`, section 13).

## 4. Components

### 4.1 Domain port and value types

Location: `src/domain/include/holonight_packages_domain/transaction_service.h`, `src/domain/src/transaction_service.cpp` (out-of-line virtual destructor, like `PackageSource`). Added to `src/domain/CMakeLists.txt` and to the umbrella header `holonight_packages_domain.h`.

```cpp
#pragma once

#include "holonight_packages_domain/transaction_error.h"
#include "holonight_packages_domain/transaction_plan.h"
#include "holonight_packages_domain/transaction_progress.h"
#include "holonight_packages_domain/transaction_request.h"
#include "holonight_packages_domain/transaction_result.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>

namespace holonight_packages_domain {

// A plan the helper prepared and the caller may confirm. `plan` is the helper's preview: targets, dependency installs,
// removals and orphans as PlannedChange (kind Target/Dependency/Orphan), the hooks, and the action.
struct PreparedPlan {
  std::string plan_id;  // opaque; held in memory only
  ResolvedPlan plan;
  std::chrono::system_clock::time_point expires_at;  // whole seconds, as sent on the wire
};

// Why a call produced no domain answer, or a refusal the helper returned as a normal reply.
struct ServiceError {
  enum class Kind : std::uint8_t {
    Refused,            // a normal reply carrying a code: PreparePlan refusals, Cancel's PlanNotFound/PastPointOfNoReturn
    HelperUnavailable,  // the helper's name has no owner and cannot be activated, or no bus connection
    NoReply,            // timeout, peer vanished, or a reply that cannot be decoded: the outcome cannot be established
    Rejected,           // the bus returned an error reply (a protocol fault); the helper did not act on the call
  };
  Kind kind = Kind::Rejected;
  TransactionFailure failure;  // meaningful when kind == Refused

  bool operator==(const ServiceError&) const = default;
};

using PrepareReply = std::expected<PreparedPlan, ServiceError>;
// A domain failure of Execute (NotAuthorized, PlanExpired, ...) is a successful reply whose outcome is Failed.
using ExecuteReply = std::expected<ExecutionResult, ServiceError>;
using CancelReply = std::expected<void, ServiceError>;

// The port the GUI drives. Implemented by DBusTransactionService (apps/packages) and by FakeTransactionService (tests).
// Contract: every completion callback is called exactly once, on the caller's thread, and never from inside the call
// that started it. The port has no `options` parameter: the adapter always sends an empty map (REQ-C-004).
class TransactionService {
 public:
  using PrepareDone = std::function<void(PrepareReply)>;
  using ExecuteDone = std::function<void(ExecuteReply)>;
  using CancelDone = std::function<void(CancelReply)>;
  // Called for every Progress signal of the plan the service prepared last (REQ-F-044), in arrival order.
  using ProgressHandler = std::function<void(const std::string& plan_id, const ProgressUpdate&)>;

  TransactionService() = default;
  TransactionService(const TransactionService&) = delete;
  TransactionService& operator=(const TransactionService&) = delete;
  TransactionService(TransactionService&&) = delete;
  TransactionService& operator=(TransactionService&&) = delete;
  virtual ~TransactionService();

  virtual void setProgressHandler(ProgressHandler handler) = 0;
  virtual void preparePlan(const PlanRequest& request, PrepareDone done) = 0;
  // Only the plan ID goes over the wire (REQ-F-005).
  virtual void execute(const std::string& plan_id, ExecuteDone done) = 0;
  virtual void cancel(const std::string& plan_id, CancelDone done) = 0;
};

}  // namespace holonight_packages_domain
```

Additions next to the existing `*Name()` functions (these headers are shared with the helper; adding functions is not a change under `apps/helper` or `src/transaction`, REQ-C-002):

```cpp
// transaction_progress.h
[[nodiscard]] std::optional<ProgressPhase> parseProgressPhase(std::string_view name) noexcept;
// transaction_result.h
[[nodiscard]] std::optional<ExecutionOutcome> parseExecutionOutcome(std::string_view name) noexcept;
[[nodiscard]] std::optional<ChangeAction> parseChangeAction(std::string_view name) noexcept;
[[nodiscard]] std::optional<WarningToken> parseWarningToken(std::string_view name) noexcept;
```

Rules: no `QDBus`, no `polkit` (H-001), no `alpm_*` (C-001); comments say "authorization", never the product name of the authorization agent. The header includes only the standard library and other domain headers, so it compiles in a target that does not link QtDBus (REQ-F-038).

### 4.2 Application: `PackageListRefresh` port

Location: `src/application/include/holonight_packages_application/package_list_refresh.h`.

```cpp
namespace holonight_packages_application {

// Asks the package lists to re-read themselves from the package database after a transaction outcome (REQ-F-019).
class PackageListRefresh {
 public:
  PackageListRefresh() = default;
  PackageListRefresh(const PackageListRefresh&) = delete;
  PackageListRefresh& operator=(const PackageListRefresh&) = delete;
  PackageListRefresh(PackageListRefresh&&) = delete;
  PackageListRefresh& operator=(PackageListRefresh&&) = delete;
  virtual ~PackageListRefresh() = default;

  // Fire and forget; called exactly once per Succeeded, Failed, Cancelled or Unknown outcome.
  virtual void refreshAfterOutcome() = 0;
};

}  // namespace holonight_packages_application
```

### 4.3 Application: `TransactionFlow` (state machine)

Location: `src/application/include/holonight_packages_application/transaction_flow.h`, `src/application/src/transaction_flow.cpp`; added to `src/application/CMakeLists.txt`. Qt-free (standard library and domain only). Single-threaded: every method and every service callback runs on the GUI thread.

```cpp
namespace holonight_packages_application {

// REQ-F-007: exactly these eleven states.
enum class FlowState : std::uint8_t {
  Idle, Preparing, Previewed, Refused, Authorizing, Executing,
  Succeeded, Failed, Cancelled, Unknown, HelperUnavailable,
};

// The buttons a result or confirmation page may offer (REQ-F-021..024). The run-time Cancel control of the progress
// page is not here: it is cancelEnabled()/cancelling().
enum class FlowAction : std::uint8_t { Confirm, Cancel, TryAgain, Close };

// What the window may do when the user asks to close it (REQ-F-033..035).
enum class CloseDecision : std::uint8_t {
  AcceptNow,             // nothing to wait for
  DeferUntilCancelReply, // a Cancel call is in flight; closeReady is signalled when it ends
  Ignore,                // past the point of no return: keep the window open
};

// Why a message is shown besides the state's own text.
enum class FlowNotice : std::uint8_t { None, NotAuthorized, CloseUnavailable };

struct ProgressView {
  bool seen = false;  // at least one event applied for this execution
  holonight_packages_domain::ProgressPhase phase = holonight_packages_domain::ProgressPhase::Resolving;
  std::uint32_t current = 0;
  std::uint32_t total = 0;
  std::string item;
};

class TransactionFlow {
 public:
  TransactionFlow(holonight_packages_domain::TransactionService& service, PackageListRefresh& refresh);
  ~TransactionFlow();  // invalidates callbacks still held by the service
  TransactionFlow(const TransactionFlow&) = delete;
  TransactionFlow& operator=(const TransactionFlow&) = delete;
  TransactionFlow(TransactionFlow&&) = delete;
  TransactionFlow& operator=(TransactionFlow&&) = delete;

  void setObserver(std::function<void()> changed);      // after every state or data change
  void setCloseReadyHandler(std::function<void()> ready);  // after a deferred close may proceed

  // User intents. Each returns/does nothing when the current state does not allow it (REQ-F-030).
  bool request(const holonight_packages_domain::PlanRequest& request);  // REQ-F-001, 002
  void confirm();       // Previewed -> Authorizing                              (REQ-F-005, 026)
  void dismiss();       // Previewed: Cancel call, -> Idle; terminal states: -> Idle  (REQ-F-006)
  void cancelRun();     // Authorizing/Executing with cancelEnabled              (REQ-F-013)
  void tryAgain();      // Refused/Failed with Busy, PlanNotFound, PlanExpired    (REQ-F-023)
  [[nodiscard]] CloseDecision closeRequested();  // window close request        (REQ-F-033..036)

  // Read side.
  [[nodiscard]] FlowState state() const noexcept;
  [[nodiscard]] bool allowNewRequest() const noexcept;   // REQ-F-029: true in Idle and the terminal result states
  [[nodiscard]] int openDialogCount() const noexcept;    // 0 in Idle, otherwise 1 (REQ-F-031)
  [[nodiscard]] bool closable() const noexcept;          // REQ-F-032: false in Preparing, Authorizing, Executing
  [[nodiscard]] const holonight_packages_domain::PlanRequest& request() const noexcept;
  [[nodiscard]] const std::optional<holonight_packages_domain::PreparedPlan>& prepared() const noexcept;
  [[nodiscard]] const std::optional<holonight_packages_domain::TransactionFailure>& failure() const noexcept;
  [[nodiscard]] const ProgressView& progress() const noexcept;
  [[nodiscard]] bool cancelEnabled() const noexcept;     // REQ-F-012 (the "cancel flag")
  [[nodiscard]] bool cancelling() const noexcept;        // REQ-F-013
  [[nodiscard]] FlowNotice notice() const noexcept;
  [[nodiscard]] const std::vector<holonight_packages_domain::ChangedPackage>& changed() const noexcept;
  [[nodiscard]] const std::vector<holonight_packages_domain::WarningToken>& warnings() const noexcept;
  [[nodiscard]] std::span<const FlowAction> actions() const noexcept;  // REQ-F-021..024 "exactly Close"
  // Counters for the tests' assertions on idempotence (also used by the controller's diagnostics).
  [[nodiscard]] int refreshRequests() const noexcept;
};

}  // namespace holonight_packages_application
```

Stale-callback protection: every `request()` and `tryAgain()` increments a generation number; each service callback captures `(generation, std::weak_ptr<Alive>)` and is dropped when the generation differs or the flow is gone. The flow tolerates a service that calls back synchronously (the contract forbids it, but a fake might), because every handler mutates state first and notifies last.

Behaviour is defined by the state-machine table in section 5.

### 4.4 Adapter: `DBusTransactionService`

Location: `apps/packages/app/DBusTransactionService.h/.cpp`. Details in section 6.

### 4.5 Wire decoding: `HelperWire`

Location: `apps/packages/app/HelperWire.h/.cpp`. Own copies of the structures that mirror the XML signatures (the helper's `apps/helper/WireTypes.h` is helper-internal and linking it would drag the polkit and libalpm targets into the GUI; REQ-C-002 forbids moving it). Tests prove the two copies agree (section 12.2).

```cpp
namespace helper_wire {

struct ChoiceWire { QString kind; QString subject; QStringList candidates; };           // (ssas)
struct StatusWire { QString code; QStringList names; QList<ChoiceWire> choices; };       // (sasa(ssas)); empty code = success
struct PlanItemWire { QString name, action, old_version, new_version, repository; };     // (sssss)
struct HookItemWire { QString name; bool conditional = false; };                         // (sb)
struct PreviewWire {                                                                      // (sa(sssss)a(sssss)a(sssss)a(sb)x)
  QString action; QList<PlanItemWire> targets, dependencies, orphans; QList<HookItemWire> hooks; qlonglong expires_at = 0;
};
struct ChangeWire { QString name, action, old_version, new_version; };                   // (ssss)
struct ResultWire { QString outcome; StatusWire status; QList<ChangeWire> changed; QStringList warnings; };  // (s(sasa(ssas))a(ssss)as)

// QDBusArgument operators for all of the above, and Q_DECLARE_METATYPE for them and their QList forms.
void registerHelperWireTypes();  // idempotent; qDBusRegisterMetaType for every type

// Pure decoders; they never throw and never return a raw helper string to the caller.
[[nodiscard]] holonight_packages_domain::PrepareReply decodePrepareReply(const QDBusMessage& reply);
[[nodiscard]] holonight_packages_domain::ExecuteReply decodeExecuteReply(const QDBusMessage& reply);
[[nodiscard]] holonight_packages_domain::CancelReply decodeCancelReply(const QDBusMessage& reply);
[[nodiscard]] std::optional<holonight_packages_domain::ProgressUpdate> decodeProgress(
    const QString& phase, uint current, uint total, const QString& item);
// Bus error -> ServiceError kind (section 6.4).
[[nodiscard]] holonight_packages_domain::ServiceError classifyBusError(const QDBusError& error);

}  // namespace helper_wire
```

Decoding rules (REQ-C-004, REQ-F-047):

- `PreparePlan` reply must have 3 arguments `(status, plan_id, preview)`; `Execute` one `(result)`; `Cancel` one `(status)`. Any other count, or a `QDBusArgument` that does not demarshal, gives `ServiceError{NoReply}` (the reply cannot be trusted; for `Execute` this selects the Unknown outcome).
- `StatusWire.code` empty means success. Otherwise `parseTransactionErrorCode(code)`; **a code that is not one of the 24 maps to `InternalError` with no names** (the raw string is dropped, REQ-F-047). Names and NeedsDecision choices are carried for known codes.
- `PreviewWire.action` must equal the requested action name; a mismatch is `NoReply`-class malformed. `targets` become `ChangeKind::Target`, `dependencies` `Dependency`, `orphans` `Orphan`. `expires_at` is epoch seconds (`x`).
- `ResultWire.outcome` through `parseExecutionOutcome`; an unknown outcome string is treated like an unknown code: `Failed` with `InternalError`. `changed[].action` through `parseChangeAction`; an unparseable entry makes the whole reply malformed (`NoReply`, which the flow shows as Unknown) rather than dropping a change silently. Unknown warning tokens are ignored.
- `Progress`: an unknown phase string drops the event.

### 4.6 `TransactionMessages` (message table)

Location: `apps/packages/app/TransactionMessages.h/.cpp`. QtCore only, so it compiles into the QtDBus-free and Quick-free test executables. Detail in section 7.

### 4.7 `TransactionController` and `TransactionRowModel`

Location: `apps/packages/app/TransactionController.h/.cpp`, `apps/packages/app/TransactionRowModel.h/.cpp`. The controller is the "transaction view-model" of the SPEC glossary. It owns a `TransactionFlow`, translates its state into QML properties and texts, and owns three `TransactionRowModel` instances. Registered like the other models (`QML_NAMED_ELEMENT`, `QML_UNCREATABLE`, header listed in `qt_add_qml_module(... SOURCES ...)`).

```cpp
class TransactionController : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(TransactionController)
  QML_UNCREATABLE("TransactionController is provided by the application")

  Q_PROPERTY(State state READ state NOTIFY changed)                          // the 11 states of REQ-F-007
  Q_PROPERTY(Page page READ page NOTIFY changed)                             // which page the popup shows
  Q_PROPERTY(bool dialogOpen READ dialogOpen NOTIFY changed)                 // state != Idle
  Q_PROPERTY(int openDialogCount READ openDialogCount NOTIFY changed)        // REQ-F-031: 0 or 1
  Q_PROPERTY(bool allowAction READ allowAction NOTIFY changed)               // REQ-F-029: Install/Remove controls enabled
  Q_PROPERTY(bool closable READ closable NOTIFY changed)                     // REQ-F-032
  Q_PROPERTY(QString actionKind READ actionKind NOTIFY changed)              // "install" | "remove" | ""
  Q_PROPERTY(QString packageName READ packageName NOTIFY changed)
  Q_PROPERTY(QString title READ title NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)                    // table text for refusal/failure/state
  Q_PROPERTY(QString noticeText READ noticeText NOTIFY changed)              // NotAuthorized / hook warning / close-unavailable
  Q_PROPERTY(QStringList actions READ actions NOTIFY changed)                // subset of "confirm","cancel","tryAgain","close"
  Q_PROPERTY(QString confirmLabel READ confirmLabel NOTIFY changed)          // "Install" | "Remove N package(s)"
  Q_PROPERTY(bool confirmEnabled READ confirmEnabled NOTIFY changed)
  Q_PROPERTY(QDateTime expiresAt READ expiresAt NOTIFY changed)              // REQ-F-003
  Q_PROPERTY(QString expiresText READ expiresText NOTIFY changed)
  Q_PROPERTY(TransactionRowModel* previewRows READ previewRows CONSTANT)     // target / dependency / orphan rows
  Q_PROPERTY(TransactionRowModel* hookRows READ hookRows CONSTANT)           // hook rows with the conditional marker
  Q_PROPERTY(TransactionRowModel* resultRows READ resultRows CONSTANT)       // changed list
  Q_PROPERTY(bool hasHookWarning READ hasHookWarning NOTIFY changed)
  // Progress page (REQ-F-008, 009, 012, 013)
  Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY changed)
  Q_PROPERTY(QString currentItem READ currentItem NOTIFY changed)
  Q_PROPERTY(qreal progressValue READ progressValue NOTIFY changed)          // current / total, 0 when indeterminate
  Q_PROPERTY(bool progressIndeterminate READ progressIndeterminate NOTIFY changed)
  Q_PROPERTY(bool cancelEnabled READ cancelEnabled NOTIFY changed)
  Q_PROPERTY(QString cancelLabel READ cancelLabel NOTIFY changed)            // "Cancel" | "Cancelling."

 public:
  enum class State : quint8 { Idle, Preparing, Previewed, Refused, Authorizing, Executing, Succeeded, Failed, Cancelled, Unknown, HelperUnavailable };
  Q_ENUM(State)
  enum class Page : quint8 { None, Preparing, Confirm, Progress, Result };
  Q_ENUM(Page)

  TransactionController(holonight_packages_domain::TransactionService& service,
                        holonight_packages_application::PackageListRefresh& refresh, QObject* parent = nullptr);

  Q_INVOKABLE bool requestInstall(const QString& name);   // REQ-F-001; false when ignored (REQ-F-030)
  Q_INVOKABLE bool requestRemove(const QString& name);    // REQ-F-002
  Q_INVOKABLE void confirm();                              // REQ-F-005 / 026
  Q_INVOKABLE void dismiss();                              // preview Cancel button, Close button, Escape
  Q_INVOKABLE void cancelRun();                            // progress page Cancel control
  Q_INVOKABLE void tryAgain();                             // REQ-F-023

  // For WindowCloseGuard (not invokable from QML).
  [[nodiscard]] holonight_packages_application::CloseDecision handleCloseRequest();

 signals:
  void changed();     // one coarse signal, like UpdateStatusClient::changed
  void closeReady();  // a deferred close may proceed (Cancel reply received or Cancel failed)
};
```

`State` mirrors `FlowState` one to one (a `static_assert` on the enumerator count and a test that iterates `QMetaEnum` assert REQ-F-007). `Page` mapping: `Idle` to `None`; `Preparing` to `Preparing`; `Previewed` to `Confirm`; `Refused` and `HelperUnavailable` to `Result` (message and actions, no changed list); `Authorizing` and `Executing` to `Progress`; `Succeeded`, `Failed`, `Cancelled`, `Unknown` to `Result`.

`TransactionRowModel` (`QAbstractListModel`, `QML_UNCREATABLE`) has roles `NameRole`, `KindRole` ("target" | "dependency" | "orphan" | "hook" | "changed"), `ActionRole` ("install" | "remove"), `OldVersionRole`, `NewVersionRole`, `RepositoryRole`, `ConditionalRole`. It is reset (not incrementally updated) on every state change that changes its content; its content for a given plan is immutable.

Message construction: `message` is `TransactionMessages::failureText(code, names, action, changedEmpty)` for `Refused` and `Failed`, and `noteText(...)` for the other terminal states. Package names are concatenated into the `QString` as plain text; the QML never interprets it as markup (section 7.3).

### 4.8 `ModelListRefresh`

Location: `apps/packages/app/ModelListRefresh.h/.cpp`; implements `PackageListRefresh` over the two existing models. Section 9.

### 4.9 `WindowCloseGuard`

Location: `apps/packages/app/WindowCloseGuard.h/.cpp`. Section 8.5.

## 5. State machine

### 5.1 States and per-request data

States are the eleven of REQ-F-007. Per-request data held by `TransactionFlow`: `request_` (action and the one name), `prepared_` (plan ID, preview, expiry; present from `Previewed` until the request ends), `failure_`, `progress_`, `cancel_enabled_`, `cancel_pending_` (a `Cancel` call is in flight for the running plan), `close_pending_` (a window close waits for a `Cancel` reply), `notice_`, `changed_`, `warnings_`, `generation_`.

Derived outputs:

| State | Page | `allowNewRequest` (REQ-F-029) | `closable` (REQ-F-032) | Actions offered |
|-------|------|-------------------------------|------------------------|-----------------|
| Idle | none | yes | n/a (no dialog) | n/a |
| Preparing | Preparing | no | no | none (bounded by the 30 s call timeout) |
| Previewed | Confirm | no | yes (Escape = dismiss) | Confirm, Cancel |
| Refused | Result | yes | yes | Try again + Close when the code is `Busy`; otherwise Close |
| Authorizing | Progress | no | no | run-time Cancel control only (`cancelEnabled`) |
| Executing | Progress | no | no | run-time Cancel control only |
| Succeeded | Result | yes | yes | Close |
| Failed | Result | yes | yes | Try again + Close when the code is `Busy`, `PlanNotFound` or `PlanExpired`; otherwise Close |
| Cancelled | Result | yes | yes | Close |
| Unknown | Result | yes | yes | Close (no retry, REQ-F-018) |
| HelperUnavailable | Result | yes | yes | Close |

`openDialogCount()` is 0 in `Idle` and 1 in every other state, because the dialog is one popup whose page changes with the state (section 8.2): a second dialog cannot exist (REQ-F-031).

### 5.2 Cancel flag

`cancel_enabled_` is set to true on entering `Authorizing` (the helper cancels the authorization and initialises no libalpm handle, helper SPEC REQ-F-056). Applying a progress event sets `cancel_enabled_ = cancel_enabled_ && phase in {Resolving, Downloading, Verifying}`: the flag only ever goes from true to false for one execution (**latching**), so a later out-of-order or repeated `Resolving` event cannot re-enable Cancel. It is false from the first `Hooks`, `Installing`, `Removing` or `Done` event (OQ-04). It is false while `cancel_pending_` (the control shows "Cancelling" and is disabled, REQ-F-013), and false in every state other than `Authorizing` and `Executing`. A `PastPointOfNoReturn` reply to `Cancel` also latches it to false (section 5.4).

### 5.3 Transition table

"Guard" is a precondition on the event; "Refresh" marks the exactly-once `refreshAfterOutcome()` call, made after the new state is entered and the observer notified (REQ-F-019).

| # | From | Event | Guard | Actions | To |
|---|------|-------|-------|---------|----|
| T1 | Idle, Refused, Succeeded, Failed, Cancelled, Unknown, HelperUnavailable | `request(r)` | one valid name | `generation_++`; drop old data; `service.preparePlan(r)` | Preparing |
| T2 | Preparing, Previewed, Authorizing, Executing | `request(r)` | none | ignored; returns false (REQ-F-030) | unchanged |
| T3 | Preparing | PrepareReply ok | current generation | store `prepared_`; clear notice | Previewed |
| T4 | Preparing | PrepareReply `Refused(failure)` | current generation | `failure_ = failure` | Refused |
| T5 | Preparing | PrepareReply `HelperUnavailable` | current generation | none | HelperUnavailable (no other call, REQ-F-028) |
| T6 | Preparing | PrepareReply `NoReply` or `Rejected` | current generation | `failure_ = InternalError` | Refused |
| T7 | Previewed | `confirm()` | `prepared_` present | `cancel_enabled_ = true`, clear notice, progress reset; `service.execute(plan_id)` | Authorizing |
| T8 | Previewed | `dismiss()` | none | `service.cancel(plan_id)` (reply ignored); drop `prepared_`; `generation_++` | Idle (REQ-F-006) |
| T9 | Authorizing | Progress event | `plan_id == prepared_.plan_id` | apply (5.5); update cancel flag | Executing |
| T10 | Executing | Progress event | same | apply (5.5); update cancel flag | Executing |
| T11 | Authorizing, Executing | Progress event | other plan ID, or stale | ignored (REQ-F-010) | unchanged |
| T12 | any other state | Progress event | none | ignored (late events) | unchanged |
| T13 | Authorizing, Executing | `cancelRun()` | `cancel_enabled_ && !cancel_pending_` | `cancel_pending_ = true`; `service.cancel(plan_id)` | unchanged (label "Cancelling", control disabled, REQ-F-013) |
| T14 | Authorizing, Executing | CancelReply ok | `!close_pending_` | none; wait for the Execute outcome | unchanged |
| T15 | Authorizing, Executing | CancelReply `Refused(PastPointOfNoReturn)` | none | `cancel_pending_ = false`; `cancel_enabled_ = false`; notice = CloseUnavailable; if `close_pending_`: `close_pending_ = false` | unchanged (REQ-F-036) |
| T16 | Authorizing, Executing | CancelReply `Refused(PlanNotFound)` or any fault | `!close_pending_` | `cancel_pending_ = false`; keep waiting for the outcome | unchanged |
| T17 | Authorizing, Executing | CancelReply ok, `Refused(PlanNotFound)` or any fault | `close_pending_` | `close_pending_ = false`; signal `closeReady` | unchanged (the window now closes) |
| T18 | Authorizing, Executing | ExecuteReply ok, outcome Succeeded | current generation | store `changed_`, `warnings_`; Refresh | Succeeded (HookFailed warning shown with it, REQ-F-017) |
| T19 | Authorizing, Executing | ExecuteReply ok, outcome Failed, code `NotAuthorized` | none | `cancel_pending_ = false`; notice = NotAuthorized; **no Refresh**; keep `prepared_` | Previewed (REQ-F-025) |
| T20 | Authorizing, Executing | ExecuteReply ok, outcome Failed, any other code | none | `failure_`, `changed_`; Refresh | Failed |
| T21 | Authorizing, Executing | ExecuteReply ok, outcome Cancelled | none | `changed_` cleared; Refresh | Cancelled (REQ-F-016) |
| T22 | Authorizing, Executing | ExecuteReply `NoReply` | none | Refresh; never call Execute or PreparePlan again (REQ-F-018) | Unknown |
| T23 | Authorizing, Executing | ExecuteReply `HelperUnavailable` | none | no Refresh (nothing can have changed) | HelperUnavailable |
| T24 | Authorizing, Executing | ExecuteReply `Rejected` or `Refused` | none | `failure_ = InternalError`; Refresh | Failed |
| T25 | Previewed (after T19) | `confirm()` | `prepared_` present | same plan ID; `service.execute(plan_id)`; **no PreparePlan** (REQ-F-026) | Authorizing |
| T26 | Refused, Failed | `tryAgain()` | code in {Busy, PlanNotFound, PlanExpired} | `generation_++`; drop old data; `service.preparePlan(request_)` (same action and names) | Preparing (REQ-F-023, 037) |
| T27 | Refused, Succeeded, Failed, Cancelled, Unknown, HelperUnavailable | `dismiss()` | none | none | Idle |

### 5.4 Window close (`closeRequested()`)

| State at the time | Decision | Actions |
|-------------------|----------|---------|
| Idle, Refused, Succeeded, Failed, Cancelled, Unknown, HelperUnavailable | AcceptNow | none |
| Preparing | AcceptNow | none; **no Cancel call**: the helper discards the plan of a caller that disconnects (REQ-F-034, helper SPEC REQ-F-012) |
| Previewed | DeferUntilCancelReply | `close_pending_ = true`; `service.cancel(plan_id)` (REQ-F-033); the CancelReply is handled by T17 (ok or fault) and the window then closes |
| Authorizing | DeferUntilCancelReply | `close_pending_ = true`; if `!cancel_pending_`, `service.cancel(plan_id)` |
| Executing, `cancel_enabled_` | DeferUntilCancelReply | as Authorizing |
| Executing, `!cancel_enabled_` | Ignore | notice = CloseUnavailable (REQ-F-035); no Cancel call |
| any, `close_pending_` already set | DeferUntilCancelReply | no second Cancel call |

If the CancelReply is `PastPointOfNoReturn` (race between the progress signal and the close), T15 clears `close_pending_`, keeps the window open and shows the close-unavailable message; a later close request is `Ignore` because the flag is now false (REQ-F-036). The `Cancel` call uses the bus default timeout (25 s), so a hung helper delays the exit by at most that long, after which the call fails and the window closes (REQ-F-033 "or the Cancel call has failed").

### 5.5 Applying a progress event

Given the previous applied event `p` (if any) and a new event `e` for the current plan ID: if `p` exists, `e.phase == p.phase` and `e.current < p.current`, the event is ignored (REQ-F-010: `current` never decreases within one occurrence of a phase; `Hooks` can occur twice, but the two occurrences are separated by other phases, so comparing with the previous applied event is exact). Otherwise `progress_` becomes `e`. Events are applied in arrival order, which the adapter preserves because a single connection delivers signals in order.

`progressIndeterminate` is `total == 0`; otherwise `progressValue = current / total` (REQ-F-009). Before the first event of an execution (`!progress_.seen`) the progress page shows the "Authorizing" label and an indeterminate bar.

### 5.6 Notes on selected requirements

- **NotAuthorized returns to previewed**: T19. Confirm stays enabled; the plan ID is unchanged; the helper keeps the plan unconsumed (helper SPEC REQ-F-009). No Refresh is made (REQ-F-019).
- **Busy at start** (REQ-F-037): a `Busy` refusal from the first `PreparePlan` is T4 plus T26 and uses the same table text as any other `Busy`.
- **PlanExpired / PlanNotFound**: arrive as ExecuteReply Failed (T20), offer Try again (T26). The helper also allows re-executing the same plan after `Busy` (helper SPEC REQ-F-009), but the SPEC chooses a fresh `PreparePlan` for `Busy`, and the design follows it.
- **Unknown**: T22. The page has only Close. The list is re-read so the user can see the truth. A new request is allowed after Close (the state is terminal, T1), which is the SPEC's intent of "check the Installed list before doing anything else".

## 6. Adapter design

### 6.1 Class

```cpp
class DBusTransactionService final : public QObject, public holonight_packages_domain::TransactionService {
  Q_OBJECT

 public:
  static constexpr const char* kServiceName = "org.holonight.PackageHelper1";
  static constexpr const char* kObjectPath = "/org/holonight/PackageHelper1";
  static constexpr const char* kInterfaceName = "org.holonight.PackageHelper1.Transactions";

  // Production defaults (REQ-F-041, REQ-F-042). Tests inject shorter values through the constructor only.
  struct Timeouts {
    int prepare_ms = 30'000;     // helper XML: "above 10 seconds"
    int execute_ms = 3'600'000;  // helper XML: "at least one hour"
    int cancel_ms = -1;          // QtDBus default (25 s); deliberately not the one-hour value
  };
  using ConnectionFactory = std::function<QDBusConnection()>;

  explicit DBusTransactionService(ConnectionFactory connect, Timeouts timeouts = {}, QObject* parent = nullptr);

  void setProgressHandler(ProgressHandler handler) override;
  void preparePlan(const PlanRequest& request, PrepareDone done) override;
  void execute(const std::string& plan_id, ExecuteDone done) override;
  void cancel(const std::string& plan_id, CancelDone done) override;

  [[nodiscard]] const Timeouts& timeouts() const noexcept { return timeouts_; }
  [[nodiscard]] static QString progressMatchRule();

 private slots:
  void onProgress(const QString& plan_id, const QString& phase, uint current, uint total, const QString& item);
  ...
};
```

### 6.2 Raw messages, not `QDBusInterface`, not a generated proxy

Decision: build `QDBusMessage::createMethodCall(...)` and call `connection.asyncCall(message, timeout)`, as `DBusUpdateCheckClient` does.

- `QDBusInterface` introspects the remote object **synchronously** in its constructor (blocking the GUI thread, and activating the helper), which breaks REQ-F-043.
- A `qdbusxml2cpp` proxy would need the custom types (`(sasa(ssas))`, `(sa(sssss)...)`) bound to C++ types through `org.qtproject.QtDBus.QtTypeName.*` annotations in the XML. The XML must stay byte-identical (REQ-C-002), so annotations cannot be added, and the proxy would also be generated from a file in `apps/helper`.
- Raw messages make the wire contract explicit: the arguments sent are exactly `(s, as, a{sv})` for `PreparePlan` with an empty `QVariantMap`, and `(s)` for `Execute` and `Cancel` (REQ-C-004). Replies are decoded with `qdbus_cast<StatusWire>` and friends from the registered `helper_wire` types.

### 6.3 Asynchronous calls

```cpp
void DBusTransactionService::preparePlan(const PlanRequest& request, PrepareDone done) {
  QDBusConnection bus = ensureConnection();          // lazily connects the system bus, subscribes to Progress once
  if (!bus.isConnected()) { postUnavailable(std::move(done)); return; }   // QTimer::singleShot(0): never synchronous
  QDBusMessage message = QDBusMessage::createMethodCall(kServiceName, kObjectPath, kInterfaceName, "PreparePlan");
  message << QString::fromUtf8(planActionName(request.action)) << toQStringList(request.names) << QVariantMap{};
  auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, timeouts_.prepare_ms), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, done = std::move(done)](QDBusPendingCallWatcher* w) {
    const QDBusMessage reply = w->reply();
    w->deleteLater();
    PrepareReply decoded = reply.type() == QDBusMessage::ErrorMessage
                               ? std::unexpected(helper_wire::classifyBusError(QDBusError(reply)))
                               : helper_wire::decodePrepareReply(reply);
    if (decoded) { active_plan_id_ = decoded->plan_id; }
    done(std::move(decoded));
  });
}
```

`createMethodCall` leaves bus activation on (the `QDBusMessage` default), so the bus starts the helper on the first call. Every call, including `Cancel`, goes through a `QDBusPendingCallWatcher` parented to the adapter; nothing blocks (REQ-F-043). Destroying the adapter destroys its watchers, so callbacks never outlive it.

### 6.4 Error mapping

`classifyBusError(const QDBusError&)`:

| Bus error | `ServiceError::Kind` |
|-----------|---------------------|
| `QDBusError::ServiceUnknown`; name `org.freedesktop.DBus.Error.NameHasNoOwner`; any name starting `org.freedesktop.DBus.Error.Spawn.` (activation failed: `ServiceNotFound`, `ExecFailed`, `ChildExited`, ...); `NoServer`; a connection that is not connected | `HelperUnavailable` (REQ-F-046) |
| `QDBusError::NoReply`, `QDBusError::Timeout`, `QDBusError::TimedOut`, `QDBusError::Disconnected` | `NoReply` (REQ-F-045). A timeout of the one-hour `Execute` and a helper that dies mid-call (`org.freedesktop.DBus.Error.NoReply` from the bus) both land here. |
| any other error reply (`AccessDenied`, `InvalidArgs`, `UnknownMethod`, ...) | `Rejected` |

`NotAuthorized` and every other domain code never pass through this table: they arrive as normal replies and are decoded as codes (REQ-F-046: the adapter does not report `HelperUnavailable` for a domain code).

How the flow treats each kind (section 5.3): `PreparePlan`: `HelperUnavailable` to the helper-unavailable state; `NoReply` and `Rejected` to `Refused(InternalError)` (nothing was executed). `Execute`: `NoReply` to `Unknown`; `HelperUnavailable` to the helper-unavailable state; `Rejected` to `Failed(InternalError)`. `Cancel`: every fault is a failed cancel; the run continues, or a pending close proceeds.

### 6.5 Wire status to domain code mapping

`StatusWire.code` goes through `parseTransactionErrorCode`; the 24 taxonomy names map to themselves, and anything else maps to `InternalError` without names (REQ-F-047). The names and NeedsDecision choices of a known code are carried unchanged and used only as package names in messages (REQ-F-048 second criterion: no other helper string reaches a dialog). The empty code is success.

### 6.6 Connection and Progress subscription

- The adapter takes a `ConnectionFactory` so the composition root (production: `QDBusConnection::connectToBus(QDBusConnection::SystemBus, "holonight-package-helper-client")`, a **dedicated named connection**) and tests (a connection to the private bus) pick the bus. The factory is invoked lazily on the first `preparePlan`. This keeps application start free of any system-bus traffic from this feature (REQ-NF-008) and makes a missing system bus a `HelperUnavailable` rather than a start failure.
- All three methods and the Progress subscription use the **same** `QDBusConnection` value. A plan belongs to the connection that prepared it (helper README, client contract item 4), so a second connection could never `Execute` or `Cancel` it.
- On the first `preparePlan`, `ensureConnection()` registers the signal handler once:

```cpp
bus.connect(kServiceName, kObjectPath, kInterfaceName, QStringLiteral("Progress"),
            this, SLOT(onProgress(QString, QString, uint, uint, QString)));
```

QtDBus turns this into the match rule `type='signal',sender='org.holonight.PackageHelper1',path='/org/holonight/PackageHelper1',interface='org.holonight.PackageHelper1.Transactions',member='Progress'`. Because the rule names the **well-known name** as sender, the bus (and QtDBus's own owner tracking) delivers only signals sent by the current owner of that name; a second connection that emits a `Progress` with the same plan ID is not delivered (REQ-F-044 first criterion). `progressMatchRule()` returns the same string for documentation and for the second criterion's test (QtDBus does not expose the rule it installed; the behavioural test is what proves it is in force).
- `onProgress` decodes with `decodeProgress` (unknown phase: dropped), compares the plan ID with `active_plan_id_` (set when a `PreparePlan` reply carried a plan) and forwards `(plan_id, update)` to the handler only on a match. The flow filters again (REQ-F-010).
- The match rule is installed before `Execute` is ever sent, so the first `Resolving` event cannot be missed.

### 6.7 Timeouts as constructor-injected defaults

`Timeouts` has the production values as default member initialisers; production code constructs `DBusTransactionService(factory)`. Tests that need a short `Execute` timeout (REQ-F-045) pass `Timeouts{.execute_ms = 300}`. The `PreparePlan` and `Execute` calls read `timeouts_.prepare_ms` / `timeouts_.execute_ms` at the single call site each; the unit tests for REQ-F-041/042 assert `DBusTransactionService::Timeouts{}` and `DBusTransactionService(factory).timeouts()` equal 30 000 / 3 600 000 / -1, and the short-timeout bus test proves the member really reaches the call.

## 7. Message table

### 7.1 Where it lives

`apps/packages/app/TransactionMessages.h/.cpp`, one translation unit, QtCore only.

```cpp
namespace transaction_messages {

// Notes and states that are not helper codes (the SPEC's non-code rows).
enum class Note : quint8 {
  Succeeded, Cancelled, Unknown, HelperUnavailable, HookWarning, NotAuthorized, CloseUnavailable, Cancelling,
  Preparing, Authorizing,
};

// One entry per TransactionErrorCode (24). Names are inserted as plain text. `changed_empty` selects the second
// TransactionFailed row. `action` selects the wording of PackageNotFound (install vs remove).
[[nodiscard]] QString failureText(holonight_packages_domain::TransactionErrorCode code,
                                  const std::vector<std::string>& names, holonight_packages_domain::PlanAction action,
                                  bool changed_empty);
[[nodiscard]] QString noteText(Note note);
[[nodiscard]] QString phaseLabel(holonight_packages_domain::ProgressPhase phase);
[[nodiscard]] QString removeConfirmLabel(int package_count);   // "Remove 1 package" | "Remove N packages"

inline constexpr std::size_t kNoteCount = 10;

}  // namespace transaction_messages
```

### 7.2 How it is made translatable (OQ-03)

The project has no translation pipeline (finding F1), so the table follows what `DataFreshness.cpp` already does: the English source strings are marked with `QT_TRANSLATE_NOOP("TransactionMessages", "...")` in a `constexpr` array indexed by the enumerator (so completeness is checkable, one entry per code), and resolved at display time with `QCoreApplication::translate("TransactionMessages", source)`. When catalogs are added later (OQ-D3), `lupdate` finds the strings with no change here. Placeholders are `%1`, filled with `QString::arg`. Plural forms are two source strings (`Remove 1 package`, `Remove %1 packages`) instead of Qt's `%n`, because without a catalog `%n` would print "package(s)" and REQ-F-004 fixes the visible text. The exact English wording is set in the table in `SPEC.md` ("Message table"); the strings in code realise those intents. Two refinements are made there (Spec corrections SC-6): `PackageNotFound` has an install wording ("not found in the official repositories") and a remove wording ("not installed"); the set stays one entry per code with an action switch inside the entry.

The completeness test (REQ-F-048) iterates `0 .. kTransactionErrorCodeCount-1` and every `Note` and asserts a non-empty text, and also iterates the QML files and C++ sources of the new feature to assert that no dialog text is built from anything but the controller's properties (a source check that `qml/transactions/` has no string literal with a dynamic helper value other than `packageName`, `currentItem` and row roles).

### 7.3 Plain-text rendering (REQ-F-049)

- The controller returns finished `QString`s; names and versions are placeholders filled with `arg()`.
- Every text item in `qml/transactions/` is an `HnLabel { rawText: ... }` (plain text, finding F7) or a `Controls.Button { text: ... }` (button text is plain). No `Text`/`Label` with `textFormat: Text.RichText`, `StyledText`, `AutoText` or `MarkdownText` appears; a source test over `qml/transactions/*.qml` rejects those identifiers and `<`-prefixed tag strings.
- The `MicroHeader` + `showPrefix` role of `HnLabel` (which uses `StyledText`) is not used in the new files.
- A controller test with a package name `<b>x</b>` checks that `message`, the row `name` role and `packageName` hold the literal angle brackets; a QML test loads the dialog with that fake reply and reads `textFormat` of every `HnLabel` (all `Text.PlainText`).

## 8. QML design

### 8.1 Button placement (OQ-01, resolved)

The shared detail header `qml/packages/PackageDetailHeader.qml` already has an action row under the name and badges: a full-width **Remove** button (`packageDetailRemoveButton`, red outline, tooltip "Not implemented yet") followed by a "More options" icon button. Both the Installed detail panel (`PackageDetailPanel`) and the Explore detail panel (`ExploreDetailPanel`) compose `PackageDetailContent`, which composes this header; Explore currently passes `showActions: false`.

Proposal:

- **Remove (Installed page):** keep the existing button in the header's action row, remove the "Not implemented yet" tooltip, and make it act on the header's `name`: `onClicked: root.transactionController.requestRemove(root.name)`.
- **Install (Explore page):** the same action row, with a full-width highlighted `Controls.Button` labelled "Install" (`objectName: "packageDetailInstallButton"`, `highlighted: true`) in the Remove button's place, and no "More options" button (`showMoreOptions: false`). Its Installed indicator follows `currentPackage.isInstalled`, including reloads after a transaction.
- `PackageDetailHeader` gains `property string actionKind: "remove"` (`"remove"` | `"install"`), `property bool showMoreOptions: true` and `property var transactionController: null`. `PackageDetailContent` forwards them. `ExploreDetailPanel` sets `showActions: true`, `actionKind: "install"`, `showMoreOptions: false`.
- **Enablement (REQ-F-029):** `enabled: root.transactionController !== null && root.transactionController.allowAction`. With no controller (the existing QML tests, which never provide one) the buttons stay disabled with the tooltip "Not implemented yet", exactly as today; REQ-NF-008 holds. While a transaction is not idle the tooltip reads "Another package operation is in progress".
- **Scope (REQ-C-003):** Remove exists only where `actionKind == "remove"` (Installed panel), Install only where `actionKind == "install"` (Explore panel); the Updates page has no use of the header. A runtime test asserts the per-page absence.
- An already installed Explore row still offers Install; the helper answers `NothingToDo` for a current package and the table text says so (a client-side hiding rule is not needed, and OQ-06 keeps the label plain).

Property plumbing follows the repository's existing `required property`/pass-down pattern: `WorkspaceWindow.qml` gets `property var transactionController: null` (optional, like `updateCheckModel`) and passes it to `InstalledPackagesView` and `ExploreView`, which pass it to `PackageDetailPanel` / `ExploreDetailPanel`, which pass it to `PackageDetailContent` and `PackageDetailHeader`.

### 8.2 Dialog components

New feature folder `qml/transactions/` (picked up automatically by the `GLOB_RECURSE qml/*.qml` in `apps/packages/CMakeLists.txt` and `tests/CMakeLists.txt`):

| File | Role |
|------|------|
| `TransactionDialog.qml` | The only popup. `Controls.Popup` (modal, centered on `Overlay.overlay`, `objectName: "transactionDialog"`, `width: Math.min(520, parent.width - 48)`, `padding: 20`, `focus: true`). Hosts one `Loader` that switches by `controller.page`. |
| `TransactionConfirmPage.qml` | Title ("Install <name>" / "Remove <name>"), the change list (`ListView` or `Repeater` over `previewRows`: name, kind marker target/dependency/orphan, old to new version, repository), the hook list with the "conditional" marker, the expiry line, the optional `NotAuthorized` notice, and the Confirm and Cancel buttons. |
| `TransactionProgressPage.qml` | One `HnLabel` for the phase label, one for the current item, a `Controls.ProgressBar` (`indeterminate: controller.progressIndeterminate`, `value: controller.progressValue`), and the Cancel control (`enabled: controller.cancelEnabled`, text `controller.cancelLabel`). Nothing else: no text area, no list (REQ-F-011). |
| `TransactionResultPage.qml` | Message label, hook-warning notice, the changed list (`resultRows`) when non-empty, the `Close-unavailable` notice, and the buttons from `controller.actions` (Try again, Close). Used for Refused, HelperUnavailable, Succeeded, Failed, Cancelled and Unknown. |
| `TransactionChangeRow.qml` | One row: name, optional kind badge, version transition, repository. All text via `HnLabel.rawText`. |
| `TransactionActionRow.qml` | The right-aligned button row shared by the pages; buttons are shown only when their id is in `controller.actions`. |

Imports follow the repository rule: `import QtQuick.Controls as Controls`, instances and attached properties qualified (`Controls.Button`, `Controls.ProgressBar`, `Controls.Popup.NoAutoClose`), `Holonight.Core`, `Holonight.Controls`, `HolonightPackages`.

Open and close:

```qml
Controls.Popup {
    id: root
    property var controller: null             // Optional TransactionController
    modal: true
    focus: true
    parent: Controls.Overlay.overlay
    anchors.centerIn: Controls.Overlay.overlay
    width: Math.min(520, root.parent ? root.parent.width - 48 : 520)
    padding: 20
    objectName: "transactionDialog"
    closePolicy: root.controller === null || root.controller.closable ? Controls.Popup.CloseOnEscape : Controls.Popup.NoAutoClose

    Connections {
        target: root.controller
        function onChanged() {
            if (root.controller !== null && root.controller.dialogOpen && !root.opened) root.open()
            else if ((root.controller === null || !root.controller.dialogOpen) && root.opened) root.close()
        }
    }
    Component.onCompleted: if (root.controller !== null && root.controller.dialogOpen) root.open()
    onClosed: if (root.controller !== null && root.controller.dialogOpen) root.controller.dismiss()   // Escape or any user dismissal
}
```

`CloseOnPressOutside` is deliberately excluded, so a stray click cannot discard a preview. Escape is honoured only when `closable` (REQ-F-032); there is no close ("x") button on the progress page. Explicit `focus: true` gives the popup keyboard focus when opened. Escape dismisses confirmation and result pages; preparing, authorizing and execution remain protected by `NoAutoClose`.

`WorkspaceWindow.qml` instantiates `TransactionDialog { controller: root.transactionController; visible: false }` as a child when the controller is not null (`Loader { active: root.transactionController !== null }`), so the existing tests that omit the controller are unaffected.

### 8.3 How actions reach the view-model

`PackageDetailHeader` buttons call `requestInstall(name)` / `requestRemove(name)` on the controller (section 8.1). Dialog buttons call `controller.confirm()`, `controller.dismiss()` (Cancel on the preview, Close on results), `controller.tryAgain()` and `controller.cancelRun()`. No QML code contains transaction logic; the pages are bindings to controller properties.

### 8.4 Enabled and disabled rules in QML

| Control | Binding |
|---------|---------|
| Install / Remove buttons | `controller !== null && controller.allowAction` |
| Confirm | `controller.confirmEnabled` (true in `Previewed`) |
| Progress Cancel | `controller.cancelEnabled`; text `controller.cancelLabel` ("Cancelling" while pending) |
| Try again | visible iff `"tryAgain"` is in `controller.actions` |
| Close | visible iff `"close"` is in `controller.actions` |
| Popup Escape | `closePolicy` per `controller.closable` |

### 8.5 Window close hook

The SPEC asks for a close hook on the window. The page tree's root is a `Rectangle` inside a `QQuickView` (finding F6), so a QML `onClosing` handler has no `Window` to attach to without `Connections` on the private `QQuickCloseEvent` type. The hook is therefore a small C++ event filter on the view, which also keeps the logic unit-testable without QML:

```cpp
class WindowCloseGuard : public QObject {
  Q_OBJECT
 public:
  WindowCloseGuard(TransactionController& controller, QWindow& window);   // installs itself as event filter on `window`
 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;   // on QEvent::Close: ignore + defer, or accept
 private:
  TransactionController* controller_;
  QWindow* window_;
  bool allow_next_close_ = false;
};
```

`eventFilter` handles `QEvent::Close` as follows: if `allow_next_close_` it accepts; otherwise it asks `controller_->handleCloseRequest()`: `AcceptNow` lets the event through; `DeferUntilCancelReply` calls `event->ignore()`, and on `TransactionController::closeReady` the guard sets `allow_next_close_ = true` and calls `window_->close()`, which passes the filter and ends the application (the default `quitOnLastWindowClosed`); `Ignore` calls `event->ignore()` (the controller has already set the close-unavailable notice, which the open dialog shows, REQ-F-035). Because `QQuickView` windows deliver `QEvent::Close` for both the window manager's close and `QWindow::close()`, one hook covers both. The test sends a `QCloseEvent` with `QCoreApplication::sendEvent`, which is not pointer or focus automation (REQ-NF-004, C-010).

## 9. List refresh mechanism

REQ-F-019 needs a re-read from libalpm after every Execute outcome (`Succeeded`, `Failed`, `Cancelled`, `Unknown`) and none after a preparation refusal or `NotAuthorized`.

- Port: `PackageListRefresh::refreshAfterOutcome()` (section 4.2), called by `TransactionFlow` exactly once per outcome, after the new state is entered and the observer notified (so the result message is in place before any list change).
- Adapter: `ModelListRefresh` in `apps/packages/app` holds `InstalledPackagesModel*` and `ExploreModel*`.

```cpp
class ModelListRefresh final : public QObject, public holonight_packages_application::PackageListRefresh {
  Q_OBJECT
 public:
  ModelListRefresh(InstalledPackagesModel& installed, ExploreModel& explore, QObject* parent = nullptr);
  void refreshAfterOutcome() override;
 private:
  void refreshInstalled();   // refresh() now, or once the running load finishes (finding F3)
  ...
};
```

`refreshInstalled()` calls `InstalledPackagesModel::refresh()` when `status() != Loading`; otherwise it sets a `pending_` flag and, on the next `statusChanged` that leaves `Loading`, calls `refresh()` once more. This closes the single-flight hole (finding F3): without it, a refresh requested while an earlier load is running would be dropped and the list could show pre-transaction content. The same adapter calls `ExploreModel::reload()` (also single-flight; the same pending rule applies), so the Explore row badge and selected detail indicator reflect the refreshed installed state. The Install label and per-page action scope remain unchanged; the helper decides `NothingToDo`. This second call is the accepted OQ-D1 default and uses an existing public method.

Visible effect: `InstalledPackagesModel::refresh()` sets `Status::Loading`, so the Installed page shows its loading state for the duration of the re-read and the selection is cleared by the model reset; the transaction dialog is a popup above the page tree and is unaffected. This is existing model behaviour and is not changed (REQ-NF-008).

Counting: tests use `FakePackageListRefresh` (counter) with the flow (REQ-F-019 criteria 1 and 2) and a `ModelListRefresh` test with a real `InstalledPackagesModel` over `MockPackageSource` for the single-flight case.

## 10. Composition-root wiring

`apps/packages/app/PackagesApplication.cpp` (`PackagesApplication` is the composition root; the constructor already builds all sources and models):

```cpp
// after explore_model_ is created
helper_wire::registerHelperWireTypes();
transaction_service_ = std::make_unique<DBusTransactionService>([] {
  return QDBusConnection::connectToBus(QDBusConnection::SystemBus, QStringLiteral("holonight-package-helper-client"));
});
list_refresh_ = std::make_unique<ModelListRefresh>(*installed_packages_model_, *explore_model_);
transaction_controller_ = std::make_unique<TransactionController>(*transaction_service_, *list_refresh_);
...
// after view_ is created and before show()
view_->setInitialProperties({..., {QStringLiteral("transactionController"), QVariant::fromValue(transaction_controller_.get())}});
close_guard_ = std::make_unique<WindowCloseGuard>(*transaction_controller_, *view_);
```

`PackagesApplication.h` gains `std::unique_ptr<DBusTransactionService> transaction_service_; std::unique_ptr<ModelListRefresh> list_refresh_; std::unique_ptr<TransactionController> transaction_controller_; std::unique_ptr<WindowCloseGuard> close_guard_;`. The destructor keeps its explicit order: `view_` first (it holds QML references), then `close_guard_`, `transaction_controller_`, `list_refresh_`, `transaction_service_`, then the existing resets. `PackagesApplication.cpp` is already excluded from rule `C-001`, and the new code adds no libalpm name.

`apps/packages/CMakeLists.txt`: add the new `.h/.cpp` files to `qt_add_executable(holonight-packages ...)`; add `app/TransactionController.h app/TransactionRowModel.h` to `qt_add_qml_module(... SOURCES ...)`; `Qt6::DBus` is already linked. `scripts/check-qmltypes.sh` gains `TransactionController` in `required_types`.

## 11. CMake targets

| Target | Kind | Sources | Links |
|--------|------|---------|-------|
| `holonight_packages_domain` | existing static | + `src/transaction_service.cpp` | unchanged |
| `holonight_packages_application` | existing static | + `src/transaction_flow.cpp` | unchanged (Core, Concurrent, domain) |
| `holonight-packages` | existing executable | + `DBusTransactionService`, `HelperWire`, `TransactionController`, `TransactionRowModel`, `TransactionMessages`, `ModelListRefresh`, `WindowCloseGuard` | unchanged (already has `Qt6::DBus`) |
| `test_holonight_packages_checks` | existing (GTest, libalpm-free, **no QtDBus, no Quick**) | + `application/transaction_flow_test.cpp`, `domain/transaction_service_test.cpp`, `apps/transaction_messages_test.cpp`, `apps/TransactionMessages.cpp` | unchanged; + CTest `holonight_packages_checks_no_qtdbus` (`ldd` must not show `libQt6DBus`), mirroring `holonight_packages_checks_no_libalpm` |
| `test_holonight_packages_transactions` (new) | GTest + QtTest, Quick/Qml, **no QtDBus** | `transactions/main.cpp`, `transactions/transaction_controller_test.cpp`, `transactions/transaction_row_model_test.cpp`, `transactions/transaction_dialog_view_test.cpp`, `transactions/window_close_guard_test.cpp`, `transactions/transaction_isolation_test.cpp`, plus `TransactionController.cpp`, `TransactionRowModel.cpp`, `TransactionMessages.cpp`, `WindowCloseGuard.cpp`, `ModelListRefresh.cpp`, `InstalledPackagesModel.cpp`, `ExploreModel.cpp` (compiled directly, existing idiom) | `GTest::gtest GTest::gmock Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test Qt6::Concurrent holonight_package_icons holonight_packages_application holonight_packages_domain`; CTest `holonight_packages_transactions_no_qtdbus`; `gtest_discover_tests(... TEST_PREFIX "holonight_transactions." PROPERTIES TIMEOUT 30 ENVIRONMENT QT_QPA_PLATFORM=offscreen)` |
| `test_holonight_packages_helper_client` (new) | GTest + QtTest + **private dbus-daemon** | `helper_client/main.cpp`, `helper_client/fake_helper_object.h`, `helper_client/dbus_transaction_service_test.cpp`, `helper_client/helper_contract_test.cpp`, plus `DBusTransactionService.cpp`, `HelperWire.cpp` | `GTest::gtest GTest::gmock Qt6::Core Qt6::DBus Qt6::Test holonight_packages_domain holonight_package_helper_service holonight_transaction_test_support`; includes `${CMAKE_CURRENT_SOURCE_DIR}/packaged` for `private_bus.h`; `gtest_discover_tests(... TEST_PREFIX "holonight_helper_client." PROPERTIES TIMEOUT 60)` |
| `test_holonight_packages` | existing | + `apps/helper_wire_test.cpp`, `apps/helper_error_classification_test.cpp`, `apps/model_list_refresh_test.cpp`, + `DBusTransactionService.cpp`, `HelperWire.cpp`, `ModelListRefresh.cpp` (pure, bus-free tests) | unchanged (already links `Qt6::DBus`) |
| `test_runtime_controls` | existing compiled QML acceptance | + `runtime/test_transaction_dialogs.cpp`, + `TransactionController.cpp`, `TransactionRowModel.cpp`, `TransactionMessages.cpp`, `ModelListRefresh.cpp` | unchanged; runs twice via the existing `runtime_controls_Holonight` and `runtime_controls_Fusion` CTests |

Test doubles (header only, namespace `holonight_packages_testing`): `tests/application/fake_transaction_service.h`, `tests/application/fake_package_list_refresh.h`.

## 12. Testing design

### 12.1 Layer 1: state machine and view-model, no bus

- `FakeTransactionService` records every `preparePlan(request)`, `execute(plan_id)`, `cancel(plan_id)` call in order and holds their completion callbacks until the test releases them: `completePrepare(PrepareReply)`, `completeExecute(ExecuteReply)`, `completeCancel(CancelReply)`, `emitProgress(plan_id, ProgressUpdate)`. It never calls back from inside a call, as the port contract says. Helpers build common replies (`previewOf(...)`, `refusal(code, names)`, `failed(code, changed)`).
- `transaction_flow_test.cpp` (GTest, in `test_holonight_packages_checks`): one test per transition row T1 to T27 and per close-table row, enumerating the 24 codes for refusals (REQ-F-021, 023, 024), the cancel-flag matrix over all seven phases (REQ-F-012), progress regression and wrong-plan filtering (REQ-F-010), refresh counts (REQ-F-019), `Execute` call counts (REQ-F-020), the open-dialog counter across the full sequence (REQ-F-031). Waiting is by signal-free direct calls: the flow has no timers and no event loop, so no `sleep` and no timing dependency exists at all (REQ-NF-001).
- `transaction_controller_test.cpp`: property mapping, enum completeness through `QMetaEnum`, `actions` lists per state, `confirmLabel` counts (REQ-F-004), row models (REQ-F-003), progress properties (REQ-F-008, 009), plain-text names (REQ-F-049), `handleCloseRequest` plumbing. Waiting is with `QSignalSpy::wait(bounded ms)` only where a signal is expected.
- `transaction_dialog_view_test.cpp`: source-based QML test (loads `qml/transactions/*.qml` from `HOLONIGHT_QML_SOURCE_DIR`, like `explore_view_test.cpp`), with a real controller over the fake service; reads item properties; rejects QML warnings; checks the progress page has no text-area or list item (REQ-F-011), `closePolicy` per state (REQ-F-032), `textFormat` of all labels.
- `transaction_isolation_test.cpp` (REQ-NF-002): runs a full install and a full remove through the fake with isolated `HOME`/`XDG_*` directories (the idiom of `tests/runtime/main.cpp`) and compares a before/after manifest of those directories.
- All new test files use the controller or flow and the fake only: no `QTest::mouse*`, `touch*`, `key*`, `forceActiveFocus` (REQ-NF-004); a source test greps the new test files for those names.

### 12.2 Layer 2: adapter on a private bus

`tests/helper_client/dbus_transaction_service_test.cpp` (every `TEST_F` derives from `HelperBusFixture`, whose `SetUp()` is `ASSERT_TRUE(bus_.start()) << bus_.error()`), using `holonight_packages_testing::PrivateBus` from `tests/packaged/private_bus.h` (a `dbus-daemon --session` on a temporary socket; never the system bus: REQ-NF-005, the tests contain no `systemBus` and the factory returns `QDBusConnection::connectToBus(bus_.address(), ...)`).

Two fake helpers:

1. `FakeHelperObject` (scripted): a `QDBusVirtualObject` registered at `/org/holonight/PackageHelper1` on a second connection that owns `org.holonight.PackageHelper1`. It reads `HOLONIGHT_HELPER_DBUS_XML` (already a PUBLIC compile definition of `holonight_package_helper_service`), parses the three methods' `in` argument signatures and **rejects a call whose signature differs** with `org.freedesktop.DBus.Error.InvalidArgs` (REQ-C-004 first criterion: "rejects any extra argument"). It scripts per method: reply at once, reply after N ms, never reply, reply with a given `StatusWire`/`ResultWire`, and emits `Progress` signals on demand with `QDBusConnection::send`. It counts calls per method.
2. The real `HelperService` (from `holonight_package_helper_service`) over `PlanService` with `FakeTransactionEngine`, `FakeAuthorizer`, `FakePlanIdGenerator` from `tests/support`, exactly as `tests/helper/helper_dbus_test.cpp` builds it. `helper_contract_test.cpp` runs the adapter against it for a prepare, a denied execute, an allowed execute with progress, and a cancel: the **decode test of REQ-C-004 against the helper's own encoder** (`toWire(...)` in `apps/helper/WireTypes.cpp`), so the two independent copies of the wire structures cannot drift.

Test list (all on the private bus):

| Test | Proves |
|------|--------|
| `TimeoutsDefault` | `DBusTransactionService(factory).timeouts()` is 30 000 / 3 600 000 / -1 (REQ-F-041, 042) |
| `ExecuteUsesInjectedTimeoutAndReportsNoReply` | fake holds `Execute` past `Timeouts{.execute_ms = 300}`; adapter reports `NoReply`, fake counted one `Execute`, no further call (REQ-F-045) |
| `NoReplyWhenHelperDiesMidCall` | fake connection dropped while `Execute` pending: `NoReply` |
| `CancelUsesDefaultTimeout` | `Cancel` while `Execute` is held completes through the bus default; asserts `cancel_ms != execute_ms` (REQ-F-042) |
| `CallsDoNotBlockTheEventLoop` | fake holds `PreparePlan` for 2 s; a 100 ms `QTimer` fires at least 10 times first (REQ-F-043) |
| `ProgressOnlyFromTheHelperName` | an unrelated third connection emits `Progress` with the current plan ID: zero events; the fake helper's emissions arrive (REQ-F-044) |
| `ProgressMatchRuleText` | `progressMatchRule()` contains the interface, `member='Progress'` and the well-known name |
| `ProgressForOtherPlanIsDropped` | an event with another plan ID does not reach the handler |
| `UnavailableWhenNoOwner` | no helper on the bus: `HelperUnavailable` for `PreparePlan`; `NotAuthorized` and other codes never map to it (REQ-F-046) |
| `UnknownCodeBecomesInternalError` | reply code `SomethingElse` becomes `InternalError` with no names (REQ-F-047) |
| `AllTwentyFourCodesRoundTrip` | each of the 24 names decodes to itself (REQ-F-047) |
| `SendsOnlyTheDeclaredArguments` | the adapter's three calls pass the XML-driven signature check (REQ-C-004) |

`tests/apps/helper_wire_test.cpp` and `helper_error_classification_test.cpp` (no bus, in `test_holonight_packages`): the structure signatures from `QDBusMetaType::typeToSignature` equal the XML `type` attributes; `decode*Reply` on hand-built `QDBusMessage::createReply(...)` values; every bus error name and `QDBusError::ErrorType` in section 6.4 maps to its kind.

### 12.3 `dbus-daemon` availability and the missing-daemon rule (REQ-NF-005, REQ-NF-006)

- Availability: `dbus-daemon` is already required and present for `tests/packaged` and `tests/helper` in the pinned CI image (finding F5); this design adds no new dependency. If `task check` is green today it is green for the daemon.
- Missing daemon: the fixture's `ASSERT_TRUE(bus_.start())` fails the test with the message "cannot start dbus-daemon; install the dbus package and make it available on PATH". Every `TEST_F` in `test_holonight_packages_helper_client` is a `HelperBusFixture`, so with the daemon missing **no test in that binary passes**. A `WILL_FAIL` CTest proves it:

```cmake
add_test(NAME helper_client_requires_dbus_daemon
  COMMAND ${CMAKE_COMMAND} -E env PATH=/nonexistent $<TARGET_FILE:test_holonight_packages_helper_client>)
set_tests_properties(helper_client_requires_dbus_daemon PROPERTIES WILL_FAIL TRUE TIMEOUT 120
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LD_LIBRARY_PATH=$<TARGET_FILE_DIR:HoloNight::Config>")
```

  The bus-free adapter tests (wire signatures, error classification) live in `test_holonight_packages`, outside this binary, so they do not dilute this check.
- Private bus: the daemon listens on a temporary socket chosen by `dbus-daemon --session --print-address`; the fixture connects with `connectToBus(address, uniqueName)`. `DBUS_SESSION_BUS_ADDRESS` is not used and the system bus is never contacted.

### 12.4 Layer 3: QML and runtime, both styles

`tests/runtime/test_transaction_dialogs.cpp` is added to `test_runtime_controls`, which already runs under `QT_QUICK_CONTROLS_STYLE=Holonight` and `Fusion` as CTests `runtime_controls_Holonight` and `runtime_controls_Fusion`. Each test names the controller call that drives it (REQ-NF-004) and the fake service reply it uses:

| Test | Drives with | Asserts |
|------|-------------|---------|
| `ConfirmPageInstall` | `requestInstall("app")`, `completePrepare(preview with target app, dep lib)` | two rows, repository, expiry, Confirm label "Install" |
| `ConfirmPageRemoveCount` | `requestRemove("app")`, preview with orphans `lib`, `util` | Confirm "Remove 3 packages" |
| `ProgressPageDeterminate` / `Indeterminate` | `confirm()`, `emitProgress(Downloading 3/10 foo)` then `(Resolving 0/0)` | bar value 0.3 then indeterminate, labels, no text-area/list item |
| `ProgressCancelDisabledFromHooks` | `emitProgress(Hooks ...)` | Cancel control disabled |
| `ResultPages` | `completeExecute(...)` per outcome | message and rows; one test for each of Succeeded (+hook warning), Failed, Cancelled, Unknown, HelperUnavailable, each refusal action set |
| `ButtonsDisabledWhileBusy` | each of the four busy states | every Install and Remove control in Explore and Installed detail views is disabled; enabled in idle and terminal states |
| `PerPageControls` (REQ-C-003) | page switch | no Install on Installed, no Remove on Explore, none on Updates |
| `NoQmlWarnings` | every state above | the engine's `warnings` signal never fires |

The `HolonightPackages` module in `test_runtime_controls` is the compiled copy of the repository QML (`qt_add_qml_module` over `HOLONIGHT_PACKAGES_QML_FILES`), so the new files are exercised as compiled QML. The controller type is registered with `qmlRegisterUncreatableType` in the test, as `explore_view_test.cpp` does for `ExploreModel`.

### 12.5 Other checks

- `scripts/check-layering.py` rule `T-001` and its fixtures (section 13).
- The policy fixtures, `task qml-import-check`, `task qmltypes-check` and `task qml-lint` run unchanged and cover the new QML.
- `task check` (REQ-NF-007) is the closing automated gate. The manual VM checklist (REQ-C-005) is the closing human gate.

## 13. Layering policy additions

`scripts/check-layering.py` gets one rule and a fixture pair in `tests/policy/test_layering_policy.py`:

```python
Rule("T-001", "the helper's bus names appear only in the GUI adapter, its wire decoding and tests",
     rx(r"org\.holonight\.PackageHelper1"),
     include=("src/domain", "src/application", "apps/packages", "qml"),
     exclude=("apps/packages/app/DBusTransactionService.h", "apps/packages/app/DBusTransactionService.cpp",
              "apps/packages/app/HelperWire.h", "apps/packages/app/HelperWire.cpp")),
```

Fixtures: `t001-helper-name-in-controller` (violation in `apps/packages/app/TransactionController.cpp`), `legit-adapter-names-helper` (clean). The rule realises REQ-F-040's source-search criterion ("only in the adapter and in tests"; `tests/` is outside the rule's scope). H-001 already realises the src/domain and src/application half of REQ-F-038 and REQ-F-039 (Spec corrections SC-1).

The existing C-007 `--base` comparison (nothing under `qml/packages` and `qml/explore` may change) belongs to the earlier online-update feature; `task layering-check` does not pass `--base`, so it is not enforced today, but it contradicts this feature if someone does pass it. The design removes the `qml/packages`/`qml/explore` half of that base check (keeping the `Updates` XML half) in the same change (OQ-D4).

## 14. Documentation amendments (REQ-C-001, REQ-C-005)

### 14.1 `AGENTS.md` (tracked)

- In "Style and tests", replace the sentence "Production code in the GUI and in `holonight-packaged` never runs package transactions." by: "Production code in `holonight-packaged` never runs package transactions. The GUI only requests install and remove transactions from `holonight-package-helper` through the `TransactionService` port (adapter in `apps/packages`); it never calls libalpm or pacman to change packages." The new text must not contain the stale phrase `never run package transactions` (rule C-006 rejects it: note `never runs package transactions` with the `s` is accepted but is avoided anyway for the GUI).
- Leave the line "Carve-out: tests may run package transactions only against a fixture root ..." byte-identical (C-006 needs exactly one such line).
- In "Structure", add `qml/transactions` to the feature-scoped QML list is implicit; add one sentence: "The GUI's helper client (`DBusTransactionService`) is the only code that names `org.holonight.PackageHelper1`." and mention `apps/packages/README.md` (manual VM checklist).

### 14.2 `CLAUDE.md` (gitignored; the edit is local and not committed)

Replace "The GUI is currently **transactionally inert**: Installed, Updates and Explore pages, no install, remove or update actions. The privileged helper that will perform install and remove exists but is not wired to the GUI yet." by: "The GUI requests install (Explore) and remove (Installed) of one package at a time from `holonight-package-helper` through the `TransactionService` port; the Updates page has no transaction action and there is no system upgrade." Add to the architecture list: the `TransactionService` port in `src/domain`, `TransactionFlow` in `src/application`, the adapter and controller in `apps/packages/app`, `qml/transactions`, and the test targets of section 11. CLAUDE.md is read by rule C-006 when present, so its carve-out line must stay verbatim as well.

### 14.3 Also stale (not named by the SPEC)

- `README.md` line 15 says the app is "transactionally inert"; line 7 and 12 describe the pages as read-only. Update them to describe Install/Remove through the helper and keep the Updates statement. (Spec corrections SC-3.)
- `CHANGELOG.md` "Unreleased / Added": one entry for the feature (user-visible behaviour change, per AGENTS.md "update user documentation when behavior changes").

### 14.4 `apps/packages/README.md` (new)

Contents: what the GUI does; how to run it (`task run`) and that Install/Remove need the helper installed and a polkit authentication agent running; the three code locations (port, flow, adapter); a pointer to `docs/sdd/gui-package-transactions/`; and the section **`Manual verification: package transactions`** containing the seven items of REQ-C-005 as an unchecked list, each with an empty `result: / date:` slot, in the format already used by `apps/helper/README.md` ("(passed YYYY-MM-DD)" / "(failed on ..., fixed and passed ...)"). A person fills it in on the disposable VM; the cycle is closed only when all seven read "passed" (REQ-C-005).

## 15. Spec corrections

Checked against the code, `apps/helper/dbus/org.holonight.PackageHelper1.xml`, `apps/helper/README.md` and `docs/sdd/package-helper-transactions/SPEC.md`. The design takes the corrected reading in each case.

**Verified without change.** The 24 codes in the SPEC (REQ-F-047, REQ-F-048, message table) equal the 24 rows of the helper SPEC "Error taxonomy" and the 24 enumerators of `TransactionErrorCode` (`InvalidArgument`, `PackageNotFound`, `PackageIgnored`, `ProtectedPackage`, `BreaksDependents`, `NeedsDecision`, `PartialUpgradeRequired`, `InsecureSigLevel`, `SyncDatabaseStale`, `DownloadFailed`, `FileConflicts`, `DiskSpace`, `SignatureInvalid`, `DatabaseUnavailable`, `ConfigurationError`, `NothingToDo`, `PlanNotFound`, `PlanExpired`, `PlanChanged`, `NotAuthorized`, `Busy`, `PastPointOfNoReturn`, `TransactionFailed`, `InternalError`). The XML itself lists no codes (its comments say a status "carries a code from the error taxonomy"); the codes come from the helper SPEC and `src/domain`. Helper cross-references in the GUI SPEC: REQ-F-009 (plan unconsumed after `NotAuthorized` or `Busy`) correct; REQ-F-012 (disconnect discards a plan that is not executing) correct; REQ-F-031 (one transaction at a time) correct; REQ-F-049 (phases, `current` non-decreasing) correct; REQ-F-051 (structured result) correct; REQ-F-056, 057, 058 (cancel before the commit point, commit-point definition, refusal after it) correct; REQ-F-071 (plan limits, `Busy`) correct. **Mismatch: REQ-F-047.** GUI SPEC Risk 4 cites it for "a helper killed mid-transaction can leave `db.lck`, so every later transaction is `Busy`". Helper REQ-F-047 is "lock held elsewhere means `Busy`; the helper never removes a lock it did not create"; the stale-lock-after-kill behaviour is described in `apps/helper/README.md` (client contract, last paragraph) and in the helper terminator, not in REQ-F-047. The conclusion (stale lock gives `Busy`) is right; the citation should read "helper SPEC REQ-F-047 and `apps/helper/README.md`".

| ID | SPEC text | Conflict | Design reading |
|----|-----------|----------|----------------|
| SC-1 | REQ-F-038 acceptance: a source search of `src/domain` finds no `QDBus`, `polkit`, `alpm` or `Q_DBUS`. REQ-F-039: a source search of `src/application` finds no `QDBus`, `polkit`, `alpm` or `D-Bus`. | Literal case-insensitive substring searches fail on existing, legitimate comments: `src/domain/.../transaction_plan.h` ("libalpm hook order"), `transaction_request.h`, `transaction_error.h`, `transaction_engine.h`, `src/domain/.../update_checker.h` ("D-Bus and logs"), `src/application/.../terminator.h` and `src/application/src/plan_service.cpp` (both mention libalpm). The repository's own enforcement is `scripts/check-layering.py` rules H-001 (`QDBus`, `<QtDBus`, `polkit`) and C-001 (`<alpm.h>`, `alpm_*`, `dbpath`, `databasePath`). | The acceptance is "`task layering-check` passes with rules H-001 and C-001 covering `src/domain` and `src/application`, plus the port header compiles in `test_holonight_packages_checks`, which does not link QtDBus (`ldd` guard)". New domain/application code and comments avoid the words polkit, QDBus and D-Bus. |
| SC-2 | REQ-C-001 acceptance: a search of `apps/packages` and `src/application` finds no "libalpm, pacman or `alpm_trans` call". | `apps/packages/app/PackagesApplication.cpp` legitimately composes `AlpmPackageSource`, `AlpmExploreSource`, `AlpmUpdateSource` and uses `kDefaultPacmanRoot`, `kDefaultPacmanConfPath`; a literal "pacman" or "libalpm" search fails today. | The acceptance is rule C-005 (`alpm_trans_*`, `pacman -S/-U/-R` forbidden in `src`, `apps`, `qml`, `tests`) plus C-001, both run by `task layering-check`. |
| SC-3 | REQ-C-001 / OQ-12 name `AGENTS.md` and `CLAUDE.md`, saying they state the GUI is "transactionally inert". | `AGENTS.md` does not say "inert"; its text is "Production code in the GUI and in `holonight-packaged` never runs package transactions." The words "transactionally inert" are in `CLAUDE.md` and in **`README.md` (line 15)**, which the SPEC does not name. `CHANGELOG.md` has no entry for this feature. `check_carve_out` in `scripts/check-layering.py` constrains the wording (exactly one carve-out line; the phrase `never run package transactions` is forbidden). | Section 14: amend `AGENTS.md`, `CLAUDE.md` (local, gitignored), `README.md` and `CHANGELOG.md`, keeping the carve-out line verbatim. |
| SC-4 | OQ-08: "the helper's success status code and the exact field layout of the `Cancel` status are not pinned". | They are pinned by the code: success is an **empty** `code` string in `StatusWire` (`okStatus()` is `StatusWire{}`), for `PreparePlan` (with an empty `plan_id` and an empty `PreviewWire` on refusal), `Cancel` and the status inside a non-Failed `ResultWire`. `Cancel` returns `(sasa(ssas))`. `PreviewWire.expires_at` is epoch **seconds** (`x`). Outcome strings are `Succeeded`, `Failed`, `Cancelled`; change actions are `install`/`remove`; plan item actions are `install`/`remove`. | Closed by the decoders in section 4.5; no pending literal remains. |
| SC-5 | Message table and REQ-F-013/036: `Cancel` can return success or `PastPointOfNoReturn`. | The helper SPEC (end of "Error taxonomy") says `Cancel` returns success, `PlanNotFound` **or** `PastPointOfNoReturn`; the GUI SPEC never says what to do with `PlanNotFound` from `Cancel`. | T14/T16/T17: `PlanNotFound` from `Cancel` is a no-op (the run continues until the `Execute` outcome) and, for a pending window close, lets the window close. |
| SC-6 | Message table, `PackageNotFound`: "A package was not found in the official repositories". `NothingToDo`: "already installed at the current version". | `PackageNotFound` is also the code for a **remove** target that is not installed (helper SPEC taxonomy, REQ-F-034); the text would be false for Remove. `NothingToDo` is install-only (helper REQ-F-023), so its text is right. | `PackageNotFound` gets an action-aware wording (section 7.2). Still one entry per code (REQ-F-048 unaffected). |
| SC-7 | REQ-F-023: Try again for `Busy`, `PlanNotFound`, `PlanExpired` "where the code of a refusal at preparation or of a failed `Execute`". | `PreparePlan` can only return `InvalidArgument`, `PackageNotFound`, `PackageIgnored`, `ProtectedPackage`, `BreaksDependents`, `NeedsDecision`, `PartialUpgradeRequired`, `InsecureSigLevel`, `DatabaseUnavailable`, `ConfigurationError`, `NothingToDo`, `Busy`, `InternalError` (helper SPEC, end of taxonomy); `PlanNotFound` and `PlanExpired` come only from `Execute`. REQ-F-021's list also omits `Busy` (it is covered by REQ-F-023). | No behavioural change: T26 is guarded by the code and applies in both `Refused` and `Failed`; the REQ-F-021 test enumerates the PreparePlan code set above plus `Busy`. |
| SC-8 | REQ-F-045/046 and the table: `Unknown` (no reply) and `HelperUnavailable`. | The SPEC has no category for a bus **error reply** (for example `AccessDenied` or `InvalidArgs`) or an undecodable reply. | New `ServiceError::Kind::Rejected` (error reply: nothing was executed, shown as `InternalError`) and undecodable replies counted as `NoReply` (shown as Unknown for `Execute`, because the transaction may have run). Section 6.4. |
| SC-9 | Glossary "Point of no return" and REQ-F-012. | The glossary lists `Done` as "the first Hooks, Installing or Removing progress phase of a transaction, and Done" and the helper's commit point is the first of {PreTransaction hook start, transaction proper start} (helper REQ-F-057). `Hooks` can occur twice (helper REQ-F-049), and a progress event can reach the GUI after the commit point already passed. | The cancel flag **latches** to false (section 5.2). A `PastPointOfNoReturn` reply to `Cancel` is a normal event (T15). |
| SC-10 | REQ-F-010: "`current` never decreases within a phase ... the view-model ignores the regression". | `Hooks` can occur twice per transaction, so "within a phase" must mean one occurrence. | Rule in section 5.5: ignore an event only if it has the same phase as the previously applied event and a smaller `current`. |
| SC-11 | REQ-F-041/042: "a unit test of the adapter reads the timeout applied to the call". | QtDBus offers no read-back of the timeout of a pending call. | The adapter stores `Timeouts`, exposes `timeouts()`, and the call sites read only that member; a short-timeout bus test proves the member reaches the call (section 6.7). |
| SC-12 | REQ-F-044: "the match rule string written by the adapter contains ...". | QtDBus builds the rule internally and exposes no read-back. | `progressMatchRule()` returns the documented string; the negative bus test (unrelated sender) proves the sender restriction is in force. |
| SC-13 | REQ-F-033/034/035 and the task text say the close hook is the "window `onClosing`". | The page root is a `Rectangle` inside a `QQuickView`; there is no QML `Window` to host `onClosing`. | C++ `WindowCloseGuard` event filter (section 8.5). Same behaviour, testable without QML. |
| SC-14 | REQ-NF-006: "no test in the run is reported as passed". | A binary that also contains bus-free tests would report some as passed. | Bus-free adapter tests are placed in `test_holonight_packages`; every test in `test_holonight_packages_helper_client` needs the bus; the `WILL_FAIL` CTest of section 12.3 enforces the rule. |
| SC-15 | REQ-F-019: refresh "the Installed list". | The Explore page shows an "Installed" badge from the same package state; it would go stale. `InstalledPackagesModel::refresh()` is single-flight and would silently drop a request made during a load. | The refresher also reloads the Explore index and repeats a dropped request (section 9); listed as OQ-D1. |
| SC-16 | SPEC OQ-01 default ("a single action button in each detail view's header"). | The Installed header already has the Remove button. | Wire the existing button; add Install in the same slot on Explore (section 8.1). OQ-01 is closed by this design. |

## 16. Key decisions

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | The port is callback-based plain C++ (`std::function`), not a `QObject` with signals. | `src/domain` must not link QtDBus and stays Qt-light; callbacks keep `TransactionFlow` Qt-free and testable with GTest, without an event loop (REQ-NF-001, REQ-F-038/039). `PlanService` in `src/application` already uses the same callback style. |
| D2 | Reuse the existing domain transaction types; add `PreparedPlan`, `ServiceError` and four `parse*` functions only. | They are the helper's own vocabulary, already tested, with the 24 codes. A second set would drift. |
| D3 | Split the view-model into `TransactionFlow` (application, Qt-free) and `TransactionController` (apps, QML-facing). | Mirrors `UpdateCheckService` / `UpdateCheckModel`. The state machine is exercised by fast GTest; the controller only maps state to properties and text. |
| D4 | One popup with page switching instead of separate confirmation and progress dialogs. | REQ-F-031 (at most one dialog) holds by construction; there is no open/close race between two popups. |
| D5 | Raw `QDBusMessage` + `asyncCall` + `QDBusPendingCallWatcher`; no `QDBusInterface`, no generated proxy. | `QDBusInterface` introspects synchronously; a proxy needs XML annotations, which REQ-C-002 forbids. Same idiom as `DBusUpdateCheckClient`. |
| D6 | Own copy of the wire structures in `apps/packages`, cross-checked by a contract test against the helper's `HelperService`. | Linking `apps/helper` code would pull polkit and libalpm into the GUI; the contract test removes the drift risk the duplication creates. |
| D7 | Dedicated, lazily connected system-bus `QDBusConnection` supplied by a factory. | One connection for calls and the signal subscription (plan ownership is per connection); no bus traffic at start-up; a missing system bus is `HelperUnavailable`, not a crash; tests inject a private bus. |
| D8 | `Execute` faults: `NoReply` is Unknown, `Rejected` is a failure, an undecodable reply is Unknown. | An unknown outcome must never be shown as "nothing changed". |
| D9 | The cancel flag latches false. | A late or repeated `Resolving` event must not re-enable a Cancel the helper will refuse. |
| D10 | Window close is a C++ event filter with a deferred-close handshake. | The root item is not a `Window` (F6); the logic needs a unit test. |
| D11 | No translation catalogs added; strings are marked with `QT_TRANSLATE_NOOP` and resolved through `QCoreApplication::translate`. | There is no pipeline (F1); the same pattern is in `DataFreshness.cpp`. |
| D12 | A new `T-001` layering rule instead of relying on text search. | Mechanical, part of `task layering-check`, with fixtures, and replaces literal grep acceptances that fail on existing comments (SC-1, SC-2). |

## 17. Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Port as `QObject` with Qt signals/slots | Pulls Qt types into `src/domain`; the flow could no longer be tested without a Qt event loop. |
| `QDBusInterface` with `callWithCallback` | Synchronous introspection on construction; hides the wire contract; harder to inject errors. |
| `qt_add_dbus_interface` / `qdbusxml2cpp` proxy from the helper XML | Needs QtTypeName annotations absent from the XML; the XML is frozen (REQ-C-002); couples the GUI build to `apps/helper`. |
| Share `apps/helper/WireTypes.h` with the GUI | Drags `holonight_package_helper_service` (polkit, libalpm) into the GUI target; the helper must stay unchanged. |
| Single `QObject` view-model containing the state machine | Larger test surface under Qt; the QtDBus-free, Quick-free state-machine target (REQ-NF-001) would need Qml integration headers. |
| Separate `ConfirmDialog` and `ProgressDialog` popups | Two popups to coordinate; REQ-F-031 would depend on correct sequencing. |
| Subscribe to `Progress` per plan, or on a second connection | The helper sends `Progress` only to the plan owner's unique name; a second connection never receives it. |
| QML `Connections` to `Window.window` `onClosing` | The root is not a `Window`; the handler would be untestable without a real window and relies on the private close-event type. |
| Install the match rule in the adapter constructor | Contacts the bus at start-up (REQ-NF-008) and fails when there is no system bus. |
| Re-execute the same plan after `Busy` instead of re-preparing | Allowed by the helper (REQ-F-009), but REQ-F-023 and REQ-F-037 specify Try again as a new `PreparePlan`; the design follows the SPEC. |
| Hide the Install button for installed packages | OQ-06/NothingToDo already cover it; client-side hiding duplicates helper policy. |
| Disable Confirm client-side after `expires_at` | Rejected by OQ-09's default: the helper decides; avoids clock-skew behaviour. |

## 18. Risks

| # | Risk | Mitigation |
|---|------|------------|
| R1 | The two copies of the wire structures drift. | `helper_contract_test.cpp` runs the adapter against the real `HelperService`; signature assertions against the XML. |
| R2 | `InstalledPackagesModel::refresh()` drops a request during a load; the list shows stale content. | `ModelListRefresh` repeats a dropped request (section 9); tested with a real model. |
| R3 | A refresh clears the Installed selection and flashes the loading state. | Existing model behaviour; the dialog is a separate popup. Accepted; recorded in section 9. |
| R4 | A `Cancel` call to a hung helper delays window exit up to the 25 s bus default. | Documented in 5.4; the call then fails and the window closes (REQ-F-033). |
| R5 | `QEvent::Close` filter misses a platform close path. | Test covers `QWindow::close()` and a sent `QCloseEvent`; the manual checklist item 6 covers the real window manager. |
| R6 | `Hooks` or `Installing` progress arrives after the user pressed Cancel, producing a late `PastPointOfNoReturn`. | Handled as a normal event (T15), tested. |
| R7 | The preview dialog is dismissed by Escape and the `Cancel` call fails silently. | The reply is ignored by design (REQ-F-006): the helper expires the plan after 300 s and discards it at disconnect. Plan limits (16 per UID, helper REQ-F-071) are not at risk from one-at-a-time use. |
| R8 | A stale system `Busy` (leftover `db.lck`) blocks every action. | The `Busy` text offers Try again and names the cause generically; documented in the helper README; checklist item covers it indirectly. |
| R9 | `qmllint` or `qmltypes-check` rejects the new type or QML. | `TransactionController` added to `required_types`; QML follows the existing import rules; run `task qml-lint` and `task qml-import-check`. |
| R10 | The `--base` form of the layering check would flag the `qml/packages` and `qml/explore` edits. | Not used by `task`; the stale half is removed (OQ-D4). |
| R11 | A user closes a modal in `Preparing` impossible for up to 30 s. | The call is bounded by the 30 s timeout; the window can still be closed (REQ-F-034). Accepted default OQ-D2. |
| R12 | Plain-text guarantee is broken by a later QML change. | A source test rejects rich-text formats in `qml/transactions/`; controller test with markup in a name. |

## 19. Resolved decisions

SPEC items closed here: **OQ-01** (section 8.1), **OQ-03** (finding F1: no translation pipeline; use `QT_TRANSLATE_NOOP` + `translate`), **OQ-08** (SC-4: empty code is success). SPEC defaults kept as written: OQ-02 (no queue UI), OQ-06 (Install label stays "Install"), OQ-07 (no remedy), OQ-09 (no client expiry check), OQ-10 (closing during `preparing` exits without `Cancel`; consistent with helper REQ-F-012). Design-level defaults accepted in SPEC OQ-13:

- **OQ-D1 — Accepted.** Reload the Explore index after every outcome (SC-15), so the "Installed" badge and Install offer stay correct.
- **OQ-D2 — Accepted.** The `Preparing` page has no dismiss control before the 30 s timeout; window close still works.
- **OQ-D3 — Accepted.** Translation catalogs and `lupdate` remain outside this feature (F1).
- **OQ-D4 — Accepted.** Remove the stale `qml/packages`/`qml/explore` half of the C-007 base check in `scripts/check-layering.py`.
- **OQ-D5 — Accepted.** Show an Execute `Rejected` bus error as `InternalError` ("nothing was changed"), because a rejected call was not executed.

## 20. Traceability

Components: DOM = domain port (4.1); FLOW = `TransactionFlow` (4.3); CTL = `TransactionController` + `TransactionRowModel` (4.7); ADP = `DBusTransactionService` (6); WIRE = `HelperWire` (4.5); MSG = `TransactionMessages` (7); REF = `ModelListRefresh` (9); CLOSE = `WindowCloseGuard` (8.5); QML = `qml/transactions/*` and header changes (8). Tests: L1 = `transaction_flow_test` / controller tests; L2 = helper_client bus tests; L3 = runtime/QML tests; POL = policy/layering; DOC = repository documents; MAN = manual VM checklist.

| ID | Realised by | Verified by |
|----|-------------|-------------|
| REQ-F-001 | FLOW T1, CTL `requestInstall`, QML Install button | L1 flow test (call args), L3 |
| REQ-F-002 | FLOW T1, CTL `requestRemove`, QML Remove button | L1 |
| REQ-F-003 | CTL `previewRows`/`hookRows`/`expiresAt`, QML confirm page | L1 controller, L3 `ConfirmPageInstall` |
| REQ-F-004 | CTL `confirmLabel`, MSG `removeConfirmLabel` | L1 controller, L3 `ConfirmPageRemoveCount` |
| REQ-F-005 | FLOW T7, DOM `execute(plan_id)` | L1 (single argument), L2 `SendsOnlyTheDeclaredArguments` |
| REQ-F-006 | FLOW T8 | L1 |
| REQ-F-007 | FLOW `FlowState`, CTL `State` | L1 (QMetaEnum count, transition coverage) |
| REQ-F-008 | FLOW T9/T10, CTL `phaseLabel`/`currentItem`, MSG | L1, L3 |
| REQ-F-009 | CTL `progressValue`/`progressIndeterminate` | L1, L3 |
| REQ-F-010 | FLOW T11, 5.5; ADP `onProgress` plan filter | L1, L2 `ProgressForOtherPlanIsDropped` |
| REQ-F-011 | QML progress page content | L3 (no text-area/list), `transaction_dialog_view_test` |
| REQ-F-012 | FLOW 5.2 (latching flag) | L1 (phase matrix), L3 |
| REQ-F-013 | FLOW T13, CTL `cancelLabel`/`cancelEnabled` | L1, L3 |
| REQ-F-014 | FLOW T18, CTL `resultRows` | L1, L3 `ResultPages` |
| REQ-F-015 | FLOW T20, MSG `failureText` | L1, L3 |
| REQ-F-016 | FLOW T21, MSG `Cancelled` | L1 |
| REQ-F-017 | FLOW T18 + `warnings`, CTL `hasHookWarning`, MSG `HookWarning` | L1, L3 |
| REQ-F-018 | FLOW T22; ADP `NoReply`; no retry action | L1, L2 `ExecuteUsesInjectedTimeoutAndReportsNoReply` |
| REQ-F-019 | FLOW (refresh on T18, T20, T21, T22, T24), DOM-adjacent port `PackageListRefresh`, REF | L1 (counter), `model_list_refresh_test` |
| REQ-F-020 | FLOW T7, T25 | L1 |
| REQ-F-021 | FLOW T4, MSG, CTL `actions` | L1 (PreparePlan code set), L3 |
| REQ-F-022 | MSG `PartialUpgradeRequired`, CTL `actions` = Close | L1, L3 |
| REQ-F-023 | FLOW T26 | L1, L3 |
| REQ-F-024 | FLOW actions table (5.1) | L1 (every code) |
| REQ-F-025 | FLOW T19 | L1 |
| REQ-F-026 | FLOW T25 | L1 |
| REQ-F-027 | FLOW T20 + T26, MSG `PlanExpired` | L1 |
| REQ-F-028 | FLOW T5, T23; ADP `HelperUnavailable` | L1, L2 `UnavailableWhenNoOwner` |
| REQ-F-029 | FLOW `allowNewRequest`, CTL `allowAction`, QML button `enabled` | L1, L3 `ButtonsDisabledWhileBusy` |
| REQ-F-030 | FLOW T2 | L1 |
| REQ-F-031 | FLOW `openDialogCount`, QML single popup (D4) | L1 (counter across the sequence) |
| REQ-F-032 | FLOW `closable`, CTL `closable`, QML `closePolicy` | L1, L3 |
| REQ-F-033 | FLOW 5.4, CLOSE | L1, `window_close_guard_test` |
| REQ-F-034 | FLOW 5.4 (Preparing: AcceptNow, no Cancel) | L1, `window_close_guard_test` |
| REQ-F-035 | FLOW 5.4 (Ignore), notice CloseUnavailable, MSG | L1, L3 |
| REQ-F-036 | FLOW T15, 5.4 | L1 |
| REQ-F-037 | FLOW T4 + T26, MSG `Busy` | L1 |
| REQ-F-038 | DOM (4.1) | POL (H-001, C-001), `ldd` guard on the checks binary |
| REQ-F-039 | FLOW uses DOM only | POL (H-001), flow test links only the port and fakes |
| REQ-F-040 | ADP, WIRE; composition root (10) | POL `T-001` |
| REQ-F-041 | ADP `Timeouts::prepare_ms` | L2 `TimeoutsDefault` |
| REQ-F-042 | ADP `Timeouts::execute_ms`, `cancel_ms` | L2 `TimeoutsDefault`, `CancelUsesDefaultTimeout` |
| REQ-F-043 | ADP `QDBusPendingCallWatcher` | L2 `CallsDoNotBlockTheEventLoop` |
| REQ-F-044 | ADP 6.6 (sender-restricted match rule, same connection) | L2 `ProgressOnlyFromTheHelperName`, `ProgressMatchRuleText` |
| REQ-F-045 | ADP 6.4, FLOW T22 | L2 `ExecuteUsesInjectedTimeoutAndReportsNoReply`, L1 |
| REQ-F-046 | ADP 6.4 | L2 `UnavailableWhenNoOwner`, `helper_error_classification_test` |
| REQ-F-047 | WIRE decoders (6.5) | L2 `UnknownCodeBecomesInternalError`, `AllTwentyFourCodesRoundTrip` |
| REQ-F-048 | MSG constexpr table | `transaction_messages_test`, L1 source check |
| REQ-F-049 | MSG, CTL strings, QML `HnLabel.rawText` | L1 controller (markup name), L3 (`textFormat`), QML source check |
| REQ-NF-001 | FLOW (Qt-free), `test_holonight_packages_checks` | `holonight_packages_checks_no_qtdbus`, bounded spies, no sleeps |
| REQ-NF-002 | no persistence code in any new component | `transaction_isolation_test` (manifest compare) |
| REQ-NF-003 | QML on `Controls.Popup`, styles via the existing runtime harness | L3 in `runtime_controls_Holonight` and `runtime_controls_Fusion`, `NoQmlWarnings` |
| REQ-NF-004 | all tests drive CTL and the fake only | POL (C-010) plus a source grep over the new test files |
| REQ-NF-005 | `PrivateBus` fixture | L2 (source grep: no `systemBus`) |
| REQ-NF-006 | `HelperBusFixture::SetUp` assertion | CTest `helper_client_requires_dbus_daemon` (`WILL_FAIL`) |
| REQ-NF-007 | all | `task check` |
| REQ-NF-008 | nullable `transactionController`, lazy bus connection, lazy subscription | existing QML tests unchanged; L3; fake-service call count is zero after page load |
| REQ-C-001 | FLOW/ADP only request; no libalpm in `apps/packages` beyond the composition root | POL (C-005, C-001), DOC (`AGENTS.md`, `CLAUDE.md`, `README.md`) |
| REQ-C-002 | no file under `apps/helper`, `src/transaction`, or the XML is edited | `git diff --stat <base> -- apps/helper src/transaction` is empty; XML hash check |
| REQ-C-003 | QML header `actionKind`, per-panel wiring (8.1) | L3 `PerPageControls` |
| REQ-C-004 | ADP raw messages, WIRE decoders | L2 `SendsOnlyTheDeclaredArguments`, `helper_contract_test` |
| REQ-C-005 | `apps/packages/README.md` section `Manual verification: package transactions` (14.4) | MAN (seven items on a disposable VM) |
