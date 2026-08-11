# Installed Packages List — Design

**Feature**: Read-only, in-process enumeration and display of currently-installed Arch Linux packages through the
domain → backends → application → UI layer stack.

**Status**: Design (Stage 2 of SDD cycle)

**Date**: 2026-08-11

**Input**: `docs/sdd/installed-packages-list/SPEC.md` (EARS requirements, referenced throughout as `REQ-*`)

---

## 0. Naming note on SPEC.md path references

SPEC.md's acceptance criteria reference `src/domain/holonight_packages_domain/package.hpp`. The repository's actual
convention (see `src/domain/include/holonight_packages_domain/holonight_packages_domain.h`) is
`src/<module>/include/holonight_packages_<module>/*.h` — a `.h` extension, under an `include/` prefix. This design
follows the repo's existing convention, not SPEC.md's illustrative path/extension. All concrete paths below are the
real paths to be created.

---

## 1. Components

New files by module. Every listed `.h` lives under `include/holonight_packages_<module>/`; every `.cpp` lives under
a new `src/` subdirectory (none of the three modules has one yet, since they've only ever held stub headers).

### `src/domain/` (→ `STATIC`, REQ-F-008/REQ-C-002)

| File | Contents |
|---|---|
| `include/holonight_packages_domain/package.h` | `Package` struct, `SourceType` enum, `InstallReason` enum (REQ-F-001) |
| `include/holonight_packages_domain/package_source.h` | `PackageSourceErrorCode`, `PackageSourceError`, abstract `PackageSource` interface (the "port" both `application` and `backends` depend on — see §6) |
| `src/package_source.cpp` | Out-of-line definition of `PackageSource::~PackageSource()` |
| `include/holonight_packages_domain/holonight_packages_domain.h` | Kept as a thin umbrella header (`#include`s the two headers above) so the existing top-level include path still resolves |

`Package` and `PackageSourceError` use `std::string`/`std::vector`/`std::expected`, never `QString`/`QVariant` —
domain stays framework-agnostic and constructible/testable without a `QCoreApplication` (see §6).

### `src/backends/` (→ `STATIC`)

| File | Contents |
|---|---|
| `include/holonight_packages_backends/alpm_package_source.h` | `AlpmPackageSource : public holonight_packages_domain::PackageSource` — declares the constructor and `enumerateInstalledPackages()` override only; never includes `<alpm.h>` |
| `src/alpm_package_source.cpp` | libalpm calls: `alpm_initialize`, sync-db discovery/registration, `alpm_db_get_pkgcache`, field mapping, `alpm_release`; the only translation unit in the codebase that includes `<alpm.h>` |
| `CMakeLists.txt` | Adds `find_package(PkgConfig REQUIRED)` + `pkg_check_modules(Alpm REQUIRED IMPORTED_TARGET libalpm)`; links `PkgConfig::Alpm` `PRIVATE` |

### `src/application/` (→ `STATIC`)

| File | Contents |
|---|---|
| `include/holonight_packages_application/package_list_use_case.h` | `PackageListUseCase` (REQ-F-003) |
| `src/package_list_use_case.cpp` | Constructor + `getInstalledPackages()` (delegates to the injected `PackageSource`, sorts ascending by `name`) |

### `apps/packages/`

| File | Contents |
|---|---|
| `app/InstalledPackagesModel.h` / `.cpp` | `QAbstractListModel` + async orchestration + loading/loaded/error state exposed to QML (§3, §4) |
| `app/PackagesApplication.h` / `.cpp` (modified) | Constructs the production `AlpmPackageSource("/", "/var/lib/pacman")` → `PackageListUseCase` → `InstalledPackagesModel` chain; injects the model with `QQuickView::setInitialProperties()` before `setSource()` |
| `CMakeLists.txt` (modified) | Adds the two new sources; links `Qt6::Concurrent` (new component, see §9) |

### `qml/`

| File | Contents |
|---|---|
| `qml/packages/InstalledPackagesView.qml` | `ListView` + loading, empty, and error states bound to a required typed model property (REQ-F-004/005/006/007) |
| `qml/packages/PackageRowDelegate.qml` | One row: name, version, "official"/"foreign" label |
| `qml/workspace/WorkspaceWindow.qml` (modified) | Replaces the placeholder `Text` with `InstalledPackagesView` |

### `tests/` (organized by module per REQ-C-004)

| File | Contents |
|---|---|
| `tests/domain/package_test.cpp` | Field construction/readback (REQ-F-001), `operator==` |
| `tests/application/mock_package_source.h` | GMock double implementing `holonight_packages_domain::PackageSource` |
| `tests/application/package_list_use_case_test.cpp` | Sorting (REQ-F-003), error pass-through |
| `tests/backends/alpm_package_source_test.cpp` | Fixture-DB enumeration + field mapping (REQ-F-002), invalid-root error (REQ-F-006), empty-db (REQ-F-007) |
| `tests/apps/installed_packages_model_test.cpp` | Status transitions Loading→Loaded/Error, `rowCount`/`data`/`roleNames`, non-blocking behavior (REQ-F-005) |
| `tests/fixtures/pacman/populated/…` | Checked-in fixture DB (§5) |
| `tests/fixtures/pacman/empty/…` | Checked-in empty fixture DB (§5) |
| `tests/CMakeLists.txt` (modified) | New sources; links `Qt6::Concurrent`, `Qt6::Test`; defines `HOLONIGHT_TEST_FIXTURES_DIR` |

---

## 2. Data flow

```mermaid
sequenceDiagram
    participant QML as InstalledPackagesView.qml
    participant Model as InstalledPackagesModel (GUI thread)
    participant Pool as QtConcurrent thread pool
    participant UC as PackageListUseCase
    participant BE as AlpmPackageSource
    participant Alpm as libalpm (C API)

    Note over Model: status = Loading (constructor)
    QML->>Model: bind ListView.model, status, errorMessage
    Model->>Pool: QtConcurrent::run([uc]{ return uc->getInstalledPackages(); })
    Note over QML: spinner visible, list/error hidden — GUI thread never blocks
    Pool->>UC: getInstalledPackages()
    UC->>BE: enumerateInstalledPackages()
    BE->>Alpm: alpm_initialize(root, dbpath, &err)
    alt handle == nullptr
        Alpm-->>BE: err
        BE-->>UC: unexpected(PackageSourceError)
    else handle opened
        BE->>Alpm: glob dbpath/sync/*.db, alpm_register_syncdb per entry
        BE->>Alpm: alpm_db_get_pkgcache(localdb)
        Alpm-->>BE: alpm_list_t* of alpm_pkg_t*
        BE->>BE: map each alpm_pkg_t -> domain::Package
        BE->>Alpm: alpm_release(handle)
        BE-->>UC: vector<Package> (possibly empty)
    end
    UC-->>Pool: expected<vector<Package>, PackageSourceError>
    Pool-->>Model: QFutureWatcher::finished() (auto-delivered on GUI thread)
    alt result has value
        Model->>Model: beginResetModel/endResetModel, status = Loaded
    else result has error
        Model->>Model: status = Error, errorMessage = result.error().message
    end
    Model-->>QML: statusChanged(), dataChanged/modelReset
    QML->>QML: spinner hidden; list or error banner shown (<=100ms, REQ-NF-002)
```

Narrative:

1. `PackagesApplication` builds the chain `AlpmPackageSource → PackageListUseCase → InstalledPackagesModel` at
   startup, with production paths `"/"` / `"/var/lib/pacman"` supplied at this one call site (REQ-C-001: the
   *backend* never hardcodes them; the app-wiring code is where the production default legitimately lives).
2. `InstalledPackagesModel`'s constructor sets `status = Loading` and immediately kicks off
   `QtConcurrent::run(...)` (§4). The GUI thread returns immediately; `PackagesApplication` proceeds to
   `view_->setSource(...)`.
3. QML never sees mock/hardcoded data (REQ-F-004): the only data path into `InstalledPackagesView.qml` is the
   injected `InstalledPackagesModel`, whose rows come from `Model::data()`, which reads `packages_`, which is only ever
   populated inside `onEnumerationFinished()`.
4. On completion, `QFutureWatcher::finished()` is delivered as a queued signal back on the GUI thread (Qt does this
   automatically because the watcher object's thread affinity is the GUI thread — no manual
   `QMetaObject::invokeMethod` needed). The model resets its rows or records the error, then emits
   `statusChanged()`; QML re-evaluates its bindings same-frame.
5. A `refresh()` invocation (future "reload" affordance, not required by this SPEC but cheap to expose as
   `Q_INVOKABLE`) re-enters at step 2, guarded by `!watcher_.isRunning()` to avoid two concurrent libalpm calls on
   the same `AlpmPackageSource` (see thread-safety note, §9).

---

## 3. Interfaces

### 3.1 `Package` (domain) — REQ-F-001, exactly 7 fields

```cpp
// src/domain/include/holonight_packages_domain/package.h
#pragma once

#include <string>

namespace holonight_packages_domain {

enum class SourceType { Official, Foreign };
enum class InstallReason { Explicit, Dependency };

struct Package {
  std::string identity;             // unique system identifier
  std::string name;                 // alpm_pkg_get_name()
  std::string installedVersion;     // alpm_pkg_get_version()
  SourceType sourceType = SourceType::Foreign;
  std::string repository;           // sync db name; empty for Foreign
  InstallReason installReason = InstallReason::Explicit;
  std::string backendSpecificId;    // libalpm package name, for backend correlation

  bool operator==(const Package&) const = default;
};

}  // namespace holonight_packages_domain
```

### 3.2 Backend adapter (backends) — REQ-F-002 / REQ-C-001

```cpp
// src/domain/include/holonight_packages_domain/package_source.h
#pragma once

#include <expected>
#include <string>
#include <vector>

#include "holonight_packages_domain/package.h"

namespace holonight_packages_domain {

enum class PackageSourceErrorCode { DatabaseRootInvalid, DatabaseOpenFailed, Unknown };

struct PackageSourceError {
  PackageSourceErrorCode code;
  std::string message;  // human-readable, from alpm_strerror() where applicable
};

class PackageSource {
 public:
  virtual ~PackageSource();
  virtual std::expected<std::vector<Package>, PackageSourceError> enumerateInstalledPackages() const = 0;
};

}  // namespace holonight_packages_domain
```

```cpp
// src/backends/include/holonight_packages_backends/alpm_package_source.h
#pragma once

#include <filesystem>

#include "holonight_packages_domain/package_source.h"

namespace holonight_packages_backends {

class AlpmPackageSource : public holonight_packages_domain::PackageSource {
 public:
  AlpmPackageSource(std::filesystem::path database_root, std::filesystem::path database_path);

  std::expected<std::vector<holonight_packages_domain::Package>, holonight_packages_domain::PackageSourceError>
  enumerateInstalledPackages() const override;

 private:
  std::filesystem::path database_root_;
  std::filesystem::path database_path_;
};

}  // namespace holonight_packages_backends
```

No constructor default arguments for the paths — passing them is mandatory, so production wiring and test wiring
look identical in shape (only the values differ), which is what REQ-C-001's acceptance criteria checks for.

### 3.3 Application use case (application) — REQ-F-003

```cpp
// src/application/include/holonight_packages_application/package_list_use_case.h
#pragma once

#include <expected>
#include <memory>
#include <vector>

#include "holonight_packages_domain/package.h"
#include "holonight_packages_domain/package_source.h"

namespace holonight_packages_application {

class PackageListUseCase {
 public:
  explicit PackageListUseCase(std::shared_ptr<holonight_packages_domain::PackageSource> source);

  std::expected<std::vector<holonight_packages_domain::Package>, holonight_packages_domain::PackageSourceError>
  getInstalledPackages() const;

 private:
  std::shared_ptr<holonight_packages_domain::PackageSource> source_;
};

}  // namespace holonight_packages_application
```

`getInstalledPackages()` calls `source_->enumerateInstalledPackages()`; on success it `std::sort`s the vector by
`name` (ascending) and returns it; on failure it passes the `PackageSourceError` through unchanged. No QML/Qt/UI
type appears anywhere in this header or its `.cpp` (REQ-F-003's constraint), which is exactly what lets
`PackageListUseCase` be unit-tested with a mock `PackageSource` and zero Qt GUI machinery (only `Qt6::Core`, already
linked, for `QString`-free utility use if ever needed — none is needed here).

### 3.4 QML exposure — decision: typed `QAbstractListModel` injection, not a singleton

`InstalledPackagesModel` (in `apps/packages/app/`, **not** in `src/application/` — see §6) is both the list model
and the small view-model that owns the three states:

```cpp
// apps/packages/app/InstalledPackagesModel.h
#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>

#include <memory>
#include <vector>

#include "holonight_packages_application/package_list_use_case.h"

class InstalledPackagesModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(Status status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)

 public:
  enum class Status { Loading, Loaded, Error };
  Q_ENUM(Status)

  enum Role { NameRole = Qt::UserRole + 1, InstalledVersionRole, SourceLabelRole, RepositoryRole };

  explicit InstalledPackagesModel(std::shared_ptr<holonight_packages_application::PackageListUseCase> use_case,
                                   QObject* parent = nullptr);
  ~InstalledPackagesModel() override;

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  Status status() const;
  QString errorMessage() const;

  Q_INVOKABLE void refresh();

 signals:
  void statusChanged();

 private:
  void onEnumerationFinished();

  using LoadResult = std::expected<std::vector<holonight_packages_domain::Package>,
                                    holonight_packages_domain::PackageSourceError>;

  std::shared_ptr<holonight_packages_application::PackageListUseCase> use_case_;
  QFutureWatcher<LoadResult> watcher_;
  std::vector<holonight_packages_domain::Package> packages_;
  Status status_ = Status::Loading;
  QString error_message_;
};
```

Exposed to QML as an uncreatable typed model under the existing `HolonightPackages` module. The application passes
the concrete instance to `WorkspaceWindow` with `QQuickView::setInitialProperties()` before `setSource()`, and the
window passes it to the feature view through required typed properties. QML then does:

```qml
import HolonightPackages

ListView {
    required property InstalledPackagesModel installedPackagesModel
    model: installedPackagesModel
    // ...
}
```

Why a role-based `QAbstractListModel` instead of a flat `Q_PROPERTY QVariantList packages`:

- `ListView` is designed around `QAbstractListModel`/roles; this is the idiomatic, documented Qt6 pairing, and it
  is what makes REQ-F-004's "hundreds of packages without lag" cheap — `ListView` only instantiates delegates for
  visible rows and reads roles per-row, versus a `QVariantList` of `QVariantMap`s where every refresh replaces the
  whole list and every delegate binding does a string-keyed map lookup.
- Loading/loaded/error stay as two extra `Q_PROPERTY`s on the *same* object that also is the model, so QML has a
  single injected object and no separate view-model object to also bind.
- `beginResetModel()`/`endResetModel()` gives `ListView` the standard reset notification; no custom signal needed.

Only 4 of `Package`'s 7 fields get roles (`name`, `installedVersion`, a derived `sourceLabel` from `sourceType`, and
`repository`, since it pairs naturally with the source badge). `identity`, `installReason`, `backendSpecificId` have
no QML consumer yet under this SPEC's REQ-F-004 and get no role — adding one later is a one-line change to
`roleNames()`/`data()` when a concrete consumer exists, consistent with REQ-NF-003's producer/consumer discipline
(that requirement governs domain fields specifically, but the same reasoning applies here by extension).

---

## 4. Async mechanism decision

**Decision: `QtConcurrent::run()` + `QFutureWatcher<T>`**, not a dedicated worker `QObject` moved to a `QThread`.

Rationale, tied directly to the three facts SPEC.md calls out in REQ-F-005:

- **One-shot operation.** `enumerateInstalledPackages()` opens a handle, reads the local DB once, and releases the
  handle — there is no persistent state or repeated interaction with libalpm across the operation's lifetime. A
  `QThread` + worker `QObject` is the right tool when a thread needs to *stay alive* (own long-lived state, receive
  further requests via queued slots, emit progress). Here nothing outlives the single call. `QtConcurrent::run`
  is exactly "run this callable once on a pool thread," which is the actual shape of the work.
- **No progress callbacks needed.** libalpm does expose progress/log callbacks (`alpm_option_set_logcb`, etc.),
  but SPEC.md's non-goals exclude transactions and REQ-F-005 explicitly only requires a binary loading/not-loading
  state, not incremental progress. A `QThread` worker earns its keep when it needs to `emit progress(int)` mid-task;
  nothing here does.
- **Qt6 already a dependency; avoid overbuilding.** `QtConcurrent` is part of the Qt6 the project already links
  against (adds one new component, `Qt6::Concurrent`, to `find_package`). It removes all the boilerplate a
  `QThread` approach requires: no `moveToThread`, no explicit `start()`/`quit()`/`wait()` lifecycle, no manual
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to marshal the result back — `QFutureWatcher` lives on the
  GUI thread by construction and delivers `finished()` there automatically.

Thread-safety consequence (this is the load-bearing constraint, see §9): `alpm_handle_t` is **not** thread-safe.
`AlpmPackageSource::enumerateInstalledPackages()` is designed to be entirely self-contained per call — it opens a
fresh `alpm_handle_t`, does all its work, and calls `alpm_release()` before returning, all within the single
lambda passed to `QtConcurrent::run`. No `alpm_handle_t` is ever cached as a member or touched from more than one
thread. `InstalledPackagesModel::refresh()` guards against re-entrancy (`if (watcher_.isRunning()) return;`) so two
concurrent calls into the same `AlpmPackageSource` instance from two pool threads can't happen.

```cpp
void InstalledPackagesModel::refresh() {
  if (watcher_.isRunning()) return;
  status_ = Status::Loading;
  emit statusChanged();
  watcher_.setFuture(QtConcurrent::run([uc = use_case_] { return uc->getInstalledPackages(); }));
}
```

---

## 5. Test fixture design (REQ-NF-001)

libalpm's local DB is a plain directory tree; its sync DB is a gzip tar of the same per-package shape. Both are
static, checked-in **text** files (plus one small checked-in **binary** sync tarball) — nothing is generated at
configure or build time, matching REQ-NF-001's wording ("checked-in fixture pacman database").

### Layout

```
tests/fixtures/pacman/
  populated/
    local/
      ALPM_DB_VERSION                # local-db format version marker libalpm expects
      apple-2.3-4/
        desc
        files
      zebra-1.0-1/
        desc
        files
      foreign-tool-9.9-1/            # not present in any sync db below -> Foreign
        desc
        files
    sync/
      core.db                        # gzip tar of apple-2.3-4/desc, zebra-1.0-1/desc (NOT foreign-tool)
  empty/
    local/
      ALPM_DB_VERSION                # valid, but no package subdirectories
    sync/
      core.db                        # empty tar, or omitted entirely
```

### `desc` file (flat key/value, local db)

```
%NAME%
apple

%VERSION%
2.3-4

%BASE%
apple

%REASON%
0

%VALIDATION%
none
```

`%REASON%` `0` = `ALPM_PKG_REASON_EXPLICIT`, `1` = `ALPM_PKG_REASON_DEPEND` — the fixture set deliberately includes
at least one of each so the Explicit/Dependency mapping (REQ-F-002) has real coverage: e.g. `apple` reason `0`
(Explicit), `zebra` reason `1` (Dependency).

### Official vs. Foreign coverage

- `apple` and `zebra` appear in `sync/core.db` → `sourceType = Official`, `repository = "core"`.
- `foreign-tool` appears only in `local/` → `sourceType = Foreign`, `repository = ""`.

This directly exercises `AlpmPackageSource`'s sync-db-membership check (§6) with both outcomes in one fixture.

### How the sync db and adapter find each other

`AlpmPackageSource` discovers sync repositories from **`<database_path>/sync/*.db`**, sorts repository names
lexically, and calls `alpm_register_syncdb()` in that order (repo name = filename without `.db`). Registration and
cache-loading failures are returned as errors; a missing sync directory means no configured official repositories.
Signature checking uses libalpm's default policy since
this feature never verifies package signatures — REQ non-goal: "Trust/security level or package verification
details"). It does **not** parse `/etc/pacman.conf`. See §6 for why.

### Getting the fixture into the test binary

`AlpmPackageSource` takes the DB root/path as explicit constructor parameters (REQ-C-001), so "getting the fixture
into the test binary" just means computing an absolute path to `tests/fixtures/pacman/populated` (or `.../empty`)
at test time. Chosen approach: bake the fixtures directory in as a compile definition pointing straight at the
source tree — no copy step:

```cmake
# tests/CMakeLists.txt
target_compile_definitions(test_holonight_packages PRIVATE
  HOLONIGHT_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
```

```cpp
auto fixture_root = std::filesystem::path(HOLONIGHT_TEST_FIXTURES_DIR) / "pacman" / "populated";
AlpmPackageSource source(fixture_root, fixture_root);  // pacman fixtures use one dir as both root and dbpath
```

(Root and dbpath can validly be the same directory in a test fixture — production pacman keeps them separate
because `dbpath` normally sits under a live filesystem root, but libalpm itself imposes no such requirement.)

Regenerating `sync/core.db` (the one binary artifact) is a one-time maintainer step — build the same-shaped `desc`
files under a scratch `core/<pkg>-<ver>/desc` layout and `bsdtar czf core.db -C core .` (or `tar`/`gzip`
equivalent) — documented as a comment at the top of the fixture directory, not automated into the build.

---

## 6. Key decisions with rationale

| Decision | Rationale |
|---|---|
| `PackageSource` interface lives in `src/domain/`, not `src/application/` | `application` currently depends only on `domain`; `backends` currently depends only on `domain` (see current `CMakeLists.txt`s). Putting the interface in `application` would force `backends → application`, and putting it in `backends` would force `application → backends` (which would drag `PkgConfig::Alpm` into every consumer of the use case, including tests that shouldn't need libalpm at all). Putting the shared port in `domain` — which both already depend on — needs **zero new CMake target edges**. |
| `Package`/`PackageSourceError` use `std::string`/`std::expected`, not `QString`/Qt error types | Keeps `domain` (and `application`, which only re-exports these types) constructible and testable without a `QCoreApplication`/event loop, and keeps REQ-F-003's "no QML-specific transformations" constraint mechanically true — there is nothing Qt-GUI-specific in these headers to transform away from. |
| `std::expected<std::vector<Package>, PackageSourceError>` as the return type at every layer (backend → use case → model) | Makes REQ-F-006 vs REQ-F-007's "error state" vs "empty-but-valid state" distinction a *type-level* fact instead of a sentinel/out-param convention: an empty `vector` inside a successful `expected` is unambiguously "0 packages, no error"; `unexpected(PackageSourceError)` is unambiguously "error." No code path can accidentally conflate the two, which is exactly what REQ-F-007's acceptance criteria is checking for. |
| `InstalledPackagesModel` (`QAbstractListModel`) lives in `apps/packages/app/`, not `src/application/` | REQ-F-003 explicitly forbids QML-specific transformations in `application`. A `QAbstractListModel` with QML-facing roles *is* a QML-specific transformation of the domain list, so it belongs in the executable/app-shell layer that already owns QML registration, not in the reusable `application` library. |
| Sync repos discovered from `<dbpath>/sync/*.db`, not by parsing `/etc/pacman.conf` | REQ-C-001 constrains the adapter to two explicit paths. Names are sorted lexically, providing deterministic precedence when a package appears in multiple repositories. Filesystem, registration, and cache-load failures are propagated. |
| `QtConcurrent::run` + `QFutureWatcher`, not `QThread` + worker `QObject` | See §4 in full — one-shot operation, no progress needed, minimizes new lifecycle code, given the explicit REQ-F-005 framing that this is a design-stage-only, mechanism-agnostic requirement. |
| `InstalledPackagesModel` injected through required typed properties | The type is registered as uncreatable for QML tooling, while ownership stays in C++. `setInitialProperties()` avoids procedural globals and keeps dependencies explicit and testable. |
| Public headers (`alpm_package_source.h`) never include `<alpm.h>` | Keeps libalpm an implementation-only dependency of `backends`; `PkgConfig::Alpm` is linked `PRIVATE`, so nothing above `backends` needs libalpm's include path, and swapping/mocking the backend never requires touching libalpm headers. |
| Domain's first `.cpp` is `PackageSource`'s out-of-line virtual destructor | Satisfies REQ-F-008's "each target receives real `.cpp` files, not stub headers only" with something genuinely idiomatic (out-of-line key function avoids emitting the vtable in every including TU, a real `-Wweak-vtables`-style concern) rather than an arbitrary placeholder function invented just to have a `.cpp`. |

---

## 7. Alternatives considered

**Async mechanism**

| Alternative | Pros | Cons | Verdict |
|---|---|---|---|
| `QThread` + worker `QObject` (`moveToThread`) | Persistent thread reusable if a future feature needs live progress/transaction callbacks (per `docs/ideas/01-high-level-project-idea.md`'s later phases) | Manual thread lifecycle (`start`/`quit`/`wait`, teardown-order bugs), `moveToThread` boilerplate, manual signal marshaling — all for a task that runs once and exits | Rejected for this iteration; revisit if/when a persistent transaction worker is actually built (separate future SDD cycle, out of this SPEC's scope) |
| Raw `std::thread`/`std::async` + manual `QMetaObject::invokeMethod` back to GUI thread | No new Qt module needed | Reimplements what `QFutureWatcher` already does correctly; easy to get the queued-connection marshaling wrong; no `QFuture`-based cancellation/`isRunning()` for free | Rejected — strictly more code and more risk for the same outcome |
| Synchronous call + `QCoreApplication::processEvents()` pump in the caller | Trivial to write | Doesn't actually parallelize (libalpm still executes on the GUI thread in bursts), reentrancy hazards from `processEvents()`, does not satisfy REQ-F-005's "does not block" intent even if it happens to hit the 16ms number on a fast disk | Rejected outright |

**QML exposure model**

| Alternative | Pros | Cons | Verdict |
|---|---|---|---|
| `Q_PROPERTY QVariantList packages` + separate status/error properties, no custom model class | Least code to write | Whole-list replace on every refresh, per-row string-keyed `QVariantMap` lookups in delegate bindings, no free `QSortFilterProxyModel` compatibility if filtering/sorting UI is added later, less idiomatic pairing with `ListView` for REQ-F-004's "hundreds of packages" case | Rejected — works, but is the less-idiomatic and less scalable option for exactly the case (large list, `ListView`) this feature is |
| Plain `rootContext()->setContextProperty(...)` | Minimal code, works immediately with the existing `QQuickView` bootstrap | Untyped/unqualified from `qmllint`'s perspective, which conflicts with the project's `task qml-lint` gate | Rejected — see §6 |
| `required property` passed from `QQuickView::setInitialProperties()` | Explicit, typed, no global singleton | Dependencies must be threaded through consuming components | **Chosen**; the dependency is currently needed by one feature subtree |

**Fixture DB approach**

| Alternative | Pros | Cons | Verdict |
|---|---|---|---|
| Generate the fixture DB at build/test-configure time (script/`CMake` custom command) | Fixture package set becomes parametrizable in code, fewer checked-in files | New build-time dependency (a generator script/tool) purely for tests, harder to `git diff`/review than flat text files, and directly conflicts with REQ-NF-001's "checked-in fixture pacman database" wording | Rejected — REQ-NF-001 specifically asks for a checked-in fixture, not a generated one |
| Copy fixtures into `${CMAKE_BINARY_DIR}` at configure time (`file(COPY ...)`) | Test binary becomes relocatable without the source tree present at runtime | Extra CMake step, must remember `CONFIGURE_DEPENDS`/re-run on fixture edits, no actual benefit for this project's dev-only (non-packaged) test binary | Rejected for now (§5's compile-definition approach is simpler); revisit only if test binaries are ever packaged/distributed independently of the source tree |
| Point directly at `tests/fixtures/` via source-tree-relative path baked in at compile time | Zero copy step, zero staleness, trivial CMake | Test binary needs the source tree present at runtime (fine — it always runs from a checkout in dev/CI) | **Chosen** |

---

## 8. Known risks

- **libalpm's ambiguous NULL returns.** Several `alpm_*` accessors (notably `alpm_db_get_pkgcache()`) return `NULL`
  both when the result set is legitimately empty *and* when an internal error occurred. `AlpmPackageSource` must
  call `alpm_errno(handle)` immediately after such calls to disambiguate, rather than treating `NULL` alone as
  "empty" — getting this wrong would directly violate REQ-F-007 (an error could silently present as an empty,
  valid list).
- **`alpm_handle_t` is not thread-safe.** The whole async design in §4 depends on the invariant that exactly one
  `alpm_handle_t`, opened and released within a single `QtConcurrent::run` callable, is ever in play at a time. A
  future change that caches the handle as a member (e.g., to avoid re-opening it on every `refresh()` for
  performance) would reintroduce a real data race unless paired with an explicit single-thread affinity — at which
  point the `QThread`-worker alternative from §7 becomes the correct design, not `QtConcurrent`.
- **libalpm as a system dependency.** The CI image (`Dockerfile.ci`, `FROM archlinux:latest`) doesn't currently
  install anything named `libalpm` explicitly — but on Arch, `libalpm.so`, `alpm.h`, and `libalpm.pc` ship as part
  of the base `pacman` package itself (Arch doesn't split "-dev" packages), and `pacman` is present in any Arch
  base image. This needs to be verified once (`pacman -Ql pacman | grep -E 'alpm\.h|libalpm\.pc'` inside the CI
  image) during implementation; if it's ever missing, `Dockerfile.ci` needs an explicit `pacman -S pacman` (a
  no-op reinstall) or, if Arch's docker image strips pkgconfig files via `NoExtract` rules, an explicit
  `pkgconf`-visible `.pc` file added by hand.
- **`INTERFACE` → `STATIC` conversion mechanics.** For each of the three targets, `target_include_directories(...
  INTERFACE ...)` becomes `PUBLIC` (consumers still need the headers) while any include directories used only by
  the `.cpp` (none currently, except `backends`' need for `PkgConfig::Alpm`'s includes) stay `PRIVATE`. Likewise
  `target_link_libraries`: `Qt6::Core` on `domain`/`application` can plausibly move to `PRIVATE` (neither module's
  public headers use any Qt type — they're pure `std::`), but `PkgConfig::Alpm` on `backends` **must** be
  `PRIVATE` specifically, since leaking it `PUBLIC` would force every consumer (including `application`'s unit
  tests, which should never need libalpm) to also see libalpm's include path. Getting `PUBLIC`/`PRIVATE` wrong in
  either direction either breaks a consumer's `#include` or needlessly leaks an implementation dependency outward.
- **`qmllint` and delegate bindings.** New QML (`InstalledPackagesView.qml`, `PackageRowDelegate.qml`) must pass
  `task qml-lint`. `ListView` delegates should use `required property` for each role they bind (e.g.
  `required property string name`) rather than bare unqualified `name`/`installedVersion` references — with
  the `unqualified` category enabled, so `required property` also keeps delegates self-documenting about
  which roles they consume.
- **Test event-loop pumping.** `tests/main.cpp` constructs a `QGuiApplication` but never calls `exec()`.
  `QFutureWatcher::finished()` is delivered via the event loop, so `installed_packages_model_test.cpp` must
  explicitly pump events (e.g. `QSignalSpy` + `QSignalSpy::wait()`, or a scoped `QEventLoop::exec()` triggered by
  the `finished` signal) around any assertion that depends on the async load completing — a plain synchronous
  `EXPECT_EQ` right after calling `refresh()` will observe `Status::Loading` and nothing else.

---

## Related Documents

- **Specification**: `docs/sdd/installed-packages-list/SPEC.md`
- **Codebase reference**: `CLAUDE.md`
- **Build system**: `Taskfile.yml`, root `CMakeLists.txt`, `Dockerfile.ci`, `.github/workflows/ci.yml`

---

## Change History

| Version | Date | Author | Notes |
|---|---|---|---|
| 1.0 | 2026-08-11 | SDD Process | Initial design derived from SPEC.md v1.0 |
