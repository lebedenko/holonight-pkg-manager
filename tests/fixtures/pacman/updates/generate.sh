#!/usr/bin/env bash
# Regenerates the updates fixture: local/ (checked in as plain files) and sync/{core,extra}.db (gzip tars).
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

# sync_pkg <repo> <name> <version> <download size> <installed size> [group]
sync_pkg() {
  local dir="${scratch}/$1/$2-$3"
  mkdir -p "${dir}"
  {
    printf '%%FILENAME%%\n%s-%s-x86_64.pkg.tar.zst\n\n' "$2" "$3"
    printf '%%NAME%%\n%s\n\n%%BASE%%\n%s\n\n%%VERSION%%\n%s\n\n' "$2" "$2" "$3"
    if [[ $# -ge 6 ]]; then
      printf '%%GROUPS%%\n%s\n\n' "$6"
    fi
    printf '%%CSIZE%%\n%s\n\n%%ISIZE%%\n%s\n\n%%ARCH%%\nx86_64\n\n' "$4" "$5"
  } >"${dir}/desc"
}

rm -rf "${here}/local" "${here}/sync"
mkdir -p "${here}/local" "${here}/sync"
printf '9\n' >"${here}/local/ALPM_DB_VERSION"

local_pkg alpha 1.0-1 1000       # newer in core
local_pkg beta 1.0-1 1000        # equal in core
local_pkg delta 2.0-1 1000       # older in core
local_pkg dup 1.0-1 1000         # newer in core and extra; core (first registered) wins
local_pkg gamma 1.0-1 5000       # newer in extra, smaller installed size (negative delta)
local_pkg ignoreme 1.0-1 1000    # newer in extra, matches IgnorePkg glob "ign*"
local_pkg banana 1.0-1 1000      # newer in extra, sync package in IgnoreGroup "fruits"
local_pkg omega 1.0-1 1000       # foreign: in no sync database

sync_pkg core alpha 2.0-1 2048 1500
sync_pkg core beta 1.0-1 2048 1000
sync_pkg core delta 1.5-1 2048 1000
sync_pkg core dup 2.0-1 1024 1000
sync_pkg extra dup 3.0-1 1024 1000
sync_pkg extra gamma 1.1-1 4096 3000
sync_pkg extra ignoreme 1.1-1 8192 1000
sync_pkg extra banana 1.2-1 16384 1000 fruits

for repo in core extra; do
  tar --sort=name --mtime='2026-01-01 00:00:00Z' --owner=0 --group=0 --numeric-owner \
    -C "${scratch}/${repo}" -cf - $(ls "${scratch}/${repo}") | gzip -n >"${here}/sync/${repo}.db"
done
