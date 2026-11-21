#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Closed server-route logger calibration. Default inert; no live input."""
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
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[2]


def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module


PROTOCOL_SHA256='99414dbad9458e71aa1fa01bc45f94ca6685787bfcb4d98948f72c1b45b60703'


def manager_metadata(base):
    # Derive wire metadata independently of the shim/task prompt, from the
    # previously pinned upstream protocol. Hash BEFORE XML parsing.
    path=ROOT/'tests/input_lab/protocol/input-method-unstable-v2.xml'
    base.regular_file(path)
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC)
    with os.fdopen(fd,'rb') as source:
        if os.fstat(source.fileno()).st_size>65536:raise base.Refused('server_route_unit_protocol_bound')
        payload=source.read(65537)
    if len(payload)>65536:raise base.Refused('server_route_unit_protocol_bound')
    actual=hashlib.sha256(payload).hexdigest()
    if actual!=PROTOCOL_SHA256:raise base.Refused('server_route_unit_protocol_pin')
    protocol=ET.fromstring(payload)
    interfaces=protocol.findall("interface[@name='zwp_input_method_manager_v2']")
    if len(interfaces)!=1:raise base.Refused('server_route_unit_manager_metadata')
    requests=interfaces[0].findall('request')
    matches=[(index,request) for index,request in enumerate(requests) if request.get('name')=='get_input_method']
    if len(matches)!=1:raise base.Refused('server_route_unit_manager_metadata')
    opcode,request=matches[0]
    codes={'int':'i','uint':'u','fixed':'f','string':'s','object':'o','new_id':'n','array':'a','fd':'h'}
    signature=''
    for argument in request.findall('arg'):
        kind=argument.get('type')
        if kind not in codes:raise base.Refused('server_route_unit_manager_metadata')
        if argument.get('allow-null')=='true':signature+='?'
        signature+=codes[kind]
    if signature!='on' or opcode>255:raise base.Refused('server_route_unit_manager_metadata')
    return {'opcode':opcode,'signature':signature,'sha256':actual}


def build():
    base=load('server_route_build_base',ROOT/'tools/fcitx_lab_runner.py')
    parent=ROOT
    for name in ('build','wayland-ime','server-route-unit'):
        parent/=name;parent.mkdir(mode=0o700,exist_ok=True)
        info=parent.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o022:
            raise base.Refused('server_route_unit_build_directory')
    for kind in ('fake','probe'):
        source=ROOT/f'tests/fcitx_transport/server_route_{kind}.pro';base.regular_file(source)
        directory=parent/kind;directory.mkdir(mode=0o700,exist_ok=True)
        if directory.is_symlink():raise base.Refused('server_route_unit_build_directory')
        for argv in (['/usr/bin/qmake6',str(source)],['/usr/bin/make','-j2']):
            p=subprocess.run(argv,cwd=directory,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,timeout=30,env={'PATH':'/usr/bin'})
            if p.returncode:raise base.Refused('server_route_unit_build_failed')
    return {'schema':1,'built':True,'sharedShimChanged':False,'hostConfigurationChanged':False}


def run():
    base=load('server_route_unit_base',ROOT/'tools/fcitx_lab_runner.py')
    if os.geteuid()==0:raise base.Refused('root_not_admitted')
    protocol=manager_metadata(base)
    guard=load('server_route_unit_abi',ROOT/'tools/server_route_witness_guard.py')
    guard.validate(base,ROOT)
    base.regular_file(Path('/usr/bin/bwrap'),True)
    metadata=Path('/usr/bin/bwrap').stat()
    if metadata.st_uid!=0 or metadata.st_mode&0o6022:raise base.Refused('unsafe_bwrap')
    artifacts={'/lab/shim.so':ROOT/'build/chromium-ime/server-route-shim/raskladserverroute.so',
        '/lab/probe':ROOT/'build/wayland-ime/server-route-unit/probe/server_route_probe',
        '/lab/fake.so':ROOT/'build/wayland-ime/server-route-unit/fake/server_route_fake.so'}
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
            'RASKLAD_NATIVE_WAYLAND_LAB':'1',
            'RASKLAD_SERVER_ROUTE_WITNESS_LAB':'1'}
        for key,value in env.items():args+=['--setenv',key,value]
        args+=['--setenv','RASKLAD_UNIT_MANAGER_OPCODE',str(protocol['opcode'])]
        cases=[(mode,[],mode,0) for mode in ('ready','counts','noise','malformed','unknown','multiple','null','attach-failure','foreign-snapshot','saturation')]
        cases += [('foreign_callback',[],'foreign-callback',125),('foreign_hook',[],'foreign-hook',125)]
        for key in env:
            if key!='PATH':cases.append(('wrong_'+key,['--setenv',key,'wrong'],'ready',125))
        for key in ('DISPLAY','WAYLAND_DEBUG','RASKLAD_PARENT_FD'):
            cases.append(('forbidden_'+key,['--setenv',key,''],'ready',125))
        cases += [('allowed_parent_fd_marker',['--setenv','WAYLAND_SOCKET','777777'],'ready',0),
                  ('existing_report',['--dir','/run/lab/server-route-witness.bin'],'ready',125),
                  ('symlink_report',['--symlink','/tmp/unused','/run/lab/server-route-witness.bin'],'ready',125)]
        def limits():
            for kind,value in ((resource.RLIMIT_CORE,0),(resource.RLIMIT_CPU,3),(resource.RLIMIT_AS,256<<20),(resource.RLIMIT_NOFILE,64)):
                resource.setrlimit(kind,(value,value))
        results={};deadline=time.monotonic()+30
        for name,extra,mode,expected in cases:
            if time.monotonic()>=deadline:raise base.Refused('server_route_unit_deadline')
            for fd in fds.values():os.lseek(fd,0,os.SEEK_SET)
            p=subprocess.run(args+extra+['--setenv','LD_PRELOAD','/lab/shim.so:/lab/fake.so','--','/lab/probe',mode],
                stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
                pass_fds=tuple(fds.values()),preexec_fn=limits,timeout=min(5,deadline-time.monotonic()),env={'PATH':'/usr/bin'})
            results[name]=p.returncode
            if p.returncode!=expected:raise base.Refused('server_route_unit_failed_'+name+'_'+str(p.returncode))
        return {'schema':1,'passed':True,'checks':len(cases),'exitCodes':results,
            'hostDisplay':False,'gpu':False,'network':False,'realInput':False,'realWaylandDisplay':False,
            'artifactSha256':hashes,'protocolSha256':protocol['sha256'],'managerOpcode':protocol['opcode'],'managerSignature':protocol['signature'],'scope':'actual server-route shim logger callback against synthetic public message metadata; no wire/display integration'}
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
