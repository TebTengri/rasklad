#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepared GPU-only closed Sway -> Hyprland mapping lab; default is inert."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import resource
import shutil
import socket
import stat
import subprocess
import tempfile
import time
import uuid

INSIDE = Path(__file__) == Path('/lab/mapping.py')
ROOT = Path('/lab') if INSIDE else Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


base = load('mapping_base', ROOT / ('fcitx_lab_runner.py' if INSIDE else 'tools/fcitx_lab_runner.py'))

# Runs OUTSIDE bwrap, inside the new own scope, then execs bwrap. Only fixed
# controller values leave this check as success/failure; host cgroupfs is never
# mounted into the compositor namespace.
SCOPE_GUARD = '''import os,sys
from pathlib import Path
rows=Path('/proc/self/cgroup').read_text().splitlines()
paths=[r[3:] for r in rows if r.startswith('0::/')]
if len(paths)!=1: raise SystemExit(71)
p=Path('/sys/fs/cgroup')/paths[0].lstrip('/')
def bounded(name,upper):
 value=(p/name).read_text().strip()
 return value.isdigit() and 0<int(value)<=upper
cpu=(p/'cpu.max').read_text().split()
ok=bounded('memory.max',MEMORY_MAX) and bounded('pids.max',TASKS_MAX) and (p/'memory.swap.max').read_text().strip()=='0'
ok=ok and len(cpu)==2 and all(v.isdigit() for v in cpu) and 0<int(cpu[0]) and int(cpu[0])*100<=CPU_PERCENT*int(cpu[1])
if not ok: raise SystemExit(72)
os.execv(sys.argv[1],sys.argv[1:])
'''


# Mapping probe defaults stay unchanged; other closed sessions may only tighten
# or use their own reviewed ceilings below.
MAPPING_LIMITS = dict(prefix='rasklad-mapping-', memory_mib=1536, tasks=96, cpu_percent=100, runtime_seconds=27)
# Ceilings are bound to the unit prefix: a mapping unit can never exceed its original limits.
CEILINGS = {'rasklad-mapping-': dict(memory_mib=1536, tasks=96, cpu_percent=100, runtime_seconds=27),
            'rasklad-slack-': dict(memory_mib=4096, tasks=512, cpu_percent=200, runtime_seconds=660)}


def scope_guard(memory_mib, tasks, cpu_percent=100):
    return (SCOPE_GUARD.replace('MEMORY_MAX', str(memory_mib << 20)).replace('TASKS_MAX', str(tasks))
            .replace('CPU_PERCENT', str(cpu_percent)))


def checked_limits(limits_):
    if type(limits_) is not dict:
        raise base.Refused('scope-limits')
    limits_ = dict(limits_)
    if set(limits_) != set(MAPPING_LIMITS) or limits_['prefix'] not in CEILINGS:
        raise base.Refused('scope-limits')
    for key, ceiling in CEILINGS[limits_['prefix']].items():
        value = limits_[key]
        if type(value) is not int or not 0 < value <= ceiling:
            raise base.Refused('scope-limits')
    return limits_


def limits():
    for kind, value in ((resource.RLIMIT_CORE, 0), (resource.RLIMIT_CPU, 25),
                        (resource.RLIMIT_NOFILE, 256), (resource.RLIMIT_FSIZE, 16 << 20)):
        resource.setrlimit(kind, (value, value))


def plan():
    return dict(schema=1, mode='plan', requiresExplicitApproval=True,
                hostWaylandConnections=0, gpu='/dev/dri/renderD128', network=False,
                hostSysfs=False, primaryStatOnly=True, realInput=False,
                privateCompositors=['Sway', 'Hyprland'], maxWrapperSeconds=40,
                hostConfigurationChanged=False, fieldBindingVerified=False,
                memoryMaxBytes=1610612736, tasksMax=96, cpuQuotaPercent=100,
                sharedGpuRisk=True)


def scope_command(command, unit, limits_=MAPPING_LIMITS):
    limits_ = checked_limits(limits_)
    if not unit.startswith(limits_['prefix']) or not unit.endswith('.scope') or not all(c.isalnum() or c in '-.' for c in unit):
        raise base.Refused('scope-name')
    return ['/usr/bin/systemd-run', '--user', '--scope', '--collect', '--quiet', '--unit=' + unit,
            f"--property=MemoryMax={limits_['memory_mib']}M", '--property=MemorySwapMax=0',
            f"--property=TasksMax={limits_['tasks']}", f"--property=CPUQuota={limits_['cpu_percent']}%",
            f"--property=RuntimeMaxSec={limits_['runtime_seconds']}s", '--property=TimeoutStopSec=1s',
            '--', '/usr/bin/python3', '-I', '-c', scope_guard(limits_['memory_mib'], limits_['tasks'], limits_['cpu_percent'])] + command


