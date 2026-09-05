# Installed Page UI Redesign – Requirements Specification

**Document ID:** SPEC-INSTALLED-PAGE-UI-2026-09  
**Project:** HoloNight Package Manager (C++23 / Qt 6 / QML)  
**Date:** 2026-09-05  
**Scope:** Redesign the Installed packages view from a bare single-column list to a rich, filterable data table with detail panel, powered by extended Package domain model  

## Executive Summary

This specification defines the functional, non-functional, and constraint requirements for redesigning the Installed page to match a provided visual mockup. The redesign extends the existing domain model with libalpm-sourced metadata, computes orphan packages, and rebuilds the UI with live-filtered tabs, search, sort, and a detail panel — all while remaining transactionally inert (no package mutations implemented).

---

## Domain Model Extension

### REQ-F-101: Package struct extended with libalpm metadata

**EARS Template:** Ubiquitous  
**Requirement:**  
The `Package` struct in `src/domain/include/holonight_packages_domain/package.h` shall be extended to include six new fields in addition to the existing seven (identity, name, installedVersion, sourceType, repository, installReason, backendSpecificId):

- `sizeBytes` (integral type: `uint64_t` or `int64_t`) – size of package in bytes
- `description` (std::string) – package description from libalpm metadata
- `installDate` (std::chrono::system_clock::time_point) – installation timestamp
- `requiredBy` (std::vector<std::string>) – names/identities of packages that depend on this package
- `optionalDependencies` (std::vector<std::string>) – optional dependency specifications
- `configFileCount` (size_t) – count of configuration files tracked by package manager

All new fields shall be obtainable in a single enumeration pass from libalpm (via `alpm_pkg_compute_requiredby`, `alpm_pkg_get_optdepends`, `alpm_pkg_get_backup`, etc.) without additional backend calls.

**Acceptance Criteria:**
- `Package` struct members are declared with types as specified above  
- Existing tests for the seven original fields continue to pass without modification  
- A new unit test exists verifying that each new field is correctly populated from a fixture Package  

---

### REQ-F-102: AlpmPackageSource populates new Package fields

**EARS Template:** Ubiquitous  
**Requirement:**  
The `AlpmPackageSource` backend (in `src/backends/`) shall populate all six new fields (sizeBytes, description, installDate, requiredBy, optionalDependencies, configFileCount) during package enumeration. Population must occur within the same pass that enumerates the original seven fields; no additional libalpm calls or backend round-trips are permitted.

**Acceptance Criteria:**
- Existing integration/unit tests at `tests/fixtures/pacman/` and `tests/` continue to pass  
- A new test fixture or test case exists verifying at least one new field (e.g., sizeBytes or description) is correctly extracted from a real or mocked libalpm package  
- `AlpmPackageSource::enumerate()` / enumeration method returns packages with all six new fields populated and non-empty/valid (as applicable; e.g., requiredBy may be empty for some packages, which is valid)  

---

## Orphan Computation

### REQ-F-103: Orphan flag derived from Package fields

**EARS Template:** Ubiquitous  
**Requirement:**  
A package shall be classified as an "orphan" if and only if its `installReason` field equals `Dependency` AND its `requiredBy` vector is empty. This classification shall be exposed via a derived or computed `isOrphan` boolean, either as a field in the Package struct or as a Q_PROPERTY / computed role in `InstalledPackagesModel`.

**Acceptance Criteria:**
- An independent, testable function or method computes orphan status from Package fields  
- A unit test exists with fixtures where:
  - Package with `installReason == Dependency` and empty `requiredBy` is classified as orphan  
  - Package with `installReason == Explicit` is NOT classified as orphan, regardless of `requiredBy`  
  - Package with `installReason == Dependency` and non-empty `requiredBy` is NOT classified as orphan  

---

### REQ-F-104: Aggregate orphan count and reclaimable size exposed to application/UI

