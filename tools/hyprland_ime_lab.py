#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepared exact-Hyprland native IME trial. Default inert; live consent required."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import selectors
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
INSIDE = Path(__file__) == Path('/lab/runner.py')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


base = load('ime_base', Path('/lab/fcitx_lab_runner.py') if INSIDE else ROOT/'tools/fcitx_lab_runner.py')
probe = load('hypr_probe', Path('/lab/probe.py') if INSIDE else ROOT/'tools/hyprland_nested_probe.py')
chrome = None if INSIDE else load('chrome_lab', ROOT/'tools/chromium_ime_lab.py')


def sandbox_args(directory, parent_fd, metadata_plan, paths, profile, snapshots, focus_stress=False, space_telemetry=False, addon_witness=False):
    if addon_witness and not (focus_stress and space_telemetry):
        raise base.Refused('addon_witness_requires_focus_and_space_telemetry')
    if addon_witness:
        paths = dict(paths)
        paths['addon'] = ROOT/'build/chromium-ime/witness-addon/raskladlab.so'
        paths['addon_conf'] = ROOT/'tests/fcitx_transport/raskladwitness.conf'
        base.regular_file(ROOT/'tests/chromium_ime/addon-witness.mjs')
    args = probe.sandbox_args(directory, parent_fd, metadata_plan)
    at = args.index('--')
    extra = ['--ro-bind', '/etc/fonts', '/etc/fonts', '--dir', '/lab/addons']
    for key, path in paths.items():
        destination = chrome.TARGETS[key]
        if destination in snapshots:
            extra += ['--perms', '0755', '--ro-bind-data', str(snapshots[destination]), destination]
        else:
            extra += ['--ro-bind', str(path), destination]
    for name in ('native_ime_clients.py',):
        extra += ['--ro-bind', str(ROOT/'tools'/name), '/lab/'+name]
    if focus_stress:
        for source, target in [('tests/chromium_ime/adversarial-focus.mjs','adversarial-focus.mjs'),
                               ('tests/chromium_ime/focus-policy.mjs','focus-policy.mjs'),
                               ('tests/chromium_ime/space-telemetry.mjs','space-telemetry.mjs'),
                               ('data/focus-source-review-001.json','focus-source-review.json')]:
            extra += ['--ro-bind', str(ROOT/source), '/lab/'+target]
        extra += ['--setenv', 'RASKLAD_FOCUS_STRESS_LAB', '1']
    if space_telemetry:
        if not focus_stress:
            raise base.Refused('space_telemetry_requires_focus_suite')
        extra += ['--setenv', 'RASKLAD_SPACE_TELEMETRY_LAB', '1']
    if addon_witness:
        extra += ['--ro-bind', str(ROOT/'tests/chromium_ime/addon-witness.mjs'), '/lab/addon-witness.mjs',
                  '--setenv', 'RASKLAD_ADDON_WITNESS_LAB', '1']
    extra += ['--perms', '0755', '--ro-bind-data', str(snapshots['/lab/rasklad-wayland-keys']), '/lab/rasklad-wayland-keys']
    for relative, path in profile.items():
        extra += ['--ro-bind', str(path), '/lab/profile/'+relative]
    env = {'RASKLAD_HYPRLAND_IME_LAB': '1', 'RASKLAD_NATIVE_WAYLAND_LAB': '1',
           'RASKLAD_CHROMIUM_IME_LAB': '1', 'RASKLAD_REVIEWED_PROFILE_LAB': '1',
           'FCITX_ADDON_DIRS': '/lab/addons:/usr/lib/fcitx5', 'XDG_DATA_DIRS': '/usr/share',
           'XDG_SESSION_DESKTOP': 'Hyprland', 'NO_AT_BRIDGE': '1'}
    for key, value in env.items():
        extra += ['--setenv', key, value]
    return args[:at] + extra + ['--', '/usr/bin/python3', '-I', '/lab/runner.py', '--inside']


