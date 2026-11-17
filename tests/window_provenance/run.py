#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Two owned native toplevels on private software Sway; plan-only by default."""
import argparse,hashlib,importlib.util,json,os,resource,socket,stat,subprocess,tempfile,time
from pathlib import Path
INSIDE=Path(__file__)==Path('/lab/runner.py')
ROOT=Path('/lab') if INSIDE else Path(__file__).resolve().parents[2]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
base=load('window_base',ROOT/'fcitx_lab_runner.py' if INSIDE else ROOT/'tools/fcitx_lab_runner.py')
BUILD=ROOT/'build/window-provenance'
XMLS={'xdg-shell':Path('/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml'),
      'foreign-list':Path('/usr/share/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml'),
      'wlr-foreign':ROOT/'vendor/wayland/wlr-foreign-toplevel-management-unstable-v1.xml',
      'hyprland-mapping':ROOT/'vendor/wayland/hyprland-toplevel-mapping-v1.xml'}
RUNTIME=('sway','libwlroots-0.20.so','libliftoff.so.0')
def directory():
    for p in (ROOT/'build',BUILD):
        p.mkdir(mode=0o700,exist_ok=True);info=p.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o022:raise base.Refused('build-directory')

def inputs():
    source=ROOT/'tests/window_provenance/native_windows.cpp'
    paths={'source':source,**XMLS}
    for path in paths.values():base.regular_file(path)
    return {name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in paths.items()}

