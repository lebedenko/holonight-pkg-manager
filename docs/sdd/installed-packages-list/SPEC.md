# Installed Packages List Specification

**Feature**: Read-only, in-process enumeration and display of currently-installed Arch Linux packages through the domain → backends → application → UI layer stack.

**Status**: Architecture proof-of-concept (vertical slice)

**Date**: 2026-08-11

---

## Non-Goals

The following are explicitly **out of scope** for this specification and will not be implemented in this iteration:

- Package transactions (install, remove, upgrade)
- D-Bus or system services
- User authentication or privilege escalation
- Privileged helper process or daemon
- Shell integration or CLI tools
- AUR (Arch User Repository) package search or RPC
- Available-version checking or update detection
- Package search, filtering, or advanced sorting (only alphabetical by name)
- Retry mechanisms for database access failures
- Network access or remote package data
- Trust/security level or package verification details

---

## Glossary

**libalpm**: Arch Linux Package Management library — the C library underlying `pacman`, providing package database enumeration, query, and transaction APIs.

**Sync Database**: The repository metadata database(s) configured in pacman, typically located in `/var/lib/pacman/sync/` (e.g., `community.db`, `core.db`, `extra.db`). Contains package metadata for packages available to install.

**Local Database**: libalpm's internal database of installed packages, typically at `/var/lib/pacman/local/`. Contains the state of packages already installed on the system.

**Official Package**: A package present in at least one configured sync (repository) database. Even if installed, it has a known upstream source in the configured repositories.

