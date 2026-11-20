#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Slack launch trial inside the nested Hyprland lab (no input, no arming).

Outer half (host): the step-0 preconditions from slack_bootstrap_probe (host Slack
closed, real profile shape, host bus address), the Secret Service proxy and the
CONNECT bridge, extra sandbox binds, and the post-run profile/process checks.
Inner half (/lab): the session addon's arm pipe is latched off before Slack starts,
so the addon never intercepts a key. Recorded: fixed booleans, counts, window
class, Fcitx program names and addon context records (event + purpose only).
Never titles, text, screenshots or Slack logs."""
import json, os, re, resource, select, signal, socket, subprocess, time
from pathlib import Path

INSIDE = Path(__file__) == Path('/lab/slack_launch_lab.py')
ROOT = Path('/lab') if INSIDE else Path(__file__).resolve().parents[1]
SLACK = '/usr/lib/slack/slack'
ADDON = ROOT / 'build/chromium-ime/slack-session-addon/raskladslacklab.so'
ADDON_CONF = ROOT / 'tests/slack_session/raskladslacklab-nested.conf'
OBSERVE_SECONDS = 90   # boot + the owner's zero-injection composer→search→composer check
QUIT_SECONDS = 15      # graceful SIGTERM before SIGKILL (profile flush)
OUTER_SECONDS = 150    # Hyprland/Fcitx start + observe 90 + quit 15 + margins
RELAY = dict(seconds=115, max_active=32)  # run 001: Slack opened >16 concurrent tunnels
BROKER = dict(seconds=170,  # created before Hyprland/Fcitx start; must outlive observe + quit
              max_requests=384, max_bytes=256 * 1024 * 1024, max_active=16)
# Live session (one owner-approved run): wait for the owner's F10, two fixed scripts, owner look, quit.
LIVE_CASES = ('correction', 'english')
LIVE_WAIT_SECONDS = 180   # Slack boot + owner opens the test channel, clicks the composer, presses F10
LIVE_LOOK_SECONDS = 10    # after revoke: the owner checks the composer before Slack quits
LIVE_OUTER_SECONDS = 320
LIVE_RELAY = dict(seconds=280, max_active=32)
LIVE_BROKER = dict(seconds=295, max_requests=512, max_bytes=256 * 1024 * 1024, max_active=16)
# Seat lock for the live session: the owner's keyboards reach nothing but F10 (go) and F12 (kill).
# F12 never re-opens the keyboard: it enters rasklad_dead (swallows everything); only the
# supervisor's unlock() resets the submap, after the helper is gone and the arm is latched.
# After F10 the mouse buttons are swallowed too (binds consume by default).
LIVE_SUBMAP = """submap = rasklad_lock
bind = , F12, submap, rasklad_dead
bind = , F10, submap, rasklad_go
bind = , catchall, submap, rasklad_lock
submap = rasklad_go
bind = , F12, submap, rasklad_dead
bind = , mouse:272, submap, rasklad_go
bind = , mouse:273, submap, rasklad_go
bind = , mouse:274, submap, rasklad_go
bind = , catchall, submap, rasklad_go
submap = rasklad_dead
bind = , mouse:272, submap, rasklad_dead
bind = , mouse:273, submap, rasklad_dead
bind = , mouse:274, submap, rasklad_dead
bind = , catchall, submap, rasklad_dead
submap = reset
"""
SLACK_FLAGS = ['--ozone-platform=wayland', '--enable-wayland-ime', '--wayland-text-input-version=3', '--disable-gpu']
SLACK_BUS = '/run/lab/slack-bus'
MAX_CONTEXTS = 64
CLASS = re.compile(r'[A-Za-z0-9._-]{1,32}')
PROGRAM = re.compile(rb'program:([A-Za-z0-9._-]{1,64})')
LANG = re.compile(r'[A-Za-z]{2,3}(?:_[A-Z]{2})?\.UTF-8|C\.UTF-8')
USER = re.compile(r'[a-z_][a-z0-9_-]{0,31}')


def load(name, path):
    import importlib.util
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# ---------------------------------------------------------------- outer (host)

def preflight(base, env=os.environ):
    """Step-0 checks; start() repeats the Slack/profile checks right before the sandbox."""
    boot = load('slack_bootstrap_for_launch', ROOT / 'tools/slack_bootstrap_probe.py')
    if boot.slack_pids():
        raise base.Refused('host_slack_running')
    for path in (Path(SLACK), Path(boot.PROXY), ADDON, ADDON_CONF, ROOT / 'tools/slack_https_bridge.py'):
        base.regular_file(path, path in (Path(SLACK), Path(boot.PROXY)))
    import pwd
    entry = pwd.getpwuid(os.getuid())
    home = Path(entry.pw_dir)
    if not USER.fullmatch(entry.pw_name) or home.parent != Path('/home') or home.name != entry.pw_name:
        raise base.Refused('slack_home_shape')
    try:
        profile = boot.profile_dir(home)
        address = boot.host_bus_address(env, os.getuid())
    except ValueError as error:
        raise base.Refused(str(error).replace('-', '_')) from None
    lang = env.get('LANG', 'C.UTF-8')
    return {'boot': boot, 'user': entry.pw_name, 'home': home, 'profile': profile, 'address': address,
            'lang': lang if LANG.fullmatch(lang) else 'C.UTF-8'}


def start(base, directory, pre, ctx):
    """Secret Service proxy (same filter as step 0) and the online CONNECT broker.
    Fills `ctx` as it goes so the caller's finally can always clean up."""
    boot = pre['boot']
    bridge = load('slack_bridge_outer', ROOT / 'tools/slack_https_bridge.py')
    ctx['proxy'] = subprocess.Popen(boot.proxy_command(pre['address'], directory / 'bus'), stdin=subprocess.DEVNULL,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env={'PATH': '/usr/bin'})
    end = time.monotonic() + 3
    while not (directory / 'bus').is_socket():
        if ctx['proxy'].poll() is not None or time.monotonic() > end:
            raise base.Refused('secrets_proxy')
        time.sleep(.04)
    ctx['broker'] = bridge.Broker(directory / 'https.sock', online=True, allow_ech=True,
                                  **(LIVE_BROKER if pre.get('mode') == 'live' else BROKER))
    if boot.slack_pids():
        raise base.Refused('host_slack_running')
    try:
        boot.profile_dir(pre['home'])
    except ValueError as error:
        raise base.Refused(str(error).replace('-', '_')) from None


