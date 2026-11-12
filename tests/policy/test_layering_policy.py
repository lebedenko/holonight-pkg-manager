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
CARVE_OUT = "tests may run package transactions only against a fixture root under a temporary directory, with file:// fixture repositories, and never automate pointer or focus input"

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
    "legit-helper-composition-names-dbpath": (None, {
        "apps/helper/HelperConfig.cpp": 'config.transaction.databasePath = parser.value("dbpath");\n',
        "apps/helper/HelperTerminator.cpp": "auto lock = databasePath / \"db.lck\";\n",
    }),
    "c001-dbpath-in-helper-service": ("C-001", {"apps/helper/HelperService.cpp": "std::string databasePath;\n"}),
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
    # T-001: the helper's bus names belong to the GUI adapter and its wire decoding only.
    "legit-adapter-names-helper": (None, {
        "apps/packages/app/DBusTransactionService.cpp": 'constexpr const char* kName = "org.holonight.PackageHelper1";\n',
        "apps/packages/app/HelperWire.cpp": "// org.holonight.PackageHelper1 wire types\n",
    }),
    "t001-helper-name-in-controller": ("T-001", {
        "apps/packages/app/TransactionController.cpp": 'auto name = "org.holonight.PackageHelper1";\n'}),
    "t001-helper-name-in-qml": ("T-001", {"qml/transactions/Leak.qml": 'Item { property string n: "org.holonight.PackageHelper1" }\n'}),
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
    "legit-transaction-library-uses-trans": (None, {
        "src/transaction/src/alpm_transaction_engine.cpp": "int r = alpm_trans_commit(h, &d);\n",
        "src/transaction/src/version_policy.cpp": "int x = alpm_pkg_vercmp(a, b);\n",
    }),
    # C-006: carve-out line and fixture-only servers.
    "c006-missing-carve-out": ("C-006", {"AGENTS.md": "# Rules\n"}),
    "c006-duplicated-carve-out": ("C-006", {"AGENTS.md": f"{CARVE_OUT}\n{CARVE_OUT}\n"}),
    "c006-stale-carve-out-wording": ("C-006", {"AGENTS.md": f"{CARVE_OUT}\nTests never run package transactions.\n"}),
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
    # H rules: helper and transaction library.
    "legit-helper-clean": (None, {
        "apps/helper/HelperService.cpp": "#include <QDBusConnection>\nvoid f() {}\n",
        "apps/helper/HelperConfig.cpp": "TransactionConfig c{root, db, conf, cache};\n",
        "src/transaction/src/alpm_transaction_engine.cpp": "#include <alpm.h>\nint r = alpm_trans_prepare(h, &d);\n",
        "src/transaction/include/holonight_packages_transaction/transaction_seams.h": "using Fetch = int (*)(); // alpm_option_set_fetchcb\n",
        "src/transaction/src/fetch_seam.h": "alpm_option_set_fetchcb(handle, cb, ctx);\n",
        "tests/transaction/file_server_test.cpp": 'conf = "Server = file:///tmp/fixture\\n";\n',
    }),
    "h001-dbus-in-application": ("H-001", {"src/application/src/leak.cpp": "#include <QDBusMessage>\n"}),
    "h001-polkit-in-domain": ("H-001", {"src/domain/src/leak.cpp": "const char* a = \"org.freedesktop.PolicyKit1\"; // polkit\n"}),
    "h002-qt-include-in-transaction": ("H-002", {"src/transaction/src/x.cpp": "#include <QString>\n"}),
    "h002-dbus-in-transaction": ("H-002", {"src/transaction/src/x.cpp": "void f(QDBusMessage m);\n"}),
    "h003-system-in-helper": ("H-003", {"apps/helper/HelperService.cpp": 'system("pacman");\n'}),
    "h003-fork-in-transaction": ("H-003", {"src/transaction/src/x.cpp": "pid_t p = fork();\n"}),
    "h004-db-update": ("H-004", {"src/transaction/src/x.cpp": "alpm_db_update(h, db, 0);\n"}),
    "h004-fetchcb-in-helper": ("H-004", {"apps/helper/HelperService.cpp": "alpm_option_set_fetchcb(h, cb, 0);\n"}),
    "h004-db-update-in-helper": ("H-004", {"apps/helper/HelperService.cpp": "alpm_db_update(h, db, 0);\n"}),
    "h004-fetch-pkgurl-in-sync-refresher": ("H-004", {
        "src/transaction/src/sync_refresher.cpp": "alpm_fetch_pkgurl(h, urls, &out);\n"}),
    "h004-fetchcb-in-sync-refresher": ("H-004", {
        "src/transaction/src/sync_refresher.cpp": "alpm_option_set_fetchcb(h, cb, 0);\n"}),
    "legit-db-update-in-sync-refresher": (None, {
        "src/transaction/src/sync_refresher.cpp": "const int r = alpm_db_update(h, dbs, 0);\n"}),
    "h005-compare-versions": ("H-005", {"src/transaction/src/x.cpp": "int compareVersions(const char*, const char*);\n"}),
    "h005-vercmp-in-domain": ("H-005", {"src/domain/src/x.cpp": "int vercmp(a, b);\n"}),
    "legit-prepare-upgrade-method": (None, {"apps/helper/dbus/x.xml": '<method name="PrepareUpgrade"/>\n'}),
    "h006-refresh-method": ("H-006", {"apps/helper/dbus/x.xml": '<method name="RefreshDatabases"/>\n'}),
    "h006-sync-method": ("H-006", {"apps/helper/dbus/x.xml": '<method name="SyncDatabases"/>\n'}),
    "h006-rescan-method": ("H-006", {"apps/helper/dbus/x.xml": '<method name="RescanDatabases"/>\n'}),
    "h006-answer-method": ("H-006", {"apps/helper/dbus/x.xml": '<method name="AnswerQuestion"/>\n'}),
    "h006-packagekit": ("H-006", {"apps/helper/CMakeLists.txt": "find_package(PackageKit)\n"}),
    "h013-promotion-outside-driver": ("H-013", {
        "src/transaction/src/alpm_transaction_engine.cpp": "SyncPromotion promotion(live, scratch);\n"}),
    "h013-promotion-in-helper": ("H-013", {"apps/helper/HelperService.cpp": "promoteSyncDatabases(a, b);\n"}),
    "legit-promotion-in-driver": (None, {
        "src/transaction/src/promotion.h": "class SyncPromotion {};\n",
        "src/transaction/src/promotion.cpp": "SyncPromotion::SyncPromotion() {}\n",
        "src/transaction/src/upgrade_driver.cpp": "SyncPromotion promotion(live, scratch);\n",
    }),
    "h007-path-argument": ("H-007", {"apps/helper/dbus/x.xml": '<arg name="pacmanConfPath" type="s"/>\n'}),
    "h007-config-from-adaptor": ("H-007", {"apps/helper/HelperAdaptor.cpp": "TransactionConfig config{a, b, c, d};\n"}),
    "h008-seams-in-main": ("H-008", {"apps/helper/main.cpp": "TransactionSeams seams;\n"}),
    "h008-fault-injector-in-config": ("H-008", {"apps/helper/HelperConfig.cpp": "FaultInjector injector;\n"}),
    "h009-siglevel-parser": ("H-009", {"src/transaction/src/x.cpp": 'if (word == "Required") {}\n'}),
    "h011-private-key-block": ("H-011", {"tests/transaction/key.cpp": 'const char* k = "-----BEGIN PGP PRIVATE KEY BLOCK-----";\n'}),
    "h011-keyring-file": ("H-011", {"tests/fixtures/pubring.kbx": "x"}),
    "h011-gnupg-dir": ("H-011", {"tests/fixtures/private-keys-v1.d/key.key": "x"}),
    "h012-https-server-in-helper-tests": ("H-012", {"tests/helper/x_test.cpp": 'conf = "Server = https://mirror.example/x";\n'}),
    "h012-http-server-in-transaction-tests": ("H-012", {"tests/transaction/x_test.cpp": 'conf = "Server = http://127.0.0.1:1/x";\n'}),
    "h010-input-automation-in-helper-tests": ("C-010", {"tests/helper/x_test.cpp": "QTest::mouseClick(&w, Qt::LeftButton);\n"}),
    # system-upgrade: the GUI's helper calls and its absent actions.
    "u001-unknown-helper-method": ("U-001", {
        "apps/packages/app/DBusTransactionService.cpp": 'QDBusMessage m = call(QStringLiteral("RefreshDatabases"));\n'}),
    "legit-four-helper-methods": (None, {
        "apps/packages/app/DBusTransactionService.cpp": 'call(QStringLiteral("PrepareUpgrade"));\ncall(QStringLiteral("Cancel"));\n'}),
    "u002-per-package-upgrade": ("U-002", {"apps/packages/app/TransactionController.h": "Q_INVOKABLE void upgradePackage();\n"}),
    "u002-standalone-refresh-in-qml": ("U-002", {"qml/updates/UpdatesView.qml": "onClicked: controller.refreshDatabases()\n"}),
    "u003-focus-automation": ("U-003", {"tests/transactions/x_test.cpp": "item->forceActiveFocus();\n"}),
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
