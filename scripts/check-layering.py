#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Layering and safety policy for the online update check (REQ-C-001..C-010).

Usage: check-layering.py [--root DIR] [--base GIT_REF]

Scans the sources under DIR (default: the repository) and prints one line per violation:

    RULE path:line: offending text

Exits 1 when anything is found. With --base, additionally checks (against that git ref) that the pre-existing D-Bus
`Updates` interface block is byte-identical.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

CARVE_OUT = "tests may run package transactions only against a fixture root under a temporary directory, with file:// fixture repositories, and never automate pointer or focus input"
STALE_CARVE_OUT = "never run package transactions"
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".qml", ".py", ".sh"}

# Existing tests that predate the "no pointer/focus automation" rule. The rule guards every other test file.
LEGACY_INPUT_AUTOMATION = {
    "tests/apps/explore_view_test.cpp",
    "tests/apps/installed_packages_view_test.cpp",
}


@dataclass
class Rule:
    rule_id: str
    description: str
    pattern: re.Pattern[str]
    include: tuple[str, ...]  # path prefixes (relative to root) or exact files
    exclude: tuple[str, ...] = field(default_factory=tuple)
    # Only files whose name starts with one of these prefixes (empty: all files under `include`).
    name_prefixes: tuple[str, ...] = field(default_factory=tuple)
    suffixes: tuple[str, ...] = (".h", ".hpp", ".cpp", ".cc", ".qml")


def rx(text: str) -> re.Pattern[str]:
    return re.compile(text)


ALPM_NAMES = rx(r"<alpm\.h>|\balpm_\w+|\bdbpath\b|\bdatabasePath\b|\bdatabase_path\b")
PROCESS_SPAWN = rx(r"\bQProcess\b|\bsystem\s*\(|\bpopen\s*\(|\bexec(?:l|le|lp|v|ve|vp|vpe)\s*\(|\bfork\s*\(|\bposix_spawn")