def cleanup_scope(unit, env):
    subprocess.run(['/usr/bin/systemctl', '--user', 'stop', unit], env=env,
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3)
    with tempfile.TemporaryFile() as output:
        subprocess.run(['/usr/bin/systemctl', '--user', 'show', unit,
                        '--property=ActiveState', '--property=LoadState', '--property=ControlGroup'],
                       env=env, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.DEVNULL, timeout=3)
        output.seek(0)
        raw = output.read(4097)
    if len(raw) > 4096:
        raise base.Refused('scope-cleanup-unverified')
    fields = dict(line.split('=', 1) for line in raw.decode('ascii').splitlines() if '=' in line)
    if fields.get('ActiveState') != 'inactive' or fields.get('ControlGroup'):
        raise base.Refused('scope-cleanup-unverified')


def verified_report(report, exit_code):
    if (exit_code != 0 or type(report) is not dict or report.get('ok') is not True or
            report.get('mappingMode') is not True or report.get('mappingVerified') is not True or
            report.get('observerCleanup') is not True):
        raise base.Refused('mapping-not-verified')
    return report


def failure_report(report, exit_code):
    """Forward finite technical categories only, never child log text."""
    result = dict(schema=1, ok=False, reason='mapping-not-verified',
                  sandboxExit=exit_code if type(exit_code) is int and -128 <= exit_code <= 255 else None)
    if type(report) is not dict:
        return result
    if report.get('phase') in ('parent-start', 'parent-feedback', 'hyprland-start', 'mapping-client'):
        result['phase'] = report['phase']
    reasons = ('private-compositor-unready', 'mapping-not-verified', 'client-output-bound',
               'compositor-exited', 'parent-feedback-not-verified', 'TimeoutExpired', 'OSError', 'ValueError')
    if report.get('reason') in reasons:
        result['reason'] = report['reason']
    categories = ('unclassified_startup_failure', 'drm_device_discovery_failed', 'parent_wayland_connect_failed',
                  'parent_protocols_missing', 'dmabuf_node_open_failed', 'allocator_node_reopen_failed',
                  'gpu_allocator_unavailable', 'egl_initialization_reported', 'backend_creation_failed')
    for key in ('parentStartup', 'hyprlandStartup'):
        if report.get(key) in categories:
            result[key] = report[key]
    for key in ('parentExit', 'hyprlandExit', 'probeExit'):
        value = report.get(key)
        if type(value) is int and -128 <= value <= 255:
            result[key] = value
    for key in ('feedbackDone', 'deviceResolved', 'renderNodeMatched'):
        if type(report.get(key)) is bool:
            result[key] = report[key]
    if type(report.get('startupFlags')) is dict:
        result['startupFlags'] = {key: report['startupFlags'][key] for key in STARTUP_MARKERS
                                  if type(report['startupFlags'].get(key)) is bool}
        if type(report['startupFlags'].get('scanTruncated')) is bool:
            result['startupFlags']['scanTruncated'] = report['startupFlags']['scanTruncated']
    return result


STARTUP_MARKERS = {
    'waylandBackendStarted': b'Starting the Wayland backend!',
    'parentConnected': b'Connected to a wayland compositor:',
    'dmabufDeviceSeen': b'zwp_linux_dmabuf_v1: Got main device',
    'dmabufNodeOpened': b'zwp_linux_dmabuf_v1: opened node',
    'drmDiscoveryFailed': b'drmGetDeviceFromDevId failed',
    'primeExportDenied': b'PRIME export is not supported',
    'gbmDeviceFailed': b'gbm failed to create a device',
    'allocatorCreated': b'Created a GBM allocator with drm fd',
}


