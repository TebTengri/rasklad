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
    import socket
    receipts,addon_end=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
    env=dict(child_options.get('env') or os.environ,RASKLAD_SESSION_ARM_FD=str(read_end),RASKLAD_SESSION_RECEIPT_FD=str(addon_end.fileno()))
    child_options.update(env=env,pass_fds=(read_end,addon_end.fileno()))
    return read_end,write_end,receipts,addon_end


def write_end_holders(fd):
    """PIDs other than ours that hold a writable descriptor of our pipe."""
    holders=[]
    for entry in os.scandir('/proc'):
        if not entry.name.isdigit() or int(entry.name)==os.getpid():continue
        try:
            if fd_present(int(entry.name),fd):holders.append(int(entry.name))
        except OSError:
            continue  # process exited during the scan
    return holders


def run_gate_driver(base,fcitx,gate,driver_env):
    """The supervisor role stays in this process: the write end never leaves it.
    The driver asks for revoke with one stdout line and waits for the ack on stdin."""
    read_end,write_end,receipts,addon_end=gate
    addon_end.close()  # only Fcitx keeps its receipt end
    report={'revokeServed':False}
    try:
        os.close(read_end);read_end=None  # only Fcitx keeps the read end
        driver=subprocess.Popen(['/usr/bin/node','/lab/driver.mjs'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,env=driver_env)
        out=b'';end=time.monotonic()+50
        try:
            while True:
                remaining=end-time.monotonic()
                if remaining<=0:raise subprocess.TimeoutExpired('driver',50)
                ready,_,_=select.select([driver.stdout],[],[],remaining)
                if not ready:continue
                chunk=os.read(driver.stdout.fileno(),65536)
                if not chunk:break
                out+=chunk
                if len(out)>32768:raise base.Refused('driver_output_bound')
                if b'{"gate":"revoke"}\n' in out and not report['revokeServed']:
                    out=out.replace(b'{"gate":"revoke"}\n',b'',1)
                    holders=write_end_holders(write_end)
                    report['armWriteEndOnlyInSupervisor']=not holders
                    os.write(write_end,b'x');os.close(write_end);write_end=None
                    report['revokeServed']=True
                    driver.stdin.write(b'revoked\n');driver.stdin.flush();driver.stdin.close()
            if 'armWriteEndOnlyInSupervisor' not in report:
                report['armWriteEndOnlyInSupervisor']=not write_end_holders(write_end)
            driver.wait(timeout=5)
        finally:
            if driver.poll() is None:driver.kill();driver.wait()
            if not driver.stdin.closed:driver.stdin.close()
            driver.stdout.close()
        return subprocess.CompletedProcess(driver.args,driver.returncode,out,None),report
    finally:
        receipts.close()
        for fd in (read_end,write_end):
            if fd is not None:os.close(fd)


class DriverLink:
    """Line protocol with the observing driver (stdout requests/answers, stdin commands)."""
    def __init__(self,base,driver,deadline):
        self.base,self.driver,self.deadline,self.buffer,self.total=base,driver,deadline,b'',0
    def line(self):
        while b'\n' not in self.buffer:
            remaining=self.deadline-time.monotonic()
            if remaining<=0:raise self.base.Refused('driver_timeout')
            ready,_,_=select.select([self.driver.stdout],[],[],remaining)
            if not ready:continue
            chunk=os.read(self.driver.stdout.fileno(),65536)
            if not chunk:raise self.base.Refused('driver_eof')
            self.total+=len(chunk)
            if self.total>65536:raise self.base.Refused('driver_output_bound')
            self.buffer+=chunk
        line,self.buffer=self.buffer.split(b'\n',1)
        return json.loads(line)
    def ask(self,command):
        self.driver.stdin.write(command.encode('ascii')+b'\n');self.driver.stdin.flush()
        return self.line()


def helper_ack(base,helper,timeout=4):
    raw=b'';end=time.monotonic()+timeout
    while not raw.endswith(b'\n'):
        remaining=end-time.monotonic()
        if remaining<=0:raise base.Refused('helper_ack_timeout')
        ready,_,_=select.select([helper.stdout],[],[],remaining)
        if ready:
            chunk=os.read(helper.stdout.fileno(),64)
            if not chunk:raise base.Refused('helper_eof')
            raw+=chunk
    if raw!=b'{"ok":true}\n':raise base.Refused('helper_ack')


def run_supervised_driver(base,fcitx,gate,driver_env,children):
    """The real Supervisor drives the fixed 'correction' script through the helper.
    Only this process holds the helper's stdin and the arm write end; the driver
    only observes the owned field."""
    sup=load_lab_module('slack_supervisor_lab',Path('/lab/slack_supervisor.py'))
    inj=sup.inj
    read_end,write_end,receipts,addon_end=gate
    addon_end.close()  # only Fcitx keeps its receipt end
    report={}
    os.close(read_end);read_end=None
    helper=subprocess.Popen(['/lab/rasklad-wayland-keys'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
    children.append(helper)
    driver=None
    try:
        ready,_,_=select.select([helper.stdout],[],[],3)
        if not ready or os.read(helper.stdout.fileno(),32)!=b'{"ready":true}\n':raise base.Refused('helper_ready')
        os.set_blocking(helper.stdin.fileno(),False)  # Supervisor contract: write must not block
        def write(data):
            # Non-blocking contract: wait briefly for room, never block, never write partially.
            _,writable,_=select.select([],[helper.stdin],[],1)
            if not writable:raise OSError('helper stdin full')
            if os.write(helper.stdin.fileno(),data)!=len(data):raise OSError('short helper write')
        def stop_helper():
            if not helper.stdin.closed:helper.stdin.close()  # no further command can be written
            if helper.poll() is None:
                helper.terminate()
                try:helper.wait(timeout=1)
                except subprocess.TimeoutExpired:helper.kill();helper.wait()
        driver=subprocess.Popen(['/usr/bin/node','/lab/driver.mjs'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,env=driver_env)
        link=DriverLink(base,driver,time.monotonic()+50)
        hello=link.line()
        pid=hello.get('chromiumPid')
        if type(pid) is not int or pid<=1:raise base.Refused('driver_hello')
        binding=(pid,0,0,'normal')  # lab binding; the real one comes from the compositor
        s=sup.Supervisor([pid],'correction')
        if not (s.arm(*binding) and s.seat_locked()):raise base.Refused('supervisor_arm')
        script=inj.script('correction')
        report['supervisedReceiptChecked']=False
        for command in script:
            if command=='backspace' and not s._receipt_ok:
                # Receipt from the addon (kind + length, no text), not from the observer.
                ready,_,_=select.select([receipts],[],[],2)
                record=receipts.recv(16) if ready else None
                report['supervisedReceiptChecked']=True
                report['supervisedReceiptMatched']=s.commit_receipt(record)
                report['supervisedReceiptFromAddon']=record==inj.receipt('correction')
                # Observer cross-check only (lab): the field shows the expected text.
                report['observerSawExpectedText']=link.ask('observe').get('value')==inj.EXPECTED['correction']
                if not report['supervisedReceiptMatched']:break
            token=s.issue(command,*binding)
            if token is None or not s.send(token,command,*binding,write):raise base.Refused('supervisor_send')
            helper_ack(base,helper)
            time.sleep(.05)
        report['supervisedScriptCompleted']=s.done() and s.sent==len(script) and report['supervisedReceiptChecked']
        report['supervisedFieldCleaned']=link.ask('observe').get('value')==''
        report['helperStdinOnlyInSupervisor']=not write_end_holders(helper.stdin.fileno())
        # F12: revoke the supervisor and the addon arm pipe (byte + close).
        before=link.ask('observe')
        report['armWriteEndOnlyInSupervisor']=not write_end_holders(write_end)
        s.event('f12')
        os.write(write_end,b'x');os.close(write_end);write_end=None
        stop_helper()
        report['supervisedRevokedRefusesNext']=s.issue('backspace',*binding) is None and s.state=='REVOKED'
        time.sleep(.3)
        after=link.ask('observe')
        report['supervisedNothingAfterRevoke']=(after.get('value')==before.get('value') and after.get('events')==before.get('events')
                                                and helper.stdin.closed and helper.poll() is not None and s.sent==len(script))
        report['supervisorSent']=s.sent
        link.ask('done')
        out=link.buffer
        driver.stdin.close()
        rest=b''
        end=time.monotonic()+10
        while True:
            ready,_,_=select.select([driver.stdout],[],[],max(0,end-time.monotonic()))
            if not ready:raise base.Refused('driver_report_timeout')
            chunk=os.read(driver.stdout.fileno(),65536)
            if not chunk:break
            rest+=chunk
            if len(rest)>32768:raise base.Refused('driver_output_bound')
        driver.wait(timeout=5)
        return subprocess.CompletedProcess(driver.args,driver.returncode,out+rest,None),report
    finally:
        receipts.close()
        if not helper.stdin.closed:helper.stdin.close()
        if helper.poll() is None:helper.kill();helper.wait()
        if driver is not None:
            if driver.poll() is None:driver.kill();driver.wait()
            if not driver.stdin.closed:driver.stdin.close()
            driver.stdout.close()
        for fd in (read_end,write_end):
            if fd is not None:os.close(fd)


def load_lab_module(name,path):
    import importlib.util
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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
    if gate and os.environ.get('RASKLAD_SLACK_SUPERVISED_LAB')=='1':
        p,gate_report=run_supervised_driver(base,fcitx,gate,driver_env,children)
    elif gate:
        p,gate_report=run_gate_driver(base,fcitx,gate,driver_env)
    else:
        p=subprocess.run(['/usr/bin/node','/lab/driver.mjs'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=50,env=driver_env)
    if len(p.stdout)>32768:raise base.Refused('driver_output_bound')
    r=json.loads(p.stdout)
    r.update(gate_report)
    if gate:
        r['fcitxAliveAfterGate']=fcitx.poll() is None
        control=os.environ.get('RASKLAD_SLACK_GATE_CONTROL')=='1'
        supervised=os.environ.get('RASKLAD_SLACK_SUPERVISED_LAB')=='1'
        required=(['armWriteEndOnlyInSupervisor'] if control else
                  ['armWriteEndOnlyInSupervisor','supervisedScriptCompleted','supervisedReceiptChecked','supervisedReceiptMatched','supervisedReceiptFromAddon','observerSawExpectedText','supervisedFieldCleaned',
                   'supervisedRevokedRefusesNext','supervisedNothingAfterRevoke','helperStdinOnlyInSupervisor'] if supervised else
                  ['armWriteEndOnlyInSupervisor','revokeServed'])
        if not r['fcitxAliveAfterGate'] or not all(r.get(k) is True for k in required):r['passed']=False
    if frontend:r.update(privateFrontendMapped=True,privateFrontendArtifactSha256=frontend[1],privateSeedKeyboard=seeded)
    r['frontendVariant']=private_frontend_variant(base)
    r['driverExitCode']=p.returncode;r['dbusInputFrontendAbsent']=dbus_input_absent;r['passed']=p.returncode==0 and r.get('passed') is True and dbus_input_absent;return r