**EARS Template:** Ubiquitous  
**Requirement:**  
The application layer (e.g., as a computed property in `PackageListUseCase` or a new use case) shall expose:
- Total count of orphan packages  
- Total reclaimable size (sum of `sizeBytes` for all packages where `isOrphan == true`)

These aggregates shall be recomputed/refreshed whenever the package list is refreshed from the backend.

**Acceptance Criteria:**
- A Q_PROPERTY or method on the model/use case exposes `orphanPackageCount: int` (or similar)  
- A Q_PROPERTY or method exposes `reclaimableSizeBytes: uint64_t` (or similar)  
- A unit test verifies that with a fixture of 5 packages (2 explicit, 1 dependency with requiredBy, 2 dependencies without requiredBy), orphanPackageCount == 2 and reclaimableSizeBytes equals the sum of those 2 packages' sizes  

---

## QML Model & Properties

### REQ-F-105: InstalledPackagesModel extends with new roles and aggregate properties

**EARS Template:** Ubiquitous  
**Requirement:**  
`InstalledPackagesModel` (in `apps/packages/app/InstalledPackagesModel.h/.cpp`) shall expose the following new list roles in addition to existing roles (NameRole, InstalledVersionRole, SourceLabelRole):

- `SizeRole` (quint64) – package size in bytes  
- `DescriptionRole` (QString) – package description  
- `InstallDateRole` (QDateTime or QString formatted) – install date for display  
- `RequiredByCountRole` (int) – count of packages requiring this package  
- `OptionalDependenciesRole` (QStringList or JSON array) – optional dependency list  
- `ConfigFileCountRole` (int) – count of configuration files  
- `IsOrphanRole` (bool) – whether package is classified as orphan  

The model shall also expose the following aggregate Q_PROPERTYs:
- `totalPackageCount: int` – total number of packages loaded  
- `totalInstalledSizeBytes: quint64` – sum of all package sizes  
- `explicitPackageCount: int` – count of packages with `installReason == Explicit`  
- `dependencyPackageCount: int` – count of packages with `installReason == Dependency`  
- `foreignPackageCount: int` – count of packages with `sourceType == Foreign`  
- `orphanPackageCount: int` – count of orphans (derived from `isOrphan` role)  
- `reclaimableSizeBytes: quint64` – sum of sizes for all orphans  

**Acceptance Criteria:**
- `InstalledPackagesModel::data()` returns valid QVariant for each new role in the role() switch  
- Each new role is registered in `roleNames()` QHash with a human-readable name (e.g., "size", "description")  
- A unit test verifies that with a fixture model of N packages, `totalPackageCount` property returns N  
- A unit test verifies that `reclaimableSizeBytes` property equals the sum of sizes for packages where `IsOrphanRole` is true  

---

## Filter Tabs

### REQ-F-106: Four-tab category filter with live count badges

**EARS Template:** State-driven, Conditional  
**Requirement:**  
The Installed page shall render a tab bar with exactly four mutually exclusive tabs:
1. **Explicit** – packages with `installReason == Explicit`  
2. **Dependencies** – packages with `installReason == Dependency`  
3. **AUR/Foreign** – packages with `sourceType == Foreign`  
4. **Orphans** – packages classified as `isOrphan == true`  

Each tab shall display a live count badge showing the number of packages matching that category. Tab selection shall be mutually exclusive; selecting a tab shall filter the visible data table rows to show only packages matching the selected category.

The default active tab on page load shall be **Explicit**.

The tab selection state shall persist within the current session (or following project preferences on multi-session persistence, if any).

**Acceptance Criteria:**
- Tab bar renders four tabs with correct labels: "Explicit", "Dependencies", "AUR/Foreign", "Orphans"  
- Each tab displays a non-negative integer badge reflecting the current count of packages in that category  
- When the Explicit tab is active, the data table shows only packages where `installReason == Explicit` AND no other filters override (see REQ-F-108, REQ-F-109)  
- When the Orphans tab is active, the data table shows only packages where `isOrphan == true`  
- Switching tabs live-updates the table row count and displayed rows (no page reload)  
- On initial load, the Explicit tab is visually selected and the table displays Explicit packages  
- Selecting Explicit tab multiple times does not cause display glitches or duplicate rows  

