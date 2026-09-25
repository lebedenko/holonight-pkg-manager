# SDD Tasks — explore-page

Each task includes its own GTest coverage and CMake edits for the files it adds, and leaves `task test` green.
C++ tasks also end with `task format` and `task tidy` clean for the touched files. The domain, backends and
application targets are already `STATIC`; no target conversion is needed.

- [x] T-001: Domain types and `ExploreSource` port
  - REQs: REQ-F-013, REQ-C-001
  - Check: `src/domain/include/holonight_packages_domain/sync_package.h`, `explore_source.h` and `src/domain/src/explore_source.cpp` (out-of-line destructor, added to `src/domain/CMakeLists.txt` and the umbrella header) compile with no Qt or libalpm includes; `ExploreSource` exposes only `loadPackages() const` returning `std::expected<ExploreSnapshot, ExploreSourceError>` with codes `DatabaseOpenFailed`, `Unknown`; `task build` succeeds.

- [x] T-002: Shared sync database files helper
  - REQs: REQ-F-009, REQ-F-011, REQ-C-001, REQ-C-003
  - Check: `src/backends/src/sync_database_files.h` and `.cpp` with functions `oldestSyncDatabaseTime(databasePath)` and `checkLocalDatabase(databasePath)`, both returning `std::string` errors; no Qt or libalpm includes; `src/backends/src/alpm_update_source.cpp` refactored to use the shared helpers; existing `alpm_update_source_test` passes; no hardcoded paths in the new functions.

- [x] T-003: `explore_search` index, ranking and cap
  - REQs: REQ-F-003, REQ-F-005, REQ-NF-001, REQ-NF-008
  - Check: `src/application/include/holonight_packages_application/explore_search.h` (with a comment recording the measured index size and search times and the synchronous-search decision from DESIGN §4.4) and `.cpp` with `ExploreIndex`, `SearchResult`, `searchPackages()` function, `kMaxSearchResults = 500`; `explore_search_test` passes: vim scenario ranks 0–3 correctly, alphabetical within rank (`vim-runtime` before `vimb`), description-only match, case-insensitive, 600 matches capped at 500 with `totalMatches == 600`, 500 matches shows no cap effect, query trimmed, multi-word literal.

- [x] T-004: Data freshness helper and `DataFreshness` Qt layer
  - REQs: REQ-F-011, REQ-NF-002, REQ-C-002
  - Check: `src/application/include/holonight_packages_application/data_freshness.h` (header-only, no Qt) with `kStaleAfter = std::chrono::days{7}` and `isStale(dataAsOf, now)` function; `apps/packages/app/DataFreshness.h` and `.cpp` with `toQDateTime`, `dataAsOfLabel`, `staleHintText` (Qt-side, using `QCoreApplication::translate`); `UpdatesModel` modified to call them and expose `staleHintText` property; `data_freshness_test` passes with 6 d / 7 d / 8 d / 2 h boundaries; `updates_model_test` and `updates_view_test` pass unchanged.

- [x] T-005: Explore fixture tree
  - REQs: REQ-NF-004, REQ-NF-005, REQ-C-004
  - Check: `tests/fixtures/pacman/explore/{local,sync,generate.sh,README.md}` exist; fixture holds packages: `vim` (core 9.1-1, installed 9.0-1), `vim-runtime` (core, installed same version), `vimb` (extra), `gvim` (extra), `neovim` (extra), `nano` (core), `dup` (core 1.0-1 and extra 2.0-1), `bare` (no desc/url/deps), `foreign-tool` (local only, not in sync); expected query "vim" order: vim, vim-runtime, vimb, gvim, neovim, nano; no `http://`, `https://`, or `file://` URLs in fixture.

- [x] T-006: `AlpmExploreSource` adapter and tests
  - REQs: REQ-F-009, REQ-F-013, REQ-C-001, REQ-C-003, REQ-C-004
  - Check: `src/backends/include/holonight_packages_backends/alpm_explore_source.h` with `AlpmExploreSourceOptions` and `AlpmExploreSource : ExploreSource`; `src/backends/src/alpm_explore_source.cpp` with conversion logic; `alpm_explore_source_test` passes: field mapping (name, version, repository, description, URL, licenses, dependencies, sizes), installed versions from local database, duplicate names in different repositories both listed, missing/empty sync dir gives `databasesFound == false`, `dataAsOf` equals oldest `.db` mtime, reload picks up manual syncs via cache invalidation, adapter tests use temp copies (not `/var/lib/pacman`), SHA-256/size/mtime of every sync `.db` unchanged before and after loads.

