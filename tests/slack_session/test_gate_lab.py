import importlib.util
import os
import subprocess
import sys
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(name, rel):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


clients = load('native_clients_gate_test', 'tools/native_ime_clients.py')
lab = load('wayland_lab_gate_test', 'tools/wayland_ime_lab.py')


class Base:
    class Refused(Exception):
        pass


class GatePipe(unittest.TestCase):
    def test_inactive_without_gate_env(self):
        env = {k: v for k, v in os.environ.items() if k not in ('RASKLAD_SLACK_GATE_LAB',)}
        with unittest.mock.patch.dict(os.environ, env, clear=True):
            self.assertIsNone(clients.slack_gate_pipe(Base, {}))

    def test_requires_session_env_and_system_frontend(self):
        with unittest.mock.patch.dict(os.environ, {'RASKLAD_SLACK_GATE_LAB': '1'}, clear=True):
            with self.assertRaises(Base.Refused):
                clients.slack_gate_pipe(Base, {})

    def test_fcitx_gets_only_read_end(self):
        with unittest.mock.patch.dict(os.environ, {'RASKLAD_SLACK_GATE_LAB': '1', 'RASKLAD_SLACK_SESSION_LAB': '1'}, clear=True):
            options = {}
            read_end, write_end = clients.slack_gate_pipe(Base, options)
        try:
            self.assertEqual(options['pass_fds'], (read_end,))
            self.assertEqual(options['env']['RASKLAD_SESSION_ARM_FD'], str(read_end))
            self.assertFalse(os.get_inheritable(write_end))
        finally:
            os.close(read_end); os.close(write_end)


class WriteEndDetection(unittest.TestCase):
    def test_read_end_only_is_absent_write_end_is_present(self):
        r, w = os.pipe()
        try:
            reader = subprocess.Popen(['/usr/bin/sleep', '5'], pass_fds=(r,))
            writer = subprocess.Popen(['/usr/bin/sleep', '5'], pass_fds=(w,))
            try:
                for child in (reader, writer):  # after exec the inherited fds are final
                    for _ in range(200):
                        if os.readlink(f'/proc/{child.pid}/exe') == '/usr/bin/sleep':
                            break
                        time.sleep(.01)
                self.assertFalse(clients.fd_present(reader.pid, w))
                self.assertTrue(clients.fd_present(writer.pid, w))
            finally:
                reader.kill(); writer.kill(); reader.wait(); writer.wait()
        finally:
            os.close(r); os.close(w)


class LabSelection(unittest.TestCase):
    def test_slack_gate_is_exclusive(self):
        for options in (dict(focus_stress=True), dict(addon_witness=True), dict(frontend_variant='keep-grab'),
                        dict(disable_suite=True), dict(focus_repeats=2), dict(nested_smoke=True)):
            with self.assertRaises(lab.base.Refused):
                lab.slack_gate_selection(True, **options)
        lab.slack_gate_selection(True, focus_stress=False, frontend_variant='system', focus_repeats=1)
        with self.assertRaises(lab.base.Refused):
            lab.slack_gate_selection(True, focus_repeats=True)
        lab.slack_gate_selection(False, focus_stress=True)

    def test_sandbox_sets_gate_env_and_addon(self):
        source = (ROOT / 'tools/wayland_ime_lab.py').read_text()
        self.assertIn("'--setenv','RASKLAD_SLACK_SESSION_LAB','1','--setenv','RASKLAD_SLACK_GATE_LAB','1'", source)
        self.assertIn("/lab/addons/raskladslackgatetest.so", source)


import unittest.mock  # noqa: E402

if __name__ == '__main__':
    unittest.main()
