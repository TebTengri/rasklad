#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded compositor-only probe. Default inert; explicit owner consent required."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
INSIDE = Path(__file__) == Path('/lab/probe.py')
CONFIG = '''monitor = , 1000x800@30, auto, 1
debug {
 disable_logs = false
 enable_stdout_logs = true
}
xwayland {
 enabled = false
}
animations {
 enabled = false
}
misc {
 disable_hyprland_logo = true
 disable_splash_rendering = true
}
'''


def endpoint(env):
    runtime = Path(env.get('XDG_RUNTIME_DIR', ''))
    name = env.get('WAYLAND_DISPLAY', '')
    if not runtime.is_absolute() or not re.fullmatch(r'wayland-[0-9]+', name):
        raise ValueError('parent_endpoint_shape')
    if env.get('WAYLAND_SOCKET'):
        raise ValueError('inherited_socket_not_admitted')
    for ancestor in (runtime, *runtime.parents):
        if ancestor.is_symlink() or not ancestor.is_dir():
            raise ValueError('parent_runtime_path')
    metadata = runtime.stat()
    if metadata.st_mode & 0o077:
        raise ValueError('parent_runtime_permissions')
    for path in (runtime, runtime / name):
        if path.is_symlink() or path.stat().st_uid != os.getuid():
            raise ValueError('parent_endpoint_owner')
    if not stat.S_ISSOCK((runtime / name).stat().st_mode):
        raise ValueError('parent_not_socket')
    return runtime / name


def sandbox_args(directory, parent_fd, metadata_plan=None):
    a = ['/usr/bin/bwrap', '--unshare-all', '--die-with-parent', '--new-session',
         '--cap-drop', 'ALL', '--clearenv', '--ro-bind', '/usr', '/usr',
         '--symlink', 'usr/bin', '/bin', '--symlink', 'usr/lib', '/lib',
         '--symlink', 'usr/lib', '/lib64', '--ro-bind-try', '/etc/ld.so.cache', '/etc/ld.so.cache',
         '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp', '--tmpfs', '/run',
         '--tmpfs', '/home', '--dir', '/home/lab', '--dir', '/run/lab',
         '--dir', '/dev/dri', '--dev-bind', '/dev/dri/renderD128', '/dev/dri/renderD128',
         '--ro-bind', str(ROOT / 'tools/hyprland_nested_probe.py'), '/lab/probe.py',
         '--ro-bind', str(directory / 'hypr.conf'), '/lab/hypr.conf',
         '--chdir', '/home/lab']
    if metadata_plan is not None:
        primary = metadata_plan['statOnlyPrimary']
        snapshot = Path(metadata_plan['sysfs'])
        if (not re.fullmatch(r'/dev/dri/card[0-9]+', primary)
                or snapshot != directory / 'gpu-metadata/sys'
                or metadata_plan.get('hostSysfsBind') is not False
                or metadata_plan.get('primaryBindMustBeNodevReadOnly') is not True):
            raise ValueError('metadata_mount_plan')
        a += ['--ro-bind', str(snapshot), '/sys', '--ro-bind', primary, primary,
              '--setenv', 'RASKLAD_STAT_ONLY_PRIMARY', primary]
    for name in ('passwd', 'group', 'machine-id'):
        a += ['--ro-bind', str(directory / name), '/etc/' + name]
    env = {'PATH': '/usr/bin', 'HOME': '/home/lab', 'LANG': 'C.UTF-8',
           'XDG_RUNTIME_DIR': '/run/lab', 'XDG_CONFIG_HOME': '/home/lab/.config',
           'XDG_CACHE_HOME': '/home/lab/.cache', 'XDG_DATA_HOME': '/home/lab/.local/share',
           'XDG_SESSION_TYPE': 'wayland', 'XDG_CURRENT_DESKTOP': 'Hyprland',
           'HYPRLAND_NO_RT': '1', 'HYPRLAND_NO_CRASHREPORTER': '1',
           'AQ_DRM_DEVICES': '/dev/dri/renderD128',
           'RASKLAD_PARENT_FD': str(parent_fd), 'RASKLAD_NESTED_PROBE': '1'}
    for key, value in env.items():
        a += ['--setenv', key, value]
    return a + ['--', '/usr/bin/python3', '-I', '/lab/probe.py', '--inside']