- [x] T-007: `sync_database_files_test`
  - REQs: REQ-F-009, REQ-C-001
  - Check: `sync_database_files_test` passes: missing database path, missing `sync/` directory, `sync/` without `.db` files all return "no databases" error; valid sync directory with `.db` files does not error; behavior unchanged from existing `alpm_update_source_test`.

- [x] T-008: `ExploreModel` async load and single-flight reload
  - REQs: REQ-F-007, REQ-F-008, REQ-NF-002, REQ-NF-009
  - Check: `apps/packages/app/ExploreModel.h` and `.cpp` added to `apps/packages/CMakeLists.txt` and `tests/CMakeLists.txt`; `tests/apps/fake_explore_source.h` with controllable source (queue, blocking gate, call count); `explore_model_test` with blocking fake: constructor returns before source completes with `loading == true`, results appear only after release, second `reload()` while loading calls source once total, uses `QtConcurrent::run` plus `QFutureWatcher` like `UpdatesModel`, `ExploreModel.h` added to QML module `SOURCES`.

- [x] T-009: `ExploreModel` debounce and search states
  - REQs: REQ-F-002, REQ-F-003, REQ-F-005, REQ-F-010
  - Check: `explore_model_test` passes: N rapid keystrokes within 25 ms debounce result in `searchCount() == 1` with the final query, two separated keystrokes result in `searchCount() == 2`, empty or whitespace-only query shows `Hint` state without running search, injected clock gives no hint at 6 d and 7 d and hint at 8 d, `matchCount` vs `rowCount()` correctly handled, cap footer text shows when `matchCount > 500`, no-matches state message includes the query and configured-repositories note.

- [x] T-010: `ExploreModel` installed badge and selection
  - REQs: REQ-F-004, REQ-F-014
  - Check: `explore_model_test` passes: not installed shows empty badge text and `isInstalled` false, same version shows "Installed", different version shows "Installed {version}" and `installedVersionDiffers` true; `currentRow` and `currentPackage` accessible; selection reconciled after result swap (found keeps row, not found clears); selection preserved across results and navigation, identity is `repository + "/" + name`.

- [x] T-011: `ExploreModel` roles and state machine
  - REQs: REQ-F-001, REQ-F-007, REQ-F-012, REQ-NF-003, REQ-NF-009
  - Check: `explore_model_test` passes: all roles (`name`, `availableVersion`, `repository`, `description`, `downloadSize`, `downloadSizeLabel`, `installedSize`, `installedSizeLabel`, `url`, `licenses`, `dependencies`, `optionalDependencies`, `isInstalled`, `installedVersion`, `installedBadgeText`, `installedVersionDiffers`) return fixture values; states `Loading`, `NoDatabases`, `Error`, `Hint`, `NoMatches`, `Results` are each reachable; failed initial load gives `Error` with `errorMessage`; failed reload with an index keeps rows and sets `reloadErrorMessage`; later success clears messages; a fresh model starts in `Loading` with an empty query, zero rows and no carried-over state, and a grep of the new files finds no `QSettings` or file writes; state transitions loading -> loaded and error -> loading -> loaded are asserted.

- [x] T-012: Shared details QML components extraction
  - REQs: REQ-F-006, REQ-C-005
  - Check: `qml/packages/PackageDetailFrame.qml` (new, `HnSurfaceFrame` shell), `PackageDetailDescription.qml` (new, description label), `PackageChipList.qml` (new, titled chip list with expander) exist; `PackageDetailPanel.qml`, `PackageDetailHeader.qml`, `PackageDetailMetadataRows.qml`, `PackageDetailDependencySections.qml` refactored to use them; `task qml-lint` passes; `installed_packages_view_test` passes unchanged (regression net); objectNames preserved in shared components.

- [x] T-013: `ExploreDetailPanel` QML component
  - REQs: REQ-F-004, REQ-F-006
  - Check: `qml/explore/ExploreDetailPanel.qml` loads in a window without errors, composes `PackageDetailFrame` with `PackageDetailHeader` (no actions), `PackageDetailMetadataRows` with Version/Download size/Installed size/License/URL rows, `PackageDetailDescription`, dependencies and optional dependencies chips; no Remove/Installed/Required-by/Local-state labels or buttons; shared `packageDetail*` objectNames present; `task qml-lint` passes.

