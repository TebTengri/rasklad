#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Plan-first core Wayland ID namespace counterexample; never a host connection."""
import argparse,hashlib,importlib.util,json,os,resource,shlex,stat,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/wayland-identity'
spec=importlib.util.spec_from_file_location('identity_base',ROOT/'tools/fcitx_lab_runner.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)

def directory():
    for path in (ROOT/'build',BUILD):
        path.mkdir(mode=0o700,exist_ok=True);info=path.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o022:raise base.Refused('build-directory')
    return BUILD

def limits():
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    resource.setrlimit(resource.RLIMIT_CPU,(6,6))
    resource.setrlimit(resource.RLIMIT_NOFILE,(64,64))
    resource.setrlimit(resource.RLIMIT_FSIZE,(65536,65536))

def compile_logged(command,where,compiler=subprocess.run):
    fd,name=tempfile.mkstemp(prefix='.compile-log-',dir=where)
    try:
        with os.fdopen(fd,'wb') as log:
            result=compiler(command,stdin=subprocess.DEVNULL,stdout=log,stderr=log,env={'PATH':'/usr/bin'},timeout=40)
        return result
    finally:
        # Replace the directory entry, never open/truncate a possibly linked target.
        try:os.replace(name,where/'compile.log')
        finally:Path(name).unlink(missing_ok=True)


def build():
    directory();source=ROOT/'tests/wayland_identity/object_id_scope.cpp';base.regular_file(source)
    source_digest=hashlib.sha256(source.read_bytes()).hexdigest()
    flags=subprocess.check_output(['/usr/bin/pkg-config','--cflags','--libs','wayland-client','wayland-server'],env={'PATH':'/usr/bin'},timeout=5,text=True)
    target=BUILD/'object-id-scope'
    if target.is_symlink():raise base.Refused('binary-symlink')
    fd,temp=tempfile.mkstemp(prefix='.identity-',dir=BUILD);os.close(fd)
    try:
        result=compile_logged(['/usr/bin/c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(source),'-pthread','-o',temp]+shlex.split(flags),BUILD)
        if result.returncode:raise base.Refused('compile-failed')
        os.replace(temp,target)
    finally:Path(temp).unlink(missing_ok=True)
    if hashlib.sha256(source.read_bytes()).hexdigest()!=source_digest:raise base.Refused('source-changed-during-build')
    record={'schema':1,'built':True,'sourceSha256':source_digest,'binarySha256':hashlib.sha256(target.read_bytes()).hexdigest()}
    manifest=BUILD/'build-record.json'
    if manifest.is_symlink():raise base.Refused('manifest-symlink')
    fd,temporary=tempfile.mkstemp(prefix='.record-',dir=BUILD)
    try:
        with os.fdopen(fd,'w') as output:json.dump(record,output)
        os.replace(temporary,manifest)
    finally:Path(temporary).unlink(missing_ok=True)
    return record

def validate_record(record,source_digest):
    if type(record) is not dict or record.get('schema')!=1 or record.get('built') is not True:raise base.Refused('build-record')
    for key in ('sourceSha256','binarySha256'):
        value=record.get(key)
        if type(value) is not str or len(value)!=64 or set(value)-set('0123456789abcdef'):raise base.Refused('build-record')
    if record['sourceSha256']!=source_digest:raise base.Refused('stale-source')

def sandbox_args(fd):
    return ['/usr/bin/bwrap','--unshare-all','--die-with-parent','--new-session','--cap-drop','ALL','--clearenv',
            '--ro-bind','/usr','/usr','--symlink','usr/bin','/bin','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
            '--ro-bind-try','/etc/ld.so.cache','/etc/ld.so.cache','--tmpfs','/home','--tmpfs','/run','--tmpfs','/tmp',
            '--dev','/dev','--dir','/lab','--perms','0755','--ro-bind-data',str(fd),'/lab/probe','--chdir','/lab',
            '--setenv','PATH','/usr/bin','--','/lab/probe']

def run():
    if os.geteuid()==0:raise base.Refused('root-not-admitted')
    directory();binary=BUILD/'object-id-scope';base.regular_file(binary,True)
    bwrap=Path('/usr/bin/bwrap');base.regular_file(bwrap,True);info=bwrap.stat()
    if info.st_uid!=0 or info.st_mode&0o6022:raise base.Refused('unsafe-bwrap')
    manifest=BUILD/'build-record.json';base.regular_file(manifest)
    if manifest.stat().st_size>4096:raise base.Refused('manifest-bound')
    source=ROOT/'tests/wayland_identity/object_id_scope.cpp';base.regular_file(source)
    record=json.loads(manifest.read_text())
    validate_record(record,hashlib.sha256(source.read_bytes()).hexdigest())
    fd=base.sealed_snapshot(binary,record['binarySha256'])
    try:
        args=sandbox_args(fd)
        with tempfile.TemporaryFile() as output:
            result=subprocess.run(args,stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.DEVNULL,env={'PATH':'/usr/bin'},pass_fds=(fd,),timeout=10,preexec_fn=limits)
            output.seek(0);raw=output.read(4097)
        if len(raw)>4096:raise base.Refused('output-bound')
        report=json.loads(raw)
        if type(report) is not dict or report.get('ok') is not True or result.returncode:raise base.Refused('counterexample-failed')
        report.update(schema=1,passed=True,hostDisplay=False,hostConfigurationChanged=False,network=False,gpu=False,realInput=False,sandboxed=True,
            sourceSha256=record['sourceSha256'],binarySha256=record['binarySha256'])
        return report
    finally:os.close(fd)

def main():
    p=argparse.ArgumentParser(description=__doc__);g=p.add_mutually_exclusive_group();g.add_argument('--build',action='store_true');g.add_argument('--run',action='store_true');args=p.parse_args()
    try:
        report=build() if args.build else run() if args.run else {'schema':1,'mode':'plan','hostDisplay':False,'nativeToplevels':False,'fcitxLoaded':False,'scope':'core_surface_object_id_namespace_only'}
        print(json.dumps(report,sort_keys=True));return 0
    except Exception as exc:
        print(json.dumps({'schema':1,'passed':False,'reason':str(exc) if isinstance(exc,base.Refused) else type(exc).__name__}));return 1
if __name__=='__main__':raise SystemExit(main())
