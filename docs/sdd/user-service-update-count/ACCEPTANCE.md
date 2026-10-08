# Acceptance — user-service-update-count

Date: 2026-10-09. Run from the repository root with the pinned sibling providers staged in `build/deps`.

| Command | Result |
|---|---|
| `task check` (build, test, format-check, tidy + qml-lint, qml-import-check, qmltypes-check; default `debug` preset) | exit 0 |
| `ctest` in `build/test` | 287 of 287 passed, including `packaged_install_layout` |
| `test_holonight_packages --gtest_filter='Packaged*:UpdateStatusClient*:UpdateMonitor*' --gtest_repeat=10` | no failures |
| `DESTDIR=<tmp> cmake --install build/test --prefix /usr` | installs `usr/bin/holonight-packaged`, `usr/share/dbus-1/services/org.holonight.Packages1.service`, `usr/lib/systemd/user/holonight-packaged.service`, `usr/share/dbus-1/interfaces/org.holonight.Packages1.Updates.xml` |
| `systemd-analyze --user verify <staged unit>` | only complains that `/usr/bin/holonight-packaged` does not exist on this machine (the staged binary is not installed for real) |

## Notes

- `qml-lint` exits 0 and reports no warnings in `Sidebar.qml` or `WorkspaceWindow.qml`; the remaining warnings in `InstalledPackagesView.qml` and others predate this work.
- D-Bus tests start their own `dbus-daemon --session` and fail with a remediation message when it is missing.
- Not done: manual run of the shell indicator (it lives in `holonight-shell`), and a run of the packaged service against the real `/var/lib/pacman`.
- Test authoring note: QTest `QTRY_*` macros do not fail GTest tests, so these tests use `ASSERT_TRUE(QTest::qWaitFor(...))`.
