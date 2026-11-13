#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only, metadata-only native Slack capability probe. Default prints a plan.
No Text/Value/Name/Description/Attributes getters, writes, signals, input or settings.
"""
from collections import deque
from pathlib import Path
import argparse,json,re,subprocess,sys,time,threading,os

ACCESSIBLE='org.a11y.atspi.Accessible'
ROOT_PATH='/org/a11y/atspi/accessible/root'
NODE_METHODS=frozenset({'GetChildren','GetInterfaces','GetRole','GetState'})
BROKER_METHODS=frozenset({'NameHasOwner','ListNames','GetConnectionUnixProcessID'})
class ProbeError(Exception):pass
class BudgetEnd(Exception):pass

class Reader:
    """Only these fixed metadata methods can reach the supplied D-Bus transport."""
    def __init__(self,transport):self.transport=transport;self.owned=set()
    def broker(self,method,argument=None):
        if method not in BROKER_METHODS:raise ProbeError('forbidden-method')
        return self.transport('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus',method,argument)
    def address(self):return self.transport('org.a11y.Bus','/org/a11y/bus','org.a11y.Bus','GetAddress',None)
    def node(self,bus,path,method):
        if bus not in self.owned or method not in NODE_METHODS or type(path) is not str or len(path)>256 or not re.fullmatch(r'/org/a11y/atspi/accessible/[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*',path):raise ProbeError('forbidden-method')
        return self.transport(bus,path,ACCESSIBLE,method,None)


def slack_pids():
    result=subprocess.run(['/usr/bin/pgrep','-i','-x','slack'],capture_output=True,text=True,timeout=2)
    found=set()
    for row in result.stdout.splitlines():
        if not row.isdecimal():continue
        try:
            process=Path('/proc')/row
            if process.stat().st_uid==os.getuid() and (process/'exe').resolve()==Path('/usr/lib/slack/slack'):found.add(int(row))
        except OSError:pass
    return found


def run():
    import gi
    gi.require_version('Atspi','2.0')
    from gi.repository import Gio,GLib,Atspi
    cancellable=Gio.Cancellable()
    timer=threading.Timer(10,cancellable.cancel);timer.daemon=True;timer.start()
    try:return _probe(Gio,GLib,Atspi,cancellable)
    finally:timer.cancel()


def _probe(Gio,GLib,Atspi,cancellable):
    deadline=time.monotonic()+10
    def transport(connection):
        def call(bus,path,interface,method,argument):
            remaining=deadline-time.monotonic()
            if remaining<=0:raise BudgetEnd()
            parameters=None if argument is None else GLib.Variant('(s)',(argument,))
            value=connection.call_sync(bus,path,interface,method,parameters,None,
                Gio.DBusCallFlags.NO_AUTO_START,max(1,min(200,int(remaining*1000))),cancellable)
            return value.unpack()
        return call
    session=Gio.bus_get_sync(Gio.BusType.SESSION,cancellable);control=Reader(transport(session))
    report={'schema':1,'a11yBusAvailable':False,'slackA11yConnections':0,'inspectedNodes':0,
            'focusedEditableObjects':[],'targetProcesses':None,'pidLookups':0,'pidLookupErrors':0,'truncated':False,'textRead':False,'writes':False,'safeAutoCorrection':False}
    if control.broker('NameHasOwner','org.a11y.Bus')!=(True,):return report
    report['a11yBusAvailable']=True
    address=control.address()[0]
    if type(address) is not str or len(address)>1024 or not address.startswith('unix:'):raise ProbeError('unsupported-transport')
    connection=Gio.DBusConnection.new_for_address_sync(address,Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT|Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,None,cancellable)
    reader=Reader(transport(connection));pids=slack_pids();report['targetProcesses']=len(pids)
    names=reader.broker('ListNames')[0]
    for name in names[:256]:
        if not name.startswith(':'):continue
        try:
            pid=reader.broker('GetConnectionUnixProcessID',name)[0];report['pidLookups']+=1
            if pid in pids:reader.owned.add(name)
        except BudgetEnd:report['truncated']=True;break
        except Exception:report['pidLookupErrors']+=1;continue
    report['slackA11yConnections']=len(reader.owned)
    queue=deque((bus,ROOT_PATH,False) for bus in reader.owned);seen=set()
    def flag(bits,state):return bool(bits & (1<<int(state)))
    while queue and len(seen)<128:
        bus,path,active=queue.popleft()
        if (bus,path) in seen:continue
        seen.add((bus,path))
        try:
            state=reader.node(bus,path,'GetState')[0]
            if len(state)>2:raise ProbeError('state-size')
            bits=sum(int(word)<<(32*i) for i,word in enumerate(state));active=active or flag(bits,Atspi.StateType.ACTIVE)
            interfaces=reader.node(bus,path,'GetInterfaces')[0]
            if len(interfaces)>32:raise ProbeError('interface-size')
            editable='org.a11y.atspi.EditableText' in interfaces
            if editable and flag(bits,Atspi.StateType.FOCUSED):
                role=reader.node(bus,path,'GetRole')[0]
                try:role_name=Atspi.Role(role).value_nick
                except ValueError:role_name='unknown'
                report['focusedEditableObjects'].append({'role':role_name,'passwordRole':role==int(Atspi.Role.PASSWORD_TEXT),
                    'activeAncestor':active,'editableState':flag(bits,Atspi.StateType.EDITABLE),
                    'multiline':flag(bits,Atspi.StateType.MULTI_LINE),'editableTextInterface':True})
                if len(report['focusedEditableObjects'])>=3:break
            children=reader.node(bus,path,'GetChildren')[0]
            if len(children)>64:report['truncated']=True;children=children[:64]
            for child_bus,child_path in children:
                child_bus=child_bus or bus
                if child_bus in reader.owned and (child_bus,child_path) not in seen and len(queue)<128:queue.append((child_bus,child_path,active))
        except BudgetEnd:report['truncated']=True;break
        except Exception:continue
    report['inspectedNodes']=len(seen);report['truncated']=report['truncated'] or bool(queue)
    try:connection.close_sync(cancellable)
    except Exception:pass
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--run',action='store_true');args=parser.parse_args()
    if not args.run:
        print(json.dumps({'mode':'plan','target':'native slack only','methods':sorted(NODE_METHODS),
                          'maxNodes':128,'seconds':10,'textRead':False,'writes':False}));return 0
    try:print(json.dumps(run(),sort_keys=True));return 0
    except Exception:print(json.dumps({'error':'slack-metadata-probe-unavailable','textRead':False,'writes':False}),file=sys.stderr);return 1
if __name__=='__main__':sys.exit(main())
