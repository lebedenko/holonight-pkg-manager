# HoloNight Packages

A standalone C++23/Qt 6 package-management application for the HoloNight desktop.

The application currently shows a read-only, live list of installed Arch Linux packages (name, version, and
official/foreign source), loaded asynchronously via libalpm. Package transactions, update checking, the per-user
service, the privileged helper, and desktop/D-Bus integration are not implemented yet.

## Requirements

- Qt 6 (`Core`, `Gui`, `Quick`, `Qml`, `Network`, `Sql`, `DBus`, and `Concurrent`)
- libalpm (ships with `pacman` on Arch Linux; no separate `-dev` package needed)
- CMake 3.25+
- Ninja
- [Task](https://taskfile.dev/)
- [`holonight-qt`](../holonight-qt) checked out as a sibling directory
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

Most commands first build and install the sibling `holonight-qt` project into
`/tmp/holonight-qt-prefix`.

## Architecture

The code is a modular monolith with dependency direction toward the domain:

| Target | Responsibility |
| --- | --- |
| `holonight_packages_domain` | `Package` model, `PackageSource` port, source/trust/install-reason concepts |
| `holonight_packages_application` | Use cases (e.g. `PackageListUseCase`) — no Qt/QML types |
| `holonight_packages_backends` | Native package-manager adapters (`AlpmPackageSource`, via libalpm) |
| `holonight_packages_advisor` | Deterministic update assessment and evidence collection |
| `holonight_packages_persistence` | Cache (e.g. `AlpmConnectionCache`), settings, and transaction history |
| `holonight_packages_platform` | D-Bus, notifications, systemd, and desktop integration |
| `holonight-packages` | Qt Quick user interface, QML-facing view-models, and composition root |

`domain`, `application`, `backends`, and `persistence` are `STATIC` libraries. `advisor` and `platform` are still
`INTERFACE` stubs — convert a target to `STATIC` when its first implementation file is added. The planned
`holonight-packaged` user service and privileged `holonight-package-helper` remain separate processes; their
interfaces should be designed before executable stubs are introduced.

See [the high-level project idea](docs/ideas/01-high-level-project-idea.md) for the product and security model.