def limits():
    os.nice(10)
    for kind, value in [(resource.RLIMIT_CORE, 0), (resource.RLIMIT_CPU, 15),
                        (resource.RLIMIT_FSIZE, 4 << 20), (resource.RLIMIT_NOFILE, 256)]:
        resource.setrlimit(kind, (value, value))


def classify_startup(payload):
    for marker, category in (
        (b'drmGetDeviceFromDevId failed', 'drm_device_discovery_failed'),
        (b'wl_display_connect failed', 'parent_wayland_connect_failed'),
        (b'Missing protocols', 'parent_protocols_missing'),
        (b'zwp_linux_dmabuf_v1: Failed to open node', 'dmabuf_node_open_failed'),
        (b'reopenDRMNode failed', 'allocator_node_reopen_failed'),
        (b'no allocator available', 'gpu_allocator_unavailable'),
        (b'Failed to initialize EGL', 'egl_initialization_reported'),
        (b'eglInitialize', 'egl_initialization_reported'),
        (b'CBackend::create() failed', 'backend_creation_failed'),
    ):
        if marker in payload:
            return category
    return 'unclassified_startup_failure'


def require_stat_only_mounts(mountinfo, nodes):
    """A metadata-only card must be a distinct RO+NODEV mount, never a device grant."""
    rows = [line.split() for line in mountinfo.splitlines()]
    for node in nodes:
        if not re.fullmatch(r'/dev/dri/card[0-9]+', node):
            raise ValueError('metadata_node_shape')
        entries = [row for row in rows if len(row) > 6 and row[4] == node]
        if len(entries) != 1 or not {'ro', 'nodev'}.issubset(entries[0][5].split(',')):
            raise ValueError('metadata_node_not_stat_only')


def startup_reason():
    # Only this namespace's bounded logs; return enums, never raw display/input data.
    payload = b''
    paths = [Path('/tmp/probe-startup.log'), Path('/home/lab/.cache/hyprland/hyprlandCrashReport.txt')]
    paths += list(Path('/run/lab').glob('hypr/*/hyprland.log'))[:4]
    for path in paths:
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as source:
                source.seek(max(0, path.stat().st_size - 32768))
                payload += source.read(32768)
    return classify_startup(payload)


def inside():
    if not INSIDE or os.environ.get('RASKLAD_NESTED_PROBE') != '1' or os.environ.get('HOME') != '/home/lab':
        raise ValueError('private_probe_required')
    fd = int(os.environ.pop('RASKLAD_PARENT_FD'))
    if fd < 3:
        raise ValueError('parent_fd_shape')
    # The parent display is never mounted. Only this compositor gets its FD.
    env = dict(os.environ, WAYLAND_SOCKET=str(fd))
    primary = env.pop('RASKLAD_STAT_ONLY_PRIMARY', None)
    if primary:
        try:
            require_stat_only_mounts(Path('/proc/self/mountinfo').read_text(), [primary])
        except BaseException:
            os.close(fd)
            raise
    os.chmod('/run/lab', 0o700)
    child = None
    try:
        with open('/tmp/probe-startup.log', 'wb') as log:
            child = subprocess.Popen(['/usr/bin/Hyprland', '--config', '/lab/hypr.conf'],
                                     env=env, pass_fds=(fd,), stdin=subprocess.DEVNULL,
                                     stdout=log, stderr=subprocess.STDOUT)
    finally:
        os.close(fd)
    try:
        deadline = time.monotonic() + 7
        while time.monotonic() < deadline:
            if child.poll() is not None:
                return {'passed': False, 'phase': 'compositor_start', 'exitCode': child.returncode,
                        'startupReason': startup_reason()}
            if any(p.is_socket() for p in Path('/run/lab').glob('wayland-*')):
                time.sleep(.25)
                return {'passed': child.poll() is None, 'phase': 'private_socket_ready'}
            time.sleep(.05)
        return {'passed': False, 'phase': 'compositor_timeout'}
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=1)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=1)