- [x] T-014: `ExploreView` and table components
  - REQs: REQ-F-001, REQ-F-002, REQ-F-004, REQ-F-007, REQ-F-008, REQ-C-005
  - Check: `qml/explore/ExploreView.qml`, `ExploreHeadline.qml`, `ExploreTable.qml`, `ExploreRow.qml`, `ExploreColumns.qml` exist; `ExploreView` has `required property ExploreModel exploreModel`, `Item` root, page-level states (Loading, NoDatabases, Error, Hint, NoMatches, Results), Reload button disabled while `loading`, search field enabled by `searchEnabled`, table and details panel visible on `Results`, no action buttons; `ExploreRow` uses `HnListDelegate`, exposes all five table fields; `task qml-lint` passes all files.

- [x] T-015: `ExploreView` test
  - REQs: REQ-F-001, REQ-F-002, REQ-F-004, REQ-F-006, REQ-F-010, REQ-F-014, REQ-NF-010
  - Check: `explore_view_test` (`rejectQmlWarnings`, no screenshots) loads `ExploreView.qml` in a `QQuickWindow` with a fake-source `ExploreModel` and fixture packages; passes: rows expose all roles via `ListView`, details panel updates on row click, stale hint and reload banner visibility, no visual assertions, `QTest::keyClicks` into search field and `QTest::mouseClick` on rows without crashes, page persists state during destruction/recreation (model outlives view).

- [x] T-016: Workspace navigation and wiring
  - REQs: REQ-C-006, REQ-F-014
  - Check: `qml/workspace/Sidebar.qml` enables Explore entry (`checked: currentPage === "explore"`, `onClicked: pageRequested("explore")`); `WorkspaceWindow.qml` adds `required property ExploreModel exploreModel`, `StackLayout` with `pageIndex` map (`installed: 0, updates: 1, explore: 2`), third child `ExploreView`, default `currentPage = "installed"`; existing `installed_packages_view_test.cpp::createWorkspace` and `updates_view_test.cpp::createWorkspace` updated to inject `exploreModel`, both pass; `test_runtime_controls.cpp::SetUp` updated, passes.

- [x] T-017: `PackagesApplication` wiring and QML type registration
  - REQs: REQ-F-013, REQ-C-001
  - Check: `PackagesApplication.cpp` constructs `AlpmExploreSource` with options `{"/", "/var/lib/pacman"}`, creates `ExploreModel`, injects into view as `exploreModel` property, destructor resets view before models; `tests/CMakeLists.txt` compiles `ExploreModel.cpp` and `DataFreshness.cpp` into `test_holonight_packages` and `test_runtime_controls`; types registered via `qmlRegisterUncreatableType<ExploreModel>` in both; `scripts/check-qmltypes.sh` updated to list `"ExploreModel"`.

- [x] T-018: Full verification and linting
  - REQs: REQ-NF-006, REQ-NF-007, REQ-NF-012, REQ-C-004
  - Check: `task format-check` and `task tidy` pass all new C++ files with no violations; `task qml-lint` passes all new QML files; `task test` passes with zero failures and zero skips; grep of new sources/tests finds no `http://`, `https://`, or `file://`; offline `task test` run succeeds.

- [x] T-019: Manual acceptance checklist
  - REQs: REQ-NF-011
  - Check: `docs/sdd/explore-page/ACCEPTANCE.md` records the user's report that light and dark theme checks passed on 2026-09-25, including the follow-up layout and copy changes.

- [x] T-020: CHANGELOG and README
  - REQs: REQ-NF-013
  - Check: `CHANGELOG.md` has Unreleased entry for Explore feature (read-only search, live sync databases, Reload, no network, no in-app sync); `README.md` updated with `SyncPackage`/`ExploreSource` in domain-layer table, `AlpmExploreSource` in backends table, `searchPackages`/`ExploreIndex` in application layer table, and Explore feature mentioned in intro paragraph.

- [x] T-021: Review follow-up
  - REQs: REQ-F-003, REQ-F-006, REQ-F-008
  - Check: full Unicode case folding uses ICU; both detail panels use `PackageDetailContent` for their common layout; successful reload replaces index and rows inside one model reset. Regression tests cover Unicode description search, old-row reads during `modelAboutToBeReset`, and the badge and scope note for a third-party sync repository. The user reported that both themes pass with the follow-up layout and copy.
