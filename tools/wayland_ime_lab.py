#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Native Wayland reference test, never the user's compositor. Default plan only."""
import argparse,hashlib,importlib.util,json,os,signal,subprocess,sys,tempfile,shutil,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
INSIDE=Path(__file__)==Path('/lab/runner.py')
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
base=load('fcitx_base',Path('/lab/fcitx_lab_runner.py') if INSIDE else ROOT/'tools/fcitx_lab_runner.py')
chrome=None if INSIDE else load('chrome_base',ROOT/'tools/chromium_ime_lab.py')
RUNTIME={'sway','libwlroots-0.20.so','libliftoff.so.0'}
SWAY_CONFIG='''xwayland disable
seat seat0 fallback true
output HEADLESS-1 mode 1000x800
input * xkb_layout us
focus_follows_mouse no
default_border none
'''

def sandbox_args(paths,identity,profile,runtime,config,nested_smoke=False,snapshot_fds=None,focus_stress=False):
 a=chrome.argv(paths,identity,profile)
 # Drop X11/GTK routing; this test must use native text-input/input-method.
 clean=[];i=0
 while i<len(a):
  if a[i]=='--setenv' and a[i+1] in ['DISPLAY','GDK_BACKEND','GTK_IM_MODULE']:i+=3;continue
  clean.append(a[i]);i+=1
 at=clean.index('--')
 extra=['--ro-bind',str(ROOT/'tools/native_ime_clients.py'),'/lab/native_ime_clients.py','--dir','/lab/wayland-runtime','--ro-bind',str(config),'/lab/sway.conf',
 '--ro-bind',str(ROOT/'build/wayland-ime/keys/rasklad-wayland-keys'),'/lab/rasklad-wayland-keys']
 for name in sorted(RUNTIME):extra+=['--ro-bind',str(runtime/name),'/lab/wayland-runtime/'+name]
 for k,v in {'LD_LIBRARY_PATH':'/lab/wayland-runtime','RASKLAD_NATIVE_WAYLAND_LAB':'1',
  'WLR_BACKENDS':'headless','WLR_RENDERER':'pixman','WLR_LIBINPUT_NO_DEVICES':'1',
  'XDG_CURRENT_DESKTOP':'sway','XDG_SESSION_DESKTOP':'sway','XDG_SESSION_TYPE':'wayland'}.items():extra+=['--setenv',k,v]
 if nested_smoke:extra+=['--setenv','RASKLAD_NESTED_HYPR_SMOKE','1']
 if focus_stress:
  extra+=['--setenv','RASKLAD_FOCUS_STRESS_LAB','1']
  for source,target in [('tests/chromium_ime/adversarial-focus.mjs','adversarial-focus.mjs'),('tests/chromium_ime/focus-policy.mjs','focus-policy.mjs'),('data/focus-source-review-001.json','focus-source-review.json')]:extra+=['--ro-bind',str(ROOT/source),'/lab/'+target]
 result=clean[:at]+extra+clean[at:]
 if snapshot_fds:
  sealed=[];i=0
  while i<len(result):
   if result[i]=='--ro-bind' and i+2<len(result) and result[i+2] in snapshot_fds:
    sealed+=['--perms','0755','--ro-bind-data',str(snapshot_fds[result[i+2]]),result[i+2]];i+=3
   else:sealed.append(result[i]);i+=1
  result=sealed
 return result