def identity_files(directory, pre):
    """Slack sees the owner's name/home (as in step 0); lab processes keep HOME=/home/lab."""
    (directory / 'passwd').write_text(f"{pre['user']}:x:{os.getuid()}:{os.getgid()}::{pre['home']}:/usr/bin/nologin\n")
    (directory / 'group').write_text(f"{pre['user']}:x:{os.getgid()}:\n")
    (directory / 'machine-id').write_bytes(Path('/etc/machine-id').read_bytes()[:33])


def sandbox_extra(directory, pre):
    home, profile = str(pre['home']), str(pre['profile'])
    extra = ['--ro-bind', '/etc/ssl', '/etc/ssl', '--ro-bind', '/etc/ca-certificates', '/etc/ca-certificates',
             '--ro-bind', '/etc/os-release', '/etc/os-release', '--ro-bind-try', '/etc/arch-release', '/etc/arch-release',
             '--dir', home, '--dir', home + '/.config', '--bind', profile, profile,
             '--bind', str(directory / 'bus'), SLACK_BUS, '--bind', str(directory / 'https.sock'), '/lab/https.sock',
             '--ro-bind', str(ROOT / 'tools/slack_https_bridge.py'), '/lab/slack_https_bridge.py',
             '--ro-bind', str(ROOT / 'tools/slack_launch_lab.py'), '/lab/slack_launch_lab.py']
    if pre.get('mode') == 'live':
        mode = 'RASKLAD_SLACK_LIVE_LAB'
        extra += ['--ro-bind', str(ROOT / 'tools/slack_supervisor.py'), '/lab/slack_supervisor.py',
                  '--ro-bind', str(ROOT / 'tools/slack_injector.py'), '/lab/slack_injector.py']
    elif pre.get('mode', 'launch') == 'launch':
        mode = 'RASKLAD_SLACK_LAUNCH_LAB'
    else:
        raise ValueError('slack_mode')
    for key, value in (('RASKLAD_SLACK_SESSION_LAB', '1'), ('RASKLAD_SLACK_GATE_LAB', '1'), (mode, '1'),
                       ('RASKLAD_SLACK_HOME', home), ('RASKLAD_SLACK_USER', pre['user']), ('RASKLAD_SLACK_LANG', pre['lang'])):
        extra += ['--setenv', key, value]
    return extra


