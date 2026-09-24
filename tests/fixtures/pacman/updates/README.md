# Updates fixture

Used by `tests/backends/alpm_update_source_test.cpp` (and the parser tests) so update detection never reads the live
system databases. Tests that mutate files copy `local/` and `sync/` into a temporary directory first; the no-databases
cases (missing or empty `sync/`) are created in temporary directories by the tests.

| Package    | Installed | core    | extra   | Expected                                              |
|------------|-----------|---------|---------|-------------------------------------------------------|
| `alpha`    | 1.0-1     | 2.0-1   |         | update from core, download 2048, delta +500           |
| `beta`     | 1.0-1     | 1.0-1   |         | equal, not listed                                     |
| `delta`    | 2.0-1     | 1.5-1   |         | older in sync, not listed                             |
| `dup`      | 1.0-1     | 2.0-1   | 3.0-1   | update to 2.0-1 from core (first registered repo wins) |
| `gamma`    | 1.0-1     |         | 1.1-1   | update from extra, download 4096, delta -2000         |
| `ignoreme` | 1.0-1     |         | 1.1-1   | update, ignored via `IgnorePkg = ign*`                |
| `banana`   | 1.0-1     |         | 1.2-1   | update, ignored via `IgnoreGroup = fruits`            |
| `omega`    | 1.0-1     |         |         | foreign, skipped                                      |

`pacman.conf` is static and hand-written: multi-line `IgnorePkg`, trailing and whole-line comments, unknown keys, and
`IgnorePkg`/`IgnoreGroup` inside repository sections that must not be collected (`alpha` would otherwise be ignored).
It contains no servers and no URLs.

## Regenerating

`local/` and the binary `sync/*.db` files (gzip tars of per-package `desc` files, pacman's sync database format) are
produced by `generate.sh`. Edit the package lists in the script and run it:

```
tests/fixtures/pacman/updates/generate.sh
```

The output is deterministic (fixed mtimes, sorted entries, no gzip timestamp), so rerunning it without changes
produces no diff.
