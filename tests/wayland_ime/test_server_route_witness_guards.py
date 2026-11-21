# SPDX-License-Identifier: MIT
"""Server diagnostics add only sealed lab artifacts, never host capabilities."""
import copy
import hashlib
import importlib.util
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

common = load('server_route_common', ROOT/'tests/wayland_ime/test_grab_entry_witness_guards.py')
guard = load('server_route_guard_test', ROOT/'tools/server_route_witness_guard.py')
REFERENCE, EXACT = common.REFERENCE, common.EXACT
OPTIONS = dict(common.OPTIONS, grab_entry_witness=True)


def reference_args(**options):
    return common.common.common.reference_args(snapshot_fds={
        '/lab/raskladcoreentry.so':12, '/lab/raskladserverroute.so':13}, **options)


def exact_args(**options):
    paths = EXACT.chrome.files(); paths.pop('xvfb'); paths.pop('keys')
    paths['addon'] = ROOT/'build/chromium-ime/profile-addon/raskladlab.so'
    plan = {'sysfs':'/private/gpu-metadata/sys', 'statOnlyPrimary':'/dev/dri/card0',
            'primaryBindMustBeNodevReadOnly':True, 'hostSysfsBind':False}
    return EXACT.sandbox_args(Path('/private'),8,plan,paths,{}, {
        '/lab/rasklad-wayland-keys':10, '/lab/raskladcoreentry.so':12,
        '/lab/raskladserverroute.so':13}, **options)


class ServerRouteIntegration(unittest.TestCase):
    def test_selection_adds_only_sealed_artifact_helpers_and_marker(self):
        for module, make_args in ((REFERENCE, reference_args), (EXACT, exact_args)):
            with self.subTest(module=module.__name__), patch.object(module.base,'regular_file'):
                ordinary = make_args(**OPTIONS)
                self.assertEqual(ordinary, make_args(**OPTIONS,server_route_witness=False))
                self.assertNotIn('LD_PRELOAD', ordinary)
                self.assertNotIn('/lab/raskladserverroute.so', ordinary)
                selected = make_args(**OPTIONS, server_route_witness=True)
                additions = [
                    ['--perms','0755','--ro-bind-data','13','/lab/raskladserverroute.so'],
                    ['--ro-bind',str(ROOT/'tools/server_route_witness_guard.py'),'/lab/server_route_witness_guard.py'],
                    ['--ro-bind',str(ROOT/'tests/chromium_ime/server-route-witness.mjs'),'/lab/server-route-witness.mjs'],
                    ['--setenv','RASKLAD_SERVER_ROUTE_WITNESS_LAB','1']]
                for addition in additions:
                    at = next(i for i in range(len(selected)) if selected[i:i+len(addition)] == addition)
                    del selected[at:at+len(addition)]
                self.assertEqual(selected, ordinary)
                self.assertNotIn('LD_PRELOAD', selected)

    def test_selection_requires_sealed_server_snapshot(self):
        for module, make_args in ((REFERENCE,common.common.core_reference_args),(EXACT,common.common.core_exact_args)):
            with self.subTest(module=module.__name__),patch.object(module.base,'regular_file'):
                with self.assertRaisesRegex(module.base.Refused,'server_route_snapshot_required'):
                    make_args(**OPTIONS,server_route_witness=True)

    def test_missing_chain_refuses_before_files_or_processes(self):
        for module, make_args in ((REFERENCE, reference_args),(EXACT, exact_args)):
            for options in ({}, {'grab_entry_witness':True},
                            {'grab_entry_witness':True,'core_entry_witness':True}):
                with self.subTest(module=module.__name__,options=options), patch.object(module.base,'regular_file') as files, patch.object(module.subprocess,'Popen') as spawn, patch.object(module.core_guard,'validate_core_entry') as abi:
                    with self.assertRaises(module.base.Refused):make_args(server_route_witness=True,**options)
                    with self.assertRaises(module.base.Refused):module.run('/unused',server_route_witness=True,**options)
                    files.assert_not_called(); spawn.assert_not_called(); abi.assert_not_called()


