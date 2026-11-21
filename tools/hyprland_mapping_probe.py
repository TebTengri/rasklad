#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Route B: own nested Hyprland on ONE host-parent Wayland FD runs the sealed
native-windows --mapping client on its PRIVATE socket. Default is an inert plan.

Reuses the known-working nested recipe (hyprland_nested_probe CONFIG/mount guard,
drm_metadata_snapshot, renderD128 only) and the closed mapping scope/report
helpers. No host window enumeration, apps, IME, profile, bus, network, config or
keyboard injection. The nested window may take host focus: owner must not type.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import stat
import struct
import subprocess
import tempfile
import uuid

INSIDE = Path(__file__) == Path('/lab/hyprland_mapping_probe.py')
ROOT = Path('/lab') if INSIDE else Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Inside, closed_hyprland_mapping is bound at /lab/mapping.py so its own ROOT
# resolves to /lab; only its pure helpers are used, never its Sway inside().
closed = load('direct_closed', ROOT / ('mapping.py' if INSIDE else 'tools/closed_hyprland_mapping.py'))
nested = load('direct_nested', ROOT / ('hyprland_nested_probe.py' if INSIDE else 'tools/hyprland_nested_probe.py'))
base = closed.base

CLIENT_SECONDS = 7          # native client also arms alarm(6)
COMPOSITOR_SECONDS = 7
WRAPPER_SECONDS = 30        # scope RuntimeMaxSec=27 + cleanup => ~40s total
EXTRA_REASONS = ('not-private-lab', 'parent-fd-shape')
STAGE_BOOLS = ('identifiersValid', 'crossObserverMatch', 'initialMatch', 'closedObservedBoth',
               'remapMatch', 'retiredIdNotReused', 'observerCleanup', 'mappingVerified')


def plan():
    return dict(schema=1, mode='plan', route='B-host-parent-nested', requiresExplicitApproval=True,
                hostConnectionOpened=False, proposedHostWaylandConnections=1, peerMustBeInstalledHyprland=True,
                gpu='/dev/dri/renderD128', hostSysfs=False, primaryStatOnly=True, network=False,
                hostWindowEnumeration=False, keyboardInjection=False, hostFocusActions=False,
                nestedWindowMayTakeFocus=True, physicalInputMayReachNested=True, ownerMustNotType=True, ownSyntheticWindows=2, observers=2,
                maxWrapperSeconds=40, nativeClientSeconds=6, automatedRetries=0,
                hostConfigurationChanged=False, fieldBindingVerified=False, userWindowAuthority=False)


def client_stage(report):
    """Bounded aggregate of the native client; strings and unknown keys dropped."""
    stage = {}
    if type(report) is not dict:
        return stage
    for key in STAGE_BOOLS:
        if type(report.get(key)) is bool:
            stage[key] = report[key]
    if type(report.get('appWindowsMapped')) is int and 0 <= report['appWindowsMapped'] <= 2:
        stage['appWindowsMapped'] = report['appWindowsMapped']
    if type(report.get('iterations')) is int and 0 <= report['iterations'] <= 10000:
        stage['iterations'] = report['iterations']
    for key in ('observerOwnedCounts', 'observerClosedCounts'):
        value = report.get(key)
        if (type(value) is list and len(value) == 2 and
                all(type(v) is int and 0 <= v <= 16 for v in value)):
            stage[key] = list(value)
    return stage


def failure(report, exit_code):
    result = closed.failure_report(report, exit_code)
    if type(report) is dict:
        if report.get('reason') in EXTRA_REASONS:
            result['reason'] = report['reason']
        stage = client_stage(report.get('clientStage'))
        if stage:
            result['clientStage'] = stage
    return result


def final_report(raw, exit_code):
    """Outer decision. Success ONLY from a verified client report with exit 0;
    socket readiness or any other shape is a failure."""
    if not raw:
        return dict(schema=1, ok=False, reason='no-report', scopeLimitsVerified=False,
                    sandboxExit=exit_code if type(exit_code) is int and -128 <= exit_code <= 255 else None)
    if len(raw) > 8192:
        raise base.Refused('output-bound')
    report = json.loads(raw)
    try:
        closed.verified_report(report, exit_code)
        if report.get('actualHyprland') is not True or report.get('hostParent') is not True:
            raise base.Refused('mapping-not-verified')
    except base.Refused:
        return dict(failure(report, exit_code), scopeLimitsVerified=True)
    return dict(client_stage(report), schema=1, ok=True, mappingMode=True, mappingVerified=True,
                observerCleanup=True, actualHyprland=True, hostParent=True, probeExit=0,
                scopeLimitsVerified=True)


