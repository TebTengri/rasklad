# SPDX-License-Identifier: MIT
"""The diagnostic shim is pinned and preloaded only into the private Fcitx child."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch, MagicMock

ROOT=Path(__file__).resolve().parents[2]


def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module


common=load('core_common',ROOT/'tests/wayland_ime/test_addon_witness_guards.py')
guard=load('abi_guard',ROOT/'tools/core_entry_witness_guard.py')
clients=load('core_clients',ROOT/'tools/native_ime_clients.py')
REFERENCE,EXACT=common.REFERENCE,common.EXACT
REASON='core_entry_witness_requires_querying_addon_witness'
CORE_ADDON=ROOT/'build/chromium-ime/core-entry-addon/raskladlab.so'
SHIM=ROOT/'build/chromium-ime/core-entry-shim/raskladcoreentry.so'
OPTIONS=dict(focus_stress=True,space_telemetry=True,addon_witness=True)


def core_reference_args(**options):
    return common.reference_args(snapshot_fds={'/lab/raskladcoreentry.so':12},**options)


def core_exact_args(**options):
    paths=EXACT.chrome.files();paths.pop('xvfb');paths.pop('keys')
    paths['addon']=ROOT/'build/chromium-ime/profile-addon/raskladlab.so'
    plan={'sysfs':'/private/gpu-metadata/sys','statOnlyPrimary':'/dev/dri/card0',
          'primaryBindMustBeNodevReadOnly':True,'hostSysfsBind':False}
    return EXACT.sandbox_args(Path('/private'),8,plan,paths,{},
        {'/lab/rasklad-wayland-keys':10,'/lab/raskladcoreentry.so':12},**options)


class CoreEntryIntegration(unittest.TestCase):
    def test_core_mode_adds_only_sealed_shim_marker_and_fixed_addon(self):
        for module,full_args,core_args in ((REFERENCE,common.reference_args,core_reference_args),
                                          (EXACT,common.exact_args,core_exact_args)):
            with self.subTest(module=module.__name__),patch.object(module.base,'regular_file'):
                full=full_args(**OPTIONS)
                selected=core_args(**OPTIONS,core_entry_witness=True)
                self.assertNotIn('/lab/raskladcoreentry.so',full)
                self.assertNotIn('LD_PRELOAD',selected)
                self.assertNotIn(str(SHIM),selected)
                self.assertIn(str(CORE_ADDON),selected)
                marker=selected.index('RASKLAD_CORE_ENTRY_WITNESS_LAB')
                self.assertEqual(selected[marker-1:marker+2],['--setenv','RASKLAD_CORE_ENTRY_WITNESS_LAB','1'])
                del selected[marker-1:marker+2]
                shim=selected.index('/lab/raskladcoreentry.so')
                self.assertEqual(selected[shim-4:shim+1],['--perms','0755','--ro-bind-data','12','/lab/raskladcoreentry.so'])
                del selected[shim-4:shim+1]
                expected=[str(CORE_ADDON) if a==str(common.BINARY) else a for a in full]
                self.assertEqual(selected,expected)

    def test_core_mode_refuses_invalid_flags_before_any_admission(self):
        for module,make_args,owner,helper in ((REFERENCE,core_reference_args,REFERENCE.chrome,'argv'),
                                             (EXACT,core_exact_args,EXACT.probe,'sandbox_args')):
            for addon,no_query in ((False,False),(False,True),(True,True)):
                with self.subTest(module=module.__name__,addon=addon,no_query=no_query):
                    with patch.object(module.core_guard,'validate_core_entry') as admit,patch.object(module.base,'regular_file') as regular,patch.object(module.subprocess,'Popen') as popen,patch.object(owner,helper) as base_plan:
                        options=dict(focus_stress=True,space_telemetry=True,addon_witness=addon,witness_no_query=no_query,core_entry_witness=True)
                        with self.assertRaisesRegex(module.base.Refused,REASON):make_args(**options)
                        with self.assertRaisesRegex(module.base.Refused,REASON):module.run('/unused',**options)
                        admit.assert_not_called();regular.assert_not_called();popen.assert_not_called();base_plan.assert_not_called()

    def test_invalid_abi_refuses_before_endpoint_or_process(self):
        for module in (REFERENCE,EXACT):
            with self.subTest(module=module.__name__),patch.object(module.core_guard,'validate_core_entry',side_effect=module.base.Refused('core_entry_abi_hash')) as admit,patch.object(module.subprocess,'Popen') as popen,patch.object(module.base,'regular_file') as regular:
                if module is EXACT:
                    with patch.object(module.probe,'endpoint') as endpoint:
                        with self.assertRaisesRegex(module.base.Refused,'core_entry_abi_hash'):module.run('/unused',**OPTIONS,core_entry_witness=True)
                        endpoint.assert_not_called()
                else:
                    with patch.object(module.chrome,'files') as files:
                        with self.assertRaisesRegex(module.base.Refused,'core_entry_abi_hash'):module.run('/unused',**OPTIONS,core_entry_witness=True)
                        files.assert_not_called()
                admit.assert_called_once_with(module.base,module.ROOT)
                popen.assert_not_called();regular.assert_not_called()

    def test_core_mode_has_no_unsealed_fallback(self):
        for module,make_args in ((REFERENCE,common.reference_args),(EXACT,common.exact_args)):
            with self.subTest(module=module.__name__),patch.object(module.base,'regular_file') as regular:
                with self.assertRaisesRegex(module.base.Refused,'core_entry_snapshot_required'):
                    make_args(**OPTIONS,core_entry_witness=True)
                regular.assert_not_called()

    def test_default_and_selected_cli_plan_do_not_read_abi_or_start(self):
        for module in (REFERENCE,EXACT):
            for args,expected in (([],False),(['--focus-stress','--space-telemetry','--addon-witness','--core-entry-witness'],True)):
                with self.subTest(module=module.__name__,selected=expected):
                    output=io.StringIO()
                    with patch('sys.argv',['lab',*args]),patch.object(module,'run') as run,patch.object(module.core_guard,'validate_core_entry') as admit,patch.object(module.subprocess,'Popen') as popen,contextlib.redirect_stdout(output):
                        self.assertEqual(module.main(),0);run.assert_not_called();admit.assert_not_called();popen.assert_not_called()
                    self.assertIs(json.loads(output.getvalue())['coreEntryWitnessPlanned'],expected)


class CoreEntryAbi(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);(self.root/'data').mkdir();libs=self.root/'libs';libs.mkdir()
        self.core=libs/'libFcitx5Core.so.5.1.23';self.frontend=libs/'libwaylandim.so';self.soname=libs/'libFcitx5Core.so.7'
        self.wayland=libs/'libwayland-client.so.0.26.0';self.wayland_soname=libs/'libwayland-client.so.0'
        elf=bytearray(32);elf[:6]=b'\x7fELF\x02\x01';elf[18:20]=b'\x3e\x00'
        self.core.write_bytes(bytes(elf)+b'core');self.frontend.write_bytes(bytes(elf)+b'frontend');self.soname.symlink_to(self.core.name)
        self.wayland.write_bytes(bytes(elf)+b'wayland');self.wayland_soname.symlink_to(self.wayland.name)
        self.lock={'schema':1,'arch':'x86_64','files':{str(path):{'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in (self.core,self.frontend,self.wayland)}}
        self.lock_path=self.root/'data/core-entry-witness-lock.json';self.write_lock()
        for name,path in (('CORE_PATH',self.core),('FRONTEND_PATH',self.frontend),('SONAME_PATH',self.soname),('WAYLAND_PATH',self.wayland),('WAYLAND_SONAME_PATH',self.wayland_soname)):
            context=patch.object(guard,name,path);context.start();self.addCleanup(context.stop)
        context=patch.object(guard.platform,'machine',return_value='x86_64');context.start();self.addCleanup(context.stop)

    def write_lock(self):self.lock_path.write_text(json.dumps(self.lock))
    def validate(self):return guard.validate_core_entry(REFERENCE.base,self.root)

    def test_matching_regular_runtime_hashes_are_admitted(self):self.assertIs(self.validate(),True)

    def test_changed_library_is_rejected(self):
        self.core.write_bytes(b'changed')
        with self.assertRaisesRegex(REFERENCE.base.Refused,'core_entry_abi_hash'):self.validate()

    def test_wrong_soname_target_is_rejected(self):
        self.soname.unlink();self.soname.symlink_to(self.frontend.name)
        with self.assertRaisesRegex(REFERENCE.base.Refused,'core_entry_soname'):self.validate()

    def test_path_injection_and_arch_change_are_rejected(self):
        self.lock['files']['/tmp/other.so']=self.lock['files'].pop(str(self.frontend));self.write_lock()
        with self.assertRaisesRegex(REFERENCE.base.Refused,'core_entry_abi_paths'):self.validate()
        self.lock['arch']='aarch64';self.write_lock()
        with self.assertRaisesRegex(REFERENCE.base.Refused,'core_entry_abi_lock'):self.validate()

    def test_oversized_or_symlink_lock_is_rejected(self):
        self.lock_path.write_bytes(b' '*16385)
        with self.assertRaisesRegex(REFERENCE.base.Refused,'core_entry_file_bound'):self.validate()
        self.lock_path.unlink();real=self.root/'lock';real.write_text(json.dumps(self.lock));self.lock_path.symlink_to(real)
        with self.assertRaisesRegex(REFERENCE.base.Refused,'symlink'):self.validate()

    def test_changed_wayland_client_or_soname_is_rejected(self):
        original=self.wayland.read_bytes();self.wayland.write_bytes(b'changed-wayland')
        with self.assertRaisesRegex(REFERENCE.base.Refused,'core_entry_abi_hash'):self.validate()
        self.wayland.write_bytes(original)
        self.wayland_soname.unlink();self.wayland_soname.symlink_to(self.frontend.name)
        with self.assertRaisesRegex(REFERENCE.base.Refused,'core_entry_wayland_soname'):self.validate()

    def test_pinned_non_x86_elf_is_rejected(self):
        payload=bytearray(self.core.read_bytes());payload[18:20]=b'\xb7\x00';self.core.write_bytes(payload)
        self.lock['files'][str(self.core)]['sha256']=hashlib.sha256(payload).hexdigest();self.write_lock()
        with self.assertRaisesRegex(REFERENCE.base.Refused,'core_entry_abi_arch'):self.validate()


class CoreEntryChildEnvironment(unittest.TestCase):
    def environment(self,selected):
        env={'RASKLAD_NATIVE_WAYLAND_LAB':'1','HOME':'/home/lab','XDG_RUNTIME_DIR':'/run/lab','WAYLAND_DISPLAY':'wayland-lab',
             'RASKLAD_ADDON_WITNESS_LAB':'1','RASKLAD_FOCUS_STRESS_LAB':'1','RASKLAD_SPACE_TELEMETRY_LAB':'1'}
        if selected:env['RASKLAD_CORE_ENTRY_WITNESS_LAB']='1'
        return env

    def test_preload_reaches_only_own_fcitx_child_never_node(self):
        for selected in (False,True):
            with self.subTest(selected=selected):
                env=self.environment(selected);fcitx=MagicMock();fcitx.poll.return_value=None;fcitx.pid=123
                results=[SimpleNamespace(returncode=0,stdout=b'(true,)\n'),SimpleNamespace(returncode=0,stdout=b'<node/>',stderr=b''),
                         SimpleNamespace(returncode=0,stdout=b'{"schema":1,"passed":true}')]
                with patch.dict(os.environ,env,clear=True),patch.object(clients.subprocess,'Popen',return_value=fcitx) as popen,patch.object(clients.subprocess,'run',side_effect=results) as run:
                    self.assertTrue(clients.run(REFERENCE.base,[])['passed'])
                    self.assertNotIn('LD_PRELOAD',os.environ)
                options=popen.call_args.kwargs
                if selected:self.assertEqual(options['env']['LD_PRELOAD'],'/lab/raskladcoreentry.so')
                else:self.assertNotIn('env',options)
                node=run.call_args_list[-1]
                self.assertEqual(node.args[0],['/usr/bin/node','/lab/driver.mjs'])
                self.assertNotIn('LD_PRELOAD',node.kwargs['env'])

    def test_inconsistent_selected_child_environment_refuses_before_spawn(self):
        env=self.environment(True);env['RASKLAD_ADDON_WITNESS_NO_QUERY_LAB']='1'
        with patch.dict(os.environ,env,clear=True),patch.object(clients.subprocess,'Popen') as popen:
            with self.assertRaisesRegex(REFERENCE.base.Refused,'private_core_entry_witness_required'):clients.run(REFERENCE.base,[])
            popen.assert_not_called()


if __name__=='__main__':unittest.main()
