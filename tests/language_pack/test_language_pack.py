# SPDX-License-Identifier: MIT
"""Project-authored bytes only: no actual dictionaries, models or user input."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('language_pack', ROOT / 'tools/language_pack.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)

class LanguagePackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.payload = {'dictionary.aff': b'SET UTF-8\n', 'dictionary.dic': b'1\nzorb\n',
                        'notices/UPSTREAM.txt': b'Project-authored fixture; no third-party content.\n',
                        'notices/MODIFICATIONS.txt': b'Fictional test bytes, unchanged.\n'}
        roles = ['aff', 'dic', 'notice', 'modifications']
        self.record = {'id': 'fixture-en-us', 'locale': 'en-US', 'revision': 'fixture-1',
                       'format': 'hunspell-utf8-v1', 'engine': 'hunspell-1.7.5',
                       'provenance': {'repository': 'https://example.invalid/fixture', 'commit': 'a'*40,
                                      'review': 'data/fixture-review.md'},
                       'admission': {'local_test': True, 'distribution': False},
                       'files': [{'role': role, 'path': name, 'bytes': len(data),
                                  'sha256': hashlib.sha256(data).hexdigest()}
                                 for role, (name, data) in zip(roles, self.payload.items())]}
        for name, data in self.payload.items():
            target = self.source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        self.out = self.root / 'pack'

    def build(self, record=None, purpose='local-test'):
        return pack.build_pack(record or self.record, self.source, self.out, purpose)

    def test_reproducible_manifest_and_byte_exact_notice(self):
        self.build()
        expected = (json.dumps(self.record, sort_keys=True, ensure_ascii=True, indent=2)+'\n').encode()
        self.assertEqual((self.out/'manifest.json').read_bytes(), expected)
        for name, data in self.payload.items():
            self.assertEqual((self.out/name).read_bytes(), data)
            self.assertEqual((self.out/name).stat().st_mode & 0o111, 0)
        pack.verify_pack(self.record, self.out)
        other = self.root/'second'
        pack.build_pack(self.record, self.source, other)
        self.assertEqual((other/'manifest.json').read_bytes(), expected)

    def test_independent_locale_not_hardcoded_to_en_ru(self):
        self.record.update(id='fixture-fr-fr', locale='fr-FR')
        self.build()
        pack.verify_pack(self.record, self.out)

    def test_denied_distribution_before_creating_output(self):
        with self.assertRaises(Exception): self.build(purpose='distribution')
        self.assertFalse(self.out.exists())
        self.record['admission']={'local_test':False,'distribution':True}
        with self.assertRaises(Exception): self.build()
        self.assertFalse(self.out.exists())

    def test_pack_cannot_approve_itself(self):
        self.build()
        forged=copy.deepcopy(self.record);forged['admission']['distribution']=True
        (self.out/'manifest.json').chmod(0o600)
        (self.out/'manifest.json').write_text(json.dumps(forged, sort_keys=True, ensure_ascii=True, indent=2)+'\n')
        with self.assertRaises(Exception): pack.verify_pack(self.record, self.out, 'distribution')
        with self.assertRaises(Exception): pack.verify_pack(self.record, self.out)

    def test_corruption_and_truncation_before_write(self):
        for bad in [b'1\nzorb!\n', b'']:
            (self.source/'dictionary.dic').write_bytes(bad)
            with self.assertRaises(Exception): self.build()
            self.assertFalse(self.out.exists())

    def test_extra_files_and_empty_directory_refused(self):
        self.build()
        for name, directory in [('extra',False),('extra-dir',True)]:
            p=self.out/name
            p.mkdir() if directory else p.write_bytes(b'not approved')
            with self.assertRaises(Exception): pack.verify_pack(self.record,self.out)
            p.rmdir() if directory else p.unlink()

    def test_source_symlink_and_symlink_ancestor_refused(self):
        p=self.source/'dictionary.dic';p.unlink();p.symlink_to(self.root/'missing')
        with self.assertRaises(Exception): self.build()
        self.assertFalse(self.out.exists())
        p.unlink();p.write_bytes(self.payload['dictionary.dic'])
        alias=self.root/'alias';alias.symlink_to(self.source,target_is_directory=True)
        with self.assertRaises(Exception):pack.build_pack(self.record,alias,self.out)

    def test_no_overwrite_and_no_fifo_blocking(self):
        self.out.mkdir();(self.out/'sentinel').write_bytes(b'keep')
        with self.assertRaises(Exception):self.build()
        self.assertEqual((self.out/'sentinel').read_bytes(),b'keep')
        import os
        p=self.source/'dictionary.dic';p.unlink();os.mkfifo(p)
        with self.assertRaises(Exception):pack.build_pack(self.record,self.source,self.root/'fifo-out')

    def test_invalid_metadata_paths_types_and_resource_bounds(self):
        mutations=[lambda r:r.update(unreviewed=True),lambda r:r.update(locale='../../en'),
                   lambda r:r.update(engine='javascript'),lambda r:r['admission'].update(distribution=1),
                   lambda r:r['files'][0].update(path='../escape'),lambda r:r['files'][0].update(path='/escape'),
                   lambda r:r['files'][0].update(path='notices'),lambda r:r['files'][0].update(path='manifest.json'),
                   lambda r:r['files'][0].update(bytes=True),lambda r:r['files'][0].update(bytes=1024*1024+1),
                   lambda r:r['files'][0].update(sha256='F'*64),lambda r:r['files'][1].update(path='DICTIONARY.AFF'),
                   lambda r:r['files'][0].update(role='execute'),lambda r:r.update(files=r['files'][:2])]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                r=copy.deepcopy(self.record);mutate(r)
                with self.assertRaises(Exception):self.build(r)
                self.assertFalse(self.out.exists())

    def test_failed_write_rolls_back_only_own_entries(self):
        from unittest.mock import patch
        original=pack._Writer.write
        calls=0
        def disk_failure(writer,fd,name,data):
            nonlocal calls
            calls+=1
            if calls==2:raise OSError('fictional disk failure')
            return original(writer,fd,name,data)
        sentinel=self.root/'unrelated';sentinel.write_bytes(b'preserve')
        with patch.object(pack._Writer,'write',disk_failure):
            with self.assertRaises(pack.PackError):self.build()
        self.assertFalse(self.out.exists())
        self.assertEqual(sentinel.read_bytes(),b'preserve')
        self.assertEqual({p.name for p in self.root.iterdir()},{'source','unrelated'})

    def test_cli_success_and_distribution_refusal(self):
        catalog=self.root/'catalog.json';catalog.write_text(json.dumps({'schema':1,'packs':[self.record]}))
        command=[sys.executable,str(ROOT/'tools/language_pack.py'),'--catalog',str(catalog),'--id',self.record['id']]
        result=subprocess.run(command+['--source',str(self.source),'--output',str(self.out)],capture_output=True,timeout=5)
        self.assertEqual(result.returncode,0,result.stderr)
        verified=subprocess.run(command+['--verify',str(self.out)],capture_output=True,timeout=5)
        self.assertEqual(json.loads(verified.stdout)['status'],'verified')
        rejected=subprocess.run(command+['--verify',str(self.out),'--purpose','distribution'],capture_output=True,timeout=5)
        self.assertEqual(rejected.returncode,1)
        self.assertEqual(json.loads(rejected.stderr),{'error':'not-admitted'})

    def test_actual_catalog_metadata_and_notice_pins_only(self):
        records=pack.load_catalog(ROOT/'data/language-packs.json')
        self.assertEqual({r['locale'] for r in records},{'en-US','ru-RU'})
        mapping=json.loads((ROOT/'data/language-pack-inputs.json').read_text())['inputs']
        for record in records:
            self.assertTrue(record['admission']['local_test'])
            self.assertFalse(record['admission']['distribution'])
            for f in record['files']:
                if f['role'] not in ('notice','modifications'): continue
                data=(ROOT/mapping[record['id']][f['path']]).read_bytes()
                self.assertEqual(len(data),f['bytes'])
                self.assertEqual(hashlib.sha256(data).hexdigest(),f['sha256'])

    def test_missing_modification_record_is_rejected(self):
        self.record['files']=[f for f in self.record['files'] if f['role']!='modifications']
        with self.assertRaises(Exception):self.build()
        self.assertFalse(self.out.exists())

    def test_verify_rejects_executable_and_symlink_files(self):
        self.build()
        p=self.out/'dictionary.dic';p.chmod(0o755)
        with self.assertRaises(Exception):pack.verify_pack(self.record,self.out)
        p.unlink();p.symlink_to(self.source/'dictionary.dic')
        with self.assertRaises(Exception):pack.verify_pack(self.record,self.out)

    def test_cli_rejects_duplicate_json_without_data_or_path_error_leak(self):
        catalog=self.root/'catalog.json';catalog.write_text('{"schema":1,"schema":1,"packs":[]}')
        result=subprocess.run([sys.executable,str(ROOT/'tools/language_pack.py'),'--catalog',str(catalog),
                               '--id',self.record['id'],'--source',str(self.source),'--output',str(self.out)],
                              capture_output=True,timeout=5)
        self.assertEqual(result.returncode,1)
        self.assertNotIn(str(self.root).encode(),result.stdout+result.stderr)
        self.assertFalse(self.out.exists())

if __name__ == '__main__': unittest.main()