def sandbox_args(fds, paths, metadata, parent_fd):
    """Closed mapping sandbox, but own inside() and one inherited host-parent FD."""
    if type(parent_fd) is not int or not 3 <= parent_fd <= 1023:
        raise base.Refused('parent-fd-shape')
    argv = closed.sandbox_args(fds, paths, metadata)
    at = len(argv) - 1 - argv[::-1].index('--')
    head = argv[:at]
    for i in range(len(head) - 2):
        if head[i:i + 3] == ['--setenv', 'RASKLAD_CLOSED_MAPPING', '1']:
            del head[i:i + 3]
            break
    else:
        raise base.Refused('sandbox-shape')
    for key, value in (('RASKLAD_DIRECT_MAPPING', '1'), ('RASKLAD_HOST_PARENT_FD', str(parent_fd)),
                       ('XDG_SESSION_TYPE', 'wayland'), ('XDG_CURRENT_DESKTOP', 'Hyprland')):
        head += ['--setenv', key, value]
    return head + ['--', '/usr/bin/python3', '-I', '/lab/hyprland_mapping_probe.py', '--inside']


def host_parent():
    """Existing endpoint ownership + kernel peer uid + installed-Hyprland exe checks."""
    try:
        address = nested.endpoint(os.environ)
    except ValueError:
        raise base.Refused('host-endpoint') from None
    registry = load('direct_registry', ROOT / 'tools/host_registry_probe.py')
    expected = Path('/usr/bin/Hyprland').resolve(strict=True)
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        connection.settimeout(2)
        connection.connect(str(address))
        pid, uid, _ = struct.unpack('3i', connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED,
                                                                 struct.calcsize('3i')))
        exe = Path(f'/proc/{pid}/exe').resolve(strict=True)
        if not registry.peer_is_hyprland(pid, uid, exe, expected, os.getuid()):
            raise base.Refused('wrong-compositor-peer')
        connection.settimeout(None)
        return connection, registry
    except BaseException:
        connection.close()
        raise


def run():
    if os.geteuid() == 0:
        raise base.Refused('root-not-admitted')
    for path in ('/usr/bin/bwrap', '/usr/bin/Hyprland', '/usr/bin/systemd-run', '/usr/bin/systemctl'):
        base.regular_file(Path(path), True)
        info = Path(path).stat()
        if info.st_uid != 0 or info.st_mode & 0o6022:
            raise base.Refused('unsafe-system-binary')
    render = Path('/dev/dri/renderD128')
    if render.is_symlink() or not stat.S_ISCHR(render.stat().st_mode) or not os.access(render, os.R_OK | os.W_OK):
        raise base.Refused('render-node-not-admitted')
    collector = load('direct_metadata', ROOT / 'tools/drm_metadata_snapshot.py')
    runtime = Path(os.environ.get('XDG_RUNTIME_DIR', ''))
    if (not runtime.is_absolute() or runtime.is_symlink() or not runtime.is_dir() or
            runtime.stat().st_uid != os.getuid() or runtime.stat().st_mode & 0o077):
        raise base.Refused('scope-runtime')
    launcher_env = {'PATH': '/usr/bin', 'XDG_RUNTIME_DIR': str(runtime)}
    connection, registry = host_parent()
    fds = {}
    directory = None
    try:
        # Manifest/source/XML validation, sealed snapshot and RPATH refusal.
        fds['probe'], digest = registry.validated_binary()
        directory = Path(tempfile.mkdtemp(prefix='direct-mapping-', dir=base.ensure_private_base(ROOT)))
        gpu = collector.collect_metadata()
        metadata = collector.write_snapshot(gpu, directory / 'gpu-metadata')
        paths = [(Path(__file__).resolve(), '/lab/hyprland_mapping_probe.py'),
                 (ROOT / 'tools/closed_hyprland_mapping.py', '/lab/mapping.py'),
                 (ROOT / 'tools/fcitx_lab_runner.py', '/lab/fcitx_lab_runner.py'),
                 (ROOT / 'tools/hyprland_nested_probe.py', '/lab/hyprland_nested_probe.py')]
        for source, _ in paths:
            base.regular_file(source)
        for filename, content in (('passwd', f'lab:x:{os.getuid()}:{os.getgid()}:Lab:/home/lab:/usr/bin/nologin\n'),
                                  ('group', f'lab:x:{os.getgid()}:\n'),
                                  ('machine-id', '00000000000000000000000000000007\n')):
            (directory / filename).write_text(content)
            paths.append((directory / filename, '/etc/' + filename))
        for node in gpu['nodes']:
            info = (Path('/dev/dri') / node['name']).lstat()
            if not stat.S_ISCHR(info.st_mode) or info.st_rdev != os.makedev(node['major'], node['minor']):
                raise base.Refused('changed-gpu-identity')
        unit = 'rasklad-mapping-' + uuid.uuid4().hex + '.scope'
        argv = closed.scope_command(sandbox_args(fds, paths, metadata, connection.fileno()), unit)
        try:
            with tempfile.TemporaryFile() as out:
                child = subprocess.Popen(argv, pass_fds=(fds['probe'], connection.fileno()), env=launcher_env,
                                         stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.DEVNULL,
                                         preexec_fn=closed.limits)
                connection.close()  # host connection now lives only in the nested compositor
                try:
                    code = child.wait(timeout=WRAPPER_SECONDS)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=2)
                    raise base.Refused('wrapper-timeout') from None
                out.seek(0)
                raw = out.read(8193)
        finally:
            closed.cleanup_scope(unit, launcher_env)  # own unique scope only; raises unless inactive
        report = final_report(raw, code)
        report.update(schema=1, hostWaylandConnections=1, peerExeMatched=True, network=False, physicalInputMayReachNested=True, ownerMustNotType=True,
                      keyboardInjection=False, hostWindowEnumeration=False, hostConfigurationChanged=False,
                      fieldBindingVerified=False, userWindowAuthority=False, binarySha256=digest,
                      scopeInactiveVerified=True)
        return report
    finally:
        connection.close()
        for fd in fds.values():
            os.close(fd)
        if directory is not None:
            shutil.rmtree(directory)