def limits():
    # Step-0 limits: Slack needs more CPU seconds and larger profile files than the Chromium fixture.
    os.nice(10)
    for kind, value in [(resource.RLIMIT_CPU, 300), (resource.RLIMIT_FSIZE, 512 << 20), (resource.RLIMIT_CORE, 0), (resource.RLIMIT_NOFILE, 4096)]:
        resource.setrlimit(kind, (value, value))


def finish(ctx, pre):
    """After the sandbox is gone: bridge counts, leftover processes, profile lock."""
    boot = pre['boot']
    report = {'finishErrors': []}
    # Every step runs even if an earlier one fails; never raises.
    try:
        if ctx.get('broker'):
            stats = ctx['broker'].close()
            report['bridgeStats'] = {k: stats.get(k) for k in ('accepted', 'blocked', 'opened', 'errors', 'forwardedBytes',
                                                                'tlsDenied', 'rejectedDomains', 'cleanupComplete')}
    except Exception as error:
        report['finishErrors'].append('broker:' + type(error).__name__)
    try:
        if ctx.get('proxy'):
            stop(ctx['proxy'], 2)
    except Exception as error:
        report['finishErrors'].append('proxy:' + type(error).__name__)
    try:
        remaining = boot.reap_sandboxed(boot.slack_pids())
    except Exception as error:
        report['finishErrors'].append('reap:' + type(error).__name__)
        remaining = [None]
    report['slackProcessesRemaining'] = len(remaining)
    try:
        report['singletonLockLeft'] = os.path.lexists(pre['profile'] / 'SingletonLock')
    except Exception as error:
        report['finishErrors'].append('lock:' + type(error).__name__)
        report['singletonLockLeft'] = True
    if report['singletonLockLeft']:
        report['ownerRecovery'] = 'SingletonLock left in the real profile: do not start Slack; report to Claude for a reviewed manual recovery step'
    report['outerClean'] = (not remaining and not report['singletonLockLeft'] and not report['finishErrors']
                            and report.get('bridgeStats', {}).get('cleanupComplete') is True)
    return report


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


# ---------------------------------------------------------------- inner (/lab)

def release_own_singleton(profile, hostname, inner_pid):
    """Same rule as the step-0 probe: remove singleton links only if the lock names our in-namespace PID."""
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


