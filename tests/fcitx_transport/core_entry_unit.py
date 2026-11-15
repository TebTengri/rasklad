#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicit closed ABI/forwarding calibration. Default inert; no real input."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import subprocess

ROOT=Path(__file__).resolve().parents[2]

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

def run():
    base=load('unit_base',ROOT/'tools/fcitx_lab_runner.py')
    guard=load('unit_abi',ROOT/'tools/core_entry_witness_guard.py')
    guard.validate_core_entry(base,ROOT)
    base.regular_file(Path('/usr/bin/bwrap'),True)
    metadata=Path('/usr/bin/bwrap').stat()
    if metadata.st_uid!=0 or metadata.st_mode&0o6022:raise base.Refused('unsafe_bwrap')
    artifacts={
        '/lab/shim.so':ROOT/'build/chromium-ime/core-entry-shim/raskladcoreentry.so',
        '/lab/probe':ROOT/'build/wayland-ime/core_entry_probe',
        '/lab/fake.so':ROOT/'build/wayland-ime/core_entry_fake.so',
    }
    fds={}
    try:
        for target,path in artifacts.items():
            base.regular_file(path);fds[target]=base.sealed_snapshot(path)
        artifact_hashes={}
        for target,fd in fds.items():
            digest=hashlib.sha256();offset=0
            while True:
                chunk=os.pread(fd,65536,offset)
                if not chunk:break
                digest.update(chunk);offset+=len(chunk)
            artifact_hashes[target]=digest.hexdigest()
        args=['/usr/bin/bwrap','--unshare-all','--die-with-parent','--new-session',
              '--cap-drop','ALL','--clearenv','--ro-bind','/usr','/usr',
              '--symlink','usr/bin','/bin','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
              '--ro-bind-try','/etc/ld.so.cache','/etc/ld.so.cache',
              '--tmpfs','/tmp','--tmpfs','/home','--tmpfs','/run',
              '--dir','/home/lab','--dir','/run/lab','--dir','/lab','--proc','/proc','--dev','/dev']
        for target,fd in fds.items():args+=['--perms','0755','--ro-bind-data',str(fd),target]
        env={'PATH':'/usr/bin','HOME':'/home/lab','XDG_RUNTIME_DIR':'/run/lab',
             'RASKLAD_NATIVE_WAYLAND_LAB':'1','RASKLAD_ADDON_WITNESS_LAB':'1',
             'RASKLAD_CORE_ENTRY_WITNESS_LAB':'1'}
        for key,value in env.items():args+=['--setenv',key,value]
        cases=[('ready',[],[],'/lab/shim.so',0),
               ('original_once_entry_before_call',[],['fake'],'/lab/shim.so:/lab/fake.so',0),
               ('foreign_thread_snapshot',[],['thread'],'/lab/shim.so',0),
               ('foreign_thread_entry',[],['thread-entry'],'/lab/shim.so:/lab/fake.so',125)]
        for key in env:
            if key!='PATH':cases.append(('wrong_'+key,['--setenv',key,'wrong'],[],'/lab/shim.so',125))
        for key in ('DISPLAY','WAYLAND_SOCKET','RASKLAD_PARENT_FD'):
            cases.append(('forbidden_'+key,['--setenv',key,''],[],'/lab/shim.so',125))
        def limits():
            resource.setrlimit(resource.RLIMIT_CORE,(0,0));resource.setrlimit(resource.RLIMIT_CPU,(5,5))
        results={}
        for name,extra,command,preload,expected in cases:
            # --ro-bind-data consumes the shared open-file offset. Every fresh
            # sandbox must receive the full immutable ELF, not an empty file.
            for fd in fds.values():os.lseek(fd,0,os.SEEK_SET)
            p=subprocess.run(args+extra+['--setenv','LD_PRELOAD',preload,'--','/lab/probe']+command,
                stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
                pass_fds=tuple(fds.values()),preexec_fn=limits,timeout=5,env={'PATH':'/usr/bin'})
            results[name]=p.returncode
            if p.returncode!=expected:raise base.Refused('core_entry_unit_failed_'+name+'_'+str(p.returncode))
        return {'schema':1,'passed':True,'checks':len(cases),'exitCodes':results,
                'hostDisplay':False,'gpu':False,'network':False,'realInput':False,
                'artifactSha256':artifact_hashes}
    finally:
        for fd in fds.values():os.close(fd)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--run',action='store_true');args=parser.parse_args()
    if not args.run:
        print(json.dumps({'schema':1,'status':'plan_only','hostDisplay':False,'gpu':False,'realInput':False}));return 0
    try:result=run()
    except Exception:
        print(json.dumps({'schema':1,'passed':False,'reason':'core_entry_unit_refused_or_failed'}));return 2
    print(json.dumps(result,sort_keys=True));return 0
if __name__=='__main__':raise SystemExit(main())
