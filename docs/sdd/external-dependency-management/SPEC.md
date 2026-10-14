# External dependency management — holonight-pkg-manager

Work package: EDM-018
Baseline: `879bea1f4b71a2ddf185f45fc2d3e153edc46ac5`
Umbrella audit: `38146d8f474317f3f8d72699f60e6d16a71dd165`
Status: Done locally; publication and umbrella pinning remain separate handoffs.

Implementation baseline: `8042ed3890fc3bf226dc7d4c991459cd5c5599a2`
Umbrella Ready checkpoint: `27540047dad600c72672971e02ba99b2626d9b63`

## Requirements

- Configuration and normal build commands shall never acquire dependencies or prepare source providers.
- `task deps` shall explicitly prepare supplied local providers and check installed external prerequisites.
- Internal providers shall resolve only from approved prefixes; external packages shall resolve from an explicit
  external prefix or normal system locations.
- Enabled dependency-dependent features shall require their dependencies; minimal profiles shall disable them explicitly.
- Where production components use Qt, discovery shall require >=6.11; private Qt components shall match
  the selected public version exactly.
- Where tests use GTest, consumers shall require installed GTest >=1.14 CONFIG and any GMock targets they use.
- Existing public APIs, component switches, install-tree consumers and acceptance coverage shall be preserved.

## Design

Adopt canonical `tooling/modules/holonight-pkg-manager.json` and `.cmake`, then synchronize bundle 2.0.0 into this repository.
The copied metadata is owned by this standalone checkout. CMake performs authoritative discovery; shared probes
provide prerequisite diagnostics and record resolved versions/paths. Incompatible caches and local provider install
trees are archived before reuse. Package acquisition occurs only in explicit environment preparation. CI switches
build/runtime/release lanes together to qualified immutable images and runs acceptance offline after preparation.

Remove the GTest download fallback; require GMock. Preserve libalpm/ICU discovery and read-only runtime acceptance.

## Implementation and verification tasks

- [x] Confirm the exact baseline and canonical handoff before implementation.
- [x] Adopt declarations, prefix helpers, explicit preparation and compatibility signatures.
- [x] Apply the repository-specific discovery changes above.
- [x] Update Task, CI preparation/entrypoints, Dockerfiles, release lanes and contributor instructions.
- [x] Run declaration/CMake/profile consistency checks and offline missing-package regressions.
- [x] Run narrow regressions, clean acceptance, installed-package consumers and required quality checks.
- [x] Inspect complete logs; record commands, versions, features, warnings and manual checks.
- [x] Make a repository-local commit only after local verification; publish only when authorized.

## Accepted implementation contract

Prepare Config `0ccd25f323662f50a47bb048be636160f2b7c135`, then Qt
`7c3ce074a8c2e02e4ee8cad8cd19eb8627aa4f68`, Controls/Wayland ON, tests/demo/gallery OFF.
Bundle 2.0.0 and canonical declarations include ICU uc, libalpm, Qt Test/QmlPrivate, GMock, Task,
format/tidy, REUSE, ripgrep, dbus/ldd and coverage-only gcovr. Use full Arch image
`ghcr.io/lebedenko/holonight-deps@sha256:37ca051c39d1688d6413c47d94b8c0f221152dac6981ec67d155f2c43ea17b40`
and REUSE `docker.io/fsfe/reuse@sha256:85462a75c0f8efda09ddd190b92816b70e7662577c8427429e11e1b9f25a992e`.
The added direct helper declarations are already installed in the qualified image; no image rebuild is claimed.

Only task deps prepares supplied local sources. CI preparation explicitly acquires images/exact sources;
acceptance is offline and unprivileged. Preparation hashes sources, provider builds/payloads and evidence;
input/toolchain/feature changes invalidate reuse and preserve incompatible state. Internal providers remain
isolated from external/system discovery. BUILD_TESTS=OFF excludes GTest/Test/QmlPrivate discovery.
Keep C++23, existing presets and coverage instrumentation. Public product APIs and behavior are unchanged.

Preserve every existing CTest registration, read-only ALPM startup, libalpm-free checks, private-bus service
tests and fixture-only file:// sync. Preserve policy fixtures, compiled acceptance with MockPackageSource,
Holonight/Fusion controls and view subsets, generated production metadata and acceptance resource separation.
Build/installed startup exercises default/environment/command-line/external-config selectors. Release/tests-OFF
staging verifies activation paths, payload exclusion and absence of build/preparation discovery paths.
Strict format/tidy, QML lint/types/imports/layering and REUSE gate closure. No package transactions, host
installation/restart, pointer/focus automation, Debian qualification or native desktop review.

## Compatibility cleanup

