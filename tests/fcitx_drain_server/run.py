#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual Fcitx old-grab fixture. Default inert; --run only after source review."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import selectors
import stat
import subprocess
import sys
import tempfile
import time
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/wayland-ime/fcitx-drain-server'
PINS={'tests/input_lab/protocol/input-method-unstable-v2.xml':'99414dbad9458e71aa1fa01bc45f94ca6685787bfcb4d98948f72c1b45b60703','tests/wayland_ime/protocol/virtual-keyboard-unstable-v1.xml':'7ad7870003ecd592cae47dc19d277a609b7f18fd7b7be012623cf3225a7294f5'}
def base():
    spec=importlib.util.spec_from_file_location('base',ROOT/'tools/fcitx_lab_runner.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def build():
    b=base();parent=ROOT
    for name in ('build','wayland-ime','fcitx-drain-server'):
        parent/=name;parent.mkdir(mode=0o700,exist_ok=True);info=parent.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o022:raise b.Refused('unsafe_build_directory')
    for (relative,pin),prefix in zip(PINS.items(),('im','vk')):
        path=ROOT/relative;b.regular_file(path)
        if hashlib.sha256(path.read_bytes()).hexdigest()!=pin:raise b.Refused('protocol_pin')
        for kind,suffix in [('server-header','server.h'),('private-code','protocol.c')]:
            fd,tmp=tempfile.mkstemp(dir=BUILD);os.close(fd)
            try:
                subprocess.run(['/usr/bin/wayland-scanner',kind,str(path),tmp],check=True,timeout=5,env={'PATH':'/usr/bin'},stdout=subprocess.DEVNULL)
                target=BUILD/f'{prefix}-{suffix}'
                if target.is_symlink():raise b.Refused('generated_symlink')
                os.replace(tmp,target)
            finally:Path(tmp).unlink(missing_ok=True)
    for cmd in (['/usr/bin/qmake6',str(ROOT/'tests/fcitx_drain_server/server.pro')],['/usr/bin/make','-j2']):
        subprocess.run(cmd,cwd=BUILD,check=True,timeout=40,env={'PATH':'/usr/bin'},stdout=subprocess.DEVNULL)
    return {'schema':1,'built':True,'fcitxStarted':False}

def read_line(child,timeout):
    deadline=time.monotonic()+timeout
    payload=bytearray()
    with selectors.DefaultSelector() as sel:
        sel.register(child.stdout,selectors.EVENT_READ)
        while True:
            remaining=deadline-time.monotonic()
            if remaining<=0 or not sel.select(remaining):raise RuntimeError('server_timeout')
            byte=os.read(child.stdout.fileno(),1)
            if not byte:raise RuntimeError('server_eof')
            if byte==b'\n':return json.loads(payload)
            payload+=byte
            if len(payload)>4096:raise RuntimeError('report_bound')

def inside(variant):
    if (Path(__file__)!=Path('/lab/runner.py') or os.environ.get('RASKLAD_NATIVE_WAYLAND_LAB')!='1' or os.environ.get('HOME')!='/home/lab' or os.environ.get('XDG_RUNTIME_DIR')!='/run/lab' or os.environ.get('DISPLAY') or os.environ.get('WAYLAND_SOCKET') or os.environ.get('WAYLAND_DISPLAY')!='wayland-lab'):raise RuntimeError('namespace_guard')
    Path('/run/lab').chmod(0o700)
    config=Path('/home/lab/.config/fcitx5');(config/'conf').mkdir(parents=True)
    (config/'profile').write_text('[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=keyboard-us\n\n[Groups/0/Items/0]\nName=keyboard-us\nLayout=\n\n[GroupOrder]\n0=Default\n')
    (config/'conf/waylandim.conf').write_text('PersistentVirtualKeyboard=True\nPreferKeyEvent=True\n')
    children=[]
    try:
        bus=subprocess.Popen(['/usr/bin/dbus-daemon','--session','--nofork','--nopidfile','--address=unix:path=/run/lab/bus','--print-address=1'],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL);children.append(bus)
        # Readiness comes from the daemon itself, never a fixed sleep.
        with selectors.DefaultSelector() as sel:
            sel.register(bus.stdout,selectors.EVENT_READ)
            if not sel.select(2):raise RuntimeError('bus_timeout')
        address=bus.stdout.readline(256)
        if not address.startswith(b'unix:path=/run/lab/bus,guid=') or not address.endswith(b'\n'):
            raise RuntimeError('bus_address')
        del address # Never retain or report the bus identifier.
        server=subprocess.Popen(['/lab/server'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL);children.append(server)
        if read_line(server,2)!={'ready':True}:raise RuntimeError('server_not_ready')
        env=dict(os.environ,DBUS_SESSION_BUS_ADDRESS='unix:path=/run/lab/bus')
        if variant=='drain':env['RASKLAD_FCITX_DRAIN_LAB']='1'
        fcitx=subprocess.Popen(['/usr/bin/fcitx5','-D','-k','--disable','all','--enable','keyboard,dbus,wayland,waylandim','--verbose','*=0'],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=env);children.append(fcitx)
        report=read_line(server,6)
        info=os.stat('/lab/addons/libwaylandim.so');loaded=False
        # Only compare the owned child's mapping identity; never emit maps/paths/PIDs.
        for line in Path(f'/proc/{fcitx.pid}/maps').read_text().splitlines():
            fields=line.split()
            if len(fields)>=6:
                major,minor=(int(x,16) for x in fields[3].split(':'))
                if int(fields[4])==info.st_ino and os.makedev(major,minor)==info.st_dev:loaded=True
        server.stdin.write(b'\n');server.stdin.flush();server.wait(timeout=1)
        expected=0 if variant=='baseline' else 1
        report.update(schema=1,variant=variant,moduleLoaded=loaded,passed=server.returncode==0 and report.get('completed') is True and report.get('vkPress')==expected and report.get('vkRelease')==1 and loaded,hostDisplay=False,gpu=False,network=False,realInput=False,scope='closed actual Fcitx protocol fixture; no GUI or password proof')
        return report
    finally:
        for child in reversed(children):
            if child.poll() is None:child.terminate()
        for child in reversed(children):
            try:child.wait(timeout=1)
            except subprocess.TimeoutExpired:child.kill();child.wait(timeout=1)

def run(variant):
    if variant=='drain':raise RuntimeError('diagnostic_candidate_not_delivery_fix')
    b=base()
    if os.geteuid()==0:raise b.Refused('root_not_admitted')
    paths={'server':BUILD/'fcitx-drain-server','runner.py':Path(__file__),'addons/libwaylandim.so':ROOT/f'build/wayland-ime/frontend-{variant}/libwaylandim.so'}
    load_guard=importlib.util.spec_from_file_location('guard',ROOT/'tools/core_entry_witness_guard.py')
    guard=importlib.util.module_from_spec(load_guard);load_guard.loader.exec_module(guard)
    guard.validate_core_entry(b,ROOT)
    b.regular_file(Path('/usr/bin/bwrap'),True)
    info=Path('/usr/bin/bwrap').stat()
    if info.st_uid!=0 or info.st_mode&0o6022:raise b.Refused('unsafe_bwrap')
    fds=[]
    try:
        args=['/usr/bin/bwrap','--unshare-all','--die-with-parent','--new-session','--cap-drop','ALL','--clearenv','--ro-bind','/usr','/usr','--symlink','usr/bin','/bin','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64','--ro-bind-try','/etc/ld.so.cache','/etc/ld.so.cache','--tmpfs','/home','--tmpfs','/run','--tmpfs','/tmp','--dir','/home/lab','--dir','/run/lab','--dir','/lab/addons','--proc','/proc','--dev','/dev','--chdir','/home/lab']
        hashes={}
        for target,path in paths.items():
            fd=b.sealed_snapshot(path,max_bytes=20000000);fds.append(fd);hashes[target]=hashlib.sha256(os.pread(fd,20000000,0)).hexdigest();args+=['--perms','0755','--ro-bind-data',str(fd),'/lab/'+target]
        for key,value in {'PATH':'/usr/bin','LANG':'C.UTF-8','HOME':'/home/lab','XDG_RUNTIME_DIR':'/run/lab','XDG_CONFIG_HOME':'/home/lab/.config','XDG_DATA_HOME':'/home/lab/.local/share','XDG_CACHE_HOME':'/home/lab/.cache','XDG_DATA_DIRS':'/usr/share','WAYLAND_DISPLAY':'wayland-lab','FCITX_ADDON_DIRS':'/lab/addons:/usr/lib/fcitx5','RASKLAD_NATIVE_WAYLAND_LAB':'1'}.items():args+=['--setenv',key,value]
        def limits():
            for kind,value in ((resource.RLIMIT_CORE,0),(resource.RLIMIT_CPU,8),(resource.RLIMIT_AS,512<<20),(resource.RLIMIT_NOFILE,128),(resource.RLIMIT_FSIZE,2<<20)):resource.setrlimit(kind,(value,value))
        result=subprocess.run(args+['--','/usr/bin/python3','-I','/lab/runner.py','--inside',variant],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=12,pass_fds=tuple(fds),preexec_fn=limits,env={'PATH':'/usr/bin'})
        if len(result.stdout)>4096:raise b.Refused('report_bound')
        report=json.loads(result.stdout);report['artifactSha256']=hashes;report['protocolSha256']=PINS
        if result.returncode or report.get('passed') is not True:raise b.Refused('actual_fcitx_contrast_failed')
        return report
    finally:
        for fd in fds:os.close(fd)

def main():
    p=argparse.ArgumentParser(description=__doc__);g=p.add_mutually_exclusive_group();g.add_argument('--build',action='store_true');g.add_argument('--run',action='store_true');g.add_argument('--inside',choices=['baseline','drain'],help=argparse.SUPPRESS);p.add_argument('--variant',choices=['baseline','drain'],default='baseline');a=p.parse_args()
    try:r=inside(a.inside) if a.inside else build() if a.build else run(a.variant) if a.run else {'schema':1,'status':'plan_only','fcitxStarted':False,'variant':a.variant}
    except Exception as e:print(json.dumps({'schema':1,'passed':False,'reason':str(e) if type(e).__name__ in ('Refused','RuntimeError') else type(e).__name__}));return 2
    print(json.dumps(r,sort_keys=True));return 0 if r.get('passed',True) else 2
if __name__=='__main__':raise SystemExit(main())