---

## Search

### REQ-F-107: Case-insensitive package name search composed with tab filter

**EARS Template:** Event-driven  
**Requirement:**  
A search field shall be provided on the Installed page that filters visible data table rows by case-insensitive substring match on the `name` field of each package. The search shall be composed (logically AND-ed) with the active tab filter; a package must match both the tab category AND the search substring to be displayed.

When the search field is empty (or cleared), no search filtering is applied and all packages matching the active tab are displayed.

**Acceptance Criteria:**
- A text input field labeled "Search packages" (or similar) is rendered and receives focus  
- Typing "vlc" in the search field filters the table to show only packages whose `name` contains "vlc" (case-insensitive)  
- Typing "VLC" shows the same results (case-insensitive)  
- With Explicit tab active and search "vlc", only packages where `installReason == Explicit` AND `name` contains "vlc" are shown  
- Clearing the search field restores the full filtered table (based on active tab only)  
- Searching while Orphans tab is active correctly intersects orphan packages with the search substring  
- No UI freeze occurs during search on a fixture of 5000 packages (in-memory filtering only)  

---

## Sort

### REQ-F-108: Sort control with Name and Size fields and direction toggle

**EARS Template:** Event-driven  
**Requirement:**  
A sort control (e.g., a dropdown or toggle button) shall be provided to re-order the visible data table rows. The sort control shall support at least two sort fields:
1. **Name** (package name, lexicographical order)  
2. **Size** (package size in bytes, numerical order)  

Each sort field shall support both ascending and descending direction. A sensible default direction per field is acceptable (e.g., Name ascending, Size descending). The sort shall be composed (orthogonal) with tab filter and search; sort reorders only the filtered rows.

Sort changes shall take effect immediately and live-update the table display without page reload.

**Acceptance Criteria:**
- A sort control is rendered with selectable options: Name, Size  
- Selecting "Name" sorts the visible (filtered) rows by package name A–Z  
- Selecting "Name" again (or clicking a direction toggle) reverses to Z–A  
- Selecting "Size" sorts the visible rows by sizeBytes (largest or smallest first, depending on direction)  
- Toggling sort direction live-updates the table display  
- With Explicit tab active, search "lib", and sort "Size descending", the table shows Explicit packages containing "lib", ordered by size descending  
- Changing tabs does not reset the sort setting; the sort applies to the new tab's results  

---

## Repository Filter Dropdown

### REQ-F-109: Repository filter dropdown with "All repositories" option

**EARS Template:** Event-driven, Conditional  
**Requirement:**  
A dropdown filter control shall be provided that filters visible data table rows by the `repository` field of each package. The dropdown shall include:
- An "All repositories" option (representing no repository filter; all repositories are included)  
- One option per unique value in the `repository` field across all packages  

Selecting a repository option shall filter the visible rows to show only packages from that repository. The repository filter shall be composed (AND-ed) with the active tab filter, search, and sort; a package must match all four constraints to be displayed.

**Acceptance Criteria:**
- A dropdown menu labeled "Repository" (or similar) is rendered  
- The dropdown contains an "All repositories" option and one entry per unique repository value in the full package list  
- Selecting "All repositories" clears the repository filter  
- Selecting a specific repository (e.g., "core") shows only packages where `repository == "core"`  
- With Explicit tab, search "lib", sort "Name", and repository "community" active, the table shows only Explicit packages with "lib" in name from the "community" repository, sorted by name  
- Changing the tab, search, or sort does not reset the repository selection; the filter applies to the new results  

---

## "All states" Dropdown (Non-Functional)