The initial strict canonical tidy run found 79 diagnostics in 14 existing fixture classes: protected fixture
state was covered by a repository-wide check suppression in the old tests/.clang-tidy. Keep the canonical
configuration unchanged and expose those test-only fields publicly for GTest-generated subclasses. Helpers
and setup remain protected where practical; test assertions, fixture lifetime/order and product behavior stay
unchanged. Const fixture query helpers use nodiscard results; their existing assertions consume those results.
Qt's metadata generator also cannot resolve the std::uint8_t backing spelling of five Q_ENUMs.
Use the equivalent Qt quint8 alias in the four QML model headers, retaining storage types, enum values and
public product behavior. Compatibility commit: `e35e21739845eadd07792f30cd59654a54c8ad9f`; dependency adoption remains separate.

The first negative private-Qt fixture accidentally allowed fallback to system Qt. Restrict the private-discovery
call to the supplied incompatible Qt configuration; all 17 real-CMake regressions then pass. No production
CMake correction was needed for that fixture error.

## Verification

Verified on 2026-10-14 with JOBS=8. [Structured evidence](verification.json) records source hashes,
252 artifact/log hashes, exact provider source content/options, 1,481 prepared file identities, image IDs,
package/toolchain inventories, commands, reviewed diagnostics and retained limitations. Full local logs remain
under ignored build/ci. GCC 16.2.1, Qt 6.11.2, CMake 4.4.3, Ninja 1.13.2 and Clang 22.1.8 were used.
Provider preparations are Debug with Config BUILD_TESTING=OFF and Qt tests/demo/gallery OFF, Wayland ON.

| Check | Result |
|---|---|
| Full Debug | All 481 CTest registrations pass; no skips. Includes private-bus service/process, fixture-only file:// synchronization, libalpm-free checks, layering/import fixtures and both compiled runtime styles. |
| Style subsets | InstalledPackagesViewTest passes 13/13 each for Holonight and Fusion. |
| Startup | All four build-tree and installed selectors pass: default, environment, command-line and external-config. |
| Release/tests OFF | Clean GUI/service build and production staging pass; activation paths, desktop/icon/XML payloads, fixture exclusion and build/preparation path isolation pass. |
| Strict static | Format, existing 105-unit strict tidy target, QML lint/types/import policy, layering and its fixtures pass with canonical configs. |
| Final fixture correction | All targets recompiled; 25 service/process/private-bus cases pass after four const qualifiers and three nodiscard annotations. No assertions or method bodies changed. |
| Dependency regressions | 12 launcher and 17 real-CMake cases pass, including missing/old packages, GMock, exact private Qt, prefixes/isolation/spaces, incompatible caches/toolchains/features and explicit preparation. Coverage configuration and instrumented main.cpp compilation pass without a threshold. |
| Canonical/installer | All 32 canonical tooling/dependency and 68 umbrella installer fixtures pass. Declaration equality, bundle drift, generated profile consistency, Python/JSON/shell/workflow YAML syntax, Task parsing/dry runs (including an external provider path with spaces), and whitespace pass. |
| Licensing | Pinned REUSE lane passes; final documentation-only closure is recorded in verification.json. |

Full build-test/Release/licensing evidence: build/ci/20261014T170246Z-0fqpnz5_, command
`JOBS=8 python3 scripts/ci/run.py`. Its static lane failed on fixture const qualifiers and is not claimed as passing.
Final static and affected tests: build/ci/20261014T181833Z-final-static-fqbofy62. The one-off local wrapper runs the
unchanged static-checks lane followed by the 25 affected CTest cases. Its complete wrapper/command and snapshot
hashes are retained in verification.json. It uses the same qualified image, hash-validated prepared providers,
caller UID/GID, read-only inputs and --pull=never/--network=none contract. All non-documentation source inputs
match this final snapshot. The only source changes after the full runtime run are the const/nodiscard qualifiers
in three fixture files; their cases are rerun against the final compiled source. Release and other runtime results
remain valid. No unaffected acceptance is repeated for documentation closure.

Explicit preparation: `JOBS=8 python3 scripts/ci/run.py --prepare --provider-source-root ..`.
The final preparation run is build/ci/20261014T170223Z-6hulxjdq; compatible providers were hash-validated and
reused after input changes, with earlier preparations preserved for inspection. Canonical remote checks confirmed
the implementation baseline and both providers before work. The retired project-specific image workflow,
Dockerfile and package bootstrap have no active entrypoints; all historical supplements remain in the umbrella audit.

Warning review retains Qt's intentional private-module version-coupling notice and optional Vulkan-header absence.
No compiler, QML metadata or runtime QML diagnostics remain in accepted Debug/Release/static logs. Generated
metadata resolves all five quint8 enums. Expected failures in negative launcher/package fixtures are retained;
their enclosing suites pass. No skipped tests, host installation/restart, package transactions, live desktop or
pointer/focus automation, Debian qualification, publication, hosted CI query or umbrella gitlink update is claimed.
Fresh native desktop/session checks remain umbrella EDM-022. The initiative remains Accepted.
