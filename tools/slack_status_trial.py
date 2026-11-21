#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One temporary live IsEnabled trial. Plan only unless explicitly approved."""
import argparse,importlib.util,json,os,signal,threading,time
from pathlib import Path

class Refused(Exception):pass

def trial(control,probe,ready=None):
    # signal.signal() works only in the main thread; refuse before any bus access so restore can't fail after mutation.
    if threading.current_thread() is not threading.main_thread():raise Refused('not-main-thread')
    if ready is not None and not ready():raise Refused('slack-not-running')
    # Ignore handled termination before any mutation, including finally entry.
    # The whole trial is bounded; SIGKILL/crashes remain outside this guarantee.
    signals=(signal.SIGINT,signal.SIGTERM)
    old_mask=signal.pthread_sigmask(signal.SIG_BLOCK,signals)
    saved={sig:signal.signal(sig,signal.SIG_IGN) for sig in signals}
    try:
        signal.pthread_sigmask(signal.SIG_SETMASK,old_mask)
        return _trial(control,probe,ready)
    finally:
        signal.pthread_sigmask(signal.SIG_BLOCK,signals)
        # Discard pending handled requests while still ignored, after rollback.
        signal.pthread_sigmask(signal.SIG_SETMASK,old_mask)
        for sig,handler in saved.items():signal.signal(sig,handler)

def _trial(control,probe,ready=None):
    report={'schema':1,'textRead':False,'writesToApps':False,'restart':False,'statusChanged':False,'restored':False}
    owner=control.owner()
    if not owner:raise Refused('service-not-running')
    if control.uid(owner)!=os.getuid():raise Refused('wrong-owner-uid')
    baseline=control.get(owner)
    if type(baseline) is not bool:raise Refused('status-type')
    report['initialIsEnabled']=baseline
    if baseline:raise Refused('status-already-enabled')
    attempted=False
    try:
        if control.owner()!=owner:raise Refused('owner-changed')
        if ready is not None and not ready():raise Refused('slack-not-running')
        attempted=True;report['enableAttempted']=True;report['enableOutcome']='unknown'
        control.set(owner,True)
        if control.get(owner) is not True:raise Refused('enable-not-confirmed')
        report['statusChanged']=True;report['enableOutcome']='confirmed'
        report['probe']=probe()
    except BaseException as exc:
        report['error']=str(exc) if isinstance(exc,Refused) else type(exc).__name__
    finally:
        # Restore even after an uncertain Set reply. Never mutate a replacement owner.
        if attempted:
            for attempt in range(3):
                report['restoreAttempts']=attempt+1
                try:
                    if control.owner()!=owner:raise Refused('owner-changed-before-restore')
                    control.set(owner,baseline)
                    if control.get(owner) is not baseline:raise Refused('restore-readback-mismatch')
                    report['restored']=True;report.pop('restoreError',None);break
                except BaseException as exc:
                    report['restoreError']=str(exc) if isinstance(exc,Refused) else type(exc).__name__
                    if report['restoreError']=='owner-changed-before-restore':break
                    if attempt<2:time.sleep(.05)
    result=report.get('probe')
    report['measurementCompleted']=(isinstance(result,dict) and result.get('schema')==1
        and result.get('textRead') is False and result.get('writes') is False
        and result.get('truncated') is False and not result.get('error') and result.get('passed') is not False)
    report['targetReady']=(isinstance(result,dict) and type(result.get('targetProcesses')) is int and result['targetProcesses']>0)
    report['passedMeaning']='status procedure and metadata measurement completed, not editor support'
    report['passed']=report['statusChanged'] and report['restored'] and report['measurementCompleted'] and 'error' not in report
    return report

class Control:
    def __init__(self):
        import gi
        from gi.repository import Gio,GLib
        self.Gio=Gio;self.GLib=GLib
        self.bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
    def call(self,bus,path,interface,method,args=None):
        return self.bus.call_sync(bus,path,interface,method,args,None,self.Gio.DBusCallFlags.NO_AUTO_START,750,None).unpack()
    def owner(self):
        return self.call('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','GetNameOwner',self.GLib.Variant('(s)',('org.a11y.Bus',)))[0]
    def uid(self,owner):
        return self.call('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','GetConnectionUnixUser',self.GLib.Variant('(s)',(owner,)))[0]
    def get(self,owner):
        return self.call(owner,'/org/a11y/bus','org.freedesktop.DBus.Properties','Get',self.GLib.Variant('(ss)',('org.a11y.Status','IsEnabled')))[0]
    def set(self,owner,value):
        self.call(owner,'/org/a11y/bus','org.freedesktop.DBus.Properties','Set',self.GLib.Variant('(ssv)',('org.a11y.Status','IsEnabled',self.GLib.Variant('b',value))))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-approved',action='store_true');args=parser.parse_args()
    if not args.run_approved:
        print(json.dumps({'mode':'plan','property':'org.a11y.Status.IsEnabled','baselineRequired':False,'temporaryValue':True,
            'probeSeconds':10,'textRead':False,'writesToApps':False,'restart':False,
            'scope':'session-wide accessibility availability; not channel-isolated',
            'restore':'finally on original bus owner; SIGKILL/crash/service replacement can prevent restoration'}));return 0
    def interrupted(signum,frame):raise Refused('interrupted')
    previous={s:signal.signal(s,interrupted) for s in (signal.SIGINT,signal.SIGTERM)}
    try:
        spec=importlib.util.spec_from_file_location('metadata',Path(__file__).with_name('slack_accessibility_probe.py'))
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        if not module.slack_pids():raise Refused('slack-not-running')
        report=trial(Control(),lambda:module.run(observe=True),lambda:bool(module.slack_pids()))
        print(json.dumps(report,sort_keys=True));return 0 if report['passed'] else 1
    except Exception as exc:
        print(json.dumps({'passed':False,'error':str(exc) if isinstance(exc,Refused) else type(exc).__name__}));return 1
    finally:
        for sig,handler in previous.items():signal.signal(sig,handler)
if __name__=='__main__':raise SystemExit(main())