### REQ-F-110: "All states" dropdown rendered but visually present with no filtering effect

**EARS Template:** Ubiquitous  
**Requirement:**  
A dropdown control labeled "All states" (or similar) shall be rendered on the Installed page, positioned alongside other filter controls, for future extensibility. In this cycle, it shall contain exactly one option ("All states" or "All") and have no filtering effect on the data table. Interacting with this dropdown shall not alter which packages are displayed.

**Acceptance Criteria:**
- A dropdown menu labeled "All states" is visually present on the page  
- The dropdown contains exactly one selectable option: "All states" (or similar)  
- The dropdown renders without error and is accessible (not hidden or disabled)  
- Clicking the dropdown and selecting the option does not filter, hide, or alter the displayed package table in any way  
- The dropdown's appearance is consistent with other filter controls on the page  

---

## List/Grid View Toggle (Grid Non-Functional)

### REQ-F-111: List/grid view toggle rendered; grid selection is inert

**EARS Template:** State-driven  
**Requirement:**  
A toggle control with two options (List, Grid) shall be rendered on the Installed page. List view is the only implemented view and shall display packages in tabular rows (see REQ-F-114). Selecting Grid view shall not crash, hide content, or perform layout changes; the table shall remain in list (tabular) view regardless of the toggle state.

**Acceptance Criteria:**
- A toggle control with two states "List" and "Grid" is rendered  
- List view is the default (active) state on page load  
- Clicking Grid view does not crash the application or JavaScript console  
- Clicking Grid view does not hide packages, change column layout, or alter displayed data  
- All packages visible in List view remain visible in Grid view (rendered as list)  
- Switching back to List view after Grid view does not cause display anomalies  
- Clicking the toggle does not trigger network requests or re-enumeration of packages  

---

## Data Table

### REQ-F-112: Data table columns and row selection model

**EARS Template:** Ubiquitous, Event-driven  
**Requirement:**  
The data table shall display installed packages with the following columns:

1. **Package** – icon placeholder + package name + brief description (first 1–2 lines of `description` field or truncated)  
2. **Origin** – badge or label showing package source and repository (e.g., "Official – core" or "AUR – community")  
3. **Installed Version** – `installedVersion` field value  
4. **Size** – `sizeBytes` field formatted as human-readable (e.g., "1.2 MiB", "234 KiB", "5.6 GiB")  
5. **Reason** – `installReason` enum value displayed as "Explicit" or "Dependency"  

A header row shall appear above the data rows with column labels. The header row shall contain a select-all checkbox (see REQ-F-115).

Clicking on a table row (anywhere except a checkbox) shall select that row as the "active" package and trigger display of its detail panel (see REQ-F-116). Exactly one row shall be selected at a time. The first non-empty row (topmost package) shall be selected by default when the table first loads (or refreshes) with packages. If the active row is removed/filtered out, the first remaining row shall become active.

Rows shall be highlighted or styled to indicate selected state.

**Acceptance Criteria:**
- Data table renders with five columns (Package, Origin, Installed Version, Size, Reason) in this order  
- Column headers appear with correct labels  
- A package row displays name, icon placeholder, and truncated description in the Package column  
- A package row displays origin badge (e.g., "Official – core") in the Origin column  
- A package row displays human-readable size (e.g., "2.4 MiB") in the Size column  
- Clicking a package row (non-checkbox) selects it and updates the detail panel (REQ-F-116)  
- Only one row is highlighted/selected at a time  
- On initial load with Explicit packages present, the first Explicit package row is selected by default  
- Clicking the same row again keeps it selected (no toggle); clicking a different row deselects the previous one  
- Filtering (tab, search, repository) updates the table; if the active row is filtered out, the first remaining row becomes active  

---

### REQ-F-113: Row and header select-all checkboxes are visual only

