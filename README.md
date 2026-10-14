# HoloNight Packages

A standalone C++23/Qt 6 package-management application for the HoloNight desktop.

The application currently shows a read-only, filterable Installed page for Arch Linux packages, loaded
asynchronously via libalpm: a data table (package, origin, installed version, size, install reason) with
per-category tabs (Explicit / Dependencies / AUR-Foreign / Orphans), search, sort, a repository filter, and a
detail panel with metadata, dependency, and orphan-reclaim information. An Updates page lists pending
official-repository updates by comparing installed packages with the sync databases already on disk, and an Explore
page searches every package in configured sync repositories, including third-party repositories (with an installed badge and a read-only details panel).
Both pages' Reload buttons only re-read the databases, so they must be synced with your package manager outside the
application. It is
transactionally inert — no install, remove, or update action is implemented. The Updates page has an opt-in **Check now** control, and `holonight-packaged` checks online on a schedule: it refreshes a private copy of the sync databases under `$XDG_CACHE_HOME/holonight-packages/checkdb`, never the system databases. Package transactions,
the per-user service, the privileged helper, and desktop/D-Bus integration are not implemented yet.

## Requirements

- Qt >=6.11 (`Core`, `Gui`, `Quick`, `Qml`, `Network`, `Sql`, `DBus`, `Concurrent`, and `QuickControls2`)
- libalpm (ships with `pacman` on Arch Linux; no separate `-dev` package needed)
- ICU (`uc`, for Unicode case-insensitive Explore search)
- CMake 3.25+
- Ninja
- [Task](https://taskfile.dev/)
- [`holonight-qt`](../holonight-qt) and [`holonight-config`](../holonight-config) checked out at the revisions in Taskfile.yml
- Python 3 and ripgrep for acceptance/policy checks
- Installed GTest >=1.14 CONFIG with GMock and Qt Test/QmlPrivate (only when tests are enabled); private Qt must match public Qt exactly
- clang-format, clang-tidy, REUSE and dbus/ldd test helpers; gcovr only for coverage

## Development

```bash
task deps
task configure
task build
task run
task test
task format-check
task tidy
task qml-lint
```

Only `task deps` prepares supplied local Config and Qt sources under `build/deps`, installing into
`build/deps/prefix`; it never downloads source. Provider examples/tests are disabled and Controls/Wayland
remain enabled. Normal configure/build/test/static commands validate installed prerequisites and prepared
providers without acquisition or preparation. Use environment variables `HOLONIGHT_CONFIG_SOURCE` and
`HOLONIGHT_QT_SOURCE` for local source overrides, `HOLONIGHT_DEPENDENCY_PREFIX` for existing internal
providers, and `HOLONIGHT_EXTERNAL_PREFIX` for installed external packages. Paths with spaces are supported;
external/system prefixes cannot satisfy internal provider requirements. Incompatible caches are archived.
`JOBS` controls preparation/build parallelism. `task run` supplies the prepared configuration-library path;
CTest and qmllint derive it from configured targets. `TIDY_JOBS` controls CMake's analysis workers.

Application standard controls use `import QtQuick.Controls as Controls`; Core and composites retain their
explicit HoloNight appearance. The executable embeds a Holonight default. Set `QT_QUICK_CONTROLS_STYLE=Fusion`,
pass `-style Fusion`, or point `QT_QUICK_CONTROLS_CONF` at an external `[Controls]` configuration to override it.
The exact build executable discovers its configured dependencies; installed copies discover QML relative to
`../lib/qt6/qml` (using the configured install libdir).

The [UQC-105 acceptance](docs/sdd/unified-qtquick-controls/SPEC.md) covers both styles, preserved scroll geometry,
independent policy fixtures, and eight isolated executable launches. Useful focused commands after building:

```bash
ctest --test-dir build/test -R 'runtime_controls|runtime_qml_import' --output-on-failure
QT_QUICK_CONTROLS_STYLE=Fusion ctest --test-dir build/test -R InstalledPackagesViewTest --output-on-failure
bash scripts/check-qmltypes.sh build
python3 scripts/check-runtime-launches.py build/debug/holonight-packages build/deps/prefix --logs build/uqc105/build-launch
```

Launch acceptance isolates HOME/XDG and desktop activation, observes only existing read-only ALPM enumeration,
and terminates/reaps each process. Tests use MockPackageSource/FakeUpdateSource/FakeExploreSource and real models; they never perform package
transactions, synchronize repositories or invoke external links.

## Architecture

The code is a modular monolith with dependency direction toward the domain:

| Target | Responsibility |
| --- | --- |
| `holonight_packages_domain` | `Package`/`PendingUpdate`/`SyncPackage` models, `PackageSource`/`UpdateSource`/`ExploreSource` ports, source/trust/install-reason concepts |
| `holonight_packages_application` | Use cases and pure helpers (e.g. `PackageListUseCase`, `summarizeUpdates`, `ExploreIndex`/`searchPackages`) — no Qt/QML types |
| `holonight_packages_backends` | Native package-manager adapters (`AlpmPackageSource`, `AlpmUpdateSource`, `AlpmExploreSource`, via libalpm) |
| `holonight_packages_snapshot_store` | libalpm-free JSON store for the last online update snapshot (`update-snapshot.json` under the XDG cache) |
| `holonight_packages_advisor` | Deterministic update assessment and evidence collection |
| `holonight_packages_persistence` | Cache (e.g. `AlpmConnectionCache`), settings, and transaction history |
| `holonight_packages_platform` | D-Bus, notifications, systemd, and desktop integration |
| `holonight-packages` | Qt Quick user interface, QML-facing view-models, and composition root |

`domain`, `application`, `backends`, and `persistence` are `STATIC` libraries. `advisor` and `platform` are still
`INTERFACE` stubs — convert a target to `STATIC` when its first implementation file is added. The planned
`holonight-packaged` user service and privileged `holonight-package-helper` remain separate processes; their
interfaces should be designed before executable stubs are introduced.

See [the high-level project idea](docs/ideas/01-high-level-project-idea.md) for the product and security model.

## Standalone developer tooling

See [tooling/README.md](tooling/README.md) for presets, local dependency overrides, editor refresh,
`task tooling:doctor`, and the independent Serena project.

## Local CI rehearsal

Run `task ci:prepare` explicitly with Python 3, Git and Docker (or Podman). It acquires the pinned
qualified Arch and REUSE images and exact provider sources, then prepares Config/Qt offline. To clone the
accepted commits from local sibling repositories instead of GitHub, run
`task ci:prepare -- --provider-source-root ..`. Source contents, installed artifacts, toolchain, features,
images and preparation inputs are hash-validated; stale/tampered preparation is rejected and archived on
explicit re-preparation. Only compatible provider state is reused.

Then run `JOBS=8 task ci` for clean build/test, Release/tests-OFF staging, strict static checks and licensing.
Acceptance runs with `--pull=never --network=none`, caller UID/GID, read-only source/prepared inputs, and
private HOME/XDG/runtime directories. Consumers see only installed providers and provenance. Existing
development builds are preserved. Licensing alone needs only its explicit pinned image pull, not providers.
Logs, complete test output, source hashes, image/tool identities, lane results and runtime evidence are
saved under ignored `build/ci/`. Add reported untracked inputs before committing. Publication, host installation
and application/package transactions are outside these commands. The old project-specific CI image and
package bootstrap have been retired; their historical package records remain in the umbrella audit.

## Online update check

Run the service directly from the build tree without installing it:

```sh
task run:packaged
task run:packaged -- --check-interval-minutes 30
```

This builds the selected preset (`debug` by default) and runs the service in the foreground on your current session
D-Bus. Run the GUI in another terminal with `task run`; stop the service with Ctrl+C. Use `PRESET=test` or
`PRESET=release` to select another build, and pass service arguments after `--`.

Only `holonight-packaged` checks. It runs the first check 60 s after start and then every 6 h (±10 min jitter), with 5 min,
15 min and 1 h backoff after failures. Set the interval in `$XDG_CONFIG_HOME/holonight/packages.toml`
(`HOLONIGHT_PACKAGES_FILE` overrides the path) or with `holonight-packaged --check-interval-minutes N`:

```toml
[updates]
check_interval_minutes = 360   # minimum 15; a missing key uses the default
```

The GUI's **Check now** calls `org.holonight.Packages1.UpdateCheck.CheckNow()` (bus-activating the service) and reads the
resulting list from the snapshot file. Tests may sync only fixture repositories under a temp dir via `file://`; run
`task layering-check` for the policy checks.

## Installed application

The application menu entry **HoloNight Packages** launches `holonight-packages` directly.
CMake installs `org.holonight.Packages.desktop` and the `holonight-pkg-manager` SVG into the hicolor fallback theme.
