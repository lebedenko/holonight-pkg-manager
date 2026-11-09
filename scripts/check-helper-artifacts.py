#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Consistency of the privileged helper's installable artifacts (REQ-C-005, REQ-C-006, REQ-C-015, REQ-F-063, REQ-F-064).

Usage: check-helper-artifacts.py [--root DIR]

The bus name, interface, action IDs, activation name and unit name appear in several files (the introspection XML, the
polkit policy, the system bus policy, the activation file, the systemd unit and the C++ identity constants). They must
agree, the unit must carry exactly the hardening that pacman can live with, and the bus policy must let anyone call the
helper while only root owns the name. Prints one line per violation and exits 1 when there is any.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from xml.etree import ElementTree

HELPER = Path("apps/helper")
IDENTITY = HELPER / "HelperIdentity.h"
INTERFACE_XML = HELPER / "dbus/org.holonight.PackageHelper1.xml"
POLICY = HELPER / "data/org.holonight.packages.policy"
BUS_CONF = HELPER / "data/org.holonight.PackageHelper1.conf"
ACTIVATION = HELPER / "data/org.holonight.PackageHelper1.service.in"
UNIT = HELPER / "data/holonight-package-helper.service.in"
PLAN_SERVICE = Path("src/application/include/holonight_packages_application/plan_service.h")

REQUIRED_UNIT = {
    "Type": "dbus",
    "KillMode": "mixed",
    "TimeoutStopSec": "300",
    "NoNewPrivileges": "yes",
    "LockPersonality": "yes",
    "RestrictRealtime": "yes",
    "RestrictAddressFamilies": "AF_UNIX AF_INET AF_INET6 AF_NETLINK",
    "ProtectClock": "yes",
    "ProtectHostname": "yes",
    "ProtectKernelLogs": "yes",
    "ProtectControlGroups": "yes",
}
FORBIDDEN_UNIT = (
    "ProtectSystem", "ProtectHome", "PrivateTmp", "ReadWritePaths", "RestrictSUIDSGID", "ProtectKernelTunables",
    "ProtectKernelModules", "SystemCallArchitectures", "MemoryDenyWriteExecute", "PrivateNetwork", "RestrictNamespaces",
    "CapabilityBoundingSet",
)
FORBIDDEN_ARGUMENT_WORDS = ("path", "repo", "server", "config")


def read(root: Path, relative: Path) -> str | None:
    path = root / relative
    return path.read_text(encoding="utf-8") if path.is_file() else None


def identity_constants(text: str) -> dict[str, str]:
    return dict(re.findall(r'inline constexpr std::string_view (k\w+) = "([^"]+)";', text))


def ini_pairs(text: str) -> list[tuple[str, str]]:
    pairs = []
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith(("#", ";", "[")) and "=" in line:
            key, _, value = line.partition("=")
            pairs.append((key.strip(), value.strip()))
    return pairs


