#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Native Wayland reference test, never the user's compositor. Default plan only."""
import argparse,hashlib,importlib.util,json,os,signal,subprocess,sys,tempfile,shutil,time,stat
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
INSIDE=Path(__file__)==Path('/lab/runner.py')
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
base=load('fcitx_base',Path('/lab/fcitx_lab_runner.py') if INSIDE else ROOT/'tools/fcitx_lab_runner.py')
chrome=None if INSIDE else load('chrome_base',ROOT/'tools/chromium_ime_lab.py')
core_guard=None if INSIDE else load('core_entry_guard',ROOT/'tools/core_entry_witness_guard.py')
RUNTIME={'sway','libwlroots-0.20.so','libliftoff.so.0'}
SWAY_CONFIG='''xwayland disable
seat seat0 fallback true
output HEADLESS-1 mode 1000x800
input * xkb_layout us
focus_follows_mouse no
default_border none
'''

FRONTEND_COMMIT='27cca6e0239938b2d53937061d0980c087856b22'

def frontend_selection(variant,nested_smoke=False):
 if variant not in ('system','baseline','keep-grab'):raise base.Refused('private_frontend_variant')
 if variant!='system' and nested_smoke:raise base.Refused('private_frontend_sway_only')

def disable_selection(disable_suite,frontend_variant,nested_smoke=False,focus_stress=False,space_telemetry=False,addon_witness=False,witness_no_query=False,core_entry_witness=False,grab_entry_witness=False,focus_repeats=1,server_route_witness=False,grab_epoch_witness=False):
 if not disable_suite:return
 if (nested_smoke or focus_stress or space_telemetry or addon_witness or witness_no_query
     or core_entry_witness or grab_entry_witness or server_route_witness or grab_epoch_witness
     or focus_repeats!=1):raise base.Refused('disable_suite_incompatible_options')
 if frontend_variant!='keep-grab':raise base.Refused('disable_suite_requires_keep_grab')

def validate_frontend(variant):
 frontend_selection(variant)
 if variant=='system':return None
 core_guard.validate_core_entry(base,ROOT)
 lock=json.loads(core_guard._read_bounded(base,ROOT/'data/fcitx-frontend-source-lock.json',16384))
 record=ROOT/('build/wayland-ime/frontend-baseline-result.json' if variant=='baseline' else 'build/wayland-ime/frontend-keep-grab-build-result.json')
 metadata=json.loads(core_guard._read_bounded(base,record,16384))
 binary=ROOT/f'build/wayland-ime/frontend-{variant}/libwaylandim.so'
 payload=core_guard._read_bounded(base,binary,5_000_000)
 digest=hashlib.sha256(payload).hexdigest()
 if not isinstance(lock,dict) or not isinstance(metadata,dict):raise base.Refused('private_frontend_build_record')
 if (lock.get('commit')!=FRONTEND_COMMIT or metadata.get('sourceCommit')!=FRONTEND_COMMIT
     or metadata.get('sourceArchiveSha256')!=lock.get('archiveSha256')
     or not isinstance(lock.get('archiveSha256'),str) or len(lock['archiveSha256'])!=64 or set(lock['archiveSha256'])-set('0123456789abcdef')
     or metadata.get('built') is not True or metadata.get('schema')!=1
     or metadata.get('variant')!=('unmodified_source_control' if variant=='baseline' else 'private_keep_grab_experiment')
     or metadata.get('artifactSha256')!=digest):raise base.Refused('private_frontend_build_record')
 if variant=='keep-grab':
  patch_lock=json.loads(core_guard._read_bounded(base,ROOT/'patches/fcitx5/keep-grab-lock.json',16384))
  if (not isinstance(patch_lock,dict) or patch_lock.get('schema')!=1
      or patch_lock.get('sourceCommit')!=FRONTEND_COMMIT):raise base.Refused('private_frontend_patch_record')
  for key in ('patchSha256','cancellationPatchSha256'):
   expected=patch_lock.get(key)
   if (not isinstance(expected,str) or len(expected)!=64 or set(expected)-set('0123456789abcdef')
       or metadata.get(key)!=expected):raise base.Refused('private_frontend_patch_record')
 return binary,digest

