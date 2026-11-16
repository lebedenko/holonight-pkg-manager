#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Provenance changes must invalidate incremental binaries without reconfiguration."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

GENERATOR = Path(__file__).resolve().parents[1] / 'scripts/build-identity.py'


class BuildIdentity(unittest.TestCase):
    def setUp(self):
        environment = os.environ.copy()
        environment.pop('HOLONIGHT_BUILD_IDENTITY_FILE', None)
        self.environment = patch.dict(os.environ, environment, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def test_commit_edits_and_untracked_sources_refresh_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source'
            source.mkdir()
            header = root / 'generated/BuildIdentity.h'

            def git(*args):
                return subprocess.check_output(['git', '-C', str(source), *args], stderr=subprocess.DEVNULL)

            def generate():
                subprocess.run([sys.executable, str(GENERATOR), str(source), str(header), '0.1.0'], check=True)
                return header.read_text()

            git('init')
            git('config', 'user.name', 'Fixture')
            git('config', 'user.email', 'fixture@example.invalid')
            (source / 'main.cpp').write_text('first')
            git('add', '.')
            git('commit', '-m', 'fixture')
            clean = generate()
            stamp = header.stat().st_mtime_ns
            self.assertEqual(clean, generate())
            self.assertEqual(stamp, header.stat().st_mtime_ns)
            (source / 'main.cpp').write_text('fixed')
            dirty = generate()
            self.assertNotEqual(clean, dirty)
            self.assertIn('+dirty.', dirty)
            (source / 'new.cpp').write_text('added')
            self.assertNotEqual(dirty, generate())
            git('add', '.')
            git('commit', '-m', 'fix')
            committed = generate()
            self.assertNotEqual(clean, committed)
            self.assertNotIn('+dirty.', committed)

    def test_snapshot_preserves_original_identity_through_override(self):
        spec = importlib.util.spec_from_file_location('container', GENERATOR.parent / 'ci/container.py')
        container = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(container)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source'
            source.mkdir()
            def git(*args):
                return subprocess.check_output(['git', '-C', str(source), *args], stderr=subprocess.DEVNULL)
            git('init')
            git('config', 'user.name', 'Fixture')
            git('config', 'user.email', 'fixture@example.invalid')
            (source / 'main.cpp').write_text('first')
            git('add', '.')
            git('commit', '-m', 'fixture')
            for dirty in (False, True):
                with self.subTest(dirty=dirty):
                    if dirty:
                        (source / 'main.cpp').write_text('edited')
                        (source / 'new.cpp').write_text('added')
                    override = root / 'identity.txt'
                    # Print mode must ignore even an invalid selected override.
                    environment = {**os.environ, 'HOLONIGHT_BUILD_IDENTITY_FILE': str(override)}
                    override.write_text('unknown')
                    identity = subprocess.check_output(
                        [sys.executable, str(GENERATOR), '--print-identity', str(source)],
                        env=environment, text=True).strip()
                    self.assertEqual('+dirty.' in identity, dirty)
                    exported = root / ('dirty' if dirty else 'clean')
                    container.snapshot(source, exported)
                    self.assertNotEqual(subprocess.run(
                        ['git', '-C', str(exported), 'rev-parse', 'HEAD'],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode, 0)
                    override.write_text(identity + '\n')
                    header = root / 'BuildIdentity.h'
                    command = [sys.executable, str(GENERATOR), str(exported), str(header), '0.1.0']
                    subprocess.run(command, env=environment, check=True)
                    self.assertIn(f'0.1.0 ({identity})', header.read_text())
                    stamp = header.stat().st_mtime_ns
                    subprocess.run(command, env=environment, check=True)
                    self.assertEqual(stamp, header.stat().st_mtime_ns)
                    # Every invocation reads the file, overriding even a valid Git source.
                    other = 'a' * 40
                    override.write_text(other)
                    subprocess.run([sys.executable, str(GENERATOR), str(source), str(header), '0.1.0'],
                                   env=environment, check=True)
                    self.assertIn(f'({other})', header.read_text())

    def test_invalid_overrides_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            override = root / 'identity.txt'
            header = root / 'BuildIdentity.h'
            environment = {**os.environ, 'HOLONIGHT_BUILD_IDENTITY_FILE': str(override)}
            for content in (None, '', 'unknown', 'bad', 'a' * 40 + '+dirty.bad',
                            '0.1.0 (' + 'a' * 40 + ')', 'a' * 40 + '\nextra'):
                with self.subTest(content=content):
                    if content is not None:
                        override.write_text(content)
                    result = subprocess.run(
                        [sys.executable, str(GENERATOR), str(root), str(header), '0.1.0'],
                        env=environment, capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('HOLONIGHT_BUILD_IDENTITY_FILE', result.stderr)
                    self.assertFalse(header.exists())

    def test_archive_is_explicitly_unknown(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            header = root / 'BuildIdentity.h'
            subprocess.run([sys.executable, str(GENERATOR), str(root), str(header), '0.1.0'], check=True)
            self.assertIn('unknown', header.read_text())


if __name__ == '__main__':
    unittest.main()
