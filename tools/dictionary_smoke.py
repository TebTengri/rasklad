#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Child of bin/test-reviewed-dictionaries; no network, data downloads or model."""
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

import language_pack as pack

ROOT = Path('/lab')
OUT = Path('/out')
PHASE = 'reference-library'
WORDS = {
    'en': ['hello','world','book','books','computer','dictionary','layout','language',
           'iPhone','iphone','John','john','qzxv','ghbdtn','https','CMake'],
    'ru': ['привет','мир','кот','коты','слово','слова','компьютер','язык','словарь',
           'раскладка','АйФон','айфон','модельнет','ghbdtn'],
}


def read_pin(root, rel, size, sha):
    fd = pack._open_abs(pack._abs_parts(root))
    try:
        return pack._read_rel(fd, rel, size, sha)
    finally:
        os.close(fd)


def synthetic_profiles():
    # Default native fixture contains only project-authored fictional bytes.
    raw = (ROOT/'build/browser-spell-native/native.json').read_bytes()
    if len(raw)>128*1024: raise pack.PackError('synthetic-fixture-size')
    return json.loads(raw)['profiles']


def records_and_payloads(synthetic):
    if synthetic:
        result=[]
        for lang,p in synthetic_profiles().items():
            payload={'dictionary.aff':base64.b64decode(p['affBase64'],validate=True),
                     'dictionary.dic':base64.b64decode(p['dicBase64'],validate=True),
                     'notices/UPSTREAM.txt':b'Project-authored fictional fixture, no third-party dictionary.\n',
                     'notices/MODIFICATIONS.txt':b'Original artificial test bytes; not language quality data.\n'}
            record={'id':'fictional-'+lang,'locale':{'en':'en-US','ru':'ru-RU'}[lang],
                    'revision':'project-authored-smoke-v1','format':pack.FORMAT,'engine':pack.ENGINE,
                    'provenance':{'repository':'https://example.invalid/rasklad-fixture','commit':'0'*40,
                                  'review':'data/fictional-review.md'},
                    'admission':{'local_test':True,'distribution':False},'files':[]}
            for (name,b),role in zip(payload.items(),['aff','dic','notice','modifications']):
                record['files'].append({'role':role,'path':name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()})
            result.append((lang,record,payload))
        return result
    catalog=pack.load_catalog(ROOT/'data/language-packs.json')
    mappings=json.loads((ROOT/'data/language-pack-inputs.json').read_text())['inputs']
    result=[]
    for lang,locale in [('en','en-US'),('ru','ru-RU')]:
        selected=[r for r in catalog if r['locale']==locale]
        if len(selected)!=1:raise pack.PackError('locale-admission')
        record=selected[0]
        pack._trusted(record,'local-test')  # Admission before opening any data.
        payload={}
        for f in record['files']:
            if f['role'] in ('aff','dic'):
                payload[f['path']]=read_pin('/inputs',lang+'.'+f['role'],f['bytes'],f['sha256'])
            else:
                payload[f['path']]=read_pin(ROOT,mappings[record['id']][f['path']],f['bytes'],f['sha256'])
        result.append((lang,record,payload))
    return result


def run(synthetic):
    global PHASE
    # Read-only /usr is trusted host tooling; this check pins the reference library.
    pin=json.loads((ROOT/'data/morphology-003-sources.json').read_text())['hunspell']
    lib=Path(pin['library_path'])
    read_pin(lib.parent,lib.name,pin['library_bytes'],pin['library_sha256'])
    PHASE='read-pins'
    bundles=records_and_payloads(synthetic)
    PHASE='package-build'
    request={'schema':1,'profiles':{}}
    packages=[]
    for lang,record,payload in bundles:
        stage=OUT/('stage-'+lang);stage.mkdir()
        for name,b in payload.items():
            p=stage/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
        destination=OUT/('pack-'+lang)
        pack.build_pack(record,stage,destination)
        pack.verify_pack(record,destination)
        # Read through the same pins AGAIN into immutable byte snapshots. These
        # exact snapshots, not later filesystem reopens, are sent to both engines.
        profiles={}
        for f in record['files']:
            if f['role'] in ('aff','dic'):
                b=read_pin(destination,f['path'],f['bytes'],f['sha256'])
                profiles[f['role']+'Base64']=base64.b64encode(b).decode('ascii')
        profiles['words']=WORDS[lang];request['profiles'][lang]=profiles
        packages.append({'locale':record['locale'],'files':len(record['files']),
                         'bytes':sum(f['bytes'] for f in record['files']),
                         'distribution':record['admission']['distribution']})
    PHASE='native-comparison'
    native=subprocess.run([str(ROOT/'build/browser-spell-native/native_spell_fixture'),'--stdin-profiles'],
                          input=json.dumps(request).encode(),stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=30,check=True)
    if len(native.stdout)>16*1024*1024:raise pack.PackError('native-output-size')
    PHASE='browser-comparison'
    node=subprocess.run(['/usr/bin/node',str(ROOT/'tests/browser/dictionary-smoke.mjs')],input=native.stdout,
                        stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=65,check=True)
    if len(node.stdout)>16384:raise pack.PackError('browser-output-size')
    report=json.loads(node.stdout)
    if report.get('passed') is not True:raise pack.PackError('browser-failed')
    report.update({'inputMode':'synthetic' if synthetic else 'reviewed-raw-dictionaries',
                   'packages':packages,'modelLoaded':False,'referenceLibraryHashChecked':True,
                   'scope':'dictionary compatibility and resource smoke; not language accuracy or editor integration'})
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__=='__main__':
    try:
        if sys.argv[1:] not in (['--synthetic'],['--real']):raise pack.PackError('usage')
        run(sys.argv[1]=='--synthetic')
    except Exception:
        print(json.dumps({'passed':False,'error':'dictionary-smoke-failed','phase':PHASE}),file=sys.stderr)
        sys.exit(1)
