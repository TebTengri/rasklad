#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Step 0 feasibility: does the owner's real Slack reach a mapped Wayland window
inside a closed headless Sway with network only through the Slack CONNECT
bridge? Plan by default. --run-approved needs a separate one-shot owner approval.
No IME, input, text reading, screenshots or profile copies. Output is fixed
booleans/counts; the Wayland debug stream is scanned in memory and discarded."""
import argparse, importlib.util, json, os, re, resource, shutil, signal, stat, subprocess, sys, tempfile, threading, time
from pathlib import Path

INSIDE = Path(__file__) == Path('/lab/runner.py')
ROOT = Path('/lab') if INSIDE else Path(__file__).resolve().parents[1]
SLACK = '/usr/lib/slack/slack'
CHROMIUM = '/usr/lib/chromium/chromium'  # positive control only, dry run
PROXY = '/usr/bin/xdg-dbus-proxy'
OBSERVE_SECONDS = 75        # inside: max wait for window + settle
SETTLE_SECONDS = 15         # after the first buffer attach
QUIT_SECONDS = 15           # graceful SIGTERM before SIGKILL (profile flush)
OUTER_SECONDS = 125          # observe 75 + quit 15 + sway/collector margins
RELAY = dict(seconds=115, max_active=16)  # inside; must cover the observation window
BROKER = dict(seconds=120, max_requests=384, max_bytes=256 * 1024 * 1024, max_active=16)
MARKERS = {
    'registryBound': (b'.get_registry(',),
    'toplevelCreated': (b'.get_toplevel(',),
    'bufferAttached': (b'.attach(wl_buffer',),
    'configureAcked': (b'.ack_configure(',),
    'secretServiceError': (b'org.freedesktop.secrets was not provided', b'failed to create secret service', b'libsecret_error'),
    'singletonFailure': (b'processsingleton', b'singletonlock'),
    'connectionFailure': (b'err_connection', b'err_name_not_resolved', b'err_proxy_connection_failed', b'err_tunnel_connection_failed'),
    'displayInit': (b'failed to connect to wayland', b'platform failed to initialize'),
    'fatal': (b'fatal:', b'check failed:'),
}
# Dry-run diagnostics only (empty offline profile, no owner data): fixed flag sets.
VARIANTS = {'plain': [], 'logging': ['--enable-logging=stderr', '--v=0'],
            'no-sandbox': ['--no-sandbox', '--enable-logging=stderr'],
            'no-zygote': ['--no-zygote', '--no-sandbox', '--enable-logging=stderr'],
            'disable-dev-shm': ['--disable-dev-shm-usage', '--enable-logging=stderr']}
WAYLAND_LINE = (b'wl_', b'xdg_', b'zwp_', b'wp_', b'zxdg_', b'org_kde', b'zwlr_')
CODES = {'diag-dry-only', 'control-dry-only', 'root-not-admitted', 'host-slack-running', 'unsafe-bwrap', 'runtime-signatures', 'secrets-proxy',
         'output-bound', 'report-shape', 'not-private-control', 'private-display'}
REASONS = {'profile-not-directory', 'profile-ownership', 'profile-symlink-parent', 'singleton-lock-present', 'host-bus-address'}


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def diag_tail(chunks, limit=6000):
    """Dry-run only: last non-Wayland-protocol stderr lines, bounded."""
    kept = []
    size = 0
    rest = b''
    for chunk in chunks:
        lines = (rest + chunk).split(b'\n')
        rest = lines.pop()[-4096:]
        for line in lines:
            if any(m in line for m in WAYLAND_LINE) or not line.strip():
                continue
            line = line[:300]
            kept.append(line)
            size += len(line) + 1
            while size > limit:
                size -= len(kept.pop(0)) + 1
    return b'\n'.join(kept).decode('utf-8', 'replace')


def process_kinds(main_pid, proc=Path('/proc')):
    counts = {}
    for entry in list(proc.iterdir())[:4096]:
        if not entry.name.isdecimal():
            continue
        try:
            if os.readlink(entry / 'exe') != SLACK:
                continue
            argv = (entry / 'cmdline').read_bytes()[:16384].split(b'\0')
        except OSError:
            continue
        kind = 'main' if int(entry.name) == main_pid else next((a[7:].decode('ascii', 'replace') for a in argv if a.startswith(b'--type=')), 'unknown')
        counts[kind] = counts.get(kind, 0) + 1
    return counts


def scan(chunks, now=time.monotonic, started=None, publish=None):
    """Fixed marker booleans plus first-seen seconds; keeps only a 128-byte overlap."""
    started = now() if started is None else started
    flags = {name: False for name in MARKERS}
    first = {} if publish is None else publish
    first.setdefault('attachCount', 0)
    tail = b''
    for chunk in chunks:
        frame = (tail + chunk).lower()
        # Count attaches that start in this chunk (overlap excluded), capped.
        first['attachCount'] = min(10000, first['attachCount'] + chunk.lower().count(b'.attach(wl_buffer'))
        for name, needles in MARKERS.items():
            if not flags[name] and any(n in frame for n in needles):
                flags[name] = True
                first[name] = round(now() - started, 2)
        tail = frame[-128:]
    return flags, first


def slack_pids(proc=Path('/proc')):
    found = []
    for entry in list(proc.iterdir())[:4096]:
        if not entry.name.isdecimal():
            continue
        try:
            if os.readlink(entry / 'exe') == SLACK:
                found.append(int(entry.name))
        except OSError:
            continue
    return found


def reap_sandboxed(pids, own_ns=None):
    """SIGKILL leftover Slack processes in a foreign mount namespace (ours); never host ones."""
    own_ns = os.readlink('/proc/self/ns/mnt') if own_ns is None else own_ns
    left = []
    for pid in pids:
        try:
            if os.readlink(f'/proc/{pid}/ns/mnt') != own_ns:
                os.kill(pid, signal.SIGKILL)
                continue
        except OSError:
            continue
        left.append(pid)
    time.sleep(.3)
    return [pid for pid in left if os.path.exists(f'/proc/{pid}')] + [p for p in slack_pids() if p not in left]


def profile_dir(home):
    path = Path(home) / '.config/Slack'
    for parent in [path, *path.parents]:
        if parent.is_symlink():
            raise ValueError('profile-symlink-parent')
    meta = os.lstat(path)
    if not stat.S_ISDIR(meta.st_mode):
        raise ValueError('profile-not-directory')
    if meta.st_uid != os.getuid() or meta.st_mode & 0o022:
        raise ValueError('profile-ownership')
    if os.path.lexists(path / 'SingletonLock'):
        raise ValueError('singleton-lock-present')
    return path


def host_bus_address(env, uid):
    address = env.get('DBUS_SESSION_BUS_ADDRESS', '')
    if not re.fullmatch(rf'unix:path=/run/user/{uid}/bus(?:,guid=[0-9a-f]{{32}})?', address):
        raise ValueError('host-bus-address')
    return address


def proxy_command(address, socket_path):
    # Only the Secret Service name is reachable: no portals, xdg-open, notifications or IME.
    return [PROXY, address, str(socket_path), '--filter', '--talk=org.freedesktop.secrets']


def sandbox_args(home, profile, uid, user, work, snapshots, lang='C.UTF-8'):
    runtime = f'/run/user/{uid}'
    a = ['/usr/bin/bwrap', '--unshare-all', '--die-with-parent', '--new-session', '--cap-drop', 'ALL', '--clearenv',
         '--ro-bind', '/usr', '/usr', '--symlink', 'usr/bin', '/bin', '--symlink', 'usr/lib', '/lib', '--symlink', 'usr/lib', '/lib64',
         '--ro-bind-try', '/etc/ld.so.cache', '/etc/ld.so.cache', '--ro-bind', '/etc/fonts', '/etc/fonts',
         '--ro-bind', '/etc/machine-id', '/etc/machine-id', '--ro-bind', '/etc/ssl', '/etc/ssl',
         '--ro-bind', '/etc/ca-certificates', '/etc/ca-certificates',
         # Slack's boot reads the distro identity; without it the environment step throws and boot stalls.
         '--ro-bind', '/etc/os-release', '/etc/os-release', '--ro-bind-try', '/etc/arch-release', '/etc/arch-release',
         '--ro-bind', str(work / 'passwd'), '/etc/passwd', '--ro-bind', str(work / 'group'), '/etc/group',
         '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp', '--tmpfs', '/run', '--tmpfs', '/home',
         '--perms', '0700', '--dir', runtime,
         '--dir', str(home), '--dir', str(home) + '/.config', '--bind', str(profile), str(profile),
         '--bind', str(work / 'bus'), runtime + '/bus', '--bind', str(work / 'https.sock'), '/lab/https.sock',
         '--ro-bind', str(ROOT / 'tools/slack_bootstrap_probe.py'), '/lab/runner.py',
         '--ro-bind', str(ROOT / 'tools/slack_https_bridge.py'), '/lab/slack_https_bridge.py',
         '--dir', '/lab/wayland-runtime', '--chdir', str(home)]
    for name, fd in sorted(snapshots.items()):
        a += ['--perms', '0755' if name == 'sway' else '0444', '--ro-bind-data', str(fd), '/lab/wayland-runtime/' + name]
    env = {'PATH': '/usr/bin', 'LANG': lang, 'HOME': str(home), 'USER': user, 'LOGNAME': user,
           'XDG_RUNTIME_DIR': runtime, 'XDG_CURRENT_DESKTOP': 'Hyprland', 'XDG_SESSION_TYPE': 'wayland',
           'DBUS_SESSION_BUS_ADDRESS': f'unix:path={runtime}/bus', 'LD_LIBRARY_PATH': '/lab/wayland-runtime',
           'WLR_BACKENDS': 'headless', 'WLR_RENDERER': 'pixman', 'WLR_LIBINPUT_NO_DEVICES': '1',
           'GTK_IM_MODULE': 'gtk-im-context-simple', 'NO_AT_BRIDGE': '1', 'RASKLAD_SLACK_BOOTSTRAP_LAB': '1'}
    for key, value in env.items():
        a += ['--setenv', key, value]
    return a + ['--', '/usr/bin/python3', '-I', '/lab/runner.py', '--inside']


def release_own_singleton(profile, hostname, inner_pid):
    """After our sandboxed Slack stopped: remove Chromium singleton links only if the lock
    names exactly our in-namespace PID (e.g. 'host-7'); that PID is meaningless outside and can
    collide with a host process, blocking the owner's normal Slack. Anything else is left alone."""
    lock = Path(profile) / 'SingletonLock'
    try:
        target = os.readlink(lock)
    except OSError:
        return 'absent'
    if target != f'{hostname}-{inner_pid}':
        return 'foreign-left'
    for name in ('SingletonLock', 'SingletonCookie', 'SingletonSocket'):
        try:
            if os.path.islink(Path(profile) / name):
                os.unlink(Path(profile) / name)
        except OSError:
            return 'remove-failed'
    return 'released'


