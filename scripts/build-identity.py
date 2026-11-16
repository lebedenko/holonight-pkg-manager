#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh build provenance without forcing recompilation when inputs are unchanged."""
import hashlib
from pathlib import Path
import subprocess
import sys

source, destination, version = sys.argv[1:]

def git(*args):
    return subprocess.check_output(['git', '-C', source, *args], stderr=subprocess.DEVNULL)

try:
    revision = git('rev-parse', 'HEAD').decode().strip()
    changes = git('diff', '--binary', 'HEAD', '--', '.')
    for relative in sorted(git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0')):
        if relative:
            path = Path(source) / relative.decode()
            if path.is_file():
                changes += relative + b'\0' + path.read_bytes()
    identity = revision + ('+dirty.' + hashlib.sha256(changes).hexdigest()[:16] if changes else '')
except (OSError, subprocess.CalledProcessError):
    identity = 'unknown'
text = f'#pragma once\n#define HOLONIGHT_BUILD_ID "{version} ({identity})"\n'
path = Path(destination)
path.parent.mkdir(parents=True, exist_ok=True)
if not path.exists() or path.read_text() != text:
    path.write_text(text)
