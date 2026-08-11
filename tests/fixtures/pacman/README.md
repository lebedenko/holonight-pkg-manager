# Fixture pacman databases

Used by `tests/backends/alpm_package_source_test.cpp` so backend tests never touch the live system database.

- `populated/` — 3 installed packages: `apple` (Official/core, Explicit), `zebra` (Official/core, Dependency),
  `foreign-tool` (Foreign — present in `local/` but absent from `sync/core.db`).
- `empty/` — valid local DB, zero installed packages, no sync DBs.

`populated/sync/core.db` is the one binary artifact (gzip tar of per-package `desc` files, pacman's real sync DB
format). To regenerate it after changing which packages are "official": create `<pkg>-<ver>/desc` files (see any
existing entry for the minimal field set) under a scratch directory and run:

```
tar -czf core.db -C <scratch-dir> apple-2.3-4 zebra-1.0-1
```

`foreign-tool-9.9-1` must never appear in `core.db` — that's what makes it Foreign.
