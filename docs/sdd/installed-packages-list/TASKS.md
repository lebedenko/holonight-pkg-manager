# SDD Tasks — installed-packages-list

- [x] T-001: Domain package model — struct with 7 fields and enums
  - REQs: REQ-F-001, REQ-NF-003
  - Check: `src/domain/include/holonight_packages_domain/package.h` compiles with exactly 7 public fields (identity, name, installedVersion, sourceType, repository, installReason, backendSpecificId) and no additional fields.

- [x] T-002: Domain PackageSource interface, error types, and out-of-line destructor
  - REQs: REQ-F-002, REQ-F-006, REQ-F-007, REQ-C-001
  - Check: `src/domain/include/holonight_packages_domain/package_source.h` defines `PackageSourceErrorCode`, `PackageSourceError`, and abstract `PackageSource` class; `src/domain/src/package_source.cpp` provides out-of-line virtual destructor definition.

- [x] T-003: Convert domain target from INTERFACE to STATIC library
  - REQs: REQ-F-008, REQ-C-002
  - Check: `src/domain/CMakeLists.txt` declares `add_library(holonight_packages_domain STATIC ...)` and compilation produces `.o`/`.a` static archive.

- [x] T-004: Unit test for Package domain model — field construction and operator==
  - REQs: REQ-F-001, REQ-C-004
  - Check: `tests/domain/package_test.cpp` compiles and passes at least 3 test cases verifying field readback, equality, and no-field-missing assertion.

- [x] T-005: Create fixture pacman databases (populated and empty)
  - REQs: REQ-NF-001, REQ-F-002, REQ-F-007
  - Check: `tests/fixtures/pacman/populated/local/ALPM_DB_VERSION` exists; `populated/local/` contains at least 3 package directories (apple, zebra, foreign-tool) with valid `desc` files; `populated/sync/core.db` is a valid gzip tar containing apple and zebra but not foreign-tool; `tests/fixtures/pacman/empty/` exists with valid but empty DB structure.

- [x] T-006: Backend AlpmPackageSource header — constructor and enumerateInstalledPackages declaration
  - REQs: REQ-F-002, REQ-C-001
  - Check: `src/backends/include/holonight_packages_backends/alpm_package_source.h` compiles without including `<alpm.h>`, declares constructor with explicit `database_root` and `database_path` parameters (no defaults), and declares `enumerateInstalledPackages()` override returning `std::expected<std::vector<Package>, PackageSourceError>`.

- [x] T-007: Backend AlpmPackageSource implementation — libalpm enumeration and field mapping
  - REQs: REQ-F-002, REQ-F-006, REQ-F-007, REQ-C-001, REQ-C-003
  - Check: `src/backends/src/alpm_package_source.cpp` includes only `<alpm.h>` (not exposed in header), initializes libalpm with provided paths, discovers sync DBs by globbing `<database_path>/sync/*.db`, populates all 7 Package fields correctly, disambiguates empty result from error via `alpm_errno()`, and passes clang-format and clang-tidy.

- [x] T-008: Convert backends target from INTERFACE to STATIC and add pkg-config libalpm linkage
  - REQs: REQ-F-008, REQ-C-002
  - Check: `src/backends/CMakeLists.txt` declares `add_library(holonight_packages_backends STATIC ...)`, finds libalpm via `pkg_check_modules(Alpm REQUIRED IMPORTED_TARGET libalpm)`, and links `PkgConfig::Alpm` as `PRIVATE`; compilation produces static archive.

- [x] T-009: Unit tests for AlpmPackageSource — fixture enumeration, error handling, empty DB
  - REQs: REQ-F-002, REQ-F-006, REQ-F-007, REQ-NF-001, REQ-C-004
  - Check: `tests/backends/alpm_package_source_test.cpp` passes at least 5 tests: valid populated fixture returns 3 packages with correct fields, empty fixture returns 0 packages (no error), invalid root path returns error without crashing, foreign-tool is marked Foreign/Official sync membership is correct, and at least one Explicit and one Dependency installReason are present.

- [x] T-010: Application layer PackageListUseCase header — constructor and enumerateInstalledPackages declaration
  - REQs: REQ-F-003
  - Check: `src/application/include/holonight_packages_application/package_list_use_case.h` declares constructor taking `std::shared_ptr<PackageSource>` and `enumerateInstalledPackages()` returning `std::expected<std::vector<Package>, PackageSourceError>`; no Qt/QML types appear in the header.

- [x] T-011: Application layer PackageListUseCase implementation — sorting and error passthrough
  - REQs: REQ-F-003, REQ-C-003
  - Check: `src/application/src/package_list_use_case.cpp` calls `source_->enumerateInstalledPackages()`, sorts result ascending by Package::name on success, passes error through unchanged, and passes clang-format and clang-tidy.

- [x] T-012: Convert application target from INTERFACE to STATIC library
  - REQs: REQ-F-008, REQ-C-002
  - Check: `src/application/CMakeLists.txt` declares `add_library(holonight_packages_application STATIC ...)` and compilation produces static archive.

