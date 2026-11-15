#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build only the pinned LGPL frontend in a closed namespace. Default inert."""
import argparse,hashlib,importlib.util,json,os,resource,signal,stat,subprocess,sys,tarfile,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
INSIDE=Path(__file__)==Path('/lab/build_frontend.py')
DIRS=['core','input-method','input-method-v2','plasma-window-management','wlr-foreign-toplevel-management-unstable-v1','ext-foreign-toplevel-list','cosmic-toplevel-info-unstable-v1']

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def inside():
    if (not INSIDE or os.environ.get('RASKLAD_FCITX_BUILD_LAB')!='1' or os.environ.get('HOME')!='/home/lab'
            or any(k in os.environ for k in ['DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','DBUS_SESSION_BUS_ADDRESS'])):
        raise ValueError('private_build_required')
    source=Path('/lab/source');out=Path('/lab/build');proto=out/'protocols';proto.mkdir(exist_ok=True)
    xmls=[]
    for part in DIRS:xmls+=list((source/'src/lib/fcitx-wayland'/part).glob('*.xml'))
    share=Path('/usr/share/wayland-protocols')
    xmls += [share/'unstable/input-method/input-method-unstable-v1.xml',
             share/'unstable/text-input/text-input-unstable-v1.xml',
             share/'unstable/text-input/text-input-unstable-v3.xml',
             share/'staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml',
             share/'staging/ext-workspace/ext-workspace-v1.xml']
    seen=set()
    for xml in xmls:
        if xml.stem in seen:raise ValueError('duplicate_protocol')
        seen.add(xml.stem)
        for action,suffix in [('client-header','-client-protocol.h'),('private-code','-protocol.c')]:
            with tempfile.TemporaryFile() as generated:
                subprocess.run(['/usr/bin/wayland-scanner',action,str(xml),'/proc/self/fd/'+str(generated.fileno())],pass_fds=(generated.fileno(),),check=True,timeout=5)
                generated.seek(0);payload=generated.read(2_000_001)
            if len(payload)>2_000_000:raise ValueError('generated_protocol_bound')
            target=proto/('wayland-'+xml.stem+suffix)
            if not target.exists() or target.read_bytes()!=payload:target.write_bytes(payload)
    config=b'#pragma once\n#define HAVE_PIPE2 1\n'
    if not (out/'config.h').exists() or (out/'config.h').read_bytes()!=config:(out/'config.h').write_bytes(config)
    subprocess.run(['/usr/bin/qmake6','/lab/frontend.pro'],cwd=out,check=True,timeout=30)
    subprocess.run(['/usr/bin/make','-j2'],cwd=out,check=True,timeout=240)

