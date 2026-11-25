# SPDX-License-Identifier: MIT
"""SYS-272: a QML property bound twice does not load at all (the settings window
or the bar widget would be missing). qmllint finds it without a display."""
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QMLLINT = shutil.which('qmllint6') or shutil.which('qmllint') or (
    '/usr/lib/qt6/bin/qmllint' if Path('/usr/lib/qt6/bin/qmllint').exists() else None)
FILES = ('settings/Main.qml', 'packaging/omarchy/rasklad.layout/RaskladLayout.qml')


@unittest.skipUnless(QMLLINT, 'qmllint is not installed')
class QmlLint(unittest.TestCase):
    def test_no_property_bound_twice(self):
        for rel in FILES:
            out = subprocess.run([QMLLINT, str(ROOT / rel)], capture_output=True, text=True, timeout=120)
            self.assertNotIn('duplicate-property-binding', out.stdout + out.stderr, rel)


if __name__ == '__main__':
    unittest.main()
