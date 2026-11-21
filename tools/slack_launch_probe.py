#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Plan-first, explicitly approved per-process Slack accessibility launch."""
import argparse,importlib.util,json,os,subprocess,time
from pathlib import Path
EXECUTABLE='/usr/lib/slack/slack'
def load(name,filename):
 spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def launch_environment(source):
 env=dict(source)
 env['ACCESSIBILITY_ENABLED']='1';env['NO_AT_BRIDGE']='0'
 return env

def run(probe,validate,launch=subprocess.Popen,source=None,now=time.monotonic,pause=time.sleep):
 if os.geteuid()==0:raise RuntimeError('root-not-admitted')
 if probe.slack_pids():raise RuntimeError('slack-already-running')
 validate(Path(EXECUTABLE),True)
 child=launch([EXECUTABLE,'--force-renderer-accessibility'],env=launch_environment(os.environ if source is None else source),
  stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
 # Do not terminate Slack on timeout/failure: it uses the owner's normal profile.
 deadline=now()+3
 while not probe.slack_pids():
  if child.poll() is not None:raise RuntimeError('slack-launch-exited')
  if now()>=deadline:raise RuntimeError('slack-process-not-ready')
  pause(min(.1,max(0,deadline-now())))
 report=probe.run(observe=True)
 return {'schema':1,'launched':True,'probe':report,'systemStatusWritten':False,
  'toolEditsConfigurationFiles':False,'normalSlackProfileUsed':True,'profileCopied':False,
  'textRead':False,'writesToApps':False,'messagesSent':False,'toolTerminatedSlack':False,'targetPresentAtProbe':type(report.get('targetProcesses')) is int and report['targetProcesses']>0,'editorSupportVerified':False,
  'accessibilityEnvironment':'process-only until Slack exits',
  'measurementCompleted':isinstance(report,dict) and report.get('schema')==1 and not report.get('error') and report.get('truncated') is False and report.get('textRead') is False and report.get('writes') is False}

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--run-approved',action='store_true');args=parser.parse_args()
 if not args.run_approved:
  print(json.dumps({'mode':'plan','requiresSlackFullyClosed':True,'executable':EXECUTABLE,
   'args':['--force-renderer-accessibility'],'processEnvironment':{'ACCESSIBILITY_ENABLED':'1','NO_AT_BRIDGE':'0'},
   'systemStatusWritten':False,'normalSlackProfileUsed':True,'probeSeconds':10,'textRead':False,
   'messagesSent':False,'toolWillTerminateSlack':False}));return 0
 try:
  probe=load('slack_metadata','slack_accessibility_probe.py');base=load('lab_guard','fcitx_lab_runner.py')
  report=run(probe,base.regular_file);print(json.dumps(report,sort_keys=True));return 0 if report['measurementCompleted'] else 1
 except Exception as exc:
  allowed={'root-not-admitted','slack-already-running','slack-launch-exited','slack-process-not-ready'}
  print(json.dumps({'schema':1,'error':str(exc) if type(exc) is RuntimeError and str(exc) in allowed else type(exc).__name__,'textRead':False,'messagesSent':False}));return 1
if __name__=='__main__':raise SystemExit(main())
