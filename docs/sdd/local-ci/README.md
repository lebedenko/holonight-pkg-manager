# Local CI rehearsal

Baseline: cc21c2974662ca28a5b97f93c8f8cde1d41ad426. Umbrella CI-012.

Preserve independent build-test/static jobs, main push/PR triggers and separate
licensing. Reuse immutable build and REUSE 6.2.0 images with checksum-pinned
ripgrep/Noto fonts installed in disposable containers. Config fe69a59 unchanged;
Qt 8d11e3e91fea5ad0d20a34f2ed27e5e5f485124a replaces obsolete 863af41 because
production QML already uses rendering/normalColor, introduced at 8d11e3e.

Each lane gets a read-only current-input snapshot and fresh Release providers
(tests/demo/gallery off, Wayland on) plus Debug application (tests/compile commands
on). Preserve full CTest, InstalledPackagesViewTest under both styles, isolated
read-only four-mode build/installed launch, QML lint/types and format/full tidy.
Installed launch additionally rejects the external provider build prefix.
No package transactions or real desktop interaction. Save host-owned complete logs,
revision/dirty/new-file state, immutable images, versions and runtime evidence in
ignored build/ci. Required unavailable checks fail. Docker first/Podman fallback;
publication, upload and submodule pins excluded.

Host clang-tidy 23 diagnostics are resolved without disabling check families.
Anchor header filters to owned source; preserve reflected role APIs. Ignore only
single-line trailing-comma policy due to clang 23 misidentifying argument delimiters
after empty inline initializers, retaining multiline validation (see the
[LLVM check](https://clang.llvm.org/extra/clang-tidy/checks/readability/trailing-comma.html)).

Locally verified on 2026-10-04. Initial full `task ci` evidence is
`build/ci/20261003T205029Z-05ijfkkn/`: independent format/full tidy and REUSE 6.2.0
pass, with 1,422-file source/development-build isolation. The build lane correctly
fails on 37 QML cases because pinned GoogleTest/CMake discovery drops the semicolon
`LD_LIBRARY_PATH` property (retained `tests.json` confirms this). The shared lane
now exports the exact staged provider library path explicitly. It retains CTest
evidence on failure as well as success.

Focused retry `python3 scripts/ci/run.py --lane build-test` passes in
`build/ci/20261003T213624Z-xdq0brbb/`: all 259 CTest cases, Holonight/Fusion
InstalledPackagesViewTest (13 each), eight isolated startup modes, installed-prefix
exclusion, QML lint/types. All 21 retry evidence files are host-owned and 252 source
inputs remain unchanged. The unaffected successful static/licensing lanes were not
repeated. Four launcher regressions and native REUSE/format checks pass after the
script correction. Real Podman is unverified because it is not installed.

Host acceptance: fresh Debug build in `build/ci/native-acceptance`, full CMake
`tidy` under clang-tidy 23.1.1, all 259 CTest cases and `task format-check` pass.
Reuse of AI's exact archived fe69a59/8d11e3e Release provider prefix was checked
against compiler (GCC 16.2.1), Qt (6.11.2), revisions and test/demo/gallery/Wayland
options; see `build/ci/native-provider-provenance.json`. Container versions remain
GCC 16.1.1 / Qt 6.11.1 / clang 22.1.6. Complete native/container logs were reviewed;
only expected private Gui/Qml version-coupling notices remain. C++ string literals
are unchanged; internal names follow conventions while public data and role APIs
are preserved with narrow annotations. No package transactions or desktop focus
interaction occurred.