def check(root: Path) -> list[str]:
    problems: list[str] = []

    def fail(rule: str, where: Path | str, message: str) -> None:
        problems.append(f"{rule} {where}: {message}")

    identity_text = read(root, IDENTITY)
    if identity_text is None:
        return [f"A-000 {IDENTITY}: missing"]
    names = identity_constants(identity_text)
    bus, interface = names.get("kBusName"), names.get("kInterfaceName")
    install_id, remove_id, unit_name = names.get("kInstallActionId"), names.get("kRemoveActionId"), names.get("kUnitName")
    for key in ("kBusName", "kObjectPath", "kInterfaceName", "kInstallActionId", "kRemoveActionId", "kUnitName"):
        if key not in names:
            fail("A-000", IDENTITY, f"constant {key} is missing")
    if not (bus and interface and install_id and remove_id and unit_name):
        return problems

    # A-001: introspection XML.
    xml_text = read(root, INTERFACE_XML)
    if xml_text is None:
        fail("A-001", INTERFACE_XML, "missing")
    else:
        try:
            tree = ElementTree.fromstring(xml_text)
        except ElementTree.ParseError as error:
            fail("A-001", INTERFACE_XML, f"not well-formed: {error}")
        else:
            interfaces = [element.get("name") for element in tree.iter("interface")]
            if interface not in interfaces:
                fail("A-001", INTERFACE_XML, f"interface {interface} not declared (found {interfaces})")
            methods = {element.get("name") for element in tree.iter("method")}
            for required in ("PreparePlan", "Execute", "Cancel"):
                if required not in methods:
                    fail("A-001", INTERFACE_XML, f"method {required} missing")
            for element in tree.iter("arg"):
                if any(word in (element.get("name") or "").lower() for word in FORBIDDEN_ARGUMENT_WORDS):
                    fail("A-001", INTERFACE_XML, f"argument {element.get('name')} names a path, repository, server or config")
            for method in tree.iter("method"):
                if method.get("name") == "Execute":
                    inputs = [arg for arg in method.iter("arg") if arg.get("direction") == "in"]
                    if len(inputs) != 1 or inputs[0].get("type") != "s":
                        fail("A-001", INTERFACE_XML, "Execute must take exactly one input of type s")

    # A-002 and A-006: system bus policy (anyone calls, only root owns).
    conf_text = read(root, BUS_CONF)
    if conf_text is None:
        fail("A-002", BUS_CONF, "missing")
    else:
        try:
            conf = ElementTree.fromstring(re.sub(r"<!DOCTYPE[^>]*>", "", conf_text, flags=re.S))
        except ElementTree.ParseError as error:
            fail("A-002", BUS_CONF, f"not well-formed: {error}")
        else:
            owners = []
            callers = []
            for policy in conf.iter("policy"):
                for rule in policy:
                    if rule.get("own"):
                        owners.append((policy.get("user"), policy.get("context"), rule.get("own")))
                    if rule.tag == "allow" and rule.get("send_destination"):
                        callers.append((policy.get("context"), rule.get("send_destination")))
            if owners != [("root", None, bus)]:
                fail("A-002", BUS_CONF, f"only root may own {bus}; found {owners}")
            if ("default", bus) not in callers:
                fail("A-002", BUS_CONF, f"the default policy must allow sending to {bus}")
            if any(rule.tag == "deny" for rule in conf.iter()):
                fail("A-002", BUS_CONF, "no deny rule is expected: polkit decides")

    # A-003: activation file.
    activation = read(root, ACTIVATION)
    if activation is None:
        fail("A-003", ACTIVATION, "missing")
    else:
        pairs = dict(ini_pairs(activation))
        if pairs.get("Name") != bus:
            fail("A-003", ACTIVATION, f"Name must be {bus}, found {pairs.get('Name')}")
        if pairs.get("SystemdService") != unit_name:
            fail("A-003", ACTIVATION, f"SystemdService must be {unit_name}, found {pairs.get('SystemdService')}")
        if pairs.get("User") != "root":
            fail("A-003", ACTIVATION, "User must be root")
        if ACTIVATION.name != f"{bus}.service.in":
            fail("A-003", ACTIVATION, f"file name must be {bus}.service.in")

    # A-004: systemd unit.
    unit = read(root, UNIT)
    if unit is None:
        fail("A-004", UNIT, "missing")
    else:
        pairs = ini_pairs(unit)
        values = dict(pairs)
        if values.get("BusName") != bus:
            fail("A-004", UNIT, f"BusName must be {bus}, found {values.get('BusName')}")
        for key, expected in REQUIRED_UNIT.items():
            if values.get(key) != expected:
                fail("A-004", UNIT, f"{key} must be '{expected}', found '{values.get(key)}'")
        for key in FORBIDDEN_UNIT:
            if key in values:
                fail("A-004", UNIT, f"{key} must not be set (pacman needs the access it would remove)")
        if UNIT.name != f"{unit_name}.in":
            fail("A-004", UNIT, f"file name must be {unit_name}.in")

    # A-005: polkit policy.
    policy_text = read(root, POLICY)
    if policy_text is None:
        fail("A-005", POLICY, "missing")
    else:
        try:
            policy = ElementTree.fromstring(re.sub(r"<!DOCTYPE[^>]*>", "", re.sub(r"<\?xml[^>]*\?>", "", policy_text), flags=re.S))
        except ElementTree.ParseError as error:
            fail("A-005", POLICY, f"not well-formed: {error}")
        else:
            actions = {action.get("id"): action for action in policy.iter("action")}
            if set(actions) != {install_id, remove_id}:
                fail("A-005", POLICY, f"actions must be exactly {install_id} and {remove_id}, found {sorted(actions)}")
            for action_id, wanted in ((install_id, "auth_admin_keep"), (remove_id, "auth_admin")):
                action = actions.get(action_id)
                if action is None:
                    continue
                defaults = action.find("defaults")
                found = {tag: (defaults.findtext(tag) or "").strip() if defaults is not None else ""
                         for tag in ("allow_any", "allow_inactive", "allow_active")}
                if set(found.values()) != {wanted} or any(not value for value in found.values()):
                    fail("A-005", POLICY, f"{action_id} defaults must all be {wanted}, found {found}")

    # A-006: the service's default action IDs are the same strings.
    service_text = read(root, PLAN_SERVICE)
    if service_text is None:
        fail("A-006", PLAN_SERVICE, "missing")
    else:
        for field, expected in (("install", install_id), ("remove", remove_id)):
            match = re.search(rf'std::string {field} = "([^"]+)";', service_text)
            if not match or match.group(1) != expected:
                fail("A-006", PLAN_SERVICE, f"AuthorizationActions::{field} must be {expected}")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    arguments = parser.parse_args()
    problems = check(arguments.root)
    for line in problems:
        print(line)
    if problems:
        return 1
    print("check-helper-artifacts: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
