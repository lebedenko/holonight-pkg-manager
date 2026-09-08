# UQC-105 implementation record

[Requirements](SPEC.md), [design](DESIGN.md), [tasks](TASKS.md).

## Baselines and publication order

Application baseline `518bb60232086e9537fb402f91b4b703d260ffcf`; local design checkpoint
`7e8dc02000779508e5cfc4c26fc54e324732f31c`; published umbrella implementation assignment `1d99e3f`.
Provider `478ef7c40a22c7c3f7ea6f45d9205411b5504834`; configuration
`fe69a59e6b73167fd5349223a4d265d75386c139`. Provider and AI remote gates passed before this assignment.

## Changes

Ten application QML files now use namespaced runtime Controls instead of direct Basic/Holonight imports.
Core/composites, semantic painting, placeholder actions, models and all layout/scroll ownership remain intact.
The executable embeds its overridable Holonight default, restricts configured dependency discovery to the exact
build executable, and discovers installed QML relative to its own directory. No public API/schema changes.

Taskfile validates exact sibling revisions and stages Release dependencies below the application build directory.
Both CI jobs pin those revisions, disable provider tests/examples, preserve Wayland and build generated artifacts
before static analysis. Test/lint discovery derives from configured HolonightQt; tidy workers are configurable.
README and local ignored AGENTS.md describe the new contract. The policy rejects runtime namespace violations,
competing styles, internal composite references and missing Core/composite imports using independent fixtures.

A separate acceptance executable shares production QML/assets and real presentation/filter models, with
MockPackageSource at the enumeration boundary. Its generated module lives in `tests/runtime/HolonightPackages`,
separate from source-based tests. Thirteen existing view tests run against both source and compiled QML;
five additional checks cover implementation/plugin origins, filters/sort/selection/orphans, popup overflow,
responsive thresholds and independently reaching table/list/detail/page scroll endpoints. All reused view tests
reject unexpected QML warnings. Acceptance adds no production diagnostic or database-path API.

## Acceptance results

Local verification on 2026-09-08 with Qt 6.11.2:

- Full CTest: 98/98 pass. Existing view suite: 13/13 under Holonight and 13/13 under Fusion.
- Separate compiled-QML processes: 18 tests each, including loading/error/empty/populated states, details refresh,
  dependency expansion, selection reconciliation, filter/category/repository changes and orphan summaries.
- Actual Button, CheckBox, ComboBox, TextField, ScrollView and ScrollBar implementation URLs match the selected
  style. Core HnLabel and composite HnSearchField retain their provider origins; loaded Core/Controls/style plugins
  come from the configured prefix. Palette-owned application painting is preserved.
- Default 1360x890 and minimum 720x480; both sides of toolbar thresholds 600/1000 and page threshold 1092 pass.
  Eighty long-metadata rows prove table horizontal scrolling and independent list/detail/page vertical scrolling,
  header/row alignment, last-row and footer reachability. Forty repositories force popup overflow and retained selection.
- All four actual build and four staged-install launch modes pass: embedded default, environment Fusion,
  command-line Fusion over environment Holonight, and external Fusion configuration. Traces and process maps prove
  implementation/plugin loading; installed probes reject build-path discovery. Successful launches are terminated/reaped.
- Format, full clang-tidy, QML lint/types, policy fixtures, Python/shell/workflow/Taskfile syntax, local documentation
  links and whitespace pass. Tidy findings in new and reused test code were corrected without production behavior changes.

Runtime and source-QML acceptance also passes with the host `/usr/lib/qt6/qml/Holonight` directory hidden in a
private filesystem namespace. Actual launches use sanitized HOME/XDG, empty executable PATH and an unavailable
D-Bus endpoint. The only production backend action is existing read-only ALPM enumeration. No package transaction,
synchronization, external link, network action, credential interaction, system installation or desktop activation
was performed. Offscreen internal keyboard/property/signal tests do not automate desktop pointer or window focus.

## Reproduction

From this repository (omit the bwrap wrapper when host-module masking is unnecessary):

