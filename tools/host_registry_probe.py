#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One separately approved host Wayland registry query; no window globals bound."""
import argparse,importlib.util,json,os,re,resource,socket,stat,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
base=load('registry_base',ROOT/'tools/fcitx_lab_runner.py')

def display_path(env,uid):
    runtime=Path(env.get('XDG_RUNTIME_DIR',''))
    name=env.get('WAYLAND_DISPLAY','')
    if not runtime.is_absolute() or not re.fullmatch(r'wayland-[A-Za-z0-9_.-]{1,64}',name):raise base.Refused('display-environment')
    info=runtime.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid!=uid or info.st_mode&0o022:raise base.Refused('runtime-owner')
    path=runtime/name;info=path.lstat()
    if not stat.S_ISSOCK(info.st_mode) or info.st_uid!=uid:raise base.Refused('display-owner')
    return path

def reject_local_search_paths(fd,inspector=subprocess.run):
    with tempfile.TemporaryFile() as output:
        result=inspector(['/usr/bin/readelf','-d','--wide',f'/proc/self/fd/{fd}'],pass_fds=(fd,),
          stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.DEVNULL,env={'PATH':'/usr/bin'},timeout=3,preexec_fn=limits)
        output.seek(0);data=output.read(16385)
    if result.returncode or len(data)>16384 or b'(RPATH)' in data or b'(RUNPATH)' in data:raise base.Refused('dynamic-search-path')

def sanitized_report(report):
    if (type(report) is not dict or report.get('ok') is not True or report.get('registryOnly') is not True
        or type(report.get('globalsBound')) is not int or report['globalsBound']!=0):raise base.Refused('registry-measurement-failed')
    result={'schema':1,'ok':True,'registryOnly':True,'globalsBound':0}
    for key in ('stableListVersion','wlrManagerVersion','hyprlandMappingVersion'):
        value=report.get(key)
        if type(value) is not int or not 0<=value<=100:raise base.Refused('registry-measurement-failed')
        result[key]=value
    return result


def validated_binary():
    runner=load('registry_build',ROOT/'tests/window_provenance/run.py')
    manifest=ROOT/'build/window-provenance/build-record.json';base.regular_file(manifest)
    if manifest.stat().st_size>4096:raise base.Refused('manifest-bound')
    record=json.loads(manifest.read_text())
    if type(record) is not dict or record.get('schema')!=1 or record.get('inputs')!=runner.inputs():raise base.Refused('stale-build')
    digest=record.get('binarySha256')
    if type(digest) is not str or not re.fullmatch('[0-9a-f]{64}',digest):raise base.Refused('binary-hash')
    binary=ROOT/'build/window-provenance/native-windows';base.regular_file(binary,True)
    fd=base.sealed_snapshot(binary,digest)
    try:reject_local_search_paths(fd)
    except BaseException:os.close(fd);raise
    return fd,digest

def peer_is_hyprland(pid,uid,exe,expected,current_uid):
    return pid>0 and uid==current_uid and exe==expected

def limits():
    for kind,value in ((resource.RLIMIT_CORE,0),(resource.RLIMIT_CPU,2),(resource.RLIMIT_NOFILE,64),(resource.RLIMIT_FSIZE,65536)):
        resource.setrlimit(kind,(value,value))

def run():
    if os.geteuid()==0:raise base.Refused('root-not-admitted')
    path=display_path(os.environ,os.getuid())
    expected=Path('/usr/bin/Hyprland').resolve(strict=True);base.regular_file(expected,True)
    if expected.stat().st_uid!=0:raise base.Refused('untrusted-compositor-binary')
    fd,digest=validated_binary()
    try:
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
            connection.settimeout(2);connection.connect(str(path))
            pid,uid,_=struct.unpack('3i',connection.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,struct.calcsize('3i')))
            exe=Path(f'/proc/{pid}/exe').resolve(strict=True)
            if not peer_is_hyprland(pid,uid,exe,expected,os.getuid()):raise base.Refused('wrong-compositor-peer')
            connection.settimeout(None)
            with tempfile.TemporaryFile() as out:
                result=subprocess.run([f'/proc/self/fd/{fd}','--registry-fd='+str(connection.fileno())],
                  pass_fds=(fd,connection.fileno()),stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.DEVNULL,
                  env={'PATH':'/usr/bin'},timeout=8,preexec_fn=limits)
                out.seek(0);raw=out.read(4097)
        if len(raw)>4096:raise base.Refused('output-bound')
        if result.returncode:raise base.Refused('registry-measurement-failed')
        report=sanitized_report(json.loads(raw))
        report.update(hostWaylandConnections=1,peerExeMatched=True,kernelPeerUidMatched=True,binarySha256=digest,
          codeScope={'windowEnumeration':False,'textRead':False,'input':False,'gpu':False,'settingsChanged':False})
        return report

    finally:os.close(fd)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run-approved',action='store_true');a=p.parse_args()
    if not a.run_approved:
        print(json.dumps({'schema':1,'mode':'plan','requiresExplicitApproval':True,'proposedHostWaylandConnections':1,
          'interfaces':['ext_foreign_toplevel_list_v1','zwlr_foreign_toplevel_manager_v1','hyprland_toplevel_mapping_manager_v1'],
          'globalsBound':0,'windowEnumeration':False,'textRead':False,'input':False,'gpu':False,'settingsChanged':False}));return 0
    try:print(json.dumps(run(),sort_keys=True));return 0
    except Exception as exc:
        print(json.dumps({'schema':1,'ok':False,'reason':str(exc) if isinstance(exc,base.Refused) else type(exc).__name__}));return 1
if __name__=='__main__':raise SystemExit(main())
