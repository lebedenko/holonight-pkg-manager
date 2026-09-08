# UQC-105 — runtime-selected Quick Controls

Status: Design accepted for implementation after the published umbrella checkpoint.

## Scope and baselines

Application baseline: `518bb60232086e9537fb402f91b4b703d260ffcf`.
Provider: `478ef7c40a22c7c3f7ea6f45d9205411b5504834`.
Configuration: `fe69a59e6b73167fd5349223a4d265d75386c139`.
[Design](DESIGN.md), [tasks](TASKS.md), and [implementation record](IMPLEMENTATION.md).

- Application standard controls import `QtQuick.Controls as Controls`; qualify instances, enums and attached properties.
- Preserve `Holonight.Core`, `Holonight.Controls`, semantic painting and all existing placeholder behavior.
- Embed root `:/qtquickcontrols2.conf` selecting Holonight. Respect environment, command-line and external configuration overrides.
- Keep QQuickView composition and real model registration; discover configured dependencies only for the exact build executable and installed dependencies relative to the executable.
- Preserve loading/error/empty/populated states, search, category/repository filtering, sorting, selection reconciliation, refreshed details, dependency expansion and orphan summaries.
- Preserve 1360x890 default and 720x480 minimum sizes, toolbar breakpoints 600/1000 and page breakpoint 1092 after sidebar/margins.
- Preserve horizontal table scrolling with aligned headers/rows and independent list/detail/page vertical scrolling.
- Own dependency builds/staging beneath the configured application build directory; pin both CI jobs and Taskfile, disable provider tests/examples, retain Wayland.
- Derive QML test/lint discovery from configured dependencies; make tidy concurrency configurable and generate artifacts before CI analysis.

## Acceptance

Fresh Holonight/Fusion processes exercise shared production QML/assets with MockPackageSource, real presentation/filter models, separate generated QML modules, and no native backend.
Verify actual control implementation URLs and loaded plugin paths, Core/composite preservation, zero unexpected QML diagnostics, ComboBox signal/property selection and overflow, and sufficient rows/long metadata to prove all scrolling directions and endpoint reachability.
Run every existing QML test under both styles; independently failing import-policy fixtures reject direct styles, wrong/missing aliases, unqualified instances/enums/attached properties and missing canonical imports.
Launch actual build and staged-install executables in embedded default, environment Fusion, command-line Fusion over environment Holonight, and external Fusion configuration modes.
Isolate HOME/XDG and desktop activation, allow only existing read-only ALPM enumeration, bound startup observation, fail early exits/crashes/missing origins/unexpected diagnostics/build-path leakage, and terminate/reap each process.
Formatting, tidy, QML lint/types, policies, full CTest, staged installation, script/workflow syntax, local documentation links, whitespace and remote build/static/licensing CI must pass before handoff.

## Exclusions

No package operation, synchronization, external-link or network action; no system installation, public configuration/schema change, production diagnostic/database-path API or other consumer implementation. Preserve the two untracked mockups. Human-operated Hyprland/Sway and ecosystem integration remain UQC-201.