def run(input_root,nested_smoke=False,focus_stress=False):
 if os.geteuid()==0:raise base.Refused('root_not_admitted')
 if input_root is None:raise base.Refused('explicit_input_root_required')
 paths=chrome.files();paths.pop('xvfb');paths.pop('keys');paths['runner']=ROOT/'tools/wayland_ime_lab.py';paths['addon']=ROOT/'build/chromium-ime/profile-addon/raskladlab.so'
 for path in paths.values():base.regular_file(path)
 if focus_stress:
  for source in ['tests/chromium_ime/adversarial-focus.mjs','tests/chromium_ime/focus-policy.mjs','data/focus-source-review-001.json']:base.regular_file(ROOT/source)
 base.regular_file(ROOT/'build/wayland-ime/keys/rasklad-wayland-keys',True)
 base.regular_file(Path('/usr/bin/bwrap'),True)
 meta=os.stat('/usr/bin/bwrap')
 if meta.st_uid!=0 or meta.st_mode&0o6022:raise base.Refused('unsafe_bwrap')
 lock=json.loads((ROOT/'data/wayland-lab-runtime-lock.json').read_text());runtime=ROOT/'build/wayland-ime/runtime'
 if {p.get('name') for p in lock['packages']}!={'sway','wlroots0.20','libliftoff'} or any(p.get('signatureVerified') is not True for p in lock['packages']):raise base.Refused('runtime_signatures')
 for name in RUNTIME:
  path=runtime/name;base.regular_file(path,name=='sway')
  if path.stat().st_size>5_000_000:raise base.Refused('runtime_size')

 profile={name:(ROOT if name.startswith('data/') else Path(input_root).absolute())/name for name in chrome.PROFILE_PATHS}
 for p in profile.values():base.regular_file(p)
 d=Path(tempfile.mkdtemp(prefix='native-wayland-',dir=base.ensure_private_base(ROOT)));child=None;snapshots={}
 try:
  for name in RUNTIME:snapshots['/lab/wayland-runtime/'+name]=base.sealed_snapshot(runtime/name,lock['runtimeFiles'][name])
  snapshots['/lab/rasklad-wayland-keys']=base.sealed_snapshot(ROOT/'build/wayland-ime/keys/rasklad-wayland-keys')
  snapshots['/lab/addons/raskladlab.so']=base.sealed_snapshot(paths['addon'])
  identity={k:d/k for k in ['passwd','group','machine-id']}
  identity['passwd'].write_text(f'lab:x:{os.getuid()}:{os.getgid()}:Private lab:/home/lab:/usr/bin/nologin\n');identity['group'].write_text(f'lab:x:{os.getgid()}:\n');identity['machine-id'].write_text('00000000000000000000000000000004\n')
  config=d/'sway.conf';config.write_text(SWAY_CONFIG)
  with (d/'stdout').open('w+b') as out:
   child=subprocess.Popen(sandbox_args(paths,identity,profile,runtime,config,nested_smoke,snapshots,focus_stress),stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.DEVNULL,env={'PATH':'/usr/bin'},start_new_session=True,preexec_fn=chrome.limits,pass_fds=tuple(snapshots.values()))
   try:rc=child.wait(timeout=65)
   except subprocess.TimeoutExpired:raise base.Refused('outer_timeout')
   out.seek(0);raw=out.read(32769)
   if len(raw)>32768:raise base.Refused('output_bound')
   try:r=json.loads(raw)
   except ValueError:return {'schema':1,'passed':False,'phase':'sandbox','exitCode':rc}
   r.update({'sandboxed':True,'referenceCompositor':'Sway/wlroots software only','hyprlandSupport':False,'slackSupport':False,'hostInput':False,'hostConfigurationChanged':False,'boundaryBasis':'fixed namespaces/mounts/env and guard tests; not a global OS change audit','exitCode':rc})
   r['passed']=rc==0 and r.get('passed') is True;return r
 finally:
  if child and child.poll() is None:
   os.killpg(child.pid,signal.SIGTERM)
   try:child.wait(timeout=1)
   except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=2)
  for fd in snapshots.values():os.close(fd)
  shutil.rmtree(d)

