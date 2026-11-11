#!/usr/bin/env python3
"""Offline experiment006 preparation: verified natural prefixes, separated labels.

No source code/Markdown execution or model fitting. Reuses the isolated frozen
004 token driver only for baseline signals; original004 host files unchanged.
"""
import atexit
from collections import Counter
import ctypes as c
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from audit_domain_decisions import xkb_entries
from prepare_technical_evaluation import FENCE, inline_spans, TRIM

SHA='0dbd4b61d265293a071a01fc74e047d968fd3dc264d2b0d0b3c37811a703560b'
RU='абвгдеёжзийклмнопрстуфхцчшщъыьэюя'
EN='abcdefghijklmnopqrstuvwxyz'
ORDER="`qwertyuiop[]asdfghjkl;'zxcvbnm,."


def digest(raw):return hashlib.sha256(raw).hexdigest()
def units(text):return len(text.encode('utf-16-le'))//2

def native(text,layout):
    alphabet=set((EN+EN.upper()) if not layout else (RU+RU.upper()))
    return bool(text) and set(text)<=alphabet

def checked(root,path,sha,limit):
    p=root/path
    if p.resolve()!=p.absolute() or not p.is_file() or p.stat().st_size>limit:raise ValueError('source boundary/limit')
    raw=p.read_bytes()
    if digest(raw)!=sha:raise ValueError('source checksum')
    return raw


def natural_tokens(markdown):
    history=[];fence=None
    def tokens(span,kind):
        nonlocal history
        for word in span.split():
            word=word.strip(TRIM)
            if not word:continue
            if any(ord(ch)<32 or 127<=ord(ch)<=159 for ch in word):raise ValueError('source control character')
            prefix=history[-4:] if kind=='valid' else []
            yield word,kind,['']*(4-len(prefix))+prefix
            if kind=='valid':
                history=(history+[word if units(word)<=32 else ''])[-4:]
    for line in markdown.splitlines():
        if not line.strip():history=[]
        marker=FENCE.match(line)
        if fence:
            if marker and marker[1][0]==fence[0] and len(marker[1])>=len(fence):fence=None;history=[]
            else:yield from tokens(line,'protected')
        elif marker:fence=marker[1];history=[]
        else:
            for role,span in inline_spans(line):
                if role=='protected':history=[]
                yield from tokens(span,role)
                if role=='protected':history=[]


