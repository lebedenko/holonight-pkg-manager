# SDD Tasks — user-service-update-count

Each task includes its GTest coverage and CMake edits for the files it adds, and leaves `task build` green.
C++ tasks end with `task format` and `task tidy` clean for the touched files.

- [x] T-001: `UpdateStatus` and `buildStatus`
  - REQs: REQ-F-001, REQ-F-002
  - Check: `update_status_builder_test` passes: 10 normal + 3 ignored rows give `count == 7`, `ignoredCount == 3`, totals excluding ignored; `databasesFound == false` gives `NoDatabases`; an error after a success keeps the earlier count and sets `Error` with `lastError`.

- [x] T-002: `UpdateMonitor` evaluation and coalescing
  - REQs: REQ-F-002, REQ-F-003, REQ-NF-004
  - Check: `update_monitor_test` passes with a fake `UpdateSource`: `refresh()` runs one off-thread Evaluation and emits `statusChanged`; a trigger during a running Evaluation yields exactly one follow-up; the event loop stays responsive during a blocked fake.

- [x] T-003: Filesystem triggers, debounce and re-arm
  - REQs: REQ-F-003, REQ-F-004, REQ-NF-003
  - Check: tests in `update_monitor_test` pass: 20 change events in the window give one Evaluation; replacing a file in a temp dbpath twice gives two Evaluations; with no events no Evaluation occurs after startup.

- [x] T-004: Monitor against the real adapter and fixture
  - REQs: REQ-C-001, REQ-C-002
  - Check: `update_monitor_integration_test` passes on `tests/fixtures/pacman/updates`: expected count and ignored count, and sync `.db` checksums, sizes and mtimes unchanged after several Evaluations.

- [x] T-005: Share default pacman paths
  - REQs: REQ-C-002
  - Check: `PackagesApplication.cpp` and the service use one helper for default paths; existing app tests unchanged and passing; no duplicated path literals (inspection).

- [x] T-006: `holonight-packaged` executable and D-Bus adaptor
  - REQs: REQ-F-005, REQ-F-006, REQ-C-003, REQ-C-004
  - Check: `apps/packaged` builds as a separate target linking `application`, `backends`, `Qt6::DBus`; `packaged_dbus_test` on a private session bus reads all six properties, calls `Refresh`, receives `PropertiesChanged`, and the introspection matches `dbus/org.holonight.Packages1.Updates.xml`; no method takes a command, path or package name.

- [x] T-007: Single instance and clean shutdown
  - REQs: REQ-F-007
  - Check: test starts two instances on the private bus; the second exits 0 without serving; SIGTERM to the first exits promptly.

- [x] T-008: Activation files and install rules
  - REQs: REQ-F-007
  - Check: `cmake --install` into a temporary `DESTDIR` lists the binary, `share/dbus-1/services/org.holonight.Packages1.service` and `lib/systemd/user/holonight-packaged.service`; the unit has `Type=dbus` and a matching `BusName=`; `systemd-analyze verify` passes if available.

- [x] T-009: Preset and CI integration
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: `task test`, `task format-check`, `task tidy` pass with the new targets; D-Bus tests fail with a remediation message when `dbus-daemon` is missing; tests use no real HOME/XDG, dbpath or network.

- [x] T-010: UI status client
  - REQs: REQ-F-008
  - Check: `update_status_client_test` passes: with no service `available == false`, no error; with the test service the badge count follows `PropertiesChanged`; `task qml-lint` and `task qml-import-check` pass; existing dual-style acceptance still passes.

- [x] T-011: Docs and memory sync
  - REQs: —
  - Check: `AGENTS.md` and Serena `core` memory list the new `apps/packaged` target and the Monitor ownership; `ACCEPTANCE.md` records the exact commands and results of `task check`.