**EARS Template:** Ubiquitous  
**Requirement:**  
Each data table row shall include a checkbox (in the leftmost column, before the Package column). The header row shall include a "select-all" checkbox. These checkboxes shall be rendered but provide no functional filtering, selection-for-detail, or bulk-action capability in this cycle. Toggling checkboxes shall not:
- Filter visible rows  
- Mark packages for removal or bulk action  
- Persist selection state beyond the current session  
- Affect the active/selected row for detail panel display (see REQ-F-112, REQ-F-116)  

**Acceptance Criteria:**
- A checkbox is rendered in each data table row  
- A checkbox is rendered in the header row  
- Clicking row checkboxes does not alter the displayed table rows  
- Clicking the header select-all checkbox does not bulk-select all rows for any action  
- Checkbox visual state (checked/unchecked) changes when clicked but has no side effects  
- Row selection (for detail panel, per REQ-F-112) remains independent of checkbox state  

---

## Detail Panel

### REQ-F-114: Detail panel for active package with metadata, badges, and sections

**EARS Template:** Event-driven, State-driven  
**Requirement:**  
When a package row is selected (REQ-F-112), a detail panel shall appear (typically to the right of, or below, the data table, per visual mockup) displaying comprehensive metadata for the selected package. The detail panel shall include:

**Header section:**
- Icon placeholder (16×16 px or per design)  
- Package name (large, bold)  
- Source and repository badges (e.g., "Official", "core" or "AUR", "community")  
- "Installed" status indicator (e.g., a green checkmark or "Installed" label)  

**Action buttons:**
- A "Remove" button (per REQ-F-118, inert/disabled in this cycle)  
- A secondary "more options" affordance, e.g., ellipsis menu icon (per REQ-F-118, non-functional)  

**Metadata rows:**
- **Installed:** date and time (from `installDate`, formatted as "MMM DD, YYYY HH:MM" or locale-appropriate)  
- **Version:** `installedVersion` value  
- **Size:** `sizeBytes` formatted as human-readable (e.g., "1.2 MiB")  
- **Reason:** `installReason` value ("Explicit" or "Dependency")  

**Description section:**
- Full `description` text (not truncated; may be scrollable if long)  

**"Required by" section:**
- A label and count: e.g., "Required by: 3 packages"  
- If count is zero, include a message: "Safe to remove" or "No packages depend on this"  
- If count is non-zero, a list or expandable list of package names from `requiredBy` vector  

**"Optional dependencies" section:**
- A label and count: e.g., "Optional dependencies: 5"  
- Render up to N entries (e.g., 5) as chip/tag elements  
- If more than N entries exist, show a "+N more" chip or link  
- Clicking "+N more" expands the list to show all entries  

**"Local state" section:**
- A label: "Configuration files"  
- Count: `configFileCount` value (e.g., "5 files")  

**Empty state:**
- If no packages are present in the table (or tab), or no row is selected, the detail panel shall display an empty/placeholder state (e.g., "Select a package to view details") rather than crashing or showing stale data.

**Acceptance Criteria:**
- When a package row is clicked, the detail panel appears within 200 ms  
- Detail panel displays the selected package's name, icon, and badges  
- Detail panel displays "Installed" status indicator (green checkmark, icon, or text)  
- Detail panel metadata rows show correct values (installed date formatted, version, size in MiB/GiB, reason)  
- Detail panel displays full package description  
- "Required by" section shows count; if zero, shows "Safe to remove" message; if non-zero, lists package names  
- "Optional dependencies" section shows count and up to 5 entries as chips; clicking "+N more" expands list (or list is scrollable)  
- "Local state" section shows configuration file count  
- Clicking a different package row updates the detail panel to show the new package within 200 ms  
- If no packages are loaded or displayed, detail panel shows an empty/placeholder state instead of crashing  

---

## Inert Actions

### REQ-F-115: Remove button, more-options menu, and action buttons are disabled/non-functional

