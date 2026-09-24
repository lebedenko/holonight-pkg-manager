# SDD Tasks — pending-updates

Each task includes its own GTest coverage and CMake edits for the files it adds, and leaves `task build` green.
C++ tasks also end with `task format` and `task tidy` clean for the touched files. The domain, backends and
application targets are already `STATIC`; no target conversion is needed.

- [x] T-001: Domain types and `UpdateSource` port
  - REQs: REQ-F-003, REQ-F-005, REQ-F-010, REQ-F-013
  - Check: `pending_update.h`, `update_source.h` and `src/update_source.cpp` (out-of-line destructor, added to `src/domain/CMakeLists.txt` and the umbrella header) compile with no Qt or libalpm includes, and `task build` succeeds; `UpdateSource` exposes only `loadUpdates() const` returning `std::expected<UpdateSnapshot, UpdateSourceError>` with codes `ConfigurationInvalid`, `DatabaseOpenFailed`, `Unknown`.

- [x] T-002: `UpdateSummary` and name sorting
  - REQs: REQ-F-002, REQ-F-009
  - Check: `update_summary_test` passes, including 10 normal plus 3 ignored rows giving `updateCount == 7`, a `totalDownloadBytes` that excludes the ignored rows, and `ignoredCount == 3`.

- [x] T-003: Signed size formatter
  - REQs: REQ-F-002
  - Check: a test in the existing size-formatter test file asserts `formatSignedSizeBytes` returns "+12.3 MiB", "-340 KiB" and "0 B" for the matching inputs.

- [x] T-004: Updates fixture tree
  - REQs: REQ-NF-003, REQ-NF-004
  - Check: `tests/fixtures/pacman/updates/{local,sync,pacman.conf,README.md}` exist and the README documents regeneration; the fixture holds a newer, an equal, an older, a foreign, an `IgnorePkg`-glob and an `IgnoreGroup` package, and a static `pacman.conf` with multi-line `IgnorePkg`/`IgnoreGroup`, with no `http(s)://` or `file://` URLs anywhere in it.

- [x] T-005: `pacman.conf` ignore-list parser
  - REQs: REQ-F-010, REQ-C-001
  - Check: `pacman_config_test` passes: `[options]` `IgnorePkg`/`IgnoreGroup` accumulate across lines, comments and unknown keys/sections are ignored, and a missing or unreadable file returns an error rather than empty lists.

- [x] T-006: Ignore matching helper
  - REQs: REQ-F-009, REQ-F-010
  - Check: `update_matching_test` passes: an `ign*` glob matches by name, an `IgnoreGroup` pattern matches a sync package's group, and a non-matching package is not flagged.

- [x] T-007: `AlpmUpdateSource` live comparison and states
  - REQs: REQ-F-003, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-013, REQ-C-001, REQ-C-004
  - Check: `alpm_update_source_test` passes against the fixture: newer versions are listed, equal and older are not, foreign packages are skipped, `dataAsOf` equals the oldest `sync/*.db` mtime, a missing or empty sync dir gives `databasesFound == false`, a valid sync with nothing newer gives `databasesFound == true` and no rows, an unreadable `pacman.conf` gives `ConfigurationInvalid`, and the options struct is the only source of paths.

- [x] T-008: Ignored flagging in the adapter
  - REQs: REQ-F-009, REQ-F-010
  - Check: an adapter test with the fixture `pacman.conf` marks exactly the `IgnorePkg` and `IgnoreGroup` matches as `ignored` and no other rows.

- [x] T-009: Cache coexistence and sync-file integrity
  - REQs: REQ-F-012, REQ-C-003, REQ-NF-003, REQ-NF-004
  - Check: adapter tests on a temporary copy of the fixture show that (a) SHA-256, size and mtime of every sync `.db` are identical before and after two `loadUpdates()` calls, and (b) touching a sync `.db` (content changed) makes the next call reflect it while an unchanged one returns a stable result via the owned `AlpmConnectionCache`.

- [x] T-010: `UpdatesModel` load and single-flight reload
  - REQs: REQ-F-004, REQ-F-011, REQ-NF-001, REQ-C-002
  - Check: `updates_model_test` (with a blocking fake `UpdateSource`, files added to `apps/packages/CMakeLists.txt` and `tests/CMakeLists.txt`) passes: the constructor returns before the source completes with `loading == true`, results appear only after release, a second `reload()` while loading calls the source once, and `UpdatesModel` uses `QtConcurrent::run` plus `QFutureWatcher` like `InstalledPackagesModel`.

