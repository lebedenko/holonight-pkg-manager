# Crisp separator joins — holonight-pkg-manager

Status: Accepted for implementation, 2026-09-21. Work package CS-005.
Baseline: `32f989f2949092886c26cea4459945c875746089`. The approved user plan authorizes this scope.

- REQ-001: Adopt HnSeparator integer physical thickness, inherited opacity, borderPassive default,
  and Leading/Center/Trailing boundary alignment without caller DPR calculations.
- REQ-002: Bottom/right boundaries shall be trailing aligned; top/left boundaries leading aligned.
  Preserve unrelated worktree edits, application architecture and existing style overrides.
- REQ-003: Verify imports and relevant application regressions against explicitly staged modified Qt.
