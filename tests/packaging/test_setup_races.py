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
    def test_saved_group_without_any_keyboard_is_never_written(self):
        fake = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')])
        self.setup.fcitx = fake.bus
        state = self.setup.new_state()
        state['fcitx'] = {'group': 'Default', 'layout': 'us', 'before': [['pinyin', '']],
                          'after': [['keyboard-us', ''], ['keyboard-ru', '']], 'after_layout': 'us'}
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertFalse(self.setup.restore_fcitx(state))
        self.assertEqual(fake.calls, 0)
        self.assertIn('no keyboard layout', out.getvalue())

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


class GroupRollbackScope(Base):
    def errors_after(self):
        self.setup.hypr_running = lambda: True
        errors = iter([[], ['bindings.lua: error']])
        self.setup.hypr_errors = lambda: next(errors, [])

    def test_non_us_group_reverts(self):
        # A German-only group is the user's own: install adds us and ru, revert puts it back.
        fake = FakeFcitx([('keyboard-de', '')])
        self.setup.fcitx = fake.bus
        state = self.setup.new_state()
        state['fcitx'] = {'group': 'Default', 'layout': 'us', 'before': [['keyboard-de', '']],
                          'after': [['keyboard-de', ''], ['keyboard-us', ''], ['keyboard-ru', '']],
                          'after_layout': 'us'}
        fake.items = [('keyboard-de', ''), ('keyboard-us', ''), ('keyboard-ru', '')]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(self.setup.restore_fcitx(state))
        self.assertEqual(fake.items, [('keyboard-de', '')])

    def test_rollback_goes_back_to_this_runs_group(self):
        # Fcitx had us and de (ru taken out since an earlier install); this run adds
        # ru, then a new error: back to us and de, never to the older snapshot.
        fake = FakeFcitx([('keyboard-us', ''), ('keyboard-de', '')])
        state = self.setup.new_state()
        state['fcitx'] = {'group': 'Default', 'layout': 'us', 'before': [['keyboard-us', '']],
                          'after': [['keyboard-us', ''], ['keyboard-ru', '']], 'after_layout': 'us'}
        self.setup.save_state(state)
        self.hypr('us', options='')  # nothing for input.lua
        self.setup.bindings_plan = lambda data=None: ('missing', b'-- rasklad rule\n')
        bindings = Path(self.setup.BINDINGS)
        bindings.write_bytes(b'-- user bindings\n')
        self.errors_after()
        code, out = self.install(fake)
        self.assertEqual(code, 1, out)
        self.assertEqual(fake.items, [('keyboard-us', ''), ('keyboard-de', '')])
        saved = self.setup.load_state()['fcitx']
        self.assertEqual(saved['before'], [['keyboard-us', '']])  # the earlier install's record stays

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
