#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The artifact checker must pass on the real files and fail on each single mutation (REQ-C-005, REQ-C-015)."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "scripts/check-helper-artifacts.py"
FILES = [
    "apps/helper/HelperIdentity.h",
    "apps/helper/dbus/org.holonight.PackageHelper1.xml",
    "apps/helper/data/org.holonight.packages.policy",
    "apps/helper/data/org.holonight.PackageHelper1.conf",
    "apps/helper/data/org.holonight.PackageHelper1.service.in",
    "apps/helper/data/holonight-package-helper.service.in",
    "src/application/include/holonight_packages_application/plan_service.h",
]
UNIT = "apps/helper/data/holonight-package-helper.service.in"
POLICY = "apps/helper/data/org.holonight.packages.policy"
CONF = "apps/helper/data/org.holonight.PackageHelper1.conf"
ACTIVATION = "apps/helper/data/org.holonight.PackageHelper1.service.in"
IDENTITY = "apps/helper/HelperIdentity.h"
XML = "apps/helper/dbus/org.holonight.PackageHelper1.xml"
SERVICE = "src/application/include/holonight_packages_application/plan_service.h"

# name -> (expected rule or None, file, old text, new text)
CASES = {
    "pristine": (None, None, "", ""),
    # REQ-C-005: one string changed in one file.
    "bus-name-in-bus-policy": ("A-002", CONF, 'own="org.holonight.PackageHelper1"', 'own="org.holonight.PackageHelper2"'),
    "bus-name-in-unit": ("A-004", UNIT, "BusName=org.holonight.PackageHelper1", "BusName=org.holonight.Other"),
    "bus-name-in-activation": ("A-003", ACTIVATION, "Name=org.holonight.PackageHelper1", "Name=org.holonight.Other"),
    "unit-name-in-activation": ("A-003", ACTIVATION, "SystemdService=holonight-package-helper.service", "SystemdService=other.service"),
    "interface-in-xml": ("A-001", XML, 'interface name="org.holonight.PackageHelper1.Transactions"', 'interface name="org.holonight.PackageHelper1.Other"'),
    "install-action-in-policy": ("A-005", POLICY, 'id="org.holonight.packages.install"', 'id="org.holonight.packages.setup"'),
    "remove-action-in-service": ("A-006", SERVICE, '"org.holonight.packages.remove"', '"org.holonight.packages.delete"'),
    "install-action-in-identity": ("A-006", IDENTITY, '"org.holonight.packages.install"', '"org.holonight.packages.add"'),
    # REQ-F-063 and REQ-F-064: the authorization defaults.
    "remove-cached": ("A-005", POLICY, "<allow_active>auth_admin</allow_active>", "<allow_active>auth_admin_keep</allow_active>"),
    "install-not-cached": ("A-005", POLICY, "<allow_any>auth_admin_keep</allow_any>", "<allow_any>auth_admin</allow_any>"),
    "install-open-to-all": ("A-005", POLICY, "<allow_inactive>auth_admin_keep</allow_inactive>", "<allow_inactive>yes</allow_inactive>"),
    # REQ-C-006: anyone calls, only root owns.
    "anyone-may-own": ("A-002", CONF, '<policy user="root">', '<policy context="default">'),
    "nobody-may-call": ("A-002", CONF, '<allow send_destination="org.holonight.PackageHelper1"/>', ""),
    "bus-policy-denies": ("A-002", CONF, "</busconfig>", '<policy context="default"><deny send_destination="org.holonight.PackageHelper1"/></policy></busconfig>'),
    # REQ-C-015: the unit.
    "no-new-privileges-removed": ("A-004", UNIT, "NoNewPrivileges=yes\n", ""),
    "protect-system-added": ("A-004", UNIT, "ProtectControlGroups=yes", "ProtectControlGroups=yes\nProtectSystem=strict"),
    "private-tmp-added": ("A-004", UNIT, "ProtectControlGroups=yes", "ProtectControlGroups=yes\nPrivateTmp=yes"),
    "kill-mode-changed": ("A-004", UNIT, "KillMode=mixed", "KillMode=control-group"),
    "address-families-widened": ("A-004", UNIT, "AF_UNIX AF_INET AF_INET6 AF_NETLINK", "AF_UNIX AF_INET AF_INET6 AF_NETLINK AF_PACKET"),
    # REQ-F-017 and REQ-F-018: nothing in the interface takes a path.
    "execute-with-a-second-argument": ("A-001", XML, '<arg direction="out" name="result"', '<arg direction="in" name="extra" type="s"/>\n      <arg direction="out" name="result"'),
    # system-upgrade: the upgrade action, the method list and the state directory.
    "upgrade-action-removed": ("A-005", POLICY, 'id="org.holonight.packages.upgrade"', 'id="org.holonight.packages.other"'),
    "upgrade-not-cached": ("A-005", POLICY, "<allow_active>auth_admin_keep</allow_active>\n    </defaults>\n  </action>\n\n  <!-- Removing", "<allow_active>auth_admin</allow_active>\n    </defaults>\n  </action>\n\n  <!-- Removing"),
    "upgrade-action-in-service": ("A-006", SERVICE, '"org.holonight.packages.upgrade"', '"org.holonight.packages.syu"'),
    "refresh-method-added": ("A-001", XML, '<method name="Cancel">', '<method name="Refresh"/>\n    <method name="Cancel">'),
    "prepare-upgrade-takes-a-server": ("A-001", XML, '<arg direction="in" name="options" type="a{sv}"/>\n      <arg direction="out" name="status" type="(sasa(ssas))"/>\n      <arg direction="out" name="plan_id" type="s"/>\n      <arg direction="out" name="preview" type="(a(', '<arg direction="in" name="options" type="a{sv}"/>\n      <arg direction="in" name="mirror" type="s"/>\n      <arg direction="out" name="status" type="(sasa(ssas))"/>\n      <arg direction="out" name="plan_id" type="s"/>\n      <arg direction="out" name="preview" type="(a('),
    "state-directory-removed": ("A-004", UNIT, "StateDirectory=holonight-package-helper\n", ""),
    "state-directory-listable": ("A-004", UNIT, "StateDirectoryMode=0711", "StateDirectoryMode=0755"),
    "read-write-paths-added": ("A-004", UNIT, "StateDirectoryMode=0711", "StateDirectoryMode=0711\nReadWritePaths=/var/lib/pacman"),
    "argument-named-path": ("A-001", XML, 'name="plan_id" type="s"/>\n      <arg direction="out" name="result"', 'name="pacman_conf_path" type="s"/>\n      <arg direction="out" name="result"'),
}


def run(case: str, rule: str | None, relative: str | None, old: str, new: str) -> str | None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name in FILES:
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(ROOT / name, target)
        if relative is not None:
            path = root / relative
            text = path.read_text(encoding="utf-8")
            if old not in text:
                return f"{case}: the mutation target {old!r} is not in {relative}"
            path.write_text(text.replace(old, new, 1), encoding="utf-8")
        result = subprocess.run([sys.executable, str(CHECKER), "--root", str(root)], capture_output=True, text=True)
    output = result.stdout + result.stderr
    if rule is None:
        return None if result.returncode == 0 else f"{case}: expected a clean run, got:\n{output}"
    if result.returncode == 0:
        return f"{case}: expected {rule} to fire, but the run was clean"
    if not any(line.startswith(rule + " ") for line in result.stdout.splitlines()):
        return f"{case}: expected {rule}, got:\n{output}"
    return None


def main() -> int:
    failures = [message for case, spec in CASES.items() if (message := run(case, *spec))]
    for message in failures:
        print(message, file=sys.stderr)
    if failures:
        return 1
    print(f"Helper artifact fixtures passed ({len(CASES)} cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