- [x] T-011: Model failure, freshness and summary properties
  - REQs: REQ-F-002, REQ-F-005, REQ-F-006, REQ-NF-002
  - Check: `updates_model_test` passes: a failed reload keeps rows and the previous `dataAsOf` and sets `reloadErrorMessage`, a later success clears it, a failed initial load gives `Error` state with `errorMessage` and Reload still accepted, an injected clock gives no hint at 6 d and 7 d and a hint at 8 d, headline count and total exclude ignored rows, and a fresh model carries no prior state.

- [x] T-012: Model states and roles
  - REQs: REQ-F-001, REQ-F-007, REQ-F-008
  - Check: `updates_model_test` passes: all nine roles (`name`, `installedVersion`, `availableVersion`, `repository`, `downloadSize`, `downloadSizeLabel`, `sizeDelta`, `sizeDeltaLabel`, `isIgnored`) return fixture values, and the states `Updates`, `UpToDate` (with a non-empty `officialOnlyNote`), `NoDatabases` and `Error` are each reachable from the fake source.

- [x] T-013: QML notice and headline components
  - REQs: REQ-F-002, REQ-F-005, REQ-F-006, REQ-C-005
  - Check: `qml/updates/UpdatesInlineNotice.qml` and `UpdatesHeadline.qml` load under `task qml-lint` with public Holonight.Controls types only and show count, total download size, the "Data as of" label, and an ignored-count caption when `ignoredCount > 0`.

- [x] T-014: QML table and row components
  - REQs: REQ-F-001, REQ-F-009, REQ-C-005
  - Check: `UpdatesTable.qml`, `UpdateRow.qml` (`HnListDelegate`, `required property` per role, an "Ignored" indicator when `isIgnored`) and `UpdatesColumns.qml` pass `task qml-lint`, and the row exposes every field of REQ-F-001.

- [x] T-015: `UpdatesView` page
  - REQs: REQ-F-001, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-C-005
  - Check: `qml/updates/UpdatesView.qml` has an `Item` root with `required property UpdatesModel updatesModel`, a Reload button disabled while `loading`, the reload banner and stale hint bound to their model properties, and distinct loading, no-databases, up-to-date and error states; it passes `task qml-lint`, and the stale hint names no specific command.

- [x] T-016: Page switching and application wiring
  - REQs: REQ-F-013, REQ-C-001
  - Check: `Sidebar.qml` gains `currentPage` and `pageRequested`, `WorkspaceWindow.qml` gains `required property UpdatesModel updatesModel` and a `StackLayout` defaulting to Installed, `PackagesApplication` builds `AlpmUpdateSource` and `UpdatesModel` with the production paths and destroys the view before the models, and `installed_packages_view_test` and `test_runtime_controls` are updated to pass a fake-source model and still pass.

- [x] T-017: `updates_view_test`
  - REQs: REQ-F-001, REQ-F-002, REQ-F-009
  - Check: `updates_view_test` loads `UpdatesView.qml` over a fake-source `UpdatesModel` and passes: rows expose all roles, ignored rows are flagged, the headline excludes ignored rows, zero updates gives an empty valid list rather than an error, and no assertion is visual.

- [x] T-018: Manual acceptance checklist
  - REQs: REQ-NF-008
  - Check: `docs/sdd/pending-updates/ACCEPTANCE.md` lists, for light and dark themes, list layout and scrolling, ignored badge, headline, Reload busy state, reload banner, stale hint, no-databases state, up-to-date state with the official-only note, and page switching, with steps to run `task run`; the assistant does not run or screenshot it.

- [x] T-019: Full verification
  - REQs: REQ-NF-005, REQ-NF-006, REQ-NF-007, REQ-C-004
  - Check: `task format-check`, `task tidy`, `task qml-lint` and `task test` all pass with at least 15 tests, zero failures and zero skips, and a grep of the new tests and sources finds no `http://`, `https://` or `file://`.

- [x] T-020: Type and import scripts
  - REQs: REQ-NF-006, REQ-C-005
  - Check: `scripts/check-qmltypes.sh` and `scripts/check-runtime-qml-imports.sh` pass, with `UpdatesModel` and the new QML types acknowledged if the scripts require it, and no qmldir-internal type is imported.

- [x] T-021: Changelog
  - REQs: REQ-NF-007
  - Check: `CHANGELOG.md` has an Unreleased entry for the read-only Updates page (live comparison, Reload, ignored flagging, no in-app database sync), and the README is updated only if it lists features or build steps this changes.