def run(with_metadata=False):
    if os.geteuid() == 0:
        raise ValueError('root_not_admitted')
    spec = importlib.util.spec_from_file_location('lab_base', ROOT / 'tools/fcitx_lab_runner.py')
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    base.regular_file(Path('/usr/bin/bwrap'), True)
    base.regular_file(Path('/usr/bin/Hyprland'), True)
    metadata = Path('/usr/bin/bwrap').stat()
    if metadata.st_uid != 0 or metadata.st_mode & 0o6022:
        raise ValueError('unsafe_bwrap')
    gpu = Path('/dev/dri/renderD128')
    if gpu.is_symlink() or not stat.S_ISCHR(gpu.stat().st_mode) or not os.access(gpu, os.R_OK | os.W_OK):
        raise ValueError('render_node_not_admitted')
    address = endpoint(os.environ)
    directory = Path(tempfile.mkdtemp(prefix='hypr-probe-', dir=base.ensure_private_base(ROOT)))
    child = None
    try:
        metadata_plan = None
        gpu_metadata = None
        collector = None
        if with_metadata:
            module_path = ROOT / 'tools/drm_metadata_snapshot.py'
            base.regular_file(module_path)
            spec = importlib.util.spec_from_file_location('drm_metadata', module_path)
            collector = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(collector)
            gpu_metadata = collector.collect_metadata()
            metadata_plan = collector.write_snapshot(gpu_metadata, directory / 'gpu-metadata')
        (directory / 'hypr.conf').write_text(CONFIG)
        (directory / 'passwd').write_text(f'lab:x:{os.getuid()}:{os.getgid()}:Lab:/home/lab:/usr/bin/nologin\n')
        (directory / 'group').write_text(f'lab:x:{os.getgid()}:\n')
        (directory / 'machine-id').write_text('00000000000000000000000000000005\n')
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as parent:
            parent.settimeout(2)
            parent.connect(str(address))
            _, peer_uid, _ = struct.unpack('3i', parent.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize('3i')))
            if peer_uid != os.getuid():
                raise ValueError('parent_peer_owner')
            parent.settimeout(None)
            with (directory / 'result').open('w+b') as output:
                if gpu_metadata is not None:
                    for node in gpu_metadata['nodes']:
                        path = Path('/dev/dri') / node['name']
                        current = path.stat()
                        if path.is_symlink() or not stat.S_ISCHR(current.st_mode) or current.st_rdev != os.makedev(node['major'], node['minor']):
                            raise ValueError('changed_gpu_identity')
                child = subprocess.Popen(sandbox_args(directory, parent.fileno(), metadata_plan), pass_fds=(parent.fileno(),),
                                         env={'PATH': '/usr/bin'}, stdin=subprocess.DEVNULL,
                                         stdout=output, stderr=subprocess.DEVNULL,
                                         start_new_session=True, preexec_fn=limits)
                parent.close()
                child.wait(timeout=12)
                output.seek(0)
                raw = output.read(4097)
                if len(raw) > 4096:
                    raise ValueError('output_bound')
                result = json.loads(raw)
                result.update(schema=1, parentDisplayConnection=True, renderNodeGranted=True,
                              hostConfigurationWritten=False, slackConnected=False,
                              typingTested=False, maxProbeSeconds=16)
                result.update(gpuMetadataSnapshotUsed=with_metadata, hostSysfsBound=False,
                              primaryStatOnly=with_metadata)
                result['passed'] = child.returncode == 0 and result.get('passed') is True
                return result
    finally:
        if child and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=1)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=1)
        shutil.rmtree(directory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--run-approved', action='store_true')
    modes.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--gpu-metadata', action='store_true', help='use bounded reconstructed PCI metadata; no host /sys mount')
    args = parser.parse_args()
    try:
        if args.inside and args.gpu_metadata:
            raise ValueError('outer_metadata_option_only')
        result = inside() if args.inside else run(args.gpu_metadata) if args.run_approved else {
            'schema': 1, 'status': 'plan_only', 'hostConnectionOpened': False,
            'scope': 'one nested Hyprland window; one render node; no Slack/IME/input tests',
            'requiresOwnerApproval': True, 'gpuMetadataSnapshotPlanned': args.gpu_metadata,
            'hostSysfsBound': False, 'maxProbeSeconds': 16}
        print(json.dumps(result, sort_keys=True))
        return 0 if result.get('passed', True) else 1
    except (OSError, ValueError, KeyError, subprocess.SubprocessError):
        print(json.dumps({'schema': 1, 'passed': False, 'reason': 'probe_refused_or_failed'}))
        return 2


if __name__ == '__main__':
    sys.exit(main())
