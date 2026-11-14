#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Optional real Chromium/GTK/Fcitx test. Default plan; no host display/profile."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile
import shutil
import time

ROOT=Path(__file__).resolve().parent.parent
INSIDE=Path(__file__)==Path('/lab/runner.py')
base_path=Path('/lab/fcitx_lab_runner.py') if INSIDE else ROOT/'tools/fcitx_lab_runner.py'
spec=importlib.util.spec_from_file_location('fcitx_base',base_path)
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
TIMEOUT=60
PROFILE_PATHS=[
 'data/morphology-003-sources.json','data/sources.json','data/experiment-002-protocol.json',
 'data/notices/MORPHOLOGY-003-REVIEW.md','data/notices/Hunspell-COPYING.MPL.txt',
 'data/notices/Hunspell-license.myspell.txt','data/notices/en_US.README.txt','data/notices/ru_RU.README.txt',
 'build/experiment-data/en.dic','build/experiment-data/ru.dic',
 'build/experiment-003-data/en.aff','build/experiment-003-data/ru.aff',
 'build/experiment-003-data/control/en.aff','build/experiment-003-data/control/ru.aff',
 'build/experiment-002-artifacts/detector-model.rskn','build/experiment-002-artifacts/exact-full-lexicons.rskp',
 'build/experiment-002-artifacts/artifact-metadata.json','build/experiment-002-artifacts/MODIFICATIONS.txt',
 'build/experiment-002-artifacts/en_US.NOTICE.txt','build/experiment-002-artifacts/ru_RU.NOTICE.txt']

def files():
 return {'runner':ROOT/'tools/chromium_ime_lab.py','base':ROOT/'tools/fcitx_lab_runner.py',
  'driver':ROOT/'tests/chromium_ime/driver.mjs','keys':ROOT/'tests/chromium_ime/keys.py',
  'fixture':ROOT/'tests/chromium_ime/fixture.html','xvfb':ROOT/'build/chromium-ime/Xvfb',
  'addon':ROOT/'build/chromium-ime/addon/raskladlab.so',
  'addon_conf':ROOT/'tests/fcitx_transport/raskladlab.conf','bus_conf':ROOT/'tests/fcitx_transport/session-bus.conf'}

TARGETS={'runner':'/lab/runner.py','base':'/lab/fcitx_lab_runner.py','driver':'/lab/driver.mjs',
 'keys':'/lab/keys.py','fixture':'/lab/fixture.html','xvfb':'/lab/Xvfb','addon':'/lab/addons/raskladlab.so',
 'addon_conf':'/lab/addon.conf','bus_conf':'/lab/session-bus.conf'}

def argv(paths,identity,profile_files=None,reject_profile=False):
 a=['/usr/bin/bwrap','--unshare-all','--die-with-parent','--new-session','--cap-drop','ALL','--clearenv',
 '--ro-bind','/usr','/usr','--symlink','usr/bin','/bin','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
 '--ro-bind-try','/etc/ld.so.cache','/etc/ld.so.cache','--ro-bind','/etc/fonts','/etc/fonts',
 '--proc','/proc','--dev','/dev','--tmpfs','/tmp','--tmpfs','/run','--tmpfs','/home',
 '--dir','/run/lab','--dir','/home/lab','--dir','/lab/addons','--chdir','/home/lab']
 for k,p in paths.items():a+=['--ro-bind',str(p),TARGETS[k]]
 for k,p in identity.items():a+=['--ro-bind',str(p),'/etc/'+k]
 if profile_files:
  a+=['--dir','/lab/profile']
  for relative,source in profile_files.items():a+=['--ro-bind',str(source),'/lab/profile/'+relative]
 env={'PATH':'/usr/bin','LANG':'C.UTF-8','HOME':'/home/lab','XDG_RUNTIME_DIR':'/run/lab',
 'XDG_CONFIG_HOME':'/home/lab/.config','XDG_DATA_HOME':'/home/lab/.local/share','XDG_CACHE_HOME':'/home/lab/.cache',
 'XDG_DATA_DIRS':'/usr/share','FCITX_ADDON_DIRS':'/lab/addons:/usr/lib/fcitx5','GTK_IM_MODULE':'fcitx',
 'GDK_BACKEND':'x11','DISPLAY':':77','NO_AT_BRIDGE':'1','RASKLAD_CHROMIUM_IME_LAB':'1'}
 if profile_files:env['RASKLAD_REVIEWED_PROFILE_LAB']='1'
 if reject_profile:env['RASKLAD_PROFILE_REJECT_TEST']='1'
 for k,v in env.items():a+=['--setenv',k,v]
 return a+['--','/usr/bin/dbus-run-session','--config-file','/lab/session-bus.conf','--','/usr/bin/python3','-I','/lab/runner.py','--inside']

