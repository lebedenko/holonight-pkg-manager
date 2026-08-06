# HoloNight Packages

A standalone C++23/Qt 6 package-management application for the HoloNight desktop.

The repository currently contains the buildable application skeleton and architectural boundaries for the future
package UI, per-user service, package backend adapters, update advisor, persistence, and desktop integration. Package
transactions and privileged helper logic are intentionally not implemented yet.

## Requirements

- Qt 6 (`Core`, `Gui`, `Quick`, `Qml`, `Network`, `Sql`, and `DBus`)
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

The code starts as a modular monolith with dependency direction toward the domain:

| Target | Responsibility |
| --- | --- |
| `holonight_packages_domain` | Package, transaction, source, trust, and action-level concepts |
| `holonight_packages_application` | Use cases and transaction coordination |
| `holonight_packages_backends` | Native package-manager and community-source adapters |
| `holonight_packages_advisor` | Deterministic update assessment and evidence collection |
| `holonight_packages_persistence` | Cache, settings, and transaction history |
| `holonight_packages_platform` | D-Bus, notifications, systemd, and desktop integration |
| `holonight-packages` | Qt Quick user interface and composition root |

Each library is currently an `INTERFACE` target. Convert a target to a `STATIC` library when its first implementation
file is added. The planned `holonight-packaged` user service and privileged `holonight-package-helper` remain separate
processes; their interfaces should be designed before executable stubs are introduced.

See [the high-level project idea](docs/ideas/01-high-level-project-idea.md) for the product and security model.
