# SDD Tasks — installed-page-ui

- [x] T-001: Extend Package struct with 6 new fields
  - REQs: REQ-F-101
  - Check: `Package` struct in `src/domain/include/holonight_packages_domain/package.h` declares all 13 fields including `sizeBytes`, `description`, `installDate`, `requiredBy`, `optionalDependencies`, `configFileCount`.

- [x] T-002: Test Package domain changes
  - REQs: REQ-F-101, REQ-NF-118
  - Check: `tests/domain/package_test.cpp` passes with regression tests for existing 7 fields and new fixture cases for 6 new fields.

- [x] T-003: Implement populateExtendedFields() in AlpmPackageSource
  - REQs: REQ-F-102
  - Check: `src/backends/src/alpm_package_source.cpp` calls `populateExtendedFields(package, pkg)` immediately after `convertPackageFields()` in `toPackage()` and before pushing into result vector.

- [x] T-004: Test AlpmPackageSource new fields population
  - REQs: REQ-F-102, REQ-NF-118
  - Check: `tests/backends/alpm_package_source_test.cpp` has new test cases verifying `sizeBytes`, `description`, `requiredBy`, `optionalDependencies`, and `configFileCount` are correctly populated from libalpm.

- [x] T-005: Implement orphan computation functions
  - REQs: REQ-F-103, REQ-F-104
  - Check: `src/application/include/holonight_packages_application/orphan_package_filter.h` and `.cpp` define `isOrphan()` and `computeOrphanStatistics()` with `OrphanStatistics` struct.

- [x] T-006: Test orphan package filter logic
  - REQs: REQ-F-103, REQ-F-104, REQ-NF-118
  - Check: `tests/application/orphan_package_filter_test.cpp` has 4 test cases covering Explicit (not orphan), Dependency-with-requiredBy (not orphan), Dependency-without-requiredBy (orphan), and 5-package aggregate statistics.

- [x] T-007: Implement size formatting function
  - REQs: REQ-F-112, REQ-F-114, REQ-F-117
  - Check: `src/application/include/holonight_packages_application/package_size_formatter.h` and `.cpp` define `formatSizeBytes()` returning human-readable strings like "1.2 MiB", "234 KiB", "5.6 GiB" with trailing ".0" trimmed.

- [x] T-008: Test package size formatting
  - REQs: REQ-NF-118
  - Check: `tests/application/package_size_formatter_test.cpp` covers boundary cases: 0 B, 1023 B, 1024 B, 1024 KiB, 1.5 MiB, 186 MiB (exact multiple showing "186 MiB" not "186.0 MiB").

- [x] T-009: Extend InstalledPackagesModel with new roles and aggregate properties
  - REQs: REQ-F-105
  - Check: `apps/packages/app/InstalledPackagesModel.h` declares 9 new roles (SizeRole, SizeLabelRole, DescriptionRole, InstallDateRole, RequiredByCountRole, RequiredByListRole, OptionalDependenciesRole, ConfigFileCountRole, IsOrphanRole) and 7 aggregate Q_PROPERTYs (totalPackageCount, totalInstalledSizeBytes, explicitPackageCount, dependencyPackageCount, foreignPackageCount, orphanPackageCount, reclaimableSizeBytes).

- [x] T-010: Test InstalledPackagesModel new roles and aggregates
  - REQs: REQ-F-105, REQ-NF-118
  - Check: `tests/apps/installed_packages_model_test.cpp` has new test cases verifying SizeRole/IsOrphanRole data correctness and that `reclaimableSizeBytes` property equals sum of orphan package sizes.

- [x] T-011: Implement InstalledPackagesFilterModel
  - REQs: REQ-F-106, REQ-F-107, REQ-F-108, REQ-F-109, REQ-NF-119
  - Check: `apps/packages/app/InstalledPackagesFilterModel.h/.cpp` implement `QSortFilterProxyModel` subclass with tab/search/repository/sort properties, `filterAcceptsRow()`, `lessThan()`, identity-based `currentRow` tracking, notified `currentPackage` data, one selection reconciliation per filter operation, and `availableRepositories()`.

- [x] T-012: Test InstalledPackagesFilterModel
  - REQs: REQ-NF-118
  - Check: `tests/apps/installed_packages_filter_model_test.cpp` covers tab filter, search text filter, repository filter, sort by Name and Size, composition of all four filters, and `currentRow` reconciliation when filters change.

- [x] T-013: Wire InstalledPackagesModel and InstalledPackagesFilterModel in CMakeLists
  - REQs: REQ-NF-120
  - Check: `apps/packages/CMakeLists.txt` includes `InstalledPackagesModel.cpp` and `InstalledPackagesFilterModel.cpp` in both `qt_add_executable()` and `qt_add_qml_module(...SOURCES ...)`.

- [x] T-014: Create PackageOriginBadge.qml
  - REQs: REQ-F-112, REQ-F-114
  - Check: `qml/packages/PackageOriginBadge.qml` renders a pill badge showing source and repository (e.g., "Official · core", "AUR").

- [x] T-015: Create PackageTableHeader.qml
  - REQs: REQ-F-112, REQ-F-113
  - Check: `qml/packages/PackageTableHeader.qml` renders column headers (Package, Origin, Installed Version, Size, Reason) and select-all checkbox in left column.

- [x] T-016: Create PackageTableRow.qml
  - REQs: REQ-F-112, REQ-F-113
  - Check: `qml/packages/PackageTableRow.qml` renders data row with visual-only checkbox + 5 columns in same column layout as header, responds to click to select row as active.

