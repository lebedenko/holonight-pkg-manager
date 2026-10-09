#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Each policy rule must fire on its own, and legitimate code must stay clean.

Fixtures are generated into a temporary tree instead of being checked in, so formatters and linters that glob the
source directories never see the deliberate violations.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "scripts/check-layering.py"
CARVE_OUT = "tests may sync only fixture repositories under a temp dir via file://"

BASELINE = {
    "AGENTS.md": f"# Rules\n{CARVE_OUT}.\n",
    "CLAUDE.md": f"# Notes\n{CARVE_OUT}.\n",
}

# name -> (expected rule id or None, files overlaid on the baseline)
CASES = {
    # Legitimate code that must pass.
    "clean-baseline": (None, {}),
    "legit-backend-uses-alpm": (None, {
        "src/backends/src/alpm_update_checker.cpp": '#include <alpm.h>\nvoid f() { alpm_initialize(nullptr, nullptr, nullptr); }\n',
        "src/backends/src/pending_update_computation.cpp": "int x = alpm_pkg_vercmp(a, b);\n",
    }),
    "legit-composition-roots-name-dbpath": (None, {
        "apps/packaged/main.cpp": 'auto dbpath = parser.value("dbpath");\n',
        "apps/packages/app/PackagesApplication.cpp": ".databasePath = kDefault,\n",
    }),
    "legit-gui-client-names": (None, {
        "apps/packages/app/UpdateCheckClient.h": "class UpdateCheckClient {};\nclass UpdateCheckModel {};\n",
    }),
    "legit-file-server-in-test": (None, {
        "tests/backends/ok_test.cpp": 'const char* conf = "Server = file:///tmp/mirror\\n";\n',
    }),
    "legit-legacy-key-clicks": (None, {
        "tests/apps/explore_view_test.cpp": "QTest::keyClick(&w, Qt::Key_Down);\nQTest::mouseClick(&w, Qt::LeftButton);\n",
    }),
    "legit-missing-claude-md": (None, {"CLAUDE.md": None}),
    # C-001: libalpm and dbpath names outside the backend and composition roots.
    "c001-alpm-header-in-application": ("C-001", {"src/application/src/leak.cpp": "#include <alpm.h>\n"}),
    "c001-alpm-call-in-domain": ("C-001", {"src/domain/src/leak.cpp": "void f() { alpm_release(h); }\n"}),
    "c001-dbpath-in-gui": ("C-001", {"apps/packages/app/Leak.cpp": 'QString dbpath;\n'}),
    "c001-database-path-in-packaged-service": ("C-001", {"apps/packaged/Leak.cpp": "std::string databasePath;\n"}),
    "c001-alpm-in-qml": ("C-001", {"qml/updates/Leak.qml": "Item { property var h: alpm_db_get_name }\n"}),
    "c001-alpm-in-snapshot-store": ("C-001", {
        "src/persistence/src/json_update_snapshot_store.cpp": "#include <alpm.h>\n"}),
    # C-002: no external process in the checker.
    "c002-qprocess": ("C-002", {"src/backends/src/alpm_update_checker.cpp": "QProcess process;\n"}),
    "c002-system": ("C-002", {"src/backends/src/scratch_dir.cpp": 'int r = system("ls");\n'}),
    "c002-popen": ("C-002", {"src/backends/src/pacman_repositories.cpp": 'FILE* f = popen("pacman-conf", "r");\n'}),
    "c002-execv": ("C-002", {"src/backends/src/alpm_error_mapping.cpp": "execvp(path, argv);\n"}),
    "c002-fork": ("C-002", {"src/application/src/update_check_service.cpp": "pid_t p = fork();\n"}),
    "c002-posix-spawn": ("C-002", {"src/backends/src/pending_update_computation.cpp": "posix_spawn(&pid, p, 0, 0, a, e);\n"}),
    # C-005: no transactions.
    "c005-trans": ("C-005", {"src/backends/src/other.cpp": "alpm_trans_init(handle, 0);\n"}),
    "c005-pacman-command": ("C-005", {"tests/backends/cmd_test.cpp": 'run("pacman -Syu");\n'}),
    # C-006: carve-out line and fixture-only servers.
    "c006-missing-carve-out": ("C-006", {"AGENTS.md": "# Rules\n"}),
    "c006-duplicated-carve-out": ("C-006", {"AGENTS.md": f"{CARVE_OUT}\n{CARVE_OUT}\n"}),
    "c006-missing-carve-out-in-claude-md": ("C-006", {"CLAUDE.md": "# Notes\n"}),
    "c006-remote-server-in-tests": ("C-006", {"tests/backends/remote_test.cpp": 'conf = "Server = https://mirror.example/$repo/os/$arch";\n'}),
    "c006-ftp-server-in-test-conf": ("C-006", {"tests/fixtures/pacman.conf": "[core]\nServer = ftp://mirror.example/core\n"}),
    # C-007: no notifications, no NetworkManager.
    "c007-notifications": ("C-007", {"apps/packages/app/Notify.cpp": 'auto n = "org.freedesktop.Notifications";\n'}),
    "c007-network-manager": ("C-007", {"apps/packaged/Net.cpp": 'auto n = "org.freedesktop.NetworkManager";\n'}),
    # C-009: only holonight-packaged checks.
    "c009-checker-in-gui": ("C-009", {"apps/packages/app/X.cpp": "AlpmUpdateChecker checker;\n"}),
    "c009-service-in-gui": ("C-009", {"apps/packages/app/X.h": "UpdateCheckService* service;\n"}),
    "c009-scheduler-in-gui": ("C-009", {"apps/packages/app/X.cpp": "UpdateCheckScheduler scheduler;\n"}),
    "c009-port-in-gui": ("C-009", {"apps/packages/app/X.cpp": "std::shared_ptr<UpdateChecker> c;\n"}),
    # C-010: no input automation in new tests.
    "c010-mouse": ("C-010", {"tests/apps/new_view_test.cpp": "QTest::mouseClick(&w, Qt::LeftButton);\n"}),
    "c010-key": ("C-010", {"tests/apps/new_view_test.cpp": "QTest::keyClick(&w, Qt::Key_A);\n"}),
    "c010-touch": ("C-010", {"tests/apps/new_view_test.cpp": "QTest::touchEvent(&w, d);\n"}),
    # One comparison loop.
    "f007-second-vercmp": ("F-007", {"src/backends/src/other.cpp": "if (alpm_pkg_vercmp(a, b) > 0) {}\n"}),
}


def run(case: str, expected: str | None, overlay: dict[str, str | None]) -> str | None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        files = dict(BASELINE)
        files.update(overlay)
        for relative, content in files.items():
            if content is None:
                continue
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        result = subprocess.run([sys.executable, str(CHECKER), "--root", str(root)], capture_output=True, text=True)
    output = result.stdout + result.stderr
    if expected is None:
        return None if result.returncode == 0 else f"{case}: expected a clean run, got:\n{output}"
    if result.returncode == 0:
        return f"{case}: expected {expected} to fire, but the run was clean"
    if not any(line.startswith(expected + " ") for line in result.stdout.splitlines()):
        return f"{case}: expected {expected}, got:\n{output}"
    return None


def main() -> int:
    failures = [message for case, (expected, overlay) in CASES.items() if (message := run(case, expected, overlay))]
    for message in failures:
        print(message, file=sys.stderr)
    if failures:
        return 1
    print(f"Layering policy fixtures passed ({len(CASES)} cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
