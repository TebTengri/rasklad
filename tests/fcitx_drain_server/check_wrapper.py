#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile/run intercepted ownership controls only; default inert. No Fcitx."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]

def checked(path,expected):
    if path.is_symlink() or not path.is_file():raise ValueError('nonregular_input')
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=expected:raise ValueError('input_hash')
    return data

def build_and_run(OUT):
    lock=json.loads((ROOT/'patches/fcitx5/primitive-lock.json').read_text())
    source=ROOT/'build/wayland-ime/upstream/fcitx5-tree-5.1.23'
    patch=ROOT/'patches/fcitx5/0001-lab-grab-release-without-local-destroy.patch'
    checked(patch,lock['patchSha256'])
    for p in [OUT,*OUT.parents]:
        if p.is_symlink():raise ValueError('output_symlink')
    OUT.mkdir(parents=True,exist_ok=True)
    if OUT.stat().st_uid!=os.getuid() or OUT.stat().st_mode&0o022:raise ValueError('output_owner')
    for p in OUT.iterdir():
        if p.is_symlink() or not p.is_file() or p.stat().st_nlink!=1:raise ValueError('unsafe_output')
    for relative,pin in lock['files'].items():
        (OUT/Path(relative).name).write_bytes(checked(source/relative,pin))
    # Only the pinned two-file patch; apply to the copied wrapper, never upstream.
    env={'PATH':'/usr/bin','LC_ALL':'C'}
    def cmd(args):subprocess.run(args,cwd=ROOT,env=env,check=True,timeout=30,stdout=subprocess.DEVNULL)
    cmd(['/usr/bin/git','apply','-p5','--directory='+str(OUT.relative_to(ROOT)),str(patch)])
    xml=ROOT/'tests/input_lab/protocol/input-method-unstable-v2.xml'
    checked(xml,'99414dbad9458e71aa1fa01bc45f94ca6685787bfcb4d98948f72c1b45b60703')
    for action,name in [('client-header','wayland-input-method-unstable-v2-client-protocol.h'),('private-code','protocol.c')]:
        cmd(['/usr/bin/wayland-scanner',action,str(xml),str(OUT/name)])
    cmd(['/usr/bin/cc','-c',str(OUT/'protocol.c'),'-o',str(OUT/'protocol.o')])
    wraps=['get_version','set_user_data','add_listener','marshal_flags','destroy']
    cmd(['/usr/bin/c++','-std=c++20','-I'+str(OUT),'-I/usr/include/Fcitx5/Utils',
         str(ROOT/'tests/fcitx_drain_server/wrapper_ownership.cpp'),str(OUT/'zwp_input_method_keyboard_grab_v2.cpp'),
         str(OUT/'protocol.o'),*[ '-Wl,--wrap=wl_proxy_'+x for x in wraps],
         '-lFcitx5Utils','-lwayland-client','-o',str(OUT/'ownership-test')])
    result=subprocess.run([str(OUT/'ownership-test')],env=env,check=True,timeout=3,capture_output=True)
    report=json.loads(result.stdout);report['patchSha256']=lock['patchSha256']
    return report

def run():
    parent=ROOT/'build/wayland-ime'
    for p in [parent,*parent.parents]:
        if p.is_symlink():raise ValueError('workspace_symlink')
    if parent.stat().st_uid!=os.getuid() or parent.stat().st_mode&0o022:
        raise ValueError('workspace_owner')
    # Fresh private directory, never reuse objects from an earlier invocation.
    with tempfile.TemporaryDirectory(prefix='wrapper-control-',dir=parent) as temp:
        return build_and_run(Path(temp))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--run',action='store_true');args=parser.parse_args()
    try:print(json.dumps(run() if args.run else {'status':'plan_only','fcitxStarted':False},sort_keys=True))
    except Exception as e:print(json.dumps({'passed':False,'reason':type(e).__name__}));raise SystemExit(2)