def inside():
    if (not INSIDE or os.environ.get('RASKLAD_DIRECT_MAPPING') != '1' or
            os.environ.get('HOME') != '/home/lab' or os.environ.get('XDG_RUNTIME_DIR') != '/run/lab' or
            any(os.environ.get(k) for k in ('WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'))):
        raise base.Refused('not-private-lab')
    raw_fd = os.environ.pop('RASKLAD_HOST_PARENT_FD', '')
    fd = int(raw_fd) if raw_fd.isdigit() else -1
    try:
        if not 3 <= fd <= 1023 or not stat.S_ISSOCK(os.fstat(fd).st_mode):
            raise base.Refused('parent-fd-shape')
        nested.require_stat_only_mounts(Path('/proc/self/mountinfo').read_text(),
                                        [os.environ['RASKLAD_STAT_ONLY_PRIMARY']])
    except BaseException:
        if fd >= 3:
            try:
                os.close(fd)
            except OSError:
                pass
        raise
    child_dir = Path('/run/lab/child')
    for path in (Path('/run/lab'), child_dir):
        path.mkdir(mode=0o700, exist_ok=True)
        path.chmod(0o700)
    hypr = None
    hypr_log = tempfile.TemporaryFile()
    phase, stage = 'hyprland-start', None
    try:
        config = Path('/tmp/hypr.conf')
        config.write_text(nested.CONFIG)
        env = dict(os.environ, XDG_RUNTIME_DIR=str(child_dir), WAYLAND_SOCKET=str(fd), LP_NUM_THREADS='2')
        env.pop('RASKLAD_STAT_ONLY_PRIMARY', None)
        try:
            hypr = subprocess.Popen(['/usr/bin/Hyprland', '--config', str(config)], env=env, pass_fds=(fd,),
                                    stdin=subprocess.DEVNULL, stdout=hypr_log, stderr=subprocess.STDOUT)
        finally:
            os.close(fd)
        private = closed.wait_socket(hypr, child_dir, COMPOSITOR_SECONDS)
        phase = 'mapping-client'
        Path('/run/lab/wayland-lab').symlink_to(private)
        with tempfile.TemporaryFile() as out:
            result = subprocess.run(['/lab/probe', '--mapping'], stdin=subprocess.DEVNULL,
                                    stdout=out, stderr=subprocess.DEVNULL, timeout=CLIENT_SECONDS)
            out.seek(0)
            raw = out.read(4097)
        if len(raw) > 4096:
            raise base.Refused('client-output-bound')
        client = json.loads(raw)
        stage = client_stage(client)
        closed.verified_report(client, result.returncode)
        if hypr.poll() is not None:
            raise base.Refused('compositor-exited')
        return dict(stage, schema=1, ok=True, mappingMode=True, mappingVerified=True, observerCleanup=True,
                    actualHyprland=True, hostParent=True, probeExit=result.returncode)
    except Exception as exc:
        report = dict(schema=1, ok=False, phase=phase,
                      reason=str(exc) if isinstance(exc, base.Refused) else type(exc).__name__)
        if stage:
            report['clientStage'] = stage
        if hypr is not None:
            report['hyprlandExit'] = hypr.poll()
        hypr_log.seek(0, os.SEEK_END)
        hypr_log.seek(max(0, hypr_log.tell() - 32768))
        report['hyprlandStartup'] = nested.classify_startup(hypr_log.read(32768))
        report['startupFlags'] = closed.startup_flags(hypr_log)
        return failure(report, 1)
    finally:
        closed.stop(hypr)
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
        print(json.dumps(dict(schema=1, ok=False,
                              reason=str(exc) if isinstance(exc, base.Refused) else type(exc).__name__)))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