- [x] T-017: Create PackageDetailHeader.qml
  - REQs: REQ-F-114, REQ-F-115
  - Check: `qml/packages/PackageDetailHeader.qml` renders icon, package name, two `PackageOriginBadge`s, "Installed" status indicator, and visually-live but inert Remove and more-options buttons.

- [x] T-018: Create PackageDetailMetadataRows.qml
  - REQs: REQ-F-114
  - Check: `qml/packages/PackageDetailMetadataRows.qml` renders Installed/Version/Size/Reason metadata rows using `HnSettingsRow` with correct formatted values.

- [x] T-019: Create PackageDetailDependencySections.qml
  - REQs: REQ-F-114, REQ-C-116
  - Check: `qml/packages/PackageDetailDependencySections.qml` renders full description, "Required by" count with list or "Safe to remove", "Optional dependencies" with chips and "+N more" expansion, and "Local state" config file count.

- [x] T-020: Create PackageDetailFooterLinks.qml
  - REQs: REQ-C-116
  - Check: `qml/packages/PackageDetailFooterLinks.qml` renders 4 disabled action delegates labeled Files, Dependencies, Changelog, Website.

- [x] T-021: Create PackageTable.qml
  - REQs: REQ-F-112, REQ-F-113
  - Check: `qml/packages/PackageTable.qml` composes `PackageTableHeader` + `ListView` of `PackageTableRow` with empty-state message when no packages match filters.

- [x] T-022: Create PackageDetailPanel.qml
  - REQs: REQ-F-114, REQ-F-115
  - Check: `qml/packages/PackageDetailPanel.qml` composes `PackageDetailHeader`/`PackageDetailMetadataRows`/`PackageDetailDependencySections`/`PackageDetailFooterLinks` and shows empty state ("Select a package to view details") when `currentRow == -1`.

- [x] T-023: Create InstalledFilterTabs.qml
  - REQs: REQ-F-106
  - Check: `qml/packages/InstalledFilterTabs.qml` renders 4 pill-styled tabs (Explicit, Dependencies, AUR/Foreign, Orphans) with live count badges from model.

- [x] T-024: Create InstalledToolbar.qml
  - REQs: REQ-F-107, REQ-F-108, REQ-F-109, REQ-F-110, REQ-F-111
  - Check: `qml/packages/InstalledToolbar.qml` contains page title, subtitle with package count/total size, search field, sort dropdown with 4 entries (Name A-Z, Name Z-A, Size Large-Small, Size Small-Large), exclusive inert list/grid toggle, repository dropdown, and "All states" dropdown.

- [x] T-025: Create OrphanFooterBar.qml
  - REQs: REQ-F-117, REQ-F-115
  - Check: `qml/packages/OrphanFooterBar.qml` renders footer bar with orphan icon + count + reclaimable size from global model aggregates, and inert "Review" button.

- [x] T-026: Modify InstalledPackagesView.qml to compose complete page layout
  - REQs: REQ-F-106, REQ-F-107, REQ-F-108, REQ-F-109, REQ-F-110, REQ-F-111, REQ-F-112, REQ-F-113, REQ-F-114, REQ-F-115, REQ-F-117, REQ-NF-119
  - Check: `qml/packages/InstalledPackagesView.qml` creates `InstalledPackagesFilterModel` wired to injected model, composes toolbar/tabs/table/detail/footer when Loaded and non-empty, and preserves `objectName` values `loadingState`, `emptyState`, `errorState`, `packageList` for existing tests.

- [x] T-027: Test InstalledPackagesView QML structure
  - REQs: REQ-NF-118
  - Check: `tests/apps/installed_packages_view_test.cpp`'s existing 4 test cases still pass against preserved `objectName` lookups; no modifications to existing tests required.

- [x] T-028: Create Sidebar.qml
  - REQs: REQ-C-116
  - Check: `qml/workspace/Sidebar.qml` renders app title, Installed nav item (checked), Updates/Explore/History nav items (disabled), Settings (disabled), and "Last synced" placeholder text.

- [x] T-029: Modify WorkspaceWindow.qml to add Sidebar
  - REQs: REQ-C-116
  - Check: `qml/workspace/WorkspaceWindow.qml` becomes a `RowLayout` of `Sidebar` + `InstalledPackagesView` instead of `Rectangle` with `InstalledPackagesView` filling entire window.

- [x] T-030: Full system acceptance verification
  - REQs: REQ-NF-121
  - Check: All 19 acceptance checkpoints from REQ-NF-121 pass: (1) domain model; (2) backend population; (3) orphan logic correctness; (4) QML model roles/aggregates; (5) tab filter; (6) search composition; (7) sort functionality; (8) repository filter; (9) all-states dropdown; (10) list/grid toggle inert; (11) data table structure; (12) checkbox visual-only behavior; (13) detail panel display; (14) inert action buttons; (15) out-of-scope sections absent; (16) footer orphan stats; (17) test coverage; (18) in-memory responsiveness <100ms; (19) code style compliance via `task format-check`, `task tidy`, `task qml-lint` all passing.

  - Evidence: See [ACCEPTANCE.md](ACCEPTANCE.md). Stakeholder manual/visual acceptance was confirmed on 2026-09-05; follow-up review fixes have automated regression coverage. The separate 60 fps scrolling target remains unmeasured.