def startup_flags(log):
    """Scan bounded OWN log from its start so format dumps cannot hide causes."""
    flags = {key: False for key in STARTUP_MARKERS}
    log.seek(0)
    remaining, carry = 16 << 20, b''
    while remaining:
        chunk = log.read(min(4096, remaining))
        if not chunk:
            break
        remaining -= len(chunk)
        window = carry + chunk
        for key, marker in STARTUP_MARKERS.items():
            flags[key] = flags[key] or marker in window
        carry = window[-128:]
    flags['scanTruncated'] = bool(log.read(1))
    return flags


def feedback_verified(report, code):
    keys = ('ok', 'feedbackDone', 'deviceResolved', 'renderNodeMatched')
    return code == 0 and type(report) is dict and all(report.get(key) is True for key in keys)


def sandbox_args(fds, paths, metadata):
    primary = metadata['statOnlyPrimary']
    if (not primary.startswith('/dev/dri/card') or
            not primary[len('/dev/dri/card'):].isdigit() or
            metadata.get('primaryBindMustBeNodevReadOnly') is not True or
            metadata.get('hostSysfsBind') is not False):
        raise base.Refused('metadata-plan')
    argv = ['/usr/bin/bwrap', '--unshare-all', '--die-with-parent', '--new-session',
            '--cap-drop', 'ALL', '--clearenv', '--ro-bind', '/usr', '/usr',
            '--symlink', 'usr/bin', '/bin', '--symlink', 'usr/lib', '/lib',
            '--symlink', 'usr/lib', '/lib64', '--ro-bind-try', '/etc/ld.so.cache', '/etc/ld.so.cache',
            '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp', '--tmpfs', '/run',
            '--tmpfs', '/home', '--dir', '/home/lab', '--dir', '/run/lab',
            '--dir', '/dev/dri', '--dir', '/lab/runtime', '--chdir', '/home/lab',
            '--dev-bind', '/dev/dri/renderD128', '/dev/dri/renderD128',
            '--ro-bind', primary, primary, '--ro-bind', metadata['sysfs'], '/sys']
    for name, fd in fds.items():
        argv += ['--perms', '0755' if name in ('sway', 'probe') else '0444',
                 '--ro-bind-data', str(fd), '/lab/probe' if name == 'probe' else '/lab/runtime/' + name]
    for source, target in paths:
        argv += ['--ro-bind', str(source), target]
    env = dict(PATH='/usr/bin', HOME='/home/lab', XDG_RUNTIME_DIR='/run/lab', LANG='C.UTF-8',
               XDG_CONFIG_HOME='/home/lab/.config', XDG_CACHE_HOME='/home/lab/.cache',
               XDG_DATA_HOME='/home/lab/.local/share', RASKLAD_CLOSED_MAPPING='1',
               RASKLAD_STAT_ONLY_PRIMARY=primary, HYPRLAND_NO_RT='1', HYPRLAND_NO_CRASHREPORTER='1',
               AQ_DRM_DEVICES='/dev/dri/renderD128')
    for key, value in env.items():
        argv += ['--setenv', key, value]
    return argv + ['--', '/usr/bin/python3', '-I', '/lab/mapping.py', '--inside']