- [x] T-013: Unit tests for PackageListUseCase with GMock backend — sorting and error propagation
  - REQs: REQ-F-003, REQ-C-004
  - Check: `tests/application/mock_package_source.h` provides GMock double of PackageSource; `tests/application/package_list_use_case_test.cpp` passes at least 3 tests verifying packages returned sorted alphabetically, error from mock backend is passed through unchanged, and use case works without any Qt GUI machinery.

- [x] T-014: QML model InstalledPackagesModel header — QAbstractListModel with status states and roles
  - REQs: REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007
  - Check: `apps/packages/app/InstalledPackagesModel.h` declares QAbstractListModel with `Status` enum (Loading, Loaded, Error), Q_PROPERTY status/errorMessage, Role enum with NameRole/InstalledVersionRole/SourceLabelRole, and Q_INVOKABLE refresh() method.

- [x] T-015: QML model InstalledPackagesModel implementation — async with QtConcurrent and state machine
  - REQs: REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-NF-002, REQ-C-003
  - Check: `apps/packages/app/InstalledPackagesModel.cpp` constructs in Loading state, launches `QtConcurrent::run([uc]{ return uc->enumerateInstalledPackages(); })`, listens to `QFutureWatcher::finished()`, transitions to Loaded or Error, emits statusChanged, implements rowCount/data/roleNames per QAbstractListModel contract, guards refresh() against reentrancy with a logical loading flag, and passes clang-format and clang-tidy.

- [x] T-016: Update apps/packages CMakeLists.txt to include new model sources and link Qt6::Concurrent
  - REQs: REQ-F-005, REQ-C-002
  - Check: `apps/packages/CMakeLists.txt` adds InstalledPackagesModel.h and .cpp to target sources, links `Qt6::Concurrent`, and compilation succeeds.

- [x] T-017: Unit tests for InstalledPackagesModel — status transitions, async behavior, model interface
  - REQs: REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-C-004
  - Check: `tests/apps/installed_packages_model_test.cpp` passes at least 5 tests: constructor enters Loading state, async completion triggers statusChanged and Loaded state, error from use case triggers Error state with message, roleNames returns Name/Version/SourceLabel, data() returns correct role values, and refresh() while running returns early (reentrancy guarded).

- [x] T-018: Wiring — construct the domain→backends→application→QML chain and inject the typed model
  - REQs: REQ-C-001, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007
  - Check: `PackagesApplication` constructs the chain and passes the model to `WorkspaceWindow` with `QQuickView::setInitialProperties()` before `setSource()`; required typed properties carry it to `InstalledPackagesView`.

- [x] T-019: Update tests/CMakeLists.txt to include new test sources and define HOLONIGHT_TEST_FIXTURES_DIR
  - REQs: REQ-NF-001, REQ-C-004
  - Check: `tests/CMakeLists.txt` adds package_test.cpp, alpm_package_source_test.cpp, package_list_use_case_test.cpp, installed_packages_model_test.cpp to test target sources; defines `HOLONIGHT_TEST_FIXTURES_DIR` compile definition pointing to source tree fixtures; links `Qt6::Test` and `Qt6::Concurrent`; compilation succeeds.

- [x] T-020: QML view InstalledPackagesView.qml — ListView with three-state UX (loading/loaded/error)
  - REQs: REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007
  - Check: `InstalledPackagesView.qml` requires an `InstalledPackagesModel`, binds the list to it, and displays loading, loaded-empty, loaded-populated, and error states.

- [x] T-021: QML delegate PackageRowDelegate.qml — one row displaying name, version, source label
  - REQs: REQ-F-004
  - Check: `qml/packages/PackageRowDelegate.qml` uses `required property` for name, installedVersion, and sourceLabel, displays all three with "official" or "foreign" text for sourceLabel, and passes qmllint.

- [x] T-022: Modify WorkspaceWindow.qml to replace placeholder with InstalledPackagesView
  - REQs: REQ-F-004
  - Check: `qml/workspace/WorkspaceWindow.qml` imports InstalledPackagesView, removes or comments out placeholder Text element, instantiates InstalledPackagesView in its place, and qmllint passes.

- [x] T-023: Full verification — build, test, format, tidy, qml-lint all pass per CLAUDE.md workflow
  - REQs: REQ-C-003, REQ-C-004
  - Check: `task clean configure build test` passes with >5 tests and zero failures; `task format-check` reports no violations; `task tidy` reports no violations; `task qml-lint` reports no violations; all existing git checks pass.

- [x] T-024: Review remediation — strict errors, deterministic repositories, typed injection, and responsiveness
  - Check: null dependencies throw `std::invalid_argument`; sync discovery errors propagate; repository precedence is lexical; async exceptions become model Error state; role is `installedVersion`; QML state integration and one-second heartbeat tests pass; QML lint runs with `unqualified` enabled.
