# Application icon refresh

Work package: AIR-002

Baseline: `d62451e4b75589b87b323dad16bfdb8ac63903ee`

Replace `assets/holonight-pkg-manager.svg` with the supplied `~/Pictures/hn-apps/packages.svg` unchanged. Preserve existing Qt resource aliases, consumers, and installation rules. No application behavior changes are required.

Verification: compare the destination byte-for-byte with the supplied SVG, render it with Qt SVG offscreen, and run `git diff --check`.

Verified 2026-10-02: destination matches the supplied SVG byte-for-byte; Qt6 QSvgRenderer accepted the SVG and produced nonempty offscreen renders at 24, 48, and 170 px; `git diff --check` passed.

Publication acceptance 2026-10-02: `task build` passed from a new `build/debug` directory, with freshly built providers at the umbrella-pinned revisions. Complete build logs were scanned for errors and warnings. Warnings are existing Qt private-module compatibility notices and unused generic provider CMake options; no artwork or compiler warnings occurred. Qt resource paths remain unchanged. No behavior, QML, or CMake code changed, so focused SVG checks are the applicable tests.

Provider revisions:
- `holonight-config`: `b185c8404368cee6b2729ee17ffbcc671ba41cf3`
- `holonight-qt`: `79ab555a886af469c9d5fd91b455c1e391008e0c`
