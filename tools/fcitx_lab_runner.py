#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""SYS-244: genuine Fcitx/Qt IM roundtrip, in a private no-seat namespace.

Default is a read-only plan. --build compiles project-authored fixture and addon.
--run admits only fixed binaries and synthetic test input; no host UI connection.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import selectors
import stat
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
TIMEOUT = 30
PROFILE = """[Groups/0]
Name=Default
Default Layout=us
DefaultIM=keyboard-us

[Groups/0/Items/0]
Name=keyboard-us
Layout=

[GroupOrder]
0=Default
"""
ALLOW_ADDONS = "keyboard,dbus,dbusfrontend,raskladlab"


class Refused(Exception):
    pass


def fixed_files(root=ROOT):
    return {
        "runner": root / "tools/fcitx_lab_runner.py",
        "client": root / "build/fcitx_lab/client/rasklad-ime-lab",
        "addon": root / "build/fcitx_lab/raskladlab.so",
        "addon_conf": root / "tests/fcitx_transport/raskladlab.conf",
        "bus_conf": root / "tests/fcitx_transport/session-bus.conf",
    }


def regular_file(path, executable=False):
    # No component may redirect the fixed artifact selection elsewhere.
    for part in [path, *path.parents]:
        if part.is_symlink():
            raise Refused("symlink")
    try:
        meta = path.stat()
    except OSError as exc:
        raise Refused("missing_artifact") from exc
    if not stat.S_ISREG(meta.st_mode) or meta.st_mode & 0o022:
        raise Refused("unsafe_artifact")
    if meta.st_uid not in (0, os.getuid()) or (executable and not meta.st_mode & 0o111):
        raise Refused("unsafe_artifact")


def sealed_snapshot(path, expected_sha256=None, max_bytes=5_000_000):
    """Immutable executable bytes; file replacement after checking cannot change them."""
    regular_file(path)
    source = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        meta = os.fstat(source)
        if not stat.S_ISREG(meta.st_mode) or meta.st_size > max_bytes:
            raise Refused("snapshot_size")
        chunks=[]; total=0
        while True:
            chunk=os.read(source,min(65536,max_bytes+1-total))
            if not chunk:break
            chunks.append(chunk);total+=len(chunk)
            if total>max_bytes:raise Refused("snapshot_size")
        payload=b"".join(chunks)
    finally:
        os.close(source)
    if expected_sha256 is not None and hashlib.sha256(payload).hexdigest()!=expected_sha256:
        raise Refused("snapshot_pin")
    flags=os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING | getattr(os,"MFD_EXEC",0)
    fd=os.memfd_create("rasklad-lab-artifact",flags)
    try:
        view=memoryview(payload)
        while view:
            count=os.write(fd,view);view=view[count:]
        os.fchmod(fd,0o755)
        fcntl.fcntl(fd,fcntl.F_ADD_SEALS,fcntl.F_SEAL_WRITE|fcntl.F_SEAL_GROW|fcntl.F_SEAL_SHRINK|fcntl.F_SEAL_SEAL)
        os.lseek(fd,0,os.SEEK_SET)
        return fd
    except BaseException:
        os.close(fd);raise

