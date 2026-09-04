# Software Design Specification: ALPM Sync Database Caching

**Feature:** In-memory cache for ALPM sync database registration and parsing
**Component:** `AlpmPackageSource` (in `src/backends/src/alpm_package_source.cpp`)
**New class:** `AlpmConnectionCache` (in `src/persistence/`)
**Status:** Requirements specification
**Date:** 2026-09-04

---

## Executive Summary

The current `AlpmPackageSource::enumerateInstalledPackages()` method invokes `alpm_initialize()` and fully re-parses all sync database files on every call, even when their contents have not changed. On systems with large/numerous sync databases (e.g., 1595 packages across 3 repositories with an 8.9 MiB `extra.db`), this redundant work causes measurable latency (110–400 ms per call). This specification defines an in-memory per-instance cache for the initialized ALPM handle and registered sync databases, invalidated only when file modification times change.

---

## Requirements

### Ubiquitous Requirements

#### REQ-F-001: AlpmConnectionCache class in persistence module

**Statement:** The `AlpmConnectionCache` class shall exist in `src/persistence/` and encapsulate ownership of a cached `alpm_handle_t` and the list of registered `alpm_db_t*` pointers.

**Rationale:** Separating cache logic into a dedicated class in the persistence layer maintains separation of concerns and allows testing the cache behavior in isolation.