def run():
    if os.geteuid() == 0:
        raise base.Refused('root-not-admitted')
    native = load('mapping_native', ROOT / 'tests/window_provenance/run.py')
    collector = load('mapping_metadata', ROOT / 'tools/drm_metadata_snapshot.py')
    for path in ('/usr/bin/bwrap', '/usr/bin/Hyprland', '/usr/bin/systemd-run', '/usr/bin/systemctl'):
        base.regular_file(Path(path), True)
        info = Path(path).stat()
        if info.st_uid != 0 or info.st_mode & 0o6022:
            raise base.Refused('unsafe-system-binary')
    native.directory()
    record = json.loads((native.BUILD / 'build-record.json').read_text())
    if record.get('schema') != 1 or record.get('inputs') != native.inputs():
        raise base.Refused('stale-build')
    lock = json.loads((ROOT / 'data/wayland-lab-runtime-lock.json').read_text())
    if len(lock['packages']) != 3 or not all(p.get('signatureVerified') is True for p in lock['packages']):
        raise base.Refused('runtime-signatures')
    fds = {}
    directory = Path(tempfile.mkdtemp(prefix='closed-mapping-', dir=base.ensure_private_base(ROOT)))
    try:
        fds['probe'] = base.sealed_snapshot(native.BUILD / 'native-windows', record['binarySha256'])
        for name in native.RUNTIME:
            fds[name] = base.sealed_snapshot(ROOT / 'build/wayland-ime/runtime' / name, lock['runtimeFiles'][name])
        gpu = collector.collect_metadata()
        metadata = collector.write_snapshot(gpu, directory / 'gpu-metadata')
        paths = [(Path(__file__).resolve(), '/lab/mapping.py'),
                 (ROOT / 'tools/fcitx_lab_runner.py', '/lab/fcitx_lab_runner.py'),
                 (ROOT / 'tools/hyprland_nested_probe.py', '/lab/hyprland_nested_probe.py')]
        for filename, content in (('passwd', f'lab:x:{os.getuid()}:{os.getgid()}:Lab:/home/lab:/usr/bin/nologin\n'),
                                  ('group', f'lab:x:{os.getgid()}:\n'),
                                  ('machine-id', '00000000000000000000000000000006\n')):
            path = directory / filename
            path.write_text(content)
            paths.append((path, '/etc/' + filename))
        for node in gpu['nodes']:
            path = Path('/dev/dri') / node['name']
            info = path.lstat()
            if not stat.S_ISCHR(info.st_mode) or info.st_rdev != os.makedev(node['major'], node['minor']):
                raise base.Refused('changed-gpu-identity')
        runtime = Path(os.environ.get('XDG_RUNTIME_DIR', ''))
        if not runtime.is_absolute() or runtime.is_symlink() or not runtime.is_dir() or runtime.stat().st_uid != os.getuid() or runtime.stat().st_mode & 0o077:
            raise base.Refused('scope-runtime')
        launcher_env = {'PATH': '/usr/bin', 'XDG_RUNTIME_DIR': str(runtime)}
        unit = 'rasklad-mapping-' + uuid.uuid4().hex + '.scope'
        try:
            with tempfile.TemporaryFile() as out:
                result = subprocess.run(scope_command(sandbox_args(fds, paths, metadata), unit), pass_fds=tuple(fds.values()),
                                        env=launcher_env, stdin=subprocess.DEVNULL, stdout=out,
                                        stderr=subprocess.DEVNULL, timeout=30, preexec_fn=limits)
                out.seek(0)
                raw = out.read(8193)
        finally:
            # Fixed unique OWN transient scope only; never stop another application.
            cleanup_scope(unit, launcher_env)
        if len(raw) > 8192:
            raise base.Refused('output-bound')
        report = json.loads(raw)
        if result.returncode or type(report) is not dict or report.get('ok') is not True:
            report = failure_report(report, result.returncode)
        else:
            report = verified_report(report, result.returncode)
        report.update(schema=1, hostWaylandConnections=0, network=False, realInput=False,
                      hostConfigurationChanged=False, fieldBindingVerified=False,
                      binarySha256=record['binarySha256'], scopeLimitsVerified=True, scopeInactiveVerified=True)
        return report
    finally:
        for fd in fds.values():
            os.close(fd)
        shutil.rmtree(directory)


def wait_socket(server, directory, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if server.poll() is not None:
            break
        sockets = [p for p in directory.glob('wayland-*') if p.is_socket()]
        if len(sockets) == 1:
            return sockets[0]
        if len(sockets) > 1:
            break
        time.sleep(.03)
    raise base.Refused('private-compositor-unready')


def stop(server):
    if server is not None and server.poll() is None:
        server.terminate()
        try:
            server.wait(timeout=1)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=1)


