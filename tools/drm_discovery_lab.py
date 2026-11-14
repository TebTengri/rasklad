#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Synthetic libdrm discovery proof: no real GPU, host sysfs or display. Default inert."""
import argparse
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import resource
import signal
import stat
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
INSIDE = Path(__file__) == Path('/lab/discovery.py')
SYNTHETIC_DEVICE = os.makedev(1, 3)  # /dev/null identity, never opened by libdrm discovery.


def create_sysfs(root):
    """Our own tiny PCI fixture, deliberately unrelated to this machine's hardware."""
    device = root / 'dev/char/1:3/device'
    (device / 'drm/renderD128').mkdir(parents=True)
    (root / 'bus/pci').mkdir(parents=True)
    (device / 'subsystem').symlink_to('/sys/bus/pci')
    for name, value in {
        'uevent': 'PCI_SLOT_NAME=0000:00:02.0\n',
        'vendor': '0x1234\n', 'device': '0x5678\n',
        'subsystem_vendor': '0x1234\n', 'subsystem_device': '0x5678\n',
    }.items():
        (device / name).write_text(value)


def sandbox_args(directory, with_sysfs=True):
    argv = ['/usr/bin/bwrap', '--unshare-all', '--die-with-parent', '--new-session',
            '--cap-drop', 'ALL', '--clearenv', '--ro-bind', '/usr', '/usr',
            '--symlink', 'usr/lib', '/lib', '--symlink', 'usr/lib', '/lib64',
            '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp', '--tmpfs', '/run',
            '--tmpfs', '/home', '--dir', '/dev/dri',
            # A benign existing char-node identity, mounted nodev (not --dev-bind).
            # No /dev/dri source is mounted. Discovery calls stat, never opens it.
            '--ro-bind', '/dev/null', '/dev/dri/renderD128',
            '--ro-bind', str(ROOT / 'tools/drm_discovery_lab.py'), '/lab/discovery.py',
            '--setenv', 'PATH', '/usr/bin', '--setenv', 'HOME', '/home/lab',
            '--setenv', 'RASKLAD_SYNTHETIC_DRM', '1']
    if with_sysfs:
        argv += ['--ro-bind', str(directory / 'sys'), '/sys']
    return argv + ['--', '/usr/bin/python3', '-I', '/lab/discovery.py', '--inside']


def limits():
    for kind, value in [(resource.RLIMIT_CORE, 0), (resource.RLIMIT_CPU, 2),
                        (resource.RLIMIT_FSIZE, 1 << 20), (resource.RLIMIT_NOFILE, 64)]:
        resource.setrlimit(kind, (value, value))


def inside():
    if not INSIDE or os.environ.get('RASKLAD_SYNTHETIC_DRM') != '1':
        raise ValueError('private_synthetic_lab_required')
    if any(os.environ.get(key) for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'DBUS_SESSION_BUS_ADDRESS')):
        raise ValueError('unexpected_host_connection')
    node = Path('/dev/dri/renderD128').stat()
    if not stat.S_ISCHR(node.st_mode) or node.st_rdev != SYNTHETIC_DEVICE:
        raise ValueError('synthetic_node_identity')
    lib = ctypes.CDLL('libdrm.so.2')
    lib.drmGetDeviceFromDevId.argtypes = [ctypes.c_ulong, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
    lib.drmGetDeviceFromDevId.restype = ctypes.c_int
    lib.drmFreeDevice.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    device = ctypes.c_void_p()
    result = lib.drmGetDeviceFromDevId(SYNTHETIC_DEVICE, 0, ctypes.byref(device))
    if result == 0:
        lib.drmFreeDevice(ctypes.byref(device))
    return {'returnCode': result, 'realGpu': False, 'hostSysfs': False, 'displayConnected': False}


def run():
    if os.geteuid() == 0:
        raise ValueError('root_not_admitted')
    spec = importlib.util.spec_from_file_location('base', ROOT / 'tools/fcitx_lab_runner.py')
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    base.regular_file(Path('/usr/bin/bwrap'), True)
    meta = Path('/usr/bin/bwrap').stat()
    if meta.st_uid != 0 or meta.st_mode & 0o6022:
        raise ValueError('unsafe_bwrap')
    reports = []
    with tempfile.TemporaryDirectory(prefix='drm-discovery-', dir=base.ensure_private_base(ROOT)) as name:
        directory = Path(name)
        create_sysfs(directory / 'sys')
        for label, with_sysfs, expected in [('absent_sysfs', False, -22), ('synthetic_sysfs', True, 0), ('missing_vendor', True, -19)]:
            if label == 'missing_vendor':
                (directory / 'sys/dev/char/1:3/device/vendor').unlink()
            child = None
            try:
                child = subprocess.Popen(sandbox_args(directory, with_sysfs), stdin=subprocess.DEVNULL,
                                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                         env={'PATH': '/usr/bin'}, start_new_session=True, preexec_fn=limits)
                output, _ = child.communicate(timeout=5)
                if child.returncode or len(output) > 4096:
                    raise ValueError('sandbox_failed')
                report = json.loads(output)
                reports.append({'case': label, 'returnCode': report['returnCode'], 'expected': expected})
            finally:
                if child and child.poll() is None:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait(timeout=1)
    return {'schema': 1, 'passed': all(row['returnCode'] == row['expected'] for row in reports),
            'cases': reports, 'realGpu': False, 'hostSysfs': False, 'displayConnected': False,
            'limitation': 'proves metadata discovery only; no allocator, driver or compositor startup proof'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--run', action='store_true')
    modes.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        result = inside() if args.inside else run() if args.run else {
            'schema': 1, 'status': 'plan_only', 'realGpu': False, 'hostSysfs': False,
            'displayConnected': False, 'syntheticDevice': '/dev/null identity via nodev bind; stat only'}
        print(json.dumps(result, sort_keys=True))
        return 0 if result.get('passed', True) else 1
    except (OSError, ValueError, KeyError, subprocess.SubprocessError):
        print(json.dumps({'schema': 1, 'passed': False, 'reason': 'synthetic_discovery_refused_or_failed'}))
        return 2


if __name__ == '__main__':
    sys.exit(main())