def slack_env(environ):
    home, user, lang = environ['RASKLAD_SLACK_HOME'], environ['RASKLAD_SLACK_USER'], environ['RASKLAD_SLACK_LANG']
    if (not USER.fullmatch(user) or home != '/home/' + user or not LANG.fullmatch(lang)
            or not Path(home, '.config/Slack').is_dir()):
        raise ValueError('slack_child_identity')
    # Built from scratch: no lab XDG_*_HOME, no private Fcitx bus, no FD numbers.
    return {'PATH': '/usr/bin', 'HOME': home, 'USER': user, 'LOGNAME': user, 'LANG': lang,
            'XDG_RUNTIME_DIR': '/run/lab', 'WAYLAND_DISPLAY': 'wayland-lab', 'XDG_SESSION_TYPE': 'wayland',
            'XDG_CURRENT_DESKTOP': 'Hyprland', 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=' + SLACK_BUS, 'NO_AT_BRIDGE': '1'}


def windows(clients, base, slack_pid):
    """Mapped toplevels by owner: ours (Slack tree) vs foreign. Class only, never titles."""
    out = clients.hyprctl(base, '-j', 'clients')
    if out.returncode != 0:
        return None
    try:
        listed = json.loads(out.stdout)
    except ValueError:
        return None
    if type(listed) is not list:
        return None
    ours, foreign, classes = 0, 0, set()
    for window in listed[:64]:
        if type(window) is not dict or window.get('mapped') is not True:
            continue
        pid = window.get('pid')
        if type(pid) is int and clients.descends_from(pid, slack_pid):
            ours += 1
            name = window.get('initialClass')
            classes.add(name if type(name) is str and CLASS.fullmatch(name) else 'other')
        else:
            foreign += 1
    return ours, foreign, sorted(classes)[:4]


def fcitx_programs():
    try:
        out = subprocess.run(['/usr/bin/gdbus', 'call', '--session', '--dest', 'org.fcitx.Fcitx5', '--object-path',
                              '/controller', '--method', 'org.fcitx.Fcitx.Controller1.DebugInfo'],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=2).stdout[:65536]
    except (OSError, subprocess.TimeoutExpired):
        return set()
    return {m.decode() for m in PROGRAM.findall(out)}


def run_inside(clients, base, fcitx, gate, children):
    if os.environ.get('RASKLAD_SLACK_LAUNCH_LAB') != '1' or os.environ.get('RASKLAD_SLACK_SUPERVISED_LAB'):
        raise base.Refused('slack_launch_environment')
    read_end, write_end, receipts, addon_end = gate
    addon_end.close()
    os.close(read_end)
    # Latch the session gate off before Slack exists: the addon must never intercept a key.
    os.write(write_end, b'x')
    os.close(write_end)
    # Written + closed by us before Popen (unit-tested); the addon re-polls the pipe on every key.
    report = {'slackLaunch': True, 'inputInjected': False, 'textRead': False, 'armLatchWrittenBeforeLaunch': True}
    slack = relay = None
    released = None
    try:
        records = clients.AddonRecords(base, receipts)
        env = slack_env(os.environ)
        bridge = load('slack_bridge_inner', Path('/lab/slack_https_bridge.py'))
        relay = bridge.Relay('/lab/https.sock', **RELAY)
        proxy = 'http://127.0.0.1:' + str(relay.port)
        env.update(HTTPS_PROXY=proxy, HTTP_PROXY=proxy, https_proxy=proxy, http_proxy=proxy, NO_PROXY='')
        started = time.monotonic()
        slack = subprocess.Popen([SLACK, '--proxy-server=' + proxy, *SLACK_FLAGS], env=env, stdin=subprocess.DEVNULL,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=env['HOME'])
        children.append(slack)
        programs, contexts, first_mapped, foreign_max, classes, samples = set(), [], None, 0, set(), 0
        next_sample = started
        while time.monotonic() - started < OBSERVE_SECONDS:
            if slack.poll() is not None or fcitx.poll() is not None:
                break
            for record in records.poll(.2):
                if len(contexts) < MAX_CONTEXTS:
                    contexts.append([round(time.monotonic() - started, 1), chr(record[0]),
                                     clients.PURPOSES[record[1:2]], int(record[2:5])])
            if time.monotonic() >= next_sample:
                next_sample += 1
                seen = windows(clients, base, slack.pid)
                if seen is not None:
                    samples += 1
                    ours, foreign, names = seen
                    foreign_max = max(foreign_max, foreign)
                    classes.update(names)
                    if ours and first_mapped is None:
                        first_mapped = round(time.monotonic() - started, 1)
                    if samples % 10 == 1:
                        programs |= fcitx_programs()
        programs |= fcitx_programs()
        report.update(slackExitedEarly=slack.poll() is not None, fcitxAlive=fcitx.poll() is None,
                      observedSeconds=round(time.monotonic() - started, 1), windowMappedSeconds=first_mapped,
                      windowMapped=first_mapped is not None, slackWindowClasses=sorted(classes),
                      foreignWindowsMax=foreign_max, fcitxPrograms=sorted(programs)[:8],
                      contextRecords=contexts, contextRecordsTruncated=len(contexts) >= MAX_CONTEXTS,
                      commitRecords=len(records.commits))
        report['slackStop'] = stop(slack, QUIT_SECONDS)
        # Same rule as step 0: once our Slack main process is dead, release only a lock naming its in-namespace PID.
        released = report['singletonRelease'] = release_own_singleton(Path(env['HOME'], '.config/Slack'), socket.gethostname(), slack.pid)
        report['relayRefused'] = relay.refused
        # The addon must have seen Slack (program matched, records delivered): a silent addon is not a pass.
        report['addonSawSlack'] = bool(contexts)
        report['passed'] = (report['windowMapped'] and report['addonSawSlack'] and not report['slackExitedEarly'] and report['fcitxAlive']
                            and report['commitRecords'] == 0 and report['foreignWindowsMax'] == 0
                            and report['slackStop'] == 'graceful' and report['singletonRelease'] in ('released', 'absent')
                            and report['relayRefused'] == 0)
        return report
    finally:
        if slack is not None:
            if slack.poll() is None:
                stop(slack, QUIT_SECONDS)
            if released is None and slack.poll() is not None:
                release_own_singleton(Path(os.environ.get('RASKLAD_SLACK_HOME', '/nonexistent'), '.config/Slack'),
                                      socket.gethostname(), slack.pid)
        if relay is not None:
            relay.close()
        receipts.close()


# ---------------------------------------------------------------- live session (/lab)

def start_slack(environ, relay_bounds):
    """Relay + Slack child, shared by the launch trial and the live session."""
    env = slack_env(environ)
    bridge = load('slack_bridge_inner', Path('/lab/slack_https_bridge.py'))
    relay = bridge.Relay('/lab/https.sock', **relay_bounds)
    proxy = 'http://127.0.0.1:' + str(relay.port)
    env.update(HTTPS_PROXY=proxy, HTTP_PROXY=proxy, https_proxy=proxy, http_proxy=proxy, NO_PROXY='')
    return env, relay, [SLACK, '--proxy-server=' + proxy, *SLACK_FLAGS]


def run_case(clients, base, sup, case, sequence, binding, records, seat, helper, write):
    """One fixed script under a fresh Supervisor. Any context record, seat change or compositor
    change before a key revokes; cleanup only after the addon receipt matched exactly."""
    inj = sup.inj
    pid, address, epoch, purpose = binding
    s = sup.Supervisor([pid], case, sequence=sequence)
    out = {'case': case, 'receiptMatched': False, 'sent': 0}

    def current():
        fresh = records.poll()
        if fresh:
            s.event(clients.CONTEXT_EVENTS[fresh[0][:1]])
            return (0, 0, 0, 'none')
        if not seat.intact():
            s.event('seat-lost')
            return (0, 0, 0, 'none')
        try:
            now = clients.compositor_binding(base)
        except (base.Refused, OSError, subprocess.SubprocessError, ValueError):
            now = None
        if not now:
            s.event('surface-change')
            return (0, 0, 0, 'none')
        return (now[0], now[1], epoch, purpose)

    def pending_receipt(timeout=0):
        # Seat and compositor focus are re-checked every 0.1 s while committed text waits for cleanup.
        end = time.monotonic() + timeout
        while not records.commits:
            if records.poll(min(.1, max(0, end - time.monotonic()))):
                s.event('focus-change')
                return None
            if not seat.intact():
                s.event('seat-lost')
                return None
            if time.monotonic() >= end:
                return None
        return records.commits.pop(0)

    if not (s.arm(*binding) and s.seat_locked()):
        out['revokeReason'] = s.revoke_reason
        return out
    if pending_receipt() is not None:
        s.revoke('commit-mismatch')
    script = inj.script(case)
    for command in script:
        if s.state == 'REVOKED':
            break
        if command == 'backspace' and not s._receipt_ok:
            record = pending_receipt(2)
            time.sleep(.1)
            extra = pending_receipt()
            out['receiptMatched'] = extra is None and s.commit_receipt(record)
            if not out['receiptMatched']:
                s.revoke('commit-mismatch')  # never clean up blindly
                break
        token = s.issue(command, *current())
        if token is None or not s.send(token, command, *current(), write):
            break
        clients.helper_ack(base, helper)
        time.sleep(.05)
    out['sent'] = s.sent
    out['completed'] = s.done() and s.sent == len(script) and out['receiptMatched'] and s.state != 'REVOKED'
    out['noReceiptAfterCleanup'] = pending_receipt(.3) is None
    out['completed'] = out['completed'] and out['noReceiptAfterCleanup']
    out['revokeReason'] = s.revoke_reason
    # Revoked after typing: the composer may hold 'ghbdtn' or the committed text; never sent (no Enter).
    out['textMayRemain'] = s.sent > 0 and not out['completed']
    return out


def run_live_inside(clients, base, fcitx, gate, children):
    """Live session in the owner's test channel. The seat is locked before Slack starts;
    the owner uses only the mouse to open the channel and the composer, then F10."""
    if (os.environ.get('RASKLAD_SLACK_LIVE_LAB') != '1' or os.environ.get('RASKLAD_SLACK_SUPERVISED_LAB')
            or os.environ.get('RASKLAD_SLACK_LAUNCH_LAB')):
        raise base.Refused('slack_live_environment')
    read_end, write_end, receipts, addon_end = gate
    addon_end.close()
    os.close(read_end)
    sup = clients.load_lab_module('slack_supervisor_live', Path('/lab/slack_supervisor.py'))
    report = {'slackLive': True, 'textRead': False, 'enterNeverSent': 'enter' not in sup.inj.WIRE}
    arm = [write_end]
    helper = slack = relay = seat = None
    released = None

    def latch():
        if arm[0] is not None:
            try:
                os.write(arm[0], b'x')
            except OSError:
                pass
            os.close(arm[0])
            arm[0] = None

    def stop_helper():
        if helper is not None:
            if not helper.stdin.closed:
                helper.stdin.close()
            if helper.poll() is None:
                helper.terminate()
                try:
                    helper.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    helper.kill()
                    helper.wait()

    def revoke_all():
        # Order: exemption off -> helper gone -> addon arm latched -> submap off. Each step runs
        # even if an earlier one raised; the exemption is retried before the helper goes.
        for step in ('exemption', 'helper', 'arm', 'submap'):
            try:
                if step == 'exemption' and seat:
                    for _ in range(3):
                        if seat.release_exemption():
                            break
                elif step == 'helper':
                    stop_helper()
                elif step == 'arm':
                    latch()
                elif step == 'submap' and seat:
                    seat.unlock()
            except Exception:
                report.setdefault('revokeErrors', []).append(step)

    def write(data):
        _, writable, _ = select.select([], [helper.stdin], [], 1)
        if not writable:
            raise OSError('helper stdin full')
        if os.write(helper.stdin.fileno(), data) != len(data):
            raise OSError('short helper write')

    try:
        records = clients.AddonRecords(base, receipts)
        helper = subprocess.Popen(['/lab/rasklad-wayland-keys'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL)
        children.append(helper)
        ready, _, _ = select.select([helper.stdout], [], [], 3)
        if not ready or os.read(helper.stdout.fileno(), 32) != b'{"ready":true}\n':
            raise base.Refused('helper_ready')
        os.set_blocking(helper.stdin.fileno(), False)
        seat = clients.HyprSeat(base)
        report['seatLockedBeforeSlack'] = seat.lock()
        if not report['seatLockedBeforeSlack']:
            raise base.Refused('seat_lock')
        env, relay, argv = start_slack(os.environ, LIVE_RELAY)
        started = time.monotonic()
        slack = subprocess.Popen(argv, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, cwd=env['HOME'])
        children.append(slack)
        signal_at = first_mapped = None
        while time.monotonic() - started < LIVE_WAIT_SECONDS:
            if slack.poll() is not None or fcitx.poll() is not None or helper.poll() is not None:
                raise base.Refused('process_exit_before_arm')
            records.poll(.2)
            if records.commits:
                raise base.Refused('commit_before_arm')
            submap = clients.hyprctl(base, 'submap', raw=True)
            if submap == 'rasklad_go':
                signal_at = round(time.monotonic() - started, 1)
                break
            now_keyboards = seat.keyboards()
            if submap == 'rasklad_lock' and now_keyboards != seat.devices:
                before = {n for n, _ in seat.devices}
                if seat.accept_additions(now_keyboards):
                    added = sorted({n for n, _ in now_keyboards} - before)
                    report.setdefault('keyboardsAddedBeforeArm', [])
                    report['keyboardsAddedBeforeArm'] = sorted(set(report['keyboardsAddedBeforeArm']) | set(added))[:4]
            if submap != 'rasklad_lock' or now_keyboards != seat.devices:
                # Diagnostics: fixed submap names and keyboard device names only (no text, no addresses).
                known = ('rasklad_lock', 'rasklad_go', 'rasklad_dead', 'default', '')
                report['seatLostDetail'] = {
                    'submap': submap if submap in known else 'other',
                    'added': sorted({n for n, _ in now_keyboards} - {n for n, _ in seat.devices}
                                    | {n for n, a in now_keyboards if (n, a) not in seat.devices})[:4],
                    'removed': sorted({n for n, a in seat.devices if (n, a) not in now_keyboards})[:4],
                    'seconds': round(time.monotonic() - started, 1)}
                raise base.Refused('seat_lost_before_arm')
            seen = windows(clients, base, slack.pid)
            if seen and seen[0] and first_mapped is None:
                first_mapped = round(time.monotonic() - started, 1)
            if seen and seen[1]:
                raise base.Refused('foreign_window')
        report.update(windowMappedSeconds=first_mapped, ownerSignalSeconds=signal_at,
                      contextRecordsBeforeArm=len(records.contexts))
        if signal_at is None:
            raise base.Refused('owner_signal_timeout')
        seat.submap = 'rasklad_go'
        time.sleep(.3)
        records.poll(.2)
        last = records.contexts[-1] if records.contexts else b''
        focused = clients.compositor_binding(base)
        report['bindingFromCompositor'] = bool(focused) and clients.descends_from(focused[0], slack.pid)
        report['contextFromAddon'] = last[:1] == b'i' and clients.PURPOSES.get(last[1:2]) in ('normal', 'alpha')
        report['seatIntactAtArm'] = seat.intact()
        if not (report['bindingFromCompositor'] and report['contextFromAddon'] and report['seatIntactAtArm']):
            raise base.Refused('arm_binding')
        binding = (focused[0], focused[1], int(last[2:5]), clients.PURPOSES[last[1:2]])
        report['cases'] = []
        for sequence, case in enumerate(LIVE_CASES, 1):
            result = run_case(clients, base, sup, case, sequence, binding, records, seat, helper, write)
            report['cases'].append(result)
            if not result.get('completed'):
                break
            time.sleep(.5)
        report['helperStdinOnlyInSupervisor'] = not clients.write_end_holders(helper.stdin.fileno())
        report['armWriteEndOnlyInSupervisor'] = not clients.write_end_holders(arm[0])
        revoke_all()
        report['seatReleasedAfterRevoke'] = (clients.hyprctl(base, 'submap', raw=True) not in ('rasklad_lock', 'rasklad_go')
                                             and not seat.exempt)
        time.sleep(LIVE_LOOK_SECONDS)
        report['commitRecordsAfterRevoke'] = len(records.commits)
        report['slackStop'] = stop(slack, QUIT_SECONDS)
        released = report['singletonRelease'] = release_own_singleton(Path(env['HOME'], '.config/Slack'),
                                                                      socket.gethostname(), slack.pid)
        report['relayRefused'] = relay.refused
        report['casesCompleted'] = [c['case'] for c in report['cases'] if c.get('completed')]
        report['textMayRemain'] = any(c.get('textMayRemain') for c in report['cases'])
        report['passed'] = (report['casesCompleted'] == list(LIVE_CASES) and report['helperStdinOnlyInSupervisor']
                            and report['armWriteEndOnlyInSupervisor'] and report['seatReleasedAfterRevoke']
                            and report['commitRecordsAfterRevoke'] == 0 and report['slackStop'] == 'graceful'
                            and released in ('released', 'absent') and report['relayRefused'] == 0
                            and not report.get('revokeErrors'))
        return report
    except base.Refused as error:
        # Keep the fixed-shape partial report (cases, diagnostics) instead of losing it upstream.
        report.update(passed=False, reason=str(error))
        return report
    finally:
        revoke_all()
        if slack is not None:
            if slack.poll() is None:
                stop(slack, QUIT_SECONDS)
            if released is None and slack.poll() is not None:
                release_own_singleton(Path(os.environ.get('RASKLAD_SLACK_HOME', '/nonexistent'), '.config/Slack'),
                                      socket.gethostname(), slack.pid)
        if relay is not None:
            relay.close()
        receipts.close()
