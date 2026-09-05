# qmllint/qmlcachegen don't enforce qmldir's `internal` type restriction

**Status:** Confirmed, worked around. Not filed upstream yet.
**Component:** `qmllint`, `qmlcachegen` (vs. the actual `QQmlEngine` type loader)
**Qt version:** 6.11.2 (Arch Linux, `qt6-declarative` package)
**Found:** 2026-09-05, during the `installed-page-ui` SDD cycle, implementing `PackageTableRow.qml` and the
filter-tab delegate in `InstalledFilterTabs.qml` against the sibling `holonight-qt` design system's
`Holonight.Controls` QML module.

## Summary

`../holonight-qt/qml/controls/qmldir` marks one of its own types as `internal`:

```
internal HnSelectableDelegate HnSelectableDelegate.qml
```

Per Qt's qmldir documentation, an `internal` type is only importable/instantiable by QML files that live inside
the *same* module directory — it's how a module hides an implementation-detail base type while still exporting
public subclasses built on top of it (here: `HnListDelegate`, `HnNavigationDelegate`, `HnCardDelegate`, and
`HnPanelHeader` all subclass `HnSelectableDelegate` internally, and *those* are the public, importable names).

An app-local QML file that writes `HnSelectableDelegate { ... }` directly as its own root/base type — i.e.
violates the `internal` restriction from outside the module — was found to:

- **pass `task qml-lint`** (`qmllint`'s static analysis does not flag the `internal` violation)
- **pass `task build`** (`qmlcachegen`'s AOT compile step does not fully verify base-type import legality either
  — it appears to defer full class-hierarchy resolution to runtime)
- **fail only at actual QML-engine instantiation**, i.e. when something actually creates a `QQmlComponent` from
  the file and calls `create()`/`beginCreate()`. In our case this first surfaced in a GTest
  (`installed_packages_view_test.cpp`) that loads the view via a real `QQmlEngine`, with errors like:

  ```
  file:///.../PackageTableRow.qml: HnSelectableDelegate is not a type
  file:///.../InstalledPackagesView.qml: Type InstalledFilterTabs unavailable
  file:///.../InstalledFilterTabs.qml: HnSelectableDelegate is not a type
  ```

## Reproduction shape

```qml
// App-local file, NOT inside Holonight.Controls' own directory
import Holonight.Controls

HnSelectableDelegate {   // <-- marked `internal` in Holonight.Controls' qmldir
    id: root
    // ... fully valid QML, builds and lints clean ...
}
```

- `qmllint` on this file: no warnings.
- `qmlcachegen` AOT-compiling this file (as part of `task build`): succeeds, produces a working `.o`.
- `QQmlComponent(&engine, url).create()` on this file at actual runtime: fails, type unresolved.

## Workaround

Don't extend the `internal` type directly. Use one of the module's public subclasses instead — in our case
`HnListDelegate` (whose own root element *is* `HnSelectableDelegate`, so it inherits every property/enum we
needed: `selected`, `highlighted`, `checked`, `hovered`, `selectionStyle`, the `Fill`/`AccentEdge`/`Outline`
enum). We fully override `contentItem` on `HnListDelegate` anyway, so none of its built-in
title/subtitle/metadata layout is used — only the inherited selection behavior. Enum references also need to be
qualified via the public subclass's name (`HnListDelegate.Fill`), not the internal type's name
(`HnSelectableDelegate.Fill`) — qualification via the internal name also fails to resolve from outside the
module.

## Why this matters for this project

This means **`task build` and `task qml-lint` passing on new/changed QML is necessary but not sufficient** when
that QML consumes the `holonight-qt` design system. Actual instantiation through a real `QQmlEngine` — i.e.
`task test`, since `installed_packages_view_test.cpp` and similar tests load views via `QQmlComponent` — is the
only check in this project's pipeline that catches an `internal`-type violation. Any future app-local QML file
that directly extends a base primitive from `Holonight.Controls` should be double-checked against that module's
`qmldir` for an `internal` marker before assuming "it built and linted clean" means it will actually run.

See also: `docs/known-issues/qmlcachegen-enum-binding-crash.md` — a related "passes lint/build, fails only at
runtime/AOT" Qt tooling gap found in the same cycle.
