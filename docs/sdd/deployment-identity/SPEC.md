# Deployment identity

A current GUI can call an older installed privileged helper. The umbrella owns
build, staging and deployment; this component owns executable and D-Bus identity.

- REQ-F-001: When `--build-info` is requested, each executable shall print its project version and source identity before initializing production services.
- REQ-F-002: While the helper is running, its transaction interface shall expose a read-only string `BuildId` property.
- REQ-F-003: When `holonight-packages --diagnostics` is requested, the client shall compare its identity with the running helper without activating the helper or changing packages.
- REQ-F-004: If the helper is unavailable, lacks identity, or differs, then diagnostics shall return a nonzero status with an explanation.

Identity is the full Git revision with a dirty-content fingerprint when tracked
changes or untracked source files exist. Unversioned archives report `unknown`.
A build-time generator updates a shared header only when identity changes; commits
and edits are detected by incremental builds without manual CMake reconfiguration.
The additive D-Bus property leaves transaction protocol version 2 unchanged.
Diagnostics are a headless command, usable without a graphical session.

Acceptance: all three binaries report the same identity; clean/dirty/commit changes
refresh it; repeat builds retain header timestamps; D-Bus returns the compiled
identity; inactive helper diagnostics do not activate it. Installer contracts and
cross-project integration are documented in the umbrella deployment identity note.
