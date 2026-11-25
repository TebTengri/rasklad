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
    def lock_path(self):
        return Path(self.setup.STATE_HOME, '.rasklad-setup.lock')

    def test_second_run_is_refused(self):
        first = self.setup.lock_state()
        self.addCleanup(os.close, first)
        self.assertTrue(self.lock_path().exists())
        with self.assertRaises(self.setup.Refused):
            self.setup.lock_state()

    def test_same_lock_whatever_the_runtime_directory(self):
        saved = os.environ.get('XDG_RUNTIME_DIR')
        self.addCleanup(lambda: os.environ.pop('XDG_RUNTIME_DIR', None) if saved is None
                        else os.environ.__setitem__('XDG_RUNTIME_DIR', saved))
        os.environ.pop('XDG_RUNTIME_DIR', None)
        first = self.setup.lock_state()
        self.addCleanup(os.close, first)
        os.environ['XDG_RUNTIME_DIR'] = self.tmp.name
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

    def test_clean_revert_keeps_the_lock_file(self):
        fake = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')])
        self.hypr('us,ru', variant=',')
        self.install(fake, single_layout=True)
        self.setup.hypr_layouts_back = lambda state: True
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.setup.main(['--revert']), 0)
        self.assertTrue(self.lock_path().exists())
        self.assertFalse(Path(self.setup.STATE_DIR).exists())


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

    def test_file_swapped_for_a_fifo_takes_the_record_back(self):
        old = self.input.read_bytes()
        state = self.setup.new_state()
        self.input.unlink()
        os.mkfifo(self.input)
        self.setup.file_state = lambda path: None  # swapped after the type check
        self.assertFalse(self.setup.record_then_check(state, self.setup.INPUT, old, old + b'-- ours\n'))
        self.assertNotIn(self.setup.INPUT, self.setup.load_state()['files'])

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
    def test_group_kept_while_the_bar_widget_is_used(self):
        fake = FakeFcitx([('keyboard-us', '')])
        self.hypr('us,ru', variant=',')
        self.setup.hypr_running = lambda: True
        errors = iter([[], ['input.lua: error']])
        self.setup.hypr_errors = lambda: next(errors, [])
        self.setup.hypr_layouts_back = lambda state: True
        self.setup.shell_uses_widget = lambda: True
        code, out = self.install(fake, single_layout=True)
        self.assertEqual(code, 1)
        self.assertIn('the bar widget switches between both layouts', out)
        self.assertEqual(fake.items, [('keyboard-us', ''), ('keyboard-ru', '')])

    def test_group_restored_after_new_hyprland_errors(self):
        # Fcitx had only us; install adds ru, then Hyprland reports a new error:
        # the files and the group both go back.
        fake = FakeFcitx([('keyboard-us', '')])
        self.setup.shell_uses_widget = lambda: False
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


class GroupRollbackScope(Base):
    def errors_after(self):
        self.setup.hypr_running = lambda: True
        errors = iter([[], ['bindings.lua: error']])
        self.setup.hypr_errors = lambda: next(errors, [])

    def setUp(self):
        super().setUp()
        self.setup.shell_uses_widget = lambda: False

    def test_group_kept_while_an_earlier_block_keeps_us(self):
        # An earlier install's input.lua block still keeps Hyprland at us: removing
        # ru from Fcitx would leave nothing that types Cyrillic.
        fake = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')])
        self.hypr('us,ru', variant=',')
        code, out = self.install(fake, single_layout=True)
        self.assertEqual(code, 0, out)
        fake.items = [('keyboard-us', '')]  # ru taken out of Fcitx by hand
        self.setup.bindings_plan = lambda data=None: ('missing', b'-- rasklad rule\n')
        Path(self.setup.BINDINGS).write_bytes(b'-- user bindings\n')
        self.errors_after()
        code, out = self.install(fake)
        self.assertEqual(code, 1, out)
        self.assertNotIn('restored fcitx-group', out)
        self.assertEqual(fake.items, [('keyboard-us', ''), ('keyboard-ru', '')])


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
