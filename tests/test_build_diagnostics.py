#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless diagnostics must report provenance without activating a privileged helper."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from xml.sax.saxutils import escape

BINARIES = sys.argv[1:]
sys.argv = sys.argv[:1]


class Diagnostics(unittest.TestCase):
    def test_all_executables_report_the_same_build_without_gui_initialization(self):
        identities = []
        for binary in BINARIES:
            result = subprocess.run([binary, '--build-info'], capture_output=True, text=True,
                                    env=dict(os.environ, QT_QPA_PLATFORM='invalid'), timeout=10, check=True)
            identities.append(result.stdout.removeprefix('holonight-packages: ').strip())
        self.assertEqual(len(identities), 3)
        self.assertTrue(all(identity == identities[0] for identity in identities))
        self.assertNotIn('unknown', identities[0])

    def test_diagnostics_do_not_activate_an_available_service(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            marker = root / 'activated'
            (root / 'org.holonight.PackageHelper1.service').write_text(
                '[D-BUS Service]\nName=org.holonight.PackageHelper1\n'
                f'Exec=/usr/bin/touch {marker}\n')
            config = root / 'bus.conf'
            config.write_text('<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen>'
                              '<auth>EXTERNAL</auth><policy context="default">'
                              '<allow send_destination="*"/><allow receive_sender="*"/><allow own="*"/></policy>'
                              f'<servicedir>{escape(str(root))}</servicedir></busconfig>')
            daemon = subprocess.Popen(['dbus-daemon', '--nofork', '--print-address=1',
                                       f'--config-file={config}'], stdout=subprocess.PIPE, text=True)
            try:
                address = daemon.stdout.readline().strip()
                self.assertTrue(address)
                result = subprocess.run([BINARIES[0], '--diagnostics'], capture_output=True, text=True,
                                        env=dict(os.environ, DBUS_SYSTEM_BUS_ADDRESS=address,
                                                 QT_QPA_PLATFORM='invalid'), timeout=10)
                self.assertEqual(result.returncode, 1)
                self.assertIn('unavailable', result.stdout)
                self.assertFalse(marker.exists())
            finally:
                daemon.terminate()
                daemon.wait(timeout=5)
                daemon.stdout.close()


if __name__ == '__main__':
    unittest.main()
