# Architecture Design: ALPM Sync Database Caching

**Feature:** In-memory cache for ALPM sync database registration and parsing
**Traces to:** `docs/sdd/alpm-sync-db-cache/SPEC.md`
**Status:** Implemented
**Date:** 2026-09-05

## 1. Scope

`AlpmPackageSource::enumerateInstalledPackages()` previously initialized libalpm and registered every sync database
on every call. The cache retains the handle that owns the registered sync databases, invalidating it when the set
or modification times of `{database_path}/sync/*.db` changes.

Caching is limited to sync databases. Each enumeration creates a separate short-lived ALPM handle for the local
installed-package database. This keeps installs, removals, upgrades, and install-reason changes visible without
paying the sync-database parsing cost again.

## 2. Components and dependency direction

- `holonight_packages_persistence::AlpmConnectionCache` owns the cached sync handle, registered database pointers,
  filesystem snapshot, and mutex.
- `AlpmPackageSource` owns one cache instance and obtains a move-only `AlpmConnection` lease for classification.
- `holonight_packages_backends` links privately to `holonight_packages_persistence`; persistence does not depend on
  backends.
- Persistence is a `STATIC` library because the cache has a `.cpp` implementation.

The public backend header forward-declares the cache and keeps libalpm types out of `AlpmPackageSource`'s API.

## 3. Connection lease and concurrency

`AlpmConnectionCache::connection()` acquires the cache mutex with `std::unique_lock` and transfers that lock into
the returned `AlpmConnection`. The lease provides read-only accessors for the cached handle and sync database list.
Its destruction releases the mutex.

Consequences:

- invalidation cannot release a handle while another enumeration uses it;
- two cache hits cannot call libalpm concurrently on the same handle;
- overlapping calls are serialized but not collapsed or given a shared result;
- callers must keep the lease alive for the complete use of its borrowed pointers.

Holding the lock through classification is intentional. The application normally prevents overlapping loads, and
correct pointer lifetime is more important than permitting concurrent access to a libalpm handle whose thread-safety
contract is not established.

## 4. Freshness snapshot

The snapshot is a lexically sorted vector of `(path, last_write_time)` pairs. Equality detects both file-list and
mtime changes. An absent `sync/` directory produces an empty listing and means that no official repositories are
configured. Inspection, type, iteration, registration, and parsing failures remain hard errors.

`known_snapshot_` is optional. A value is published only when all of the following describe one stable generation:

1. List and snapshot the files before registration.
2. Initialize a new handle and parse that list.
3. List and snapshot the files again.
4. Confirm that both listings and snapshots are identical.

If either stat pass fails or the generation changes during registration, the populated handle may serve the current
call, but the cache snapshot remains invalid. The next call must therefore repopulate rather than blessing parsed
data that does not correspond to the recorded filesystem state.

On registration failure, the new handle is released and no partial database list or valid snapshot is retained.

## 5. Local database freshness

The cached handle is used only for sync repository lookup. While holding its lease, `AlpmPackageSource` initializes
a second scoped ALPM handle and loads the local package database from it. That handle is released at the end of the
enumeration. Repeated calls therefore see local database changes even when the sync cache is warm.

This deliberately keeps `alpm_initialize()` in the warm path. The avoided work is sync registration and parsing,
which is the measured bottleneck and the feature's purpose.

## 6. Error and lifetime invariants

- `AlpmHandleDeleter` calls `alpm_release()` for every owned non-null handle.
- Cache invalidation clears the valid snapshot and database pointers before initializing a replacement handle.
- Initialization and registration errors use the existing `PackageSourceError` codes and messages.
- A missing sync directory is successful and yields an empty sync database list.
- The move-only lease cannot outlive the cache; `AlpmPackageSource` keeps the cache alive throughout enumeration.

## 7. Verification strategy

Tests establish observable behavior rather than relying on allocator pointer identity:

- corrupting a sync database while restoring its original mtime proves an unchanged snapshot reuses parsed data;
- corrupting it with an advanced mtime proves mtime invalidation reparses it;
- adding and removing database files proves file-list invalidation;
- holding one lease while starting another call proves concurrent access remains blocked until lease destruction;
- changing a local package's install reason after warming proves local package data is refreshed;
- corrupt registration followed by repair proves failures leave the cache retryable;
- missing and invalid directory fixtures preserve their distinct success/error behavior.

Timing assertions are used only to observe that a deliberately held lease blocks another call; cache performance has
no numeric SLA.

## 8. Trade-offs and non-goals

- Per-call local handle initialization remains; only sync parsing is cached.
- Classification still scans registered repositories linearly.
- The cache is per `AlpmPackageSource` instance and does not survive process termination.
- There is no explicit invalidation API, filesystem watcher, single-flight result sharing, or disk-backed cache.
- A filesystem update racing the current call can affect that call as it could without caching. The transactional
  snapshot rule guarantees only that such a generation is not reused as a future cache hit.

## Revision history

| Date | Version | Changes |
|------|---------|---------|
| 2026-09-04 | 1.0 | Initial design |
| 2026-09-05 | 1.1 | Added connection lease, transactional snapshots, fresh local handles, and behavior-proving tests |
