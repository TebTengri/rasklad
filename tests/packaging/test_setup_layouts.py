# SPDX-License-Identifier: MIT
"""SYS-268: rasklad-setup keeps a layout the lock screen can use, and asks first.

input_plan() and install() with stand-ins for hyprctl and Fcitx, in a temporary
XDG_CONFIG_HOME and XDG_STATE_HOME: the user's own files are never touched.
"""
import contextlib
import importlib.machinery
import importlib.util
import io
import os
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
    def __init__(self, items, apply=True):
        self.items = items
        self.apply = apply
        self.calls = 0

    def bus(self):
        return {'group': lambda: ('Default', 'us', [list(i) for i in self.items]),
                'set_group': self.set_group, 'status': lambda: None}

    def set_group(self, name, layout, items):
        self.calls += 1
        if self.apply:
            self.items = [tuple(i) for i in items]
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
        self.setup.lua_ok = lambda data: True
        self.setup.hypr_errors = lambda: []
        self.setup.hypr_running = lambda: False
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
        self.setup.hypr_running = lambda: True
        self.setup.hypr_layouts_back = lambda: False  # reload failed or still one layout
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(self.setup.revert(), 1)
        self.assertIn('has not taken the restored input.lua', out.getvalue())
        self.assertEqual(self.input.read_bytes(), b'-- user file\n')  # the file is back
        self.assertTrue(self.setup.load_state().get('await_hypr'))
        self.assertEqual(fake.calls, 1)  # Fcitx's layouts still there
        self.setup.hypr_layouts_back = lambda: True
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.setup.revert(), 0)
        self.assertEqual(fake.calls, 2)  # now Fcitx goes back too
        self.assertFalse(os.path.exists(self.setup.STATE_FILE))

    def test_usage_rejects_single_layout_elsewhere(self):
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(self.setup.main(['--check', '--single-layout']), 2)
            self.assertEqual(self.setup.main(['--install', '--yes', '--yes']), 2)


if __name__ == '__main__':
    unittest.main()
