#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
build=build/dev
prefix="$PWD/build/deps/prefix"
stage="$PWD/build/installed"
mkdir -p /work/output
    for style in Holonight Fusion; do
      printf "Installed packages QML acceptance: style=%s\n" "$style"
      QT_QUICK_CONTROLS_STYLE="$style" ctest --test-dir "$build" \
        -R InstalledPackagesViewTest --output-on-failure --no-tests=error
    done
    python3 scripts/check-runtime-launches.py "$build/holonight-packages" "$prefix" --logs /work/output/build-launch
