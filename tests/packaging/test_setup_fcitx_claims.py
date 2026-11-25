# SPDX-License-Identifier: MIT
"""SYS-272: rasklad-setup records, per Fcitx group, only the methods it added.

--revert and the auto-rollback take exactly those rows out of the group as it is
now; the user's other methods, their order and the group's default layout stay.
Stand-ins for hyprctl and Fcitx, in a temporary XDG_CONFIG_HOME and XDG_STATE_HOME.
"""
import contextlib
import io
import unittest
from pathlib import Path

from test_setup_layouts import Base, FakeFcitx

US, RU, DE, PINYIN = ('keyboard-us', ''), ('keyboard-ru', ''), ('keyboard-de', ''), ('pinyin', '')


class Claims(Base):
    def setUp(self):
        super().setUp()
        self.setup.shell_uses_widget = lambda: False
        self.hypr('us', options='')  # nothing for input.lua: only Fcitx changes

    def run_quiet(self, fn, *a):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = fn(*a)
        return code, out.getvalue()

    def install_with(self, fake, bus=None):
        self.setup.fcitx = bus or fake.bus
        return self.run_quiet(self.setup.install, True)

    def revert_with(self, fake):
        self.setup.fcitx = fake.bus
        return self.run_quiet(self.setup.revert)

    def claim(self, name='Default'):
        groups = (self.setup.load_state().get('fcitx') or {}).get('groups', {})
        return [tuple(r) for r in groups.get(name, {}).get('added', [])]

    # --- the bug and the case that broke the first fix --------------------------

    def test_reinstall_after_replacing_ru_keeps_the_users_layout(self):
        fake = FakeFcitx([US])
        self.assertEqual(self.install_with(fake)[0], 0)
        fake.items = [US, DE]  # the user replaced ru with de
        self.assertEqual(self.install_with(fake)[0], 0)
        self.assertEqual(fake.items, [US, DE, RU])
        code, out = self.revert_with(fake)
        self.assertEqual(code, 0, out)
        self.assertEqual(fake.items, [US, DE])

    def test_pinyin_reinstall_after_removing_ru_gives_pinyin_back(self):
        fake = FakeFcitx([PINYIN])
        self.install_with(fake)
        self.assertEqual(fake.items, [PINYIN, US, RU])
        fake.items = [PINYIN, US]
        self.install_with(fake)
        self.assertEqual(sorted(self.claim()), sorted([US, RU]))
        self.assertEqual(self.revert_with(fake)[0], 0)
        self.assertEqual(fake.items, [PINYIN])

    # --- what is ours ----------------------------------------------------------

    def test_nothing_claimed_when_both_were_there(self):
        fake = FakeFcitx([US, RU])
        self.install_with(fake)
        self.assertEqual(fake.calls, 0)
        self.assertEqual(self.claim(), [])

    def test_user_order_and_other_methods_survive_revert(self):
        fake = FakeFcitx([US])
        self.install_with(fake)
        fake.items = [RU, DE, US]
        self.revert_with(fake)
        self.assertEqual(fake.items, [DE, US])

    def test_row_given_another_layout_is_the_users(self):
        fake = FakeFcitx([US])
        self.install_with(fake)
        fake.items = [US, ('keyboard-ru', 'ru(phonetic)')]
        code, out = self.revert_with(fake)
        self.assertEqual(code, 0, out)
        self.assertEqual(fake.items, [US, ('keyboard-ru', 'ru(phonetic)')])

    def test_removed_row_is_forgotten_at_the_next_install(self):
        # us claimed earlier, the user removed it; this install adds it again: ours.
        fake = FakeFcitx([DE])
        self.install_with(fake)
        fake.items = [DE, RU]  # us taken out by hand
        self.install_with(fake)
        self.assertEqual(sorted(self.claim()), sorted([US, RU]))

    def test_group_default_layout_is_never_changed(self):
        fake = FakeFcitx([US])
        self.install_with(fake)
        self.assertEqual(fake.writes[-1][1], 'us')
        fake.layout = 'de'  # the user changes the group's default layout
        self.revert_with(fake)
        self.assertEqual(fake.layout, 'de')
        self.assertEqual(fake.writes[-1][1], 'de')

    # --- writes that fail or cannot be confirmed --------------------------------

    def test_write_without_effect_claims_nothing(self):
        fake = FakeFcitx([US], apply=False)
        code, out = self.install_with(fake)
        self.assertEqual(code, 1)
        self.assertRegex(out, r'failed\s+fcitx-group')
        self.assertEqual(self.claim(), [])
        fake.apply, fake.items = True, [US, RU]  # the user adds ru by hand
        self.revert_with(fake)
        self.assertEqual(fake.items, [US, RU])

    def test_failed_reply_that_landed_is_still_ours(self):
        fake = FakeFcitx([US])
        bus = fake.bus()
        landed = bus['set_group']
        bus['set_group'] = lambda *a: (landed(*a), False)[1]
        code, out = self.install_with(fake, bus=lambda: bus)
        self.assertEqual(code, 1)
        self.assertEqual(self.claim(), [RU])
        self.revert_with(fake)
        self.assertEqual(fake.items, [US])

    def test_unconfirmed_write_keeps_the_claim(self):
        fake = FakeFcitx([US])
        bus = fake.bus()
        bus['group_by'] = lambda name: None  # Fcitx stops answering after the write
        code, out = self.install_with(fake, bus=lambda: bus)
        self.assertEqual(code, 1)
        self.assertRegex(out, r'unknown\s+fcitx-group')
        self.assertEqual(self.claim(), [RU])

    def test_failed_reinstall_keeps_the_earlier_claim(self):
        fake = FakeFcitx([PINYIN])
        self.install_with(fake)
        fake.items = [PINYIN, US]  # ru removed by hand
        fake.apply = False
        self.install_with(fake)
        self.assertEqual(self.claim(), [US])
        fake.apply = True
        self.revert_with(fake)
        self.assertEqual(fake.items, [PINYIN])

    def test_edit_while_the_question_waits_is_kept(self):
        fake = FakeFcitx([US])
        bus = fake.bus()
        first = bus['group']
        calls = iter(range(100))

        def group():
            if next(calls) == 1:  # the read right before the write
                fake.items = [US, DE]
            return first()
        bus['group'] = group
        self.install_with(fake, bus=lambda: bus)
        self.assertEqual(fake.items, [US, DE, RU])

    # --- auto-rollback ----------------------------------------------------------

    def errors_after(self):
        self.setup.hypr_running = lambda: True
        errors = iter([[], ['bindings.lua: error']])
        self.setup.hypr_errors = lambda: next(errors, [])
        self.setup.bindings_plan = lambda data=None: ('missing', b'-- rasklad rule\n')
        Path(self.setup.BINDINGS).write_bytes(b'-- user bindings\n')

    def test_rollback_takes_out_only_this_runs_rows(self):
        fake = FakeFcitx([US])
        self.setup.fcitx = fake.bus
        state = self.setup.new_state()
        self.setup.set_claim(state, 'Default', [list(US)])  # an earlier install's us
        self.setup.save_state(state)
        fake.items = [US, DE]
        self.errors_after()
        code, out = self.install_with(fake)
        self.assertEqual(code, 1, out)
        self.assertEqual(fake.items, [US, DE])
        self.assertEqual(self.claim(), [US])

    def test_rollback_keeps_a_users_add_made_meanwhile(self):
        fake = FakeFcitx([US])
        bus = fake.bus()
        write = bus['set_group']
        first = [True]

        def set_group(name, layout, items):
            ok = write(name, layout, items)
            if first[0]:  # right after our add, the user adds de
                first[0] = False
                fake.items = fake.items + [DE]
            return ok
        bus['set_group'] = set_group
        self.errors_after()
        code, out = self.install_with(fake, bus=lambda: bus)
        self.assertEqual(code, 1, out)
        self.assertEqual(fake.items, [US, DE])

    def test_rollback_keeps_rows_while_the_widget_is_used(self):
        fake = FakeFcitx([US])
        self.setup.shell_uses_widget = lambda: True
        self.errors_after()
        code, out = self.install_with(fake)
        self.assertEqual(code, 1)
        self.assertEqual(fake.items, [US, RU])
        self.assertEqual(self.claim(), [RU])

    # --- revert guards ----------------------------------------------------------

    def test_revert_keeps_rows_while_the_widget_is_used(self):
        fake = FakeFcitx([US])
        self.install_with(fake)
        self.setup.shell_uses_widget = lambda: True
        code, out = self.revert_with(fake)
        self.assertEqual(code, 1)
        self.assertIn('shell.json still uses the bar widget', out)
        self.assertEqual(fake.items, [US, RU])
        self.assertEqual(self.claim(), [RU])

    def test_never_writes_an_empty_group(self):
        fake = FakeFcitx([PINYIN])
        self.install_with(fake)
        fake.items = [US, RU]  # the user removed pinyin
        calls = fake.calls
        code, out = self.revert_with(fake)
        self.assertEqual(code, 1)
        self.assertIn('empty', out)
        self.assertEqual(fake.calls, calls)
        self.assertEqual(sorted(self.claim()), sorted([US, RU]))

    def test_a_group_that_is_gone_is_reported(self):
        fake = FakeFcitx([US])
        self.install_with(fake)
        fake.name = 'Renamed'
        calls = fake.calls
        code, out = self.revert_with(fake)
        self.assertEqual(code, 0, out)
        self.assertIn('the group Default is gone', out)
        self.assertEqual(fake.calls, calls)

    def test_revert_edits_the_recorded_group_not_the_current_one(self):
        fake = FakeFcitx([US])
        self.install_with(fake)
        other = FakeFcitx([PINYIN])
        other.name = 'Work'
        bus = fake.bus()
        bus['group'] = other.bus()['group']  # Work is current now
        bus['groups'] = lambda: ['Default', 'Work']
        self.setup.fcitx = lambda: bus
        self.run_quiet(self.setup.revert)
        self.assertEqual(fake.items, [US])
        self.assertEqual(other.items, [PINYIN])

    # --- state written before SYS-272 -------------------------------------------

    def old_state(self, before, after):
        state = self.setup.new_state()
        state['fcitx'] = {'group': 'Default', 'layout': 'us', 'before': [list(i) for i in before],
                          'after': [list(i) for i in after], 'after_layout': 'us'}
        self.setup.save_state(state)

    def test_old_record_from_the_bug_keeps_de(self):
        self.old_state([US], [US, DE, RU])
        self.assertEqual(self.claim(), [RU])
        fake = FakeFcitx([US, DE, RU])
        self.assertEqual(self.revert_with(fake)[0], 0)
        self.assertEqual(fake.items, [US, DE])

    def test_old_clean_record_reverts_both(self):
        self.old_state([PINYIN], [PINYIN, US, RU])
        fake = FakeFcitx([PINYIN, US, RU])
        self.revert_with(fake)
        self.assertEqual(fake.items, [PINYIN])

    def test_old_record_never_claims_a_row_with_its_own_layout(self):
        # main: de only; install; the user gives ru a layout and removes us;
        # reinstall wrote that custom ru into after.
        custom = ('keyboard-ru', 'ru(phonetic)')
        self.old_state([DE], [DE, custom, US])
        self.assertEqual(self.claim(), [US])
        fake = FakeFcitx([DE, custom, US])  # still as that install left it
        self.revert_with(fake)
        self.assertEqual(fake.items, [DE, custom])

    def test_a_group_named_before_is_just_a_name(self):
        fake = FakeFcitx([US])
        fake.name = 'before'
        self.install_with(fake)
        self.assertEqual(self.claim('before'), [RU])
        self.assertEqual(self.revert_with(fake)[0], 0)
        self.assertEqual(fake.items, [US])

    def test_install_with_both_there_forgets_a_changed_row(self):
        fake = FakeFcitx([US])
        self.install_with(fake)
        fake.items = [US, ('keyboard-ru', 'ru(phonetic)')]
        self.install_with(fake)
        self.assertEqual(self.claim(), [])
        fake.items = [US, RU]  # the user puts the plain ru back
        self.revert_with(fake)
        self.assertEqual(fake.items, [US, RU])

    def test_ru_put_back_while_the_question_waits_is_the_users(self):
        fake = FakeFcitx([US])
        self.install_with(fake)
        fake.items = [US]  # ru removed by hand
        bus = fake.bus()
        first = bus['group']
        calls = iter(range(100))

        def group():
            if next(calls) == 1:  # after this run has seen it gone (its first read)
                fake.items = [US, RU]
            return first()
        bus['group'] = group
        self.install_with(fake, bus=lambda: bus)
        self.assertEqual(self.claim(), [])
        self.revert_with(fake)
        self.assertEqual(fake.items, [US, RU])

    def test_old_record_counts_only_if_the_group_is_as_it_left_it(self):
        # main: an install whose write failed still recorded ru in after; the
        # user then added ru and de by hand. Nothing of that is ours.
        self.old_state([US], [US, RU])
        fake = FakeFcitx([US, RU, DE])
        code, out = self.revert_with(fake)
        self.assertIn('changed since an older install', out)
        self.assertEqual(code, 1)
        self.assertEqual(fake.items, [US, RU, DE])
        self.assertEqual(self.revert_with(fake)[0], 0)  # nothing left to do

    def test_old_record_needs_its_default_layout_too(self):
        self.old_state([US], [US, RU])
        fake = FakeFcitx([US, RU])
        fake.layout = 'de'  # the user changed only the group's default layout
        code, out = self.revert_with(fake)
        self.assertIn('changed since an older install', out)
        self.assertEqual(fake.items, [US, RU])

    def test_working_notes_never_reach_the_state_file(self):
        state = self.setup.new_state()
        state['_drop'] = ['/nonexistent']
        self.setup.save_state(state)
        self.assertNotIn('_drop', self.setup.load_state())

    def test_removal_seen_at_the_last_check_makes_a_re_add_the_users(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([US, RU])
        self.setup.fcitx = fake.bus
        state = self.setup.new_state()
        self.setup.set_claim(state, 'Default', [list(RU)])
        self.setup.save_state(state)
        bus = fake.bus()
        first = bus['group']
        calls = iter(range(100))

        def group():
            if next(calls) == 1:  # gone at the last check before input.lua
                fake.items = [US]
            return first()
        bus['group'] = group
        self.setup.fcitx = lambda: bus
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.setup.install(True, single_layout=True)
        self.assertIn('Fcitx does not have both layouts', out.getvalue())
        self.assertEqual(self.claim(), [])

    def test_rollback_spares_a_row_this_run_saw_removed(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([US])
        bus = fake.bus()
        first = bus['group']
        calls = iter(range(100))

        def group():
            if next(calls) == 2:  # the last check before input.lua: ru gone
                fake.items = [US]
            return first()
        bus['group'] = group
        self.setup.fcitx = lambda: bus
        self.errors_after()
        back = [True]

        def reload(argv, timeout=5):  # the user adds ru back before the rollback
            if back[0]:
                back[0] = False
                fake.items = [US, RU]
        self.setup.run = reload
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.setup.install(True, single_layout=True)
        self.assertEqual(fake.items, [US, RU], out.getvalue())

    def test_rollback_never_revives_a_cleared_earlier_claim(self):
        self.hypr('ru,us', variant=',')
        fake = FakeFcitx([DE, US])
        state = self.setup.new_state()
        self.setup.set_claim(state, 'Default', [list(US)])  # an earlier install's us
        self.setup.save_state(state)
        bus = fake.bus()
        first = bus['group']
        calls = iter(range(100))

        def group():
            if next(calls) == 2:  # the last check: us gone (and ru too)
                fake.items = [DE]
            return first()
        bus['group'] = group
        self.setup.fcitx = lambda: bus
        self.errors_after()
        back = [True]

        def reload(argv, timeout=5):  # the user adds us back before the rollback
            if back[0]:
                back[0] = False
                fake.items = [DE, US]
        self.setup.run = reload
        with contextlib.redirect_stdout(io.StringIO()):
            self.setup.install(True, single_layout=True)
        self.assertEqual(self.claim(), [])
        self.revert_with(fake)
        self.assertEqual(fake.items, [DE, US])

    def test_other_tracked_group_read_while_waiting_is_settled(self):
        fake = FakeFcitx([US])
        self.install_with(fake)  # Default: ru ours
        work = FakeFcitx([PINYIN])
        work.name = 'Work'
        state = self.setup.load_state()
        self.setup.set_claim(state, 'Work', [list(US)])  # an older claim in Work
        self.setup.save_state(state)
        fake.items = [US]  # ru removed from Default: missing again
        bus = fake.bus()
        calls = iter(range(100))

        def group():  # Work becomes current while the question waits
            return fake.info('Default') if next(calls) == 0 else work.info('Work')
        bus['group'] = group
        self.setup.fcitx = lambda: bus
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.setup.install(True)
        self.assertIn('another group became current', out.getvalue())
        self.assertEqual(self.claim('Work'), [])  # us is not in Work: not ours

    def test_revert_keeps_the_claim_when_fcitx_did_not_apply(self):
        fake = FakeFcitx([US])
        self.install_with(fake)
        fake.apply = False  # Fcitx answers but keeps the group
        code, out = self.revert_with(fake)
        self.assertEqual(code, 1)
        self.assertRegex(out, r'failed\s+fcitx-group')
        self.assertEqual(self.claim(), [RU])

    def test_old_record_with_the_group_back_as_before_is_clean(self):
        self.old_state([US], [US, RU])
        fake = FakeFcitx([US])  # never applied, or already put back
        code, out = self.revert_with(fake)
        self.assertEqual(code, 0, out)
        self.assertNotIn('changed since', out)
        self.assertEqual(fake.calls, 0)

    def test_partly_removed_rows_are_settled_one_by_one(self):
        fake = FakeFcitx([PINYIN])
        self.install_with(fake)  # us and ru ours
        bus = fake.bus()

        def half(name, layout, items):  # Fcitx takes out us but keeps ru
            fake.calls += 1
            fake.items = [PINYIN, RU]
            return True
        bus['set_group'] = half
        self.setup.fcitx = lambda: bus
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.setup.revert(), 1)
        self.assertEqual(self.claim(), [RU])
        fake.items = [PINYIN, US, RU]  # the user adds us back
        self.revert_with(fake)
        self.assertEqual(fake.items, [PINYIN, US])

    def test_record_claiming_other_methods_is_refused(self):
        state = self.setup.new_state()
        state['fcitx'] = {'groups': {'Default': {'added': [list(DE)]}}}
        self.setup.save_state(state)
        with self.assertRaises(self.setup.Refused):
            self.setup.load_state()


if __name__ == '__main__':
    unittest.main()
