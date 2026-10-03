#!/bin/sh
set -eu
lane=$1
mkdir /work/source
cp -a /input/. /work/source/
cd /work/source
export HOME=/work/build/home LC_ALL=C.UTF-8 TZ=UTC
mkdir -p "$HOME"
if [ "$lane" = licensing ]; then
  reuse --version
  reuse lint
  exit
fi
case "$lane" in build-test|static-checks) ;; *) exit 2 ;; esac
python3 --version
git --version
cmake --version
ninja --version
c++ --version
clang-format --version
clang-tidy --version
pkg-config --modversion Qt6Core Qt6Quick libalpm
rg --version
python3 scripts/ci/test_launcher.py
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_FORCE_STDERR_LOGGING=1
export XDG_RUNTIME_DIR=/work/runtime
mkdir -m 700 "$XDG_RUNTIME_DIR"
mkdir -p /work/providers
fetch_provider() {
  name=$1
  revision=$2
  git init -q "/work/providers/$name"
  git -C "/work/providers/$name" fetch --depth 1 "https://github.com/lebedenko/$name.git" "$revision"
  git -C "/work/providers/$name" checkout --detach FETCH_HEAD
  [ "$(git -C "/work/providers/$name" rev-parse HEAD)" = "$revision" ]
}
fetch_provider holonight-config fe69a59e6b73167fd5349223a4d265d75386c139
fetch_provider holonight-qt 8d11e3e91fea5ad0d20a34f2ed27e5e5f485124a
prefix=/work/providers/prefix
cmake -S /work/providers/holonight-config -B /work/providers/config-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build /work/providers/config-build --parallel 2
cmake --install /work/providers/config-build --prefix "$prefix"
cmake -S /work/providers/holonight-qt -B /work/providers/qt-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$prefix" \
  -DBUILD_TESTS=OFF -DBUILD_DEMO=OFF -DBUILD_CONTROLS_GALLERY=OFF -DBUILD_WAYLAND=ON
cmake --build /work/providers/qt-build --parallel 2
cmake --install /work/providers/qt-build --prefix "$prefix"
build=build/verification
if [ "$lane" = build-test ]; then
  trap 'status=$?; if [ -d "$build/Testing" ]; then cp -a "$build/Testing" /output/; fi; exit "$status"' 0
fi
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTS=ON -DCMAKE_PREFIX_PATH="$prefix" -DTIDY_JOBS=2
if [ "$lane" = static-checks ]; then
  cmake --build "$build" --target format-check
  cmake --build "$build" --parallel 2
  cmake --build "$build" --target tidy
  exit
fi
cmake --build "$build" --parallel 2
export LD_LIBRARY_PATH="$prefix/lib"
ctest --test-dir "$build" --show-only=json-v1 > /output/tests.json
ctest --test-dir "$build" --output-on-failure --no-tests=error
for style in Holonight Fusion; do
  printf "Installed packages QML acceptance: style=%s\n" "$style"
  QT_QUICK_CONTROLS_STYLE="$style" ctest --test-dir "$build" \
    -R InstalledPackagesViewTest --output-on-failure --no-tests=error
done
python3 scripts/check-runtime-launches.py "$build/holonight-packages" "$prefix" --logs /output/build-launch
stage=/work/install
cmake -S . -B "$build" -DCMAKE_INSTALL_PREFIX="$stage"
cmake --build "$build" --parallel 2
cmake --install /work/providers/config-build --prefix "$stage"
cmake --install /work/providers/qt-build --prefix "$stage"
cmake --install "$build"
python3 scripts/check-runtime-launches.py "$stage/bin/holonight-packages" "$stage" \
  --forbid-path "$PWD/build" --logs /output/install-launch
python3 - "$prefix" /output/install-launch <<'PY_ASSERT'
from pathlib import Path
import sys
for mode in ('default', 'environment', 'command-line', 'external-config'):
    for extension in ('log', 'maps'):
        evidence = Path(sys.argv[2]) / f'{mode}.{extension}'
        assert sys.argv[1] not in evidence.read_text(), f'{mode}: installed provider build path discovered'
PY_ASSERT
cmake --build "$build" --target qml-lint
bash scripts/check-qmltypes.sh "$build"