class ServerRouteAdmission(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name); (self.root/'data').mkdir()
        self.server=self.root/'server.so'; self.hypr=self.root/'Hyprland'; self.soname=self.root/'server-soname.so'
        elf=bytearray(32); elf[:6]=b'\x7fELF\x02\x01'; elf[18:20]=b'\x3e\x00'
        for path in (self.server,self.hypr):path.write_bytes(elf)
        self.soname.symlink_to(self.server.name)
        self.value={'schema':1,'arch':'x86_64','files':{str(p):{'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in (self.server,self.hypr)}}
        self.lock=self.root/'data/server-route-witness-lock.json'
        for name,value in (('SERVER',self.server),('HYPR',self.hypr),('SONAME',self.soname)):
            context=patch.object(guard,name,value); context.start(); self.addCleanup(context.stop)
        context=patch.object(guard.platform,'machine',return_value='x86_64'); context.start(); self.addCleanup(context.stop)
        real_fstat = os.fstat
        def root_owned(fd):
            info = real_fstat(fd)
            return SimpleNamespace(st_uid=0, st_mode=info.st_mode, st_size=info.st_size)
        context=patch.object(guard.os,'fstat',side_effect=root_owned); context.start(); self.addCleanup(context.stop)

    def validate(self,value=None,exact=True):
        import json
        self.lock.write_text(json.dumps(self.value if value is None else value))
        return guard.validate(REFERENCE.base,self.root,exact=exact)

    def test_matching_pins_admitted(self):self.assertIs(self.validate(),True)

    def test_bad_schema_arch_paths_and_hash_rejected(self):
        for mutate in (lambda x:x.update(schema=2), lambda x:x.update(arch='aarch64'),
                       lambda x:x['files'].update({'/tmp/unpinned.so':{'sha256':'0'*64}}),
                       lambda x:x['files'][str(self.server)].update(sha256='0'*64)):
            value=copy.deepcopy(self.value); mutate(value)
            with self.subTest(value=value),self.assertRaises(REFERENCE.base.Refused):self.validate(value)

    def test_invalid_lock_types_refused(self):
        values=[[],None,1,'lock',{'schema':1,'arch':'x86_64','files':None},
                {'schema':1,'arch':'x86_64','files':[]}, dict(self.value,schema=True)]
        for row in (None,[],1,{'sha256':None},{'sha256':17}):
            value=copy.deepcopy(self.value);value['files'][str(self.server)]=row;values.append(value)
        for value in values:
            with self.subTest(value=value),self.assertRaises(REFERENCE.base.Refused):
                # None must remain JSON null rather than selecting the default fixture.
                import json
                self.lock.write_text(json.dumps(value));guard.validate(REFERENCE.base,self.root)

    def test_exact_checks_hypr_hash_reference_checks_server_only(self):
        self.hypr.write_bytes(b'changed-hypr')
        self.assertIs(self.validate(exact=False),True)
        with self.assertRaisesRegex(REFERENCE.base.Refused,'server_route_elf|server_route_hash'):self.validate(exact=True)

    def test_pinned_wrong_arch_elf_rejected(self):
        payload=bytearray(self.server.read_bytes());payload[18:20]=b'\xb7\x00';self.server.write_bytes(payload)
        self.value['files'][str(self.server)]['sha256']=hashlib.sha256(payload).hexdigest()
        with self.assertRaisesRegex(REFERENCE.base.Refused,'server_route_elf'):self.validate()

    def test_changed_binary_soname_and_elf_rejected(self):
        self.server.write_bytes(b'changed')
        with self.assertRaises(REFERENCE.base.Refused):self.validate()
        self.soname.unlink();self.soname.symlink_to(self.hypr.name)
        with self.assertRaisesRegex(REFERENCE.base.Refused,'server_route_soname'):self.validate()


class ServerRouteEnvironment(unittest.TestCase):
    def environment(self):
        return dict.fromkeys(('RASKLAD_SERVER_ROUTE_WITNESS_LAB','RASKLAD_NATIVE_WAYLAND_LAB',
            'RASKLAD_FOCUS_STRESS_LAB','RASKLAD_SPACE_TELEMETRY_LAB','RASKLAD_ADDON_WITNESS_LAB',
            'RASKLAD_CORE_ENTRY_WITNESS_LAB','RASKLAD_GRAB_ENTRY_WITNESS_LAB'),'1') | {'HOME':'/home/lab','XDG_RUNTIME_DIR':'/run/lab'}

    def test_default_inert_and_selected_copy_only(self):
        with patch.dict(os.environ,{},clear=True):self.assertIsNone(guard.child_env(REFERENCE.base))
        env=self.environment()
        with patch.dict(os.environ,env,clear=True),patch.object(guard.Path,'exists',return_value=False):
            child=guard.child_env(REFERENCE.base)
            self.assertEqual(dict(os.environ),env)
            self.assertEqual(child,dict(env,LD_PRELOAD='/lab/raskladserverroute.so'))

    def test_missing_marker_forbidden_environment_and_existing_file_refused(self):
        env=self.environment()
        for name in env:
            if name=='RASKLAD_SERVER_ROUTE_WITNESS_LAB':continue
            changed=dict(env);changed.pop(name)
            with self.subTest(missing=name),patch.dict(os.environ,changed,clear=True),patch.object(guard.Path,'exists',return_value=False),self.assertRaises(REFERENCE.base.Refused):guard.child_env(REFERENCE.base)
        for name in ('LD_PRELOAD','DISPLAY','WAYLAND_DEBUG','RASKLAD_PARENT_FD'):
            with self.subTest(forbidden=name),patch.dict(os.environ,dict(env,**{name:''}),clear=True),patch.object(guard.Path,'exists',return_value=False),self.assertRaises(REFERENCE.base.Refused):guard.child_env(REFERENCE.base)
        with patch.dict(os.environ,env,clear=True),patch.object(guard.Path,'exists',return_value=True),self.assertRaises(REFERENCE.base.Refused):guard.child_env(REFERENCE.base)


if __name__=='__main__':unittest.main()
