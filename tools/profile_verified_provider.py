#!/usr/bin/env python3
"""Developer-only Linux peak-memory probe of the exact research provider stack.

One neutral token, no full corpus; no user-input argument. The parent isolates
one worker, and a fresh supervisor attributes kernel rusage only to that worker.
Not part of the app, not a GUI/steady-state memory measurement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import time


def worker(root):
    args=[str(root/'build/tools/domain_decisions'),'--protocol',str(root/'data/experiment-004-protocol.json'),
          '--cases-manifest',str(root/'data/.provider-006-adapter.json'),'--morph-manifest',str(root/'data/morphology-003-sources.json')]
    start=time.monotonic();p=subprocess.run(args,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if p.returncode or p.stderr or len(p.stdout.splitlines())!=1:raise ValueError('worker failed')
    u=resource.getrusage(resource.RUSAGE_CHILDREN)
    print(json.dumps({'workerMaxRssKiB':u.ru_maxrss,'workerUserSeconds':u.ru_utime,'workerSystemSeconds':u.ru_stime,
                      'wallSeconds':time.monotonic()-start,'scope':'single actual domain_decisions worker; full+control Hunspell pairs, pinned model and full stem index; one synthetic neutral token; peak includes loader/source verification; no full corpus or QtQuick GUI',
                      'method':'fresh supervisor spawns exactly one direct worker, no other children; kernel getrusage(RUSAGE_CHILDREN), no bwrap/supervisor RSS'}))


def profile(root):
    m=json.loads((root/'data/evaluation-006-sources.json').read_text());raw=b'0\ten\tvalid\tus\texample\texample\n'
    out=root/'build/experiment-006-data'
    if out.resolve()!=out.absolute():raise ValueError('build boundary')
    probe=out/'provider-one-token.tsv'
    if probe.exists():
        if probe.resolve()!=probe.absolute() or probe.read_bytes()!=raw:raise ValueError('probe exists with other content')
    else:probe.write_bytes(raw)
    adapter=root/'data/.provider-006-adapter.json'
    with adapter.open('x') as f:
        json.dump({'schema_version':1,'status':'approved-local-evaluation','scope':'local-evaluation-only','release_approved':False,
                   'notices':m['notices'],'purpose':'resource-only006adapter, original004hostfileunchanged',
                   'cases':{'path':'build/experiment-004-data/cases.tsv','sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'rows':1}},f)
    args=['bwrap','--unshare-all','--die-with-parent','--new-session','--clearenv','--ro-bind','/usr','/usr','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
          '--ro-bind',str(root),'/work','--ro-bind',str(probe),'/work/build/experiment-004-data/cases.tsv','--proc','/proc','--dev','/dev','--tmpfs','/tmp',
          '--setenv','HOME','/tmp','--setenv','PATH','/usr/bin','--setenv','LANG','C.UTF-8','--chdir','/work',
          '/usr/bin/python3','/work/tools/profile_verified_provider.py','--worker']
    try:
        p=subprocess.run(args,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        if p.returncode or p.stderr:raise ValueError('isolated profile failed')
        print(p.stdout.decode(),end='')
    finally:adapter.unlink()


if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--worker',action='store_true');args=a.parse_args()
    root=Path(__file__).resolve().parent.parent
    worker(root) if args.worker else profile(root)
