#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify/apply private diagnostic patches and syntax-check; never load frontend."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
ROOT=Path(__file__).resolve().parent.parent
PARTS=['core','input-method','input-method-v2','plasma-window-management','wlr-foreign-toplevel-management-unstable-v1','ext-foreign-toplevel-list','cosmic-toplevel-info-unstable-v1']
def digest(path,expected,limit):
    if path.is_symlink() or not path.is_file() or path.stat().st_size>limit:raise ValueError('input_bound')
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=expected:raise ValueError('input_hash')
    return data

def check():
    lock=json.loads((ROOT/'data/fcitx-frontend-source-lock.json').read_text())
    patches=json.loads((ROOT/'patches/fcitx5/retirement-lock.json').read_text())
    archive=ROOT/'build/wayland-ime/upstream/fcitx5-5.1.23.tar.gz'
    digest(archive,lock['archiveSha256'],8_000_000)
    parent=ROOT/'build/wayland-ime'
    for p in [parent,*parent.parents]:
        if p.is_symlink():raise ValueError('workspace_symlink')
    if parent.stat().st_uid!=os.getuid() or parent.stat().st_mode&0o022:raise ValueError('workspace_owner')
    patch_names=['0001-lab-grab-release-without-local-destroy.patch','0002-lab-track-listener-depth.patch','0003-lab-bounded-grab-retirement.patch']
    for name in patch_names:digest(ROOT/'patches/fcitx5'/name,patches['patches'][name],100_000)
    with tempfile.TemporaryDirectory(prefix='retirement-syntax-',dir=parent) as temp:
        tree=Path(temp);source=tree/'source';source.mkdir();generated=tree/'generated';generated.mkdir()
        members=total=0
        with tarfile.open(archive) as tar:
            for member in tar:
                members+=1;total+=member.size
                if members>2000 or total>100_000_000 or member.size>10_000_000:raise ValueError('archive_bound')
                parts=Path(member.name).parts
                if not parts or parts[0]!='fcitx5-5.1.23' or '..' in parts:raise ValueError('archive_path')
                if member.isdir():continue
                if member.issym() and parts[1:3]==('data','icon'):continue
                if not member.isfile():raise ValueError('archive_type')
                dest=source.joinpath(*parts[1:]);dest.parent.mkdir(parents=True,exist_ok=True)
                with tar.extractfile(member) as stream:data=stream.read(member.size+1)
                if len(data)!=member.size or dest.exists():raise ValueError('archive_member')
                dest.write_bytes(data)
        env={'PATH':'/usr/bin','LC_ALL':'C'}
        log_path=parent/'retirement-syntax.log'
        if log_path.is_symlink() or (log_path.exists() and log_path.stat().st_nlink!=1):raise ValueError('log_path')
        with log_path.open('wb') as log:
            def cmd(args):subprocess.run(args,cwd=ROOT,env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=log,timeout=30,check=True)
            for name in patch_names:
                cmd(['/usr/bin/git','apply','--directory='+str(source.relative_to(ROOT)),str(ROOT/'patches/fcitx5'/name)])
            xmls=[]
            for part in PARTS:xmls+=list((source/'src/lib/fcitx-wayland'/part).glob('*.xml'))
            share=Path('/usr/share/wayland-protocols')
            xmls += [share/x for x in ['unstable/input-method/input-method-unstable-v1.xml','unstable/text-input/text-input-unstable-v1.xml','unstable/text-input/text-input-unstable-v3.xml','staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml','staging/ext-workspace/ext-workspace-v1.xml']]
            names=set()
            for xml in xmls:
                if xml.stem in names:raise ValueError('protocol_collision')
                names.add(xml.stem)
                cmd(['/usr/bin/wayland-scanner','client-header',str(xml),str(generated/('wayland-'+xml.stem+'-client-protocol.h'))])
            (generated/'config.h').write_text('#pragma once\n#define HAVE_PIPE2 1\n')
            incs=[generated,source/'src/lib',source/'src/lib/fcitx-wayland',source/'src/frontend/waylandim',source/'src/frontend/virtualinputcontext']
            incs += [source/'src/lib/fcitx-wayland'/p for p in PARTS]
            incs += [Path('/usr/include/Fcitx5')/p for p in ['Core','Utils','Config','Module/fcitx-module/wayland','Module/fcitx-module/dbus']]
            files=['src/frontend/waylandim/waylandimserverv2.cpp','src/frontend/waylandim/waylandim.cpp','src/lib/fcitx-wayland/input-method-v2/zwp_input_method_keyboard_grab_v2.cpp']
            for f in files:cmd(['/usr/bin/c++','-std=c++20','-fsyntax-only','-DHAVE_PIPE2=1',*['-I'+str(p) for p in incs],str(source/f)])
    return {'schema':1,'syntaxPassed':True,'translationUnits':len(files),'sourceArchiveSha256':lock['archiveSha256'],'patches':patches['patches'],'frontendLoaded':False,'installedChanges':False,'functionalFix':False,'retiredInputPolicy':'abort125','scope':'syntax-only against system SDK; not runtime ABI admission or delivery evidence'}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--syntax',action='store_true');args=parser.parse_args()
    try:print(json.dumps(check() if args.syntax else {'status':'plan_only','frontendLoaded':False},sort_keys=True))
    except Exception as e:print(json.dumps({'syntaxPassed':False,'reason':type(e).__name__}));raise SystemExit(2)