def limits():
 os.nice(10)
 # Chromium reserves a large virtual address range; RLIMIT_AS would reject its
 # normal sandboxed startup. Wall/CPU/file/core limits still apply.
 for k,n in [(resource.RLIMIT_CPU,45),(resource.RLIMIT_FSIZE,128<<20),(resource.RLIMIT_CORE,0),(resource.RLIMIT_NOFILE,512)]:resource.setrlimit(k,(n,n))

def run(profile=False,input_root=None,reject_profile=False):
 if os.geteuid()==0:raise base.Refused('root_not_admitted')
 paths=files()
 profile_files=None
 if profile:
  if input_root is None:raise base.Refused('explicit_input_root_required')
  input_root=Path(input_root).absolute()
  profile_files={name:(ROOT if name.startswith('data/') else input_root)/name for name in PROFILE_PATHS}
  for path in profile_files.values():base.regular_file(path)
  paths['addon']=ROOT/'build/chromium-ime/profile-addon/raskladlab.so'
 for k,p in paths.items():base.regular_file(p,k in ['xvfb'])
 base.regular_file(Path('/usr/bin/bwrap'),True)
 meta=os.stat('/usr/bin/bwrap')
 if meta.st_uid!=0 or meta.st_mode&0o6022:raise base.Refused('unsafe_bwrap')
 lock_path=ROOT/'data/xvfb-lab-lock.json';base.regular_file(lock_path)
 lock=json.loads(lock_path.read_text())
 if lock.get('signatureVerified') is not True or hashlib.sha256(paths['xvfb'].read_bytes()).hexdigest()!=lock.get('binarySha256'):raise base.Refused('xvfb_pin')
 directory=Path(tempfile.mkdtemp(prefix='chromium-',dir=base.ensure_private_base(ROOT)))
 child=None
 try:
  identity={k:directory/k for k in ['passwd','group','machine-id']}
  identity['passwd'].write_text(f'lab:x:{os.getuid()}:{os.getgid()}:Private lab:/home/lab:/usr/bin/nologin\n')
  identity['group'].write_text(f'lab:x:{os.getgid()}:\n');identity['machine-id'].write_text('00000000000000000000000000000002\n')
  if reject_profile:
   bad=directory/'rejected-manifest.json';bad.write_text('{}')
   profile_files['data/sources.json']=bad
  with (directory/'stdout').open('w+b') as out:
   child=subprocess.Popen(argv(paths,identity,profile_files,reject_profile),stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.DEVNULL,
    env={'PATH':'/usr/bin'},start_new_session=True,preexec_fn=limits)
   try:rc=child.wait(timeout=TIMEOUT)
   except subprocess.TimeoutExpired:raise base.Refused('timeout')
   out.seek(0);raw=out.read(32769)
   if len(raw)>32768:raise base.Refused('output_limit')
   try:r=json.loads(raw)
   except ValueError:return {'schema':1,'passed':False,'phase':'sandbox_start','exitCode':rc}
   if not isinstance(r,dict) or r.get('schema')!=1:raise base.Refused('report_shape')
   r.update({'sandboxed':True,'hostConfigurationChanged':False,'hostInput':False,'exitCode':rc})
   r['passed']=rc==0 and r.get('passed') is True
   return r
 finally:
  if child and child.poll() is None:
   os.killpg(child.pid,signal.SIGTERM)
   try:child.wait(timeout=1)
   except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=2)
  shutil.rmtree(directory)

