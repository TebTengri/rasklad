# SPDX-License-Identifier: MIT
"""SYS-272: rasklad-setup runs once at a time, never blocks on a swapped file,
records before its last comparison, and explains what it leaves as is.

Stand-ins for hyprctl and Fcitx, in a temporary XDG_CONFIG_HOME, XDG_STATE_HOME
and XDG_RUNTIME_DIR: the user's own files are never touched.
"""
import contextlib
import io
import os
import tempfile
import time
import unittest
from pathlib import Path

from test_setup_layouts import Base, FakeFcitx


class Lock(Base):
    def setUp(self):
        super().setUp()
        runtime = tempfile.TemporaryDirectory()
        self.addCleanup(runtime.cleanup)
        os.chmod(runtime.name, 0o700)
        saved = os.environ.get('XDG_RUNTIME_DIR')
        self.addCleanup(lambda: os.environ.pop('XDG_RUNTIME_DIR', None) if saved is None
                        else os.environ.__setitem__('XDG_RUNTIME_DIR', saved))
        os.environ['XDG_RUNTIME_DIR'] = runtime.name
        self.runtime = Path(runtime.name)

    def test_second_run_is_refused(self):
        first = self.setup.lock_state()
        self.addCleanup(os.close, first)
        self.assertTrue((self.runtime / 'rasklad-setup.lock').exists())
        with self.assertRaises(self.setup.Refused):
            self.setup.lock_state()

    def test_lock_released_with_the_process(self):
        os.close(self.setup.lock_state())
        os.close(self.setup.lock_state())

    def test_main_refuses_while_locked(self):
        first = self.setup.lock_state()
        self.addCleanup(os.close, first)
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.setup.main(['--revert']), 1)
        self.assertIn('another rasklad-setup is running', err.getvalue())

    def test_shared_runtime_directory_is_not_used(self):
        os.chmod(self.runtime, 0o755)
        fd = self.setup.lock_state()
        self.addCleanup(os.close, fd)
        self.assertFalse((self.runtime / 'rasklad-setup.lock').exists())
        self.assertTrue(Path(self.setup.STATE_DIR, 'lock').exists())


class Read(Base):
    def test_fifo_is_refused_without_blocking(self):
        fifo = Path(self.tmp.name) / 'fifo'
        os.mkfifo(fifo)
        started = time.monotonic()
        with self.assertRaises(OSError):
            self.setup.read(str(fifo))
        self.assertLess(time.monotonic() - started, 1)

    def test_missing_and_regular(self):
        self.assertIsNone(self.setup.read(str(Path(self.tmp.name) / 'none')))
        self.assertEqual(self.setup.read(str(self.input)), b'-- user file\n')


class RecordThenCheck(Base):
    def test_edit_after_preview_takes_the_new_record_back(self):
        old = self.input.read_bytes()
        state = self.setup.new_state()
        self.input.write_bytes(b'-- edited meanwhile\n')
        self.assertFalse(self.setup.record_then_check(state, self.setup.INPUT, old, old + b'-- ours\n'))
        self.assertNotIn(self.setup.INPUT, state['files'])
        self.assertFalse(Path(self.setup.backup_path(self.setup.INPUT)).exists())
        self.assertFalse(Path(self.setup.INPUT + '.rasklad.bak').exists())
        self.assertEqual(self.input.read_bytes(), b'-- edited meanwhile\n')

    def test_users_own_bak_is_kept(self):
        old = self.input.read_bytes()
        bak = Path(self.setup.INPUT + '.rasklad.bak')
        bak.write_bytes(old)  # not ours: it was there before
        state = self.setup.new_state()
        self.input.write_bytes(b'-- edited meanwhile\n')
        self.assertFalse(self.setup.record_then_check(state, self.setup.INPUT, old, old + b'-- ours\n'))
        self.assertTrue(bak.exists())

    def test_unchanged_file_is_recorded(self):
        old = self.input.read_bytes()
        state = self.setup.new_state()
        self.assertTrue(self.setup.record_then_check(state, self.setup.INPUT, old, old + b'-- ours\n'))
        self.assertIn(self.setup.INPUT, state['files'])
        self.assertEqual(Path(self.setup.backup_path(self.setup.INPUT)).read_bytes(), old)


class FcitxGroup(Base):
    def test_saved_group_without_us_is_never_written(self):
        fake = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')])
        self.setup.fcitx = fake.bus
        state = self.setup.new_state()
        state['fcitx'] = {'group': 'Default', 'layout': 'us', 'before': [],
                          'after': [['keyboard-us', ''], ['keyboard-ru', '']], 'after_layout': 'us'}
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertFalse(self.setup.restore_fcitx(state))
        self.assertEqual(fake.calls, 0)
        self.assertIn('no keyboard-us', out.getvalue())

    def test_group_restored_after_new_hyprland_errors(self):
        # Fcitx had only us; install adds ru, then Hyprland reports a new error:
        # the files and the group both go back.
        fake = FakeFcitx([('keyboard-us', '')])
        self.hypr('us,ru', variant=',')
        self.setup.hypr_running = lambda: True
        errors = iter([[], ['input.lua: error']])
        self.setup.hypr_errors = lambda: next(errors, [])
        self.setup.hypr_layouts_back = lambda state: True
        code, out = self.install(fake, single_layout=True)
        self.assertEqual(code, 1)
        self.assertIn('restored fcitx-group', out)
        self.assertEqual(fake.items, [('keyboard-us', '')])
        self.assertEqual(self.input.read_bytes(), b'-- user file\n')


class Explained(Base):
    def test_single_other_layout_is_said(self):
        self.assertEqual(self.plan('de', options='compose:caps'), ('single', None))
        self.assertEqual(self.plan('us', options='compose:caps'), ('ok', None))
        fake = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')])
        self.hypr('de', options='')
        code, out = self.install(fake, single_layout=True)
        self.assertIn('one layout that is not us', out)

    def test_legacy_block_removed_by_hand_then_revert_then_install(self):
        fake = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')])
        self.hypr('us,ru', variant=',')
        code, out = self.install(fake, single_layout=True)
        self.assertEqual(code, 0, out)
        self.assertIn(b'rasklad', self.input.read_bytes())
        # The user takes the block out by hand and edits the file too.
        self.input.write_bytes(b'-- user file\n-- edited\n')
        self.setup.hypr_layouts_back = lambda state: True
        code, out = self.install(fake, single_layout=True)
        self.assertIn('run --revert first', out)
        # --revert inside the session forgets the record of a block that is gone.
        self.setup.hypr_session = lambda: 'running'
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.setup.revert()
        self.assertIn('our block is gone', out.getvalue())
        self.assertNotIn(self.setup.INPUT, self.setup.load_state()['files'])
        # ... and --install adds the current block again, with no new advice to revert.
        self.setup.hypr_session = lambda: 'none'
        code, out = self.install(fake, single_layout=True)
        self.assertEqual(code, 0, out)
        self.assertNotIn('run --revert first', out)
        self.assertIn('done     input.lua', out)
        self.assertIn(b'rasklad', self.input.read_bytes())

    def test_legacy_advice_names_the_order(self):
        for word in ('--revert inside the Hyprland session', 'remove it', '--revert again', '--install'):
            self.assertIn(word, self.setup.LEGACY_ADVICE)


if __name__ == '__main__':
    unittest.main()