def inside():
 if not INSIDE or os.environ.get('RASKLAD_NATIVE_WAYLAND_LAB')!='1' or os.environ.get('HOME')!='/home/lab' or os.environ.get('DISPLAY') or os.environ.get('WAYLAND_SOCKET'):raise base.Refused('not_private_lab')
 os.chmod('/run/lab',0o700)
 config=Path('/home/lab/.config/fcitx5');config.mkdir(parents=True);(config/'profile').write_text(base.PROFILE)
 data=Path('/home/lab/.local/share/fcitx5/addon');data.mkdir(parents=True);(data/'raskladlab.conf').write_bytes(Path('/lab/addon.conf').read_bytes())
 children=[];phase='software_compositor'
 try:
  sway=subprocess.Popen(['/lab/wayland-runtime/sway','-c','/lab/sway.conf'],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(sway)
  for _ in range(100):
   if sway.poll() is not None:raise base.Refused('compositor_exited')
   sockets=[p for p in Path('/run/lab').glob('wayland-*') if p.is_socket()]
   if sockets:break
   time.sleep(.03)
  else:raise base.Refused('compositor_timeout')
  if len(sockets)!=1:raise base.Refused('ambiguous_private_display')
  Path('/run/lab/wayland-lab').symlink_to(sockets[0].name)
  os.environ['WAYLAND_DISPLAY']='wayland-lab'
  if os.environ.get('RASKLAD_NESTED_HYPR_SMOKE')=='1':
   runtime=Path('/run/nested');runtime.mkdir(mode=0o700)
   cfg=Path('/tmp/nested-hypr.conf');cfg.write_text('xwayland {\n enabled = false\n}\n')
   env=dict(os.environ,XDG_RUNTIME_DIR=str(runtime),WAYLAND_DISPLAY='/run/lab/wayland-lab',LIBGL_ALWAYS_SOFTWARE='1',LP_NUM_THREADS='1',HYPRLAND_NO_RT='1',HYPRLAND_NO_CRASHREPORTER='1')
   with open('/tmp/nested-hypr.log','w+b') as log:
    nested=subprocess.Popen(['/usr/bin/Hyprland','--config',str(cfg)],stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,env=env);children.append(nested)
    time.sleep(2)
    alive=nested.poll() is None;ready=any(p.is_socket() for p in runtime.glob('wayland-*'))
    log.seek(0,2);size=log.tell();log.seek(max(0,size-1600));tail=log.read(1600).decode('utf8','replace')
    return {'schema':1,'passed':ready,'mode':'nested_hyprland_smoke','aliveAfter2Seconds':alive,'nestedWaylandSocket':ready,'nestedExitCode':nested.poll(),'diagnostic':tail}
  phase='native_browser_driver'
  clients=load('native_clients',Path('/lab/native_ime_clients.py'))
  return clients.run(base,children)
 except (base.Refused,ValueError,subprocess.SubprocessError) as e:return {'schema':1,'passed':False,'phase':phase,'reason':str(e) if isinstance(e,base.Refused) else type(e).__name__}
 finally:
  for child in reversed(children):
   if child.poll() is None:
    child.terminate()
    try:child.wait(timeout=1)
    except subprocess.TimeoutExpired:child.kill();child.wait(timeout=1)

def main():
 p=argparse.ArgumentParser(description=__doc__);a=p.add_mutually_exclusive_group();a.add_argument('--run',action='store_true');a.add_argument('--smoke-nested-hypr',action='store_true');a.add_argument('--inside',action='store_true',help=argparse.SUPPRESS);p.add_argument('--input-root');p.add_argument('--focus-stress',action='store_true');args=p.parse_args()
 try:
  if args.focus_stress and (args.inside or args.smoke_nested_hypr):raise base.Refused('focus_suite_outer_native_only')
  r=inside() if args.inside else run(args.input_root,True) if args.smoke_nested_hypr else run(args.input_root,focus_stress=args.focus_stress) if args.run else {'schema':1,'status':'plan_only','tested':False,'hostInput':False,'hyprlandSupport':False,'slackSupport':False,'focusStressPlanned':args.focus_stress,'transport':'private headless Sway/pixman; virtual keyboard -> native Wayland Chromium -> real Fcitx; no host seat/GPU'}
  print(json.dumps(r,sort_keys=True));return 0 if r.get('passed',True) else 1
 except (base.Refused,OSError,ValueError,KeyError,subprocess.SubprocessError) as e:
  print(json.dumps({'schema':1,'passed':False,'reason':str(e) if isinstance(e,base.Refused) else type(e).__name__}));return 2
if __name__=='__main__':sys.exit(main())