```sh
task configure-tests NPROC=6
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$PWD/build/uqc105/stage" -DTIDY_JOBS=4
cmake --build build -j 6
bwrap --bind / / --tmpfs /usr/lib/qt6/qml/Holonight --dev /dev --proc /proc ctest --test-dir build -R 'runtime_controls|runtime_qml_import' --output-on-failure -j 2
bwrap --bind / / --tmpfs /usr/lib/qt6/qml/Holonight --dev /dev --proc /proc ctest --test-dir build --output-on-failure -j 4
QT_QUICK_CONTROLS_STYLE=Holonight ctest --test-dir build -R InstalledPackagesViewTest --output-on-failure -j 4
QT_QUICK_CONTROLS_STYLE=Fusion ctest --test-dir build -R InstalledPackagesViewTest --output-on-failure -j 4
ctest --test-dir build -R runtime_controls -V
cmake --build build --target format-check qml-lint tidy
bash scripts/check-qmltypes.sh build
python3 scripts/check-runtime-launches.py build/holonight-packages build/dependencies/prefix --logs build/uqc105/build-launch
cmake --install build/dependencies/config --prefix "$PWD/build/uqc105/stage"
cmake --install build/dependencies/qt --prefix "$PWD/build/uqc105/stage"
cmake --install build
python3 scripts/check-runtime-launches.py build/uqc105/stage/bin/holonight-packages build/uqc105/stage --forbid-path build --logs build/uqc105/install-launch
git diff --check
```

Local transient logs are `/tmp/uqc105-*.log`; launch traces/maps are below `build/uqc105`.
The two untracked mockups are preserved and their SHA-256 hashes were checked unchanged.
Publication and remote CI acceptance must be confirmed in the umbrella handoff before UQC-105 becomes Done.
Human-operated Hyprland/Sway and final ecosystem integration remain UQC-201.

## Native dependency discovery follow-up

CI `34272476545` compiled successfully, but QML tests could not load privately staged `libholonight_config.so`.
Host QML masking alone had not hidden the system native configuration library. CTest and qmllint now derive
LD_LIBRARY_PATH from the configured HoloNight::Config target; `task run` supplies its private staging library path.
Actual build/install acceptance already supplied the explicit staged native loader path. No consumer linkage or
public API workaround was added. Verification repeats with both the host QML module and native library hidden.

The stronger masking also exposed an old local HoloNightConfig_DIR cache pointing at `/usr` despite the new
prefix. Taskfile now passes the staged configuration package directory explicitly to provider/application
configuration, preventing stale cache selection from bypassing the pinned dependency.

After this correction, all 98 CTest entries and qmllint pass with both host providers hidden (13.89 seconds).
The native mask adds `--ro-bind /dev/null /usr/lib/libholonight_config.so` to the bwrap commands above;
only private mount namespaces are affected. The cache now resolves configuration below `build/dependencies/prefix`.

## CI font dependency follow-up

CI `34273801509` passed static checks and loaded the staged native dependencies, but four CTest entries
failed existing text geometry assertions. The container had no fonts. Running the two affected source tests
with an empty Fontconfig configuration reproduced both failures locally. CI jobs and the image recipe now
install `noto-fonts`, matching the local sans-serif font. Geometry assertions and production layout are unchanged.

## Accepted publication — 2026-09-09

Implementation and CI corrections are published at `50ee371f3807572806f466f23e7ef40080a8599b`; canonical
`origin/main` availability was confirmed before handoff. [CI 34275422302](https://github.com/lebedenko/holonight-pkg-manager/actions/runs/34275422302)
passes build/test, both source-QML styles, eight actual build/install launches, QML lint/types, formatting and tidy.
[Licensing 34275422316](https://github.com/lebedenko/holonight-pkg-manager/actions/runs/34275422316)
and CI image publication `34275422324` also pass.

Final local verification uses Noto-only Fontconfig discovery with both system HoloNight QML and the native
configuration library hidden: all 98 CTest entries pass (26.64 seconds). The focused four geometry/runtime entries
pass first. Earlier dual-style source suites, isolated eight-launch acceptance and full static checks remain valid;
this follow-up changes only CI dependencies and documentation. Documentation links, Python/shell syntax and
whitespace pass. Both mockup hashes are unchanged. No package transaction or live desktop interaction occurred.

This documentation handoff completes the repository work; the umbrella owns the final published gitlink and
UQC-105 status. Greeter implementation and human-operated Hyprland/Sway integration are subsequent assignments.