def prepare(root):
    if digest((root/'data/experiment-006-protocol.json').read_bytes())!=SHA:raise ValueError('protocol changed')
    mp=root/'data/evaluation-006-sources.json';m=json.loads(mp.read_text())
    if m['features'] or m['labels'] is not None or m['protocol_sha256']!=SHA:raise ValueError('already prepared/unapproved')
    out=root/'build/experiment-006-data'
    if out.resolve()!=out.absolute() or (root/'build').is_symlink():raise ValueError('build boundary')
    stage=out/'staging.jsonl'
    if stage.exists():raise ValueError('staging exists')
    train=json.loads(checked(root,'data/sources.json',m['source_manifests']['data/sources.json'],65536))
    morph=json.loads(checked(root,'data/morphology-003-sources.json',m['source_manifests']['data/morphology-003-sources.json'],65536))
    for n in m['notices']:checked(root,n['notice'],n['notice_sha256'],1048576)
    entries=xkb_entries();base={e['key']:e['us'] for e in entries if not e['level']};mapping=[{},{}];conversion=[{},{}]
    for e in entries:
        conversion[0][e['us']]=e['ru'];conversion[1][e['ru']]=e['us']
        if e['ru'].lower() in RU:
            mapping[0][e['us']]=ORDER.index(base[e['key']]);mapping[1][e['ru']]=ORDER.index(base[e['key']])
    def canonical(text,layout):
        if not 1<=units(text)<=32 or any(ch not in mapping[layout] for ch in text):return None
        return bytes(mapping[layout][ch] for ch in text)
    stems=[]
    for layout,lang in enumerate(['en','ru']):
        s=next(x for x in train['training_sources'] if x['language']==lang)
        raw=checked(root,f'build/experiment-data/{lang}.dic',s['sha256'],67108864)
        alpha=set(EN if not layout else RU)
        words={line.split('/')[0].split('\t')[0].lower() for line in raw.decode().splitlines()[1:]}
        stems.append({canonical(w,layout) for w in words if 1<=len(w)<=32 and set(w)<=alpha})
    if [len(x) for x in stems]!=[47828,146229]:raise ValueError('stem baseline changed')
    lr=morph['hunspell'];libpath=Path(lr['library_path'])
    if digest(libpath.read_bytes())!=lr['library_sha256']:raise ValueError('library changed')
    lib=c.CDLL(str(libpath));lib.Hunspell_create.argtypes=[c.c_char_p,c.c_char_p];lib.Hunspell_create.restype=c.c_void_p
    lib.Hunspell_spell.argtypes=[c.c_void_p,c.c_char_p];lib.Hunspell_spell.restype=c.c_int;lib.Hunspell_destroy.argtypes=[c.c_void_p]
    lib.Hunspell_get_library_version.restype=c.c_char_p
    if lib.Hunspell_get_library_version().decode()!=lr['version']:raise ValueError('library version')
    handles={}
    for i,s in enumerate(morph['languages']):
        aff=checked(root,s['aff_path'],s['aff_sha256'],1048576);control=checked(root,s['control_aff_path'],s['control_aff_sha256'],1048576)
        checked(root,s['dic_path'],s['dic_sha256'],67108864)
        if b''.join(row for row in aff.splitlines(keepends=True) if not row.split() or row.split()[0] not in [b'PFX',b'SFX'])!=control:raise ValueError('control derivation')
        for mode,path in [('full',s['aff_path']),('control',s['control_aff_path'])]:
            h=lib.Hunspell_create(str(root/path).encode(),str(root/s['dic_path']).encode())
            if not h:raise ValueError('engine unavailable')
            handles[mode,i]=h
    atexit.register(lambda:[lib.Hunspell_destroy(h) for h in handles.values()])
    @lru_cache(None)
    def accepts(mode,layout,text):return bool(lib.Hunspell_spell(handles[mode,layout],text.encode()))
    counts=Counter();probes=set();wordsets={s:set() for s in ['development','holdout']};original_hashes=Counter()
    with stage.open('w') as stream:
        def add(d,raw,layout,prefix,kind,field,gold,original):
            target=raw if units(raw)<=32 else '\ufffd'
            if '\t' in target:raise ValueError('unexpected token whitespace')
            probes.add((layout,target));key=canonical(target,layout)
            label={'document':d['index'],'language':d['language'],'split':d['split'],'kind':kind,'gold':gold,'layout':['us','ru'][layout],
                   'wordHash':digest(original.encode()),'eligible':key is not None and len(key)>=4,'length':len(key) if key is not None else units(raw),
                   'borrowedEnglish':d['language']=='ru' and set(original.lower())<=set(EN),'sentinel':target!=raw}
            stream.write(json.dumps({'layout':layout,'target':target,'prefix':prefix,'field':field,'label':label},ensure_ascii=False)+'\n')
            counts[d['split'],kind]+=1
        for d in m['documents']:
            raw=checked(root,d['path'],d['sha256'],1048576)
            if hashlib.sha1(f'blob {len(raw)}\0'.encode()+raw).hexdigest()!=d['git_blob_sha1']:raise ValueError('Git blob differs')
            original_hashes[d['sha256']]+=1
            for token,role,prefix in natural_tokens(raw.decode()):
                layout=int(any(ch.lower() in RU for ch in token));alpha=set(EN if not layout else RU)
                wordsets[d['split']].add(digest(token.encode()))
                add(d,token,layout,prefix,role,'prose' if role=='valid' else 'protected','K',token)
                if role!='valid' or not 1<=len(token)<=32 or not native(token,layout):continue
                wrong=''.join(conversion[layout][ch] for ch in token)
                burst=[''.join(conversion[layout][ch] for ch in p) if native(p,layout) else p for p in prefix]
                add(d,wrong,1-layout,prefix,'single','prose','C',token)
                add(d,wrong,1-layout,burst,'burst','prose','C',token)
                add(d,wrong,1-layout,prefix,'unknown','unknown','K',token)
    ordered=sorted(probes);neutral=''.join(f'{layout}\t{["en","ru"][layout]}\tvalid\t{["us","ru"][layout]}\t{text}\t{text}\n' for layout,text in ordered).encode()
    probe_path=out/'token-probes.tsv';probe_path.write_bytes(neutral)
    adapter=root/'data/.natural-006-token-adapter.json'
    if adapter.exists():raise ValueError('adapter exists')
    adapter.write_text(json.dumps({'schema_version':1,'status':'approved-local-evaluation','scope':'local-evaluation-only','release_approved':False,'notices':m['notices'],
                                  'purpose':'006natural-prefix tokenbaseline adapter, original004 hostsourceunchanged',
                                  'cases':{'path':'build/experiment-004-data/cases.tsv','sha256':digest(neutral),'bytes':len(neutral),'rows':len(ordered)}}))
    prefix=['bwrap','--unshare-all','--die-with-parent','--new-session','--clearenv','--ro-bind','/usr','/usr','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
            '--ro-bind',str(root),'/work','--ro-bind',str(probe_path),'/work/build/experiment-004-data/cases.tsv','--proc','/proc','--dev','/dev','--tmpfs','/tmp',
            '--setenv','HOME','/tmp','--setenv','PATH','/usr/bin','--setenv','LANG','C.UTF-8','--chdir','/work']
    try:
        result=subprocess.run(prefix+['/work/build/tools/domain_decisions','--protocol','/work/data/experiment-004-protocol.json','--cases-manifest','/work/data/.natural-006-token-adapter.json','--morph-manifest','/work/data/morphology-003-sources.json'],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        if result.returncode or result.stderr:raise ValueError('baseline driver failed')
    finally:adapter.unlink()
    (out/'token-actions.tsv').write_bytes(result.stdout)
    blob=checked(root,'build/experiment-002-artifacts/detector-model.rskn','e92e549e5f0853e4cd78d6d997653098b78550eb9911de11421434e26e3985d8',88290)
    tables=[struct.unpack('44135b',blob[20+i*44135:20+(i+1)*44135]) for i in range(2)]
    @lru_cache(None)
    def scores(keys):
        sums=[0,0];a=b=33
        for ch in list(keys)+[34]:
            for i in range(2):sums[i]+=tables[i][ch]+tables[i][35+b*35+ch]+tables[i][1260+(a*35+b)*35+ch]
            a,b=b,ch
        return [v/(8*(len(keys)+1)) for v in sums]
    def model(keys,layout):
        if keys is None or len(keys)<4:return 'A'
        obs,alt=scores(keys)[layout],scores(keys)[1-layout]
        return 'C' if alt>obs and alt>=-13 and obs<=-9 and alt-obs>=2.5 else 'K' if obs>-9 and obs>=alt else 'A'
    baseline={};lines=result.stdout.decode().splitlines()
    if len(lines)!=len(ordered):raise ValueError('baseline rows differ')
    for i,((layout,text),line) in enumerate(zip(ordered,lines)):
        ordinal,eligible,amb,rt,codes=line.split('\t');keys=canonical(text,layout)
        if keys is None:obs=alt=shared=False
        else:
            other=''.join(conversion[layout][ch] for ch in text)
            obs=accepts('full',layout,text);alt=accepts('full',1-layout,other)
            shared=(keys in stems[0] and keys in stems[1]) or obs and alt or accepts('control',layout,text) and accepts('control',1-layout,other)
        full=('K' if obs else 'C' if alt else 'A') if keys is not None and len(keys)>=4 else 'A'
        if ordinal!=str(i) or codes[1]!=model(keys,layout) or codes[2]!=full or rt!='1':raise ValueError('independent baseline mismatch')
        baseline[layout,text]=[str(int(obs)),str(int(alt)),str(int(shared)),codes[1],codes[2]]
    features=[];labels=[];raw_f=bytearray();raw_l=bytearray();rows=0;total=0
    def flush():
        nonlocal raw_f,raw_l,rows
        if not rows:return
        index=len(features)
        if index>=32:raise ValueError('shard bound')
        for name,raw,refs in [(f'features-{index:02d}.tsv',raw_f,features),(f'labels-{index:02d}.jsonl',raw_l,labels)]:
            dst=out/name
            if dst.exists():raise ValueError('shard output exists')
            dst.write_bytes(raw);refs.append({'path':str(dst.relative_to(root)),'sha256':digest(raw),'bytes':len(raw),'rows':rows})
        raw_f=bytearray();raw_l=bytearray();rows=0
    with stage.open() as stream:
        for line in stream:
            item=json.loads(line);layout,target=item['layout'],item['target'];flags=baseline[layout,target]
            feature=('\t'.join([['us','ru'][layout],target,*item['prefix'],item['field'],*flags])+'\n').encode()
            if rows>=100000 or len(raw_f)+len(feature)>8388608:flush()
            label=item['label'];label.update(ordinal=total,shard=len(features),row=rows,shared=flags[2]=='1')
            raw_f+=feature;raw_l+=(json.dumps(label,sort_keys=True)+'\n').encode();rows+=1;total+=1
    flush()
    m['features']=features;m['labels']=labels
    m['preparation_summary']={'rows':total,'shards':len(features),'tokenBaselineProbesMatched':len(ordered),
                              'kindCounts':{'/'.join(k):v for k,v in counts.items()},'wordTypesSharedAcrossSplits':len(wordsets['development']&wordsets['holdout']),
                              'developmentWordTypes':len(wordsets['development']),'holdoutWordTypes':len(wordsets['holdout']),
                              'devHoldoutExactDocumentHashOverlap':len({d['sha256'] for d in m['documents'] if d['split']=='development'}&{d['sha256'] for d in m['documents'] if d['split']=='holdout'})}
    mp.write_text(json.dumps(m,ensure_ascii=False,indent=2)+'\n');print(json.dumps(m['preparation_summary']))


if __name__=='__main__':prepare(Path(__file__).resolve().parent.parent)
