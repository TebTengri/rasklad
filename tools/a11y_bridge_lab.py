#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Owned Chromium/AT-SPI control. Plan by default; never connects to host buses."""
import argparse, importlib.util, json, os, signal, subprocess, sys, tempfile, shutil, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
INSIDE=Path(__file__)==Path('/lab/runner.py')
MODES=('disabled','enabled')

def private_context(env,inside=INSIDE):
    return (inside and env.get('HOME')=='/home/lab' and env.get('XDG_RUNTIME_DIR')=='/run/lab'
        and env.get('RASKLAD_A11Y_CONTROL')=='1' and env.get('DISPLAY')==':77'
        and env.get('RASKLAD_A11Y_MODE') in MODES
        and env.get('DBUS_SESSION_BUS_ADDRESS','').startswith('unix:path=/run/lab/')
        and ';' not in env.get('DBUS_SESSION_BUS_ADDRESS','')
        and not any(env.get(k) for k in ('WAYLAND_DISPLAY','WAYLAND_SOCKET','RASKLAD_PARENT_FD')))

def sandbox_args(chrome,paths,identity,mode):
    if mode not in MODES:raise ValueError('mode')
    args=chrome.argv(paths,identity);clean=[];i=0
    dropped={'NO_AT_BRIDGE','GTK_IM_MODULE','FCITX_ADDON_DIRS','RASKLAD_CHROMIUM_IME_LAB'}
    while i<len(args):
        if args[i]=='--setenv' and args[i+1] in dropped:i+=3;continue
        clean.append(args[i]);i+=1
    at=clean.index('--')
    return clean[:at]+['--setenv','NO_AT_BRIDGE','0','--setenv','GTK_IM_MODULE','gtk-im-context-simple',
        '--setenv','RASKLAD_A11Y_CONTROL','1','--setenv','RASKLAD_A11Y_MODE',mode]+clean[at:]

def run():
    if os.geteuid()==0:raise RuntimeError('root-not-admitted')
    spec=importlib.util.spec_from_file_location('chrome',ROOT/'tools/chromium_ime_lab.py')
    chrome=importlib.util.module_from_spec(spec);spec.loader.exec_module(chrome)
    paths={k:v for k,v in chrome.files().items() if k in ('base','fixture','xvfb','bus_conf')}
    paths['runner']=Path(__file__).resolve()
    for key,path in paths.items():chrome.base.regular_file(path,key=='xvfb')
    for path in ('/usr/bin/bwrap','/usr/lib/at-spi-bus-launcher','/usr/lib/chromium/chromium'):
        chrome.base.regular_file(Path(path),True)
    import hashlib
    lock=json.loads((ROOT/'data/xvfb-lab-lock.json').read_text())
    if lock.get('signatureVerified') is not True or hashlib.sha256(paths['xvfb'].read_bytes()).hexdigest()!=lock.get('binarySha256'):
        raise RuntimeError('xvfb-pin')
    trials=[]
    for mode in MODES:
        directory=Path(tempfile.mkdtemp(prefix='a11y-control-',dir=chrome.base.ensure_private_base(ROOT)))
        child=None
        try:
            identity={k:directory/k for k in ('passwd','group','machine-id')}
            identity['passwd'].write_text(f'lab:x:{os.getuid()}:{os.getgid()}:Owned lab:/home/lab:/usr/bin/nologin\n')
            identity['group'].write_text(f'lab:x:{os.getgid()}:\n')
            identity['machine-id'].write_text('00000000000000000000000000000006\n')
            with (directory/'report').open('w+b') as out:
                child=subprocess.Popen(sandbox_args(chrome,paths,identity,mode),stdin=subprocess.DEVNULL,
                    stdout=out,stderr=subprocess.DEVNULL,env={'PATH':'/usr/bin'},start_new_session=True,preexec_fn=chrome.limits)
                rc=child.wait(timeout=30)
                out.seek(0);raw=out.read(16385)
                if len(raw)>16384:raise RuntimeError('output-bound')
                report=json.loads(raw)
                if type(report) is not dict or report.get('mode')!=mode:raise RuntimeError('report-shape')
                report['exitCode']=rc;trials.append(report)
        finally:
            if child and child.poll() is None:
                os.killpg(child.pid,signal.SIGTERM)
                try:child.wait(timeout=1)
                except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=2)
            shutil.rmtree(directory)
    observed=[t.get('chromiumPeers',0)>0 for t in trials]
    outcome={ (False,False):'no-peers-in-either', (False,True):'enabled-only-peers',
        (True,True):'peers-in-both', (True,False):'disabled-only-peers'}[tuple(observed)]
    return {'schema':1,'trials':trials,'comparisonOutcome':outcome,'supportsStatusHypothesis':observed==[False,True],
        'passed':all(t.get('passed') is True and t['exitCode']==0 for t in trials),
        'sandboxed':True,'hostBusConnected':False,'hostConfigurationChanged':False,'slackAccessed':False,
        'textRead':False,'writesToApps':False,'interpretation':'passed means measurement completed, not bridge behavior; compare counters, not Slack compatibility'}