def build():
    directory();before=inputs();sources=[]
    for prefix,xml in XMLS.items():
        for kind,suffix in [('client-header','-client.h'),('private-code','-protocol.c')]:
            fd,name=tempfile.mkstemp(prefix='.protocol-',dir=BUILD);os.close(fd)
            try:
                subprocess.run(['/usr/bin/wayland-scanner',kind,str(xml),name],check=True,timeout=5,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                target=BUILD/(prefix+suffix);os.replace(name,target)
                if kind=='private-code':sources.append(str(target))
            finally:Path(name).unlink(missing_ok=True)
    helpers=load('window_helpers',ROOT/'tests/wayland_identity/run.py')
    fd,name=tempfile.mkstemp(prefix='.native-',dir=BUILD);os.close(fd)
    try:
        objects=[]
        for source in sources:
            fd,temporary=tempfile.mkstemp(prefix='.protocol-object-',dir=BUILD);os.close(fd)
            try:
                result=helpers.compile_logged(['/usr/bin/cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-c',source,'-o',temporary],BUILD)
                if result.returncode:raise base.Refused('protocol-compile-failed')
                obj=Path(source).with_suffix('.o');os.replace(temporary,obj);objects.append(str(obj))
            finally:Path(temporary).unlink(missing_ok=True)
        flags=subprocess.check_output(['/usr/bin/pkg-config','--cflags','--libs','wayland-client'],text=True,timeout=5,env={'PATH':'/usr/bin'}).split()
        result=helpers.compile_logged(['/usr/bin/c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(BUILD),str(ROOT/'tests/window_provenance/native_windows.cpp')]+objects+['-o',name]+flags,BUILD)
        if result.returncode:raise base.Refused('compile-failed')
        if inputs()!=before:raise base.Refused('inputs-changed')
        target=BUILD/'native-windows';os.replace(name,target)
        record={'schema':1,'inputs':before,'binarySha256':hashlib.sha256(target.read_bytes()).hexdigest()}
        fd,manifest=tempfile.mkstemp(prefix='.record-',dir=BUILD)
        try:
            with os.fdopen(fd,'w') as out:json.dump(record,out)
            os.replace(manifest,BUILD/'build-record.json')
        finally:Path(manifest).unlink(missing_ok=True)
        return {'schema':1,'built':True,**record}
    finally:Path(name).unlink(missing_ok=True)

def sandbox_args(fds,paths):
    args=['/usr/bin/bwrap','--unshare-all','--die-with-parent','--new-session','--cap-drop','ALL','--clearenv',
      '--ro-bind','/usr','/usr','--symlink','usr/bin','/bin','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
      '--ro-bind-try','/etc/ld.so.cache','/etc/ld.so.cache','--tmpfs','/home','--tmpfs','/run','--tmpfs','/tmp','--dev','/dev',
      '--dir','/home/lab','--dir','/run/lab','--dir','/lab/runtime','--chdir','/home/lab']
    for name,fd in fds.items():args+=['--perms','0755' if name in ('sway','probe') else '0444','--ro-bind-data',str(fd),'/lab/probe' if name=='probe' else '/lab/runtime/'+name]
    for source,target in paths:args+=['--ro-bind',str(source),target]
    for key,value in {'PATH':'/usr/bin','HOME':'/home/lab','XDG_RUNTIME_DIR':'/run/lab','LANG':'C.UTF-8','LD_LIBRARY_PATH':'/lab/runtime',
       'WLR_BACKENDS':'headless','WLR_RENDERER':'pixman','WLR_LIBINPUT_NO_DEVICES':'1','RASKLAD_WINDOW_PROVENANCE':'1'}.items():args+=['--setenv',key,value]
    return args+['--','/usr/bin/python3','-I','/lab/runner.py','--inside']

def native_limits():
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    resource.setrlimit(resource.RLIMIT_CPU,(6,6))
    resource.setrlimit(resource.RLIMIT_NOFILE,(128,128))
    # bwrap materializes sealed runtime libraries before exec (largest ~1.4MiB).
    resource.setrlimit(resource.RLIMIT_FSIZE,(2*1024*1024,2*1024*1024))


def run(registry=False):
    if os.geteuid()==0:raise base.Refused('root-not-admitted')
    directory();manifest=BUILD/'build-record.json';base.regular_file(manifest)
    if manifest.stat().st_size>4096:raise base.Refused('manifest-bound')
    record=json.loads(manifest.read_text())
    if record.get('schema')!=1 or record.get('inputs')!=inputs():raise base.Refused('stale-inputs')
    bwrap=Path('/usr/bin/bwrap');base.regular_file(bwrap,True);meta=bwrap.stat()
    if meta.st_uid!=0 or meta.st_mode&0o6022:raise base.Refused('unsafe-bwrap')
    lock=json.loads((ROOT/'data/wayland-lab-runtime-lock.json').read_text());fds={}
    if len(lock['packages'])!=3 or not all(p.get('signatureVerified') is True for p in lock['packages']):raise base.Refused('runtime-signatures')
    try:
        binary=BUILD/'native-windows';base.regular_file(binary,True);fds['probe']=base.sealed_snapshot(binary,record['binarySha256'])
        for name in RUNTIME:fds[name]=base.sealed_snapshot(ROOT/'build/wayland-ime/runtime'/name,lock['runtimeFiles'][name])
        paths=[(Path(__file__).resolve(),'/lab/runner.py'),(ROOT/'tools/fcitx_lab_runner.py','/lab/fcitx_lab_runner.py')]
        for path,_ in paths:base.regular_file(path)
        with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as errors:
            command=sandbox_args(fds,paths)
            if registry:
                at=command.index('--');command[at:at]=['--setenv','RASKLAD_REGISTRY_ONLY','1']
            result=subprocess.run(command,stdin=subprocess.DEVNULL,stdout=out,stderr=errors,
              env={'PATH':'/usr/bin'},pass_fds=tuple(fds.values()),timeout=15,preexec_fn=native_limits)
            out.seek(0);raw=out.read(4097)
            if not raw:
                errors.seek(0);diagnostic=errors.read(8192)
                fd,name=tempfile.mkstemp(prefix='startup-error-',suffix='.log',dir=BUILD)
                with os.fdopen(fd,'wb') as log:log.write(diagnostic)
        if len(raw)>4096:raise base.Refused('output-bound')
        report=json.loads(raw)
        if result.returncode or report.get('ok') is not True:raise base.Refused('native-probe-failed')
        return dict(report,schema=1,passed=True,sandboxed=True,hostDisplay=False,gpu=False,network=False,realInput=False,
          hostConfigurationChanged=False,inputs=record['inputs'],binarySha256=record['binarySha256'])
    finally:
        for fd in fds.values():os.close(fd)

def inside():
    if (not INSIDE or os.environ.get('RASKLAD_WINDOW_PROVENANCE')!='1' or os.environ.get('HOME')!='/home/lab'
        or os.environ.get('XDG_RUNTIME_DIR')!='/run/lab' or any(os.environ.get(k) for k in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','DBUS_SESSION_BUS_ADDRESS'))):raise base.Refused('not-private-lab')
    os.chmod('/run/lab',0o700)
    config=Path('/tmp/sway.conf');config.write_text('xwayland disable\nseat seat0 fallback true\noutput HEADLESS-1 mode 640x480\nfocus_follows_mouse no\n')
    server=subprocess.Popen(['/lab/runtime/sway','-c',str(config)],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        end=time.monotonic()+3
        while True:
            sockets=[p for p in Path('/run/lab').glob('wayland-*') if p.is_socket()]
            if sockets:break
            if server.poll() is not None or time.monotonic()>=end:raise base.Refused('private-compositor-unready')
            time.sleep(.03)
        if len(sockets)!=1:raise base.Refused('ambiguous-display')
        Path('/run/lab/wayland-lab').symlink_to(sockets[0].name)
        with tempfile.TemporaryFile() as out:
            if os.environ.get('RASKLAD_REGISTRY_ONLY')=='1':
                with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
                    connection.settimeout(2);connection.connect('/run/lab/wayland-lab');connection.settimeout(None)
                    result=subprocess.run(['/lab/probe','--registry-fd='+str(connection.fileno())],pass_fds=(connection.fileno(),),stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.DEVNULL,timeout=8)
            else:result=subprocess.run(['/lab/probe'],stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.DEVNULL,timeout=8)
            out.seek(0);raw=out.read(4097)
        if result.returncode or len(raw)>4096:raise base.Refused('private-client-failed')
        return json.loads(raw)
    finally:
        if server.poll() is None:
            server.terminate()
            try:server.wait(timeout=1)
            except subprocess.TimeoutExpired:server.kill();server.wait(timeout=1)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--registry-only',action='store_true');p.add_argument('--mapping',action='store_true');g=p.add_mutually_exclusive_group();g.add_argument('--build',action='store_true');g.add_argument('--run',action='store_true');g.add_argument('--inside',action='store_true',help=argparse.SUPPRESS);args=p.parse_args()
    try:
        if args.mapping and not args.build:raise base.Refused('mapping-private-hyprland-runner-required')
        report=inside() if args.inside else build() if args.build else run(args.registry_only) if args.run else {'schema':1,'mode':'plan','hostDisplay':False,'fcitxLoaded':False,'fieldBindingVerified':False}
        print(json.dumps(report,sort_keys=True));return 0
    except Exception as exc:
        print(json.dumps({'schema':1,'ok':False,'reason':str(exc) if isinstance(exc,base.Refused) else type(exc).__name__}));return 1
if __name__=='__main__':raise SystemExit(main())