def private_context(env, inside_flag=INSIDE, uid=None):
    uid = os.getuid() if uid is None else uid
    return (inside_flag and env.get('RASKLAD_SLACK_BOOTSTRAP_LAB') == '1'
            and env.get('XDG_RUNTIME_DIR') == f'/run/user/{uid}'
            and env.get('DBUS_SESSION_BUS_ADDRESS') == f'unix:path=/run/user/{uid}/bus'
            and not any(env.get(k) for k in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'RASKLAD_PARENT_FD')))


def stop(process, seconds):
    if process.poll() is not None:
        return 'exited'
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=seconds)
        return 'graceful'
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)
        return 'killed'


def inside():
    if not private_context(os.environ):
        raise RuntimeError('not-private-control')
    bridge = load('slack_https_bridge', Path('/lab/slack_https_bridge.py'))
    report = {'passed': False, 'textRead': False, 'inputInjected': False}
    children = []
    relay = bridge.Relay('/lab/https.sock', seconds=RELAY['seconds'], max_active=RELAY['max_active'])
    try:
        runtime = Path(os.environ['XDG_RUNTIME_DIR'])
        config = Path('/tmp/sway.conf')
        config.write_text('xwayland disable\nseat seat0 fallback true\noutput HEADLESS-1 mode 1280x900\n')
        sway = subprocess.Popen(['/lab/wayland-runtime/sway', '-c', str(config)], stdin=subprocess.DEVNULL,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        children.append(sway)
        end = time.monotonic() + 3
        while True:
            sockets = [p for p in runtime.glob('wayland-*') if p.is_socket()]
            if sockets:
                break
            if sway.poll() is not None or time.monotonic() > end:
                raise RuntimeError('private-display')
            time.sleep(.04)
        if len(sockets) != 1:
            raise RuntimeError('private-display')
        env = dict(os.environ, WAYLAND_DISPLAY=sockets[0].name, WAYLAND_DEBUG='client')
        proxy = 'http://127.0.0.1:' + str(relay.port)
        env.update(HTTPS_PROXY=proxy, HTTP_PROXY=proxy, https_proxy=proxy, http_proxy=proxy, NO_PROXY='')
        started = time.monotonic()
        control = os.environ.get('RASKLAD_BOOTSTRAP_CONTROL')
        diag = os.environ.get('RASKLAD_BOOTSTRAP_DIAG')
        variant = diag if diag in VARIANTS else 'plain'
        chromium = [CHROMIUM, '--ozone-platform=wayland', '--disable-gpu', '--no-first-run', '--user-data-dir=/tmp/control']
        target = (chromium + ['about:blank'] if control == 'chromium'
                  else chromium + ['--proxy-server=' + proxy, 'https://app.slack.com/'] if control == 'chromium-hello'
                  else [SLACK, '--ozone-platform=wayland', '--proxy-server=' + proxy, '--disable-gpu'] + VARIANTS[variant])
        slack = subprocess.Popen(target,
                                 env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        children.append(slack)
        first = {}
        result = {}
        def collect():
            if diag:
                tee = []
                def chunks():
                    for chunk in iter(lambda: slack.stdout.read(4096), b''):
                        tee.append(chunk)
                        if sum(map(len, tee)) > 8 << 20:
                            tee.pop(0)
                        yield chunk
                result['flags'], _ = scan(chunks(), started=started, publish=first)
                result['tail'] = diag_tail(tee)
            else:
                result['flags'], _ = scan(iter(lambda: slack.stdout.read(4096), b''), started=started, publish=first)
        collector = threading.Thread(target=collect, daemon=True)
        collector.start()
        attached_at = None
        deadline = started + OBSERVE_SECONDS
        exited_early = False
        while time.monotonic() < deadline:
            if slack.poll() is not None:
                exited_early = True
                break
            if diag and 'processKinds10s' not in report and time.monotonic() - started >= 10:
                report['processKinds10s'] = process_kinds(slack.pid)
            if attached_at is None and 'bufferAttached' in first:
                attached_at = time.monotonic()
            if attached_at is not None and time.monotonic() - attached_at >= SETTLE_SECONDS:
                break
            time.sleep(.2)
        report['observedSeconds'] = round(time.monotonic() - started, 2)
        report['slackExitedEarly'] = exited_early
        report['slackExitCode'] = slack.returncode if exited_early else None
        report['slackStop'] = stop(slack, QUIT_SECONDS)
        if report['slackStop'] != 'killed' or slack.poll() is not None:
            import socket as _socket
            report['singletonRelease'] = release_own_singleton(Path(os.environ['HOME'], '.config/Slack'), _socket.gethostname(), slack.pid)
        report['relayRefused'] = relay.refused
        collector.join(3)
        report['collectionComplete'] = not collector.is_alive()
        report['markers'] = result.get('flags', {})
        if diag:
            report['variant'] = variant
            report['diagTail'] = result.get('tail', '')
            # Dry run: the profile is the empty temporary one, so Slack's own logs hold no owner data.
            logs = sorted(Path(os.environ['HOME'], '.config/Slack/logs').rglob('*.log'), key=lambda q: q.stat().st_mtime)[-3:]
            report['slackLogs'] = {str(q.relative_to(Path(os.environ['HOME'], '.config/Slack'))): q.read_bytes()[-2500:].decode('utf-8', 'replace') for q in logs}
        report['firstSeenSeconds'] = dict(first)
        markers = report['markers']
        report['attachCount'] = report['firstSeenSeconds'].pop('attachCount', 0)
        # A mapped, repainting toplevel: created, configure acked, >=10 buffer attaches, no fatal/display error.
        report['windowMapped'] = bool(markers.get('toplevelCreated') and markers.get('configureAcked')
                                      and report['attachCount'] >= 10 and not markers.get('fatal') and not markers.get('displayInit'))
        report['passed'] = report['collectionComplete'] and not exited_early
        return report
    finally:
        relay.close()
        for child in reversed(children):
            stop(child, 2)


def limits():
    os.nice(10)
    for kind, value in [(resource.RLIMIT_CPU, 120), (resource.RLIMIT_FSIZE, 512 << 20), (resource.RLIMIT_CORE, 0), (resource.RLIMIT_NOFILE, 4096)]:
        resource.setrlimit(kind, (value, value))


def plan():
    return {'schema': 1, 'mode': 'plan', 'step': 'slack-bootstrap-feasibility',
            'requires': ['host Slack closed by the owner', 'one-shot owner approval'],
            'profile': 'real ~/.config/Slack bound read-write at the same path; no copy',
            'compositor': 'closed headless Sway (pixman), no host display/GPU/input devices',
            'network': 'private netns; TCP only via unix-socket relay to the CONNECT+SNI bridge (slack.com, slack-edge.com, slack-imgs.com); ECH outer-SNI allowed',
            'hostBus': 'xdg-dbus-proxy filtered to org.freedesktop.secrets only',
            'ime': 'none', 'input': 'none', 'textRead': False, 'screenshots': False,
            'observeSeconds': OBSERVE_SECONDS, 'outerSeconds': OUTER_SECONDS,
            'output': 'fixed marker booleans, first-seen seconds, stop mode, bridge counts, rejected-domain suffixes'}


def run(dry=False, control=False, hello=False, diag=None):
    if (control or hello) and not dry:
        raise RuntimeError('control-dry-only')
    if diag is not None and (not dry or diag not in VARIANTS):
        raise RuntimeError('diag-dry-only')
    """dry=True: empty temporary profile, private bus, offline bridge; no owner data or network."""
    if os.geteuid() == 0:
        raise RuntimeError('root-not-admitted')
    if slack_pids():
        raise RuntimeError('host-slack-running')
    base = load('base', ROOT / 'tools/fcitx_lab_runner.py')
    bridge = load('slack_https_bridge', ROOT / 'tools/slack_https_bridge.py')
    for path in (Path('/usr/bin/bwrap'), Path(SLACK), Path(PROXY)):
        base.regular_file(path, True)
    meta = os.stat('/usr/bin/bwrap')
    if meta.st_uid != 0 or meta.st_mode & 0o6022:
        raise RuntimeError('unsafe-bwrap')
    import pwd
    entry = pwd.getpwuid(os.getuid())
    home = Path(entry.pw_dir)
    profile = None
    if not dry:
        profile = profile_dir(home)
        address = host_bus_address(os.environ, os.getuid())
    lock = json.loads((ROOT / 'data/wayland-lab-runtime-lock.json').read_text())
    if not all(p.get('signatureVerified') is True for p in lock['packages']):
        raise RuntimeError('runtime-signatures')
    work = Path(tempfile.mkdtemp(prefix='slack-bootstrap-', dir=base.ensure_private_base(ROOT)))
    snapshots = {}
    broker = proxy = child = None
    report = {'schema': 1, 'step': 'slack-bootstrap-feasibility', 'passed': False, 'dryRunEmptyProfile': dry}
    try:
        if dry:
            home = work / 'home'
            (home / '.config/Slack').mkdir(parents=True, mode=0o700)
            os.chmod(home, 0o700); os.chmod(home / '.config', 0o700)
            profile = profile_dir(home)
        for name in ('sway', 'libwlroots-0.20.so', 'libliftoff.so.0'):
            snapshots[name] = base.sealed_snapshot(ROOT / 'build/wayland-ime/runtime' / name, lock['runtimeFiles'][name])
        (work / 'passwd').write_text(f'{entry.pw_name}:x:{os.getuid()}:{os.getgid()}::{home}:/usr/bin/nologin\n')
        (work / 'group').write_text(f'{entry.pw_name}:x:{os.getgid()}:\n')
        bus_command = (['/usr/bin/dbus-daemon', '--nofork', '--config-file=' + str(ROOT / 'tests/fcitx_transport/session-bus.conf'),
                        '--address=unix:path=' + str(work / 'bus')] if dry else proxy_command(address, work / 'bus'))
        proxy = subprocess.Popen(bus_command, stdin=subprocess.DEVNULL,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env={'PATH': '/usr/bin'})
        end = time.monotonic() + 3
        while not (work / 'bus').is_socket():
            if proxy.poll() is not None or time.monotonic() > end:
                raise RuntimeError('secrets-proxy')
            time.sleep(.04)
        broker = bridge.Broker(work / 'https.sock', online=not dry, allow_ech=not dry or hello, accept_hello=hello, **BROKER)
        command = sandbox_args(home, profile, os.getuid(), entry.pw_name, work, snapshots, os.environ.get('LANG', 'C.UTF-8'))
        if control or hello:
            at = command.index('--')
            command[at:at] = ['--setenv', 'RASKLAD_BOOTSTRAP_CONTROL', 'chromium-hello' if hello else 'chromium']
        report['positiveControl'] = 'chromium-hello' if hello else 'chromium' if control else None
        if diag:
            at = command.index('--')
            command[at:at] = ['--setenv', 'RASKLAD_BOOTSTRAP_DIAG', diag]
        if slack_pids():
            raise RuntimeError('host-slack-running')
        if not dry:
            profile_dir(home)
        with (work / 'report').open('w+b') as out:
            child = subprocess.Popen(command, pass_fds=tuple(snapshots.values()), stdin=subprocess.DEVNULL, stdout=out,
                                     stderr=subprocess.DEVNULL, env={'PATH': '/usr/bin'}, start_new_session=True, preexec_fn=limits)
            report['sandboxExitCode'] = child.wait(timeout=OUTER_SECONDS)
            out.seek(0)
            raw = out.read(32769)
        if len(raw) > 32768:
            raise RuntimeError('output-bound')
        inner = json.loads(raw)
        if type(inner) is not dict:
            raise RuntimeError('report-shape')
        report['inside'] = inner
    finally:
        if child and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=QUIT_SECONDS)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=2)
        if broker:
            report['bridgeStats'] = broker.close()
        if proxy:
            stop(proxy, 2)
        for fd in snapshots.values():
            os.close(fd)
        shutil.rmtree(work, ignore_errors=True)
    remaining = reap_sandboxed(slack_pids())
    report['slackProcessesRemaining'] = len(remaining)
    report['singletonLockLeft'] = os.path.lexists(profile / 'SingletonLock') if profile and not dry else None
    if report['singletonLockLeft']:
        report['ownerRecovery'] = 'SingletonLock left in the real profile: do not start Slack; report to Claude for a reviewed manual recovery step'
    inner = report.get('inside', {})
    report['passed'] = (inner.get('passed') is True and not remaining and not report['singletonLockLeft']
                        and report.get('bridgeStats', {}).get('cleanupComplete') is True)
    report['windowMapped'] = inner.get('windowMapped') is True
    report['interpretation'] = ('passed = bounded measurement completed and cleaned up. windowMapped = a toplevel was created, configured '
                                'and repainted >=10 times; it means Slack renders a window in the sandbox, NOT that its UI or login is ready')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--run-approved', action='store_true')
    group.add_argument('--dry-run-empty-profile', action='store_true')
    group.add_argument('--dry-run-chromium-control', action='store_true')
    group.add_argument('--dry-run-hello-capture', action='store_true')
    group.add_argument('--dry-run-diag', choices=sorted(VARIANTS))
    group.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        report = inside() if args.inside else run() if args.run_approved else run(dry=True) if args.dry_run_empty_profile else run(dry=True, control=True) if args.dry_run_chromium_control else run(dry=True, hello=True) if args.dry_run_hello_capture else run(dry=True, diag=args.dry_run_diag) if args.dry_run_diag else plan()
    except Exception as exc:
        text = str(exc)
        known = (type(exc) is RuntimeError and text in CODES) or (type(exc) is ValueError and text in REASONS)
        report = {'schema': 1, 'passed': False, 'error': text if known else type(exc).__name__}
    print(json.dumps(report, sort_keys=True))
    return 0 if report.get('passed', True) else 1


if __name__ == '__main__':
    sys.exit(main())
