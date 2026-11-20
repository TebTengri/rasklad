#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Shared native Chromium/Fcitx fixture; called only inside a private lab namespace."""
import hashlib
import json
import os
import select
from pathlib import Path
import subprocess
import time


def input_frontend_absent(returncode, output, error):
    if returncode == 0:
        return b'org.fcitx.Fcitx.InputMethod1' not in output
    return (b'org.freedesktop.DBus.Error.UnknownObject' in error
            or (b'org.freedesktop.DBus.Error.UnknownMethod' in error
                and b'Object does not exist' in error))


def private_frontend_variant(base):
    variant=os.environ.get('RASKLAD_PRIVATE_FRONTEND_VARIANT','system')
    if variant not in ('system','baseline','keep-grab'):
        raise base.Refused('private_frontend_variant')
    return variant


def fcitx_child_env(base):
    variant=private_frontend_variant(base)
    env=None
    if variant=='keep-grab':
        env=dict(os.environ,RASKLAD_FCITX_KEEP_GRAB_LAB='1',RASKLAD_KEEP_GRAB_INACTIVE_RAW='1')
    if os.environ.get('RASKLAD_CORE_ENTRY_WITNESS_LAB') != '1':
        return env
    if (os.environ.get('RASKLAD_ADDON_WITNESS_LAB') != '1'
            or os.environ.get('RASKLAD_FOCUS_STRESS_LAB') != '1'
            or os.environ.get('RASKLAD_SPACE_TELEMETRY_LAB') != '1'
            or os.environ.get('RASKLAD_ADDON_WITNESS_NO_QUERY_LAB') == '1'):
        raise base.Refused('private_core_entry_witness_required')
    # Never mutate the runner environment: only the own Fcitx child is instrumented.
    return dict(os.environ if env is None else env,LD_PRELOAD='/lab/raskladcoreentry.so')


def prepare_frontend(base):
    if private_frontend_variant(base)=='system':return None
    binary=Path('/lab/addons/libwaylandim.so')
    base.regular_file(binary)
    if binary.stat().st_size>5_000_000:raise base.Refused('private_frontend_size')
    digest=hashlib.sha256(binary.read_bytes()).hexdigest()
    config=Path('/home/lab/.config/fcitx5/conf/waylandim.conf')
    config.parent.mkdir(parents=True,exist_ok=True)
    config.write_text('PersistentVirtualKeyboard=True\nPreferKeyEvent=True\n')
    return binary.stat(),digest


def verify_frontend_mapping(base,fcitx,expected):
    end=time.monotonic()+2
    while time.monotonic()<end:
        if fcitx.poll() is not None:raise base.Refused('fcitx_exited')
        with open(f'/proc/{fcitx.pid}/maps','rb') as stream:raw=stream.read(2_000_001)
        if len(raw)>2_000_000:raise base.Refused('private_frontend_maps_bound')
        for line in raw.splitlines():
            fields=line.split(None,5)
            if len(fields)<5:continue
            try:
                major,minor=(int(x,16) for x in fields[3].split(b':'))
                matches=(os.makedev(major,minor)==expected.st_dev and int(fields[4])==expected.st_ino)
            except ValueError:continue
            if matches:return True
        time.sleep(.03)
    raise base.Refused('private_frontend_not_mapped')


def wait_seed_keyboard(base,child):
    expected=b'{"ready":true}\n';raw=b'';end=time.monotonic()+2
    while time.monotonic()<end:
        if child.poll() is not None:raise base.Refused('private_seed_keyboard_exited')
        remaining=end-time.monotonic()
        if remaining<=0:break
        ready,_,_=select.select([child.stdout],[],[],remaining)
        if not ready:break
        chunk=os.read(child.stdout.fileno(),len(expected)+1-len(raw))
        if not chunk:raise base.Refused('private_seed_keyboard_eof')
        raw+=chunk
        if b'\n' in raw:
            if raw!=expected:raise base.Refused('private_seed_keyboard_ready')
            return True
        if len(raw)>=len(expected):raise base.Refused('private_seed_keyboard_ready')
    raise base.Refused('private_seed_keyboard_timeout')


