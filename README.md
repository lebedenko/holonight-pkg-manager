# HoloNight Packages

A standalone C++23/Qt 6 package-management application for the HoloNight desktop.

The application currently shows a read-only, filterable Installed page for Arch Linux packages, loaded
asynchronously via libalpm: a data table (package, origin, installed version, size, install reason) with
per-category tabs (Explicit / Dependencies / AUR-Foreign / Orphans), search, sort, a repository filter, and a
detail panel with metadata, dependency, and orphan-reclaim information. An Updates page lists pending
official-repository updates by comparing installed packages with the sync databases already on disk; its Reload
button only re-reads them, so databases must be synced with your package manager outside the application. It is
transactionally inert — no install, remove, update, or database-sync action is implemented. Package transactions,
the per-user service, the privileged helper, and desktop/D-Bus integration are not implemented yet.

## Requirements

- Qt 6 (`Core`, `Gui`, `Quick`, `Qml`, `Network`, `Sql`, `DBus`, `Concurrent`, and `QuickControls2`)
- libalpm (ships with `pacman` on Arch Linux; no separate `-dev` package needed)
- CMake 3.25+
- Ninja
- [Task](https://taskfile.dev/)
- [`holonight-qt`](../holonight-qt) and [`holonight-config`](../holonight-config) checked out at the revisions in Taskfile.yml
- Python 3 and ripgrep for acceptance/policy checks
- GTest (optional; fetched automatically when tests are enabled and it is not installed)

## Development

```bash
task configure
task build
task run
task test
task format-check
task tidy
task qml-lint
```

Most commands validate the pinned sibling revisions, build configuration and provider in
`build/dependencies`, and stage them into `build/dependencies/prefix`. Provider examples/tests are disabled;
Wayland support remains enabled. Set `BUILD_DIR`, `HOLONIGHT_QT_SOURCE`, `HOLONIGHT_CONFIG_SOURCE`, or `NPROC`
as Task variables when needed. `task run` supplies the staged native configuration-library path; CTest and
qmllint derive it from the configured dependency target. Configure `-DTIDY_JOBS=2` (default 4) to limit analysis workers.

Application standard controls use `import QtQuick.Controls as Controls`; Core and composites retain their
explicit HoloNight appearance. The executable embeds a Holonight default. Set `QT_QUICK_CONTROLS_STYLE=Fusion`,
pass `-style Fusion`, or point `QT_QUICK_CONTROLS_CONF` at an external `[Controls]` configuration to override it.
The exact build executable discovers its configured dependencies; installed copies discover QML relative to
`../lib/qt6/qml` (using the configured install libdir).

The [UQC-105 acceptance](docs/sdd/unified-qtquick-controls/SPEC.md) covers both styles, preserved scroll geometry,
independent policy fixtures, and eight isolated executable launches. Useful focused commands after building:

```bash
ctest --test-dir build -R 'runtime_controls|runtime_qml_import' --output-on-failure
QT_QUICK_CONTROLS_STYLE=Fusion ctest --test-dir build -R InstalledPackagesViewTest --output-on-failure
bash scripts/check-qmltypes.sh build
python3 scripts/check-runtime-launches.py build/holonight-packages build/dependencies/prefix --logs build/uqc105/build-launch
```

Launch acceptance isolates HOME/XDG and desktop activation, observes only existing read-only ALPM enumeration,
and terminates/reaps each process. Tests use MockPackageSource/FakeUpdateSource and real models; they never perform package
transactions, synchronize repositories or invoke external links.

## Architecture

The code is a modular monolith with dependency direction toward the domain:

| Target | Responsibility |
| --- | --- |
| `holonight_packages_domain` | `Package`/`PendingUpdate` models, `PackageSource`/`UpdateSource` ports, source/trust/install-reason concepts |
| `holonight_packages_application` | Use cases and pure helpers (e.g. `PackageListUseCase`, `summarizeUpdates`) — no Qt/QML types |
| `holonight_packages_backends` | Native package-manager adapters (`AlpmPackageSource`, `AlpmUpdateSource`, via libalpm) |
| `holonight_packages_advisor` | Deterministic update assessment and evidence collection |
| `holonight_packages_persistence` | Cache (e.g. `AlpmConnectionCache`), settings, and transaction history |
| `holonight_packages_platform` | D-Bus, notifications, systemd, and desktop integration |
| `holonight-packages` | Qt Quick user interface, QML-facing view-models, and composition root |

`domain`, `application`, `backends`, and `persistence` are `STATIC` libraries. `advisor` and `platform` are still
`INTERFACE` stubs — convert a target to `STATIC` when its first implementation file is added. The planned
`holonight-packaged` user service and privileged `holonight-package-helper` remain separate processes; their
interfaces should be designed before executable stubs are introduced.

See [the high-level project idea](docs/ideas/01-high-level-project-idea.md) for the product and security model.
