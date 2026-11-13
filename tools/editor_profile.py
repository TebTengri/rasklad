# SPDX-License-Identifier: MIT
"""Build an explicitly local browser profile from pinned snapshots, not user data."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import language_pack as pack

ROOT=Path('/lab')
OUT=Path('/out')


def _read_pin(directory,rel,pin):
    fd=pack._open_abs(pack._abs_parts(directory))
    try:return pack._read_rel(fd,rel,pin['bytes'],pin['sha256'])
    finally:
        import os
        os.close(fd)


def run_editor(bundles,synthetic):
    assets={}
    for lang,record,payload in bundles:
        for name,b in payload.items():assets['packs/'+lang+'/'+name]=b
        assets['packs/'+lang+'/manifest.json']=pack.canonical_manifest(record)
    lock=json.loads((ROOT/'tools/browser_spell/source-lock.json').read_text())
    for kind,name in [('loader','engine.js'),('wasm','engine.wasm')]:
        b=(ROOT/lock[kind+'_path']).read_bytes()
        if hashlib.sha256(b).hexdigest()!=lock[kind+'_sha256']:raise pack.PackError('engine-pin')
        assets[name]=b
    if synthetic:
        gold=json.loads((ROOT/'build/browser-fixture/synthetic-conformance.json').read_bytes())
        assets['model.rskn']=base64.b64decode(gold['models'][6]['bytesBase64'],validate=True)
        index=gold['lexicons']['fixtureIndex']
        part=base64.b64decode(gold['lexicons']['cases'][index]['bytesBase64'],validate=True)
        assets['stems.rskp']=b'RSKP'+struct.pack('<IIII',1,2,len(part),len(part))+part+part
        assets['notices/MODEL-MODIFICATIONS.txt']=b'Project-authored synthetic mechanism fixture; not distribution data.\n'
        assets['notices/MODEL-METADATA.json']=b'{"synthetic":true,"distribution":false}\n'
    else:
        profile=json.loads((ROOT/'data/browser-profile.json').read_text())
        if profile['local_test'] is not True or profile['distribution'] is not False:raise pack.PackError('profile-admission')
        pins=profile['files']
        for key,name in [('model','model.rskn'),('stem','stems.rskp'),('metadata','notices/MODEL-METADATA.json'),('modifications','notices/MODEL-MODIFICATIONS.txt')]:
            assets[name]=_read_pin('/inputs',key,pins[key])
        # The upstream copies next to the original trained artifacts must agree
        # with the same retained dictionary notices, not just a metadata claim.
        for lang in ['en','ru']:
            b=_read_pin('/inputs',lang+'-notice',pins[lang+'-notice'])
            if b!=assets['packs/'+lang+'/notices/UPSTREAM.txt']:raise pack.PackError('artifact-notice')
        metadata=json.loads(assets['notices/MODEL-METADATA.json'])
        expected={'model':('model','model.rskn'),'exactFullLexicons':('stem','stems.rskp')}
        for key,(pin_key,name) in expected.items():
            row=metadata['artifacts'][key]
            if row['sha256']!=pins[pin_key]['sha256'] or row['bytes']!=len(assets[name]):raise pack.PackError('artifact-metadata')
        for lang,record,_ in bundles:
            dic=next(f for f in record['files'] if f['role']=='dic')
            if metadata['trainingSources'][lang]['sha256']!=dic['sha256']:raise pack.PackError('training-source')
        if metadata['trainingManifestSha256']!=hashlib.sha256((ROOT/'data/sources.json').read_bytes()).hexdigest():raise pack.PackError('training-manifest')
        if metadata['protocolSha256']!=hashlib.sha256((ROOT/'data/experiment-002-protocol.json').read_bytes()).hexdigest():raise pack.PackError('model-protocol')
        if 'release_approved=false' not in metadata['scope'] or b'release_approved=false' not in assets['notices/MODEL-MODIFICATIONS.txt']:raise pack.PackError('derivative-scope')
        review=pins['review']
        assets['notices/PROFILE-REVIEW.md']=_read_pin(ROOT,review['path'],review)
    manifest={'schema':1,'id':'rasklad-en-ru-local-v1','admission':{'local_test':True,'distribution':False},
              'engine':{'version':'1.7.5','loader':'engine.js','wasm':'engine.wasm'},
              'model':'model.rskn','stems':'stems.rskp',
              'languages':{lang:{'locale':record['locale'],'aff':'packs/'+lang+'/dictionary.aff','dic':'packs/'+lang+'/dictionary.dic'} for lang,record,_ in bundles},
              'files':[{'name':name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()} for name,b in sorted(assets.items())]}
    directory=OUT/'editor-profile';directory.mkdir()
    # No executable third-party assets are loaded from the profile directory.
    # Only the pinned loader is explicitly instantiated by the test driver.
    for name,b in assets.items():
        p=directory/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
    (directory/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    fixture=OUT/'editor-fixture';fixture.mkdir()
    (fixture/'editor.html').write_text('''<!doctype html><meta charset="utf-8"><title>Rasklad local editor test</title>
<h1>Local developer fixture — no messages are sent</h1>
<label>Own prose<textarea id="prose"></textarea></label>
<label>Unregistered<textarea id="unknown"></textarea></label>
<label>Code<textarea id="code" spellcheck="false"></textarea></label>
<form><input type="password"><textarea id="protected"></textarea></form>''')
    config={'manifest':manifest,'assets':{name:base64.b64encode(b).decode('ascii') for name,b in assets.items()},
            'protocol':json.loads((ROOT/'data/browser-editor-001-protocol.json').read_text())}
    result=subprocess.run(['/usr/bin/node',str(ROOT/'tests/browser/editor-run.mjs')],
                          input=json.dumps(config).encode(),stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,check=False,timeout=70)
    if len(result.stdout)>16384:raise pack.PackError('editor-output')
    report=json.loads(result.stdout)
    (OUT/'editor-report.json').write_text(json.dumps(report,indent=2)+'\n')
    if result.returncode or report.get('passed') is not True:raise pack.PackError('editor-failed')
    report.update({'inputMode':'synthetic' if synthetic else 'reviewed-local-profile',
                   'modelLoaded':True,'profileBytes':sum(len(b) for b in assets.values()),
                   'scope':'fixed editor functional cases; not a language accuracy benchmark','releaseApproved':False})
    return report