def build():
    if os.geteuid()==0:raise ValueError('root_not_admitted')
    base=load('frontend_base',ROOT/'tools/fcitx_lab_runner.py')
    load('frontend_abi',ROOT/'tools/core_entry_witness_guard.py').validate_core_entry(base,ROOT)
    bwrap=Path('/usr/bin/bwrap');base.regular_file(bwrap,True);info=bwrap.stat()
    if info.st_uid!=0 or info.st_mode&0o6022:raise ValueError('unsafe_bwrap')
    source=ROOT/'build/wayland-ime/upstream/fcitx5-tree-5.1.23'
    if source.is_symlink() or not source.resolve().is_relative_to((ROOT/'build').resolve()):raise ValueError('source_root')
    archive=ROOT/'build/wayland-ime/upstream/fcitx5-5.1.23.tar.gz'
    lock=json.loads((ROOT/'data/fcitx-frontend-source-lock.json').read_bytes())
    base.regular_file(archive)
    if archive.stat().st_size!=lock['archiveBytes'] or hashlib.sha256(archive.read_bytes()).hexdigest()!=lock['archiveSha256']:
        raise ValueError('source_archive_pin')
    expected={};total=0;members=0
    with tarfile.open(archive) as tar:
        for item in tar:
            members+=1
            if members>2000:raise ValueError('source_member_bound')
            total+=item.size
            if item.size>10_000_000 or total>100_000_000:raise ValueError('source_size_bound')
            parts=Path(item.name).parts
            if not parts or parts[0]!='fcitx5-5.1.23' or '..' in parts:raise ValueError('source_archive_path')
            if item.isdir():continue
            if item.issym() and parts[1:3]==('data','icon'):continue
            if not item.isfile():raise ValueError('nonregular_source')
            path=source.joinpath(*parts[1:])
            if path.is_symlink() or not path.resolve().is_relative_to(source.resolve()):raise ValueError('source_escape')
            base.regular_file(path)
            with tar.extractfile(item) as stream:payload=stream.read(item.size+1)
            if len(payload)!=item.size or path.read_bytes()!=payload:raise ValueError('source_tree_mismatch')
            expected[str(path.relative_to(source))]=hashlib.sha256(payload).hexdigest()
    actual={str(p.relative_to(source)) for p in source.rglob('*') if p.is_file()}
    if actual!=set(expected):raise ValueError('source_tree_inventory')
    target=ROOT
    for part in ['build','wayland-ime','frontend-baseline']:
        target/=part;target.mkdir(mode=0o700,exist_ok=True);info=target.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o022:raise ValueError('build_directory')
    for path in target.rglob('*'):
        if path.is_symlink() or (path.is_file() and path.stat().st_nlink!=1):raise ValueError('unsafe_build_output')
    args=['/usr/bin/bwrap','--unshare-all','--die-with-parent','--new-session','--cap-drop','ALL','--clearenv',
          '--ro-bind','/usr','/usr','--symlink','usr/bin','/bin','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
          '--ro-bind-try','/etc/ld.so.cache','/etc/ld.so.cache','--tmpfs','/home','--tmpfs','/tmp','--tmpfs','/run',
          '--dir','/home/lab','--dir','/lab','--proc','/proc','--dev','/dev',
          '--ro-bind',str(source),'/lab/source','--bind',str(target),'/lab/build',
          '--ro-bind',str(ROOT/'tools/fcitx_frontend_lab.pro'),'/lab/frontend.pro',
          '--ro-bind',str(ROOT/'tools/fcitx_frontend_build.py'),'/lab/build_frontend.py',
          '--setenv','HOME','/home/lab','--setenv','PATH','/usr/bin','--setenv','RASKLAD_FCITX_BUILD_LAB','1',
          '--chdir','/lab/build','--','/usr/bin/python3','-I','/lab/build_frontend.py','--inside']
    def limits():
        for kind,value in [(resource.RLIMIT_CORE,0),(resource.RLIMIT_CPU,300),(resource.RLIMIT_AS,2<<30),(resource.RLIMIT_NOFILE,1024),(resource.RLIMIT_FSIZE,128<<20)]:
            resource.setrlimit(kind,(value,value))
    with (target/'build.log').open('wb') as log:
        child=subprocess.Popen(args,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,env={'PATH':'/usr/bin'},start_new_session=True,preexec_fn=limits)
        try:rc=child.wait(timeout=300)
        finally:
            if child.poll() is None:
                os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
    if rc:raise ValueError('frontend_build_failed')
    binary=target/'libwaylandim.so';base.regular_file(binary)
    if any(hashlib.sha256((source/p).read_bytes()).hexdigest()!=h for p,h in expected.items()):raise ValueError('source_changed_during_build')
    return {'schema':1,'built':True,'variant':'unmodified_source_control','sourceCommit':lock['commit'],
            'sourceArchiveSha256':lock['archiveSha256'],'sourceFilesVerified':len(expected),
            'artifactSha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'hostDisplay':False,'network':False,'gpu':False,'installedLibrariesChanged':False,'executedFrontend':False}

def main():
    parser=argparse.ArgumentParser(description=__doc__);m=parser.add_mutually_exclusive_group();m.add_argument('--build',action='store_true');m.add_argument('--inside',action='store_true',help=argparse.SUPPRESS);a=parser.parse_args()
    if not a.build and not a.inside:print(json.dumps({'schema':1,'status':'plan_only','systemChanges':False}));return 0
    try:
        if a.inside:inside();return 0
        result=build()
    except Exception as e:
        print(json.dumps({'schema':1,'built':False,'reason':str(e) if type(e) is ValueError else type(e).__name__}));return 2
    print(json.dumps(result,sort_keys=True));return 0
if __name__=='__main__':raise SystemExit(main())