def sandbox_argv(files, identity_files=None):
    argv = ["/usr/bin/bwrap", "--unshare-all", "--die-with-parent", "--new-session", "--cap-drop", "ALL", "--clearenv",
        "--ro-bind", "/usr", "/usr", "--symlink", "usr/bin", "/bin", "--symlink", "usr/lib", "/lib",
        "--symlink", "usr/lib", "/lib64", "--ro-bind-try", "/etc/ld.so.cache", "/etc/ld.so.cache",
        "--ro-bind-try", "/etc/fonts", "/etc/fonts", "--proc", "/proc", "--dev", "/dev",
        "--tmpfs", "/tmp", "--tmpfs", "/run", "--tmpfs", "/home", "--dir", "/run/lab",
        "--dir", "/home/lab", "--dir", "/lab/addons", "--chdir", "/home/lab"]
    targets = {"runner":"/lab/runner.py", "client":"/lab/rasklad-ime-lab", "addon":"/lab/addons/raskladlab.so",
        "addon_conf":"/lab/addon.conf", "bus_conf":"/lab/session-bus.conf"}
    for name, path in files.items():
        argv += ["--ro-bind", str(path), targets[name]]
    if identity_files:
        for name in ("passwd","group","machine-id"):
            argv += ["--ro-bind",str(identity_files[name]),"/etc/"+name]
    env = {
        "PATH":"/usr/bin", "LANG":"C.UTF-8", "HOME":"/home/lab", "XDG_RUNTIME_DIR":"/run/lab",
        "XDG_CONFIG_HOME":"/home/lab/.config", "XDG_DATA_HOME":"/home/lab/.local/share",
        "XDG_CACHE_HOME":"/home/lab/.cache", "XDG_DATA_DIRS":"/usr/share",
        "FCITX_ADDON_DIRS":"/lab/addons:/usr/lib/fcitx5", "RASKLAD_PRIVATE_LAB":"1",
        "QT_QPA_PLATFORM":"offscreen", "QT_QPA_OFFSCREEN_NO_GLX":"1", "QT_IM_MODULE":"fcitx",
        "FCITX_QT_USE_SYNC":"1", "FCITX_QT_ENABLE_SURROUNDING_TEXT":"0",
        "QT_ACCESSIBILITY":"0", "QT_LINUX_ACCESSIBILITY_ALWAYS_ON":"0", "NO_AT_BRIDGE":"1",
        "QT_FORCE_STDERR_LOGGING":"1", "QT_LOGGING_RULES":"*.debug=false;*.info=false;*.warning=false",
    }
    for name, value in env.items():
        argv += ["--setenv", name, value]
    return argv + ["--", "/usr/bin/dbus-run-session", "--config-file", "/lab/session-bus.conf",
        "--", "/usr/bin/python3", "-I", "/lab/runner.py", "--inside"]


def limits():
    os.nice(10)
    for kind, amount in ((resource.RLIMIT_CPU,25),(resource.RLIMIT_AS,1<<30),
        (resource.RLIMIT_NOFILE,256),(resource.RLIMIT_CORE,0),(resource.RLIMIT_FSIZE,1<<20)):
        resource.setrlimit(kind,(amount,amount))


def ensure_private_base(root=ROOT):
    path = root
    # ROOT is fixed by __file__; refuse unsafe project build directories.
    for name in ("build", "fcitx_lab", "runs"):
        path /= name
        try:
            path.mkdir(mode=0o700)
        except FileExistsError:
            pass
        meta = path.lstat()
        if not stat.S_ISDIR(meta.st_mode) or meta.st_uid != os.getuid() or meta.st_mode & 0o022:
            raise Refused("unsafe_build_directory")
    return path


def build():
    ensure_private_base()
    plans = [(ROOT/"build/fcitx_lab", ROOT/"tools/fcitx_lab.pro"),
             (ROOT/"build/fcitx_lab/client", ROOT/"tests/fcitx_transport/client.pro")]
    for directory, source in plans:
        regular_file(source)
        directory.mkdir(mode=0o700, exist_ok=True)
        if directory.is_symlink():
            raise Refused("unsafe_build_directory")
        for argv in (["/usr/bin/qmake6",str(source)], ["/usr/bin/make","-j2"]):
            result = subprocess.run(argv,cwd=directory,stdin=subprocess.DEVNULL,timeout=90,
                stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=False)
            if result.returncode:
                raise Refused("build_failed")
    return {"schema":1,"built":True,"hostConfigurationChanged":False}


