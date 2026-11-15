#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Closed grab-listener forwarding calibration. Default inert; no live input."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import stat
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]


def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module


def build():
    base=load('grab_build_base',ROOT/'tools/fcitx_lab_runner.py')
    parent=ROOT
    for name in ('build','wayland-ime','grab-unit'):
        parent/=name;parent.mkdir(mode=0o700,exist_ok=True)
        info=parent.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o022:
            raise base.Refused('grab_unit_build_directory')
    for kind in ('fake','probe'):
        source=ROOT/f'tests/fcitx_transport/grab_entry_{kind}.pro';base.regular_file(source)
        directory=parent/kind;directory.mkdir(mode=0o700,exist_ok=True)
        if directory.is_symlink():raise base.Refused('grab_unit_build_directory')
        for argv in (['/usr/bin/qmake6',str(source)],['/usr/bin/make','-j2']):
            p=subprocess.run(argv,cwd=directory,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,timeout=30,env={'PATH':'/usr/bin'})
            if p.returncode:raise base.Refused('grab_unit_build_failed')
    return {'schema':1,'built':True,'sharedShimChanged':False,'hostConfigurationChanged':False}


def run():
    base=load('grab_unit_base',ROOT/'tools/fcitx_lab_runner.py')
    if os.geteuid()==0:raise base.Refused('root_not_admitted')
    guard=load('grab_unit_abi',ROOT/'tools/core_entry_witness_guard.py')
    guard.validate_core_entry(base,ROOT)
    base.regular_file(Path('/usr/bin/bwrap'),True)
    metadata=Path('/usr/bin/bwrap').stat()
    if metadata.st_uid!=0 or metadata.st_mode&0o6022:raise base.Refused('unsafe_bwrap')
    artifacts={'/lab/shim.so':ROOT/'build/chromium-ime/grab-entry-shim/raskladcoreentry.so',
        '/lab/probe':ROOT/'build/wayland-ime/grab-unit/probe/grab_entry_probe',
        '/lab/fake.so':ROOT/'build/wayland-ime/grab-unit/fake/grab_entry_fake.so'}
    fds={}
    try:
        for target,path in artifacts.items():
            base.regular_file(path);fds[target]=base.sealed_snapshot(path)
        hashes={}
        for target,fd in fds.items():
            digest=hashlib.sha256();offset=0
            while True:
                payload=os.pread(fd,65536,offset)
                if not payload:break
                digest.update(payload);offset+=len(payload)
            hashes[target]=digest.hexdigest()
        args=['/usr/bin/bwrap','--unshare-all','--die-with-parent','--new-session','--cap-drop','ALL','--clearenv',
            '--ro-bind','/usr','/usr','--symlink','usr/bin','/bin','--symlink','usr/lib','/lib',
            '--symlink','usr/lib','/lib64','--ro-bind-try','/etc/ld.so.cache','/etc/ld.so.cache',
            '--tmpfs','/home','--tmpfs','/run','--tmpfs','/tmp','--dir','/home/lab','--dir','/run/lab',
            '--dir','/lab','--proc','/proc','--dev','/dev']
        for target,fd in fds.items():args+=['--perms','0755','--ro-bind-data',str(fd),target]
        env={'PATH':'/usr/bin','HOME':'/home/lab','XDG_RUNTIME_DIR':'/run/lab',
            'RASKLAD_NATIVE_WAYLAND_LAB':'1','RASKLAD_ADDON_WITNESS_LAB':'1',
            'RASKLAD_CORE_ENTRY_WITNESS_LAB':'1','RASKLAD_GRAB_ENTRY_WITNESS_LAB':'1'}
        for key,value in env.items():args+=['--setenv',key,value]
        cases=[(mode,[],mode,0) for mode in ('forward','reuse','failed','other','version','null-listener','null-key','overflow','saturation')]
        cases.append(('foreign_thread',[],'foreign-thread',125))
        for key in env:
            if key!='PATH':cases.append(('wrong_'+key,['--setenv',key,'wrong'],'other',125))
        for key in ('DISPLAY','WAYLAND_SOCKET','RASKLAD_PARENT_FD'):
            cases.append(('forbidden_'+key,['--setenv',key,''],'other',125))
        def limits():
            for kind,value in ((resource.RLIMIT_CORE,0),(resource.RLIMIT_CPU,3),(resource.RLIMIT_AS,256<<20),(resource.RLIMIT_NOFILE,64)):
                resource.setrlimit(kind,(value,value))
        results={};deadline=time.monotonic()+30
        for name,extra,mode,expected in cases:
            if time.monotonic()>=deadline:raise base.Refused('grab_unit_deadline')
            for fd in fds.values():os.lseek(fd,0,os.SEEK_SET)
            p=subprocess.run(args+extra+['--setenv','LD_PRELOAD','/lab/shim.so:/lab/fake.so','--','/lab/probe',mode],
                stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
                pass_fds=tuple(fds.values()),preexec_fn=limits,timeout=min(5,deadline-time.monotonic()),env={'PATH':'/usr/bin'})
            results[name]=p.returncode
            if p.returncode!=expected:raise base.Refused('grab_unit_failed_'+name+'_'+str(p.returncode))
        return {'schema':1,'passed':True,'checks':len(cases),'exitCodes':results,
            'hostDisplay':False,'gpu':False,'network':False,'realInput':False,'realWaylandProxy':False,
            'artifactSha256':hashes,'scope':'actual shim callback bridge against synthetic RTLD_NEXT listener registration'}
    finally:
        for fd in fds.values():os.close(fd)


def main():
    parser=argparse.ArgumentParser(description=__doc__);modes=parser.add_mutually_exclusive_group()
    modes.add_argument('--build',action='store_true');modes.add_argument('--run',action='store_true');args=parser.parse_args()
    if not args.run and not args.build:
        print(json.dumps({'schema':1,'status':'plan_only','hostDisplay':False,'gpu':False,'realInput':False}));return 0
    try:result=build() if args.build else run()
    except Exception as error:
        print(json.dumps({'schema':1,'passed':False,'reason':str(error) if type(error).__name__=='Refused' else type(error).__name__}));return 2
    print(json.dumps(result,sort_keys=True));return 0


if __name__=='__main__':raise SystemExit(main())
