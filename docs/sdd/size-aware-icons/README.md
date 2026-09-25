# Explicit shared icon rendering in Packages

Baseline: `fd701c648d529234298ce4c2ed39747ecfc654fd`.

Package view glyphs are bundled QRC assets. Select semantic rendering explicitly and verify QML lint and focused package presentation tests against the accepted `holonight-qt` revision.

Implementation: three QML callers with bundled assets. Local verification (2026-09-25): `task qml-lint` and `task test` passed (259 tests) against the local provider build.
