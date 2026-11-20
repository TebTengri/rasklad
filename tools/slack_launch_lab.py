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
import json, os, re, resource, signal, socket, subprocess, time
from pathlib import Path

INSIDE = Path(__file__) == Path('/lab/slack_launch_lab.py')
ROOT = Path('/lab') if INSIDE else Path(__file__).resolve().parents[1]
SLACK = '/usr/lib/slack/slack'
ADDON = ROOT / 'build/chromium-ime/slack-session-addon/raskladslacklab.so'
ADDON_CONF = ROOT / 'tests/slack_session/raskladslacklab-nested.conf'
OBSERVE_SECONDS = 90   # boot + the owner's zero-injection composer→search→composer check
QUIT_SECONDS = 15      # graceful SIGTERM before SIGKILL (profile flush)
OUTER_SECONDS = 150    # Hyprland/Fcitx start + observe 90 + quit 15 + margins
RELAY = dict(seconds=115, max_active=16)
BROKER = dict(seconds=170,  # created before Hyprland/Fcitx start; must outlive observe + quit
              max_requests=384, max_bytes=256 * 1024 * 1024, max_active=16)
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
    ctx['broker'] = bridge.Broker(directory / 'https.sock', online=True, allow_ech=True, **BROKER)
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
    for key, value in (('RASKLAD_SLACK_SESSION_LAB', '1'), ('RASKLAD_SLACK_GATE_LAB', '1'), ('RASKLAD_SLACK_LAUNCH_LAB', '1'),
                       ('RASKLAD_SLACK_HOME', home), ('RASKLAD_SLACK_USER', pre['user']), ('RASKLAD_SLACK_LANG', pre['lang'])):
        extra += ['--setenv', key, value]
    return extra


def limits():
    # Step-0 limits: Slack needs more CPU seconds and larger profile files than the Chromium fixture.
    os.nice(10)
    for kind, value in [(resource.RLIMIT_CPU, 120), (resource.RLIMIT_FSIZE, 512 << 20), (resource.RLIMIT_CORE, 0), (resource.RLIMIT_NOFILE, 4096)]:
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
