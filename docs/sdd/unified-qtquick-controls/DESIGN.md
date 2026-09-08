# UQC-105 design

[Requirements](SPEC.md) are authoritative for this package.

## Runtime controls and discovery

Replace Basic and direct H imports in the sixteen application QML files with file-local `Controls` imports where needed. Keep explicit Core/composite imports and application-owned delegates/painting. Preserve all layouts and independent scroll ownership; selected styles supply their control implementations.

Embed the default through qt_add_resources. Configure a small private startup header with the exact build executable, selected provider QML root and install libdir. PackagesApplication compares its canonical executable path with the configured build executable before adding build discovery; it always supports executable-relative installed discovery. No new application API or imperative style selection.

Resolve the provider QML root from HolonightQt_DIR using NO_DEFAULT_PATH. Reuse it for tests and qmllint. Taskfile validates exact sibling dependency revisions and builds configuration/provider privately. Both CI jobs use identical pinned Release dependency staging with Wayland and without examples/tests. Static analysis follows the complete build and supports a configurable worker limit.

## Verification architecture

A separate tests/runtime executable embeds exactly the production QML/assets and registers the same real models. Its QML output directory is separate from the source-based test target. Reuse source view tests through an explicit compiled-QML fixture mode, avoiding duplicated behavior coverage, and add focused runtime checks for implementation/plugin origins, deterministic overflow and large-list scrolling.

Use MockPackageSource only at the enumeration boundary. Hold asynchronous loading with explicit synchronization where additional loading tests are needed; use Qt event-condition waits. Access ComboBox, scrolling, selection and dependency expansion through properties/signals. Never move/click the desktop pointer or automate window focus.

Production launch acceptance uses sanitized environments, disposable HOME/XDG state and an unavailable desktop bus. Read-only ALPM enumeration is the sole allowed production backend interaction. Trace resolved QML URLs/plugins, require survival for the bounded observation, and terminate/reap in finally blocks. Staged probes reject application-build discovery and use staged native libraries.

Policy and negative fixtures run independently of QML loading. README/local instructions explain runtime imports, overrides, exact dependency staging and verification. Production domain/backend behavior remains unchanged.
