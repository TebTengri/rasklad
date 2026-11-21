#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real private-socket Wayland lifecycle contrast. Default inert; no user compositor."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import stat
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/wayland-ime/wire-lifecycle'

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
BASE=load('wire_base',ROOT/'tools/fcitx_lab_runner.py')

def prepare():
    parent=ROOT
    for name in ('build','wayland-ime','wire-lifecycle'):
        parent/=name;parent.mkdir(mode=0o700,exist_ok=True);info=parent.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o022:
            raise BASE.Refused('wire_build_directory')
    xml=ROOT/'tests/wayland_lifecycle/rasklad-wire-lab-v1.xml';BASE.regular_file(xml)
    for kind,name in [('client-header','wire-client.h'),('server-header','wire-server.h'),('private-code','wire-protocol.c')]:
        fd,temporary=tempfile.mkstemp(prefix='.wire-',dir=BUILD);os.close(fd)
        try:
            subprocess.run(['/usr/bin/wayland-scanner',kind,str(xml),temporary],check=True,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=5,env={'PATH':'/usr/bin'})
            target=BUILD/name
            if target.is_symlink():raise BASE.Refused('wire_generated_symlink')
            os.replace(temporary,target)
        finally:Path(temporary).unlink(missing_ok=True)
    for args in (['/usr/bin/qmake6',str(ROOT/'tests/wayland_lifecycle/wire_lifecycle.pro')],['/usr/bin/make','-j2']):
        result=subprocess.run(args,cwd=BUILD,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=30,env={'PATH':'/usr/bin'})
        if result.returncode:raise BASE.Refused('wire_build_failed')
    return {'schema':1,'built':True,'hostConfigurationChanged':False}

def run():
    if os.geteuid()==0:raise BASE.Refused('root_not_admitted')
    load('wire_server_abi',ROOT/'tools/server_route_witness_guard.py').validate(BASE,ROOT)
    load('wire_client_abi',ROOT/'tools/core_entry_witness_guard.py').validate_core_entry(BASE,ROOT)
    bwrap=Path('/usr/bin/bwrap');BASE.regular_file(bwrap,True);info=bwrap.stat()
    if info.st_uid!=0 or info.st_mode&0o6022:raise BASE.Refused('unsafe_bwrap')
    binary=BUILD/'wire_lifecycle';BASE.regular_file(binary,True);fd=BASE.sealed_snapshot(binary)
    try:
        digest=hashlib.sha256();offset=0
        while True:
            part=os.pread(fd,65536,offset)
            if not part:break
            digest.update(part);offset+=len(part)
        args=['/usr/bin/bwrap','--unshare-all','--die-with-parent','--new-session','--cap-drop','ALL','--clearenv',
              '--ro-bind','/usr','/usr','--symlink','usr/bin','/bin','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
              '--ro-bind-try','/etc/ld.so.cache','/etc/ld.so.cache','--tmpfs','/home','--tmpfs','/run','--tmpfs','/tmp',
              '--dir','/home/lab','--dir','/lab','--proc','/proc','--dev','/dev',
              '--perms','0755','--ro-bind-data',str(fd),'/lab/probe','--setenv','PATH','/usr/bin',
              '--setenv','HOME','/home/lab','--setenv','RASKLAD_WIRE_LIFECYCLE_LAB','1']
        def limits():
            for kind,value in ((resource.RLIMIT_CORE,0),(resource.RLIMIT_CPU,5),(resource.RLIMIT_AS,256<<20),(resource.RLIMIT_NOFILE,64),(resource.RLIMIT_FSIZE,1<<20)):
                resource.setrlimit(kind,(value,value))
        results=[]
        for mode,press in [('drop',0),('drain',1)]:
            os.lseek(fd,0,os.SEEK_SET)
            child=subprocess.run(args+['--','/lab/probe',mode],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,
                                 timeout=8,pass_fds=(fd,),preexec_fn=limits,env={'PATH':'/usr/bin'})
            if len(child.stdout)>4096:raise BASE.Refused('wire_report_bound')
            result=json.loads(child.stdout)
            required={'passed':True,'serverPress':1,'serverRelease':1,'clientPress':press,'clientRelease':1,
                      'oneLiveGrabInvariant':True,'protocolError':0,'barrierCompleted':True,'mode':mode}
            if child.returncode!=0 or any(type(result.get(k)) is not type(v) or result[k]!=v for k,v in required.items()):
                raise BASE.Refused('wire_contrast_failed_'+mode)
            results.append(result)
        names=['tests/wayland_lifecycle/'+n for n in ['wire_lifecycle.cpp','wire_client.cpp','wire_common.h','wire_lifecycle.pro','rasklad-wire-lab-v1.xml','run.py']]
        return {'schema':1,'passed':True,'modes':results,'artifactSha256':digest.hexdigest(),
                'sourceSha256':{n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in names},
                'hostDisplay':False,'gpu':False,'network':False,'realInput':False,'fcitxLoaded':False,
                'scope':'actual libwayland private socketpair with own toy lifecycle protocol; not input-method compatibility or Fcitx fix'}
    finally:os.close(fd)

def main():
    parser=argparse.ArgumentParser(description=__doc__);group=parser.add_mutually_exclusive_group();group.add_argument('--build',action='store_true');group.add_argument('--run',action='store_true');args=parser.parse_args()
    if not args.build and not args.run:
        print(json.dumps({'schema':1,'status':'plan_only','hostDisplay':False,'gpu':False,'realInput':False}));return 0
    try:result=prepare() if args.build else run()
    except Exception as error:
        print(json.dumps({'schema':1,'passed':False,'reason':str(error) if type(error).__name__=='Refused' else type(error).__name__}));return 2
    print(json.dumps(result,sort_keys=True));return 0
if __name__=='__main__':raise SystemExit(main())