**EARS Template:** Unwanted-behavior  
**Requirement:**  
The following UI affordances shall be rendered but shall NOT perform any package mutation or state change in this cycle:
- Detail panel "Remove" button (REQ-F-114)  
- Detail panel "more options" menu/ellipsis (REQ-F-114)  
- Any row-level "more options" or action affordances (if rendered)  
- Footer "Review" button (REQ-F-119)  

These affordances shall be clearly marked as non-functional via one or more of:
- Disabled/grayed-out appearance with cursor not-allowed  
- Tooltip or help text: "Not implemented yet" or "Feature coming soon"  
- No-op behavior (click does nothing, no error message)  

Critically, interacting with any of these affordances shall NOT:
- Trigger package removal, installation, or transaction  
- Modify package state or application state  
- Show false success messages (e.g., "Package removed" when nothing changed)  
- Throw unhandled exceptions or JavaScript errors  

**Acceptance Criteria:**
- Detail panel "Remove" button is rendered in a disabled visual state (e.g., grayed out)  
- Hovering over or clicking the "Remove" button shows a tooltip or help message (e.g., "Not yet implemented") or does nothing  
- Clicking "Remove" does not modify any package state or trigger backend calls  
- The "more options" affordance (ellipsis or menu) is rendered but non-functional or disabled  
- Clicking "more options" does not open a menu or perform any action  
- Footer "Review" button (REQ-F-119) is disabled/non-functional per this requirement  
- No JavaScript console errors or exceptions occur when clicking these affordances  
- Application state (model, package list) remains unchanged after interacting with disabled buttons  

---

## Out of Scope & Non-Goals

### REQ-C-116: Omit unimplemented sections and depth features

**EARS Template:** Unwanted-behavior  
**Requirement:**  
The following features and sections are NOT implemented in this cycle and shall either be omitted entirely or rendered as clearly inert/disabled:

1. **Files section** – Package file listing / file integrity checking  
2. **Dependencies section** (deep detail) – Interactive dependency tree or graph  
3. **Changelog section** – Package changelog viewing  
4. **Website deep-link** – Opening package home page or external links  
5. **File-modified check** – Detecting or displaying modified package files  
6. **Bulk actions from checkboxes** – Selecting multiple packages and performing group actions  
7. **Grid view layout** – Grid/card-based view of packages  
8. **Package mutations** – Installation, removal, updating, or downgrading packages  
9. **Transaction rollback or undo** – Reverting package changes  
10. **Left sidebar navigation** – Pages beyond Installed (Explore, History, Updates, Settings) remain static/disabled placeholders  

**Acceptance Criteria:**
- No code path attempts to open, read, or display package file lists  
- No dependency graph or interactive tree visualization is rendered  
- No changelog section appears in the detail panel  
- No external website links are clickable or functional  
- The application does not perform or log file-integrity checks  
- Checkboxes do not enable bulk-action buttons or filters  
- Grid view toggle has no effect on layout (per REQ-F-111)  
- No `remove()`, `install()`, or transaction methods are called from the UI  
- Left sidebar Explore/History/Updates/Settings buttons are disabled/non-functional or labeled as placeholders  

---

## Footer Orphan Bar

### REQ-F-117: Footer displays real orphan count and reclaimable size

**EARS Template:** Ubiquitous, State-driven  
**Requirement:**  
A footer bar or section shall be displayed at the bottom of the Installed page showing:
- **Orphan count:** Real count of packages classified as orphans (REQ-F-103)  
- **Reclaimable size:** Real total size (sum of `sizeBytes` for all orphans), formatted as human-readable (e.g., "234 MiB")  
- **"Review" button:** Inert affordance per REQ-F-115  

