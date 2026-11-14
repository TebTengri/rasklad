#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Synthetic Wayland wire parent; never connects a host display or exposes GPU."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('probe', ROOT/'tools/hyprland_nested_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def run(logs_enabled, debug_startup=False, ime_target=False):
    server, client = socket.socketpair()
    counts = {'getRegistry': 0, 'sync': 0, 'other': 0}
    errors = []
    snapshots = {}

    def fake_parent():
        server.settimeout(8)
        pending = b''
        try:
            while True:
                block = server.recv(4096)
                if not block:
                    break
                pending += block
                while len(pending) >= 8:
                    obj, word = struct.unpack_from('II', pending)
                    length, opcode = word >> 16, word & 65535
                    if length < 8 or length > 4096:
                        raise ValueError('packet_bound')
                    if len(pending) < length:
                        break
                    frame, pending = pending[:length], pending[length:]
                    if obj == 1 and opcode == 1 and length == 12:
                        counts['getRegistry'] += 1
                    elif obj == 1 and opcode == 0 and length == 12:
                        counts['sync'] += 1
                        callback = struct.unpack_from('I', frame, 8)[0]
                        server.sendall(struct.pack('III', callback, 12 << 16, 0)
                                       + struct.pack('III', 1, (12 << 16) | 1, callback))
                    else:
                        counts['other'] += 1
        except Exception as exc:
            errors.append(type(exc).__name__)
        finally:
            server.close()

    thread = threading.Thread(target=fake_parent)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='rasklad-fake-wayland-', dir='/tmp') as tmp:
            directory = Path(tmp)
            # Explicit logging toggle makes the control independent of future defaults.
            config = '''monitor = , 1000x800@30, auto, 1
debug {
 enable_stdout_logs = true
 disable_logs = %s
}
xwayland {
 enabled = false
}
animations {
 enabled = false
}
''' % ('false' if logs_enabled else 'true')
            (directory/'hypr.conf').write_text(config)
            for key, value in {
                'passwd': f'lab:x:{os.getuid()}:{os.getgid()}:Lab:/home/lab:/usr/bin/nologin\n',
                'group': f'lab:x:{os.getgid()}:\n',
                'machine-id': '00000000000000000000000000000006\n'
            }.items():
                (directory/key).write_text(value)
            if ime_target:
                spec = importlib.util.spec_from_file_location('ime_lab', ROOT/'tools/hyprland_ime_lab.py')
                ime = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(ime)
                (directory/'gpu-metadata/sys').mkdir(parents=True)
                plan = {'sysfs': str(directory/'gpu-metadata/sys'), 'statOnlyPrimary': '/dev/dri/card0',
                        'primaryBindMustBeNodevReadOnly': True, 'hostSysfsBind': False}
                snapshots['/lab/rasklad-wayland-keys'] = ime.base.sealed_snapshot(ROOT/'build/wayland-ime/keys/rasklad-wayland-keys')
                paths = {'runner': ROOT/'tools/hyprland_ime_lab.py', 'base': ROOT/'tools/fcitx_lab_runner.py',
                         'bus_conf': ROOT/'tests/fcitx_transport/session-bus.conf'}
                args = ime.sandbox_args(directory, client.fileno(), plan, paths, {}, snapshots)
                for at, item in enumerate(args):
                    if item == '--ro-bind' and args[at+1:at+3] == ['/dev/dri/card0']*2:
                        args[at+1] = '/dev/null'  # Harmless NODEV identity, never a host GPU.
            else:
                args = probe.sandbox_args(directory, client.fileno(), debug_startup=debug_startup)
            if args.count('--dev-bind') != 1:
                raise ValueError('unexpected_device_plan')
            at = args.index('--dev-bind')
            if args[at+1:at+3] != ['/dev/dri/renderD128'] * 2:
                raise ValueError('unexpected_device_plan')
            del args[at:at+3]
            result = subprocess.run(args, pass_fds=(client.fileno(), *snapshots.values()), capture_output=True,
                                    timeout=12, env={'PATH': '/usr/bin'}, preexec_fn=probe.limits)
            client.close()
            thread.join(2)
            return {'loggingExplicitlyEnabled': logs_enabled, 'wireRequests': counts,
                    'serverErrors': errors, 'runnerExitCode': result.returncode,
                    'runnerResult': json.loads(result.stdout)}
    finally:
        client.close()
        for fd in snapshots.values():
            os.close(fd)
        thread.join(2)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--debug-startup', action='store_true')
    parser.add_argument('--ime-plan', action='store_true', help='test the full IME runner startup with no GPU/display')
    args = parser.parse_args()
    if not args.run:
        print(json.dumps({'schema': 1, 'status': 'plan_only', 'hostDisplayConnected': False, 'gpuExposed': False}))
        raise SystemExit(0)
    if os.geteuid() == 0:
        raise SystemExit('root not admitted')
    if args.ime_plan and args.debug_startup:
        raise SystemExit('IME plan uses no debugger')
    output = {'schema': 1, 'hostDisplayConnected': False, 'gpuExposed': False,
              'privateSyntheticParent': True, 'globalsAdvertised': 0,
              'debuggerTest': args.debug_startup,
              'imeTarget': args.ime_plan,
              'variants': [run(False, args.debug_startup, args.ime_plan), run(True, args.debug_startup, args.ime_plan)]}
    variants = output['variants']
    phase = 'hyprland_start' if args.ime_plan else 'compositor_start'
    exit_key = 'compositorExitCode' if args.ime_plan else 'exitCode'
    output['passed'] = all(v['wireRequests']['getRegistry'] == 1 and v['wireRequests']['sync'] == 1 and v['wireRequests']['other'] == 0 and not v['serverErrors'] and v['runnerExitCode'] == 1 and v['runnerResult'].get('passed') is False and v['runnerResult'].get('phase') == phase and (v['runnerResult'].get('debuggerSignal') == 'SIGABRT' if args.debug_startup else v['runnerResult'].get(exit_key) == -6) for v in variants) and [v['runnerResult'].get('startupReason') for v in variants] == ['backend_creation_failed', 'parent_protocols_missing']
    print(json.dumps(output, sort_keys=True))
    raise SystemExit(0 if output['passed'] else 1)
