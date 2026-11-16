#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Owned empty-profile Chromium/Slack AT-SPI control. Plan by default; never connects to host buses."""
import argparse, importlib.util, json, os, signal, subprocess, sys, tempfile, shutil, time, threading
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
INSIDE=Path(__file__)==Path('/lab/runner.py')
MODES=('disabled','enabled')
TARGETS={'chromium':'/usr/lib/chromium/chromium','slack':'/usr/lib/slack/slack'}

def private_context(env,inside=INSIDE):
    return (inside and env.get('HOME')=='/home/lab' and env.get('XDG_RUNTIME_DIR')=='/run/lab'
        and env.get('RASKLAD_A11Y_CONTROL')=='1' and env.get('DISPLAY')==':77'
        and env.get('RASKLAD_A11Y_MODE') in MODES and env.get('RASKLAD_A11Y_TARGET') in TARGETS
        and env.get('DBUS_SESSION_BUS_ADDRESS','').startswith('unix:path=/run/lab/')
        and ';' not in env.get('DBUS_SESSION_BUS_ADDRESS','')
        and not any(env.get(k) for k in ('WAYLAND_DISPLAY','WAYLAND_SOCKET','RASKLAD_PARENT_FD')))

def sandbox_args(chrome,paths,identity,mode,target='chromium',bridge=None,allow_ech=False):
    if allow_ech and bridge!='online':raise ValueError('ech-policy')
    if bridge not in (None,'check','online') or (bridge and target!='slack'):raise ValueError('bridge-target')
    if target not in TARGETS:raise ValueError('target')
    if mode not in MODES:raise ValueError('mode')
    args=chrome.argv(paths,identity);clean=[];i=0
    dropped={'NO_AT_BRIDGE','GTK_IM_MODULE','FCITX_ADDON_DIRS','RASKLAD_CHROMIUM_IME_LAB'}
    while i<len(args):
        if args[i]=='--setenv' and args[i+1] in dropped:i+=3;continue
        clean.append(args[i]);i+=1
    at=clean.index('--')
    network=(['--hostname','rasklad-a11y-lab','--ro-bind',str(paths['runner'].parent/'slack_https_bridge.py'),'/lab/slack_https_bridge.py',
        '--ro-bind',str(identity['passwd'].parent/'https.sock'),'/lab/https.sock',
        '--setenv','RASKLAD_A11Y_BRIDGE',bridge] if bridge else [])
    if allow_ech:network+=['--setenv','RASKLAD_ECH_OUTER_ALLOWED','1']
    return clean[:at]+network+['--setenv','NO_AT_BRIDGE','0','--setenv','GTK_IM_MODULE','gtk-im-context-simple',
        '--setenv','RASKLAD_A11Y_CONTROL','1','--setenv','RASKLAD_A11Y_MODE',mode,'--setenv','RASKLAD_A11Y_TARGET',target]+clean[at:]