def inside():
 if not INSIDE or os.environ.get('RASKLAD_CHROMIUM_IME_LAB')!='1' or os.environ.get('DISPLAY')!=':77' or os.environ.get('HOME')!='/home/lab' or os.environ.get('WAYLAND_DISPLAY'):raise base.Refused('not_private_lab')
 if not os.environ.get('DBUS_SESSION_BUS_ADDRESS','').startswith('unix:'):raise base.Refused('private_bus')
 os.chmod('/run/lab',0o700)
 config=Path('/home/lab/.config/fcitx5');config.mkdir(parents=True);(config/'profile').write_text(base.PROFILE)
 data=Path('/home/lab/.local/share/fcitx5/addon');data.mkdir(parents=True);(data/'raskladlab.conf').write_bytes(Path('/lab/addon.conf').read_bytes())
 children=[];phase='xvfb_start'
 try:
  xvfb=subprocess.Popen(['/lab/Xvfb',':77','-screen','0','1000x800x24','-nolisten','tcp','-noreset','-ac'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(xvfb)
  for _ in range(80):
   if xvfb.poll() is not None:raise base.Refused('xvfb_exited')
   if Path('/tmp/.X11-unix/X77').exists():break
   time.sleep(.025)
  else:raise base.Refused('xvfb_timeout')
  phase='fcitx_start'
  fcitx=subprocess.Popen(['/usr/bin/fcitx5','-D','-k','--disable','all','--enable',base.ALLOW_ADDONS,'--verbose','*=0'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(fcitx)
  ready_deadline=time.monotonic()+3
  while time.monotonic()<ready_deadline:
   if fcitx.poll() is not None:raise base.Refused('fcitx_exited')
   p=subprocess.run(['/usr/bin/gdbus','call','--session','--dest','org.freedesktop.DBus','--object-path','/org/freedesktop/DBus','--method','org.freedesktop.DBus.NameHasOwner','org.fcitx.Fcitx5'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=.3)
   if p.stdout==b'(true,)\n':break
   time.sleep(.025)
  else:raise base.Refused('fcitx_timeout')
  phase='chromium_driver'
  p=subprocess.run(['/usr/bin/node','/lab/driver.mjs'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=48,env=dict(os.environ,RASKLAD_OWN_FCITX_PID=str(fcitx.pid),RASKLAD_CLK_TCK=str(os.sysconf('SC_CLK_TCK'))))
  if len(p.stdout)>32768:raise base.Refused('driver_output_limit')
  r=json.loads(p.stdout);r['driverExitCode']=p.returncode;r['passed']=p.returncode==0 and r.get('passed') is True
  if fcitx.poll() is None:r.update(base.daemon_resources(fcitx.pid))
  r['resourceScope']='own private Fcitx before optional crash/after scenarios; includes startup/profile load/queries; not idle baseline or full browser RSS'
  return r
 except (base.Refused,ValueError,subprocess.SubprocessError) as e:
  return {'schema':1,'passed':False,'phase':phase,'reason':str(e) if isinstance(e,base.Refused) else type(e).__name__}
 finally:
  for p in reversed(children):
   if p.poll() is None:
    p.terminate()
    try:p.wait(timeout=1)
    except subprocess.TimeoutExpired:p.kill();p.wait(timeout=1)

def main():
 p=argparse.ArgumentParser(description=__doc__);a=p.add_mutually_exclusive_group();a.add_argument('--run',action='store_true');a.add_argument('--run-profile',action='store_true');a.add_argument('--test-profile-rejection',action='store_true');p.add_argument('--input-root');a.add_argument('--inside',action='store_true',help=argparse.SUPPRESS);args=p.parse_args()
 try:
  if args.inside:r=inside()
  elif args.test_profile_rejection:r=run(True,args.input_root,True)
  elif args.run_profile:r=run(True,args.input_root)
  elif args.run:r=run()
  else:r={'schema':1,'status':'plan_only','tested':False,'hostInput':False,'hostConfigurationChanged':False,'slackSupport':False,'transport':'private Xvfb XTest -> real Chromium GTK IM -> private Fcitx; CDP observation only','xvfbInstallation':'none; signed package binary in ignored build'}
  print(json.dumps(r,sort_keys=True));return 0 if r.get('passed',True) else 1
 except (base.Refused,OSError,ValueError,subprocess.SubprocessError) as e:
  print(json.dumps({'schema':1,'passed':False,'reason':str(e) if isinstance(e,base.Refused) else type(e).__name__}));return 2
if __name__=='__main__':sys.exit(main())
