#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""SYS-253: which program name Fcitx reports per app and toolkit route.

Runs a headless Sway (the pinned lab runtime), its own D-Bus session, its own
Fcitx and a throwaway HOME; nothing of the owner's session is touched. Reads
only metadata: Fcitx DebugInfo program/frontend/capability flags and Sway
app_id. No text is typed or read. Prints one JSON object.

--addon DIR loads the rasklad.so built in DIR into that Fcitx (with the real
descriptor addon/rasklad.conf) and also reports the addon's SeenApplications
status after each app: the names the settings app will offer (SYS-253).
"""
import json
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / 'build/wayland-ime/runtime'
IC = re.compile(r'program:([A-Za-z0-9._-]{0,128}) frontend:([a-z0-9_]{1,32}) cap:([0-9a-f]{1,16}) focus:([01])')
PREEDIT = 1 << 1  # fcitx::CapabilityFlag::Preedit

# (label, argv, extra env). QT_IM_MODULE/GTK_IM_MODULE pick the route:
# "fcitx" = the toolkit's Fcitx module over D-Bus, "wayland" = text-input-v3.
CASES = [
    ('foot', ['foot'], {}),
    ('qt6-fcitx-module', ['omawrite'], {'QT_IM_MODULE': 'fcitx'}),
    ('qt6-text-input-v3', ['omawrite'], {'QT_IM_MODULE': 'wayland'}),
    ('gtk4-fcitx-module', ['nautilus', '--new-window'], {'GTK_IM_MODULE': 'fcitx'}),
    ('gtk4-text-input-v3', ['nautilus', '--new-window'], {'GTK_IM_MODULE': 'wayland'}),
]


def debug_info(env):
    try:
        out = subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.fcitx.Fcitx5', '--object-path',
                              '/controller', '--method', 'org.fcitx.Fcitx.Controller1.DebugInfo'],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=3, env=env)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if out.returncode != 0:
        return None
    ics = []
    for program, frontend, cap, focus in IC.findall(out.stdout[:65536].decode('utf-8', 'replace')):
        ics.append({'program': program, 'frontend': frontend,
                    'preedit': bool(int(cap, 16) & PREEDIT), 'focus': focus == '1'})
    return ics


def seen_applications(env):
    """The addon's status list; None if absent (no addon, or one older than SYS-253)."""
    try:
        out = subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.fcitx.Fcitx5', '--object-path',
                              '/controller', '--method', 'org.fcitx.Fcitx.Controller1.GetConfig',
                              'fcitx://config/addon/rasklad/status'],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=3, env=env)
    except (OSError, subprocess.TimeoutExpired):
        return None
    text = out.stdout[:65536].decode('utf-8', 'replace')
    if out.returncode != 0 or "'SeenApplications'" not in text:
        return None
    block = text.split("'SeenApplications'", 1)[1]
    block = block[:block.find('}>') + 1] if '}>' in block else ''
    return sorted(set(re.findall(r"'[0-9]+': <'([A-Za-z0-9._-]{1,128})'>", block)))


def app_ids(env):
    # Sway IPC GET_TREE (type 4) directly: swaymsg is not installed on the host.
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(3)
            s.connect(env['SWAYSOCK'])
            s.sendall(b'i3-ipc' + struct.pack('=II', 0, 4))
            header = b''
            while len(header) < 14:
                header += s.recv(14 - len(header))
            size, _ = struct.unpack('=II', header[6:])
            if size > 4_000_000:
                return None
            body = b''
            while len(body) < size:
                chunk = s.recv(size - len(body))
                if not chunk:
                    return None
                body += chunk
        root = json.loads(body)
    except (OSError, ValueError, struct.error):
        return None
    found = set()

    def walk(node):
        if node.get('app_id'):
            found.add(node['app_id'])
        if (node.get('window_properties') or {}).get('class'):
            found.add('x11:' + node['window_properties']['class'])
        for child in node.get('nodes', []) + node.get('floating_nodes', []):
            walk(child)
    walk(root)
    return sorted(found)


def stop(proc):
    if proc and proc.poll() is None:
        os.killpg(proc.pid, signal.SIGTERM)
        try:
            proc.wait(3)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()