**Foreign Package**: A package installed locally but absent from all configured sync databases. This indicates a package installed outside the normal repository system (e.g., built locally via `makepkg`, installed from `.pkg.tar.zst` directly, or from a repository no longer in pacman's config). **Note**: This is libalpm's local-vs-sync distinction, *not* AUR detection via AUR RPC — the system does not query AUR.

**Install Reason**: libalpm's categorization of *why* a package was installed:
- **Explicit**: User explicitly requested installation.
- **Dependency**: Installed automatically as a dependency of another package.

---

## Functional Requirements

### REQ-F-001: Domain Model — Package Data Type

**Ubiquitous**: The domain library (`holonight_packages_domain`) shall define a `Package` data type as a C++ class or struct with **exactly** the following public fields:

- `identity`: a unique system identifier (string)
- `name`: the human-readable package name (string)
- `installedVersion`: the installed version string (string)
- `sourceType`: an enum indicating `Official` (package found in a configured sync database) or `Foreign` (locally installed, not in any sync database)
- `repository`: the repository name from which the package originated (string; may be empty for Foreign packages)
- `installReason`: an enum indicating `Explicit` (user explicitly installed) or `Dependency` (installed as a dependency)
- `backendSpecificId`: the raw libalpm package identifier, used internally to correlate back to the backend (string)

**Constraint**: The `Package` type shall not include fields for `availableVersion` or `trustLevel` or any other metadata not explicitly listed above. Unused fields with no producer are considered a code smell and are forbidden in this iteration.

**Acceptance Criteria**:
- A reviewer can inspect `src/domain/holonight_packages_domain/package.hpp` and confirm all seven fields are present and only those fields.
- A C++ unit test can construct a `Package` instance, assign values to all seven fields, and read them back correctly.
- Compilation fails if a ninth field is added or if a listed field is removed.

---

### REQ-F-002: Backend Adapter — libalpm Integration

**Ubiquitous**: The backends library (`holonight_packages_backends`) shall provide a C++ adapter (class or factory function) that enumerates all installed packages via libalpm.

**Constraint**: The adapter's constructor or factory function shall accept **explicit parameters** for:
- The pacman database root directory (e.g., `/` or `/` in production, or `/path/to/test-fixture` in tests)
- The pacman database directory (e.g., `var/lib/pacman` or `test-fixtures/pacman`)

The adapter shall **not** hardcode these paths to `/var/lib/pacman` or `/`. This ensures test isolation: tests shall use a fixture pacman database checked into `tests/fixtures/pacman/` (or similar) instead of the live system database.

**Constraint**: The adapter shall map libalpm's `pmpkg_t` package objects to the domain `Package` type:
- `identity` ← a unique identifier (libalpm name or handle)
- `name` ← libalpm `alpm_pkg_get_name()`
- `installedVersion` ← libalpm `alpm_pkg_get_version()`
- `sourceType` ← `Official` if the package is found in any configured sync database; `Foreign` otherwise
- `repository` ← the sync database name (empty string for Foreign packages)
- `installReason` ← libalpm `alpm_pkg_get_reason()` mapped to `Explicit` or `Dependency`
- `backendSpecificId` ← the libalpm package name (for backend-to-domain correlation)

When a package appears in multiple discovered sync databases, repository names shall be ordered lexically and the
first selected. A missing sync directory means no configured official repositories; filesystem, registration, and
corrupt-database failures shall be reported rather than silently classifying packages as Foreign.

**Acceptance Criteria**:
- A C++ unit test instantiates the adapter with paths pointing to `tests/fixtures/pacman/` and calls an enumeration method that returns a `std::vector<Package>` or similar.
- The test verifies that each returned `Package` has all seven fields correctly populated from the fixture database.
- The test does not require live system pacman installation or read `/var/lib/pacman`.
- A second test verifies that passing an invalid (non-existent) database root path results in a graceful error (not a crash).

---

### REQ-F-003: Application Layer — Use Case / View Model

**Ubiquitous**: The application library (`holonight_packages_application`) shall expose a use case or view-model class that:
- Accepts the backend adapter as a dependency (constructor injection or factory parameter)
- Provides a method (e.g., `getInstalledPackages()` or similar) that returns the list of `Package` domain objects
- Sorts the returned packages **alphabetically by `name`** (ascending order)

**Constraint**: The application layer shall not perform UI serialization or QML-specific transformations; it shall return domain `Package` objects only.

**Acceptance Criteria**:
- A C++ unit test creates the use case with a mock backend adapter, calls the list-retrieval method, and verifies the returned packages are sorted alphabetically by name.
- A second test verifies that if the backend returns packages named "zebra", "apple", "banana", the use case returns them in order: "apple", "banana", "zebra".
- The use case can be instantiated and used without any QML, Qt, or UI framework present.

---

### REQ-F-004: UI Display — Installed Packages List

**Ubiquitous**: The QML user interface shall display the list of installed packages in a scrollable, vertical list view. Each package entry shall display:
- The package **name**
- The installed **version** string
- A visible indicator of the source (**Official** vs **Foreign** — plain text is sufficient, e.g., "official" or "foreign")

**Constraint**: The QML interface shall *not* use hardcoded or mock data. All data shall be sourced live from the application layer use case through the complete domain → backends → application chain.

**Constraint**: The list shall be scrollable and shall accommodate package counts ranging from zero (empty list) to hundreds of packages without UI lag or truncation (see REQ-NF-002 for performance requirements).

**Acceptance Criteria**:
- A QML component or window can be instantiated with an application-layer use case instance passed via a property binding.
- `WorkspaceWindow` and `InstalledPackagesView` require a typed `InstalledPackagesModel` property supplied through
  `QQuickView::setInitialProperties()`; no procedural singleton or context property is used.
- The model role carrying the installed version is named `installedVersion`.
- Visual inspection or automated snapshot testing confirms that the component displays at least 10 test packages with name, version, and source label visible.
- Removing or modifying the hardcoded mock data in QML (if any exists) causes a test to fail, confirming the list is live.
- A test with zero packages shows an empty (but valid) list view, not an error message or placeholder text (see REQ-F-008 for error vs empty-list distinction).

---

### REQ-F-005: Asynchronous Loading — Non-Blocking UI

**Event-driven**: When the user opens the window or requests a package list refresh, the system shall load the package database asynchronously, not blocking the Qt event loop or UI rendering thread.

**State-driven**: While packages are being loaded from the backend, the UI shall display a distinct loading state (e.g., a spinner, progress indicator, or "Loading..." message) that is visually distinguishable from both the success state (loaded list) and the error state.

**Constraint**: This specification requires that loading is asynchronous and that a loading state is shown. The specific mechanism (QtConcurrent, QThread, QObject slot/signal, separate worker object, or other) is a **Design-stage decision** and is *not* prescribed by this specification. The implementation shall choose an approach appropriate to Qt 6 and the codebase architecture.

**Acceptance Criteria**:
- An automated or manual test confirms that initiating a package list load does not block user input (e.g., button clicks, text input) for more than 16 ms (one frame at 60 Hz).
- Visual inspection or snapshot testing confirms a distinct loading indicator appears while packages are being enumerated and disappears once the list is displayed.
- A test with a mock backend that delays enumeration by 1 second verifies that the UI remains responsive during that delay and updates to show the list upon completion.

---

### REQ-F-006: Error Handling — Graceful Degradation

**Unwanted-behaviour**: If the backend fails to open or read the package database (e.g., invalid root path, permission denied, corrupted database, libalpm initialization failure), the application shall not crash.

**State-driven**: When a database access error occurs, the UI shall display a distinct, visible error state with a human-readable error message (e.g., "Failed to load packages: <error details>").

**Constraint**: No automatic retry or fallback mechanism is required or in scope.

**Acceptance Criteria**:
- A test passes an invalid database root path (e.g., `/nonexistent/path/`) to the backend and verifies the application returns an error status without crashing.
- A test with a mock backend that simulates permission-denied on database access confirms the error is propagated to the UI and displayed.
- Visual inspection or snapshot testing confirms the error message is displayed in the UI with different styling/layout than the success list view.
- A separate test (REQ-F-007) confirms that an empty-but-valid database (zero installed packages) is visually distinct from this error state.

---

### REQ-F-007: Empty List vs Error State

**Unwanted-behaviour**: If the package database is valid but contains no installed packages, the system shall display an empty list, not an error message.

**Constraint**: An empty result (zero packages, valid database) must be visually and functionally distinguishable from an error state (database access failure).

**Acceptance Criteria**:
- A test with a fixture database containing zero packages runs the use case and verifies it returns an empty vector/list with no error status.
- Visual snapshot testing confirms the UI displays a blank/empty scrollable area with no error message or error styling.
- A second snapshot comparing the empty-list view with the error-state view confirms they appear different (e.g., empty list has no red background or error icon; error state does).

---

### REQ-F-008: Build Target Conversion

**Ubiquitous**: The CMake targets `holonight_packages_domain`, `holonight_packages_backends`, and `holonight_packages_application` shall be converted from `INTERFACE` libraries to `STATIC` libraries.

**Constraint**: This conversion shall occur as part of the implementation, since each target receives real `.cpp` files for the first time (not stub headers only). The usage requirements in `CMakeLists.txt` in dependent targets (e.g., the executable linking to `holonight_packages_application`) shall be updated to match (remove `INTERFACE` and treat as traditional `STATIC` library).

**Acceptance Criteria**:
- Inspection of `src/domain/CMakeLists.txt`, `src/backends/CMakeLists.txt`, and `src/application/CMakeLists.txt` confirms each defines `add_library(...STATIC)` instead of `add_library(...INTERFACE)`.
- A `cmake --build` invocation produces `.o` or `.a` files (static object/archive) for each target, not just interface definitions.
- The executable target `holonight-packages` links successfully to all three targets with no configuration errors.

---

## Non-Functional Requirements

### REQ-NF-001: Test Isolation — Fixture Database

**Ubiquitous**: All unit tests that exercise the backend adapter shall use a checked-in fixture pacman database (located in `tests/fixtures/pacman/` or similar), not the live system database at `/var/lib/pacman`.

**Constraint**: The fixture database shall be independent of the host system's installed packages and shall not be modified by tests.

**Acceptance Criteria**:
- A test runs in a CI environment (container, VM, or sandboxed host) where no pacman installation exists and passes without error.
- A second test runs on a developer machine with a different set of installed packages than the fixture and returns the fixture's expected list, not the system's installed packages.
- Inspection of test code confirms all database paths use `tests/fixtures/` or a build-time variable, not hardcoded `/var/lib/pacman`.

---

### REQ-NF-002: UI Responsiveness — Non-Blocking Enumeration

**Event-driven**: When the package enumeration completes (or when an error occurs), the UI shall update within 100 ms of the backend reporting completion, without stuttering or frame drops.

**Constraint**: The specific async mechanism is a design decision (REQ-F-005); this requirement only constrains the observable responsiveness, not the implementation.

**Acceptance Criteria**:
- A performance test measures the time from "enumeration complete" signal to "list displayed on screen" and confirms it is ≤100 ms.
- Visual/automated frame-rate monitoring during a load operation confirms the main thread does not stall for more than one frame (16.7 ms at 60 Hz).

---

### REQ-NF-003: Field Validation — No Unused Metadata

**Ubiquitous**: The `Package` domain type shall contain only the seven fields listed in REQ-F-001. No additional fields (e.g., `availableVersion`, `trustLevel`, `description`, `size`) shall be added.

**Constraint**: Unused fields with no live data producer are a code smell and indicate over-design. Fields shall be added only when a concrete data producer and consumer are committed to the same SDD cycle.

**Acceptance Criteria**:
- A code review of the `Package` type confirms exactly seven public fields.
- A compiler check (e.g., exhaustive pattern match or static_assert on field count) would fail if an eighth field is added.
- Documentation or comments on the type explain why each field is present (what produces it, what uses it).

---

## Constraints

### REQ-C-001: Dependency Isolation — Explicit Paths, No Hardcoding

**Ubiquitous**: The backend adapter shall not hardcode database paths. Instead, all paths shall be provided as constructor/factory parameters and propagated through all internal libalpm initialization calls.

**Rationale**: Enables test isolation and deployment flexibility (e.g., containerized environments with non-standard DB locations).

**Acceptance Criteria**:
- Source code inspection confirms no occurrence of `"/var/lib/pacman"` or `"/"` as hardcoded string literals in backend implementation.
- A test instantiates the adapter with arbitrary root and DB paths and verifies they are used (by mock inspection, test logs, or by pointing to a fixture database).

---

### REQ-C-002: CMake Standard — STATIC Library Targets

**Ubiquitous**: The three domain, backends, and application targets shall be CMake `STATIC` libraries (not `INTERFACE`, `HEADER_ONLY`, or custom target types).

**Constraint**: This aligns with the existing codebase convention (stated in CLAUDE.md) of converting `INTERFACE` to `STATIC` when `.cpp` files are first added.

**Acceptance Criteria**:
- Inspection of CMakeLists.txt in each module confirms `add_library(holonight_packages_domain STATIC ...)` syntax.
- Dependent targets link via `target_link_libraries(...PRIVATE holonight_packages_domain)` or similar, not `target_link_libraries(...INTERFACE ...)`.

---

### REQ-C-003: Code Style — C++23, Naming Conventions

**Ubiquitous**: All C++ code shall conform to:
- **Language**: C++23 standard
- **Format**: Google-based `.clang-format` (120-column line length, 2-space indentation)
- **Linting**: clang-tidy with `WarningsAsErrors: '*'` (all warnings treated as errors)
- **Naming**:
  - Classes: `CamelCase` (e.g., `PackageListUseCase`)
  - Functions: `camelCase` (e.g., `getInstalledPackages()`)
  - Private data members: `lower_case_` (e.g., `backend_adapter_`)

**Constraint**: All C++ changes must pass `task format`, `task format-check`, and `task tidy` before being considered complete.

**Acceptance Criteria**:
- Running `task format-check` on the implementation branch reports no violations.
- Running `task tidy` completes without errors or warnings.
- Code review confirms class names start with uppercase (e.g., `Package`, `PackageLoader`), method/function names start with lowercase (e.g., `loadPackages()`), and private members end with underscore (e.g., `list_`).

---

### REQ-C-004: Testing — GTest Coverage

**Ubiquitous**: The implementation shall include focused GTest unit tests covering:
- Backend adapter initialization with various path scenarios (valid, invalid, missing)
- Domain model serialization and field presence
- Application layer sorting and filtering
- Empty vs error state distinction (REQ-F-007)
- Mock backends for deterministic testing

**Constraint**: Tests shall be executable via `task test` and shall pass with zero failures and zero skipped tests.

**Acceptance Criteria**:
- Running `task test` completes successfully with a test count > 5 and zero failures.
- Test code is located in `tests/` and organized by module (e.g., `tests/domain/`, `tests/backends/`, `tests/application/`).
- A code reviewer can identify at least one test for each REQ-F-xxx requirement.

---

## Acceptance Test Plan

| Requirement | Test Type | Trigger | Expected Outcome |
|---|---|---|---|
| REQ-F-001 | Unit (C++) | Inspect `src/domain/` / Compile | Package type has 7 fields, no more/fewer |
| REQ-F-002 | Unit (C++) | Run test with fixture DB | Adapter returns correctly mapped Package objects |
| REQ-F-003 | Unit (C++) | Run test, sort check | Use case returns alphabetically sorted packages |
| REQ-F-004 | Visual/Snapshot | Launch QML view | List displays name, version, source label; no mock data |
| REQ-F-005 | Performance/Manual | Load with delayed mock | UI responsive (<16 ms stall); loading indicator visible |
| REQ-F-006 | Unit + Visual | Pass invalid DB path | No crash; error message displayed |
| REQ-F-007 | Visual/Snapshot | Load zero-package fixture | Empty list displayed, distinct from error state |
| REQ-F-008 | Build | Compile | No INTERFACE targets in domain/backends/application; links succeed |
| REQ-NF-001 | Unit (CI) | Run tests in container w/o pacman | Tests pass using only fixture DB |
| REQ-NF-002 | Performance | Measure frame time | Update ≤100 ms; no >16 ms stalls |
| REQ-NF-003 | Code review | Inspect Package.hpp | 7 fields only; no extra metadata |
| REQ-C-001 | Code review | Inspect backend adapter | No hardcoded paths; all from parameters |
| REQ-C-002 | Build + Inspect | Run cmake; check CMakeLists.txt | All three targets are STATIC; link succeeds |
| REQ-C-003 | Lint + Formatter | Run `task format-check` and `task tidy` | No format or linting violations |
| REQ-C-004 | Test suite | Run `task test` | >5 passing tests, zero failures |

---

## Related Documents

- **Architecture**: `/home/andrii/Projects/pet/holonight/holonight-pkg-manager/docs/sdd/installed-packages-list/ARCHITECTURE.md` (to be produced in Design phase)
- **Codebase Reference**: `/home/andrii/Projects/pet/holonight/holonight-pkg-manager/CLAUDE.md`
- **Build System**: `/home/andrii/Projects/pet/holonight/holonight-pkg-manager/Taskfile.yml`

---

## Change History

| Version | Date | Author | Notes |
|---|---|---|---|
| 1.0 | 2026-08-11 | SDD Process | Initial EARS specification |
