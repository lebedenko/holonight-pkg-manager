# Explore fixture

Used by `tests/backends/alpm_explore_source_test.cpp` so package enumeration never reads the live system databases.
Tests that mutate files copy `local/` and `sync/` into a temporary directory first. The fixture contains no network
locations: URLs are bare host names.

| Package        | core        | extra       | Installed | Notes                                                         |
|----------------|-------------|-------------|-----------|---------------------------------------------------------------|
| `vim`          | 9.1-1       |             | 9.0-1     | installed version differs; licenses, depends, optdepends set  |
| `vim-runtime`  | 9.1-1       |             | 9.1-1     | installed, same version                                       |
| `vimb`         |             | 3.7.0-1     |           |                                                               |
| `gvim`         |             | 9.1-1       |           |                                                               |
| `neovim`       |             | 0.10.0-1    |           | two licenses                                                  |
| `nano`         | 8.0-1       |             |           | matches "vim" through its description only                    |
| `dup`          | 1.0-1       | 2.0-1       |           | same name in two repositories, both listed                    |
| `bare`         |             | 0.1-1       |           | no description, URL, licenses or dependencies                 |
| `foreign-tool` |             |             | 1.0-1     | local only; never listed                                      |

Query `vim` ranks: `vim`, `vim-runtime`, `vimb`, `gvim`, `neovim`, `nano`.

## Regenerating

`local/` and the binary `sync/*.db` files (gzip tars of per-package `desc` files) are produced by `generate.sh`:

```
tests/fixtures/pacman/explore/generate.sh
```
