# SPDX-License-Identifier: MIT
"""SYS-268: rasklad-setup keeps a layout the lock screen can use.

input_plan() with a stand-in for hyprctl: no file, no Hyprland, no Fcitx.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_setup():
    loader = importlib.machinery.SourceFileLoader('rasklad_setup', str(ROOT / 'packaging/arch/rasklad-setup'))
    spec = importlib.util.spec_from_loader('rasklad_setup', loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class Layouts(unittest.TestCase):
    def plan(self, layout, options='compose:caps,grp:alts_toggle', variant=''):
        setup = load_setup()
        values = {'input:kb_layout': layout, 'input:kb_options': options, 'input:kb_variant': variant}
        setup.hypr_option = values.get
        setup.file_state = lambda path: None
        setup.lua_ok = lambda data: True
        return setup.input_plan(b'-- user file\n')

    def kept(self, block):
        text = block.decode()
        return [line.strip() for line in text[text.index('kb_layout = "'):].split('\n')[:3]]

    def test_us_first(self):
        status, new = self.plan('us,ru', variant=',')
        self.assertEqual(status, 'missing')
        self.assertEqual(self.kept(new)[0], 'kb_layout = "us",')
        self.assertIn('kb_options = "compose:caps"', new.decode())

    def test_ru_first_keeps_us_for_the_lock_screen(self):
        status, new = self.plan('ru,us', variant='winkeys,intl')
        self.assertEqual(status, 'missing')
        kept = self.kept(new)
        self.assertEqual(kept[0], 'kb_layout = "us",')
        self.assertEqual(kept[1], 'kb_variant = "intl",')

    def test_third_layout_left_as_is(self):
        self.assertEqual(self.plan('us,ru,kz'), ('other-layouts', None))

    def test_other_pair_left_as_is(self):
        self.assertEqual(self.plan('de,ru'), ('other-layouts', None))

    def test_without_us_left_as_is(self):
        self.assertEqual(self.plan('ru', options='grp:alts_toggle'), ('other-layouts', None))

    def test_single_us_without_toggle_is_ok(self):
        self.assertEqual(self.plan('us', options='compose:caps'), ('ok', None))

    def test_unknown_without_hyprland(self):
        setup = load_setup()
        setup.hypr_option = lambda name: None
        setup.file_state = lambda path: None
        self.assertEqual(setup.input_plan(b'')[0], 'unknown')

    def test_settings_knows_the_new_state(self):
        core = (ROOT / 'settings/settingscore.cpp').read_text()
        self.assertGreaterEqual(core.count('"other-layouts"'), 2)


if __name__ == '__main__':
    unittest.main()