def extract_private_browser_diagnostic(result,variant):
 raw=result.pop('_privateBrowserDiagnostic',None)
 if (variant not in ('baseline','keep-grab') or result.get('passed') is not False
     or not isinstance(raw,str)):return
 raw=raw[-3500:]
 path=ROOT/f'build/wayland-ime/native-browser-{variant}-error.log'
 # Fixed private fixture output only; never follow or truncate an unsafe file.
 for parent in (ROOT/'build',ROOT/'build/wayland-ime'):
  info=parent.lstat()
  if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o022:
   raise base.Refused('private_browser_diagnostic_directory')
 fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_NOFOLLOW|os.O_CLOEXEC,0o600)
 try:
  info=os.fstat(fd)
  if not stat.S_ISREG(info.st_mode) or info.st_nlink!=1 or info.st_uid!=os.getuid():
   raise base.Refused('private_browser_diagnostic_file')
  os.fchmod(fd,0o600);os.ftruncate(fd,0)
  payload=raw.encode('utf8')
  while payload:
   count=os.write(fd,payload);payload=payload[count:]
 finally:os.close(fd)

MANUAL_SUITE=False  # SYS-225 slice 1 suite; set only by run(manual_suite=True)
PRODUCT_SUITE=None  # SYS-246 product addon suite: 'listed' or 'unlisted'; set only by run()
def sandbox_args(paths,identity,profile,runtime,config,nested_smoke=False,snapshot_fds=None,focus_stress=False,space_telemetry=False,addon_witness=False,witness_no_query=False,core_entry_witness=False,grab_entry_witness=False,focus_repeats=1,server_route_witness=False,grab_epoch_witness=False,frontend_variant="system",disable_suite=False,slack_gate=False):
 disable_selection(disable_suite,frontend_variant,nested_smoke,focus_stress,space_telemetry,addon_witness,witness_no_query,core_entry_witness,grab_entry_witness,focus_repeats,server_route_witness,grab_epoch_witness)
 frontend_selection(frontend_variant,nested_smoke)
 if grab_epoch_witness and not server_route_witness:raise base.Refused('grab_epoch_requires_server_route_witness')
 if server_route_witness and not grab_entry_witness:raise base.Refused('server_route_requires_grab_entry_witness')
 if type(focus_repeats) is not int or not 1<=focus_repeats<=5:raise base.Refused('focus_repeats_bound')
 if focus_repeats>1 and not focus_stress:raise base.Refused('focus_repeats_requires_focus_suite')
 if grab_entry_witness and not core_entry_witness:raise base.Refused('grab_entry_witness_requires_core_entry_witness')
 if core_entry_witness and (not addon_witness or witness_no_query):raise base.Refused('core_entry_witness_requires_querying_addon_witness')
 if witness_no_query and not addon_witness:raise base.Refused('witness_no_query_requires_addon_witness')
 if addon_witness and not (focus_stress and space_telemetry):raise base.Refused('addon_witness_requires_focus_and_space_telemetry')
 if core_entry_witness and (not snapshot_fds or '/lab/raskladcoreentry.so' not in snapshot_fds):raise base.Refused('core_entry_snapshot_required')
 if server_route_witness and (not snapshot_fds or '/lab/raskladserverroute.so' not in snapshot_fds):raise base.Refused('server_route_snapshot_required')
 if addon_witness:
  paths=dict(paths);paths['addon']=ROOT/('build/chromium-ime/grab-entry-addon/raskladlab.so' if grab_entry_witness else 'build/chromium-ime/core-entry-addon/raskladlab.so' if core_entry_witness else 'build/chromium-ime/witness-addon/raskladlab.so');paths['addon_conf']=ROOT/'tests/fcitx_transport/raskladwitness.conf'
  base.regular_file(ROOT/'tests/chromium_ime/addon-witness.mjs')
 if frontend_variant!='system' and (not snapshot_fds or '/lab/addons/libwaylandim.so' not in snapshot_fds):raise base.Refused('private_frontend_snapshot_required')
 if disable_suite:
  paths=dict(paths);paths['addon']=ROOT/'build/chromium-ime/disable-addon/raskladlab.so'
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
 if frontend_variant!='system':extra+=['--perms','0755','--ro-bind-data',str(snapshot_fds['/lab/addons/libwaylandim.so']),'/lab/addons/libwaylandim.so','--setenv','RASKLAD_PRIVATE_FRONTEND_VARIANT',frontend_variant]
 if disable_suite:extra+=['--setenv','RASKLAD_DISABLE_TEST_LAB','1']
 if MANUAL_SUITE:extra+=['--setenv','RASKLAD_MANUAL_LAB','1']
 if PRODUCT_SUITE:extra+=['--setenv','RASKLAD_PRODUCT_SUITE',PRODUCT_SUITE]
 if slack_gate:
  # The gate-test library loads from the sealed snapshot at /lab/addons/raskladlab.so.
  extra+=['--setenv','RASKLAD_SLACK_SESSION_LAB','1','--setenv','RASKLAD_SLACK_GATE_LAB','1']
  if slack_gate=='control':extra+=['--setenv','RASKLAD_SLACK_GATE_CONTROL','1']
  if slack_gate=='supervised':extra+=['--setenv','RASKLAD_SLACK_SUPERVISED_LAB','1','--ro-bind',str(ROOT/'tools/slack_supervisor.py'),'/lab/slack_supervisor.py','--ro-bind',str(ROOT/'tools/slack_injector.py'),'/lab/slack_injector.py']
 if nested_smoke:extra+=['--setenv','RASKLAD_NESTED_HYPR_SMOKE','1']
 if focus_stress:
  extra+=['--setenv','RASKLAD_FOCUS_STRESS_LAB','1']
  for source,target in [('tests/chromium_ime/adversarial-focus.mjs','adversarial-focus.mjs'),('tests/chromium_ime/focus-policy.mjs','focus-policy.mjs'),('tests/chromium_ime/space-telemetry.mjs','space-telemetry.mjs'),('data/focus-source-review-001.json','focus-source-review.json')]:extra+=['--ro-bind',str(ROOT/source),'/lab/'+target]
 if space_telemetry:
  if not focus_stress:raise base.Refused('space_telemetry_requires_focus_suite')
  extra+=['--setenv','RASKLAD_SPACE_TELEMETRY_LAB','1']
 if addon_witness:
  extra+=['--ro-bind',str(ROOT/'tests/chromium_ime/addon-witness.mjs'),'/lab/addon-witness.mjs','--setenv','RASKLAD_ADDON_WITNESS_LAB','1']
 if witness_no_query:extra+=['--setenv','RASKLAD_ADDON_WITNESS_NO_QUERY_LAB','1']
 if core_entry_witness:extra+=['--perms','0755','--ro-bind-data',str(snapshot_fds['/lab/raskladcoreentry.so']),'/lab/raskladcoreentry.so','--setenv','RASKLAD_CORE_ENTRY_WITNESS_LAB','1']
 if grab_entry_witness:extra+=['--setenv','RASKLAD_GRAB_ENTRY_WITNESS_LAB','1']
 if server_route_witness:extra+=['--perms','0755','--ro-bind-data',str(snapshot_fds['/lab/raskladserverroute.so']),'/lab/raskladserverroute.so','--ro-bind',str(ROOT/'tools/server_route_witness_guard.py'),'/lab/server_route_witness_guard.py','--ro-bind',str(ROOT/'tests/chromium_ime/server-route-witness.mjs'),'/lab/server-route-witness.mjs','--setenv','RASKLAD_SERVER_ROUTE_WITNESS_LAB','1']
 if grab_epoch_witness:extra+=['--setenv','RASKLAD_GRAB_EPOCH_WITNESS_LAB','1']
 if focus_repeats>1:extra+=['--setenv','RASKLAD_FOCUS_REPEATS_LAB',str(focus_repeats)]
 result=clean[:at]+extra+clean[at:]
 if snapshot_fds:
  sealed=[];i=0
  while i<len(result):
   if result[i]=='--ro-bind' and i+2<len(result) and result[i+2] in snapshot_fds:
    sealed+=['--perms','0755','--ro-bind-data',str(snapshot_fds[result[i+2]]),result[i+2]];i+=3
   else:sealed.append(result[i]);i+=1
  result=sealed
 return result

