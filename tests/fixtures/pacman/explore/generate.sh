#!/usr/bin/env bash
# Regenerates the explore fixture: local/ (checked in as plain files) and sync/{core,extra}.db (gzip tars).
# Output is deterministic (fixed mtimes, sorted entries, no gzip timestamp) so reruns produce no diff.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
scratch="$(mktemp -d)"
trap 'rm -rf "${scratch}"' EXIT

# local_pkg <name> <version> <installed size>
local_pkg() {
  local dir="${here}/local/$1-$2"
  mkdir -p "${dir}"
  printf '%%NAME%%\n%s\n\n%%VERSION%%\n%s\n\n%%BASE%%\n%s\n\n%%SIZE%%\n%s\n\n%%REASON%%\n0\n\n%%VALIDATION%%\nnone\n\n' \
    "$1" "$2" "$1" "$3" >"${dir}/desc"
  printf '%%FILES%%\nusr/bin/%s\n\n' "$1" >"${dir}/files"
}

# sync_pkg <repo> <name> <version> <download size> <installed size> [description [url [licenses [depends [optdepends]]]]]
# licenses, depends and optdepends are ';'-separated lists.
sync_pkg() {
  local dir="${scratch}/$1/$2-$3"
  mkdir -p "${dir}"
  {
    printf '%%FILENAME%%\n%s-%s-x86_64.pkg.tar.zst\n\n' "$2" "$3"
    printf '%%NAME%%\n%s\n\n%%BASE%%\n%s\n\n%%VERSION%%\n%s\n\n' "$2" "$2" "$3"
    if [[ -n "${6:-}" ]]; then printf '%%DESC%%\n%s\n\n' "$6"; fi
    printf '%%CSIZE%%\n%s\n\n%%ISIZE%%\n%s\n\n' "$4" "$5"
    if [[ -n "${7:-}" ]]; then printf '%%URL%%\n%s\n\n' "$7"; fi
    printf '%%ARCH%%\nx86_64\n\n'
    if [[ -n "${8:-}" ]]; then printf '%%LICENSE%%\n%s\n\n' "${8//;/$'\n'}"; fi
    if [[ -n "${9:-}" ]]; then printf '%%DEPENDS%%\n%s\n\n' "${9//;/$'\n'}"; fi
    if [[ -n "${10:-}" ]]; then printf '%%OPTDEPENDS%%\n%s\n\n' "${10//;/$'\n'}"; fi
  } >"${dir}/desc"
}

rm -rf "${here}/local" "${here}/sync"
mkdir -p "${here}/local" "${here}/sync"
printf '9\n' >"${here}/local/ALPM_DB_VERSION"

local_pkg vim 9.0-1 3000           # installed older than core's 9.1-1
local_pkg vim-runtime 9.1-1 8000   # installed same version as core
local_pkg foreign-tool 1.0-1 100   # foreign: in no sync database

sync_pkg core vim 9.1-1 1500000 3500000 "Vi Improved, a highly configurable text editor" vim.example.org \
  "custom:vim" "vim-runtime=9.1-1;glibc>=2.38" "python: Python language support;ruby: Ruby language support"
sync_pkg core vim-runtime 9.1-1 7000000 8000000 "Runtime files for Vim" vim.example.org "custom:vim"
sync_pkg extra vimb 3.7.0-1 200000 500000 "Vim-like browser" fanglingsu.example.org "GPL-3.0-or-later" "gtk3"
sync_pkg extra gvim 9.1-1 1600000 4000000 "Vi Improved, with GUI support" vim.example.org "custom:vim" "vim-runtime=9.1-1;gtk3"
sync_pkg extra neovim 0.10.0-1 5000000 30000000 "Fork of Vim aiming to improve user experience" neovim.example.org \
  "Apache-2.0;custom:vim" "libuv;luajit"
sync_pkg core nano 8.0-1 600000 2000000 "Pico editor clone with enhancements, not vim-like at all" nano.example.org "GPL-3.0-or-later" "glibc"
sync_pkg core dup 1.0-1 1024 2048 "Package present in two repositories" dup.example.org "MIT"
sync_pkg extra dup 2.0-1 2048 4096 "Package present in two repositories" dup.example.org "MIT"
sync_pkg extra bare 0.1-1 10 20

for repo in core extra; do
  tar --sort=name --mtime='2026-01-01 00:00:00Z' --owner=0 --group=0 --numeric-owner \
    -C "${scratch}/${repo}" -cf - $(ls "${scratch}/${repo}") | gzip -n >"${here}/sync/${repo}.db"
done