def run():
    if os.geteuid() == 0:
        raise Refused("root_not_admitted")
    files = fixed_files()
    for name, path in files.items():
        regular_file(path, name == "client")
    # bwrap is required; there is deliberately no unsandboxed fallback.
    meta = os.lstat("/usr/bin/bwrap")
    if not stat.S_ISREG(meta.st_mode) or meta.st_uid or meta.st_mode & (0o022|stat.S_ISUID|stat.S_ISGID):
        raise Refused("unsafe_bwrap")
    base = ensure_private_base()
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=base))
    proc = None
    try:
        identity_files={"passwd":directory/"passwd","group":directory/"group","machine-id":directory/"machine-id"}
        identity_files["passwd"].write_text(f"lab:x:{os.getuid()}:{os.getgid()}:Isolated test:/home/lab:/usr/bin/nologin\n")
        identity_files["group"].write_text(f"lab:x:{os.getgid()}:\n")
        identity_files["machine-id"].write_text("00000000000000000000000000000001\n")
        with (directory/"stdout").open("w+b") as out:
            proc = subprocess.Popen(sandbox_argv(files,identity_files),stdin=subprocess.DEVNULL,stdout=out,
                stderr=subprocess.DEVNULL,env={"PATH":"/usr/bin"},start_new_session=True,preexec_fn=limits)
            try:
                rc = proc.wait(timeout=TIMEOUT)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid,signal.SIGTERM)
                try: proc.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid,signal.SIGKILL); proc.wait(timeout=2)
                return {"schema":1,"passed":False,"status":"timeout","textRead":False,"hostInput":False}
            out.seek(0); raw=out.read(16385)
            if len(raw)>16384:
                raise Refused("output_limit")
            try:
                report=json.loads(raw)
            except ValueError:
                return {"schema":1,"passed":False,"status":"sandbox_or_client_failed","exitCode":rc,
                    "hostInput":False,"hostConfigurationChanged":False}
            if not isinstance(report,dict) or report.get("schema") != 1:
                raise Refused("invalid_report")
            report.update({"sandboxed":True,"hostInput":False,"hostConfigurationChanged":False,
                "realApplicationSupport":False,"exitCode":rc})
            report["passed"] = rc == 0 and report.get("passed") is True
            return report
    finally:
        # Only this process's private namespace and temporary directory are owned.
        if proc is not None and proc.poll() is None:
            os.killpg(proc.pid,signal.SIGKILL); proc.wait(timeout=2)
        shutil.rmtree(directory)


def daemon_resources(pid):
    # Only our own child in this private PID namespace is sampled.
    fields=Path(f"/proc/{pid}/stat").read_text().rsplit(")",1)[1].split()
    cpu=(int(fields[11])+int(fields[12]))/os.sysconf("SC_CLK_TCK")
    memory={}
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        if line.startswith(("VmRSS:","VmHWM:")):
            key,value=line.split(":",1);memory[key]=int(value.split()[0])
    return {"daemonCpuSeconds":cpu,"daemonRssKiB":memory.get("VmRSS"),"daemonHighWaterKiB":memory.get("VmHWM"),
        "resourceScope":"own private daemon sampled before synthetic mid-preedit crash; not idle baseline"}