def run(target='chromium',bridge=None,allow_ech=False):
    if allow_ech and bridge!='online':raise ValueError('ech-policy')
    if bridge not in (None,'check','online') or (bridge and target!='slack'):raise ValueError('bridge-target')
    if target not in TARGETS:raise ValueError('target')
    if os.geteuid()==0:raise RuntimeError('root-not-admitted')
    spec=importlib.util.spec_from_file_location('chrome',ROOT/'tools/chromium_ime_lab.py')
    chrome=importlib.util.module_from_spec(spec);spec.loader.exec_module(chrome)
    paths={k:v for k,v in chrome.files().items() if k in ('base','fixture','xvfb','bus_conf')}
    paths['runner']=Path(__file__).resolve()
    for key,path in paths.items():chrome.base.regular_file(path,key=='xvfb')
    for path in ('/usr/bin/bwrap','/usr/lib/at-spi-bus-launcher',TARGETS[target]):
        chrome.base.regular_file(Path(path),True)
    if bridge:chrome.base.regular_file(ROOT/'tools/slack_https_bridge.py')
    import hashlib
    lock=json.loads((ROOT/'data/xvfb-lab-lock.json').read_text())
    if lock.get('signatureVerified') is not True or hashlib.sha256(paths['xvfb'].read_bytes()).hexdigest()!=lock.get('binarySha256'):
        raise RuntimeError('xvfb-pin')
    trials=[]
    for mode in (('enabled',) if bridge=='check' else MODES):
        directory=Path(tempfile.mkdtemp(prefix='a11y-control-',dir=chrome.base.ensure_private_base(ROOT)))
        child=None;broker=None
        try:
            if bridge:
                module=load_bridge(ROOT/'tools/slack_https_bridge.py')
                broker=module.Broker(directory/'https.sock',online=bridge=='online',allow_ech=allow_ech)
            identity={k:directory/k for k in ('passwd','group','machine-id')}
            identity['passwd'].write_text(f'lab:x:{os.getuid()}:{os.getgid()}:Owned lab:/home/lab:/usr/bin/nologin\n')
            identity['group'].write_text(f'lab:x:{os.getgid()}:\n')
            identity['machine-id'].write_text('00000000000000000000000000000006\n')
            with (directory/'report').open('w+b') as out:
                child=subprocess.Popen(sandbox_args(chrome,paths,identity,mode,target,bridge,allow_ech),stdin=subprocess.DEVNULL,
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
            if broker:
                stats=broker.close()
                if trials and trials[-1].get('mode')==mode:trials[-1]['bridgeStats']=stats
            shutil.rmtree(directory)
    if bridge=='check':
        return {'schema':1,'target':target,'passed':all(t.get('bridgeCheck') and t['exitCode']==0 and t.get('bridgeStats',{}).get('cleanupComplete') and t['bridgeStats']['opened']==0 for t in trials),
            'trials':trials,'outboundNetworkEnabled':False,'hostNetworkShared':False}
    observed=[t.get('targetPeers',0)>0 for t in trials]
    outcome={ (False,False):'no-peers-in-either', (False,True):'enabled-only-peers',
        (True,True):'peers-in-both', (True,False):'disabled-only-peers'}[tuple(observed)]
    return {'schema':1,'target':target,'hostNetworkShared':False,'outboundNetworkEnabled':bridge=='online','echPolicy':'outer-sni-only' if allow_ech else 'deny','innerRoutingVerified':False,'trials':trials,'comparisonOutcome':outcome,'supportsStatusHypothesis':observed==[False,True],
        'passed':all(t.get('passed') is True and t['exitCode']==0 and (not bridge or t.get('bridgeStats',{}).get('cleanupComplete')) for t in trials),
        'sandboxed':True,'hostBusConnected':False,'hostConfigurationChanged':False,'workingSlackAccessed':False,'emptyProfileOnly':True,
        'textRead':False,'writesToApps':False,'interpretation':'passed means measurement completed, not bridge behavior; compare counters, not Slack compatibility'}

def load_bridge(path):
    spec=importlib.util.spec_from_file_location('slack_https_bridge',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

ERROR_MARKERS={
    'secretService':(b'libsecret',b'org.freedesktop.secrets'),
    'displayInit':(b'missing x server',b'platform failed to initialize'),
    'certificate':(b'cert_authority_invalid',b'certificate verify failed'),
    'busUnavailable':(b'failed to connect to the bus',b'serviceunknown'),
    'fatal':(b'fatal:',b'check failed:'),
}
def stderr_categories(chunks):
    flags={name:False for name in ERROR_MARKERS};tail=b''
    for chunk in chunks:
        frame=(tail+chunk).lower()
        for name,markers in ERROR_MARKERS.items():flags[name]=flags[name] or any(m in frame for m in markers)
        tail=frame[-128:]
    return flags

def process_kind(arguments,is_main=False):
    if is_main:return 'main'
    kind=next((part[7:] for part in arguments if part.startswith(b'--type=')),b'')
    if not kind:return 'unknown'
    if kind==b'renderer':return 'renderer'
    if kind==b'gpu-process':return 'gpu'
    if kind==b'utility' and any(part==b'--utility-sub-type=network.mojom.NetworkService' for part in arguments):return 'network'
    return 'other'

def process_counts(target,main_pid):
    if not private_context(os.environ):raise RuntimeError('not-private-control')
    counts={key:0 for key in ('main','renderer','gpu','network','other','unknown')}
    for process in list(Path('/proc').iterdir())[:1024]:
        if not process.name.isdecimal():continue
        try:
            if (process/'exe').resolve()!=Path(TARGETS[target]):continue
            with (process/'cmdline').open('rb') as stream:data=stream.read(16385)
            if len(data)<=16384:counts[process_kind(data.split(b'\0'),int(process.name)==main_pid)]+=1
        except OSError:continue
    return counts

def visible_windows():
    # Metadata only on our private X server. No names, pixels, focus or input.
    if not private_context(os.environ):raise RuntimeError('not-private-control')
    import ctypes as c
    D=c.c_void_p;W=c.c_ulong
    class Attr(c.Structure):
        _fields_=[('x',c.c_int),('y',c.c_int),('width',c.c_int),('height',c.c_int),
            ('border_width',c.c_int),('depth',c.c_int),('visual',D),('root',W),
            ('winclass',c.c_int),('bit_gravity',c.c_int),('win_gravity',c.c_int),
            ('backing_store',c.c_int),('backing_planes',W),('backing_pixel',W),
            ('save_under',c.c_int),('colormap',W),('map_installed',c.c_int),
            ('map_state',c.c_int),('all_event_masks',c.c_long),('your_event_mask',c.c_long),
            ('do_not_propagate_mask',c.c_long),('override_redirect',c.c_int),('screen',D)]
    x=c.CDLL('/usr/lib/libX11.so.6')
    x.XOpenDisplay.argtypes=[c.c_char_p];x.XOpenDisplay.restype=D
    x.XDefaultRootWindow.argtypes=[D];x.XDefaultRootWindow.restype=W
    x.XQueryTree.argtypes=[D,W,c.POINTER(W),c.POINTER(W),c.POINTER(c.POINTER(W)),c.POINTER(c.c_uint)]
    x.XGetWindowAttributes.argtypes=[D,W,c.POINTER(Attr)]
    x.XFree.argtypes=[D];x.XCloseDisplay.argtypes=[D]
    display=x.XOpenDisplay(b':77')
    if not display:return None
    children=c.POINTER(W)();count=c.c_uint();root=W();parent=W()
    try:
        if not x.XQueryTree(display,x.XDefaultRootWindow(display),c.byref(root),c.byref(parent),c.byref(children),c.byref(count)):return None
        visible=0
        for i in range(min(count.value,256)):
            attrs=Attr()
            if x.XGetWindowAttributes(display,children[i],c.byref(attrs)) and attrs.map_state==2 and attrs.winclass==1 and attrs.width>=100 and attrs.height>=100:visible+=1
        return visible
    finally:
        if children:x.XFree(children)
        x.XCloseDisplay(display)

def inside():
    if not private_context(os.environ):raise RuntimeError('not-private-control')
    import gi
    gi.require_version('Atspi','2.0')
    from gi.repository import Gio,GLib,Atspi
    mode=os.environ['RASKLAD_A11Y_MODE'];target=os.environ['RASKLAD_A11Y_TARGET'];children=[];relay=None
    report={'mode':mode,'target':os.environ['RASKLAD_A11Y_TARGET'],'passed':False,'isEnabled':None,'targetPeers':0,'inspectedNodes':0,'documentNodes':0,'textRead':False}
    bridge_mode=os.environ.get('RASKLAD_A11Y_BRIDGE')
    if bridge_mode:
        if target!='slack' or bridge_mode not in ('check','online'):raise RuntimeError('not-private-control')
        module=load_bridge(Path('/lab/slack_https_bridge.py'));relay=module.Relay('/lab/https.sock')
        if bridge_mode=='check':
            try:
                import socket
                with socket.create_connection(('127.0.0.1',relay.port),timeout=2) as client:
                    client.sendall(b'CONNECT rejected.invalid:443 HTTP/1.1\r\n\r\n')
                    blocked=client.recv(256).startswith(b'HTTP/1.1 403')
                return dict(report,passed=blocked,bridgeCheck=blocked)
            finally:relay.close()
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
        browser_env=dict(os.environ)
        proxy_args=[]
        if relay:
            proxy='http://127.0.0.1:'+str(relay.port)
            browser_env.update(HTTPS_PROXY=proxy,HTTP_PROXY=proxy,https_proxy=proxy,http_proxy=proxy,NO_PROXY='')
            proxy_args=['--proxy-server='+proxy]
            if os.environ.get('RASKLAD_ECH_OUTER_ALLOWED')!='1':proxy_args+=['--disable-features=EncryptedClientHello']
        diagnostics={}
        def consume_stderr(stream):
            diagnostics.update(stderr_categories(iter(lambda:stream.read(1024),b'')))
        browser=subprocess.Popen([TARGETS[target],'--ozone-platform=x11','--force-renderer-accessibility',
            '--disable-gpu','--disable-dev-shm-usage','--disable-background-networking','--disable-component-update',
            '--disable-sync','--no-first-run','--no-default-browser-check','--password-store=basic',
            '--user-data-dir=/tmp/control-profile']+(['file:///lab/fixture.html'] if target=='chromium' else [])+proxy_args,env=browser_env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE);children.append(browser)
        collector=threading.Thread(target=consume_stderr,args=(browser.stderr,),daemon=True);collector.start()
        end=time.monotonic()+5;peers=set();seen=set()
        while time.monotonic()<end:
            if browser.poll() is not None:raise RuntimeError('owned-browser-exited')
            names=call(bus,'org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','ListNames')[0]
            for name in names[:128]:
                if not name.startswith(':'):continue
                try:
                    pid=call(bus,'org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','GetConnectionUnixProcessID',GLib.Variant('(s)',(name,)))[0]
                    if Path(f'/proc/{pid}/exe').resolve()==Path(TARGETS[target]):peers.add(name)
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
        report.update(targetPeers=len(peers),inspectedNodes=len(seen),visibleWindows=visible_windows(),processKinds=process_counts(target,browser.pid),passed=True)
        bus.close_sync(None)
        return report
    finally:
        if relay:relay.close()
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try:child.wait(timeout=1)
                except subprocess.TimeoutExpired:child.kill();child.wait(timeout=1)
        if 'collector' in locals():
            collector.join(1)
            report['stderrCategories']=dict(diagnostics) if not collector.is_alive() else {'collectionIncomplete':True}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--target',choices=tuple(TARGETS),default='chromium');parser.add_argument('--allow-ech-outer',action='store_true');action=parser.add_mutually_exclusive_group();action.add_argument('--run',action='store_true');action.add_argument('--inside',action='store_true',help=argparse.SUPPRESS);action.add_argument('--plan-bridge',action='store_true');action.add_argument('--check-bridge',action='store_true');action.add_argument('--run-via-bridge',action='store_true');args=parser.parse_args()
    try:
        bridge='check' if args.check_bridge else 'online' if (args.plan_bridge or args.run_via_bridge) else None
        if bridge and args.target!='slack':raise ValueError('bridge-target')
        if args.allow_ech_outer and bridge!='online':raise ValueError('ech-policy')
        report=inside() if args.inside else run(args.target,bridge,args.allow_ech_outer) if (args.run or args.check_bridge or args.run_via_bridge) else {'schema':1,'mode':'plan','target':args.target,'emptyProfileOnly':True,'hostNetworkShared':False,'outboundNetworkEnabled':bridge=='online','bridgeMode':bridge,'connectAndSniAllowlisted':bool(bridge),'echPolicy':'outer-sni-only' if args.allow_ech_outer else 'deny','innerRoutingVerified':False,'privateStatusModes':list(MODES),'hostBusConnected':False,'hostConfigurationChanged':False,'slackAccessed':False}
        print(json.dumps(report,sort_keys=True));return 0 if report.get('passed',True) else 1
    except Exception as exc:
        codes={'root-not-admitted','xvfb-pin','output-bound','report-shape','not-private-control','wrong-private-owner','private-service-unavailable','private-status-mismatch','private-address','private-display','owned-browser-exited'}
        code=str(exc) if type(exc) is RuntimeError and str(exc) in codes else type(exc).__name__
        print(json.dumps({'schema':1,'mode':os.environ.get('RASKLAD_A11Y_MODE','outer'),'passed':False,'error':code}));return 1
if __name__=='__main__':sys.exit(main())
