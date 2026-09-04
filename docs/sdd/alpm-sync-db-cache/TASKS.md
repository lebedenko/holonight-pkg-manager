# SDD Tasks — alpm-sync-db-cache

- [x] T-001: Configure persistence module as STATIC library linking ALPM
  - REQs: REQ-C-001
  - Check: `src/persistence/CMakeLists.txt` declares `add_library(holonight_packages_persistence STATIC ...)`, links `PkgConfig::Alpm PUBLIC`, and `task configure && task build` succeeds without persistence-build errors.

- [x] T-002: Link backends module to persistence
  - REQs: REQ-C-002
  - Check: `src/backends/CMakeLists.txt` includes `target_link_libraries(holonight_packages_backends holonight_packages_persistence)` with no circular dependency; `task configure && task build` succeeds.

- [x] T-003: Wire test target to include alpm_connection_cache_test and persistence dependency
  - REQs: REQ-C-001 (build impact)
  - Check: `tests/CMakeLists.txt` includes `tests/persistence/alpm_connection_cache_test.cpp` in sources and links `holonight_packages_persistence`; `task configure` succeeds.

- [x] T-004: Implement AlpmConnectionCache header per DESIGN.md §7.1
  - REQs: REQ-F-001, REQ-NF-001
  - Check: the header defines the ALPM deleter, a move-only `AlpmConnection` lease with read-only accessors, and a non-copyable/non-movable cache whose `connection()` returns `std::expected<AlpmConnection, PackageSourceError>`.

- [x] T-005: Implement AlpmConnectionCache methods per DESIGN.md §7.2
  - REQs: REQ-F-003, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-009, REQ-F-010
  - Check: `connection()` transfers a `std::unique_lock` into the returned lease; population records a valid snapshot only when pre/post listings and mtimes match; failures leave the cache retryable.

- [x] T-006: Refactor AlpmPackageSource header and constructor per DESIGN.md §7.3
  - REQs: REQ-F-002, REQ-C-003
  - Check: `src/backends/include/holonight_packages_backends/alpm_package_source.h` forward-declares `AlpmConnectionCache`, adds out-of-line destructor declaration, adds private `std::unique_ptr<holonight_packages_persistence::AlpmConnectionCache> connection_cache_` member, and no new `#include` of `<alpm.h>` or persistence header; constructor initializes `connection_cache_` via `std::make_unique`.

- [x] T-007: Update AlpmPackageSource::enumerateInstalledPackages to use cache per DESIGN.md §7.4
  - REQs: REQ-F-004, REQ-F-008, REQ-F-009
  - Check: `AlpmPackageSource` holds the sync lease through classification and uses a fresh scoped ALPM handle for local-db enumeration so local changes remain visible.

- [x] T-008: Write unit tests for AlpmConnectionCache per DESIGN.md §9.1
  - REQs: REQ-F-003, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-009, REQ-F-010
  - Check: cache tests prove unchanged-mtime reuse, changed-mtime reparsing, serialized leases, file-list invalidation, retry after failure, and missing/invalid directory behavior.

- [x] T-009: Write integration tests for AlpmPackageSource caching behavior per DESIGN.md §9.2
  - REQs: REQ-F-008
  - Check: integration tests cover unchanged output, repository changes, and local package changes after warming.

- [x] T-010: Verify format, tidy, and test gates
  - REQs: all (verification)
  - Check: `task format`, `task tidy`, and `task test` all complete without new errors; `ctest --test-dir build --output-on-failure --no-tests=error` passes all tests including the 10 new cache/integration tests from T-008 and T-009.
