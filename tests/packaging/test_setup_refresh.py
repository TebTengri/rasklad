# SPDX-License-Identifier: MIT
"""SYS-270: rasklad-setup after omarchy refresh and after pacman -R.

Stand-ins for hyprctl and Fcitx, in a temporary XDG_CONFIG_HOME and
XDG_STATE_HOME: the user's own files are never touched.
"""
import contextlib
import io
import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_setup_layouts import Base, FakeFcitx, load_setup  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SHELL = b'{"bar": {"layout": {"right": [{"id": "omarchy.keyboard-layout"}]}}}\n'


class Refresh(Base):
    def setUp(self):
        super().setUp()
        config = Path(self.tmp.name) / 'config'
        self.bindings = config / 'hypr' / 'bindings.lua'
        self.bindings.write_bytes(b'-- user bindings\n')
        (config / 'omarchy').mkdir()
        self.shell = config / 'omarchy' / 'shell.json'
        self.shell.write_bytes(SHELL)
        # Base stubs these two plans out; here they are the real ones.
        real = load_setup(self.tmp.name)
        real.lua_ok = lambda data: True
        self.setup.bindings_plan = real.bindings_plan
        self.setup.shell_plan = real.shell_plan
        self.hypr('us', options='compose:caps')  # nothing for input.lua
        self.fake = FakeFcitx([('keyboard-us', ''), ('keyboard-ru', '')])
        self.setup.fcitx = lambda: self.fake.bus()

    def quiet(self, fn, *args, **kw):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = fn(*args, **kw)
        return code, out.getvalue()

    def refresh(self):
        # omarchy refresh: both files back to Omarchy's own.
        self.bindings.write_bytes(b'-- omarchy bindings\n')
        self.shell.write_bytes(SHELL)

    def test_install_after_refresh_starts_over(self):
        self.assertEqual(self.quiet(self.setup.install, True)[0], 0)
        self.assertIn(self.setup.BEGIN.encode(), self.bindings.read_bytes())
        self.assertIn(b'rasklad.layout', self.shell.read_bytes())
        self.refresh()
        code, out = self.quiet(self.setup.install, True)
        self.assertEqual(code, 0, out)
        self.assertIn('omarchy refresh', out)
        self.assertNotIn('run --revert first', out)
        self.assertIn(self.setup.BEGIN.encode(), self.bindings.read_bytes())
        # Revert goes back to the refreshed files, not the ones from before.
        self.quiet(self.setup.revert)
        self.assertEqual(self.bindings.read_bytes(), b'-- omarchy bindings\n')
        self.assertEqual(self.shell.read_bytes(), SHELL)

    def test_declining_after_refresh_keeps_the_records(self):
        from unittest import mock
        self.quiet(self.setup.install, True)
        before = self.setup.load_state()['files']
        self.refresh()
        with mock.patch.object(self.setup.sys.stdin, 'isatty', return_value=True), \
             mock.patch('builtins.input', return_value='n'):
            code, out = self.quiet(self.setup.install, False)
        self.assertIn('Nothing changed', out)
        self.assertEqual(self.setup.load_state()['files'], before)

    def test_revert_after_refresh_is_clean(self):
        self.quiet(self.setup.install, True)
        self.refresh()
        code, out = self.quiet(self.setup.revert)
        self.assertEqual(code, 0, out)
        self.assertEqual(self.bindings.read_bytes(), b'-- omarchy bindings\n')
        self.assertFalse(os.path.exists(self.setup.STATE_FILE))

    def test_widget_link_stays_while_shell_json_uses_it(self):
        self.quiet(self.setup.install, True)
        state = self.setup.load_state()
        state['link'] = self.setup.WIDGET
        self.setup.save_state(state)
        self.shell.write_bytes(self.shell.read_bytes() + b'\n')  # edited: cannot be restored
        code, out = self.quiet(self.setup.revert)
        self.assertEqual(code, 1)
        self.assertIn('shell.json still uses the widget', out)
        self.assertEqual(self.setup.load_state()['link'], self.setup.WIDGET)

    def test_input_entry_survives_install_after_refresh(self):
        # SYS-268 safeguard: only --revert's live check may forget input.lua.
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.setup.fcitx = lambda: fake.bus()
        self.assertEqual(self.quiet(self.setup.install, True, single_layout=True)[0], 0)
        self.input.write_bytes(b'-- omarchy input\n')
        self.hypr('us', options='compose:caps')  # Hyprland still on the old block
        self.quiet(self.setup.install, True)
        self.assertIn(self.setup.INPUT, self.setup.load_state()['files'])

    def test_only_copy_is_kept_when_the_sidecar_is_older(self):
        sidecar = Path(str(self.bindings) + '.rasklad.bak')
        sidecar.write_bytes(b'-- a much older bindings.lua\n')  # not ours: left alone
        self.quiet(self.setup.install, True)
        self.refresh()
        code, out = self.quiet(self.setup.install, True)
        kept = [p for p in Path(self.setup.BACKUP_DIR).iterdir() if p.name.endswith('.before-reset')]
        self.assertEqual(len(kept), 1, out)
        self.assertEqual(kept[0].read_bytes(), b'-- user bindings\n')
        self.assertIn(str(kept[0]), out)

    def test_escaped_widget_id_counts(self):
        self.assertTrue(self.setup.names_widget(b'{"bar": {"layout": [{"id": "rasklad\\u002elayout"}]}}'))
        self.assertFalse(self.setup.names_widget(SHELL))

    def test_symlinked_shell_json_is_read_through(self):
        real = Path(self.tmp.name) / 'dotfiles-shell.json'
        real.write_bytes(b'{"bar": {"layout": [{"id": "rasklad.layout"}]}}')
        self.shell.unlink()
        self.shell.symlink_to(real)
        self.assertTrue(self.setup.shell_uses_widget())
        real.write_bytes(SHELL)
        self.assertFalse(self.setup.shell_uses_widget())
        real.unlink()  # dangling
        self.assertFalse(self.setup.shell_uses_widget())

    def test_reset_input_releases_after_a_reload(self):
        # omarchy refresh gave the stock single us: the old layouts never come back,
        # but our block is gone and Hyprland has reloaded without it.
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.setup.fcitx = lambda: fake.bus()
        self.assertEqual(self.quiet(self.setup.install, True, single_layout=True)[0], 0)
        self.input.write_bytes(b'-- omarchy input\n')
        self.hypr('us', options='compose:caps')
        self.setup.hypr_session = lambda: 'running'
        code, out = self.quiet(self.setup.revert)
        self.assertEqual(code, 0, out)
        self.assertEqual(fake.calls, 2)
        self.assertFalse(os.path.exists(self.setup.STATE_FILE))

    def test_reset_input_without_session_says_where_to_run(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([('keyboard-us', '')])
        self.setup.fcitx = lambda: fake.bus()
        self.quiet(self.setup.install, True, single_layout=True)
        self.input.write_bytes(b'-- omarchy input\n')
        self.setup.hypr_session = lambda: 'unknown'
        code, out = self.quiet(self.setup.revert)
        self.assertEqual(code, 1)
        self.assertIn('inside the Hyprland session', out)
        self.assertNotIn('remove the rasklad-setup block', out)

    def test_override_without_its_begin_line_still_counts(self):
        self.hypr('ru,us', variant=',')
        status, new = self.setup.input_plan(b'-- user file\n')
        self.input.write_bytes(new.replace(self.setup.BEGIN.encode(), b'-- (edited)'))
        self.assertTrue(self.setup.input_block_present())

    def test_any_trace_counts_in_every_version(self):
        v2 = (b'-- user\n-- rasklad-setup layout 2: us kept for the lock screen\n'
              b'hl.config({input = {kb_layout = "us"}})\n')  # BEGIN taken out of a 0.5.21 block
        self.input.write_bytes(v2)
        self.assertTrue(self.setup.input_block_present())
        loader = ('-- user\nlocal rasklad = "%s"\ndofile(rasklad)\n' % self.setup.RULE).encode()
        self.assertTrue(self.setup.ours_in(self.setup.BINDINGS, loader))
        self.assertFalse(self.setup.ours_in(self.setup.BINDINGS, b'-- omarchy bindings\n'))
        split = b'local r = "/usr/share/rasklad" .. "-fcitx5/hyprland/double-alt.lua"\ndofile(r)\n'
        self.assertTrue(self.setup.ours_in(self.setup.BINDINGS, split))

    def test_reload_with_config_errors_does_not_count(self):
        self.setup = load_setup(self.tmp.name)
        self.setup.run = lambda argv, timeout=5: type('Done', (), {'returncode': 0})()
        self.setup.hypr_errors = lambda: ['input.lua:3: syntax error']
        self.assertFalse(self.setup.hypr_reloaded())
        self.setup.hypr_errors = lambda: []
        self.assertTrue(self.setup.hypr_reloaded())

    def test_shell_json_directory_counts_as_in_use(self):
        self.shell.unlink()
        self.shell.mkdir()
        self.assertTrue(self.setup.shell_uses_widget())

    def test_input_block_is_off_without_the_package(self):
        self.hypr('ru,us', variant=',')
        status, new = self.setup.input_plan(b'-- user file\n')
        self.assertEqual(status, 'missing')
        text = new.decode()
        self.assertIn('io.open("%s", "r")' % self.setup.RULE, text)
        self.assertLess(text.index('if rasklad_package then'), text.index('hl.config'))
        self.assertEqual(self.setup.input_plan(new)[0], 'ok')  # still recognised as ours
        lua = shutil.which('lua') or shutil.which('lua5.4')
        if lua:
            # Without the package file the block calls nothing.
            # The package file is looked up under a path that does not exist here.
            gone = text.replace(self.setup.RULE, str(Path(self.tmp.name) / 'removed' / 'double-alt.lua'))
            stub = 'hl = {config = function() error("applied") end}\n'
            done = subprocess.run([lua, '-'], input=(stub + gone).encode(), capture_output=True)
            self.assertEqual(done.returncode, 0, done.stderr)
            present = text.replace(self.setup.RULE, str(self.input))  # any readable file
            done = subprocess.run([lua, '-'], input=(stub + present).encode(), capture_output=True)
            self.assertNotEqual(done.returncode, 0)  # applied while the package is there
            done = subprocess.run([lua, '-'], input=(stub + gone).encode(), capture_output=True)
            self.assertEqual(done.returncode, 0, done.stderr)


class Package(unittest.TestCase):
    def test_pkgbuild_ships_the_removal_notice(self):
        pkgbuild = (ROOT / 'packaging/arch/PKGBUILD').read_text()
        self.assertIn('install=rasklad-fcitx5.install', pkgbuild)
        self.assertIn('rasklad-fcitx5.install', (ROOT / 'tools/build_arch_package.sh').read_text())
        path = ROOT / 'packaging/arch/rasklad-fcitx5.install'
        notice = path.read_text()
        self.assertIn('post_remove()', notice)  # read after pacman is done
        self.assertIn('hyprctl reload', notice)
        self.assertEqual(subprocess.run(['bash', '-n', str(path)]).returncode, 0)


if __name__ == '__main__':
    unittest.main()
