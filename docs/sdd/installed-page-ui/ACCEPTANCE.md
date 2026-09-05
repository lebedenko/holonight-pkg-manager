# Installed page acceptance evidence

Date: 2026-09-05. Toolchain: Qt 6.11.2, Debug build, offscreen QML tests.

Status: Automated review regressions verified. Full stakeholder acceptance is pending; T-030 remains open.
This report records evidence, not stakeholder sign-off or a claim that all visual requirements passed.

## Review regressions

All four added regression tests failed against the reviewed implementation before the fixes and pass afterward.

| Finding | Regression coverage | Result |
|---|---|---|
| Detail panel retained the previous package at row zero | `DetailPanelFollowsReplacementAtSameRowAndMetadataRefresh`: search from apple to foreign-tool at row zero, refresh its version, then filter to an empty result | Pass |
| Table names collapsed and metadata columns were clipped | `TableKeepsReadableColumnsAtDefaultAndMinimumWindowWidths`: readable names, a usable viewport, and horizontal access to the final column with its header aligned at 1100×720 and 720×480 | Pass |
| Filtering repeatedly scanned a late selection | `RepositoryFilteringPreservesLateSelectionWithinResponsivenessBudget`: 5,000 alternating-repository packages, final package selected, apply and clear a repository filter in under 100 ms each while preserving selection | Pass |
| List/Grid could both be checked | `ListAndGridChoicesRemainExclusiveAndDoNotChangeTheTable`: activate Grid, then List twice; exactly one stays checked and table contents remain present | Pass |

Before the batching fix, the new performance regression measured 1,193 ms to filter and 1,244 ms to clear the
filter. Its assertions cover the filtering operations separately from fixture loading.

The table preserves the side-by-side layout when page content is at least 780 px wide, and moves details below
the table otherwise. An 850 px table content width keeps package names readable; header and rows scroll
horizontally together. Stakeholder visual acceptance of this responsive behavior remains pending.

## REQ-NF-121 checkpoints

| # | Checkpoint | Evidence / remaining verification |
|---|---|---|
| 1 | Domain model | Package construction, extended-field, and equality tests pass |
| 2 | Backend population | libalpm fixture tests pass |
| 3 | Orphan logic | Classification and aggregate tests pass |
| 4 | Model roles and aggregates | InstalledPackagesModel tests pass |
| 5 | Tab filter | Four-category/default filter tests pass; visual badge acceptance pending |
| 6 | Search composition | Case-insensitive search and composed filtering tests pass |
| 7 | Sorting | Name/size and direction tests pass |
| 8 | Repository filter | Repository options and composed filtering tests pass |
| 9 | All states dropdown | Single inert option confirmed by code review; manual interaction pending |
| 10 | List/Grid toggle | Exclusive selection and inert table behavior tested |
| 11 | Data table | Layout and horizontal reachability regression passes; full mockup comparison pending |
| 12 | Visual-only checkboxes | No bulk-action/selection handlers confirmed by code review; manual click isolation pending |
| 13 | Detail panel | Selection, same-row replacement, refreshed metadata, and empty-state regressions pass; dependency expansion/manual presentation pending |
| 14 | Inert action buttons | No mutation handlers confirmed by code review; manual affordance checks pending |
| 15 | Out-of-scope sections | Disabled placeholders and absence of transaction handlers confirmed by code review |
| 16 | Global orphan footer | Aggregate tests and direct global-model bindings verified; visual acceptance pending |
| 17 | Tests | `task test`: 88/88 tests pass |
| 18 | Responsiveness | Existing 5,000-package filter/sort test and late-selection regression pass; 60 fps scrolling has not been measured |
| 19 | Tooling | QML lint, formatting and clang-tidy checks recorded below |

## Verification

- `task test`: full application build (including QML cache generation) and 88/88 tests passed.
- `task qml-lint`: passed without warnings.
- `cmake --build build --target format-check`: passed; the final test-fixture adjustment also passed a targeted
  `clang-format --dry-run --Werror` check.
- `cmake --build build --target tidy`: checked all application and test sources. Its one finding (reserve the
  performance fixture's vector capacity) was fixed and the affected file passed a targeted clang-tidy recheck.
- The performance regression passed again after that fixture adjustment; `git diff --check` also passed.
- Existing Qt tooling workarounds remain in place. Both known issues were independently reproduced during review.

## Sign-off

Stakeholder mockup comparison, the manual checks listed above, and product/technical sign-off are pending.