SLACK_GATE_ADDON=ROOT/'build/chromium-ime/slack-gate-addon/raskladslackgatetest.so'


def slack_gate_selection(slack_gate,**options):
 # Exclusive offline gate test: plain system frontend, no other lab experiment.
 if slack_gate not in (False,True,'control','supervised'):raise base.Refused('slack_gate_mode')
 if not slack_gate:return
 for key,value in options.items():
  expected={'focus_repeats':1,'frontend_variant':'system'}.get(key,False)
  if type(value) is not type(expected) or value!=expected:raise base.Refused('slack_gate_incompatible_options')


def run(input_root,nested_smoke=False,focus_stress=False,space_telemetry=False,addon_witness=False,witness_no_query=False,core_entry_witness=False,grab_entry_witness=False,focus_repeats=1,server_route_witness=False,grab_epoch_witness=False,frontend_variant="system",disable_suite=False,slack_gate=False,*,manual_suite=False,manual_gesture=False,product_suite=None):
 slack_gate_selection(slack_gate,nested_smoke=nested_smoke,focus_stress=focus_stress,space_telemetry=space_telemetry,addon_witness=addon_witness,witness_no_query=witness_no_query,core_entry_witness=core_entry_witness,grab_entry_witness=grab_entry_witness,focus_repeats=focus_repeats,server_route_witness=server_route_witness,grab_epoch_witness=grab_epoch_witness,frontend_variant=frontend_variant,disable_suite=disable_suite)
 disable_selection(disable_suite,frontend_variant,nested_smoke,focus_stress,space_telemetry,addon_witness,witness_no_query,core_entry_witness,grab_entry_witness,focus_repeats,server_route_witness,grab_epoch_witness)
 frontend_selection(frontend_variant,nested_smoke)
 if grab_epoch_witness and not server_route_witness:raise base.Refused('grab_epoch_requires_server_route_witness')
 if server_route_witness and not grab_entry_witness:raise base.Refused('server_route_requires_grab_entry_witness')
 if type(focus_repeats) is not int or not 1<=focus_repeats<=5:raise base.Refused('focus_repeats_bound')
 if focus_repeats>1 and not focus_stress:raise base.Refused('focus_repeats_requires_focus_suite')
 if grab_entry_witness and not core_entry_witness:raise base.Refused('grab_entry_witness_requires_core_entry_witness')
 if core_entry_witness and (not addon_witness or witness_no_query):raise base.Refused('core_entry_witness_requires_querying_addon_witness')
 if witness_no_query and not addon_witness:raise base.Refused('witness_no_query_requires_addon_witness')
 if addon_witness and not (focus_stress and space_telemetry):raise base.Refused('addon_witness_requires_focus_and_space_telemetry')
 frontend=validate_frontend(frontend_variant)
 if core_entry_witness:core_guard.validate_core_entry(base,ROOT)
 if server_route_witness:
  load('server_route_guard',ROOT/'tools/server_route_witness_guard.py').validate(base,ROOT)
  base.regular_file(ROOT/'tests/chromium_ime/server-route-witness.mjs')
  base.regular_file(ROOT/('build/chromium-ime/server-grab-epoch-shim/raskladserverroute.so' if grab_epoch_witness else 'build/chromium-ime/server-route-shim/raskladserverroute.so'))
 if os.geteuid()==0:raise base.Refused('root_not_admitted')
 if input_root is None and not slack_gate and not manual_suite and not product_suite:raise base.Refused('explicit_input_root_required')
 if space_telemetry and not focus_stress:raise base.Refused('space_telemetry_requires_focus_suite')
 if product_suite not in (None,'listed','unlisted'):raise base.Refused('product_suite_mode')
 global MANUAL_SUITE,PRODUCT_SUITE;MANUAL_SUITE=bool(manual_suite or manual_gesture or product_suite);PRODUCT_SUITE=product_suite
 paths=chrome.files();paths.pop('xvfb');paths.pop('keys');paths['runner']=ROOT/'tools/wayland_ime_lab.py';paths['addon']=ROOT/('build/chromium-ime/grab-entry-addon/raskladlab.so' if grab_entry_witness else 'build/chromium-ime/core-entry-addon/raskladlab.so' if core_entry_witness else 'build/chromium-ime/witness-addon/raskladlab.so' if addon_witness else 'build/chromium-ime/profile-addon/raskladlab.so')
 if disable_suite:paths['addon']=ROOT/'build/chromium-ime/disable-addon/raskladlab.so'
 if manual_suite:
  if disable_suite or slack_gate or nested_smoke or focus_stress or space_telemetry or addon_witness or server_route_witness:raise base.Refused('manual_suite_alone_only')
  paths['addon']=ROOT/'build/chromium-ime/manual-addon/raskladlab.so'
 if product_suite:
  # SYS-246 product addon (no lab environment). Mounted under the lab library name.
  if manual_suite or manual_gesture or disable_suite or slack_gate or nested_smoke or focus_stress or space_telemetry or addon_witness or server_route_witness:raise base.Refused('product_suite_alone_only')
  paths['addon']=ROOT/'build/product-addon/rasklad.so';paths['addon_conf']=ROOT/'tests/fcitx_transport/raskladproduct.conf'
  if os.environ.get('RASKLAD_PRODUCT_PACKAGED_CLI')=='1':paths['addon']=ROOT/'build/arch-package/x/usr/lib/fcitx5/rasklad.so'  # SYS-228: the library extracted from the built package
 if slack_gate:paths['addon']=SLACK_GATE_ADDON;paths['addon_conf']=ROOT/'tests/slack_session/raskladslackgatetest.conf'
 if manual_gesture:
  if slack_gate!='supervised':raise base.Refused('manual_gesture_requires_slack_supervised')
  paths['addon']=ROOT/'build/chromium-ime/slack-manual-gate-addon/raskladslackmanualgatetest.so'
 if addon_witness:
  paths['addon_conf']=ROOT/'tests/fcitx_transport/raskladwitness.conf'
  base.regular_file(ROOT/'tests/chromium_ime/addon-witness.mjs')
 shim=ROOT/('build/chromium-ime/grab-entry-shim/raskladcoreentry.so' if grab_entry_witness else 'build/chromium-ime/core-entry-shim/raskladcoreentry.so')
 if core_entry_witness:base.regular_file(shim)
 for path in paths.values():base.regular_file(path)
 if focus_stress:
  for source in ['tests/chromium_ime/adversarial-focus.mjs','tests/chromium_ime/focus-policy.mjs','tests/chromium_ime/space-telemetry.mjs','data/focus-source-review-001.json']:base.regular_file(ROOT/source)
 base.regular_file(ROOT/'build/wayland-ime/keys/rasklad-wayland-keys',True)
 base.regular_file(Path('/usr/bin/bwrap'),True)
 meta=os.stat('/usr/bin/bwrap')
 if meta.st_uid!=0 or meta.st_mode&0o6022:raise base.Refused('unsafe_bwrap')
 lock=json.loads((ROOT/'data/wayland-lab-runtime-lock.json').read_text());runtime=ROOT/'build/wayland-ime/runtime'
 if {p.get('name') for p in lock['packages']}!={'sway','wlroots0.20','libliftoff'} or any(p.get('signatureVerified') is not True for p in lock['packages']):raise base.Refused('runtime_signatures')
 for name in RUNTIME:
  path=runtime/name;base.regular_file(path,name=='sway')
  if path.stat().st_size>5_000_000:raise base.Refused('runtime_size')

 # The gate test addon has no profile/dictionaries.
 profile={} if slack_gate or manual_suite or product_suite else {name:(ROOT if name.startswith('data/') else Path(input_root).absolute())/name for name in chrome.PROFILE_PATHS}
 for p in profile.values():base.regular_file(p)
 d=Path(tempfile.mkdtemp(prefix='native-wayland-',dir=base.ensure_private_base(ROOT)));child=None;snapshots={}
 try:
  if frontend:snapshots['/lab/addons/libwaylandim.so']=base.sealed_snapshot(frontend[0],frontend[1])
  for name in RUNTIME:snapshots['/lab/wayland-runtime/'+name]=base.sealed_snapshot(runtime/name,lock['runtimeFiles'][name])
  snapshots['/lab/rasklad-wayland-keys']=base.sealed_snapshot(ROOT/'build/wayland-ime/keys/rasklad-wayland-keys')
  snapshots['/lab/addons/raskladlab.so']=base.sealed_snapshot(paths['addon'])
  if core_entry_witness:snapshots['/lab/raskladcoreentry.so']=base.sealed_snapshot(shim)
  if server_route_witness:snapshots['/lab/raskladserverroute.so']=base.sealed_snapshot(ROOT/('build/chromium-ime/server-grab-epoch-shim/raskladserverroute.so' if grab_epoch_witness else 'build/chromium-ime/server-route-shim/raskladserverroute.so'))
  identity={k:d/k for k in ['passwd','group','machine-id']}
  identity['passwd'].write_text(f'lab:x:{os.getuid()}:{os.getgid()}:Private lab:/home/lab:/usr/bin/nologin\n');identity['group'].write_text(f'lab:x:{os.getgid()}:\n');identity['machine-id'].write_text('00000000000000000000000000000004\n')
  config=d/'sway.conf';config.write_text(SWAY_CONFIG)
  with (d/'stdout').open('w+b') as out:
   child=subprocess.Popen(sandbox_args(paths,identity,profile,runtime,config,nested_smoke,snapshots,focus_stress,space_telemetry,addon_witness,witness_no_query,core_entry_witness,grab_entry_witness,focus_repeats,server_route_witness,grab_epoch_witness,frontend_variant,disable_suite,slack_gate),stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.DEVNULL,env={'PATH':'/usr/bin'},start_new_session=True,preexec_fn=chrome.limits,pass_fds=tuple(snapshots.values()))
   try:rc=child.wait(timeout=65)
   except subprocess.TimeoutExpired:raise base.Refused('outer_timeout')
   out.seek(0);raw=out.read(32769)
   if len(raw)>32768:raise base.Refused('output_bound')
   try:r=json.loads(raw)
   except ValueError:return {'schema':1,'passed':False,'phase':'sandbox','exitCode':rc}
   extract_private_browser_diagnostic(r,frontend_variant)
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
 if os.environ.get('RASKLAD_PRODUCT_SUITE')=='unlisted':
  # Product addon configured without the fixture's app id: it must stay inert.
  (config/'conf').mkdir();(config/'conf'/'rasklad.conf').write_text('[Applications]\n0=slack\n')
 children=[];phase='software_compositor'
 try:
  server_env=load('server_route_child',Path('/lab/server_route_witness_guard.py')).child_env(base) if os.environ.get('RASKLAD_SERVER_ROUTE_WITNESS_LAB')=='1' else None
  sway=subprocess.Popen(['/lab/wayland-runtime/sway','-c','/lab/sway.conf'],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,**({} if server_env is None else {'env':server_env}));children.append(sway)
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
 p=argparse.ArgumentParser(description=__doc__);a=p.add_mutually_exclusive_group();a.add_argument('--run',action='store_true');a.add_argument('--smoke-nested-hypr',action='store_true');a.add_argument('--inside',action='store_true',help=argparse.SUPPRESS);p.add_argument('--input-root');p.add_argument('--disable-suite',action='store_true');p.add_argument('--focus-stress',action='store_true');p.add_argument('--space-telemetry',action='store_true');p.add_argument('--addon-witness',action='store_true');p.add_argument('--witness-no-query',action='store_true');p.add_argument('--core-entry-witness',action='store_true');p.add_argument('--grab-entry-witness',action='store_true');p.add_argument('--grab-epoch-witness',action='store_true');p.add_argument('--server-route-witness',action='store_true');p.add_argument('--focus-repeats',type=int,choices=range(1,6),default=1);p.add_argument('--frontend-variant',choices=('system','baseline','keep-grab'),default='system');p.add_argument('--slack-gate',action='store_true');p.add_argument('--slack-gate-control',action='store_true');p.add_argument('--slack-supervised',action='store_true');p.add_argument('--manual-suite',action='store_true');p.add_argument('--manual-gesture',action='store_true');p.add_argument('--product-suite',choices=('listed','unlisted'));p.add_argument('--packaged',action='store_true');args=p.parse_args()
 os.environ.pop('RASKLAD_PRODUCT_PACKAGED_CLI',None)
 if args.packaged and not args.product_suite:raise SystemExit('--packaged requires --product-suite')
 if args.packaged:os.environ['RASKLAD_PRODUCT_PACKAGED_CLI']='1'
 if (args.slack_gate or args.slack_gate_control or args.slack_supervised) and (not args.run or args.smoke_nested_hypr or args.inside or sum((args.slack_gate,args.slack_gate_control,args.slack_supervised))>1):raise base.Refused('slack_gate_requires_run_only')
 try:
  disable_selection(args.disable_suite,args.frontend_variant,args.smoke_nested_hypr,args.focus_stress,args.space_telemetry,args.addon_witness,args.witness_no_query,args.core_entry_witness,args.grab_entry_witness,args.focus_repeats,args.server_route_witness,args.grab_epoch_witness)
  frontend_selection(args.frontend_variant,args.smoke_nested_hypr)
  if args.grab_epoch_witness and not args.server_route_witness:raise base.Refused('grab_epoch_requires_server_route_witness')
  if args.server_route_witness and not args.grab_entry_witness:raise base.Refused('server_route_requires_grab_entry_witness')
  if type(args.focus_repeats) is not int or not 1<=args.focus_repeats<=5:raise base.Refused('focus_repeats_bound')
  if args.focus_repeats>1 and not args.focus_stress:raise base.Refused('focus_repeats_requires_focus_suite')
  if args.grab_entry_witness and not args.core_entry_witness:raise base.Refused('grab_entry_witness_requires_core_entry_witness')
  if args.core_entry_witness and (not args.addon_witness or args.witness_no_query):raise base.Refused('core_entry_witness_requires_querying_addon_witness')
  if args.witness_no_query and not args.addon_witness:raise base.Refused('witness_no_query_requires_addon_witness')
  if args.addon_witness and not (args.focus_stress and args.space_telemetry):raise base.Refused('addon_witness_requires_focus_and_space_telemetry')
  if args.focus_stress and (args.inside or args.smoke_nested_hypr):raise base.Refused('focus_suite_outer_native_only')
  if args.space_telemetry and not args.focus_stress:raise base.Refused('space_telemetry_requires_focus_suite')
  r=inside() if args.inside else run(args.input_root,True,frontend_variant=args.frontend_variant) if args.smoke_nested_hypr else run(args.input_root,manual_suite=args.manual_suite,manual_gesture=args.manual_gesture,product_suite=args.product_suite,focus_stress=args.focus_stress,space_telemetry=args.space_telemetry,addon_witness=args.addon_witness,witness_no_query=args.witness_no_query,core_entry_witness=args.core_entry_witness,grab_entry_witness=args.grab_entry_witness,focus_repeats=args.focus_repeats,server_route_witness=args.server_route_witness,grab_epoch_witness=args.grab_epoch_witness,frontend_variant=args.frontend_variant,disable_suite=args.disable_suite,slack_gate='control' if args.slack_gate_control else 'supervised' if args.slack_supervised else args.slack_gate) if args.run else {'schema':1,'frontendVariant':args.frontend_variant,'status':'plan_only','tested':False,'hostInput':False,'hyprlandSupport':False,'slackSupport':False,'focusStressPlanned':args.focus_stress,'spaceTelemetryPlanned':args.space_telemetry,'addonWitnessPlanned':args.addon_witness,'witnessNoQueryPlanned':args.witness_no_query,'coreEntryWitnessPlanned':args.core_entry_witness,'grabEntryWitnessPlanned':args.grab_entry_witness,'focusRepeatCount':args.focus_repeats,'serverRouteWitnessPlanned':args.server_route_witness,'grabEpochWitnessPlanned':args.grab_epoch_witness,'transport':'private headless Sway/pixman; virtual keyboard -> native Wayland Chromium -> real Fcitx; no host seat/GPU'}
  if args.disable_suite and not (args.run or args.inside):r['disableSuitePlanned']=True
  print(json.dumps(r,sort_keys=True));return 0 if r.get('passed',True) else 1
 except (base.Refused,OSError,ValueError,KeyError,subprocess.SubprocessError) as e:
  print(json.dumps({'schema':1,'passed':False,'reason':str(e) if isinstance(e,base.Refused) else type(e).__name__}));return 2
if __name__=='__main__':sys.exit(main())
