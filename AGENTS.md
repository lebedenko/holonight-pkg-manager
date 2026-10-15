# Repository Guidelines

Use Conventional Commits for every new commit: `type(scope): imperative summary`, or `type: imperative summary` when a scope adds no clarity.

## Structure

`apps/packages/` owns the `holonight-packages` executable, application wiring, and QML module registration.
`apps/packaged/` owns the `holonight-packaged` session D-Bus service (adaptor, activation files, introspection XML);
its `UpdateMonitor` lives in `src/application/`. Reusable
C++ targets live in `src/domain/`, `src/application/`, `src/backends/`, `src/advisor/`, `src/persistence/`, and
`src/platform/`. QML sources are feature-scoped under `qml/`; design notes and mockups live in `docs/`; tests live in
`tests/`.

## Commands

Use `task` as the primary workflow: `task configure`, `task build`, `task run`, `task test`, `task format-check`,
`task tidy`, and `task qml-lint`.

## Style and tests

Use C++23 and the checked-in clang-format/clang-tidy configuration. Classes use `CamelCase`, functions use
`camelBack`, and private data members use `lower_case_`. Add focused GTest coverage with behavior changes and keep QML
files grouped by feature.

`domain`, `application`, `backends` and `persistence` are static libraries (`application` links Qt Core and
Concurrent for `UpdateMonitor`); `holonight_packages_snapshot_store` is a libalpm-free static library (JSON update
snapshot store); `advisor` and `platform` remain
interface stubs. Preserve the current ownership and dependency direction.

Application QML imports `QtQuick.Controls as Controls` and qualifies instances, enums and attached properties.
Keep Core/composite imports and semantic painting. Use public composite enums; HnSelectableDelegate is internal.
The executable embeds its overridable Holonight default; do not set the style imperatively. Build discovery is
restricted to the exact configured executable; installed copies use executable-relative discovery.

Run policy fixtures, dual-style runtime acceptance and existing QML tests for UI changes. Keep the compiled
acceptance module under `tests/runtime` separate from source-based tests. Its source boundary is MockPackageSource,
with real presentation/filter models. Preserve table horizontal and independent list/detail/page vertical scrolling.
Production launch checks may only enumerate ALPM read-only with isolated HOME/XDG and desktop activation.
No pointer/focus automation or package transactions.
Carve-out: tests may sync only fixture repositories under a temp dir via file://; nothing runs package transactions.
See `docs/sdd/unified-qtquick-controls/SPEC.md`.



## Development and documentation

Use `task build` and `task test` for incremental native CMake/CTest work. Native builds
consume installed packages; use `CMAKE_PREFIX_PATH` for an explicit development prefix.
Use `task format` for the pinned formatter and `task check` for container acceptance.
Run `task check:integration` for installation/runtime changes. Container builds use
fresh build/prefix directories, separate from `build/dev` and `build/release`.
clangd reads `build/dev/compile_commands.json` directly.

Routine changes need tests and a commit explanation; update user documentation when
behavior changes. Substantial designs need one local SDD describing intent, decisions,
interfaces and acceptance. Cross-project changes need one umbrella note linking local
designs and stating contracts, dependency order and integration checks. Historical SDDs
remain historical. No separate planning, readiness, handoff or closure commits are required.
Keep routine logs and generated evidence in ignored build directories or CI artifacts.
Provider and consumer implementation may proceed together; publish providers before
consumer commits that reference them. Update umbrella pins per coherent milestone.