def start_bus(children):
    # Called after the parent FD has been closed. No host bus, activation dirs or shell.
    if os.environ.get('RASKLAD_PARENT_FD') or os.environ.get('WAYLAND_SOCKET'):
        raise base.Refused('parent_capability_not_released')
    bus = subprocess.Popen(['/usr/bin/dbus-daemon', '--nofork', '--config-file=/lab/session-bus.conf',
                            '--print-address=1'], stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    children.append(bus)
    with selectors.DefaultSelector() as selector:
        selector.register(bus.stdout, selectors.EVENT_READ)
        raw = b''
        deadline = time.monotonic()+2
        while b'\n' not in raw and len(raw) <= 4096:
            if not selector.select(max(0, deadline-time.monotonic())):
                raise base.Refused('private_bus_timeout')
            chunk = os.read(bus.stdout.fileno(), 4097-len(raw))
            if not chunk:
                raise base.Refused('private_bus_exited')
            raw += chunk
    if len(raw) > 4096 or b'\n' not in raw:
        raise base.Refused('private_bus_address_bound')
    address = raw.split(b'\n', 1)[0].decode('ascii')
    if not re.fullmatch(r'unix:path=/run/lab/dbus-[A-Za-z0-9]+(?:,guid=[0-9a-f]{32})?', address):
        raise base.Refused('private_bus_address')
    os.environ['DBUS_SESSION_BUS_ADDRESS'] = address


def inside():
    if (not INSIDE or os.environ.get('RASKLAD_HYPRLAND_IME_LAB') != '1'
            or os.environ.get('HOME') != '/home/lab'
            or os.environ.get('XDG_RUNTIME_DIR') != '/run/lab'
            or os.environ.get('DISPLAY') or os.environ.get('WAYLAND_SOCKET')
            or os.environ.get('DBUS_SESSION_BUS_ADDRESS')):
        raise base.Refused('private_hyprland_ime_required')
    fd = int(os.environ.pop('RASKLAD_PARENT_FD'))
    if fd < 3:
        raise base.Refused('parent_fd_shape')
    children = []
    parent_open = True
    phase = 'hyprland_start'
    try:
        primary = os.environ.pop('RASKLAD_STAT_ONLY_PRIMARY')
        probe.require_stat_only_mounts(Path('/proc/self/mountinfo').read_text(), [primary])
        os.chmod('/run/lab', 0o700)
        with open('/tmp/probe-startup.log', 'wb') as log:
            hyprland = subprocess.Popen(['/usr/bin/Hyprland', '--config', '/lab/hypr.conf'],
                                        env=dict(os.environ, WAYLAND_SOCKET=str(fd)), pass_fds=(fd,),
                                        stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        children.append(hyprland)
        os.close(fd)
        parent_open = False
        deadline = time.monotonic()+6
        while time.monotonic() < deadline:
            if hyprland.poll() is not None:
                return {'schema': 1, 'passed': False, 'phase': phase,
                        'compositorExitCode': hyprland.returncode, 'startupReason': probe.startup_reason()}
            sockets = [p for p in Path('/run/lab').glob('wayland-*') if p.is_socket()]
            if sockets:
                break
            time.sleep(.03)
        else:
            raise base.Refused('private_display_timeout')
        if len(sockets) != 1:
            raise base.Refused('ambiguous_private_display')
        Path('/run/lab/wayland-lab').symlink_to(sockets[0].name)
        os.environ['WAYLAND_DISPLAY'] = 'wayland-lab'
        phase = 'private_bus'
        start_bus(children)
        config = Path('/home/lab/.config/fcitx5')
        config.mkdir(parents=True)
        (config/'profile').write_text(base.PROFILE)
        addons = Path('/home/lab/.local/share/fcitx5/addon')
        addons.mkdir(parents=True)
        (addons/'raskladlab.conf').write_bytes(Path('/lab/addon.conf').read_bytes())
        phase = 'native_ime_clients'
        clients = load('native_clients', Path('/lab/native_ime_clients.py'))
        report = clients.run(base, children)
        report['compositorStayedAlive'] = hyprland.poll() is None
        report['passed'] = report.get('passed') is True and report['compositorStayedAlive']
        return report
    except (base.Refused, ValueError, KeyError, subprocess.SubprocessError) as error:
        reason = str(error) if isinstance(error, base.Refused) else type(error).__name__
        if isinstance(error, ValueError) and re.fullmatch(r'[a-z_]{1,64}', str(error)):
            reason = str(error)
        return {'schema': 1, 'passed': False, 'phase': phase,
                'reason': reason}
    finally:
        if parent_open:
            os.close(fd)
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=1)


def run(input_root, focus_stress=False, space_telemetry=False, addon_witness=False):
    if addon_witness and not (focus_stress and space_telemetry):
        raise base.Refused('addon_witness_requires_focus_and_space_telemetry')
    if os.geteuid() == 0:
        raise base.Refused('root_not_admitted')
    if input_root is None:
        raise base.Refused('explicit_input_root_required')
    if space_telemetry and not focus_stress:
        raise base.Refused('space_telemetry_requires_focus_suite')
    base.regular_file(Path('/usr/bin/bwrap'), True)
    meta = Path('/usr/bin/bwrap').stat()
    if meta.st_uid != 0 or meta.st_mode & 0o6022:
        raise base.Refused('unsafe_bwrap')
    base.regular_file(Path('/usr/bin/Hyprland'), True)
    paths = chrome.files()
    paths.pop('xvfb')
    paths.pop('keys')
    paths['runner'] = ROOT/'tools/hyprland_ime_lab.py'
    paths['addon'] = ROOT/('build/chromium-ime/witness-addon/raskladlab.so' if addon_witness else 'build/chromium-ime/profile-addon/raskladlab.so')
    if addon_witness:
        paths['addon_conf'] = ROOT/'tests/fcitx_transport/raskladwitness.conf'
        base.regular_file(ROOT/'tests/chromium_ime/addon-witness.mjs')
    for path in [*paths.values(), ROOT/'tools/native_ime_clients.py']:
        base.regular_file(path)
    if focus_stress:
        for source in ['tests/chromium_ime/adversarial-focus.mjs','tests/chromium_ime/focus-policy.mjs','tests/chromium_ime/space-telemetry.mjs','data/focus-source-review-001.json']:
            base.regular_file(ROOT/source)
    keys = ROOT/'build/wayland-ime/keys/rasklad-wayland-keys'
    base.regular_file(keys, True)
    profile = {relative: (ROOT if relative.startswith('data/') else Path(input_root).absolute())/relative
               for relative in chrome.PROFILE_PATHS}
    for path in profile.values():
        base.regular_file(path)
    collector = load('metadata_snapshot', ROOT/'tools/drm_metadata_snapshot.py')
    metadata = collector.collect_metadata()
    address = probe.endpoint(os.environ)
    directory = Path(tempfile.mkdtemp(prefix='hyprland-ime-', dir=base.ensure_private_base(ROOT)))
    child = None
    snapshots = {}
    try:
        plan = collector.write_snapshot(metadata, directory/'gpu-metadata')
        snapshots['/lab/addons/raskladlab.so'] = base.sealed_snapshot(paths['addon'])
        snapshots['/lab/rasklad-wayland-keys'] = base.sealed_snapshot(keys)
        (directory/'hypr.conf').write_text(probe.CONFIG)
        (directory/'passwd').write_text(f'lab:x:{os.getuid()}:{os.getgid()}:Lab:/home/lab:/usr/bin/nologin\n')
        (directory/'group').write_text(f'lab:x:{os.getgid()}:\n')
        (directory/'machine-id').write_text('00000000000000000000000000000007\n')
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as parent:
            parent.settimeout(2)
            parent.connect(str(address))
            _, uid, _ = struct.unpack('3i', parent.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize('3i')))
            if uid != os.getuid():
                raise base.Refused('parent_peer_owner')
            parent.settimeout(None)
            with (directory/'result').open('w+b') as output:
                for node in metadata['nodes']:
                    path = Path('/dev/dri')/node['name']
                    current = path.stat()
                    if (path.is_symlink() or not stat.S_ISCHR(current.st_mode)
                            or current.st_rdev != os.makedev(node['major'], node['minor'])):
                        raise base.Refused('changed_gpu_identity')
                if not os.access('/dev/dri/renderD128', os.R_OK | os.W_OK):
                    raise base.Refused('render_node_not_admitted')
                child = subprocess.Popen(sandbox_args(directory, parent.fileno(), plan, paths, profile, snapshots, focus_stress, space_telemetry, addon_witness),
                                         pass_fds=(parent.fileno(), *snapshots.values()), env={'PATH': '/usr/bin'},
                                         stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.DEVNULL,
                                         start_new_session=True, preexec_fn=chrome.limits)
                parent.close()
                child.wait(timeout=68)
                output.seek(0)
                raw = output.read(32769)
                if len(raw) > 32768:
                    raise base.Refused('report_bound')
                result = json.loads(raw)
                result.update(exactCompositor='installed nested Hyprland', parentDisplayConnection=True,
                              primaryStatOnly=True, renderNodeGranted=True, hostSysfsBound=False,
                              hostConfigurationWritten=False, slackConnected=False,
                              readyForLiveUse=False, maxTrialSeconds=75)
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
        for fd in snapshots.values():
            os.close(fd)
        shutil.rmtree(directory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--run-approved', action='store_true')
    modes.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--input-root')
    parser.add_argument('--focus-stress', action='store_true')
    parser.add_argument('--space-telemetry', action='store_true')
    parser.add_argument('--addon-witness', action='store_true')
    args = parser.parse_args()
    try:
        if args.addon_witness and not (args.focus_stress and args.space_telemetry):
            raise base.Refused('addon_witness_requires_focus_and_space_telemetry')
        if args.inside and args.focus_stress:
            raise base.Refused('focus_suite_outer_native_only')
        if args.space_telemetry and not args.focus_stress:
            raise base.Refused('space_telemetry_requires_focus_suite')
        report = inside() if args.inside else run(args.input_root, args.focus_stress, args.space_telemetry, args.addon_witness) if args.run_approved else {
            'schema': 1, 'status': 'plan_only', 'hostDisplayConnected': False,
            'requiresOwnerApproval': True, 'maxTrialSeconds': 75,
            'focusStressPlanned': args.focus_stress,
            'spaceTelemetryPlanned': args.space_telemetry,
            'addonWitnessPlanned': args.addon_witness,
            'scope': 'fixed native Chromium/Fcitx fixture in nested Hyprland; no Slack/host IME/config changes'}
        print(json.dumps(report, sort_keys=True))
        return 0 if report.get('passed', True) else 1
    except (base.Refused, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(json.dumps({'schema': 1, 'passed': False,
                          'reason': str(error) if isinstance(error, base.Refused) else type(error).__name__}))
        return 2


if __name__ == '__main__':
    sys.exit(main())
