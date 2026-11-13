# Development

`task build` and `task test` call CMake and CTest directly. Install dependencies first;
CMake reports missing packages and never downloads them. Use `cmake --preset dev
-DCMAKE_PREFIX_PATH=/path/to/prefix` for an additional installed provider prefix.
The development preset enables tests; `release` stages optimized builds.
clangd uses `build/dev/compile_commands.json`.

The GUI requires AppStreamQt (`appstream-qt` on Arch) in addition to the Qt and ALPM dependencies. The acceptance
image must include it; older image digests without AppStreamQt fail configuration and need to be rebuilt before
running the container gate. HolonightImages is a required installed GUI dependency. The supported Arch installation and acceptance image
require the catalog (`archlinux-appstream-data`) and JXL Qt decoder (`kimageformats`).
Integration requires successful JXL decoding; incomplete installations retain tile fallback.

`task format` uses the pinned image. `task check` is the acceptance gate: fresh provider
builds, build, tests, format, static analysis, applicable QML checks and REUSE.
`task check:integration` adds Release staging and project runtime checks. GitHub invokes
the same launcher with `integration`; no additional validation is hidden in the workflow.
The only host launcher prerequisites are Python 3, Git, tar and Docker, plus Task for aliases.
Builds and clang-tidy use all available CPUs by default (`nproc` inside the container).
Set `JOBS=4 task check` to override the job count. Native component builds use Ninja
defaults; use `CMAKE_BUILD_PARALLEL_LEVEL=4 task build` to limit those.
Image, tool versions and the selected job count appear in the acceptance log. Logs and fresh builds stay under
ignored `build/acceptance/`; downloaded sources are reused under `build/sources/`.

Standalone checks automatically acquire the exact sources in `scripts/ci/providers.json`.
For simultaneous provider/consumer development, explicitly run
`task check -- --siblings /path/to/umbrella`. This uses current sibling files, including
edits. Final umbrella integration uses its gitlinks; never substitute sibling mode for it.
Provider sources are built with ordinary CMake into a disposable prefix. QMarkdown is
an independently installed package in the shared image and is not owned by our installer.

Routine fixes need tests and a commit explanation. Substantial designs use one SDD;
cross-project work uses one umbrella contract note. Keep execution logs out of Git.

Before publishing a replacement acceptance image, test its immutable local image ID with
`task check:integration -- --image-id sha256:...`. This explicit override leaves the
shared registry pin unchanged. Publish and update shared pins only when authorized.