**Acceptance criteria:**
- A header file `src/persistence/include/holonight_packages_persistence/alpm_connection_cache.h` defines the `AlpmConnectionCache` class.
- The class is declared in namespace `holonight_packages_persistence` (matching the project's existing per-module namespace convention).
- An implementation file `src/persistence/src/alpm_connection_cache.cpp` provides method definitions.
- `src/persistence/CMakeLists.txt` declares a `STATIC` target `holonight_packages_persistence` that links `PkgConfig::Alpm` and exports the cache header.

#### REQ-F-002: Cache ownership by AlpmPackageSource instance

**Statement:** The `AlpmPackageSource` class shall own a single `AlpmConnectionCache` instance as a private member, scoped to the lifetime and configuration of that `AlpmPackageSource` instance.

**Rationale:** Each `AlpmPackageSource` instance manages its own database root; the cache must not leak or be shared across instances, and must be freed when the source is destroyed.

**Acceptance criteria:**
- The `AlpmPackageSource` class declares a private member of type `std::unique_ptr<AlpmConnectionCache>` or equivalent scoped ownership.
- Constructor initializes the cache member with the `database_path_` and `database_root_` constructor parameters.
- No global or static cache variable exists; cache is not shared across instances.
- Destructor (or cache member destructor) releases the cached `alpm_handle_t` via `alpm_release()` if the handle is still valid.

#### REQ-NF-001: Thread safety guard on cached state

**Statement:** The `AlpmConnectionCache` shall protect its internal cached `alpm_handle_t` and `alpm_db_t*` list with a `std::mutex` to ensure memory-model correctness across concurrent calls to `enumerateInstalledPackages()` on the same instance.

**Rationale:** Although overlapping calls do not occur in current production use (guarded by `load_in_progress_` on the GUI thread), `enumerateInstalledPackages()` may be called from different OS threads over time via `QtConcurrent::run`. The mutex ensures C++ memory safety without requiring contention-handling logic.

**Acceptance criteria:**
- `AlpmConnectionCache` declares a `std::mutex` member and acquires a lock before reading or modifying cached state.
- `connection()` returns a move-only lease that retains the lock while callers use the cached ALPM pointers.
- Concurrent calls remain serialized until the previous lease is destroyed; invalidation cannot release an in-use handle.

---

### Event-Driven Requirements

#### REQ-F-003: Freshness check on enumerateInstalledPackages() call

**Statement:** When `AlpmPackageSource::enumerateInstalledPackages()` is called, the cache shall check the modification time (mtime) of every file matching `{database_path}/sync/*.db` and compare against mtimes recorded during the last cache population.

**Rationale:** Sync database updates are infrequent; checking mtime is a low-cost way to detect staleness without re-parsing. Per-file stat() calls are more precise than directory-level mtime checks (which can miss updates).

**Acceptance criteria:**
- Before re-registering any databases, the code calls `std::filesystem::last_write_time()` on each existing `.db` file in the sync directory.
- File mtimes are compared bit-for-bit against cached values; any mismatch marks the cache as stale.
- The set of `.db` file paths is also recorded; if added/removed files are detected, cache is stale.
- If the freshness check succeeds (all files and mtimes unchanged), the code proceeds to REQ-F-004.
- If the freshness check fails (any mtime differs, file list differs, or stat() fails), the code proceeds to REQ-F-005.

#### REQ-F-004: Reuse registered databases on cache hit

**Statement:** When the freshness check succeeds, `enumerateInstalledPackages()` shall reuse the cached `alpm_handle_t` and `alpm_db_t*` list without calling `alpm_register_syncdb()` or `alpm_db_get_pkgcache()` again.

**Rationale:** Registering and parsing sync databases is the dominant cost; skipping it on a cache hit yields the performance benefit.

**Acceptance criteria:**
- When cache mtimes match, `alpm_register_syncdb()` is NOT called.
- `alpm_db_get_pkgcache()` is not invoked for a cached **sync** database. The local package database is opened independently on every enumeration so installed-package changes remain visible.
- The cached `alpm_db_t*` pointers are used directly for the linear-scan classification phase (Official vs. Foreign package detection).
- Return value of `enumerateInstalledPackages()` is identical to a fresh call (see REQ-F-011 for observable correctness).

---

### State-Driven Requirements

#### REQ-F-005: Cache invalidation and repopulation on mtime mismatch

**Statement:** While any sync `.db` file's mtime differs from the cached value, the cache shall be cleared; `alpm_initialize()`, `registerSyncDatabases()`, and all registered database re-parsing shall proceed as if the cache were empty, and the new mtimes shall be recorded for the next call.

**Rationale:** A changed mtime (even if the file content is coincidentally the same for a moment) indicates a potential update; the safest conservative approach is to re-parse. Recording new mtimes ensures the next call can detect further changes.

**Acceptance criteria:**
- When a mtime mismatch is detected, the cached `alpm_handle_t` is released via `alpm_release()` and discarded.
- `alpm_initialize()` is called to create a fresh handle.
- `registerSyncDatabases()` is invoked to re-register all sync databases.
- After successful registration, the new mtimes are stored in cache alongside the new `alpm_handle_t` and `alpm_db_t*` list.
- Subsequent calls to `enumerateInstalledPackages()` on the same instance will use the updated cached state until a further mtime change is detected.

#### REQ-F-006: Cache invalidation on file list change

**Statement:** While the set of `.db` files under `{database_path}/sync/` differs from the set recorded in cache (files added or removed), the cache shall be cleared and re-registered databases shall be recorded.

**Rationale:** Adding or removing repositories changes the set of sync databases; the cache must reflect the current on-disk state.

**Acceptance criteria:**
- The cache records not just mtimes but also the exact list of `.db` file paths (or hashes of that list).
- On freshness check, if the current file list differs from the cached list, cache is marked stale.
- Cache is repopulated and the new file list is recorded (same procedure as REQ-F-005).

---

### Conditional Requirements

#### REQ-F-007: Failure handling during mtime stat()

**Statement:** If a `std::filesystem::last_write_time()` call fails on a sync `.db` file during the freshness check (e.g., transient race condition, file removed by external process), the cache shall be treated as stale and the code shall fall back to full re-parsing without propagating the stat error as a new exception or error code.

**Rationale:** Stat failures are transient and rare; treating them conservatively as "cache invalid" avoids introducing new failure modes while ensuring correctness. An absent sync directory means there are no configured repositories and is not an error; unreadable/non-directory paths and corrupt databases remain fatal.

**Acceptance criteria:**
- A `std::filesystem::filesystem_error` during stat() is caught locally within the freshness-check method.
- No exception is re-thrown; instead, cache is marked stale and re-parsing proceeds.
- The method returns the same `std::vector<Package>` as a full fresh call would.
- Existing hard-error paths (unreadable sync directory, sync path not a directory, `alpm_register_syncdb` failure, corrupt database) continue to propagate `PackageSourceError` exactly as today.

---

### Unwanted Behaviour Requirements

#### REQ-F-008: Observable behavior unchanged (correctness invariant)

**Statement:** If the on-disk state of packages, repositories, and sync database files is unchanged, then caching shall not change which packages are returned, their classification (Official or Foreign), repository names, versions, install reasons, or any other field in the returned `std::vector<Package>`.

**Rationale:** Caching is a pure optimization; it must be transparent to the caller and the business logic.

**Acceptance criteria:**
- Two consecutive calls to `enumerateInstalledPackages()` on the same instance, with no intervening file modifications, return identical `Package` vectors (same count, same fields, same order).
- A call with a cold cache (first call on the instance) returns the same `Package` vector as a call with a warm cache (second call, files unchanged).
- If a sync `.db` file's content changes (new packages, version updates, repository name change), a subsequent call reflecting that change must return the updated `Package` vector (cache properly invalidated).
- If the local installed-package database changes, a subsequent call must reflect the updated package set, version, and install reason even when sync databases are unchanged.

#### REQ-F-009: No new error paths introduced by caching

**Statement:** If a fatal error occurs during cache freshness check, database registration, or package enumeration, the system shall return a `PackageSourceError` with a descriptive message, and caching shall not introduce new error conditions beyond those that would occur without caching.

**Rationale:** Caching is internal optimization; it must not destabilize error handling or create scenarios where a fresh call succeeds but a cached call fails (or vice versa).

**Acceptance criteria:**
- If `alpm_initialize()` fails when repopulating cache, a `PackageSourceError` is returned (same as today, without caching).
- If `alpm_register_syncdb()` fails, a `PackageSourceError` is returned (same as today).
- If the sync directory is missing, enumeration succeeds with no registered sync repositories; if it is unreadable, a `PackageSourceError` is returned.
- A call that fails due to a cache-unrelated error is not masked or transformed by cache logic.
- No new error code, exception type, or message is added specifically because of caching.

#### REQ-F-010: Cache does not mask hard errors during population

**Statement:** If `registerSyncDatabases()` or `alpm_initialize()` fails during cache repopulation, then the error shall be propagated to the caller via `PackageSourceError`, and the cache shall remain in a valid (empty/stale) state safe for retry on a subsequent call.

**Rationale:** Partial or corrupt cache state could cause subtle bugs; cache must always be either fully valid or provably invalid.

**Acceptance criteria:**
- On `alpm_initialize()` failure, no cached handle is retained; the cache's handle pointer is null or marked invalid.
- On `registerSyncDatabases()` failure, if partially-registered databases exist, they are discarded before the method returns an error; the cache is cleared.
- A retry of `enumerateInstalledPackages()` after such a failure attempts fresh initialization (no stale cached state).

---

## Non-Goals and Exclusions

The following are explicitly **out of scope** for this specification:

1. **Package-to-repository classification cost (`toPackage()` O(n·m) scan)**: The related linear scan of each installed package against all registered repositories is addressed separately; this cache addresses only sync database registration/parsing.

2. **Disk-backed or persistent cache**: No SQLite, file-system serialization, or cache that survives process termination. Cache lifetime = AlpmPackageSource instance lifetime.

3. **Cross-instance cache sharing**: No global/static cache or shared state between multiple AlpmPackageSource instances. Each instance has its own independent cache.

4. **Explicit invalidation API or signal**: No public method to manually clear cache, no Qt signals connected to filesystem monitors, no cache-clear hook. Invalidation is automatic via mtime change detection only.

5. **Single-flight or call-collapsing logic**: No deduplication of overlapping in-flight calls. A connection lease keeps the mutex locked so concurrent calls are serialized safely, but their work and results are not merged.

6. **Hard numeric performance SLA**: No requirement that `enumerateInstalledPackages()` complete in under X milliseconds, to avoid flaky timing-dependent tests. Performance improvement is the motivation; acceptance criteria are behavioral and correctness-based.

---

## Dependencies and Build Integration

### REQ-C-001: Persistence module build configuration

**Statement:** The `src/persistence/CMakeLists.txt` shall declare the `holonight_packages_persistence` target (currently an empty `INTERFACE` library) as a `STATIC` library if it contains `.cpp` files, and shall link `PkgConfig::Alpm` as a public/private dependency.

**Rationale:** AlpmConnectionCache needs access to ALPM data types and initialization functions.

**Acceptance criteria:**
- `src/persistence/CMakeLists.txt` includes an `add_library(holonight_packages_persistence STATIC ...)` or `INTERFACE` declaration.
- `target_link_libraries(holonight_packages_persistence ... PkgConfig::Alpm)` is present.
- Build succeeds: `task configure && task build`.

### REQ-C-002: Backends module dependency on persistence

**Statement:** The `src/backends/CMakeLists.txt` shall declare a dependency on `holonight_packages_persistence`, establishing the new layering edge from backends → persistence.

**Rationale:** AlpmPackageSource (in backends) needs to own an AlpmConnectionCache (in persistence).

**Acceptance criteria:**
- `src/backends/CMakeLists.txt` includes `target_link_libraries(holonight_packages_backends holonight_packages_persistence)` or similar.
- No circular dependency: persistence does not link backends.
- Build succeeds: `task configure && task build`.

### REQ-C-003: No shared cache across instances

**Statement:** The cache shall be encapsulated as a private member of AlpmPackageSource and shall not be accessible via any static, global, or inter-instance interface.

**Rationale:** Isolation prevents subtle bugs from cache state leaking between logically independent AlpmPackageSource instances.

**Acceptance criteria:**
- AlpmConnectionCache has no static factory method or global registry.
- AlpmPackageSource's cache member is private.
- No code in AlpmPackageSource or elsewhere accesses another instance's cache.

---

## Testability Approach

Existing tests in `tests/backends/alpm_package_source_test.cpp` use black-box assertions on the returned `std::vector<Package>`. Cache validation follows the same style:

- **Cache-hit proof**: Two consecutive calls on the same instance, with sync `.db` file content unchanged but its mtime artificially held constant via `std::filesystem::last_write_time()`, shall return identical `Package` vectors. This proves the cache is serving the second call without re-parsing.

- **Cache-invalidation proof**: Modify a sync `.db` file's content and allow its mtime to advance naturally. A subsequent call shall return the updated `Package` vector (e.g., new package versions, added/removed repositories). This proves cache invalidation works.

- **Stat-failure robustness**: (If feasible) Mock or test the failure path where stat() on a sync file fails during freshness check. Verify the method falls back to re-parsing without raising a new exception.

No test-only instrumentation (counters, mock call counts) is required; all verification is via observable `Package` data and return values.

---

## Revision History

| Date | Version | Author | Changes |
|------|---------|--------|---------|
| 2026-09-04 | 1.0 | Requirements grilling | Initial specification |