RULES: list[Rule] = [
    Rule("C-001", "libalpm and dbpath names belong in src/backends and the two composition roots only",
         ALPM_NAMES,
         include=("src/domain", "src/application", "apps", "qml"),
         exclude=("apps/packaged/main.cpp", "apps/packages/app/PackagesApplication.cpp",
                  # The helper's command line and its terminator name the database path (the lock file lives there).
                  "apps/helper/main.cpp", "apps/helper/HelperConfig.cpp", "apps/helper/HelperConfig.h",
                  "apps/helper/HelperTerminator.cpp", "apps/helper/HelperTerminator.h")),
    Rule("C-001", "the snapshot store and cache locations are libalpm-free",
         ALPM_NAMES,
         include=("src/persistence",),
         name_prefixes=("json_update_snapshot_store", "cache_locations")),
    Rule("C-002", "no external process in the checker sources",
         PROCESS_SPAWN,
         include=("src/backends", "src/application"),
         name_prefixes=("alpm_update_checker", "scratch_dir", "pacman_repositories", "alpm_error_mapping",
                        "pending_update_computation", "update_check_")),
    Rule("C-005", "no package transaction API or package-manager commands",
         rx(r"\balpm_trans_\w*|\bpacman\s+-[SUR]\w*"),
         include=("src", "apps", "qml", "tests"),
         exclude=("tests/policy", "src/transaction")),
    Rule("C-007", "no notification or network-state integration",
         rx(r"org\.freedesktop\.Notifications|NetworkManager"),
         include=("src", "apps", "qml"),
         suffixes=(".h", ".hpp", ".cpp", ".cc", ".qml", ".xml")),
    Rule("C-009", "the GUI process neither checks nor schedules: it only calls CheckNow over D-Bus",
         rx(r"\b(?:UpdateChecker|UpdateCheckService|UpdateCheckScheduler|AlpmUpdateChecker)\b"),
         include=("apps/packages",)),
    Rule("C-010", "tests must not automate pointer or touch input",
         rx(r"QTest::(?:mouse\w*|touch\w*)\b"),
         include=("tests",), exclude=("tests/policy",)),
    Rule("C-010", "tests must not automate keyboard input (new files)",
         rx(r"QTest::(?:keyClick|keyClicks|keyPress|keyRelease|sendKeyEvent)\b"),
         include=("tests",), exclude=("tests/policy",)),
    Rule("C-006", "tests may only point repositories at file:// fixtures",
         rx(r"Server\s*=\s*(?!file://)[A-Za-z][A-Za-z0-9+.-]*://"),
         include=("tests",), exclude=("tests/policy",),
         suffixes=(".h", ".hpp", ".cpp", ".cc", ".conf", ".in", ".sh", ".py", ".txt", ".md", ""))
    ,
    Rule("H-001", "domain and application stay free of D-Bus and polkit",
         rx(r"QDBus|<QtDBus|(?i:polkit)"),
         include=("src/domain", "src/application")),
    Rule("H-002", "the transaction library is Qt-free, D-Bus-free and polkit-free",
         rx(r"#\s*include\s*[<\"]Q|QDBus|(?i:polkit)"),
         include=("src/transaction",)),
    Rule("H-003", "no external process in the helper and the transaction library",
         PROCESS_SPAWN,
         include=("apps/helper", "src/transaction")),
    Rule("H-004", "the helper never fetches files by URL",
         rx(r"\balpm_(?:syncdb|fetch_pkgurl)\b"),
         include=("apps/helper", "src/transaction")),
    Rule("H-004", "the fetch callback is installed by the test seam only",
         rx(r"\balpm_option_set_fetchcb\b"),
         include=("apps/helper", "src/transaction"),
         exclude=("src/transaction/include/holonight_packages_transaction/transaction_seams.h",
                  "src/transaction/src/fetch_seam.h")),
    Rule("H-004", "only the system upgrade's sync refreshes databases, into its scratch root (system-upgrade REQ-F-071)",
         rx(r"\balpm_db_update\b"),
         include=("apps/helper", "src/transaction"),
         exclude=("src/transaction/src/sync_refresher.cpp",)),
    Rule("H-005", "no second version comparison in the helper or the domain",
         rx(r"\b(?:compareVersions|versionCompare|vercmp)\b"),
         include=("apps/helper", "src/transaction", "src/domain")),
    Rule("H-006", "the helper exposes no answer, history or standalone refresh method and no AUR/PackageKit/-U",
         rx(r"<method\s+name=\"\w*(?:Answer|History|Refresh|Rescan|Sync)|(?i:packagekit)|\baur\b|\"-U\""),
         include=("apps/helper",),
         suffixes=(".h", ".hpp", ".cpp", ".cc", ".xml", ".in", ".txt")),
    Rule("H-013", "the live sync databases are replaced only by the upgrade driver's promotion (REQ-F-071)",
         rx(r"\b(?:SyncPromotion|promoteSyncDatabases)\b"),
         include=("src", "apps"),
         exclude=("src/transaction/src/promotion.h", "src/transaction/src/promotion.cpp",
                  "src/transaction/src/upgrade_driver.cpp")),
    Rule("H-007", "no caller-supplied path, repository, server or config argument",
         rx(r"(?i)<arg[^>]*name=\"\w*(?:path|repo|server|config)\w*\""),
         include=("apps/helper",),
         suffixes=(".xml",)),
    Rule("H-007", "TransactionConfig is constructed only from the command line",
         rx(r"\bTransactionConfig\b\s*\w*\s*[{(]"),
         include=("apps/helper",),
         exclude=("apps/helper/HelperConfig.cpp", "apps/helper/HelperConfig.h")),
    Rule("H-008", "production code cannot reach test seams",
         rx(r"\b(?:FaultInjector|TransactionSeams)\b"),
         include=("apps/helper/main.cpp", "apps/helper/HelperConfig.cpp", "apps/helper/HelperConfig.h")),
    Rule("H-009", "SigLevel keywords are parsed in one place only",
         rx(r"\"(?:Required|TrustAll|TrustedOnly|Optional|Never)\""),
         include=("apps/helper", "src/transaction")),
    Rule("H-012", "helper and transaction tests may only use file:// repositories",
         rx(r"Server\s*=\s*(?!file://)[A-Za-z][A-Za-z0-9+.-]*://"),
         include=("tests/helper", "tests/transaction"),
         suffixes=(".h", ".hpp", ".cpp", ".cc", ".conf", ".in", ".sh", ".py", ".txt", ".md", "")),
    Rule("U-001", "the GUI calls only the helper's four methods (system-upgrade REQ-C-006)",
         rx(r"\bcall\(QStringLiteral\(\"(?!(?:PreparePlan|PrepareUpgrade|Execute|Cancel)\")"),
         include=("apps/packages",)),
    Rule("U-002", "no per-package upgrade, multi-select, standalone refresh, scheduled upgrade or downgrade action",
         rx(r"\b(?:upgradePackage|refreshDatabases|syncDatabases|scheduleUpgrade|downgrade|multiSelect|selectAll)\b"),
         include=("apps/packages", "qml", "src/application")),
    Rule("U-003", "tests drive the view-model, never the focus (system-upgrade REQ-C-004)",
         rx(r"\bforceActiveFocus\s*\(|QTest::(?:mousePress|mouseRelease|mouseMove)\b"),
         include=("tests",), exclude=("tests/policy", *sorted(LEGACY_INPUT_AUTOMATION))),
    Rule("T-001", "the helper's bus names appear only in the GUI adapter and its wire decoding",
         rx(r"org\.holonight\.PackageHelper1"),
         include=("src/domain", "src/application", "apps/packages", "qml"),
         exclude=("apps/packages/app/DBusTransactionService.h", "apps/packages/app/DBusTransactionService.cpp",
                  "apps/packages/app/HelperWire.h", "apps/packages/app/HelperWire.cpp")),
    Rule("F-007", "the installed-versus-available comparison exists once",
         rx(r"\balpm_pkg_vercmp\b"),
         include=("src", "apps"),
         exclude=("src/backends/src/pending_update_computation.cpp", "src/transaction/src/version_policy.cpp")),
]


