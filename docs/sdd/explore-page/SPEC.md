# Explore Page Specification

**Feature**: Read-only package search and discovery in the HoloNight Packages Qt Quick application, allowing users to browse packages in configured sync repositories by name and description with a focused, searchable index.

**Status**: Requirements specification

**Date**: 2026-09-25

---

## Executive Summary

The HoloNight Packages application shall provide users with a read-only Explore page for discovering packages available in configured pacman sync repositories, including third-party repositories. The page displays a searchable list of packages indexed from the live sync databases at `/var/lib/pacman/sync/` (or the configured dbpath), with matching driven by case-insensitive search against package names and descriptions. Search results are ranked by relevance (exact name match, prefix match, substring match, description-only match) and capped at 500 visible rows with a footer message when exceeded. An empty search query displays a hint rather than listing all packages. Selecting a row opens a read-only details panel reusing the existing details component, showing description, URL, licenses, dependencies, optional dependencies, download size, and installed size. The Explore page is added as a new sidebar entry; the Installed page remains the landing page. The application never synchronises databases and makes no network access; all data comes from the current live sync databases without modification.

---

## Non-Goals

The following are explicitly **out of scope** for this specification and will not be implemented in this iteration:

- Package installation, removal, or upgrade operations
- Transaction planning or dependency resolution
- Polkit authorization or privileged helper integration
- AUR (Arch User Repository) or local-only package search; packages in configured third-party sync repositories remain in scope
- Network access, database synchronization, or `checkupdates`-style refresh
- Search filters by repository, installed status, or any dimension beyond name and description
- Provides, groups, file-list search, or regex query support
- Manual sort controls (beyond ranking by relevance; stable sort by name within same rank)
- D-Bus API or system service exposure
- Persisted search history or user preferences
- Autocomplete or suggestion lists (only direct matching and ranking of results)
- Installed-only fields in details (install date, required-by, config file count, install reason)
- Install/Remove/Upgrade buttons (not even disabled)

---

## Glossary

**Sync Database**: The repository metadata database(s) configured in pacman, typically located in `/var/lib/pacman/sync/` (e.g., `core.db`, `extra.db`). Contains metadata for packages available from configured repositories, including third-party repositories.

**Live Sync Databases**: The actual, persistent sync databases at the system's configured dbpath (e.g., `/var/lib/pacman/sync/`). These are used as-is without modification and are read-only for this feature.

**Package Index**: An in-memory searchable index built from all packages in the live sync databases. Constructed at application startup and used for all subsequent searches without re-reading the databases until Reload is pressed.

**Search Query**: A case-insensitive text string entered by the user in the search field. Matched against package names and descriptions.

