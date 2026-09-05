# Installed page acceptance evidence

Date: 2026-09-05. Toolchain: Qt 6.11.2, Debug build, offscreen QML tests.

Status: Automated review regressions verified. Stakeholder confirmed manual/visual acceptance against the
mockup on 2026-09-05; T-030 is closed.

The stakeholder approval covers the reviewed baseline. Subsequent review fixes have the automated evidence
below; this report does not claim a new manual mockup comparison for those fixes. Scrolling at 60 fps remains
unmeasured, independently of the verified 100 ms filtering/sorting budget.

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
horizontally together. This baseline responsive behavior was included in the recorded stakeholder acceptance.

### Follow-up review regressions

All five following tests failed before their fixes and pass afterward.

| Finding | Regression coverage | Result |
|---|---|---|
| Toolbar controls and category tabs extended outside the minimum window | `ToolbarAndCategoryTabsRemainReachableAtMinimumWindowSize`: control hit targets fit at 720×480 and 1100×720, at least one table row fits vertically, and page scrolling reaches the footer | Pass |
| Arrow keys changed the highlight without updating package details | `ArrowKeysKeepHighlightedRowAndPackageDetailsInSync`: Up/Down navigation, first/last row boundaries, filtering to a replacement at row zero, and empty results | Pass |
| Category tabs had empty accessible names | `CategoryTabsExposeTheirVisibleAccessibleNames`: the accessibility interface exposes each visible category label | Pass |
| Optional dependencies could only be expanded by pointer | `OptionalDependenciesCanBeExpandedFromTheKeyboard`: Tab reaches the named expansion button and Space reveals the sixth dependency | Pass |
| Loading packages into an already visible page triggered a height binding loop | `PageLayoutRemainsFreeOfBindingLoopsDuringLoadAndResize`: release a deferred backend result after the window is visible, resize across compact/wide layouts, and assert the QML engine emits no warnings | Pass |

The toolbar moves its title above the controls on narrow pages, and category tabs wrap. The page scrolls
vertically when needed, retaining minimum table/detail heights so wrapping cannot collapse the table viewport.
Its content uses a natural-height `Column`; the table/detail area fills the remaining viewport space without
binding the outer layout's height to its own implicit height.

## REQ-NF-121 checkpoints

| # | Checkpoint | Evidence / remaining verification |
|---|---|---|
| 1 | Domain model | Package construction, extended-field, and equality tests pass |
| 2 | Backend population | libalpm fixture tests pass |
| 3 | Orphan logic | Classification and aggregate tests pass |
| 4 | Model roles and aggregates | InstalledPackagesModel tests pass |
| 5 | Tab filter | Four-category/default filter tests and accessible-name regression pass; baseline badge appearance covered by stakeholder acceptance |
| 6 | Search composition | Case-insensitive search and composed filtering tests pass |
| 7 | Sorting | Name/size and direction tests pass |
| 8 | Repository filter | Repository options and composed filtering tests pass |
| 9 | All states dropdown | Single inert option confirmed by code review; manual interaction covered by recorded stakeholder acceptance |
| 10 | List/Grid toggle | Exclusive selection and inert table behavior tested |
| 11 | Data table | Column and control reachability regressions pass; baseline mockup comparison accepted |
| 12 | Visual-only checkboxes | No bulk-action/selection handlers confirmed by code review; manual click isolation covered by recorded stakeholder acceptance |
| 13 | Detail panel | Selection, keyboard navigation, metadata refresh, empty states, and keyboard dependency expansion regressions pass; baseline presentation accepted |
| 14 | Inert action buttons | No mutation handlers confirmed by code review; manual affordances covered by recorded stakeholder acceptance |
| 15 | Out-of-scope sections | Disabled placeholders and absence of transaction handlers confirmed by code review |
| 16 | Global orphan footer | Aggregate tests, global-model bindings, and scroll reachability verified; baseline appearance accepted |
| 17 | Tests | `task test`: 93/93 tests pass, including five follow-up regressions |
| 18 | Responsiveness | Existing 5,000-package filter/sort test and late-selection regression pass; 60 fps scrolling has not been measured |
| 19 | Tooling | QML lint, formatting and clang-tidy checks recorded below |

## Verification

- `task test`: full application build (including QML cache generation) and 93/93 tests passed after follow-up fixes.
- `cmake --build build --target qml-lint`: passed without warnings after follow-up fixes.
- `cmake --build build --target format-check`: passed after follow-up fixes.
- `cmake --build build --target tidy`: checked all application and test sources with no findings after follow-up fixes.
- Both performance regressions passed in the full test run; `git diff --check` also passed.
- Existing Qt tooling workarounds remain in place. Both known issues were independently reproduced during review.

## Sign-off

Stakeholder manual/visual acceptance was confirmed on 2026-09-05 and T-030 is closed. The manual checkpoints
above refer to that recorded approval; no separate named product-owner or technical-lead signature is recorded.
Follow-up review fixes are verified by the automated checks above. The 60 fps scrolling target has not been
measured and must not be inferred from stakeholder acceptance or the filtering performance tests.