def files_under(root: Path, prefixes: tuple[str, ...], suffixes: tuple[str, ...]):
    for prefix in prefixes:
        base = root / prefix
        if base.is_file():
            yield base
        elif base.is_dir():
            for path in sorted(base.rglob("*")):
                if path.is_file() and (path.suffix in suffixes or (path.suffix == "" and "" in suffixes)):
                    yield path


def excluded(relative: str, exclusions: tuple[str, ...]) -> bool:
    return any(relative == item or relative.startswith(item.rstrip("/") + "/") for item in exclusions)


def scan(root: Path) -> list[str]:
    found: list[str] = []
    for rule in RULES:
        for path in files_under(root, rule.include, rule.suffixes):
            relative = path.relative_to(root).as_posix()
            if excluded(relative, rule.exclude):
                continue
            if rule.rule_id == "C-010" and "keyboard" in rule.description and relative in LEGACY_INPUT_AUTOMATION:
                continue
            if rule.rule_id == "C-010" and "pointer" in rule.description and relative in LEGACY_INPUT_AUTOMATION:
                continue
            if rule.name_prefixes and not path.name.startswith(rule.name_prefixes):
                continue
            try:
                lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            except OSError:
                continue
            for number, line in enumerate(lines, 1):
                if rule.pattern.search(line):
                    found.append(f"{rule.rule_id} {relative}:{number}: {line.strip()}")
    found.extend(check_carve_out(root))
    found.extend(check_key_material(root))
    return found


KEY_FILE_SUFFIXES = {".gpg", ".kbx", ".asc"}
KEY_CONTENT = re.compile(r"BEGIN PGP PRIVATE KEY|private-keys-v1\.d")


def check_key_material(root: Path) -> list[str]:
    """H-011: tests generate their keyring at run time and commit no key material."""
    problems: list[str] = []
    base = root / "tests"
    if not base.is_dir():
        return problems
    for path in sorted(base.rglob("*")):
        if not path.is_file() or "policy" in path.relative_to(base).parts[:1]:
            continue
        relative = path.relative_to(root).as_posix()
        if path.suffix in KEY_FILE_SUFFIXES or "private-keys-v1.d" in path.parts:
            problems.append(f"H-011 {relative}:1: key material file")
            continue
        if path.suffix in SOURCE_SUFFIXES | {".txt", ".md", ".conf", ".in", ""}:
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                if KEY_CONTENT.search(line):
                    problems.append(f"H-011 {relative}:{number}: {line.strip()}")
    return problems


def check_carve_out(root: Path) -> list[str]:
    problems: list[str] = []
    for name, required in (("AGENTS.md", True), ("CLAUDE.md", False)):
        path = root / name
        if not path.exists():
            # CLAUDE.md is untracked in this repository, so a clean checkout legitimately lacks it.
            if required:
                problems.append(f"C-006 {name}: file is missing")
            continue
        text = path.read_text(encoding="utf-8")
        if STALE_CARVE_OUT in text:
            problems.append(f"C-006 {name}: still contains '{STALE_CARVE_OUT}'")
        count = sum(1 for line in text.splitlines() if CARVE_OUT in line.lower())
        if count != 1:
            problems.append(f"C-006 {name}: expected exactly one line containing '{CARVE_OUT}', found {count}")
    return problems


def git(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, check=False)


UPDATES_INTERFACE = re.compile(r'<interface name="org\.holonight\.Packages1\.Updates">.*?</interface>', re.S)


def check_against_base(root: Path, base: str) -> list[str]:
    if git(root, "rev-parse", "--verify", "--quiet", base).returncode != 0:
        print(f"check-layering: base ref '{base}' not found; skipping the base comparison", file=sys.stderr)
        return []
    problems: list[str] = []
    xml = "apps/packaged/dbus/org.holonight.Packages1.Updates.xml"
    before = git(root, "show", f"{base}:{xml}")
    if before.returncode == 0 and (root / xml).exists():
        old = UPDATES_INTERFACE.search(before.stdout)
        new = UPDATES_INTERFACE.search((root / xml).read_text(encoding="utf-8"))
        if old is None or new is None or old.group(0) != new.group(0):
            problems.append(f"F-040 {xml}: the existing Updates interface block must stay byte-identical")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--base", help="git ref to compare the Updates D-Bus block with")
    arguments = parser.parse_args()
    problems = scan(arguments.root)
    if arguments.base:
        problems.extend(check_against_base(arguments.root, arguments.base))
    for problem in problems:
        print(problem)
    if problems:
        print(f"check-layering: {len(problems)} violation(s)", file=sys.stderr)
        return 1
    print("check-layering: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