The orphan count and reclaimable size shall be recomputed and live-updated whenever the package list is refreshed or filters change. If there are zero orphans, the footer may display "No orphan packages" or show "0 orphans, 0 B reclaimable" (designer's choice).

**Acceptance Criteria:**
- A footer section appears at the bottom of the Installed page  
- Footer displays a non-negative count of orphan packages  
- Footer displays reclaimable size in human-readable format (e.g., "1.2 GiB")  
- With a fixture of 10 packages (5 Explicit, 5 Dependency-only), footer shows "5 orphans, 2.3 GiB reclaimable" (if 5 dependencies sum to 2.3 GiB)  
- Filtering (tab, search, repository) may change which packages are *visible*, but the footer always reflects the *total* orphan stats across all packages  
- Changing the tab from Explicit to Orphans does not change the footer's orphan count (it represents global stats, not filtered stats)  
- Footer "Review" button is non-functional per REQ-F-115  

---

## Non-Functional Requirements

### REQ-NF-118: No regression in existing tests; new test coverage for orphans and new roles

**EARS Template:** Ubiquitous  
**Requirement:**  
All changes to the backend, domain model, and application layer shall maintain backward compatibility and pass all existing unit tests without modification. New unit test coverage shall be added for:
- Orphan computation logic (at least three test cases covering Explicit, Dependency-with-requiredBy, and Dependency-without-requiredBy)  
- New Package fields populated by `AlpmPackageSource`  
- New `InstalledPackagesModel` roles (at least SizeRole, IsOrphanRole, and aggregate properties)  
- Repository filter composition with tab + search filters  

**Acceptance Criteria:**
- `task test` (running GTest suite) reports zero failures for existing tests  
- New test file or test cases added to `tests/` directory covering orphan logic  
- New test cases cover a Package fixture where `isOrphan` returns true and false under correct conditions  
- `alpm_pkg_get_backup`, `alpm_pkg_compute_requiredby`, `alpm_pkg_get_optdepends` are called and verified in backend tests  
- `InstalledPackagesModel` unit test verifies that `IsOrphanRole` returns correct boolean for each package  
- `InstalledPackagesModel` test verifies `reclaimableSizeBytes` property matches sum of orphan sizes  
- Test coverage for filter composition (tab AND search AND repository) with multiple examples  

---

### REQ-NF-119: UI responsiveness; filtering and sorting in-memory only

**EARS Template:** State-driven  
**Requirement:**  
All filtering (tab, search, repository), sorting, and table row updates shall operate on the already-loaded, in-memory package list. No backend re-enumeration, libalpm calls, or network requests shall be triggered by:
- Changing active tab  
- Typing in the search field  
- Selecting a repository filter  
- Selecting a sort order  
- Selecting a package row for detail panel  

The UI shall remain responsive (no perceptible freeze) on a fixture of up to 5000 packages, with sorting/filtering completing within 100 ms per operation.

**Acceptance Criteria:**
- Typing in the search field filters the table instantly (no 500+ ms delay)  
- Clicking a tab updates the filtered display within 100 ms  
- Selecting a repository filter updates the table within 100 ms  
- No backend logs or `AlpmPackageSource::enumerate()` calls occur during filtering/sorting/search  
- With 5000 fixture packages loaded, filtering/sorting operations complete in < 100 ms and UI does not freeze  
- Scroll performance within the data table remains smooth (60 fps) on a 1000-row display  

---

### REQ-NF-120: Code style and tooling compliance

**EARS Template:** Ubiquitous  
**Requirement:**  
All C++ and QML code added or modified in this cycle shall comply with project conventions (per CLAUDE.md):
- **C++ version:** C++23  
- **Format:** `clang-format` using Google-based configuration (120 columns, 2-space indentation)  
- **Linting:** `clang-tidy` with `WarningsAsErrors: '*'` (treat all warnings as errors)  
- **Naming conventions:**
  - Classes: `CamelCase` (e.g., `PackageListModel`)  
  - Functions/methods: `camelCase` (e.g., `computeOrphanStatus()`)  
  - Private data members: `lower_case_with_underscores` (e.g., `orphan_packages_`)  
- **QML:** Must pass `qmllint` without warnings or errors  
- **Tests:** GTest framework; focused coverage for new logic  

Commands to verify compliance:
- `task format` – apply `clang-format -i` (or verify with `task format-check`)  
- `task tidy` – run `clang-tidy` and fix errors  
- `task qml-lint` – lint all QML files  
- `task test` – run all GTest tests  

**Acceptance Criteria:**
- `task format-check` reports zero formatting violations  
- `task tidy` reports zero clang-tidy warnings or errors (WarningsAsErrors mode)  
- `task qml-lint` reports zero linting violations in new/modified QML files  
- `task test` reports zero test failures  
- All new C++ classes follow CamelCase naming (e.g., `OrphanPackageFilter`, `PackageSizeFormatter`)  
- All new C++ functions follow camelCase naming (e.g., `computeOrphanStatus()`, `formatSizeBytes()`)  
- All private data members use `lower_case_underscore` naming (e.g., `filtered_packages_`, `active_row_index_`)  

---

## Acceptance & Verification

### REQ-NF-121: Comprehensive acceptance checklist

**EARS Template:** Ubiquitous  
**Requirement:**  
Acceptance of this specification requires verification of all requirements listed above. A checklist-based acceptance test shall be conducted covering:

1. **Domain model:** New Package fields present and populated  
2. **Backend:** AlpmPackageSource test coverage shows new fields populated; existing tests pass  
3. **Orphan logic:** Orphan classification is correct; counts and sizes are accurate  
4. **QML model:** All new roles and aggregate properties exposed and working  
5. **Tab filter:** Four tabs render, correct counts, filtering works, default to Explicit  
6. **Search:** Case-insensitive substring match on name, composed with tab filter  
7. **Sort:** Name and Size fields functional in both directions  
8. **Repository filter:** Dropdown renders, "All repositories" option present, filtering works  
9. **All states dropdown:** Rendered, one option, no effect  
10. **List/grid toggle:** Both options render, grid selection inert  
11. **Data table:** Five columns correct, header with select-all checkbox, row selection for detail panel, default first row selected  
12. **Row/header checkboxes:** Visual only, no functional side effects  
13. **Detail panel:** All sections display, badges/indicators render, inert actions are disabled  
14. **Inert actions:** Remove button, more-options, Review button non-functional with clear affordances  
15. **Out of scope:** No unimplemented sections rendered; bulk actions, grid layout, mutations, file checking absent  
16. **Footer orphan bar:** Real orphan count and reclaimable size displayed, Review button inert  
17. **Testing:** New tests added and passing; no regressions in existing tests  
18. **Responsiveness:** Filtering/sorting in-memory, < 100 ms per operation on 5000-package fixture  
19. **Code style:** `task format`, `task tidy`, `task qml-lint` all pass; naming conventions followed  

**Acceptance Criteria:**
- A signed-off acceptance test report exists documenting pass/fail for all 19 checkpoints  
- All 19 checkpoints pass without exception  
- Manual testing by project stakeholder confirms UI matches provided visual mockup (screenshots/screen recording)  
- No critical or high-severity bugs or regressions reported during acceptance testing  

---

## Appendix: Visual Mockup Reference

The visual mockup for this redesign is located at:
- `docs/mockups/installed.png` (or equivalent provided design artifact)

The implemented UI shall closely match the mockup in:
- Layout (header, tab bar, filter controls, data table, detail panel, footer)  
- Typography (font sizes, weights)  
- Color scheme and badges  
- Icon usage and positioning  
- Spacing and alignment  

Minor deviations may be necessary based on technical constraints or design refinements, but shall be documented and approved by the project stakeholder.

---

## Sign-Off

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Requirements Author | Claude Code (AI) | 2026-09-05 | ✓ |
| Product Owner | (To be assigned) | | |
| Technical Lead | (To be assigned) | | |

