#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh build provenance without forcing recompilation when inputs are unchanged."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess


def source_identity(source):
    """Inspect source content, independently of any container identity override."""
    def git(*args):
        return subprocess.check_output(['git', '-C', str(source), *args], stderr=subprocess.DEVNULL)

    try:
        revision = git('rev-parse', 'HEAD').decode().strip()
        changes = git('diff', '--binary', 'HEAD', '--', '.')
        for relative in sorted(git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0')):
            if relative:
                path = Path(source) / relative.decode()
                if path.is_file():
                    changes += relative + b'\0' + path.read_bytes()
        return revision + ('+dirty.' + hashlib.sha256(changes).hexdigest()[:16] if changes else '')
    except (OSError, subprocess.CalledProcessError):
        return 'unknown'


def build_identity(source):
    override = os.environ.get('HOLONIGHT_BUILD_IDENTITY_FILE')
    if override is None:
        return source_identity(source)
    try:
        identity = Path(override).read_text().strip()
    except (OSError, ValueError) as error:
        raise ValueError(f'Cannot read HOLONIGHT_BUILD_IDENTITY_FILE {override!r}: {error}') from error
    if not re.fullmatch(r'[0-9a-f]{40}(?:\+dirty\.[0-9a-f]{16})?', identity):
        raise ValueError(f'Invalid identity in HOLONIGHT_BUILD_IDENTITY_FILE {override!r}: expected revision and optional dirty fingerprint')
    return identity


def generate_header(source, destination, version):
    identity = build_identity(source)
    text = f'#pragma once\n#define HOLONIGHT_BUILD_ID "{version} ({identity})"\n'
    path = Path(destination)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--print-identity', metavar='SOURCE', type=Path)
    parser.add_argument('arguments', nargs='*', metavar='ARG')
    args = parser.parse_args()
    if args.print_identity is not None:
        if args.arguments:
            parser.error('--print-identity accepts only SOURCE')
        print(source_identity(args.print_identity))
    else:
        if len(args.arguments) != 3:
            parser.error('expected SOURCE DESTINATION VERSION')
        try:
            generate_header(*args.arguments)
        except ValueError as error:
            parser.error(str(error))


if __name__ == '__main__':
    main()