def inside(addon_dir=None):
    home = Path(tempfile.mkdtemp(prefix='sys253-home-'))
    run = Path(tempfile.mkdtemp(prefix='sys253-run-'))
    run.chmod(0o700)
    env = {k: v for k, v in os.environ.items() if k in ('PATH', 'LANG', 'DBUS_SESSION_BUS_ADDRESS')}
    env.update(HOME=str(home), XDG_RUNTIME_DIR=str(run), XDG_CONFIG_HOME=str(home / '.config'),
               XDG_DATA_HOME=str(home / '.local/share'), XDG_CACHE_HOME=str(home / '.cache'),
               WLR_BACKENDS='headless', WLR_RENDERER='pixman', WLR_LIBINPUT_NO_DEVICES='1',
               LD_LIBRARY_PATH=str(RUNTIME), XDG_SESSION_TYPE='wayland', XDG_CURRENT_DESKTOP='sway')
    (home / '.config/sway').mkdir(parents=True)
    (home / '.config/sway/config').write_text('output HEADLESS-1 resolution 1280x720\n')
    (home / '.config/fcitx5').mkdir(parents=True)
    (home / '.config/fcitx5/profile').write_text(
        '[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=keyboard-us\n\n'
        '[Groups/0/Items/0]\nName=keyboard-us\nLayout=\n\n[GroupOrder]\n0=Default\n')
    if addon_dir:
        lib = home / 'addon-lib'
        lib.mkdir()
        shutil.copy2(Path(addon_dir) / 'rasklad.so', lib / 'rasklad.so')
        (home / '.local/share/fcitx5/addon').mkdir(parents=True)
        shutil.copy2(ROOT / 'addon/rasklad.conf', home / '.local/share/fcitx5/addon/rasklad.conf')
        env['FCITX_ADDON_DIRS'] = f'{lib}:/usr/lib/fcitx5'
    procs = []
    result = {'cases': [], 'addon': bool(addon_dir)}
    try:
        sway = subprocess.Popen([str(RUNTIME / 'sway'), '-c', str(home / '.config/sway/config')], env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        procs.append(sway)
        for _ in range(50):
            sockets = sorted(p.name for p in run.iterdir() if p.name.startswith('wayland-') and not p.name.endswith('.lock'))
            ipc = sorted(run.glob('sway-ipc.*.sock'))
            if sockets and ipc:
                break
            time.sleep(0.1)
        else:
            result['error'] = 'sway_start'
            return result
        env.update(WAYLAND_DISPLAY=sockets[0], SWAYSOCK=str(ipc[0]))
        fcitx = subprocess.Popen(['fcitx5', '--disable=notificationitem,clipboard,quickphrase,cloudpinyin'],
                                 env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        procs.append(fcitx)
        for _ in range(50):
            if debug_info(env) is not None:
                break
            time.sleep(0.2)
        result['baseline'] = debug_info(env)
        for label, argv, extra in CASES:
            app = subprocess.Popen(argv, env=dict(env, **extra), stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, start_new_session=True)
            procs.append(app)  # an interruption mid-case stops it in finally as well
            time.sleep(4)
            before = {(i['program'], i['frontend']) for i in (result['baseline'] or [])}
            ics = debug_info(env) or []
            entry = {'case': label, 'appIds': app_ids(env),
                     'ics': [i for i in ics if (i['program'], i['frontend']) not in before]}
            if addon_dir:
                entry['seenApplications'] = seen_applications(env)
            result['cases'].append(entry)
            stop(app)
            procs.remove(app)
            time.sleep(1)
    finally:
        for p in reversed(procs):
            stop(p)
        shutil.rmtree(home, ignore_errors=True)
        shutil.rmtree(run, ignore_errors=True)
    return result


def main():
    args = sys.argv[1:]
    if args[:1] == ['--inside']:
        # SIGTERM (timeout, dbus-run-session ending) runs the finally cleanup too.
        signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
        print(json.dumps(inside(args[1] if len(args) > 1 else None), ensure_ascii=False))
        return
    addon = []
    if args[:1] == ['--addon'] and len(args) == 2:
        addon = [str(Path(args[1]).resolve())]
    elif args:
        raise SystemExit('usage: program_name_probe.py [--addon BUILD_DIR]')
    # A private D-Bus session: our Fcitx never meets the owner's.
    os.execvp('dbus-run-session', ['dbus-run-session', '--', sys.executable, '-I', str(Path(__file__).resolve()),
                                   '--inside'] + addon)


if __name__ == '__main__':
    main()
