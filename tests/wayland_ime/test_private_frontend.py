# SPDX-License-Identifier: MIT
"""Admission and process boundaries for the optional closed reference frontend."""
import hashlib
import importlib.util
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
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
lab=load('private_lab',ROOT/'tools/wayland_ime_lab.py')
clients=load('private_clients',ROOT/'tools/native_ime_clients.py')
common=load('private_common',ROOT/'tests/wayland_ime/test_addon_witness_guards.py')

class PrivateFrontend(unittest.TestCase):
    def test_selected_requires_snapshot_and_sway(self):
        for variant in ('baseline','keep-grab'):
            with self.subTest(variant=variant),patch.object(lab.chrome,'argv') as argv:
                with self.assertRaisesRegex(lab.base.Refused,'snapshot_required'):
                    lab.sandbox_args({}, {}, {}, Path('/runtime'),Path('/config'),frontend_variant=variant)
                with self.assertRaisesRegex(lab.base.Refused,'sway_only'):
                    lab.run('/unused',nested_smoke=True,frontend_variant=variant)
                argv.assert_not_called()

    def test_selected_mount_is_sealed_and_controls_are_child_only(self):
        for variant in ('baseline','keep-grab'):
            args=common.reference_args(frontend_variant=variant,snapshot_fds={'/lab/addons/libwaylandim.so':44})
            i=args.index('/lab/addons/libwaylandim.so')
            self.assertEqual(args[i-4:i+1],['--perms','0755','--ro-bind-data','44','/lab/addons/libwaylandim.so'])
            self.assertNotIn('RASKLAD_FCITX_KEEP_GRAB_LAB',args);self.assertNotIn('RASKLAD_KEEP_GRAB_INACTIVE_RAW',args)
            self.assertEqual(args[args.index('RASKLAD_PRIVATE_FRONTEND_VARIANT')+1],variant)

    def test_build_record_and_binary_must_match(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'data').mkdir();(root/'build/wayland-ime/frontend-baseline').mkdir(parents=True)
            binary=root/'build/wayland-ime/frontend-baseline/libwaylandim.so';binary.write_bytes(b'fixture')
            lock={'commit':lab.FRONTEND_COMMIT,'archiveSha256':'a'*64}
            (root/'data/fcitx-frontend-source-lock.json').write_text(json.dumps(lock))
            record={'schema':1,'built':True,'variant':'unmodified_source_control','sourceCommit':lock['commit'],'sourceArchiveSha256':lock['archiveSha256'],'artifactSha256':hashlib.sha256(b'fixture').hexdigest()}
            metadata=root/'build/wayland-ime/frontend-baseline-result.json';metadata.write_text(json.dumps(record))
            with patch.object(lab,'ROOT',root),patch.object(lab.core_guard,'validate_core_entry') as abi:
                self.assertEqual(lab.validate_frontend('baseline')[0],binary);abi.assert_called_once()
                binary.write_bytes(b'changed')
                with self.assertRaisesRegex(lab.base.Refused,'build_record'):lab.validate_frontend('baseline')
                metadata.write_bytes(b'x'*16385)
                with self.assertRaisesRegex(lab.base.Refused,'file_bound'):lab.validate_frontend('baseline')

    def test_keep_requires_current_cancellation_patch_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'data').mkdir();(root/'patches/fcitx5').mkdir(parents=True)
            (root/'build/wayland-ime/frontend-keep-grab').mkdir(parents=True)
            binary=root/'build/wayland-ime/frontend-keep-grab/libwaylandim.so';binary.write_bytes(b'fixture')
            (root/'data/fcitx-frontend-source-lock.json').write_text(json.dumps({'commit':lab.FRONTEND_COMMIT,'archiveSha256':'a'*64}))
            lock={'schema':1,'sourceCommit':lab.FRONTEND_COMMIT,'patchSha256':'b'*64,'cancellationPatchSha256':'c'*64}
            (root/'patches/fcitx5/keep-grab-lock.json').write_text(json.dumps(lock))
            record={'schema':1,'built':True,'variant':'private_keep_grab_experiment','sourceCommit':lab.FRONTEND_COMMIT,'sourceArchiveSha256':'a'*64,'artifactSha256':hashlib.sha256(b'fixture').hexdigest(),'patchSha256':'b'*64,'cancellationPatchSha256':'c'*64}
            metadata=root/'build/wayland-ime/frontend-keep-grab-build-result.json'
            with patch.object(lab,'ROOT',root),patch.object(lab.core_guard,'validate_core_entry'):
                metadata.write_text(json.dumps(record));self.assertEqual(lab.validate_frontend('keep-grab')[0],binary)
                for cancellation in (None,'d'*64):
                    stale=dict(record)
                    if cancellation is None:stale.pop('cancellationPatchSha256')
                    else:stale['cancellationPatchSha256']=cancellation
                    metadata.write_text(json.dumps(stale))
                    with self.assertRaisesRegex(lab.base.Refused,'patch_record'):lab.validate_frontend('keep-grab')

    def test_child_env_composes_preload_without_mutating_parent(self):
        for variant in ('system','baseline','keep-grab'):
            env={'RASKLAD_PRIVATE_FRONTEND_VARIANT':variant,'RASKLAD_CORE_ENTRY_WITNESS_LAB':'1','RASKLAD_ADDON_WITNESS_LAB':'1','RASKLAD_FOCUS_STRESS_LAB':'1','RASKLAD_SPACE_TELEMETRY_LAB':'1'}
            with patch.dict(os.environ,env,clear=True):
                child=clients.fcitx_child_env(lab.base)
                self.assertEqual(child['LD_PRELOAD'],'/lab/raskladcoreentry.so')
                self.assertEqual(child.get('RASKLAD_FCITX_KEEP_GRAB_LAB'), '1' if variant=='keep-grab' else None)
                self.assertEqual(child.get('RASKLAD_KEEP_GRAB_INACTIVE_RAW'),'1' if variant=='keep-grab' else None)
                self.assertNotIn('RASKLAD_FCITX_KEEP_GRAB_LAB',os.environ);self.assertNotIn('LD_PRELOAD',os.environ)

    def test_both_selected_variants_get_own_config_system_is_untouched(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary=root/'libwaylandim.so';binary.write_bytes(b'fixture')
            config=root/'conf/waylandim.conf'
            def private_path(value):
                return binary if value=='/lab/addons/libwaylandim.so' else config
            for variant in ('baseline','keep-grab'):
                with patch.dict(os.environ,{'RASKLAD_PRIVATE_FRONTEND_VARIANT':variant},clear=True),patch.object(clients,'Path',side_effect=private_path):
                    self.assertEqual(clients.prepare_frontend(lab.base)[1],hashlib.sha256(b'fixture').hexdigest())
                self.assertEqual(config.read_text(),'PersistentVirtualKeyboard=True\nPreferKeyEvent=True\n')
            with patch.dict(os.environ,{},clear=True),patch.object(clients,'Path') as paths:
                self.assertIsNone(clients.prepare_frontend(lab.base));paths.assert_not_called()

    def test_private_diagnostics_are_extracted_and_reject_links(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'build/wayland-ime').mkdir(parents=True)
            log=root/'build/wayland-ime/native-browser-baseline-error.log'
            with patch.object(lab,'ROOT',root):
                report={'passed':False,'_privateBrowserDiagnostic':'x'*4000}
                lab.extract_private_browser_diagnostic(report,'baseline')
                self.assertNotIn('_privateBrowserDiagnostic',report)
                self.assertEqual(log.read_text(),'x'*3500);self.assertEqual(log.stat().st_mode&0o777,0o600)
                report={'passed':False,'_privateBrowserDiagnostic':'system ignored'}
                lab.extract_private_browser_diagnostic(report,'system');self.assertNotIn('_privateBrowserDiagnostic',report)
                other=root/'other';other.write_text('unchanged');log.unlink();log.hardlink_to(other)
                report={'passed':False,'_privateBrowserDiagnostic':'private'}
                with self.assertRaisesRegex(lab.base.Refused,'diagnostic_file'):lab.extract_private_browser_diagnostic(report,'baseline')
                self.assertNotIn('_privateBrowserDiagnostic',report);self.assertEqual(other.read_text(),'unchanged')
                log.unlink();log.symlink_to(other)
                with self.assertRaises(OSError):lab.extract_private_browser_diagnostic({'passed':False,'_privateBrowserDiagnostic':'private'},'baseline')
                self.assertEqual(other.read_text(),'unchanged')

    def test_seed_keyboard_only_selected_and_never_sends_input(self):
        for variant in ('system','baseline','keep-grab'):
            child=MagicMock();children=[]
            with patch.dict(os.environ,{'RASKLAD_PRIVATE_FRONTEND_VARIANT':variant},clear=True),patch.object(clients.subprocess,'Popen',return_value=child) as spawn,patch.object(clients,'wait_seed_keyboard',return_value=True) as wait:
                self.assertEqual(clients.seed_keyboard(lab.base,children),variant!='system')
                if variant=='system':spawn.assert_not_called();wait.assert_not_called()
                else:
                    self.assertEqual(spawn.call_args.args[0],['/lab/rasklad-wayland-keys'])
                    self.assertEqual(spawn.call_args.kwargs['stdin'],clients.subprocess.PIPE)
                    self.assertEqual(children,[child]);child.stdin.write.assert_not_called()

    def test_seed_readiness_is_exact_and_partial_or_eof_is_bounded(self):
        child=MagicMock();child.poll.return_value=None;child.stdout.fileno.return_value=88
        with patch.object(clients.select,'select',return_value=([child.stdout],[],[])),patch.object(clients.os,'read',side_effect=[b'{"rea',b'dy":true}\n']):
            self.assertTrue(clients.wait_seed_keyboard(lab.base,child))
        for chunks,reason in (([b''], 'eof'),([b'{"ready":false}\n'],'ready')):
            with patch.object(clients.select,'select',return_value=([child.stdout],[],[])),patch.object(clients.os,'read',side_effect=chunks):
                with self.assertRaisesRegex(lab.base.Refused,'private_seed_keyboard_'+reason):clients.wait_seed_keyboard(lab.base,child)
        with patch.object(clients.select,'select',side_effect=[([child.stdout],[],[]),([],[],[])]),patch.object(clients.os,'read',return_value=b'{'):
            with self.assertRaisesRegex(lab.base.Refused,'private_seed_keyboard_timeout'):clients.wait_seed_keyboard(lab.base,child)
        child.poll.return_value=1
        with patch.object(clients.select,'select') as ready:
            with self.assertRaisesRegex(lab.base.Refused,'private_seed_keyboard_exited'):clients.wait_seed_keyboard(lab.base,child)
            ready.assert_not_called()

    def test_mapping_checks_device_and_inode(self):
        process=SimpleNamespace(pid=123,poll=lambda:None)
        expected=SimpleNamespace(st_dev=os.makedev(0,5),st_ino=88)
        import io
        with patch('builtins.open',return_value=io.BytesIO(b'100-200 r-xp 0 00:05 88 /private/module\n')):
            self.assertTrue(clients.verify_frontend_mapping(lab.base,process,expected))
        with patch('builtins.open',return_value=io.BytesIO(b'100-200 r-xp 0 00:06 88 /private/module\n')),patch.object(clients.time,'monotonic',side_effect=[0,0,3]),patch.object(clients.time,'sleep'):
            with self.assertRaisesRegex(lab.base.Refused,'not_mapped'):clients.verify_frontend_mapping(lab.base,process,expected)

if __name__=='__main__':unittest.main()
