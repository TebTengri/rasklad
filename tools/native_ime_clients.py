#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Shared native Chromium/Fcitx fixture; called only inside a private lab namespace."""
import hashlib
import json
import re
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


def hyprctl(base,*args,raw=False):
    hypr=[p for p in Path(os.environ['XDG_RUNTIME_DIR'],'hypr').iterdir() if p.is_dir()]
    if len(hypr)!=1:raise base.Refused('hyprland_instance')
    env={'PATH':'/usr/bin','XDG_RUNTIME_DIR':os.environ['XDG_RUNTIME_DIR'],'HYPRLAND_INSTANCE_SIGNATURE':hypr[0].name}
    out=subprocess.run(['/usr/bin/hyprctl',*args],capture_output=True,timeout=3,env=env)
    if len(out.stdout)>65536:raise base.Refused('hyprctl_bound')
    return out.stdout.decode('utf-8','replace').strip()[:200] if raw else out


def run_seat_probe(base,fcitx,gate,driver_env,children):
    """Measurement only (nested Hyprland): do a catch-all submap and per-device
    settings stop (a) the supervisor's helper keyboard and (b) a second, stray
    virtual keyboard standing in for the physical one? Fixture field only."""
    read_end,write_end,receipts,addon_end=gate
    addon_end.close();os.close(read_end)
    report={'seatProbe':True}
    helpers=[];driver=None
    def spawn():
        h=subprocess.Popen(['/lab/rasklad-wayland-keys'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
        children.append(h);helpers.append(h)
        ready,_,_=select.select([h.stdout],[],[],3)
        if not ready or os.read(h.stdout.fileno(),32)!=b'{"ready":true}\n':raise base.Refused('helper_ready')
        return h
    def key(h,command='hello'):
        h.stdin.write(command.encode()+b'\n');h.stdin.flush()
        try:helper_ack(base,h)
        except base.Refused:return False
        return True
    try:
        driver=subprocess.Popen(['/usr/bin/node','/lab/driver.mjs'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,env=driver_env)
        link=DriverLink(base,driver,time.monotonic()+50)
        link.line()
        def delta(h,command='hello'):
            link.ask('clear');time.sleep(.2)
            before=link.ask('observe').get('events')
            acked=key(h,command);time.sleep(.4)
            return {'acked':acked,'events':link.ask('observe').get('events')-before}
        supervisor_kb=spawn()
        report['baselineSupervisorKeyboard']=delta(supervisor_kb)
        stray=spawn()
        report['baselineStrayKeyboard']=delta(stray)
        devices=json.loads(hyprctl(base,'-j','devices').stdout or b'{}')
        names=[k.get('name') for k in devices.get('keyboards',[]) if type(k) is dict]
        report['keyboardNames']=[n for n in names if type(n) is str and len(n)<=64][:8]
        report['submapEnter']=hyprctl(base,'dispatch','submap','rasklad_lock',raw=True)
        report['submapActive']=hyprctl(base,'submap',raw=True)
        report['lockedSupervisorKeyboard']=delta(supervisor_kb)
        report['lockedStrayKeyboard']=delta(stray)
        # Runtime per-device setting: does it take effect without a config reload?
        report['strayKeywordResult']=hyprctl(base,'keyword','device[hl-virtual-keyboard-unknown-1]:keybinds','false',raw=True)
        report['lockedStrayAfterKeyword']=delta(stray)
        report['submapReset']=hyprctl(base,'dispatch','submap','reset',raw=True)
        report['afterResetSupervisorKeyboard']=delta(supervisor_kb)
        report['afterResetStrayKeyboard']=delta(stray)
        report['compositorAlive']=hyprctl(base,'version',raw=True)!=''
        link.ask('done');driver.stdin.close()
        rest=b'';end=time.monotonic()+10
        while True:
            ready,_,_=select.select([driver.stdout],[],[],max(0,end-time.monotonic()))
            if not ready:raise base.Refused('driver_report_timeout')
            chunk=os.read(driver.stdout.fileno(),65536)
            if not chunk:break
            rest+=chunk
        driver.wait(timeout=5)
        report['seatProbeComplete']=True
        return subprocess.CompletedProcess(driver.args,driver.returncode,link.buffer+rest,None),report
    finally:
        receipts.close();os.close(write_end)
        for h in helpers:
            if not h.stdin.closed:h.stdin.close()
            if h.poll() is None:h.kill();h.wait()
        if driver is not None:
            if driver.poll() is None:driver.kill();driver.wait()
            if not driver.stdin.closed:driver.stdin.close()
            driver.stdout.close()


class HyprSeat:
    """Lock the nested seat for the supervisor: exempt only its own virtual keyboard
    from keybinds, then enter the catch-all submap (F12 leaves)."""
    PREFIX='hl-virtual-keyboard-unknown'
    def __init__(self,base):
        self.base,self.exempt,self.locked,self.devices=base,False,False,None
    def keyboards(self):
        """Sorted (name, address) of every keyboard; the address identifies the live device."""
        devices=json.loads(hyprctl(self.base,'-j','devices').stdout or b'{}')
        return sorted((k['name'],k.get('address')) for k in devices.get('keyboards',[])
                      if type(k) is dict and type(k.get('name')) is str and type(k.get('address')) is str)
    def lock(self):
        try:
            before=self.keyboards()
            ours=[d for d in before if d[0].startswith(self.PREFIX)]
            if [d[0] for d in ours]!=[self.PREFIX]:return False  # exactly one helper: ours
            self.exempt=True  # set before the call: cleanup must try to undo it
            if hyprctl(self.base,'keyword',f'device[{self.PREFIX}]:keybinds','false',raw=True)!='ok':return False
            self.locked=True
            if hyprctl(self.base,'dispatch','submap','rasklad_lock',raw=True)!='ok':return False
            # Re-list after the keyword: the exempt name must still be the same live device.
            self.devices=before
            if self.keyboards()!=before:return False
            return self.intact()
        finally:
            if self.devices is None or not self.intact():self.unlock()
    def intact(self):
        try:
            return (self.devices is not None and hyprctl(self.base,'submap',raw=True)=='rasklad_lock'
                    and self.keyboards()==self.devices)
        except (self.base.Refused,OSError,subprocess.SubprocessError,ValueError):
            return False
    def release_exemption(self):
        """First step of every revoke: the helper name must not stay exempt."""
        if not self.exempt:return True
        try:
            if hyprctl(self.base,'keyword',f'device[{self.PREFIX}]:keybinds','true',raw=True)=='ok':self.exempt=False
        except (self.base.Refused,OSError,subprocess.SubprocessError):
            pass
        return not self.exempt
    def unlock(self):
        """Exemption off, then submap off; state is kept until each step succeeds."""
        self.devices=None
        released=self.release_exemption()
        if self.locked:
            try:
                if hyprctl(self.base,'dispatch','submap','reset',raw=True)=='ok':self.locked=False
            except (self.base.Refused,OSError,subprocess.SubprocessError):
                pass
        return released and not self.locked


def compositor_binding(base):
    """Nested Hyprland only: the focused window as the compositor reports it.
    Returns (pid, window generation = window address), or None."""
    hypr=[p for p in Path(os.environ['XDG_RUNTIME_DIR'],'hypr').iterdir() if p.is_dir()]
    if len(hypr)!=1:raise base.Refused('hyprland_instance')
    env={'PATH':'/usr/bin','XDG_RUNTIME_DIR':os.environ['XDG_RUNTIME_DIR'],'HYPRLAND_INSTANCE_SIGNATURE':hypr[0].name}
    out=subprocess.run(['/usr/bin/hyprctl','-j','activewindow'],capture_output=True,timeout=3,env=env)
    if out.returncode!=0 or len(out.stdout)>65536:raise base.Refused('hyprctl_activewindow')
    try:window=json.loads(out.stdout)
    except ValueError:return None
    if type(window) is not dict:return None
    pid,address=window.get('pid'),window.get('address')
    if type(pid) is not int or pid<=1 or type(address) is not str or not re.fullmatch(r'0x[0-9a-f]{1,16}',address):
        return None
    return pid,int(address,16)


def descends_from(pid,ancestor):
    for _ in range(64):
        if pid==ancestor:return True
        try:pid=int(Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()[1])
        except (OSError,ValueError,IndexError):return False
        if pid<=1:return False
    return False


PURPOSES={b'n':'normal',b'a':'alpha',b'p':'password',b'x':'other'}
CONTEXT_EVENTS={b'i':'focus-change',b'u':'focus-change',b'r':'ic-reset',b'k':'purpose-change',b's':'ic-reset'}


class AddonRecords:
    """Fixed 6-byte records from the addon: commits (c/o + count + sequence) and
    context events (i/u/r/k + purpose + id). Never text."""
    def __init__(self,base,sock):
        self.base,self.sock,self.commits,self.contexts=base,sock,[],[]
    def poll(self,timeout=0):
        fresh=[];end=time.monotonic()+timeout
        while True:
            ready,_,_=select.select([self.sock],[],[],max(0,end-time.monotonic()))
            if not ready:return fresh
            record=self.sock.recv(16)
            if not record:raise self.base.Refused('addon_records_closed')  # addon hung up: stop
            if len(record)!=6 or record[-1:]!=b'\n':raise self.base.Refused('addon_record_shape')
            if record[:1] in (b'c',b'o'):self.commits.append(record)
            elif record[:1] in CONTEXT_EVENTS and record[1:2] in PURPOSES and record[2:5].isdigit():
                self.contexts.append(record);fresh.append(record)
            else:raise self.base.Refused('addon_record_kind')
            end=time.monotonic()  # drain what is queued, then return


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
    hyprland_seat_holder={}
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
        def revoke_all():
            # Order: exemption off → helper gone → addon arm closed → submap off.
            nonlocal write_end
            seat=hyprland_seat_holder.get('seat')
            if seat:seat.release_exemption()
            stop_helper()
            if write_end is not None:
                try:os.write(write_end,b'x')
                except OSError:pass
                os.close(write_end);write_end=None
            if seat:seat.unlock()
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
        hyprland=os.environ.get('RASKLAD_HYPRLAND_IME_LAB')=='1'
        if hyprland:
            focused=None
            for _ in range(50):
                focused=compositor_binding(base)
                if focused and descends_from(focused[0],pid):break
                time.sleep(.1)
            report['bindingFromCompositor']=bool(focused) and descends_from(focused[0],pid)
            if not report['bindingFromCompositor']:raise base.Refused('compositor_focus_not_fixture')
            surface=focused
        else:
            surface=(pid,0)  # Sway lab: no compositor query
        # IC epoch and purpose come from the addon's last context record.
        records=AddonRecords(base,receipts)
        records.poll(.5)
        last=records.contexts[-1] if records.contexts else b''
        report['contextFromAddon']=last[:1]==b'i' and PURPOSES.get(last[1:2]) in ('normal','alpha')
        if not report['contextFromAddon'] or records.commits:raise base.Refused('addon_context')
        epoch,purpose=int(last[2:5]),PURPOSES[last[1:2]]
        report['addonPurpose']=purpose
        binding=(surface[0],surface[1],epoch,purpose)
        def current(sv=None):
            sv=sv or s
            try:
                fresh=records.poll()
            except base.Refused:
                sv.event('error');revoke_all();raise
            if fresh:  # any focus/reset/capability change after arm revokes and stops the helper
                sv.event(CONTEXT_EVENTS[fresh[0][:1]])
                if sv is s:revoke_all()
                return (0,0,0,'none')
            if not hyprland:return binding
            try:
                now=compositor_binding(base)
                if now and not seat.intact():
                    sv.event('seat-lost')
                    if sv is s:revoke_all()
                    return (0,0,0,'none')
            except (base.Refused,OSError,subprocess.SubprocessError,ValueError):
                now=None
            if not now:
                sv.event('surface-change');return (0,0,0,'none')
            return (now[0],now[1],epoch,purpose)
        s=sup.Supervisor([binding[0]],'correction')
        if not s.arm(*binding):raise base.Refused('supervisor_arm')
        seat=None
        if hyprland:
            seat=HyprSeat(base);hyprland_seat_holder['seat']=seat
            report['seatLocked']=seat.lock()
            if not report['seatLocked']:s.revoke('seat-lost');raise base.Refused('seat_lock')
        if not s.seat_locked():raise base.Refused('supervisor_seat')
        def pending_receipt(timeout=0):
            end=time.monotonic()+timeout
            while not records.commits:
                if records.poll(max(0,end-time.monotonic())):
                    s.event('focus-change');return None
                if time.monotonic()>=end:return None
            return records.commits.pop(0)
        report['receiptQueueEmptyBeforeTyping']=pending_receipt() is None
        if not report['receiptQueueEmptyBeforeTyping']:s.revoke();raise base.Refused('stale_receipt')
        script=inj.script('correction')
        report['supervisedReceiptChecked']=False
        for command in script:
            if command=='backspace' and not s._receipt_ok:
                # Receipt from the addon (kind + length, no text), not from the observer.
                record=pending_receipt(2)
                time.sleep(.1)
                extra=pending_receipt()  # exactly one commit is expected
                report['supervisedReceiptChecked']=True
                report['supervisedReceiptMatched']=extra is None and s.commit_receipt(record)
                report['supervisedReceiptFromAddon']=record==inj.receipt('correction')
                # Observer cross-check only (lab): the field shows the expected text.
                report['observerSawExpectedText']=link.ask('observe').get('value')==inj.EXPECTED['correction']
                if not report['supervisedReceiptMatched']:
                    s.revoke()  # orphaned or unexpected commit: stop, never clean up blindly
                    break
            token=s.issue(command,*current())  # compositor focus re-read before every key
            if token is None or not s.send(token,command,*current(),write):raise base.Refused('supervisor_send')
            helper_ack(base,helper)
            time.sleep(.05)
        report['supervisedScriptCompleted']=s.done() and s.sent==len(script) and report['supervisedReceiptChecked']
        report['supervisedFieldCleaned']=link.ask('observe').get('value')==''
        report['noReceiptAfterCleanup']=pending_receipt(.2) is None
        report['helperStdinOnlyInSupervisor']=not write_end_holders(helper.stdin.fileno())
        # Purpose change: focus the password field; the addon must report it and
        # the supervisor must refuse any further key.
        check=sup.Supervisor([binding[0]],'correction')
        report['purposeCheckArmed']=check.arm(*binding) and check.seat_locked()
        seen=len(records.contexts)
        link.ask('focus-secret')
        time.sleep(.5)
        # Refusal must come from the live check alone (no hand-written revoke).
        report['passwordFocusRefused']=check.issue('word',*current(check)) is None and check.state=='REVOKED'
        records.poll(.2)
        report['passwordFocusReported']=any(r[:1]==b'i' and r[1:2]==b'p' for r in records.contexts[seen:])
        link.ask('focus-prose');records.poll(.3)
        report['armWriteEndOnlyInSupervisor']=not write_end_holders(write_end)
        if hyprland:
            # A new keyboard during the session (stand-in for any other input source):
            # the submap must swallow its keys and the supervisor must revoke.
            stray=subprocess.Popen(['/lab/rasklad-wayland-keys'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
            children.append(stray)
            try:
                ready,_,_=select.select([stray.stdout],[],[],3)
                if not ready or os.read(stray.stdout.fileno(),32)!=b'{"ready":true}\n':raise base.Refused('stray_ready')
                before_stray=link.ask('observe').get('events')
                stray.stdin.write(b'hello\n');stray.stdin.flush();helper_ack(base,stray);time.sleep(.4)
                report['strayBlockedDuringLock']=link.ask('observe').get('events')==before_stray
                report['newDeviceRevokes']=current()==(0,0,0,'none') and s.state=='REVOKED'
            finally:
                stray.stdin.close();stray.kill();stray.wait()
        # F12: revoke the supervisor and the addon arm pipe (byte + close).
        before=link.ask('observe')
        s.event('f12')
        revoke_all()
        if hyprland:
            report['seatReleasedAfterRevoke']=hyprctl(base,'submap',raw=True)!='rasklad_lock' and not seat.exempt
        report['supervisedRevokedRefusesNext']=s.issue('backspace',*binding) is None and s.state=='REVOKED'
        if hyprland:
            # A changed surface generation must be refused (fresh supervisor, no key sent).
            check=sup.Supervisor([binding[0]],'correction')
            check.arm(*binding);check.seat_locked()
            report['bindingChangeRefused']=check.issue('word',binding[0],binding[1]+1,0,'normal') is None and check.state=='REVOKED'
            now=compositor_binding(base)
            report['compositorFocusUnchanged']=bool(now) and (now[0],now[1])==binding[:2]
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
        if hyprland_seat_holder.get('seat'):
            hyprland_seat_holder['seat'].release_exemption()
            if not helper.stdin.closed:helper.stdin.close()
            if helper.poll() is None:helper.kill();helper.wait()
            hyprland_seat_holder['seat'].unlock()
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
    if gate and os.environ.get('RASKLAD_SEAT_PROBE_LAB')=='1' and os.environ.get('RASKLAD_HYPRLAND_IME_LAB')=='1':
        p,gate_report=run_seat_probe(base,fcitx,gate,driver_env,children)
    elif gate and os.environ.get('RASKLAD_SLACK_SUPERVISED_LAB')=='1':
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
        if os.environ.get('RASKLAD_SEAT_PROBE_LAB')=='1':supervised=False;control=True  # measurement: only require completion
        hypr_required=['bindingFromCompositor','bindingChangeRefused','seatLocked','strayBlockedDuringLock','newDeviceRevokes','seatReleasedAfterRevoke','compositorFocusUnchanged'] if os.environ.get('RASKLAD_HYPRLAND_IME_LAB')=='1' else []
        required=(['seatProbeComplete'] if os.environ.get('RASKLAD_SEAT_PROBE_LAB')=='1' else ['armWriteEndOnlyInSupervisor'] if control else
                  ['armWriteEndOnlyInSupervisor','supervisedScriptCompleted','supervisedReceiptChecked','supervisedReceiptMatched','supervisedReceiptFromAddon','observerSawExpectedText','receiptQueueEmptyBeforeTyping','noReceiptAfterCleanup','contextFromAddon','purposeCheckArmed','passwordFocusReported','passwordFocusRefused','supervisedFieldCleaned',
                   'supervisedRevokedRefusesNext','supervisedNothingAfterRevoke','helperStdinOnlyInSupervisor']+hypr_required if supervised else
                  ['armWriteEndOnlyInSupervisor','revokeServed'])
        if not r['fcitxAliveAfterGate'] or not all(r.get(k) is True for k in required):r['passed']=False
    if frontend:r.update(privateFrontendMapped=True,privateFrontendArtifactSha256=frontend[1],privateSeedKeyboard=seeded)
    r['frontendVariant']=private_frontend_variant(base)
    r['driverExitCode']=p.returncode;r['dbusInputFrontendAbsent']=dbus_input_absent;r['passed']=p.returncode==0 and r.get('passed') is True and dbus_input_absent;return r
