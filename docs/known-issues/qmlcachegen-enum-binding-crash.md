# qmlcachegen segfaults on declarative enum-literal binding to a same-typed Q_PROPERTY

**Status:** Confirmed, worked around. Not filed upstream yet.
**Component:** `qmlcachegen` (Qt QML AOT compiler)
**Qt version:** 6.11.2 (Arch Linux, `qt6-declarative` package)
**Found:** 2026-09-05, during the `installed-page-ui` SDD cycle, implementing `InstalledPackagesFilterModel`.

## Summary

`qmlcachegen` crashes (SIGSEGV, exit code 139) when a QML file declaratively binds a scoped-enum literal to a
`Q_PROPERTY` of that *exact* enum type, on an instance of the C++ type that declares the enum. `qmllint` reports
no issue on the same file — this is purely an AOT-compiler crash, not a QML correctness problem.

## Minimal repro

Given a `QML_ELEMENT` C++ type:

```cpp
class InstalledPackagesFilterModel : public QSortFilterProxyModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(TabFilter tabFilter READ tabFilter WRITE setTabFilter NOTIFY tabFilterChanged)

 public:
  enum class TabFilter : std::uint8_t { Explicit, Dependencies, Foreign, Orphans };
  Q_ENUM(TabFilter)
  ...
};
```

This QML file crashes `qmlcachegen`:

```qml
import HolonightPackages

InstalledPackagesFilterModel {
    tabFilter: InstalledPackagesFilterModel.Explicit
}
```

Reproduced directly via the generated Ninja build command:

```
/usr/lib/qt6/qmlcachegen --bare --resource-path /HolonightPackages/packages/ZZTest3.qml \
  -I <build>/apps/packages -I /usr/lib/qt6/qml -i <build>/apps/packages/HolonightPackages/qmldir \
  --resource <...>.qrc [...] -o /tmp/out.cpp ZZTest3.qml
# exit code: 139
```

## What does and doesn't crash

| Pattern | Result |
|---|---|
| `tabFilter: InstalledPackagesFilterModel.Explicit` (declarative binding) | **Crashes** |
| `tabFilter: InstalledPackagesFilterModel.TabFilter.Explicit` (fully qualified) | **Crashes** — qualification style doesn't matter |
| `property int x: InstalledPackagesModel.Loading` (assign enum literal to unrelated `int` property) | OK |
| `property bool ok: fm.tabFilter === InstalledPackagesFilterModel.Explicit` (comparison) | OK |
| `property var opts: [ { field: InstalledPackagesFilterModel.Explicit } ]` (stored in JS object literal) | OK |
| `Component.onCompleted: fm.tabFilter = InstalledPackagesFilterModel.Explicit` (imperative assignment in a handler) | OK |
| `Component.onCompleted: fm.tabFilter = opts[0].tab` (imperative assignment of a value read back out of a JS structure) | OK |

So the crash is specific to the **static declarative binding form** assigning an enum-class literal to a
property of the identical enum type on an instance of the type declaring it. Every other access pattern
(comparison, storage, or imperative assignment of the same literal) works fine.

## Workaround

Don't write the declarative binding. Either:

1. Rely on the C++ default member initializer if the desired value is already the default (what we did —
   `TabFilter tab_filter_ = TabFilter::Explicit;` already covered our REQ-F-106 default-tab requirement, so the
   binding was simply redundant and could be deleted), or
2. Set the value imperatively inside a `Component.onCompleted:` handler (or any other JS handler) instead of a
   declarative property assignment.

## Why this matters for this project

Neither `task qml-lint` (`qmllint`) nor `task format-check` catches this — the crash only manifests during
`task build`/`task test`'s actual `qmlcachegen` invocation, as a hard Ninja build failure (`exit code 267` /
`139` bubbling up through the `.rcc/qmlcache/*.cpp` target). If a future change reintroduces a declarative
`SomeType.EnumValue` binding of this shape, expect a mysterious Ninja build crash, not a lint warning.

See also: `docs/known-issues/qmllint-does-not-enforce-internal-types.md` — a related "passes lint/build, fails
only at runtime" gap found in the same cycle.
