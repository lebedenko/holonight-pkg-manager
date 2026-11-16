#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Provenance changes must invalidate incremental binaries without reconfiguration."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

GENERATOR = Path(__file__).resolve().parents[1] / 'scripts/build-identity.py'


class BuildIdentity(unittest.TestCase):
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

    def test_archive_is_explicitly_unknown(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            header = root / 'BuildIdentity.h'
            subprocess.run([sys.executable, str(GENERATOR), str(root), str(header), '0.1.0'], check=True)
            self.assertIn('unknown', header.read_text())


if __name__ == '__main__':
    unittest.main()