def inside():
    if (not INSIDE or os.environ.get('RASKLAD_CLOSED_MAPPING') != '1' or
            os.environ.get('HOME') != '/home/lab' or os.environ.get('XDG_RUNTIME_DIR') != '/run/lab' or
            any(os.environ.get(k) for k in ('WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'))):
        raise base.Refused('not-private-lab')
    previous = load('mapping_mount_guard', Path('/lab/hyprland_nested_probe.py'))
    previous.require_stat_only_mounts(Path('/proc/self/mountinfo').read_text(), [os.environ['RASKLAD_STAT_ONLY_PRIMARY']])
    parent_dir, child_dir = Path('/run/lab/parent'), Path('/run/lab/child')
    for path in (Path('/run/lab'), parent_dir, child_dir):
        path.mkdir(mode=0o700, exist_ok=True)
        path.chmod(0o700)
    config = Path('/tmp/sway.conf')
    config.write_text('xwayland disable\nseat seat0 fallback true\noutput HEADLESS-1 mode 640x480\n')
    env = dict(os.environ, XDG_RUNTIME_DIR=str(parent_dir), LD_LIBRARY_PATH='/lab/runtime',
               WLR_BACKENDS='headless', WLR_RENDERER='gles2', WLR_LIBINPUT_NO_DEVICES='1',
               WLR_RENDER_DRM_DEVICE='/dev/dri/renderD128', LP_NUM_THREADS='2')
    sway = hypr = None
    parent_log, hypr_log = tempfile.TemporaryFile(), tempfile.TemporaryFile()
    phase = 'parent-start'
    feedback = {}
    try:
        sway = subprocess.Popen(['/lab/runtime/sway', '-c', str(config)], env=env,
                                stdin=subprocess.DEVNULL, stdout=parent_log, stderr=subprocess.STDOUT)
        parent = wait_socket(sway, parent_dir, 5)
        phase = 'parent-feedback'
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection, tempfile.TemporaryFile() as out:
            connection.settimeout(2)
            connection.connect(str(parent))
            connection.settimeout(None)
            control = subprocess.run(['/lab/probe', '--feedback-fd=' + str(connection.fileno())],
                                     pass_fds=(connection.fileno(),), stdin=subprocess.DEVNULL,
                                     stdout=out, stderr=subprocess.DEVNULL, timeout=4)
            out.seek(0)
            raw = out.read(4097)
        if len(raw) > 4096:
            raise base.Refused('client-output-bound')
        feedback = json.loads(raw)
        if not feedback_verified(feedback, control.returncode):
            raise base.Refused('parent-feedback-not-verified')
        phase = 'hyprland-start'
        hypr_config = Path('/tmp/hypr.conf')
        hypr_config.write_text(previous.CONFIG)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(2)
            connection.connect(str(parent))
            connection.settimeout(None)
            env = dict(os.environ, XDG_RUNTIME_DIR=str(child_dir), WAYLAND_SOCKET=str(connection.fileno()), LP_NUM_THREADS='2')
            hypr = subprocess.Popen(['/usr/bin/Hyprland', '--config', str(hypr_config)], env=env,
                                    pass_fds=(connection.fileno(),), stdin=subprocess.DEVNULL,
                                    stdout=hypr_log, stderr=subprocess.STDOUT)
        private = wait_socket(hypr, child_dir, 7)
        phase = 'mapping-client'
        Path('/run/lab/wayland-lab').symlink_to(private)
        with tempfile.TemporaryFile() as out:
            result = subprocess.run(['/lab/probe', '--mapping'], stdin=subprocess.DEVNULL,
                                    stdout=out, stderr=subprocess.DEVNULL, timeout=8)
            out.seek(0)
            raw = out.read(4097)
        if len(raw) > 4096:
            raise base.Refused('client-output-bound')
        report = verified_report(json.loads(raw), result.returncode)
        if sway.poll() is not None or hypr.poll() is not None:
            raise base.Refused('compositor-exited')
        return dict(report, privateParent=True, actualHyprland=True, probeExit=result.returncode,
                    parentFeedbackVerified=True)
    except Exception as exc:
        report = dict(schema=1, ok=False, phase=phase,
                      reason=str(exc) if isinstance(exc, base.Refused) else type(exc).__name__)
        if type(feedback) is dict:
            for key in ('feedbackDone', 'deviceResolved', 'renderNodeMatched'):
                if type(feedback.get(key)) is bool:
                    report[key] = feedback[key]
        for key, server in (('parentExit', sway), ('hyprlandExit', hypr)):
            if server is not None:
                report[key] = server.poll()
        for key, log in (('parentStartup', parent_log), ('hyprlandStartup', hypr_log)):
            log.seek(0, os.SEEK_END)
            log.seek(max(0, log.tell() - 32768))
            report[key] = previous.classify_startup(log.read(32768))
        report['startupFlags'] = startup_flags(hypr_log)
        return failure_report(report, 1)
    finally:
        stop(hypr)
        stop(sway)
        parent_log.close()
        hypr_log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--run-approved', action='store_true')
    modes.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        report = inside() if args.inside else run() if args.run_approved else plan()
        print(json.dumps(report, sort_keys=True))
        return 1 if report.get('ok') is False else 0
    except Exception as exc:
        print(json.dumps(dict(schema=1, ok=False, reason=str(exc) if isinstance(exc, base.Refused) else type(exc).__name__)))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
