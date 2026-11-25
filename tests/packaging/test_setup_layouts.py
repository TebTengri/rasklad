# SPDX-License-Identifier: MIT
"""SYS-268: rasklad-setup keeps a layout the lock screen can use, and asks first.

input_plan() and install() with stand-ins for hyprctl and Fcitx, in a temporary
XDG_CONFIG_HOME and XDG_STATE_HOME: the user's own files are never touched.
"""
import contextlib
import importlib.machinery
import importlib.util
import io
from unittest import mock
import os
import re
import socket
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True  # no __pycache__ next to packaging/arch/rasklad-setup


def load_setup(home):
    os.environ['XDG_CONFIG_HOME'] = str(Path(home) / 'config')
    os.environ['XDG_STATE_HOME'] = str(Path(home) / 'state')
    loader = importlib.machinery.SourceFileLoader('rasklad_setup', str(ROOT / 'packaging/arch/rasklad-setup'))
    spec = importlib.util.spec_from_loader('rasklad_setup', loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class FakeFcitx:
    """One group, "Default" unless renamed; .layout is its default layout."""
    def __init__(self, items, apply=True):
        self.items = items
        self.apply = apply
        self.calls = 0
        self.name = 'Default'
        self.layout = 'us'
        self.writes = []

    def info(self, name):
        if name != self.name:
            return None
        return (self.name, self.layout, [list(i) for i in self.items])

    def bus(self):
        return {'group': lambda: self.info(self.name), 'group_by': self.info,
                'groups': lambda: [self.name], 'set_group': self.set_group, 'status': lambda: None}

    def set_group(self, name, layout, items):
        self.calls += 1
        self.writes.append((name, layout, [list(i) for i in items]))
        if self.apply and name == self.name:
            self.items = [tuple(i) for i in items]
            self.layout = layout
        return True  # Fcitx answers normally even when nothing changed


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.saved_env = {k: os.environ.get(k) for k in ('XDG_CONFIG_HOME', 'XDG_STATE_HOME')}
        self.addCleanup(self.restore_env)
        self.setup = load_setup(self.tmp.name)
        hypr = Path(self.tmp.name) / 'config' / 'hypr'
        hypr.mkdir(parents=True)
        self.input = hypr / 'input.lua'
        self.input.write_bytes(b'-- user file\n')
        # The other two steps are stubbed as done; their files exist (SYS-272: a
        # missing one is a fault that makes --install exit 1).
        (hypr / 'bindings.lua').write_bytes(b'-- bindings\n')
        shell = Path(self.setup.SHELL_JSON); shell.parent.mkdir(parents=True, exist_ok=True)
        shell.write_bytes(b'{}\n')
        self.setup.lua_ok = lambda data: True
        self.setup.hypr_errors = lambda: []
        self.setup.hypr_running = lambda: False
        self.setup.hypr_session = lambda: 'none'
        # No real hyprctl, luac or anything else from a test.
        self.setup.run = lambda argv, timeout=5: None
        self.setup.hypr_reloaded = lambda: True
        self.setup.link_state = lambda: 'ok'
        self.setup.bindings_plan = lambda data=None: ('ok', None)
        self.setup.shell_plan = lambda data=None: ('ok', None)

    def restore_env(self):
        for k, v in self.saved_env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    def hypr(self, layout, options='compose:caps,grp:alts_toggle', variant=''):
        values = {'input:kb_layout': layout, 'input:kb_options': options, 'input:kb_variant': variant}
        self.setup.hypr_option = values.get

    def plan(self, layout, **kw):
        self.hypr(layout, **kw)
        return self.setup.input_plan(b'-- user file\n')

    def kept(self, block):
        text = block.decode()
        return [line.strip() for line in text[text.index('kb_layout = "'):].split('\n')[:3]]

    def install(self, fake, **kw):
        self.setup.fcitx = (lambda: fake.bus()) if fake else (lambda: None)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = self.setup.install(True, **kw)
        return code, out.getvalue()


class Plan(Base):
    def test_us_first(self):
        status, new = self.plan('us,ru', variant=',')
        self.assertEqual(status, 'missing')
        self.assertEqual(self.kept(new)[0], 'kb_layout = "us",')
        self.assertIn('kb_options = "compose:caps"', new.decode())

    def test_ru_first_keeps_us_for_the_lock_screen(self):
        status, new = self.plan('ru,us', variant='winkeys,intl')
        self.assertEqual(status, 'missing')
        self.assertEqual(self.kept(new)[:2], ['kb_layout = "us",', 'kb_variant = "intl",'])

    def test_missing_trailing_variant_is_the_base_layout(self):
        status, new = self.plan('ru,us', variant='winkeys')
        self.assertEqual(self.kept(new)[1], 'kb_variant = "",')

    def test_other_layouts_left_as_is(self):
        for layout in ('us,ru,kz', 'de,ru', 'ru', 'us,ru,us'):
            self.assertEqual(self.plan(layout, options='grp:alts_toggle'), ('other-layouts', None), layout)

    def test_us_variant_without_latin_is_not_offered(self):
        self.assertEqual(self.plan('ru,us', variant='ruchey_en,rus'), ('other-layouts', None))
        self.assertEqual(self.plan('us,ru', variant='intl,')[0], 'missing')

    def test_single_us_without_toggle_is_ok(self):
        self.assertEqual(self.plan('us', options='compose:caps'), ('ok', None))

    def test_unknown_when_a_query_fails(self):
        self.setup.hypr_option = {'input:kb_layout': 'ru,us', 'input:kb_options': ''}.get  # no kb_variant
        self.assertEqual(self.setup.input_plan(b'')[0], 'unknown')
        self.setup.hypr_option = lambda name: None
        self.assertEqual(self.setup.input_plan(b'')[0], 'unknown')

    def test_block_from_before_sys268_is_legacy(self):
        # Unmarked blocks: ru kept from ru,us, or us kept from us,ru,kz.
        for layout in (b'ru', b'us'):
            old = b'-- user\n' + self.setup.BEGIN.encode() + b'\nhl.config({input={kb_layout = "' + layout + b'"}})\n'
            self.assertEqual(self.setup.input_plan(old)[0], 'legacy', layout)
        status, new = self.plan('ru,us', variant=',')
        self.assertIn(self.setup.LAYOUT_MARK.encode(), new)
        self.assertEqual(self.setup.input_plan(new)[0], 'ok')
        # A marked block edited to ru, or a mark outside an unmarked block.
        self.assertEqual(self.setup.input_plan(new.replace(b'kb_layout = "us"', b'kb_layout = "ru"'))[0], 'legacy')
        old = (b'-- user\n' + self.setup.BEGIN.encode() + b'\nhl.config({input={kb_layout = "us"}})\n'
               + self.setup.END.encode() + b'\n' + self.setup.LAYOUT_MARK.encode() + b'\n')
        self.assertEqual(self.setup.input_plan(old)[0], 'legacy')
        # ru added in a comment-and-replace edit, single quotes.
        edited = new.replace(b'    kb_layout = "us",\n', b"    -- kb_layout = \"us\",\n    kb_layout = 'ru',\n")
        self.assertNotEqual(edited, new)
        self.assertEqual(self.setup.input_plan(edited)[0], 'legacy')
        # A group toggle put back in.
        toggled = new.replace(b'kb_options = "compose:caps"', b'kb_options = "compose:caps,grp:alts_toggle"')
        self.assertNotEqual(toggled, new)
        self.assertEqual(self.setup.input_plan(toggled)[0], 'legacy')

    def test_spaces_in_options_are_ignored(self):
        status, new = self.plan('us,ru', options='compose:caps, grp:alt_shift_toggle', variant=',')
        self.assertEqual(status, 'missing')
        self.assertIn('kb_options = "compose:caps"', new.decode())

    def test_odd_characters_only_skip_this_step(self):
        self.assertEqual(self.plan('us,ru', options='compose:caps;rm', variant=',')[0], 'unknown')


class Install(Base):
    def test_yes_alone_keeps_hyprland_layouts(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')])
        code, out = self.install(fake)
        self.assertEqual(code, 0)
        self.assertIn('--single-layout', out)
        self.assertEqual(self.input.read_bytes(), b'-- user file\n')

    def test_single_layout_writes_us_after_fcitx_has_both(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        code, out = self.install(fake, single_layout=True)
        self.assertEqual(code, 0, out)
        self.assertEqual(fake.calls, 1)
        self.assertIn(b'kb_layout = "us"', self.input.read_bytes())

    def test_no_fcitx_is_not_nothing_to_change(self):
        code, out = self.install(None)
        self.assertEqual(code, 1, out)
        self.assertIn('start Fcitx', out)
        self.assertNotIn('Nothing to change', out)

    def test_missing_bindings_is_a_fault(self):
        (Path(self.setup.BINDINGS)).unlink()
        code, out = self.install(FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')]))
        self.assertEqual(code, 1, out)
        self.assertIn('Not everything is connected', out)

    def test_symlinked_dotfiles_are_not_a_fault(self):
        bindings = Path(self.setup.BINDINGS)
        real = bindings.with_name('dotfiles-bindings.lua')
        bindings.rename(real); bindings.symlink_to(real)
        code, out = self.install(FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')]))
        self.assertEqual(code, 0, out)
        self.assertIn('Nothing to change', out)

    def test_bar_without_the_stock_widget_is_not_a_fault(self):
        self.setup.shell_plan = lambda data=None: ('no-slot', None)
        code, out = self.install(FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')]))
        self.assertEqual(code, 0, out)

    def test_unparsable_shell_json_is_a_fault(self):
        self.setup.shell_plan = lambda data=None: ('unparsable', None)
        code, out = self.install(FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')]))
        self.assertEqual(code, 1, out)

    def test_an_error_that_went_away_is_not_new(self):
        self.hypr('ru,us', variant=',')
        seen = iter([['old A', 'old B'], ['old A']])
        self.setup.hypr_errors = lambda: next(seen)
        self.setup.hypr_running = lambda: True
        code, out = self.install(FakeFcitx([('keyboard-us', '')]), single_layout=True)
        self.assertEqual(code, 0, out)
        self.assertNotIn('undoing', out)

    def test_nothing_to_change_when_all_is_done(self):
        code, out = self.install(FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')]))
        self.assertEqual(code, 0, out)
        self.assertIn('Nothing to change', out)

    def test_deleted_input_waits_for_hyprland_to_decide(self):
        # Hyprland may still run with our block's layouts: outside a session the
        # record stays and revert says where to run it (no temporary file left).
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        self.input.unlink()
        self.setup.fcitx = fake.bus
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(self.setup.revert(), 1)
        self.assertIn('deleted since install', out.getvalue())
        self.assertIn('inside the Hyprland session', out.getvalue())
        self.assertIn(self.setup.INPUT, self.setup.load_state()['files'])
        self.assertEqual([p.name for p in self.input.parent.iterdir() if p.name.startswith('.rasklad-setup.')], [])

    def test_deleted_input_is_released_once_hyprland_reloaded_without_it(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        self.input.unlink()
        self.setup.fcitx = fake.bus
        self.setup.hypr_session = lambda: 'running'
        self.setup.hypr_layouts_back = lambda state: True
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.setup.revert()
        state = self.setup.load_state() if os.path.exists(self.setup.STATE_FILE) else {'files': {}}
        self.assertNotIn(self.setup.INPUT, state['files'], out.getvalue())

    def test_failure_names_the_cause(self):
        def boom(*a, **k):
            raise PermissionError(13, 'Permission denied', '/some/where')
        self.setup.install = boom
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(self.setup.main(['--install', '--yes']), 1)
        self.assertIn('Permission denied (/some/where)', err.getvalue())

    def test_block_as_written_is_ours(self):
        self.hypr('ru,us', variant=',')
        self.assertEqual(self.install(FakeFcitx([('keyboard-us', '')]), single_layout=True)[0], 0)
        self.assertRegex(self.setup.load_state()['input_block'], r'^[0-9a-f]{64}$')
        self.assertEqual(self.setup.input_plan(None, self.setup.load_state()['input_block'])[0], 'ok')

    def test_edited_block_is_not_ours(self):
        self.hypr('ru,us', variant=',')
        self.assertEqual(self.install(FakeFcitx([('keyboard-us', '')]), single_layout=True)[0], 0)
        data = self.input.read_bytes()
        self.input.write_bytes(data.replace(b'kb_variant = ""', b'kb_variant = "intl"'))
        state = self.setup.load_state()
        self.assertEqual(self.setup.input_plan(None, state['input_block'])[0], 'edited')
        self.assertEqual(self.setup.input_plan(None, None)[0], 'ok')  # no record: as before
        self.setup.fcitx = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')]).bus
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.setup.check()
        self.assertRegex(out.getvalue(), r'edited\s+hyprland-layout')

    def test_revert_without_record_lists_traces(self):
        # SYS-272: the state directory is gone but our block is still there.
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake)[0], 0)
        Path(self.setup.STATE_FILE).unlink()
        self.setup.fcitx = fake.bus
        Path(self.setup.BINDINGS).write_bytes(('-- user\nlocal rasklad = "%s"\n' % self.setup.RULE).encode())
        before = Path(self.setup.BINDINGS).read_bytes()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(self.setup.revert(), 1)
        self.assertRegex(out.getvalue(), r'untracked\s+bindings\.lua')
        self.assertEqual(Path(self.setup.BINDINGS).read_bytes(), before)  # nothing guessed
        # Nothing of ours anywhere: nothing to revert.
        Path(self.setup.BINDINGS).write_bytes(b'-- bindings\n')
        Path(self.setup.SHELL_JSON).write_bytes(b'{}\n')
        self.input.write_bytes(b'-- user file\n')
        if os.path.islink(self.setup.PLUGIN_LINK):
            os.unlink(self.setup.PLUGIN_LINK)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.setup.revert(), 0)

    def test_revert_keeps_controls_for_an_untracked_layout_block(self):
        # SYS-272: the record is lost, Fcitx's group is reset and a new --install
        # records only the Fcitx row; the old input.lua block still keeps Hyprland
        # on us. --revert must not take keyboard-ru away.
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        block = self.input.read_bytes()
        Path(self.setup.STATE_FILE).unlink()
        fake2 = FakeFcitx([('keyboard-us', '')])
        self.install(fake2)  # re-adds keyboard-ru; the block is not recorded again
        self.assertEqual(self.input.read_bytes(), block)
        self.setup.fcitx = fake2.bus
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(self.setup.revert(), 1)
        self.assertRegex(out.getvalue(), r'untracked\s+input\.lua')
        self.assertIn('keyboard-ru', [name for name, _ in fake2.items])

    def test_check_reports_rasklad_off(self):
        # Owner decision 2026-11-25: the addon is off until turned on in the window;
        # check says so and treats it as a choice, not a fault.
        fake = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')])
        for enabled, shown in (('False', True), ('True', False), (None, False)):
            values = {'ProfileReady': 'True', 'AutoCorrect': 'True'}
            if enabled is not None:
                values['Enabled'] = enabled
            bus = dict(fake.bus(), status=lambda v=values: v)
            self.setup.fcitx = lambda b=bus: b
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.setup.check()
            self.assertEqual(bool(re.search(r'^off\s+rasklad\s', out.getvalue(), re.M)), shown)

    def test_revert_drops_the_block_hash(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        with contextlib.redirect_stdout(io.StringIO()):
            self.setup.fcitx = fake.bus
            self.setup.revert()
        state = self.setup.load_state() if os.path.exists(self.setup.STATE_FILE) else {}
        self.assertNotIn('input_block', state)

    def test_fcitx_reply_without_change_leaves_layouts(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')], apply=False)
        code, out = self.install(fake, single_layout=True)
        self.assertEqual(code, 1)
        self.assertIn('Fcitx does not have both layouts', out)
        self.assertEqual(self.input.read_bytes(), b'-- user file\n')

    def test_without_fcitx_layouts_wait(self):
        self.hypr('ru,us', variant=',')
        code, out = self.install(None, single_layout=True)
        self.assertIn('until Fcitx runs', out)
        self.assertEqual(self.input.read_bytes(), b'-- user file\n')

    def test_failed_input_revert_keeps_every_control(self):
        # Installed with --single-layout, then input.lua edited by hand.
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        self.input.write_bytes(self.input.read_bytes() + b'-- edited\n')
        state = self.setup.load_state()
        state['link'] = self.setup.WIDGET
        self.setup.save_state(state)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(self.setup.revert(), 1)
        self.assertIn('rasklad-controls', out.getvalue())
        self.assertEqual(fake.calls, 1)  # Fcitx's layouts not taken back
        state = self.setup.load_state()
        self.assertIn(self.setup.INPUT, state['files'])
        self.assertEqual(state['link'], self.setup.WIDGET)

    def test_revert_waits_until_hyprland_takes_the_file(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        self.setup.hypr_session = lambda: 'running'
        self.setup.hypr_layouts_back = lambda *a: False  # reload failed or still one layout
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(self.setup.revert(), 1)
        self.assertIn('has not taken the restored input.lua', out.getvalue())
        self.assertEqual(self.input.read_bytes(), b'-- user file\n')  # the file is back
        self.assertTrue(self.setup.load_state().get('await_hypr'))
        self.assertEqual(fake.calls, 1)  # Fcitx's layouts still there
        self.setup.hypr_layouts_back = lambda *a: True
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.setup.revert(), 0)
        self.assertEqual(fake.calls, 2)  # now Fcitx goes back too
        self.assertFalse(os.path.exists(self.setup.STATE_FILE))

    def test_revert_waits_while_hyprland_does_not_answer(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        self.setup.hypr_session = lambda: 'unknown'
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.setup.revert(), 1)
            self.assertEqual(self.setup.revert(), 1)  # the retry waits too
        self.assertEqual(fake.calls, 1)
        self.assertTrue(self.setup.load_state().get('await_hypr'))

    def test_layouts_changed_while_asking(self):
        self.hypr('us,ru', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        real = self.setup.fcitx_plan
        def plan_then_kz(bus, *current):
            self.hypr('us,ru,kz', variant=',,')  # after the question
            return real(bus, *current)
        self.setup.fcitx_plan = plan_then_kz
        code, out = self.install(fake, single_layout=True)
        self.assertEqual(code, 1)
        self.assertIn('layouts changed while waiting', out)
        self.assertEqual(self.input.read_bytes(), b'-- user file\n')

    def test_rollback_keeps_controls_until_hyprland_takes_input(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.setup.hypr_running = lambda: True
        errors = iter([[], ['new error']])
        self.setup.hypr_errors = lambda: next(errors)
        self.setup.hypr_layouts_back = lambda *a: False
        self.setup.run = lambda argv, timeout=5: None
        code, out = self.install(fake, single_layout=True)
        self.assertEqual(code, 1)
        self.assertIn('has not taken the restored input.lua', out)
        self.assertEqual(self.input.read_bytes(), b'-- user file\n')
        self.assertTrue(self.setup.load_state().get('await_hypr'))

    def test_save_during_the_last_check_is_kept(self):
        self.hypr('us,ru', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        real = self.setup.input_plan
        calls = []
        def plan_and_save(data=None, written=None):
            calls.append(1)
            if len(calls) == 2:  # the re-check just before writing
                self.input.write_bytes(b'-- saved meanwhile\n')
            return real(data, written)
        self.setup.input_plan = plan_and_save
        code, out = self.install(fake, single_layout=True)
        self.assertEqual(code, 1)
        self.assertIn('edited while waiting', out)
        self.assertEqual(self.input.read_bytes(), b'-- saved meanwhile\n')

    def test_session_from_a_tty_needs_proof_hyprland_is_gone(self):
        self.setup = load_setup(self.tmp.name)
        self.setup.hypr_running = lambda: False
        runtime = Path(self.tmp.name) / 'run'
        saved = {k: os.environ.get(k) for k in ('XDG_RUNTIME_DIR', 'HYPRLAND_INSTANCE_SIGNATURE')}
        self.addCleanup(lambda: [os.environ.pop(k, None) if v is None else os.environ.__setitem__(k, v)
                                 for k, v in saved.items()])
        os.environ['XDG_RUNTIME_DIR'] = str(runtime)
        os.environ.pop('HYPRLAND_INSTANCE_SIGNATURE', None)
        self.assertEqual(self.setup.hypr_session(), 'none')
        (runtime / 'hypr' / 'abc').mkdir(parents=True)
        path = str(runtime / 'hypr' / 'abc' / '.socket.sock')
        stale = socket.socket(socket.AF_UNIX)
        stale.bind(path)
        stale.close()  # left behind by a crash: connections refused
        self.assertEqual(self.setup.hypr_session(), 'none')
        os.unlink(path)
        live = socket.socket(socket.AF_UNIX)
        self.addCleanup(live.close)
        live.bind(path)
        live.listen(1)
        self.assertEqual(self.setup.hypr_session(), 'unknown')
        os.environ['HYPRLAND_INSTANCE_SIGNATURE'] = 'abc'
        self.assertEqual(self.setup.hypr_session(), 'unknown')

    def test_block_removed_by_hand_releases_the_controls(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        self.input.write_bytes(b'-- user file\n-- my own edit\n')  # our block taken out
        self.setup.hypr_session = lambda: 'running'
        self.setup.hypr_layouts_back = lambda *a: True
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.setup.revert(), 0)  # nothing of ours left to undo
        self.assertEqual(self.input.read_bytes(), b'-- user file\n-- my own edit\n')
        self.assertEqual(fake.calls, 2)  # Fcitx back as it was
        self.assertFalse(os.path.exists(self.setup.STATE_FILE))

    def test_single_layout_flag_still_asks(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')])  # only the layout step left
        self.setup.fcitx = lambda: fake.bus()
        out = io.StringIO()
        with contextlib.redirect_stdout(out), mock.patch.object(self.setup.sys.stdin, 'isatty', return_value=False):
            with self.assertRaises(self.setup.Refused):  # a script needs --yes as well
                self.setup.install(False, single_layout=True)
        self.assertEqual(self.input.read_bytes(), b'-- user file\n')
        with contextlib.redirect_stdout(out), mock.patch.object(self.setup.sys.stdin, 'isatty', return_value=True), \
             mock.patch('builtins.input', return_value='n') as asked:
            self.assertEqual(self.setup.install(False, single_layout=True), 0)
        self.assertIn('[Y/n]', asked.call_args[0][0])
        self.assertEqual(self.input.read_bytes(), b'-- user file\n')
        with contextlib.redirect_stdout(out), mock.patch.object(self.setup.sys.stdin, 'isatty', return_value=True), \
             mock.patch('builtins.input', return_value=''):
            self.assertEqual(self.setup.install(False, single_layout=True), 0)  # Enter: the flag's default
        self.assertIn(b'kb_layout = "us"', self.input.read_bytes())
        self.assertEqual(self.setup.load_state()['hypr_layout'], 'ru,us')

    def test_layouts_back_means_the_recorded_ones(self):
        state = {'hypr_layout': 'ru,us'}
        self.setup.run = lambda argv, timeout=5: mock.Mock(returncode=0)
        self.hypr('us', options='grp:alts_toggle')  # one layout and a toggle: no Cyrillic
        self.assertFalse(self.setup.hypr_layouts_back(state))
        self.hypr('ru,us')
        self.assertTrue(self.setup.hypr_layouts_back(state))
        self.hypr('us', options='grp:alts_toggle')
        self.assertTrue(self.setup.hypr_layouts_back({'hypr_layout': 'us'}))  # what they had
        self.hypr('us', options='compose:caps')  # our block still loaded: toggle gone
        self.assertFalse(self.setup.hypr_layouts_back({'hypr_layout': 'us'}))

    def test_state_from_before_hypr_layout_still_reverts(self):
        self.setup.run = lambda argv, timeout=5: mock.Mock(returncode=0)
        self.hypr('us', options='compose:caps,grp:alts_toggle')  # what they had: us and a toggle
        self.assertTrue(self.setup.hypr_layouts_back({}))
        self.hypr('us', options='compose:caps')  # still our one-layout block
        self.assertFalse(self.setup.hypr_layouts_back({}))

    def test_recorded_layout_ignores_whitespace(self):
        self.hypr('ru,\tus', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.install(fake, single_layout=True)
        self.assertEqual(self.setup.load_state()['hypr_layout'], 'ru,us')

    def test_revert_without_input_entry_ignores_odd_input_path(self):
        state = self.setup.load_state()
        self.setup.save_state(state)
        self.input.unlink()
        self.input.mkdir()  # a directory where input.lua would be
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.setup.revert(), 0)

    def test_input_moved_to_a_symlink_releases_the_controls(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        clean = Path(self.tmp.name) / 'dotfiles-input.lua'
        clean.write_bytes(b'-- dotfiles\n')
        self.input.unlink()
        self.input.symlink_to(clean)
        self.setup.hypr_session = lambda: 'running'
        self.setup.hypr_layouts_back = lambda *a: True
        with contextlib.redirect_stdout(io.StringIO()):
            self.setup.revert()
        self.assertTrue(self.input.is_symlink())
        self.assertEqual(clean.read_bytes(), b'-- dotfiles\n')
        self.assertEqual(fake.calls, 2)  # Fcitx back

    def test_input_deleted_releases_the_controls(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        self.input.unlink()  # settings moved to another include
        self.setup.hypr_session = lambda: 'running'
        self.setup.hypr_layouts_back = lambda *a: True
        with contextlib.redirect_stdout(io.StringIO()):
            self.setup.revert()
        self.assertFalse(self.input.exists())  # not recreated
        self.assertEqual(fake.calls, 2)

    def test_odd_input_paths_release_the_controls(self):
        for kind in ('dangling', 'directory'):
            with self.subTest(kind):
                self.setUp()
                self.hypr('ru,us', variant=',')
                fake = FakeFcitx([('keyboard-us', '')])
                self.assertEqual(self.install(fake, single_layout=True)[0], 0)
                self.input.unlink()
                if kind == 'dangling':
                    self.input.symlink_to(Path(self.tmp.name) / 'gone.lua')
                else:
                    self.input.mkdir()
                self.setup.hypr_session = lambda: 'running'
                self.setup.hypr_layouts_back = lambda *a: True
                with contextlib.redirect_stdout(io.StringIO()):
                    self.setup.revert()
                self.assertEqual(fake.calls, 2, kind)
                self.assertTrue(self.input.is_symlink() if kind == 'dangling' else self.input.is_dir())

    def test_unreadable_input_goes_to_the_live_check(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        real_read = self.setup.read
        def denied(path):
            if path == self.setup.INPUT:
                raise PermissionError(13, 'Permission denied')
            return real_read(path)
        self.setup.read = denied
        self.setup.input_block_present = lambda: False  # cannot be read either
        self.setup.hypr_session = lambda: 'running'
        self.setup.hypr_layouts_back = lambda *a: True
        with contextlib.redirect_stdout(io.StringIO()):
            self.setup.revert()
        self.assertEqual(fake.calls, 2)

    def test_fifo_does_not_block(self):
        self.input.unlink()
        os.mkfifo(self.input)
        self.assertFalse(self.setup.input_block_present())

    def test_block_removed_without_hyprland_keeps_the_controls(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.assertEqual(self.install(fake, single_layout=True)[0], 0)
        self.input.write_bytes(b'-- user file\n-- my own edit\n')
        self.setup.hypr_session = lambda: 'none'  # cannot prove the layouts are back
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.setup.revert(), 1)
        self.assertEqual(fake.calls, 1)
        self.assertIn(self.setup.INPUT, self.setup.load_state()['files'])

    def test_usage_rejects_single_layout_elsewhere(self):
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(self.setup.main(['--check', '--single-layout']), 2)
            self.assertEqual(self.setup.main(['--install', '--yes', '--yes']), 2)


if __name__ == '__main__':
    unittest.main()
