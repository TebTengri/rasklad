# SPDX-License-Identifier: MIT
"""Admission/report tests: no Fcitx, compositor, host input or display."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('drain_fixture',Path(__file__).with_name('run.py'))
fixture=importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

class RunnerTests(unittest.TestCase):
    def report(self,code):
        child=subprocess.Popen([sys.executable,'-c',code],stdout=subprocess.PIPE)
        try:return fixture.read_line(child,.2)
        finally:
            if child.poll() is None:child.terminate()
            child.wait(timeout=2)
            child.stdout.close()

    def test_partial_report_cannot_block_past_deadline(self):
        with self.assertRaisesRegex(RuntimeError,'server_timeout'):
            self.report("import sys,time;sys.stdout.write('{');sys.stdout.flush();time.sleep(5)")

    def test_oversized_report_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError,'report_bound'):
            self.report("print('x'*4097)")

    def test_complete_report(self):
        self.assertEqual(self.report("print('{\"ready\":true}')"),{'ready':True})

    def test_eof_is_not_readiness(self):
        with self.assertRaisesRegex(RuntimeError,'server_eof'):
            self.report('pass')

    def test_forged_environment_cannot_enter_private_mode(self):
        env={'RASKLAD_NATIVE_WAYLAND_LAB':'1','HOME':'/home/lab',
             'XDG_RUNTIME_DIR':'/run/lab','WAYLAND_DISPLAY':'wayland-lab'}
        with patch.dict(fixture.os.environ,env,clear=True),patch.object(fixture.subprocess,'Popen') as start:
            with self.assertRaisesRegex(RuntimeError,'namespace_guard'):fixture.inside('baseline')
            start.assert_not_called()

if __name__=='__main__':unittest.main()