**Ranking**: The relevance ordering of search results: (1) exact name match, (2) prefix of name, (3) substring of name, (4) description-only match (name doesn't match but description does). Within a rank, results are sorted alphabetically by name.

**Debounce**: A delay applied to search execution to avoid running the search too frequently as the user types. Search is performed on the in-memory index, not a network request.

**Load**: The construction of the package index from the live sync databases, run when the Explore page first loads.

**Reload**: A user-triggered rebuild of the package index from the live sync databases, initiated by the Reload button. Useful when the user has manually synced databases outside the application.

**Installed Badge**: A visual indicator in a table row showing that the package is installed, with a sub-label showing the installed version if different from the available version in the repository.

**Data Freshness Timestamp**: The modification time (mtime) of the oldest `.db` file in the sync directory.

**Stale Hint**: A non-blocking, user-facing message displayed when the Data Freshness Timestamp is more than 7 days old, telling the user their package databases are out of date and should be synced with their package manager, and then Reload pressed. Same wording and behavior as the Updates page.

---

## Functional Requirements

### REQ-F-001: Explore Page — UI Display

**Ubiquitous**: The Qt Quick application shall display an Explore page with a search field at the top and a scrollable table of matching packages below. Each row shall expose:
- Package name
- Available version (from the sync database)
- Repository name
- Download size (human-readable)
- An Installed badge with the installed version (only if installed; no badge otherwise)

**Constraint**: The page shall be built from Holonight.Controls delegates following the project's existing QML conventions, and shall take all data from `ExploreModel`; no hardcoded or mock data. No Install, Remove, or Upgrade buttons shall appear, even disabled.

**Acceptance Criteria**:
- A QML-level test loads the page against a model with fixture packages and asserts each row exposes name, version, repository, download size, and installed badge through model roles.
- A model test with zero matches yields an empty valid list, not an error state.
- `task qml-lint` passes on the page.
- Code inspection confirms no disabled action buttons are present.
- The visual layout is confirmed by the manual checklist in REQ-NF-011, not by automated or assistant-driven visual checks.

---

### REQ-F-002: Search Field and Debouncing

**Event-driven**: The user may type in the search field at the top of the Explore page. As the user types (with each keystroke or text change), the application shall execute a search against the in-memory package index with a debounce delay (e.g., 300 ms), such that rapid keystrokes do not trigger multiple searches.

**State-driven**: While the index is being built (during the initial application load), the search field shall be disabled and display a loading placeholder. Once the index is ready, the field shall be enabled and ready for input.

**Constraint**: The search executes against the in-memory index, not the network. Empty search queries (or whitespace-only) shall not display a list of all packages; instead, they shall show a hint message (e.g., "Enter a search term to find packages").

**Acceptance Criteria**:
- A model test with an injectable or short debounce interval asserts that several keystrokes each arriving within the debounce window result in exactly one search execution, using the final query.
- A model test asserts that two keystrokes separated by more than the debounce interval result in two searches.
- A model test asserts that an empty or whitespace-only search displays the hint state, not a table.
- A QML test asserts the search field is disabled during index construction and enabled afterward.

---

### REQ-F-003: Search Ranking

**Ubiquitous**: Search results shall be ranked by relevance:
1. **Exact name match** (case-insensitive; entire package name equals the query)
2. **Prefix match** (package name starts with the query, case-insensitive)
3. **Substring match** (query appears anywhere in the package name, case-insensitive)
4. **Description match** (query does not match the name, but appears in the description, case-insensitive)

Within each rank, results shall be sorted alphabetically by package name.

**Constraint**: Ranking and substring matching are case-insensitive. The search does not support regex or glob patterns. A query of "" (empty) or whitespace-only shall not trigger a search; see REQ-F-002.

**Acceptance Criteria**:
- A unit test with fixture packages verifies that a query "core" returns the "core" package (if exists) in rank 1, then packages like "coreutils" in rank 2, then packages with "core" in the description in rank 4.
- A test verifies that within each rank, results are sorted alphabetically.
- A test with a query matching description but not name verifies it appears in rank 4.
- A test verifies case-insensitive matching for non-ASCII description text, including a case fold that expands to multiple characters.

---

### REQ-F-004: Result Table — Name, Version, Repository, Size, Installed Badge

**Ubiquitous**: Each result row in the table shall display:
- **Name**: The package name
- **Version**: The available version from the sync database
- **Repository**: The name of the repository (e.g., "core", "extra")
- **Download Size**: Human-readable download size (e.g., "2.5 MB")
- **Installed Badge**: shown only for installed packages:
  - If the package is not installed: no badge is shown
  - If installed and the version matches the available version: "Installed"
  - If installed but the version differs: "Installed {installed_version}" (with a visual distinction, e.g., subdued)

**Constraint**: No upgrade or downgrade affordance (button, dropdown, or link) shall appear, even disabled or grayed out.

**Acceptance Criteria**:
- A model test with fixture packages (some installed, some not, some with version mismatches) asserts each row exposes all five fields with correct values.
- A test verifies that the installed badge text is correct for all three cases (not installed shows no badge, same version, different version).
- Code inspection confirms no action buttons in the row.

---

### REQ-F-005: Result Cap and Footer Message

**Ubiquitous**: If a search query matches more than 500 packages, the table shall display only the first 500 (in ranked order) and show a footer message stating: "Showing first 500 of {N} matches, refine your search to find what you're looking for."

**Constraint**: The footer message shall appear only when the cap is exceeded; the total count N must be accurate even if only 500 are displayed.

**Acceptance Criteria**:
- A model test with a query matching 600 packages verifies that the table displays exactly 500 rows and the footer message shows the correct count of 600.
- A model test with a query matching 500 or fewer packages verifies no footer message appears.
- A unit test of the search/ranking logic verifies the count is correct.

---

### REQ-F-006: Details Panel — Read-Only Reuse

**Event-driven**: When the user selects (clicks on) a row in the table, the application shall display a details panel on the right side of the page (using the shared Installed/Explore details layout) with the following fields:
- Package description
- URL (project or upstream link)
- Licenses (list)
- Dependencies (list)
- Optional dependencies (list)
- Download size (human-readable)
- Installed size (human-readable)

**State-driven**: The details panel is read-only; no edit, install, remove, or upgrade buttons shall be present. The panel may be dismissed or left open; selecting a different row updates the panel to show that package's details.

**Constraint**: Installed-only fields (install date, required-by, config file count, install reason) shall not be displayed in Explore's details panel. This constraint applies whether the package is installed or not. The details panels shall share one header, metadata, description, and scrolling layout; each page supplies its own fields and page-specific sections.

**Acceptance Criteria**:
- Code inspection confirms the field layout and styling are not duplicated between the Installed and Explore panels.
- Code inspection verifies both panels compose the same header, metadata, description, and scrolling components; view tests verify Explore's fields and the absence of installed-only sections.
- A QML test verifies the details panel is read-only (no action buttons for install/remove/upgrade).
- A test verifies that installed-only fields are not shown even if the package is installed.
- A test asserts that selecting a row updates the panel to show that package's details.

---

### REQ-F-007: Index Construction and Loading State

**Event-driven**: At application startup, the application shall immediately begin constructing an in-memory index from all packages in the live sync databases.

**State-driven**: While index construction is in progress:
- The search field shall be disabled and show a placeholder (e.g., "Searching...")
- A progress indicator (spinner, bar, or similar) shall be displayed
- The page shall not display results; instead, it shows a loading state

Once index construction completes:
- The search field shall be enabled
- The loading state shall be replaced with an empty hint state (no search entered yet)
- The user may now enter a search query

**Constraint**: Index construction runs off the UI thread using the same async pattern (QtConcurrent::run + QFutureWatcher) as the installed-packages list model. The index is built once at application startup and persists in memory for the lifetime of the application session; it is not rebuilt on every search.

**Acceptance Criteria**:
- A model test with a fake source that delays index construction asserts that the model's `loading` property is true during construction and false afterward.
- The same test asserts that the search field is disabled during loading and enabled after.
- A model test asserts that a newly constructed `ExploreModel` starts in the loading state with an empty search query and zero displayed rows.

---

### REQ-F-008: Reload Button

**Event-driven**: The Explore page shall display a Reload button in the headline area. When pressed, it shall trigger a rebuild of the package index from the live sync databases, picking up any manual syncs the user may have performed outside the application.

**State-driven**: While index construction is in progress (either initial load or reload), the Reload button shall be shown in a busy/disabled state and further Reload requests shall be ignored.

**Constraint**: Reload is not user-cancellable and never automatic. It makes no network access. Only one load or reload runs at a time. The current search query, if any, is preserved during a reload.

**Acceptance Criteria**:
- A model test with a fake source calls `reload()` twice in a row and asserts the fake source was asked to load exactly once for the reload.
- A test asserts that the Reload button is disabled while a load/reload is in progress and enabled afterward.
- A test asserts that a reload keeps the previous index searchable while rebuilding, replaces it atomically on success, preserves it on failure, and re-executes any active search against the new index.

---

### REQ-F-009: No-Database State

**State-driven**: If the sync directory does not exist or contains no `.db` files (i.e., no configured sync repositories), the application shall display a distinct, explicit state (not an error, not a generic empty state) with a user-facing message such as "No package databases found. Arch package management requires at least one repository to be configured in pacman.conf."

**Constraint**: This state is visually and functionally different from an empty search result (REQ-F-010) and the hint state (REQ-F-002).

**Acceptance Criteria**:
- A test with an invalid or missing sync directory verifies that the application displays the "No package databases" state, not an error.
- A test with a valid but empty sync directory (directory exists, no `.db` files) verifies the same state.
- A test with a valid sync directory containing `.db` files verifies the page does not show the no-database state.

---

### REQ-F-010: No Matches State

**State-driven**: If a search query returns zero matches, the page shall show a message such as "No packages match '{query}'. Note: Configured sync repositories are searched; AUR and local-only packages are not covered."

**Constraint**: This state is visually distinct from the no-database state (REQ-F-009) and the hint state for an empty query (REQ-F-002).

**Acceptance Criteria**:
- A test with a query matching no packages verifies the "No packages match" message is displayed with the query and configured-repositories note.
- A test with a valid query (matching 1+ package) verifies the message is not shown.

---

### REQ-F-011: Freshness Label and Stale-Database Hint

**State-driven**: The Explore page shall display a "Data as of {timestamp}" label in the headline section showing the modification time (mtime) of the oldest sync `.db` file in the sync directory (e.g., "Data as of Sep 22, 14:30").

**State-driven**: If that timestamp is more than 7 days old (strictly greater), the application shall display a soft, non-blocking hint stating that the user's package databases are out of date and should be synced with their package manager, after which they can press Reload. The hint shall not suggest a specific command (a bare `pacman -Sy` risks partial upgrades on Arch). The hint does not prevent interaction and is not an error.

**Constraint**: The 7-day threshold is a soft reminder, not a hard block; the application shall function correctly even if databases are older than 7 days. The stale state shall be computed in the model using an injectable clock. The hint wording and behavior shall match the Updates page (REQ-F-006 in the Updates spec).

**Acceptance Criteria**:
- A model test with an injected clock and a snapshot 6 days old verifies no stale hint is exposed; exactly 7 days verifies no hint; 8 days verifies the hint is exposed.
- A test with a snapshot 2 hours old verifies the label shows a recent timestamp and no hint.
- An adapter test verifies the snapshot's data timestamp equals the mtime of the oldest sync `.db` file.
- A model test verifies a reload that yields a fresh timestamp removes the hint.
- Inspection of the hint text confirms it names no specific pacman command and matches the Updates page wording.

---

### REQ-F-012: Load Error State

**Unwanted-behaviour**: If the initial index construction fails for any reason (database open failure, unexpected exception, I/O error), the application shall not crash and shall display an error state with the reason.

**State-driven**: The error state shall show an error message and a Reload button (enabled) to allow the user to retry.

**Constraint**: No automatic retry; the user retries by pressing Reload.

**Acceptance Criteria**:
- A model test with a fake source that fails the initial load asserts an error state with the reason, an empty list, and that a subsequent `reload()` is accepted.
- A test asserts that the Reload button is enabled in the error state and can be clicked to retry.

---

### REQ-F-013: Architecture — Port, Adapter, Model, QML

**Ubiquitous**: The implementation shall follow the installed-packages layout:
- A domain-layer `ExploreSource` port defining package enumeration, with plain C++ domain types (`Package` or reused types, result and error types).
- An ALPM adapter in `src/backends` implementing the port.
- An application-layer `ExploreModel` orchestrating async index building, searching, and ranking for QML.
- QML under `qml/`, feature-scoped (e.g., `qml/explore/ExploreView.qml`).

**Constraint**: Dependencies point domain ← backends/application ← QML. The port and its domain types shall not depend on Qt GUI or Quick.

**Acceptance Criteria**:
- `ExploreModel` tests run against a fake `ExploreSource` with no ALPM and no QML engine.
- The port header includes no Qt GUI/Quick or libalpm headers.
- `task tidy` and the architecture checks in the repo pass.

---

### REQ-F-014: Page State Preservation

**Ubiquitous**: When the user navigates away from the Explore page and back to it (or to another page and back), the page shall preserve:
- The current search query (if any) and results
- The selected package (if any) and its details panel state

**Constraint**: State is preserved in memory for the application session. It is not persisted across application restarts.

**Acceptance Criteria**:
- A model test asserts that the search query and results are retained when the page is not actively displayed.
- A QML/integration test asserts that navigating to another page and back to Explore restores the search query and selected row.

---

## Non-Functional Requirements

### REQ-NF-001: Search Index Size and Performance

**Ubiquitous**: The in-memory search index (built from all packages in the sync databases) shall be measured as to its size in bytes and memory footprint. A design decision (open question) shall be made regarding whether search can run synchronously on the UI thread or must run off-thread.

**Rationale**: Index size and search performance determine whether real-time, debounced search is feasible on the main thread or requires async execution.

**Acceptance Criteria**:
- A performance test with the full fixture sync databases measures the index size and search execution time for representative queries (e.g., single character, 5-character, 10-character queries).
- The measurement is documented in the design document or in a comment in the implementation.
- A design decision on sync-vs-async search is recorded.

---

### REQ-NF-002: Asynchronous Index Building — Non-Blocking UI

**Event-driven**: The initial index construction (REQ-F-007) and reload (REQ-F-008) shall run off the UI thread and shall expose a loading state while in progress.

**Constraint**: The async mechanism follows REQ-C-002 (same pattern as installed-packages).

**Acceptance Criteria**:
- A model test with a fake source that blocks until released asserts that index construction and subsequent reloads return to the caller before the source completes, and that the loading state is exposed meanwhile.
- The same test asserts results and the enabled search field appear only after construction completes.

---

### REQ-NF-003: Session-Scoped State

**Ubiquitous**: The package index, search results, and selected package live in memory for the app session only and are not persisted.

**Acceptance Criteria**:
- A model test asserts a newly constructed `ExploreModel` starts in the loading state with no index carried over.
- Code inspection confirms no file or settings writes of index or search state.

---

### REQ-NF-004: Test Isolation — Fixture Databases

**Ubiquitous**: All adapter and model unit tests shall use fixture sync databases checked into the repository (e.g., `tests/fixtures/pacman/`), not the live system `/var/lib/pacman`.

**Constraint**: Tests shall not require a system installation of pacman or libalpm beyond what the backends target already links against; they use only fixture databases.

**Acceptance Criteria**:
- A test runs in a CI environment without pacman installed and passes using fixture databases.
- A test on a developer machine returns the fixture's expected results, not the system's installed packages.
- Inspection of test code confirms all database paths use `tests/fixtures/` or a CMake variable, not hardcoded `/var/lib/pacman`.

---

### REQ-NF-005: Fixture Coverage — Test Cases

**Ubiquitous**: The fixture suite (reused or extended from Updates tests) shall include sync databases covering:
- Multiple repositories (e.g., core, extra)
- A variety of packages and versions
- Some packages installed, some not
- Some installed packages with version mismatches
- Package descriptions with keyword matches for testing rank-4 (description-only) search
- Missing and empty sync directories (to test the no-database state)

**Acceptance Criteria**:
- Inspection of `tests/fixtures/pacman/` confirms sync databases with the above coverage.
- Inspection of fixture `.db` files (or fixture generation scripts) confirms packages with description keywords for testing rank-4 search.

---

### REQ-NF-006: Code Style — C++23, Naming Conventions

**Ubiquitous**: All C++ code shall conform to:
- **Language**: C++23 standard
- **Format**: Google-based `.clang-format` (120-column line length, 2-space indentation)
- **Linting**: clang-tidy with `WarningsAsErrors: '*'` (all warnings treated as errors)
- **Naming**:
  - Classes: `CamelCase` (e.g., `ExploreModel`, `AlpmExploreSource`)
  - Functions: `camelCase` (e.g., `buildIndex()`, `search()`)
  - Private data members: `lower_case_` (e.g., `source_`, `index_`, `search_query_`)

**Constraint**: All changes must pass `task format-check` and `task tidy` before being considered complete.

**Acceptance Criteria**:
- Running `task format-check` on the implementation branch reports no violations.
- Running `task tidy` completes without errors or warnings.
- Code review confirms naming conventions are followed throughout.

---

### REQ-NF-007: QML Linting and Controls

**Ubiquitous**: The Explore page QML shall pass `qmllint` and use Holonight.Controls types per the project's QML conventions (HnListDelegate/HnNavigationDelegate for rows; no qmldir-internal types).

**Acceptance Criteria**:
- `task qml-lint` passes on the Explore page QML.
- The QML test suite passes.
- Code inspection confirms only public Holonight.Controls types are imported.

---

### REQ-NF-008: Ranking, Cap, and Count — Unit Tests

**Ubiquitous**: Core search logic (ranking by relevance, capping at 500, counting matches) shall be unit-tested in isolation with a variety of fixture queries and package lists.

**Constraint**: Tests shall pass with zero failures and zero skipped tests. No test shall require network access.

**Acceptance Criteria**:
- A unit test of ranking verifies exact name, prefix, substring, and description-only matches are ordered correctly within each rank and alphabetically within a rank.
- A unit test verifies the cap of 500 is applied and the footer count is accurate for queries matching > 500 packages.
- A unit test verifies match counting is correct for a variety of query and package combinations.

---

### REQ-NF-009: Fake-Source Model Test

**Ubiquitous**: The `ExploreModel` shall be tested against a fake `ExploreSource` that simulates various behaviors (successful load, delayed load, load failure, reload).

**Acceptance Criteria**:
- A model test with a fake source asserts state transitions (loading → loaded, error → loading → loaded).
- A test with a fake source that delays completion asserts that the UI is not blocked and results appear when the source completes.
- A test with a fake source that fails asserts error handling and retry capability (REQ-F-012).

---

### REQ-NF-010: QML View Test

**Ubiquitous**: The Explore page QML (e.g., `ExploreView.qml`) shall be tested by loading it in a window with a model fixture, without visual assertions or screenshot comparisons.

**Constraint**: The test asserts that the QML loads without runtime errors, that model role bindings work, and that basic interactions (typing in search field, clicking rows) execute without crashes. It does not assert visual layout or appearance; those are covered by the manual checklist (REQ-NF-011).

**Acceptance Criteria**:
- A QML test loads `ExploreView.qml` in a window with a fixture model.
- The test asserts the search field can be typed into, rows can be clicked, and the page loads without errors.
- No screenshot or visual comparison is made.

---

### REQ-NF-011: Manual Visual Verification

**Ubiquitous**: Visual correctness is verified by a manual checklist run by the user, not by screenshots or visual checks made by the assistant or by snapshot tooling.

**Acceptance Criteria**:
- An ACCEPTANCE.md file in the docs/sdd/explore-page/ directory contains a checklist covering:
  - Search field layout and placeholder text
  - Result table columns and row layout (name, version, repository, size, installed badge)
  - Ranking visually correct (manual inspection of a few search queries)
  - "No matches" message and wording
  - "No package databases" state
  - Stale-database hint (manual setup with old fixture, or documentation of manual test)
  - Loading state (spinner/progress bar)
  - Error state message
  - Reload button (enabled/disabled states)
  - Details panel (read-only, no action buttons)
  - Light and dark themes
- The checklist is run by the user (not the assistant) and results documented before the feature is considered complete.

---

### REQ-NF-012: Testing — GTest and QML Coverage

**Ubiquitous**: The implementation shall include focused unit and QML tests covering:
- Search ranking (exact, prefix, substring, description-only) (REQ-F-003)
- Search debouncing and in-memory execution (REQ-F-002)
- Result table display and installed badge (REQ-F-004)
- Result capping and footer message (REQ-F-005)
- Details panel reuse and read-only property (REQ-F-006)
- Index construction and loading state (REQ-F-007)
- Reload button and state transitions (REQ-F-008)
- No-database and no-matches states (REQ-F-009, REQ-F-010)
- Freshness label and stale hint (REQ-F-011)
- Load error and retry (REQ-F-012)
- Architecture and port usage (REQ-F-013)
- Live sync database integrity (no modifications) (REQ-C-003)

**Constraint**: Tests shall be executable via `task test` and shall pass with zero failures and zero skipped tests. No test shall require network access.

**Acceptance Criteria**:
- Running `task test` completes successfully with zero failures and zero skipped tests.
- Test code is located in `tests/backends/`, `tests/application/`, and/or `tests/apps/` as appropriate.
- A code reviewer can identify at least one test for each REQ-F-xxx and REQ-C-xxx requirement.

---

### REQ-NF-013: CHANGELOG Entry

**Ubiquitous**: The unreleased changelog entry shall include a summary of the Explore page feature.

**Acceptance Criteria**:
- An entry is added to CHANGELOG.md under "Unreleased" with a concise description (e.g., "feat: add read-only Explore page for package search and discovery").

---

## Constraints

### REQ-C-001: Dependency Isolation — Explicit Paths, No Hardcoding

**Ubiquitous**: The backend adapter shall not hardcode pacman paths (root, dbpath, pacman.conf). Instead, all paths shall be provided as constructor or factory parameters.

**Rationale**: Enables test isolation and deployment flexibility.

**Acceptance Criteria**:
- Source code inspection confirms no occurrence of hardcoded `"/var/lib/pacman"`, `"/etc"`, `"/var"`, or `"/"` as string literals in the ExploreSource adapter.
- A test instantiates the adapter with arbitrary test-fixture paths and verifies they are used.

---

### REQ-C-002: Async Pattern Consistency

**Ubiquitous**: The async implementation for index building and reload shall follow the same pattern established by the installed-packages-list and Updates features (InstalledPackagesModel, UpdatesModel), reusing QtConcurrent or the codebase's established async pattern.

**Rationale**: Ensures consistency and reduces cognitive load for future maintainers.

**Acceptance Criteria**:
- Code inspection confirms ExploreModel uses the same async mechanism (QtConcurrent::run with QFutureWatcher) as InstalledPackagesModel and UpdatesModel.
- Comments or documentation explain why the chosen mechanism was selected.

---

### REQ-C-003: Live Sync Database Integrity

**Ubiquitous**: The live sync databases at the configured dbpath (default `/var/lib/pacman/sync/`) shall never be modified by any operation in this specification. Index building and reload shall only read them.

**Rationale**: Essential for safety; any mutation of live system databases is a critical bug.

**Acceptance Criteria**:
- A test on a temporary copy of the fixture computes SHA-256 checksums, sizes and mtimes of all sync `.db` files before and after index building (and reload) and asserts they are identical.
- If any sync file's content, size or mtime changes, the test fails with a clear message.

---

### REQ-C-004: No Network Access

**Ubiquitous**: The application shall make no network calls in this feature, and no automated test shall attempt any network access.

**Rationale**: Ensures the feature is read-only and local, and tests are deterministic and fast.

**Acceptance Criteria**:
- Inspection of the new source and test code confirms no HTTP client, socket, or download API is used and no `http://`, `https://` or `file://` URL occurs.
- Running `task test` offline (without network access) succeeds with zero skipped tests.

---

### REQ-C-005: Holonight.Controls Integration

**Ubiquitous**: The Explore page shall use Holonight.Controls for interactive and list elements, consistent with the Installed page, and shall not use qmldir-internal types.

**Acceptance Criteria**:
- Code inspection and `task qml-lint` confirm only public Holonight.Controls types are used for interactive and list elements.

---

### REQ-C-006: Sidebar Navigation and Landing Page

**Ubiquitous**: The Explore page shall be added as a new entry in the sidebar navigation menu. The Installed page shall remain the landing page (the page loaded when the application first opens).

**Rationale**: Provides easy access to Explore while preserving the existing user entry point.

**Acceptance Criteria**:
- Code inspection of the navigation/sidebar configuration confirms Explore is listed alongside Installed.
- A test or manual check confirms the application opens on the Installed page, not Explore.

---

## Acceptance Test Plan

| Requirement | Test Type | Expected Outcome |
|---|---|---|
| REQ-F-001 | Model/QML test, qml-lint | Rows expose all fields; empty list valid |
| REQ-F-002 | Model test | Debounce working; empty query shows hint |
| REQ-F-003 | Unit test | Ranking correct (exact, prefix, substring, description) |
| REQ-F-004 | Model/QML test | Table rows show name, version, repo, size, installed badge |
| REQ-F-005 | Unit/model test | Cap at 500; footer message accurate |
| REQ-F-006 | QML test + inspection | Details panel shared with Installed; read-only; no install buttons |
| REQ-F-007 | Model test | Loading state during index build; search disabled until ready |
| REQ-F-008 | Model test | Reload button disabled during load; single-flight; preserves query |
| REQ-F-009 | Adapter + model test | Missing/empty sync dir gives no-databases state |
| REQ-F-010 | Model test | No matches query shows correct message |
| REQ-F-011 | Model test + adapter test | Freshness label correct; 7-day threshold; hint wording |
| REQ-F-012 | Model test | Error state on failed load; Reload button enabled |
| REQ-F-013 | Test + inspection | Model tested without ALPM/QML; port has no GUI headers |
| REQ-F-014 | Integration test | Search query and selection preserved across page transitions |
| REQ-NF-001 | Design doc + measurement | Index size and search time measured; design decision documented |
| REQ-NF-002 | Model test | Calls return before source completes |
| REQ-NF-003/004/005 | Inspection | Fixtures cover repos, searches; missing/empty sync dir |
| REQ-NF-006 | `task format-check`, `task tidy` | Clean |
| REQ-NF-007 | `task qml-lint` | Clean |
| REQ-NF-008 | Unit tests | Ranking, cap, count correct |
| REQ-NF-009 | Model test (fake source) | State transitions, error handling |
| REQ-NF-010 | QML test | Page loads; interactions work without errors; no visual assertions |
| REQ-NF-011 | Manual checklist | Checklist run by user; light and dark themes |
| REQ-NF-012 | `task test` | All pass, zero failures, zero skips, no network |
| REQ-NF-013 | Inspection | CHANGELOG.md Unreleased entry added |
| REQ-C-001 | Inspection + test | No hardcoded pacman paths |
| REQ-C-002 | Inspection | Same async mechanism as Installed/Updates models |
| REQ-C-003 | Adapter test | Sync DB checksums, sizes and mtimes unchanged |
| REQ-C-004 | Inspection | No network code or URLs in feature and tests |
| REQ-C-005 | Inspection, qml-lint | Public Holonight.Controls only |
| REQ-C-006 | Inspection + manual check | Explore in sidebar; Installed is landing page |

---

## Design Decisions

The implementation decisions are recorded in `DESIGN.md`:

1. **Details sharing (§4.1):** Installed and Explore use the same detail frame and content layout, with each page
   supplying its own metadata and extra sections.
2. **Freshness and reload (§4.2):** The models share stateless timestamp and stale-hint helpers while retaining
   separate load state machines. The stale hint appears only when data is more than seven days old.
3. **Search execution (§4.4):** The index is built on a worker; debounced searches run synchronously over precomputed
   Unicode-folded strings. The original timing baseline predates Unicode folding, so current performance should be
   remeasured before relying on its figures.

---

## Related Documents

- **Feature Context**: `docs/ideas/01-high-level-project-idea.md` (Phase 1, Explore page section)
- **Updates Page Specification**: `docs/sdd/pending-updates/SPEC.md` (related vertical slice; freshness and reload patterns)
- **Installed Packages Specification**: `docs/sdd/installed-packages-list/SPEC.md` (related vertical slice; async pattern, details panel)
- **ALPM Sync Database Caching**: `docs/sdd/alpm-sync-db-cache/SPEC.md` (related infrastructure)
- **Project Build & Style**: `CLAUDE.md`

---

## Change History

| Version | Date | Author | Notes |
|---|---|---|---|
| 1.0 | 2026-09-25 | SDD Stage 1 | Initial EARS specification for Explore page feature |
| 1.1 | 2026-09-25 | Review fixes | Corrected debounce acceptance; aligned non-goals; merged REQ-F-014 into REQ-F-006 (renumbered page-state to REQ-F-014); dropped arbitrary test count; installed badge only when installed |
