#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Layering and safety policy for the online update check (REQ-C-001..C-010).

Usage: check-layering.py [--root DIR] [--base GIT_REF]

Scans the sources under DIR (default: the repository) and prints one line per violation:

    RULE path:line: offending text

Exits 1 when anything is found. With --base, additionally checks (against that git ref) that nothing changed under
qml/packages and qml/explore, and that the pre-existing D-Bus `Updates` interface block is byte-identical.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

CARVE_OUT = "tests may sync only fixture repositories under a temp dir via file://"
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
         exclude=("apps/packaged/main.cpp", "apps/packages/app/PackagesApplication.cpp")),
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
         exclude=("tests/policy",)),
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
    Rule("F-007", "the installed-versus-available comparison exists once",
         rx(r"\balpm_pkg_vercmp\b"),
         include=("src", "apps"),
         exclude=("src/backends/src/pending_update_computation.cpp",)),
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
    return found


def check_carve_out(root: Path) -> list[str]:
    problems: list[str] = []
    for name, required in (("AGENTS.md", True), ("CLAUDE.md", False)):
        path = root / name
        if not path.exists():
            # CLAUDE.md is untracked in this repository, so a clean checkout legitimately lacks it.
            if required:
                problems.append(f"C-006 {name}: file is missing")
            continue
        count = sum(1 for line in path.read_text(encoding="utf-8").splitlines() if CARVE_OUT in line)
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
    changed = git(root, "diff", "--name-only", base, "--", "qml/packages", "qml/explore").stdout.split()
    problems.extend(f"C-007 {path}: qml/packages and qml/explore must not change" for path in changed)
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
    parser.add_argument("--base", help="git ref to compare qml/packages, qml/explore and the Updates D-Bus block with")
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