def seed_keyboard(base,children):
    if private_frontend_variant(base)=='system':return False
    # Setup control: establish a synthetic seat/keymap before PersistentVirtualKeyboard.
    # Keep stdin open without sending input; the browser driver owns its input helper.
    child=subprocess.Popen(['/lab/rasklad-wayland-keys'],stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
    children.append(child)
    return wait_seed_keyboard(base,child)


def slack_gate_pipe(base,child_options):
    """Offline Slack-session gate test: Fcitx gets only the read end of the arm pipe."""
    if os.environ.get('RASKLAD_SLACK_GATE_LAB')!='1':return None
    if os.environ.get('RASKLAD_SLACK_SESSION_LAB')!='1' or private_frontend_variant(base)!='system':raise base.Refused('slack_gate_environment')
    read_end,write_end=os.pipe()  # both O_CLOEXEC; only the read end is passed on
    env=dict(child_options.get('env') or os.environ,RASKLAD_SESSION_ARM_FD=str(read_end))
    child_options.update(env=env,pass_fds=(read_end,))
    return read_end,write_end


def fd_present(pid,fd):
    """True if `pid` holds a writable descriptor of the same pipe as our write end `fd`.
    Both pipe ends share one inode, so the access mode from fdinfo tells them apart."""
    target=os.fstat(fd)
    for entry in os.scandir(f'/proc/{pid}/fd'):
        try:
            st=os.stat(entry.path)
            if (st.st_dev,st.st_ino)!=(target.st_dev,target.st_ino):continue
            flags=next(line for line in Path(f'/proc/{pid}/fdinfo/{entry.name}').read_text().splitlines() if line.startswith('flags:'))
        except (OSError,StopIteration):return True  # cannot prove absence: fail closed
        if int(flags.split()[1],8)&os.O_ACCMODE!=os.O_RDONLY:return True
    return False


def run(base, children):
    if (os.environ.get('RASKLAD_NATIVE_WAYLAND_LAB') != '1'
            or os.environ.get('HOME') != '/home/lab'
            or os.environ.get('XDG_RUNTIME_DIR') != '/run/lab'
            or os.environ.get('WAYLAND_DISPLAY') != 'wayland-lab'
            or os.environ.get('DISPLAY') or os.environ.get('WAYLAND_SOCKET')
            or os.environ.get('RASKLAD_PARENT_FD')):
        raise base.Refused('private_native_clients_required')
    frontend=prepare_frontend(base)
    seeded=seed_keyboard(base,children)
    fcitx_env=fcitx_child_env(base)
    child_options={} if fcitx_env is None else {'env':fcitx_env}
    gate=slack_gate_pipe(base,child_options)
    fcitx=subprocess.Popen(['/usr/bin/fcitx5','-D','-k','--disable','all','--enable','keyboard,dbus,wayland,waylandim,raskladlab','--verbose','*=0'],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,**child_options);children.append(fcitx)
    end=time.monotonic()+4
    while time.monotonic()<end:
     if fcitx.poll() is not None:raise base.Refused('fcitx_exited')
     p=subprocess.run(['/usr/bin/gdbus','call','--session','--dest','org.freedesktop.DBus','--object-path','/org/freedesktop/DBus','--method','org.freedesktop.DBus.NameHasOwner','org.fcitx.Fcitx5'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=.3)
     if p.stdout==b'(true,)\n':break
     time.sleep(.03)
    else:raise base.Refused('fcitx_timeout')
    factory=subprocess.run(['/usr/bin/gdbus','call','--session','--dest','org.fcitx.Fcitx5','--object-path','/inputmethod','--method','org.freedesktop.DBus.Introspectable.Introspect'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=.5)
    dbus_input_absent=input_frontend_absent(factory.returncode,factory.stdout,factory.stderr)
    if fcitx.poll() is not None:raise base.Refused('fcitx_exited')
    if not dbus_input_absent:raise base.Refused('unexpected_dbus_input_frontend')
    if frontend:verify_frontend_mapping(base,fcitx,frontend[0])
    driver_env=dict(os.environ,RASKLAD_OWN_FCITX_PID=str(fcitx.pid),RASKLAD_CLK_TCK=str(os.sysconf('SC_CLK_TCK')))
    gate_report={}
    if gate:
        read_end,write_end=gate
        os.close(read_end)  # only Fcitx keeps the read end
        gate_report['armWriteEndAbsentFromFcitx']=not fd_present(fcitx.pid,write_end)
        if not gate_report['armWriteEndAbsentFromFcitx']:raise base.Refused('arm_write_end_leaked')
        driver_env['RASKLAD_SESSION_ARM_WRITE_FD']=str(write_end)
        try:
            p=subprocess.run(['/usr/bin/node','/lab/driver.mjs'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=50,env=driver_env,pass_fds=(write_end,))
        finally:
            os.close(write_end)  # driver revokes by writing a byte and closing its copy
    else:
        p=subprocess.run(['/usr/bin/node','/lab/driver.mjs'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=50,env=driver_env)
    if len(p.stdout)>32768:raise base.Refused('driver_output_bound')
    r=json.loads(p.stdout)
    r.update(gate_report)
    if gate and r.get('armWriteEndAbsentFromFcitx') is not True:r['passed']=False
    if frontend:r.update(privateFrontendMapped=True,privateFrontendArtifactSha256=frontend[1],privateSeedKeyboard=seeded)
    r['frontendVariant']=private_frontend_variant(base)
    r['driverExitCode']=p.returncode;r['dbusInputFrontendAbsent']=dbus_input_absent;r['passed']=p.returncode==0 and r.get('passed') is True and dbus_input_absent;return r