def inside():
    required={"RASKLAD_PRIVATE_LAB":"1","HOME":"/home/lab","XDG_RUNTIME_DIR":"/run/lab"}
    if any(os.environ.get(k) != v for k,v in required.items()) or os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
        raise Refused("not_private_lab")
    if not os.environ.get("DBUS_SESSION_BUS_ADDRESS","").startswith("unix:"):
        raise Refused("missing_private_bus")
    os.chmod("/run/lab",0o700)
    config=Path("/home/lab/.config/fcitx5"); config.mkdir(parents=True)
    config.joinpath("profile").write_text(PROFILE)
    data=Path("/home/lab/.local/share/fcitx5/addon"); data.mkdir(parents=True)
    data.joinpath("raskladlab.conf").write_bytes(Path("/lab/addon.conf").read_bytes())
    daemon=subprocess.Popen(["/usr/bin/fcitx5","-D","-k","--disable","all","--enable",ALLOW_ADDONS,"--verbose","*=0"],
        stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        ready=False
        for _ in range(50):
            if daemon.poll() is not None:break
            probe=subprocess.run(["/usr/bin/gdbus","call","--session","--dest","org.freedesktop.DBus",
                "--object-path","/org/freedesktop/DBus","--method","org.freedesktop.DBus.NameHasOwner","org.fcitx.Fcitx5"],
                stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=1)
            if probe.stdout == b"(true,)\n":ready=True;break
            time.sleep(0.05)
        if not ready:
            return {"schema":1,"passed":False,"status":"fcitx_not_ready","realApplicationSupport":False}
        client=subprocess.Popen(["/lab/rasklad-ime-lab"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(client.stdout,selectors.EVENT_READ)
                if not selector.select(timeout=18):raise Refused("client_handshake_timeout")
                first=client.stdout.readline(1024)
            if not first:
                return {"schema":1,"passed":False,"status":"client_exited_before_handshake",
                    "clientExitCode":client.wait(timeout=1),"fcitxDaemonReady":True}
            if first != b'{"phase":"ready_for_daemon_crash"}\n':
                try: early=json.loads(first)
                except ValueError: raise Refused("client_handshake_failed")
                if not isinstance(early,dict) or early.get("schema") != 1 or early.get("passed") is not False:
                    raise Refused("client_handshake_failed")
                early["clientExitCode"]=client.wait(timeout=1)
                early["fcitxDaemonReady"]=True
                return early
            resources=daemon_resources(daemon.pid)
            daemon.kill();daemon.wait(timeout=1)
            raw,_=client.communicate(input=b"1\n",timeout=4)
            if len(raw)>16384:raise Refused("output_limit")
            report=json.loads(raw)
            report.update(resources)
        finally:
            if client.poll() is None:
                client.kill();client.wait(timeout=1)
        required=("inputContextIsFcitx","focusAccepted","wordHeldBeforeSpace","preeditObserved","correctedOnce","oneCommit","nextCharacterInPreedit","nextWordPreserved","backspaceInPreedit","mixedAndLongTokensPreserved","capabilityLossClearsPending","focusSwitchDoesNotLeakWord","passwordPassthrough","preeditHeldAtCrash","postCrashInputContinues")
        if not isinstance(report,dict) or report.get("schema") != 1 or any(type(report.get(k)) is not bool for k in required):
            raise Refused("invalid_client_report")
        report["clientExitCode"]=client.returncode
        report["passed"]=client.returncode == 0 and report.get("passed") is True and all(report[k] for k in required)
        report["fcitxDaemonReady"]=True
        return report
    finally:
        if daemon.poll() is None:
            daemon.terminate()
            try:daemon.wait(timeout=1)
            except subprocess.TimeoutExpired:daemon.kill();daemon.wait(timeout=1)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    actions=parser.add_mutually_exclusive_group()
    actions.add_argument("--run",action="store_true")
    actions.add_argument("--build",action="store_true")
    actions.add_argument("--inside",action="store_true",help=argparse.SUPPRESS)
    args=parser.parse_args()
    try:
        if args.inside: report=inside()
        elif args.build:report=build()
        elif args.run:report=run()
        else:report={"schema":1,"status":"plan_only","tested":False,"syntheticInputOnly":True,
            "hostInput":False,"realApplicationSupport":False,"addonAllowlist":ALLOW_ADDONS.split(","),
            "isolation":"bwrap --unshare-all; private HOME/runtime/session bus; offscreen Qt; no host display/devices/network",
            "actions":["--build compiles addon and client","--run executes isolated roundtrip"]}
        print(json.dumps(report,sort_keys=True))
        return 0 if report.get("passed",True) else 1
    except (Refused,OSError,ValueError,subprocess.SubprocessError) as exc:
        print(json.dumps({"schema":1,"passed":False,"status":"refused","reason":str(exc) if isinstance(exc,Refused) else type(exc).__name__}))
        return 2


if __name__ == "__main__":
    sys.exit(main())
