# Repository Guidelines

Use Conventional Commits for every new commit: `type(scope): imperative summary`, or `type: imperative summary` when a scope adds no clarity.

## Structure

`apps/packages/` owns the `holonight-packages` executable, application wiring, and QML module registration. Reusable
C++ targets live in `src/domain/`, `src/application/`, `src/backends/`, `src/advisor/`, `src/persistence/`, and
`src/platform/`. QML sources are feature-scoped under `qml/`; design notes and mockups live in `docs/`; tests live in
`tests/`.

## Commands

Use `task` as the primary workflow: `task configure`, `task build`, `task run`, `task test`, `task format-check`,
`task tidy`, and `task qml-lint`. These commands validate and stage pinned sibling configuration/provider dependencies in `build/dependencies` first.

## Style and tests

Use C++23 and the checked-in clang-format/clang-tidy configuration. Classes use `CamelCase`, functions use
`camelBack`, and private data members use `lower_case_`. Add focused GTest coverage with behavior changes and keep QML
files grouped by feature.

`domain`, `application`, `backends` and `persistence` are static libraries; `advisor` and `platform` remain
interface stubs. Preserve the current ownership and dependency direction.

Application QML imports `QtQuick.Controls as Controls` and qualifies instances, enums and attached properties.
Keep Core/composite imports and semantic painting. Use public composite enums; HnSelectableDelegate is internal.
The executable embeds its overridable Holonight default; do not set the style imperatively. Build discovery is
restricted to the exact configured executable; installed copies use executable-relative discovery.

Run policy fixtures, dual-style runtime acceptance and existing QML tests for UI changes. Keep the compiled
acceptance module under `tests/runtime` separate from source-based tests. Its source boundary is MockPackageSource,
with real presentation/filter models. Preserve table horizontal and independent list/detail/page vertical scrolling.
Production launch checks may only enumerate ALPM read-only with isolated HOME/XDG and desktop activation.
No pointer/focus automation or package transactions. See `docs/sdd/unified-qtquick-controls/SPEC.md`.