def inside():
    if not private_context(os.environ):raise RuntimeError('not-private-control')
    import gi
    gi.require_version('Atspi','2.0')
    from gi.repository import Gio,GLib,Atspi
    mode=os.environ['RASKLAD_A11Y_MODE'];children=[]
    report={'mode':mode,'passed':False,'isEnabled':None,'chromiumPeers':0,'inspectedNodes':0,'documentNodes':0,'textRead':False}
    session=Gio.bus_get_sync(Gio.BusType.SESSION,None)
    def call(connection,bus,path,interface,method,args=None):
        return connection.call_sync(bus,path,interface,method,args,None,Gio.DBusCallFlags.NO_AUTO_START,500,None).unpack()
    def owner_pid(name):
        return call(session,'org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','GetConnectionUnixProcessID',GLib.Variant('(s)',(name,)))[0]
    try:
        launcher=subprocess.Popen(['/usr/lib/at-spi-bus-launcher'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(launcher)
        end=time.monotonic()+3
        while True:
            try:
                if owner_pid('org.a11y.Bus')!=launcher.pid:raise RuntimeError('wrong-private-owner')
                break
            except Exception:
                if time.monotonic()>end:raise RuntimeError('private-service-unavailable')
                time.sleep(.04)
        # Sole mutation: our verified child service on our namespaced session bus.
        call(session,'org.a11y.Bus','/org/a11y/bus','org.freedesktop.DBus.Properties','Set',
            GLib.Variant('(ssv)',('org.a11y.Status','IsEnabled',GLib.Variant('b',mode=='enabled'))))
        value=call(session,'org.a11y.Bus','/org/a11y/bus','org.freedesktop.DBus.Properties','Get',GLib.Variant('(ss)',('org.a11y.Status','IsEnabled')))[0]
        if type(value) is not bool or value!=(mode=='enabled'):raise RuntimeError('private-status-mismatch')
        report['isEnabled']=value
        address=call(session,'org.a11y.Bus','/org/a11y/bus','org.a11y.Bus','GetAddress')[0]
        if (type(address) is not str or len(address)>1024 or ';' in address or
            not address.startswith(('unix:path=/tmp/','unix:abstract=/tmp/','unix:path=/run/lab/'))):raise RuntimeError('private-address')
        bus=Gio.DBusConnection.new_for_address_sync(address,Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT|Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,None,None)
        xvfb=subprocess.Popen(['/lab/Xvfb',':77','-screen','0','900x700x24','-nolisten','tcp','-noreset','-ac'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(xvfb)
        end=time.monotonic()+3
        while not Path('/tmp/.X11-unix/X77').exists():
            if xvfb.poll() is not None or time.monotonic()>end:raise RuntimeError('private-display')
            time.sleep(.04)
        browser=subprocess.Popen(['/usr/lib/chromium/chromium','--ozone-platform=x11','--force-renderer-accessibility',
            '--disable-gpu','--disable-dev-shm-usage','--disable-background-networking','--disable-component-update',
            '--disable-sync','--no-first-run','--no-default-browser-check','--password-store=basic',
            '--user-data-dir=/tmp/control-profile','file:///lab/fixture.html'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(browser)
        end=time.monotonic()+5;peers=set();seen=set()
        while time.monotonic()<end:
            if browser.poll() is not None:raise RuntimeError('owned-browser-exited')
            names=call(bus,'org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','ListNames')[0]
            for name in names[:128]:
                if not name.startswith(':'):continue
                try:
                    pid=call(bus,'org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','GetConnectionUnixProcessID',GLib.Variant('(s)',(name,)))[0]
                    if Path(f'/proc/{pid}/exe').resolve()==Path('/usr/lib/chromium/chromium'):peers.add(name)
                except Exception:continue
            if peers:break
            time.sleep(.1)
        queue=[(name,'/org/a11y/atspi/accessible/root') for name in peers]
        while queue and len(seen)<64:
            name,path=queue.pop(0)
            if (name,path) in seen:continue
            if name not in peers or not path.startswith('/org/a11y/atspi/accessible/'):continue
            seen.add((name,path))
            try:
                role=call(bus,name,path,'org.a11y.atspi.Accessible','GetRole')[0]
                if role==int(Atspi.Role.DOCUMENT_WEB):report['documentNodes']+=1
                items=call(bus,name,path,'org.a11y.atspi.Accessible','GetChildren')[0]
                queue.extend((b or name,p) for b,p in items[:32] if b in peers or not b)
                queue=queue[:64]
            except Exception:continue
        report.update(chromiumPeers=len(peers),inspectedNodes=len(seen),passed=True)
        bus.close_sync(None)
        return report
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try:child.wait(timeout=1)
                except subprocess.TimeoutExpired:child.kill();child.wait(timeout=1)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--run',action='store_true');parser.add_argument('--inside',action='store_true',help=argparse.SUPPRESS);args=parser.parse_args()
    try:
        report=inside() if args.inside else run() if args.run else {'schema':1,'mode':'plan','privateStatusModes':list(MODES),'hostBusConnected':False,'hostConfigurationChanged':False,'slackAccessed':False}
        print(json.dumps(report,sort_keys=True));return 0 if report.get('passed',True) else 1
    except Exception as exc:
        codes={'root-not-admitted','xvfb-pin','output-bound','report-shape','not-private-control','wrong-private-owner','private-service-unavailable','private-status-mismatch','private-address','private-display','owned-browser-exited'}
        code=str(exc) if type(exc) is RuntimeError and str(exc) in codes else type(exc).__name__
        print(json.dumps({'schema':1,'mode':os.environ.get('RASKLAD_A11Y_MODE','outer'),'passed':False,'error':code}));return 1
if __name__=='__main__':sys.exit(main())
