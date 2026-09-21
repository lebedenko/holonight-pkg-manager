# Tasks

- [x] Audit current consumers and preserve unrelated edits.
- [x] Apply the accepted separator contract where needed.
- [x] Build and run relevant regressions against the explicitly staged modified provider.
- [x] Record local verification and remaining integration boundary.

Verified 2026-09-21 against the uncommitted provider working tree, staged from
`holonight-files/build/deps/holonight-qt` into this repository's dependency prefix.

Package detail and table headers use trailing bottom boundaries. QML lint now resolves the explicit
provider prefix before standard Qt imports using --bare; no system-installed older API is substituted.

Full build and 98 CTest cases passed, including Holonight/Fusion runtime controls. QML lint passes.
The final installed-package view/runtime-control subset passes 15/15 against the final staged provider.

Native connected-window acceptance belongs to Files and has passed. The user subsequently authorized
publication and pin updates; the umbrella ledger records published revisions and the CI snapshot.
